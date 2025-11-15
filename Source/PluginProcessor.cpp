#include "PluginProcessor.h"

//==============================================================================
SolaireAudioProcessor::SolaireAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createParameterLayout())
{
}

SolaireAudioProcessor::~SolaireAudioProcessor()
{
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout SolaireAudioProcessor::createParameterLayout()
{
    // APVTS parameter layout (modern JUCE pattern from Context7)
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // All parameters normalized to 0.0 - 1.0 range
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        paramTime, "Time",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        paramBlur, "Blur",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        paramWarp, "Warp",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));  // 0.5 = no warp

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        paramFeedback, "Feedback",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        paramMix, "Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        paramColour, "Colour",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));  // 0.5 = flat

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        paramFloat, "Float",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        paramVoices, "Voices",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));  // Placeholder

    return {params.begin(), params.end()};
}

//==============================================================================
void SolaireAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    // Report latency to host (CRITICAL - see juce_critical_knowledge.md)
    // This triggers ComponentRestarter which can cause race condition
    // Both engines protected by internal SpinLock
    setLatencySamples(engines[0].getLatencyInSamples());

    // Prepare both stereo engines
    for (auto& engine : engines)
    {
        engine.prepareToPlay(sampleRate, samplesPerBlock);
    }

    // Initialize parameter smoothing (60Hz update rate)
    const float smoothingTime = 0.05f;  // 50ms
    timeSmooth.reset(sampleRate, smoothingTime);
    blurSmooth.reset(sampleRate, smoothingTime);
    warpSmooth.reset(sampleRate, smoothingTime);
    feedbackSmooth.reset(sampleRate, smoothingTime);
    mixSmooth.reset(sampleRate, smoothingTime);
    colourSmooth.reset(sampleRate, smoothingTime);
    floatSmooth.reset(sampleRate, smoothingTime);
    voicesSmooth.reset(sampleRate, smoothingTime);
}

void SolaireAudioProcessor::releaseResources()
{
    for (auto& engine : engines)
    {
        engine.releaseResources();
    }
}

bool SolaireAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    // Only stereo supported
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void SolaireAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                               juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // Clear unused output channels
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    // Update parameter smoothers from APVTS
    timeSmooth.setTargetValue(apvts.getRawParameterValue(paramTime)->load());
    blurSmooth.setTargetValue(apvts.getRawParameterValue(paramBlur)->load());
    warpSmooth.setTargetValue(apvts.getRawParameterValue(paramWarp)->load());
    feedbackSmooth.setTargetValue(apvts.getRawParameterValue(paramFeedback)->load());
    mixSmooth.setTargetValue(apvts.getRawParameterValue(paramMix)->load());
    colourSmooth.setTargetValue(apvts.getRawParameterValue(paramColour)->load());
    floatSmooth.setTargetValue(apvts.getRawParameterValue(paramFloat)->load());
    voicesSmooth.setTargetValue(apvts.getRawParameterValue(paramVoices)->load());

    // Process each sample
    const int numSamples = buffer.getNumSamples();

    for (int sample = 0; sample < numSamples; ++sample)
    {
        // Update smoothed parameter values
        const float time = timeSmooth.getNextValue();
        const float blur = blurSmooth.getNextValue();
        const float warp = warpSmooth.getNextValue();
        const float feedback = feedbackSmooth.getNextValue();
        const float mix = mixSmooth.getNextValue();
        const float colour = colourSmooth.getNextValue();
        const float floatParam = floatSmooth.getNextValue();
        const float voices = voicesSmooth.getNextValue();

        // Apply parameters to engines
        for (auto& engine : engines)
        {
            // PHASE 4: Core Panharmonium parameters
            engine.setSlice(time);         // TIME → SLICE (FFT window size)
            engine.setVoice(voices);       // VOICES → VOICE (active oscillators)
            // TODO: Add FREEZE, GLIDE, WAVEFORM parameters once parameter tree is updated

            // PHASE 5: Spectral modifiers
            engine.setBlur(blur);
            engine.setWarp(warp);
            engine.setFeedback(feedback);

            // Output effects (PHASE 8: COLOR and FLOAT kept, RESONANCE removed)
            engine.setMix(mix);
            engine.setColour(colour);
            engine.setFloat(floatParam);
        }

        // Process stereo channels
        float* channelL = buffer.getWritePointer(0);
        float* channelR = buffer.getWritePointer(1);

        channelL[sample] = engines[0].processSample(channelL[sample]);
        channelR[sample] = engines[1].processSample(channelR[sample]);
    }
}

//==============================================================================
bool SolaireAudioProcessor::hasEditor() const
{
    return true;  // Use GenericAudioProcessorEditor
}

juce::AudioProcessorEditor* SolaireAudioProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor(*this);
}

//==============================================================================
void SolaireAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    // Save APVTS state to XML
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void SolaireAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    // Load APVTS state from XML
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));

    if (xmlState.get() != nullptr)
        if (xmlState->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
}

//==============================================================================
// Boilerplate implementations

const juce::String SolaireAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool SolaireAudioProcessor::acceptsMidi() const
{
    return false;
}

bool SolaireAudioProcessor::producesMidi() const
{
    return false;
}

bool SolaireAudioProcessor::isMidiEffect() const
{
    return false;
}

double SolaireAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int SolaireAudioProcessor::getNumPrograms()
{
    return 1;
}

int SolaireAudioProcessor::getCurrentProgram()
{
    return 0;
}

void SolaireAudioProcessor::setCurrentProgram(int index)
{
}

const juce::String SolaireAudioProcessor::getProgramName(int index)
{
    return {};
}

void SolaireAudioProcessor::changeProgramName(int index, const juce::String& newName)
{
}

//==============================================================================
// Plugin instantiation function

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SolaireAudioProcessor();
}
