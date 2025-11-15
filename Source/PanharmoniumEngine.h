#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <complex>
#include <memory>
#include "SpectralPeakExtraction.h"

/**
 * Panharmonium Spectral Processing Engine
 *
 * Real-time STFT processor using juce::dsp::FFT with:
 * - 75% overlap-add with Hann window
 * - Spectral manipulation (BLUR, RESONANCE, WARP, FEEDBACK)
 * - Output effects (COLOR tilt EQ, FLOAT reverb, MIX)
 *
 * Thread-safe with SpinLock for prepareToPlay/processBlock race condition protection
 */
class PanharmoniumEngine
{
public:
    PanharmoniumEngine();

    void prepareToPlay(double sampleRate, int samplesPerBlock);
    void releaseResources();

    /** Process a single sample (uses internal FIFOs and overlap-add) */
    float processSample(float inputSample);

    /** Parameter setters (0.0 to 1.0 range) */
    void setTime(float value);          // FFT window size control
    void setBlur(float value);          // Spectral smoothing (EMA alpha)
    void setResonance(float value);     // Spectral filter Q/Gain
    void setWarp(float value);          // Phase vocoder warp factor
    void setFeedback(float value);      // Spectral feedback gain
    void setMix(float value);           // Dry/Wet blend
    void setColour(float value);        // Tilt EQ balance
    void setFloat(float value);         // Reverb decay time
    void setVoices(float value);        // Placeholder (bypass gain)

    int getLatencyInSamples() const { return fftSize; }

private:
    // FFT configuration constants (based on audiodev.blog pattern)
    static constexpr int fftOrder = 10;                     // 2^10 = 1024
    static constexpr int fftSize = 1 << fftOrder;           // 1024 samples
    static constexpr int numBins = fftSize / 2 + 1;         // 513 bins
    static constexpr int overlap = 4;                       // 75% overlap
    static constexpr int hopSize = fftSize / overlap;       // 256 samples
    static constexpr float windowCorrection = 2.0f / 3.0f;  // Hann^2 with 75% overlap

    // Panharmonium spectral resynthesis constants
    static constexpr int maxSpectralPeaks = 33;             // Rossum Panharmonium: 33 oscillators

    //==========================================================================
    // Core FFT objects
    std::unique_ptr<juce::dsp::FFT> fft;
    std::unique_ptr<juce::dsp::WindowingFunction<float>> window;

    // Circular FIFOs for overlap-add (audiodev.blog pattern)
    std::array<float, fftSize> inputFifo;
    std::array<float, fftSize> outputFifo;
    std::array<float, fftSize * 2> fftData;  // Interleaved complex numbers

    int fifoPos = 0;    // Current position in circular buffer
    int hopCount = 0;   // Counter for hop size

    //==========================================================================
    // Spectral processing state
    std::array<float, numBins> prevMagnitude;      // For BLUR (EMA smoothing)
    std::array<float, numBins> prevPhase;          // For WARP (phase vocoder)
    std::array<float, numBins> feedbackMagnitude;  // For FEEDBACK

    // Spectral peak extraction (Phase 1: Panharmonium resynthesis)
    // SOURCE: audiodev.blog FFT tutorial + DSPRelated peak detection
    std::vector<SpectralPeak> currentPeaks;        // Extracted peaks from current frame

    //==========================================================================
    // Output effects (juce::dsp patterns)
    juce::Reverb reverb;
    juce::dsp::IIR::Filter<float> lowShelf;
    juce::dsp::IIR::Filter<float> highShelf;

    // Dry buffer for mix
    std::array<float, fftSize> dryBuffer;
    int dryBufferPos = 0;

    //==========================================================================
    // Parameters (atomic for thread-safe parameter changes)
    std::atomic<float> currentTime{0.5f};
    std::atomic<float> currentBlur{0.0f};
    std::atomic<float> currentResonance{0.0f};
    std::atomic<float> currentWarp{0.5f};          // 0.5 = no warp
    std::atomic<float> currentFeedback{0.0f};
    std::atomic<float> currentMix{0.5f};
    std::atomic<float> currentColour{0.5f};        // 0.5 = flat
    std::atomic<float> currentFloat{0.0f};
    std::atomic<float> currentVoices{0.5f};        // Placeholder

    double sampleRate = 44100.0;

    //==========================================================================
    // Thread safety (critical - see juce_critical_knowledge.md)
    juce::SpinLock processingLock;

    //==========================================================================
    // Private methods
    void reset();
    void processFrame();
    void spectralManipulation(float* fftDataBuffer);
    void applyOutputEffects(float& sample);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PanharmoniumEngine)
};
