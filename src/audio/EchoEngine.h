#pragma once

// Audio-thread engine: schedules repetitions on the musical timeline and plays
// them from pre-rendered samples. No locks, no allocation in process().
//
// Each enabled point becomes one or more PlannedEchos: a single full
// repetition, or (Stutter) a run of short slices. Voices follow live edits of
// their point (matched by a stable key + slice index), so dragging a point
// while it sounds moves it smoothly instead of cutting it. Pitch is applied
// by playing a pre-rendered pitched copy of the fragment (see PitchRenderer).

#include "audio/SourceAudio.h"
#include "model/Scene.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <map>
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
    float gainL = 1.0f, gainR = 1.0f;  // includes point gain and depth attenuation
    float lowpassCoef = 1.0f;          // one-pole coefficient, 1 = no filtering
};

Placement placementFor (const Point&, double sampleRate, double extraGainDb = 0.0);
float onePoleCoef (double cutoffHz, double sampleRate) noexcept;  // 1 = no filtering

struct PlannedEcho
{
    juce::uint64 key = 0;     // stable per point (hash of its id); 0 is reserved for the fragment preview
    int index = 0;            // 0 = the repetition, 1.. = stutter slices
    double pointBeat = 0.0;   // the point's moment (quarter notes after the trigger)
    double beat = 0.0;        // when this echo starts (>= pointBeat), beats after the reference
    // Free time: the point's moment is `seconds` after the reference instead of
    // pointBeat (tempo changes do not move it); `beat` then holds only the
    // stutter offset (beat - pointBeat).
    bool freeTime = false;
    double seconds = 0.0;
    SharedSample sample;      // buffer to play (base or pitched copy)
    bool pitchPending = false;  // true while the pitched copy is still rendering (base plays instead)
    // Slice of `sample` to play, in samples. sliceBeats > 0 limits the length
    // to that many beats at the tempo in force; alignEnd takes it from regionEnd backwards.
    juce::int64 regionStart = 0, regionEnd = 0;
    double sliceBeats = 0.0;
    bool alignEnd = false;
    Placement placement;
    ShaperSettings shaper;   // volume curve on top (enabled or not)
    PointEffects effects;
    float highpassCoef = 0.0f;  // one-pole, 0 = off
};

// Volume Shaper evaluation (pure; shared by the engine and the UI preview).
// `t` and `length` in seconds from the start of the voice; `periodSeconds`
// is only used by the Division mode (tempo in force). Returns 0..1 before
// anticlick smoothing.
float shaperGain (const ShaperSettings&, double t, double length, double divisionSeconds) noexcept;

// Everything the audio thread needs, rendered at the host sample rate.
// Immutable once published.
struct PlaybackPlan
{
    double sampleRate = 0.0;
    SharedSample sample;                 // base fragment (unpitched); may be null
    std::vector<SharedSample> owned;     // every buffer a voice may still be reading (incl. recently replaced ones)
    double gridBeats = 1.0;              // rhythm step used to align triggers
    std::vector<PlannedEcho> echoes;
    int pendingPitchRenders = 0;

    const PlannedEcho* find (juce::uint64 key, int index) const noexcept;
    const PlannedEcho* find (juce::uint64 key) const noexcept { return find (key, 0); }
    bool owns (const SampleBuffer*) const noexcept;
};

juce::uint64 keyForPoint (const juce::String& pointId);
int centsFor (double semitones) noexcept;  // cache key for pitched renders

// Message thread. Trims (source samples) and resamples to the host rate.
SharedSample renderSample (const SourceRef&, const DecodedAudio* source, double hostSampleRate);

// Message thread. One or more echoes per enabled point; if any enabled point
// is soloed, only soloed points play. `pitched` maps centsFor(semitones) to a
// rendered copy; a missing entry plays the base and is counted as pending.
std::unique_ptr<PlaybackPlan> buildPlan (const Scene&, SharedSample base, const std::map<int, SharedSample>& pitched,
                                         double hostSampleRate);

inline std::unique_ptr<PlaybackPlan> buildPlan (const Scene& scene, SharedSample base, double hostSampleRate)
{
    return buildPlan (scene, std::move (base), {}, hostSampleRate);
}

// Convenience (tests, no pitch): render + build. Null when there is no audio.
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
    int fired = 0;             // echoes started
    int firedLate = 0;         // started late (host skipped a little past them)
    int dropped = 0;           // pending events dropped (too late, queue or voices full)
    int jumps = 0;             // transport relocations detected
    double lastJumpBeats = 0.0;
    double maxJitterBeats = 0.0;  // largest host-vs-predicted difference accepted as jitter
    double lastTriggerBeat = 0.0, lastScheduledBeat = 0.0, lastFiredBeat = 0.0;
    double lastReferenceBeat = 0.0;  // timeline beat that the points' beats count from (last trigger/preview)
    double lastReferenceSeconds = 0.0;  // same moment on the engine's sample clock, in seconds
    int references = 0;              // bumps on every trigger/preview
    bool usedHostTimeline = false;
    bool ppqDerivedFromSamples = false;
};

struct EngineRequests
{
    bool triggerRisingEdge = false;  // all points, aligned to the grid while the host plays
    bool previewScene = false;       // all points, now
    bool previewFragment = false;    // the fragment once, centred, unity, now
    juce::uint64 triggerPoint = 0;   // one point (by key), now, with its own settings
};

class EchoEngine
{
public:
    static constexpr int kMaxVoices = 64;
    static constexpr int kMaxPending = 16 * (1 + 32) + 16;
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
    void process (juce::AudioBuffer<float>& buffer, const PlaybackPlan* plan, const TransportInfo&,
                  const EngineRequests&, float wetStart, float wetEnd) noexcept;

    // Older signature kept for the existing tests.
    void process (juce::AudioBuffer<float>& buffer, const PlaybackPlan* plan, const TransportInfo& t,
                  bool triggerRisingEdge, bool previewScene, float wetStart, float wetEnd,
                  bool previewFragment = false) noexcept
    {
        process (buffer, plan, t, EngineRequests { triggerRisingEdge, previewScene, previewFragment, 0 }, wetStart, wetEnd);
    }

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
    double currentSeconds() const noexcept { return sampleClock / sampleRate; }
    const EngineStats& stats() const noexcept { return engineStats; }

private:
    struct Voice
    {
        bool active = false;
        const SampleBuffer* buffer = nullptr;  // compared by address; read only while the plan owns it
        juce::int64 position = 0, start = 0, end = 0;
        juce::uint64 key = 0;
        int index = 0;
        float gainL = 0.0f, gainR = 0.0f, coef = 1.0f;
        float z[2] {};                 // lowpass state
        float hz[2] {};                // highpass state (tracks the low part to subtract)
        float hpCoef = 0.0f;
        bool reverse = false;
        float release = 1.0f;          // < 1 while fading out
        bool releasing = false;
        float shaperEnv = 1.0f;        // smoothed Volume Shaper gain
    };

    struct Pending
    {
        bool active = false;
        double referenceBeat = 0.0;    // trigger moment; the echo's beat is looked up live
        double referenceSample = 0.0;  // the same moment on the sample clock (free time)
        juce::uint64 key = 0;
        int index = 0;
    };

    void schedule (const PlaybackPlan&, double referenceBeat, double referenceSample, juce::uint64 onlyKey = 0) noexcept;
    void addPending (double referenceBeat, double referenceSample, juce::uint64 key, int index) noexcept;
    bool startVoice (const PlannedEcho&, int offsetInBlock, juce::AudioBuffer<float>&, const PlaybackPlan&,
                     double beatsPerSample) noexcept;
    void renderVoice (Voice&, juce::AudioBuffer<float>&, int start, int end, const PlaybackPlan&, double beatsPerSample) noexcept;
    void cancelPending() noexcept;

    double sampleRate = 44100.0;
    int fadeSamples = 88;
    float releaseStep = 0.01f;
    double bpmUsed = kFallbackBpm;
    float wetA = 1.0f, wetStep = 0.0f;  // per-block wet ramp

    // Timeline: host ppq when the host is playing, otherwise an internal clock.
    bool onHostTimeline = false;
    double internalBeat = 0.0;
    double lastBlockStart = 0.0;
    double sampleClock = 0.0;  // samples processed since prepare(); free-time echoes count on it
    std::optional<double> expectedHostBeat;
    EngineStats engineStats;

    std::array<Voice, kMaxVoices> voices {};
    std::array<Pending, kMaxPending> pending {};
};

} // namespace ses
