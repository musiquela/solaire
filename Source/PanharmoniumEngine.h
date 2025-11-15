#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <complex>
#include <memory>
#include "SpectralPeakExtraction.h"
#include "PartialTracking.h"
#include "OscillatorBank.h"

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
    void setSlice(float value);         // PHASE 4: FFT window size (17ms - 6400ms)
    void setVoice(float value);         // PHASE 4: Active oscillator count (1-33)
    void setFreeze(float value);        // PHASE 4: Spectral freeze on/off
    void setBlur(float value);          // Spectral smoothing (EMA alpha)
    void setResonance(float value);     // Spectral filter Q/Gain (to be removed)
    void setWarp(float value);          // Phase vocoder warp factor
    void setFeedback(float value);      // Spectral feedback gain
    void setMix(float value);           // Dry/Wet blend
    void setColour(float value);        // Tilt EQ balance
    void setFloat(float value);         // Reverb decay time

    int getLatencyInSamples() const { return fftSize; }

private:
    // PHASE 4: Dynamic FFT configuration (SLICE parameter)
    // SOURCE: JUCE forum (forum.juce.com/t/29348) - IvanC: "Use unique_ptr and reset to change size"
    // SOURCE: JUCE dsp::Convolution - thread-safe FFT size changing pattern
    int fftOrder = 10;                                      // 2^10 = 1024 (default)
    int fftSize = 1 << fftOrder;                            // 1024 samples (updated dynamically)
    int numBins = fftSize / 2 + 1;                          // 513 bins (updated dynamically)
    static constexpr int overlap = 4;                       // 75% overlap (constant)
    int hopSize = fftSize / overlap;                        // 256 samples (updated dynamically)
    static constexpr float windowCorrection = 2.0f / 3.0f;  // Hann^2 with 75% overlap

    // Panharmonium spectral resynthesis constants
    static constexpr int maxSpectralPeaks = 33;             // Rossum Panharmonium: 33 oscillators

    // PHASE 4: SLICE parameter range (Rossum Panharmonium specification)
    // SOURCE: Rossum Panharmonium manual - 17ms to 6400ms window sizes
    static constexpr float MIN_SLICE_MS = 17.0f;
    static constexpr float MAX_SLICE_MS = 6400.0f;

    //==========================================================================
    // Core FFT objects
    std::unique_ptr<juce::dsp::FFT> fft;
    std::unique_ptr<juce::dsp::WindowingFunction<float>> window;

    // PHASE 4: Dynamic buffers for variable FFT size (SLICE parameter)
    // SOURCE: JUCE forum pattern - use std::vector for runtime-resizable buffers
    // Circular FIFOs for overlap-add (audiodev.blog pattern)
    std::vector<float> inputFifo;
    std::vector<float> outputFifo;
    std::vector<float> fftData;  // Interleaved complex numbers

    int fifoPos = 0;    // Current position in circular buffer
    int hopCount = 0;   // Counter for hop size

    //==========================================================================
    // Spectral processing state (dynamic for variable FFT size)
    std::vector<float> prevMagnitude;      // For BLUR (EMA smoothing)
    std::vector<float> prevPhase;          // For WARP (phase vocoder)
    std::vector<float> feedbackMagnitude;  // For FEEDBACK

    // Spectral peak extraction (Phase 1: Panharmonium resynthesis)
    // SOURCE: audiodev.blog FFT tutorial + DSPRelated peak detection
    std::vector<SpectralPeak> currentPeaks;        // Extracted peaks from current frame

    // Partial tracking (Phase 2: Panharmonium resynthesis)
    // SOURCE: McAulay-Quatieri algorithm + JUCE forums
    PartialTrackingEngine partialTracker;          // Maintains peak identity across frames

    // Oscillator bank (Phase 3: Panharmonium resynthesis)
    // SOURCE: JUCE DSP Tutorial + JUCE forums
    OscillatorBank oscillatorBank;                 // 33 independent sine oscillators

    //==========================================================================
    // Output effects (juce::dsp patterns)
    juce::Reverb reverb;
    juce::dsp::IIR::Filter<float> lowShelf;
    juce::dsp::IIR::Filter<float> highShelf;

    // Dry buffer for mix (dynamic for variable FFT size)
    std::vector<float> dryBuffer;
    int dryBufferPos = 0;

    //==========================================================================
    // Parameters (atomic for thread-safe parameter changes)
    // PHASE 4: Core Panharmonium parameters
    std::atomic<float> currentSlice{0.1f};         // FFT window size (17ms - 6400ms, log scale)
    std::atomic<float> currentVoice{1.0f};         // Active oscillator count (1-33)
    std::atomic<float> currentFreeze{0.0f};        // Spectral freeze (0=off, 1=on)

    std::atomic<float> currentBlur{0.0f};
    std::atomic<float> currentResonance{0.0f};     // To be removed in Phase 8
    std::atomic<float> currentWarp{0.5f};          // 0.5 = no warp
    std::atomic<float> currentFeedback{0.0f};
    std::atomic<float> currentMix{0.5f};
    std::atomic<float> currentColour{0.5f};        // 0.5 = flat
    std::atomic<float> currentFloat{0.0f};

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

    // PHASE 4: Dynamic FFT size management (SLICE parameter)
    // SOURCE: JUCE dsp::Convolution pattern - thread-safe FFT reset
    void updateFFTSize(int newOrder);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PanharmoniumEngine)
};
