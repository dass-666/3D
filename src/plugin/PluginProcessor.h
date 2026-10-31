#pragma once

#include "model/Scene.h"
#include "plugin/Parameters.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace ses
{

// B0: passes audio through with a smoothed output gain. Holds the scene
// document so it is saved and restored with the host project. The audio
// thread never touches the document.
class SpatialEchoProcessor final : public juce::AudioProcessor
{
public:
    static constexpr int kStateVersion = 1;

    SpatialEchoProcessor();
    ~SpatialEchoProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getParameters() noexcept { return parameters; }

    // Thread-safe copies; never call from the audio thread.
    Document getDocument() const;
    void setDocument (Document);
    juce::StringArray getLastLoadWarnings() const;

private:
    juce::AudioProcessorValueTreeState parameters;
    std::atomic<float>* outputDb = nullptr;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> outputGain { 1.0f };

    mutable juce::CriticalSection documentLock;
    Document document;
    juce::StringArray lastLoadWarnings;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpatialEchoProcessor)
};

} // namespace ses
