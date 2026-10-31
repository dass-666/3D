#pragma once

#include "audio/EchoEngine.h"
#include "audio/PlanExchange.h"
#include "audio/SourceAudio.h"
#include "model/Scene.h"
#include "plugin/Diagnostics.h"
#include "plugin/Parameters.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace ses
{

// Passes the input through untouched and adds the repetition(s) on top.
// The scene document and the decoded source live on the message thread; the
// audio thread only sees immutable PlaybackPlans handed over lock-free.
class SpatialEchoProcessor final : public juce::AudioProcessor
{
public:
    static constexpr int kStateVersion = 1;

    SpatialEchoProcessor();
    ~SpatialEchoProcessor() override;

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

    struct SourceStatus
    {
        bool loaded = false;
        juce::String fileName;
        double seconds = 0.0;
        double sampleRate = 0.0;
        int numChannels = 0;
        bool embedded = false;
        juce::String message;  // last error or warning, empty if none
    };

    // Message thread.
    juce::Result loadSourceFile (const juce::File&);
    juce::Result loadSourceBytes (const juce::MemoryBlock& wavBytes, const juce::String& originalPath);
    SourceStatus getSourceStatus() const;
    void requestPreview() noexcept { previewRequested.store (true); }
    // Momentary trigger from the UI: same as an off->on edge of the Trigger
    // parameter, but a click can never be "used up" by leaving the toggle on.
    void requestTrigger() noexcept { triggerRequested.store (true); }

    const HostSnapshot& getHostSnapshot() const noexcept { return hostSnapshot; }
    DiagnosticLog& getDiagnostics() noexcept { return diagnostics; }

private:
    void rebuildPlan();
    void restoreSourceFromDocument();

    EchoEngine engine;
    PlanExchange<PlaybackPlan> plans;
    std::atomic<double> hostSampleRate { 44100.0 };
    std::atomic<bool> previewRequested { false };
    std::atomic<bool> triggerRequested { false };
    HostSnapshot hostSnapshot;
    DiagnosticLog diagnostics { hostSnapshot };
    bool lastTrigger = false;
    std::atomic<float>* mixPercent = nullptr;
    std::atomic<float>* triggerValue = nullptr;
    juce::SmoothedValue<float> wetLevel { 0.7f };

    std::shared_ptr<const DecodedAudio> source;  // guarded by documentLock
    juce::String sourceMessage;                  // guarded by documentLock

    juce::AudioProcessorValueTreeState parameters;
    std::atomic<float>* outputDb = nullptr;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> outputGain { 1.0f };

    mutable juce::CriticalSection documentLock;
    Document document;
    juce::StringArray lastLoadWarnings;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpatialEchoProcessor)
};

} // namespace ses
