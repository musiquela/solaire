#include "PanharmoniumEngine.h"

PanharmoniumEngine::PanharmoniumEngine()
{
    // Constructor - FFT initialization (audiodev.blog pattern)
    // Note: Actual allocation happens in prepareToPlay to avoid race conditions
}

void PanharmoniumEngine::prepareToPlay(double newSampleRate, int samplesPerBlock)
{
    // CRITICAL: SpinLock guard for prepareToPlay/processBlock race condition
    // See .claude/juce_critical_knowledge.md - Nuendo can call this during processing
    const juce::SpinLock::ScopedLockType lock(processingLock);

    sampleRate = newSampleRate;

    // PHASE 4: Initialize dynamic FFT size (default 1024)
    // SOURCE: JUCE dsp::Convolution pattern - initialize FFT in prepareToPlay
    updateFFTSize(fftOrder);

    // Prepare output effects and oscillator bank (juce::dsp pattern)
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
    spec.numChannels = 1;  // Mono processing

    lowShelf.prepare(spec);
    highShelf.prepare(spec);
    oscillatorBank.prepare(spec);  // Phase 3: Prepare oscillator bank

    // Initialize reverb
    reverb.reset();
    juce::Reverb::Parameters reverbParams;
    reverbParams.roomSize = 0.5f;
    reverbParams.damping = 0.5f;
    reverbParams.wetLevel = 0.0f;
    reverbParams.dryLevel = 1.0f;
    reverbParams.width = 1.0f;
    reverb.setParameters(reverbParams);

    reset();
}

void PanharmoniumEngine::releaseResources()
{
    const juce::SpinLock::ScopedLockType lock(processingLock);
    // Resources released via unique_ptr destructors
}

void PanharmoniumEngine::updateFFTSize(int newOrder)
{
    // PHASE 4: Dynamic FFT size update (SLICE parameter)
    // SOURCE: JUCE dsp::Convolution pattern - reset unique_ptr to change FFT size
    // SOURCE: JUCE forum (forum.juce.com/t/29348) - IvanC: thread-safe FFT reset
    // NOTE: Must be called with processingLock held!

    fftOrder = newOrder;
    fftSize = 1 << fftOrder;
    numBins = fftSize / 2 + 1;
    hopSize = fftSize / overlap;

    // Reset FFT and window (JUCE dsp::Convolution pattern)
    // Window is fftSize + 1 to make it periodic (not symmetric)
    fft = std::make_unique<juce::dsp::FFT>(fftOrder);
    window = std::make_unique<juce::dsp::WindowingFunction<float>>(
        fftSize + 1,
        juce::dsp::WindowingFunction<float>::hann,
        false  // normalise = false (we apply our own correction)
    );

    // Resize all dynamic buffers (std::vector pattern)
    inputFifo.resize(fftSize, 0.0f);
    outputFifo.resize(fftSize, 0.0f);
    fftData.resize(fftSize * 2, 0.0f);  // Interleaved complex
    prevMagnitude.resize(numBins, 0.0f);
    prevPhase.resize(numBins, 0.0f);
    feedbackMagnitude.resize(numBins, 0.0f);
    dryBuffer.resize(fftSize, 0.0f);

    // Reset positions
    fifoPos = 0;
    hopCount = 0;
    dryBufferPos = 0;
}

void PanharmoniumEngine::reset()
{
    // Zero out FIFOs and state arrays (audiodev.blog pattern)
    fifoPos = 0;
    hopCount = 0;

    std::fill(inputFifo.begin(), inputFifo.end(), 0.0f);
    std::fill(outputFifo.begin(), outputFifo.end(), 0.0f);
    std::fill(prevMagnitude.begin(), prevMagnitude.end(), 0.0f);
    std::fill(prevPhase.begin(), prevPhase.end(), 0.0f);
    std::fill(feedbackMagnitude.begin(), feedbackMagnitude.end(), 0.0f);
    std::fill(dryBuffer.begin(), dryBuffer.end(), 0.0f);

    dryBufferPos = 0;

    // Reset oscillator bank (Phase 3)
    oscillatorBank.reset();

    // PHASE 5: Clear spectral modifier state
    prevPartialAmplitudes.clear();
    feedbackAmplitudes.clear();
}

float PanharmoniumEngine::processSample(float inputSample)
{
    // CRITICAL: TryLock - skip processing if prepareToPlay is running
    const juce::SpinLock::ScopedTryLockType lock(processingLock);
    if (!lock.isLocked())
        return inputSample;  // Bypass if preparing

    // Store input sample in FIFO (audiodev.blog pattern)
    inputFifo[fifoPos] = inputSample;

    // Store dry signal for mix
    dryBuffer[dryBufferPos] = inputSample;
    dryBufferPos = (dryBufferPos + 1) % fftSize;

    // Phase 3: Generate output from oscillator bank (replaces IFFT reconstruction)
    // SOURCE: JUCE DSP Tutorial - continuous sample generation from oscillators
    float outputSample = oscillatorBank.processSample();

    // Advance FIFO position (circular)
    fifoPos = (fifoPos + 1) % fftSize;

    // Process FFT frame every hopSize samples (for spectral analysis only)
    hopCount++;
    if (hopCount >= hopSize)
    {
        hopCount = 0;
        processFrame();
    }

    // Apply output effects
    applyOutputEffects(outputSample);

    return outputSample;
}

void PanharmoniumEngine::processFrame()
{
    // audiodev.blog STFT pattern: Copy FIFO to FFT buffer
    // Handle circular buffer wrap-around in two parts
    float* fftPtr = fftData.data();
    const float* inputPtr = inputFifo.data();

    // Copy from fifoPos to end of FIFO
    std::memcpy(fftPtr, inputPtr + fifoPos, (fftSize - fifoPos) * sizeof(float));

    // Copy from start of FIFO to fifoPos (if wrapped)
    if (fifoPos > 0)
    {
        std::memcpy(fftPtr + fftSize - fifoPos, inputPtr, fifoPos * sizeof(float));
    }

    // Apply Hann window before FFT (audiodev.blog pattern)
    window->multiplyWithWindowingTable(fftPtr, fftSize);

    // Perform FFT (juce::dsp pattern)
    fft->performRealOnlyForwardTransform(fftPtr, true);

    // SPECTRAL ANALYSIS (Phase 1-2: peak extraction & tracking)
    spectralManipulation(fftPtr);

    // Phase 3, 4 & 5: Update oscillator bank from tracked partials
    // SOURCE: Custom logic using JUCE patterns
    // Replaces IFFT reconstruction - oscillators generate audio directly
    auto activeTracks = partialTracker.getActiveTracks();  // Copy for modification

    // PHASE 5: Apply spectral modifiers to partial tracks
    applySpectralModifiers(activeTracks);

    // PHASE 4: VOICE parameter - limit active oscillators
    // SOURCE: Simple loop control (standard C++ pattern)
    const float voiceParam = currentVoice.load();
    const int maxVoices = static_cast<int>(voiceParam * 32.0f) + 1;  // 1-33 range
    oscillatorBank.updateFromPartials(activeTracks, maxVoices);

    // NOTE: IFFT and overlap-add removed - now using oscillator bank synthesis
}

void PanharmoniumEngine::spectralManipulation(float* fftDataBuffer)
{
    // PHASE 4: FREEZE parameter - gate spectral analysis
    // SOURCE: Simple boolean gate pattern (standard DSP technique)
    const float freeze = currentFreeze.load();
    const bool isFrozen = (freeze > 0.5f);

    if (!isFrozen)
    {
        // PHASE 1: Extract dominant spectral peaks (Panharmonium resynthesis)
        // SOURCE: audiodev.blog FFT tutorial + DSPRelated quadratic interpolation
        // Extract 33 dominant peaks for oscillator bank resynthesis
        currentPeaks = extractDominantPeaks(
            fftDataBuffer,
            numBins,
            maxSpectralPeaks,
            sampleRate,
            fftSize
        );

        // PHASE 2: Track peaks across frames (Panharmonium resynthesis)
        // SOURCE: McAulay-Quatieri algorithm - maintain peak identity over time
        // Enables stable oscillator frequency/amplitude trajectories
        partialTracker.processFrame(currentPeaks);
    }
    // When frozen, partials keep their last tracked values (oscillators continue)
}

void PanharmoniumEngine::applySpectralModifiers(std::vector<PartialTrack>& tracks)
{
    // PHASE 5 & 6: Apply spectral modifiers to partial tracks
    // SOURCE: Adapted from verified FFT bin processing patterns (Perplexity 95%+)

    // Load parameters (atomic read - thread-safe)
    const float blur = currentBlur.load();
    const float feedback = currentFeedback.load();
    const float warp = currentWarp.load();

    // PHASE 6: Frequency control parameters
    const float centerFreq = currentCenterFreq.load();
    const float bandwidth = currentBandwidth.load();
    const float freqShift = currentFreq.load();
    const float octaveShift = currentOctave.load();

    // Calculate frequency window bounds
    // SOURCE: Logarithmic frequency scaling (standard audio/DSP technique)
    const float MIN_FREQ = 20.0f;
    const float MAX_FREQ = 20000.0f;
    const float centerHz = MIN_FREQ * std::pow(MAX_FREQ / MIN_FREQ, centerFreq);

    // Bandwidth: 0 = narrow (±1 semitone), 1 = full spectrum
    const float bandwidthSemitones = 1.0f + bandwidth * 59.0f;  // 1 to 60 semitones
    const float bandwidthRatio = std::pow(2.0f, bandwidthSemitones / 12.0f);
    const float minFreq = centerHz / std::sqrt(bandwidthRatio);
    const float maxFreq = centerHz * std::sqrt(bandwidthRatio);

    // Calculate global frequency shift ratios
    // SOURCE: Standard pitch shift formula (verified in WARP)
    // FREQ: -100 to +100 cents (-1 to +1 semitones)
    const float centsShift = (freqShift - 0.5f) * 200.0f;  // Map 0-1 to -100 to +100 cents
    const float freqRatio = std::pow(2.0f, centsShift / 1200.0f);

    // OCTAVE: -2 to +2 octaves
    const float octaves = (octaveShift - 0.5f) * 4.0f;  // Map 0-1 to -2 to +2
    const float octaveRatio = std::pow(2.0f, octaves);

    for (auto& track : tracks)
    {
        if (!track.isActive)
            continue;

        const int trackID = track.trackID;

        // PHASE 6: Frequency window filtering (CENTER_FREQ + BANDWIDTH)
        // SOURCE: Simple range check (standard C++ conditional logic)
        // Deactivate partials outside the frequency window
        if (track.frequency < minFreq || track.frequency > maxFreq)
        {
            track.isActive = false;
            continue;  // Skip processing for out-of-range partials
        }

        // EFFECT 1: BLUR (Exponential Moving Average on amplitude)
        // SOURCE: Previously verified pattern (Perplexity 95%+)
        if (blur > 0.0f)
        {
            float alpha = 1.0f - blur;
            float prevAmp = prevPartialAmplitudes[trackID];
            track.amplitude = (1.0f - alpha) * prevAmp + alpha * track.amplitude;
        }

        // EFFECT 2: FEEDBACK (Spectral amplitude feedback with decay)
        // SOURCE: Previously verified pattern (Perplexity 95%+)
        if (feedback > 0.0f)
        {
            const float FEEDBACK_DECAY = 0.97f;
            feedbackAmplitudes[trackID] *= FEEDBACK_DECAY;
            float currentFeedback = feedbackAmplitudes[trackID];
            track.amplitude = track.amplitude * (1.0f - feedback) + currentFeedback * feedback;
        }

        // EFFECT 3: WARP (Frequency shift/scaling)
        // SOURCE: Standard pitch shift formula (verified in Phase 5)
        if (warp != 0.5f)
        {
            const float warpAmount = (warp - 0.5f) * 2.0f;
            const float warpRatio = std::pow(2.0f, warpAmount * 0.5f);  // ±6 semitones
            track.frequency *= warpRatio;
        }

        // PHASE 6: FREQ + OCTAVE (Global frequency transposition)
        // SOURCE: Standard pitch shift formula (multiply by frequency ratio)
        track.frequency *= freqRatio * octaveRatio;

        // Store state for next frame
        prevPartialAmplitudes[trackID] = track.amplitude;
        feedbackAmplitudes[trackID] = track.amplitude;
    }
}

void PanharmoniumEngine::applyOutputEffects(float& sample)
{
    // Load parameters
    const float colour = currentColour.load();
    const float floatParam = currentFloat.load();
    const float mix = currentMix.load();

    // Get dry sample for mix
    float drySample = dryBuffer[(dryBufferPos + fftSize - getLatencyInSamples()) % fftSize];

    // COLOR: Tilt EQ using complementary low/high shelves
    // Verification: First-order shelving filters with complementary gains
    lowShelf.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowShelf(
        sampleRate, 1000.0f, 0.707f, 1.0f - colour + 0.5f);
    highShelf.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighShelf(
        sampleRate, 1000.0f, 0.707f, colour + 0.5f);

    // Apply filters (single sample processing)
    float filtered = sample;
    filtered = lowShelf.processSample(filtered);
    filtered = highShelf.processSample(filtered);

    // FLOAT: Reverb
    // Verification: JUCE Reverb with decay time mapped to room size
    juce::Reverb::Parameters reverbParams;
    reverbParams.roomSize = floatParam;
    reverbParams.damping = 0.5f;
    reverbParams.wetLevel = floatParam;
    reverbParams.dryLevel = 1.0f - floatParam;
    reverbParams.width = 1.0f;
    reverb.setParameters(reverbParams);

    float reverbSample = filtered;
    reverb.processMono(&reverbSample, 1);
    filtered = reverbSample;

    // MIX: Dry/Wet blend
    // Verification: Linear crossfade formula
    sample = mix * filtered + (1.0f - mix) * drySample;
}

//==============================================================================
// Parameter setters

// PHASE 4: Core Panharmonium parameters
void PanharmoniumEngine::setSlice(float value)
{
    // SOURCE: Rossum Panharmonium - logarithmic SLICE control (17ms - 6400ms)
    // Convert 0-1 to logarithmic FFT size range
    value = juce::jlimit(0.0f, 1.0f, value);
    currentSlice.store(value);

    // Convert to milliseconds logarithmically
    // SOURCE: Standard logarithmic pot scaling (audiodev.blog)
    const float sliceMs = MIN_SLICE_MS * std::pow(MAX_SLICE_MS / MIN_SLICE_MS, value);

    // Convert milliseconds to samples
    const float sliceSamples = (sliceMs / 1000.0f) * static_cast<float>(sampleRate);

    // Find nearest power of 2 for FFT order
    // SOURCE: JUCE FFT requirements - size must be power of 2
    int newOrder = static_cast<int>(std::round(std::log2(sliceSamples)));
    newOrder = juce::jlimit(7, 14, newOrder);  // 128 to 16384 samples

    // Update FFT size if changed (thread-safe with SpinLock)
    if (newOrder != fftOrder)
    {
        const juce::SpinLock::ScopedLockType lock(processingLock);
        updateFFTSize(newOrder);
    }
}

void PanharmoniumEngine::setBlur(float value)
{
    currentBlur.store(juce::jlimit(0.0f, 1.0f, value));
}

void PanharmoniumEngine::setResonance(float value)
{
    currentResonance.store(juce::jlimit(0.0f, 1.0f, value));
}

void PanharmoniumEngine::setWarp(float value)
{
    currentWarp.store(juce::jlimit(0.0f, 1.0f, value));
}

void PanharmoniumEngine::setFeedback(float value)
{
    currentFeedback.store(juce::jlimit(0.0f, 1.0f, value));
}

void PanharmoniumEngine::setMix(float value)
{
    currentMix.store(juce::jlimit(0.0f, 1.0f, value));
}

void PanharmoniumEngine::setColour(float value)
{
    currentColour.store(juce::jlimit(0.0f, 1.0f, value));
}

void PanharmoniumEngine::setFloat(float value)
{
    currentFloat.store(juce::jlimit(0.0f, 1.0f, value));
}

void PanharmoniumEngine::setVoice(float value)
{
    // PHASE 4: VOICE parameter (1-33 active oscillators)
    // SOURCE: Simple atomic store (standard C++ pattern)
    currentVoice.store(juce::jlimit(0.0f, 1.0f, value));
}

void PanharmoniumEngine::setFreeze(float value)
{
    // PHASE 4: FREEZE parameter (spectral freeze on/off)
    // SOURCE: Boolean gate pattern (standard DSP technique)
    currentFreeze.store(juce::jlimit(0.0f, 1.0f, value));
}

// PHASE 6: Frequency control parameter setters
void PanharmoniumEngine::setCenterFreq(float value)
{
    // Center frequency of spectral window (20Hz - 20kHz, logarithmic)
    // SOURCE: Simple atomic store (standard C++ pattern)
    currentCenterFreq.store(juce::jlimit(0.0f, 1.0f, value));
}

void PanharmoniumEngine::setBandwidth(float value)
{
    // Bandwidth of spectral window (narrow to full spectrum)
    // SOURCE: Simple atomic store (standard C++ pattern)
    currentBandwidth.store(juce::jlimit(0.0f, 1.0f, value));
}

void PanharmoniumEngine::setFreq(float value)
{
    // Fine frequency shift (-100 to +100 cents)
    // SOURCE: Simple atomic store (standard C++ pattern)
    currentFreq.store(juce::jlimit(0.0f, 1.0f, value));
}

void PanharmoniumEngine::setOctave(float value)
{
    // Octave transposition (-2 to +2 octaves)
    // SOURCE: Simple atomic store (standard C++ pattern)
    currentOctave.store(juce::jlimit(0.0f, 1.0f, value));
}
