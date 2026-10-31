#pragma once

// Diagnostic log for testing inside a real host (FL Studio). The audio thread
// only writes atomics; a message-thread timer turns changes into text lines and
// appends them to Documents/Spatial Echo Studio/diagnostico.log (capped size).

#include "audio/EchoEngine.h"

#include <juce_events/juce_events.h>

#include <atomic>

namespace ses
{

// Written by the audio thread at the end of each block, read anywhere.
struct HostSnapshot
{
    std::atomic<double> bpm { 0.0 }, ppq { 0.0 };
    std::atomic<bool> hasBpm { false }, hasPpq { false }, hasTimeSamples { false }, isPlaying { false };
    std::atomic<int> blockSize { 0 }, maxBlockSize { 0 };
    std::atomic<juce::int64> blocks { 0 };
    std::atomic<int> triggers { 0 }, fired { 0 }, firedLate { 0 }, dropped { 0 }, jumps { 0 };
    std::atomic<double> lastJumpBeats { 0.0 }, maxJitterBeats { 0.0 };
    std::atomic<double> lastTriggerBeat { 0.0 }, lastScheduledBeat { 0.0 }, lastFiredBeat { 0.0 };
    std::atomic<bool> usedHostTimeline { false }, ppqDerived { false };
    std::atomic<int> activeVoices { 0 };
    std::atomic<float> limiterGainDb { 0.0f };

    void store (const TransportInfo&, int numSamples, const EngineStats&, int voices) noexcept;
};

class DiagnosticLog final : private juce::Timer
{
public:
    explicit DiagnosticLog (const HostSnapshot&);
    ~DiagnosticLog() override;

    void setEnabled (bool);
    bool isEnabled() const noexcept { return enabled; }
    juce::File getFile() const;

    // Message thread (or any non-audio thread).
    void log (const juce::String& line);

private:
    void timerCallback() override;
    void write (const juce::String& line);

    const HostSnapshot& snapshot;
    bool enabled = true;
    juce::CriticalSection lock;
    juce::StringArray queued;
    std::unique_ptr<juce::FileOutputStream> out;

    // Last values seen by the timer, to log only changes.
    bool lastPlaying = false, lastHasPpq = false;
    double lastBpm = -1.0;
    int lastTriggers = 0, lastFired = 0, lastDropped = 0, lastJumps = 0;
    int lastMaxBlock = 0;
};

} // namespace ses
