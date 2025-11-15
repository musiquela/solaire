# JUCE FFT/Spectral Processing - Production Patterns

## STFT (Short-Time Fourier Transform) Requirements

### Buffer Management
- **FFT size**: Must be power of 2 (512, 1024, 2048, 4096)
- **Overlap**: 75% with Hann window (hopSize = fftSize / 4)
- **Latency**: Always fftSize samples
- **FIFOs**: Need input FIFO and output FIFO (both fftSize length)

### Windowing
- **Hann window**: Apply TWICE (before FFT, after IFFT)
- **Must be periodic**, not symmetric (JUCE's is symmetric)
- **Workaround**: Create WindowingFunction with `fftSize + 1`, use only first `fftSize` samples
- **Window correction**: `2.0f / 3.0f` for Hann with 75% overlap

### JUCE FFT API
```cpp
juce::dsp::FFT fft{fftOrder}; // fftOrder = 10 means 1024 samples
juce::dsp::WindowingFunction<float> window{fftSize + 1,
    juce::dsp::WindowingFunction<float>::hann,
    false}; // normalise = false

// Forward
std::array<float, fftSize * 2> fftData; // Interleaved complex
window.multiplyWithWindowingTable(fftData.data(), fftSize);
fft.performRealOnlyForwardTransform(fftData.data(), true);

// Process spectrum (first fftSize/2 + 1 bins are valid)
auto* cdata = reinterpret_cast<std::complex<float>*>(fftData.data());
for (int i = 0; i < numBins; ++i) {
    float magnitude = std::abs(cdata[i]);
    float phase = std::arg(cdata[i]);
    // Process magnitude/phase
    cdata[i] = std::polar(magnitude, phase);
}

// Inverse
fft.performRealOnlyInverseTransform(fftData.data());
window.multiplyWithWindowingTable(fftData.data(), fftSize);
for (int i = 0; i < fftSize; ++i) {
    fftData[i] *= windowCorrection; // 2.0f/3.0f
}
```

### Overlap-Add Pattern
```cpp
int pos = 0; // Circular buffer position
int count = 0; // Hop counter
std::array<float, fftSize> inputFifo;
std::array<float, fftSize> outputFifo;

void processSample(float sample) {
    inputFifo[pos] = sample;
    float output = outputFifo[pos];
    outputFifo[pos] = 0.0f; // Clear for next overlap-add

    pos = (pos + 1) % fftSize;

    if (++count == hopSize) {
        count = 0;
        processFrame(); // FFT + spectral processing + IFFT + overlap-add
    }

    return output;
}

void processFrame() {
    // Copy FIFO to FFT buffer (handle wrap-around)
    // Apply window, FFT, process, IFFT, window again
    // Overlap-add result back to outputFifo
    for (int i = 0; i < fftSize; ++i) {
        outputFifo[(pos + i) % fftSize] += fftData[i];
    }
}
```

## Phase Vocoder Patterns

### Phase Unwrapping
```cpp
// Store previous phase
std::vector<float> prevPhase(numBins);

// In spectral processing
for (int i = 0; i < numBins; ++i) {
    float currentPhase = std::arg(cdata[i]);
    float phaseDelta = currentPhase - prevPhase[i];

    // Warp factor controls time-stretch
    float warpFactor = 1.0f + warpAmount; // 0.5 to 2.0
    float newPhase = currentPhase + phaseDelta * warpFactor;

    cdata[i] = std::polar(magnitude, newPhase);
    prevPhase[i] = currentPhase;
}
```

## Spectral Filters

### EMA Smoothing (Spectral Blur)
```cpp
std::vector<float> prevMagnitude(numBins);

for (int i = 0; i < numBins; ++i) {
    float alpha = blurAmount; // 0.0 to 1.0
    float smoothed = alpha * magnitude + (1.0f - alpha) * prevMagnitude[i];
    prevMagnitude[i] = smoothed;
}
```

### Spectral Resonance (Band-pass Peak)
```cpp
// Center frequency in Hz
float centerFreq = 500.0f;
int centerBin = (centerFreq / sampleRate) * fftSize;

float Q = 1.0f + resonanceAmount * 10.0f;

for (int i = 0; i < numBins; ++i) {
    float binFreq = (i * sampleRate) / fftSize;
    float distance = std::abs(binFreq - centerFreq);
    float gain = 1.0f / (1.0f + distance / (centerFreq / Q));
    magnitude *= gain;
}
```

## Filters (juce::dsp)

### Tilt EQ
```cpp
juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                juce::dsp::IIR::Coefficients<float>> lowShelf;
juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                juce::dsp::IIR::Coefficients<float>> highShelf;

// In prepareToPlay
*lowShelf.state = *juce::dsp::IIR::Coefficients<float>::makeLowShelf(
    sampleRate, 1000.0f, 0.707f, 1.0f - colourAmount);
*highShelf.state = *juce::dsp::IIR::Coefficients<float>::makeHighShelf(
    sampleRate, 1000.0f, 0.707f, colourAmount);
```

### Reverb
```cpp
juce::Reverb reverb;
juce::Reverb::Parameters params;
params.roomSize = 0.5f;
params.damping = 0.5f;
params.wetLevel = floatAmount;
params.dryLevel = 1.0f - floatAmount;
params.width = 1.0f;
reverb.setParameters(params);

// In processBlock
reverb.processStereo(leftData, rightData, numSamples);
```

## Threading Safety

### CRITICAL: prepareToPlay vs processBlock
```cpp
juce::SpinLock processingLock;

void prepareToPlay(double sampleRate, int samplesPerBlock) {
    const juce::SpinLock::ScopedLockType lock(processingLock);

    // Allocate all buffers here
    inputFifo.resize(fftSize);
    outputFifo.resize(fftSize);
    fftData.resize(fftSize * 2);
    prevPhase.resize(numBins);

    // Initialize DSP objects
    fft = std::make_unique<juce::dsp::FFT>(fftOrder);
}

void processBlock(AudioBuffer<float>& buffer, MidiBuffer&) {
    const juce::SpinLock::ScopedTryLockType lock(processingLock);
    if (!lock.isLocked()) return; // Skip if preparing

    // Process audio
}
```

## Memory Allocation

### Pre-allocate EVERYTHING in prepareToPlay
- FFT buffers (fftData)
- FIFOs (input/output)
- Previous state arrays (prevPhase, prevMagnitude, feedbackBuffer)
- Window coefficients (if custom)
- DSP objects (filters, reverb)

### NEVER allocate in processBlock
- No `std::vector::resize()`
- No `new` or `std::make_unique`
- No `std::complex<float>` temporaries (use reinterpret_cast)

## APVTS Setup

```cpp
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout() {
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "time", "Time",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));

    return {params.begin(), params.end()};
}

// In AudioProcessor constructor
: apvts(*this, nullptr, "Parameters", createParameterLayout())
```

## Null Testing

1. Comment out all spectral processing
2. FFT → IFFT with proper windowing should null
3. Use spectrum analyzer at -180 dB to verify
4. Below -144 dB is floating point precision limit
