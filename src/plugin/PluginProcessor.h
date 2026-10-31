#pragma once

#include "audio/EchoEngine.h"
#include "audio/PitchRenderer.h"
#include "audio/PlanExchange.h"
#include "audio/SafetyLimiter.h"
#include "audio/SourceAudio.h"
#include "model/Scene.h"
#include "plugin/Diagnostics.h"
#include "plugin/Parameters.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>

namespace ses
{

enum class UiMode { Easy, Pro };

// Passes the input through untouched and adds the repetitions on top.
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
    juce::AudioProcessorParameter* getBypassParameter() const override;

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

    // ---- Document (never call from the audio thread) -----------------------

    // Full copy including embedded audio bytes (save/export; can be large).
    Document getDocument() const;
    // Replaces everything and reloads the source audio from the document.
    void setDocument (Document);
    juce::StringArray getLastLoadWarnings() const;

    // Cheap copy for the UI: embedded audio bytes are left out.
    Scene getSceneForUi() const;
    // Edits the active scene and republishes the plan (sample re-rendered only
    // if the source or trim changed). Bumps the document version.
    void editScene (const std::function<void (Scene&)>&);
    juce::uint32 getDocumentVersion() const noexcept { return documentVersion.load(); }

    juce::Result saveSceneFile (const juce::File&) const;
    juce::Result loadSceneFile (const juce::File&);

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

    juce::Result loadSourceFile (const juce::File&);
    juce::Result loadSourceBytes (const juce::MemoryBlock& wavBytes, const juce::String& originalPath);
    SourceStatus getSourceStatus() const;
    std::shared_ptr<const DecodedAudio> getSourceAudio() const;

    void requestPreview() noexcept { previewRequested.store (true); }          // all points, now
    void requestTriggerPoint (const juce::String& pointId) noexcept { pointTrigger.store (keyForPoint (pointId)); }
    void requestFragmentPreview() noexcept { fragmentRequested.store (true); } // the fragment once, centred
    // Momentary trigger from the UI: same as an off->on edge of the Trigger
    // parameter, but a click can never be "used up" by leaving the toggle on.
    void requestTrigger() noexcept { triggerRequested.store (true); }

    const HostSnapshot& getHostSnapshot() const noexcept { return hostSnapshot; }
    DiagnosticLog& getDiagnostics() noexcept { return diagnostics; }
    double getCurrentBeat() const noexcept { return currentBeat.load(); }

    void refreshPlan() { rebuildPlan(); }  // message thread

    // Pitch pre-rendering status (message thread).
    int numPendingPitchRenders() const { return pitchRenderer.numPending(); }
    bool isPitchPending (const Point& p) const { return pitchRenderer.isPending (centsFor (p.sound.pitchSemitones)); }

    UiMode getUiMode() const noexcept { return uiMode.load(); }
    void setUiMode (UiMode m) noexcept { uiMode.store (m); }

private:
    void rebuildPlan();                   // caller must NOT hold documentLock
    void restoreSourceFromDocument();     // reads/clears embedded bytes of the active scene
    void takeEmbeddedBytes();             // moves embedded bytes out of `document`

    EchoEngine engine;
    SafetyLimiter limiter;
    PlanExchange<PlaybackPlan> plans;
    std::atomic<double> hostSampleRate { 44100.0 };
    std::atomic<bool> previewRequested { false }, fragmentRequested { false }, triggerRequested { false };
    std::atomic<juce::uint64> pointTrigger { 0 };
    PitchRenderer pitchRenderer;
    std::atomic<double> currentBeat { 0.0 };
    std::atomic<UiMode> uiMode { UiMode::Easy };
    bool lastTrigger = false;
    HostSnapshot hostSnapshot;
    DiagnosticLog diagnostics { hostSnapshot };

    juce::AudioProcessorValueTreeState parameters;
    std::atomic<float>* outputDb = nullptr;
    std::atomic<float>* mixPercent = nullptr;
    std::atomic<float>* triggerValue = nullptr;
    std::atomic<float>* bypassValue = nullptr;
    std::atomic<float>* protectValue = nullptr;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> outputGain { 1.0f };
    juce::SmoothedValue<float> wetLevel { 0.7f };
    juce::SmoothedValue<float> bypassMix { 0.0f };
    juce::AudioBuffer<float> dryCopy;  // preallocated in prepareToPlay

    // Message-thread state, guarded by documentLock.
    mutable juce::CriticalSection documentLock;
    Document document;                   // embeddedData of the active scene kept in `embeddedBytes`
    juce::MemoryBlock embeddedBytes;
    juce::StringArray lastLoadWarnings;
    std::shared_ptr<const DecodedAudio> source;
    juce::String sourceMessage;
    std::atomic<juce::uint32> documentVersion { 1 };

    // Rendered fragment cache (message thread, guarded by planLock).
    juce::CriticalSection planLock;
    SharedSample renderedSample;
    std::vector<std::pair<SharedSample, juce::uint32>> retiring;  // replaced buffers, kept 2 s so voices fade out
    std::vector<SharedSample> lastOwned;
    const DecodedAudio* renderedFrom = nullptr;
    juce::int64 renderedTrimStart = -1, renderedTrimEnd = -1;
    double renderedRate = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpatialEchoProcessor)
};

} // namespace ses
