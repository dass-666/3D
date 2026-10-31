#pragma once

// Audio-thread engine: schedules repetitions on the musical timeline and plays
// them from a pre-rendered sample. No locks, no allocation in process().

#include "audio/SourceAudio.h"
#include "model/Scene.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <optional>

namespace ses
{

struct PlannedEcho
{
    double beat = 0.0;  // quarter notes after the (grid-aligned) trigger
    float gain = 1.0f;  // linear
};

// Everything the audio thread needs, rendered at the host sample rate.
// Immutable once published.
struct PlaybackPlan
{
    double sampleRate = 0.0;
    juce::AudioBuffer<float> sample;      // trimmed fragment, 1 or 2 channels
    double gridBeats = 1.0;               // rhythm step used to align triggers
    std::vector<PlannedEcho> echoes;      // B2: at most one
};

// Message thread. Trims the source (scene.source.trimStart/trimEnd, in source
// samples) and resamples it to the host rate. B2 policy: the first enabled
// point (lowest number) becomes the single echo. Null when there is no audio.
std::unique_ptr<PlaybackPlan> buildPlan (const Scene&, const DecodedAudio* source, double hostSampleRate);

struct TransportInfo
{
    std::optional<double> bpm;     // host tempo if known
    std::optional<double> ppq;     // host position in quarter notes if known
    bool isPlaying = false;
    std::optional<juce::int64> timeSamples;  // host position in samples, used when ppq is missing
};

// Counters for diagnostics (read by the processor after each block).
struct EngineStats
{
    int triggers = 0;          // trigger edges accepted
    int fired = 0;             // repetitions started
    int firedLate = 0;         // started late (host skipped a little past them)
    int dropped = 0;           // pending events dropped (too late, queue or voices full)
    int jumps = 0;             // transport relocations detected
    double lastJumpBeats = 0.0;
    double maxJitterBeats = 0.0;  // largest host-vs-predicted difference accepted as jitter
    double lastTriggerBeat = 0.0, lastScheduledBeat = 0.0, lastFiredBeat = 0.0;
    bool usedHostTimeline = false;
    bool ppqDerivedFromSamples = false;
};

class EchoEngine
{
public:
    static constexpr int kMaxVoices = 8;
    static constexpr int kMaxPending = 16;
    static constexpr double kFallbackBpm = 120.0;
    static constexpr double kFadeSeconds = 0.002;
    // Host position differences up to this are jitter/rounding (FL Studio and
    // tempo changes inside a block produce them); larger ones are relocations.
    static constexpr double kJumpToleranceSeconds = 0.1;

    void prepare (double sampleRate);
    void reset();  // stops everything immediately

    // Adds the repetitions to `buffer` (dry signal untouched). The wet level
    // (Mix) ramps linearly from wetStart to wetEnd across the block.
    void process (juce::AudioBuffer<float>& buffer, const PlaybackPlan* plan, const TransportInfo&,
                  bool triggerRisingEdge, bool previewRequested, float wetStart, float wetEnd) noexcept;

    int numActiveVoices() const noexcept;
    int numPendingEvents() const noexcept;
    double lastBpmUsed() const noexcept { return bpmUsed; }
    const EngineStats& stats() const noexcept { return engineStats; }

private:
    struct Voice
    {
        bool active = false;
        juce::int64 position = 0;   // read index in plan->sample
        float gain = 1.0f;
    };

    struct Pending
    {
        bool active = false;
        double startBeat = 0.0;     // on the current timeline
        float gain = 1.0f;
    };

    void schedule (const PlaybackPlan&, double referenceBeat) noexcept;
    bool startVoice (float gain, int offsetInBlock, juce::AudioBuffer<float>&, const PlaybackPlan&) noexcept;
    void renderVoice (Voice&, juce::AudioBuffer<float>&, int start, int end, const PlaybackPlan&) noexcept;
    void cancelPending() noexcept;

    double sampleRate = 44100.0;
    int fadeSamples = 88;
    double bpmUsed = kFallbackBpm;
    float wetA = 1.0f, wetStep = 0.0f;  // per-block wet ramp

    // Timeline: host ppq when the host is playing, otherwise an internal clock.
    bool onHostTimeline = false;
    double internalBeat = 0.0;
    std::optional<double> expectedHostBeat;
    EngineStats engineStats;
    const PlaybackPlan* lastPlan = nullptr;

    std::array<Voice, kMaxVoices> voices {};
    std::array<Pending, kMaxPending> pending {};
};

} // namespace ses
