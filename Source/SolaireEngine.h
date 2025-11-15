#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <complex>
#include <memory>
#include <unordered_map>
#include "SpectralPeakExtraction.h"
#include "PartialTracking.h"
#include "OscillatorBank.h"

/**
 * Solaire Spectral Processing Engine
 *
 * Real-time spectral resynthesis engine implementing Rossum Panharmonium architecture:
 * - FFT analysis with spectral peak extraction (33 peaks)
 * - Partial tracking (McAulay-Quatieri algorithm)
 * - Oscillator bank resynthesis (33 independent oscillators)
 * - Spectral modifiers (BLUR, WARP, FEEDBACK)
 * - Output effects (COLOR tilt EQ, FLOAT reverb, MIX)
 *
 * Thread-safe with SpinLock for prepareToPlay/processBlock race condition protection
 */
class SolaireEngine
{
public:
    SolaireEngine();

    void prepareToPlay(double sampleRate, int samplesPerBlock);
    void releaseResources();

    /** Process a single sample (uses internal FIFOs and overlap-add) */
    float processSample(float inputSample);

    /** Parameter setters (0.0 to 1.0 range) */
    void setSlice(float value);         // PHASE 4: FFT window size (17ms - 6400ms)
    void setVoice(float value);         // PHASE 4: Active oscillator count (1-33)
    void setFreeze(float value);        // PHASE 4: Spectral freeze on/off
    void setBlur(float value);          // PHASE 5: Spectral smoothing (EMA alpha)
    void setWarp(float value);          // PHASE 5: Frequency warp
    void setFeedback(float value);      // PHASE 5: Spectral feedback gain

    // PHASE 6: Frequency controls
    void setCenterFreq(float value);    // Center frequency of spectral window
    void setBandwidth(float value);     // Width of spectral window
    void setFreq(float value);          // Fine frequency shift (-100 to +100 cents)
    void setOctave(float value);        // Octave transposition (-2 to +2 octaves)

    // PHASE 7: Glide and waveform selection
    void setGlide(float value);         // Portamento/glide time (0 - 1000ms)
    void setWaveform(float value);      // Waveform selection (0-1 maps to 0-3 index)

    // Output effects (COLOR and FLOAT kept per user request)
    void setMix(float value);           // Dry/Wet blend
    void setColour(float value);        // Tilt EQ balance (complementary shelving)
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

    // PHASE 5: Spectral modifier state (per-partial tracking)
    // SOURCE: Adapted from verified FFT bin processing patterns
    std::unordered_map<int, float> prevPartialAmplitudes;  // For BLUR (trackID → amplitude)
    std::unordered_map<int, float> feedbackAmplitudes;     // For FEEDBACK (trackID → amplitude)

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

    // PHASE 5: Spectral modifiers
    std::atomic<float> currentBlur{0.0f};
    std::atomic<float> currentWarp{0.5f};          // 0.5 = no warp
    std::atomic<float> currentFeedback{0.0f};

    // PHASE 6: Frequency controls
    std::atomic<float> currentCenterFreq{0.5f};    // Center frequency (20Hz - 20kHz, log scale)
    std::atomic<float> currentBandwidth{1.0f};     // Bandwidth (0 = narrow, 1 = full spectrum)
    std::atomic<float> currentFreq{0.5f};          // Fine frequency shift (0.5 = no shift)
    std::atomic<float> currentOctave{0.5f};        // Octave shift (0.5 = no shift, 0-1 = -2 to +2)

    // PHASE 7: Glide and waveform
    std::atomic<float> currentGlide{0.01f};        // Glide time in seconds (0 - 1.0s)
    std::atomic<float> currentWaveform{0.0f};      // Waveform index (0-1 maps to 0-3)

    // Output effects (PHASE 8: COLOR and FLOAT kept, RESONANCE removed)
    std::atomic<float> currentMix{0.5f};
    std::atomic<float> currentColour{0.5f};        // 0.5 = flat (tilt EQ)
    std::atomic<float> currentFloat{0.0f};         // Reverb amount

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

    // PHASE 5: Spectral modifier application to partial tracks
    // SOURCE: Adapted from verified FFT bin processing patterns
    void applySpectralModifiers(std::vector<PartialTrack>& tracks);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SolaireEngine)
};
