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

    // Initialize FFT and window (audiodev.blog pattern)
    // Window is fftSize + 1 to make it periodic (not symmetric)
    fft = std::make_unique<juce::dsp::FFT>(fftOrder);
    window = std::make_unique<juce::dsp::WindowingFunction<float>>(
        fftSize + 1,
        juce::dsp::WindowingFunction<float>::hann,
        false  // normalise = false (we apply our own correction)
    );

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

    // Phase 3: Update oscillator bank from tracked partials
    // SOURCE: Custom logic using JUCE patterns
    // Replaces IFFT reconstruction - oscillators generate audio directly
    const auto& activeTracks = partialTracker.getActiveTracks();
    oscillatorBank.updateFromPartials(activeTracks);

    // NOTE: IFFT and overlap-add removed - now using oscillator bank synthesis
}

void PanharmoniumEngine::spectralManipulation(float* fftDataBuffer)
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

    // NOTE: Spectral effects (BLUR, FEEDBACK, WARP) will be implemented in Phase 5
    // They will modify the partial tracks before oscillator bank synthesis
    // RESONANCE removed per user request (keeping COLOR and FLOAT only)
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

void PanharmoniumEngine::setTime(float value)
{
    currentTime.store(juce::jlimit(0.0f, 1.0f, value));
    // Note: TIME changes FFT size, would require full reinitialization
    // For simplicity, this demo uses fixed fftSize
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

void PanharmoniumEngine::setVoices(float value)
{
    currentVoices.store(juce::jlimit(0.0f, 1.0f, value));
    // Placeholder - could control polyphonic processing
}
