#pragma once

// Pre-renders pitched copies of the fragment on a background thread, keeping
// the duration (Signalsmith Stretch, MIT). The audio thread never waits for
// it: until a copy is ready the unpitched fragment plays and the UI shows
// "processing". Results are cached per (base buffer, cents) and dropped when
// the base changes.

#include "audio/EchoEngine.h"

#include <juce_events/juce_events.h>

#include <map>
#include <set>

namespace ses
{

// Synchronous (test / background use): same length as the input, same channels.
SampleBuffer renderPitched (const SampleBuffer& input, double sampleRate, double semitones);

class PitchRenderer final : private juce::Thread, private juce::AsyncUpdater
{
public:
    PitchRenderer();
    ~PitchRenderer() override;

    // Message thread. Sets the fragment to pitch (cache cleared when it changes)
    // and the cents values needed; starts rendering the missing ones.
    void setBase (SharedSample base, double sampleRate);
    void request (const std::set<int>& cents);

    // Message thread. Ready copies for the current base.
    std::map<int, SharedSample> ready() const;
    int numPending() const;
    bool isPending (int cents) const;

    std::function<void()> onRenderFinished;  // message thread

private:
    void run() override;
    void handleAsyncUpdate() override;

    mutable juce::CriticalSection lock;
    SharedSample base;
    double sampleRate = 44100.0;
    std::map<int, SharedSample> done;
    std::set<int> wanted;
    juce::WaitableEvent wake;
};

} // namespace ses
