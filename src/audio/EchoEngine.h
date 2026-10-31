#pragma once

// Audio-thread engine: schedules repetitions on the musical timeline and plays
// them from a pre-rendered sample. No locks, no allocation in process().
//
// B3: several independent repetitions (one per enabled point), each with its
// own moment, level, stereo pan and approximate depth. Voices follow live
// edits of their point (matched by a stable key), so dragging a point while it
// sounds moves it smoothly instead of cutting it.

#include "audio/SourceAudio.h"
#include "model/Scene.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <memory>
#include <optional>

namespace ses
{

using SampleBuffer = juce::AudioBuffer<float>;
using SharedSample = std::shared_ptr<const SampleBuffer>;

// Stereo placement of a repetition. Stereo only: X = left/right, Y = an
// approximate depth cue (farther from the listener line = quieter and darker,
// behind = a little darker still). This is NOT 3D/binaural localisation.
struct Placement
{
    float gainL = 1.0f, gainR = 1.0f;  // includes point volume and depth attenuation
    float lowpassCoef = 1.0f;          // one-pole coefficient, 1 = no filtering
};

Placement placementFor (const Point&, double sampleRate);

struct PlannedEcho
{
    juce::uint64 key = 0;     // stable per point (hash of its id); 0 is reserved for the fragment preview
    double beat = 0.0;        // quarter notes after the (grid-aligned) trigger
    Placement placement;
};

// Everything the audio thread needs, rendered at the host sample rate.
// Immutable once published.
struct PlaybackPlan
{
    double sampleRate = 0.0;
    SharedSample sample;          // trimmed fragment, 1 or 2 channels; may be null
    SharedSample previousSample;  // kept alive briefly so voices on it can fade out
    double gridBeats = 1.0;       // rhythm step used to align triggers
    std::vector<PlannedEcho> echoes;

    const PlannedEcho* find (juce::uint64 key) const noexcept;
};

juce::uint64 keyForPoint (const juce::String& pointId);

// Message thread. Trims (source samples) and resamples to the host rate.
SharedSample renderSample (const SourceRef&, const DecodedAudio* source, double hostSampleRate);

// Message thread. One echo per enabled point; if any enabled point is soloed,
// only soloed points play.
std::unique_ptr<PlaybackPlan> buildPlan (const Scene&, SharedSample sample, double hostSampleRate);

// Convenience (tests): render + build. Null when there is no audio.
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
    double lastReferenceBeat = 0.0;  // timeline beat that the points' beats count from (last trigger/preview)
    int references = 0;              // bumps on every trigger/preview
    bool usedHostTimeline = false;
    bool ppqDerivedFromSamples = false;
};

class EchoEngine
{
public:
    static constexpr int kMaxVoices = 32;
    static constexpr int kMaxPending = 64;
    static constexpr double kFallbackBpm = 120.0;
    static constexpr double kFadeSeconds = 0.002;
    static constexpr double kReleaseSeconds = 0.01;  // fade when a point is removed/muted while sounding
    // Host position differences up to this are jitter/rounding (FL Studio and
    // tempo changes inside a block produce them); larger ones are relocations.
    static constexpr double kJumpToleranceSeconds = 0.1;

    void prepare (double sampleRate);
    void reset();  // stops everything immediately

    // Adds the repetitions to `buffer` (dry signal untouched). The wet level
    // (Mix) ramps linearly from wetStart to wetEnd across the block.
    // previewScene: play all points now (internal timing). previewFragment:
    // play the selected fragment once, centred, at unity gain.
    void process (juce::AudioBuffer<float>& buffer, const PlaybackPlan* plan, const TransportInfo&,
                  bool triggerRisingEdge, bool previewScene, float wetStart, float wetEnd,
                  bool previewFragment = false) noexcept;

    int numActiveVoices() const noexcept;
    // Keys of the sounding voices (0 = fragment preview), for UI feedback.
    template <typename Fn> void forEachActiveKey (Fn&& fn) const noexcept
    {
        for (auto& v : voices)
            if (v.active && ! v.releasing)
                fn (v.key);
    }
    int numPendingEvents() const noexcept;
    double lastBpmUsed() const noexcept { return bpmUsed; }
    double currentBeat() const noexcept { return lastBlockStart; }
    const EngineStats& stats() const noexcept { return engineStats; }

private:
    struct Voice
    {
        bool active = false;
        const SampleBuffer* buffer = nullptr;  // compared by address; read only while a plan owns it
        juce::int64 position = 0;
        juce::uint64 key = 0;
        float gainL = 0.0f, gainR = 0.0f, coef = 1.0f;
        float z[2] {};                 // lowpass state
        float release = 1.0f;          // < 1 while fading out
        bool releasing = false;
    };

    struct Pending
    {
        bool active = false;
        double referenceBeat = 0.0;    // trigger moment; the point's beat is looked up live
        juce::uint64 key = 0;
    };

    void schedule (const PlaybackPlan&, double referenceBeat) noexcept;
    void addPending (double referenceBeat, juce::uint64 key) noexcept;
    bool startVoice (juce::uint64 key, int offsetInBlock, juce::AudioBuffer<float>&, const PlaybackPlan&) noexcept;
    void renderVoice (Voice&, juce::AudioBuffer<float>&, int start, int end, const PlaybackPlan&) noexcept;
    void cancelPending() noexcept;
    bool targetFor (const Voice&, const PlaybackPlan&, Placement&) const noexcept;

    double sampleRate = 44100.0;
    int fadeSamples = 88;
    float releaseStep = 0.01f;
    double bpmUsed = kFallbackBpm;
    float wetA = 1.0f, wetStep = 0.0f;  // per-block wet ramp

    // Timeline: host ppq when the host is playing, otherwise an internal clock.
    bool onHostTimeline = false;
    double internalBeat = 0.0;
    double lastBlockStart = 0.0;
    std::optional<double> expectedHostBeat;
    EngineStats engineStats;

    std::array<Voice, kMaxVoices> voices {};
    std::array<Pending, kMaxPending> pending {};
};

} // namespace ses
