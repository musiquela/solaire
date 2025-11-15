#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "PanharmoniumEngine.h"

/**
 * Panharmonium Audio Processor
 *
 * JUCE plugin wrapper for the PanharmoniumEngine spectral processor
 * Uses AudioProcessorValueTreeState for parameter management
 */
class PanharmoniumAudioProcessor : public juce::AudioProcessor
{
public:
    PanharmoniumAudioProcessor();
    ~PanharmoniumAudioProcessor() override;

    //==============================================================================
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    //==============================================================================
    // Parameter IDs
    static inline const juce::String paramTime{"time"};
    static inline const juce::String paramBlur{"blur"};
    static inline const juce::String paramResonance{"resonance"};
    static inline const juce::String paramWarp{"warp"};
    static inline const juce::String paramFeedback{"feedback"};
    static inline const juce::String paramMix{"mix"};
    static inline const juce::String paramColour{"colour"};
    static inline const juce::String paramFloat{"float"};
    static inline const juce::String paramVoices{"voices"};

private:
    //==============================================================================
    // APVTS for parameter management (modern JUCE pattern)
    juce::AudioProcessorValueTreeState apvts;

    // Create parameter layout
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // DSP engines (stereo = 2 instances)
    std::array<PanharmoniumEngine, 2> engines;

    // Parameter smoothing (to avoid zipper noise)
    juce::SmoothedValue<float> timeSmooth;
    juce::SmoothedValue<float> blurSmooth;
    juce::SmoothedValue<float> resonanceSmooth;
    juce::SmoothedValue<float> warpSmooth;
    juce::SmoothedValue<float> feedbackSmooth;
    juce::SmoothedValue<float> mixSmooth;
    juce::SmoothedValue<float> colourSmooth;
    juce::SmoothedValue<float> floatSmooth;
    juce::SmoothedValue<float> voicesSmooth;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PanharmoniumAudioProcessor)
};
