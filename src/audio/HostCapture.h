#pragma once

// Keeps the last seconds of the audio arriving from the host (the Mixer
// track) in a ring buffer, so a fragment can be taken from it after the
// fact ("últimos N s") or by start/stop recording. The audio thread only
// writes; the message thread reads a range and validates that it was not
// overwritten meanwhile (the ring is much longer than any capture).

#include "audio/SourceAudio.h"

#include <atomic>
#include <optional>

namespace ses
{

class HostCapture
{
public:
    static constexpr double kRingSeconds = 20.0;    // memory: 2 ch x 20 s x 4 bytes = 7.7 MB at 48 kHz
    static constexpr double kMarginSeconds = 1.0;   // read window must stay this far from the write head
    static constexpr double kMaxCaptureSeconds = kRingSeconds - 2.0 * kMarginSeconds;

    void prepare (double sampleRate, int numChannels);  // not concurrent with push()

    // Audio thread. Copies `numSamples` of `input` (first `channels` of the ring).
    void push (const juce::AudioBuffer<float>& input, int numSamples) noexcept;

    // Message thread.
    void startRecording() noexcept;
    bool isRecording() const noexcept { return recordStart.load() >= 0; }
    double recordedSeconds() const noexcept;
    // Stops and returns what was recorded (at most kMaxCaptureSeconds, the most recent part).
    std::optional<DecodedAudio> stopRecording();
    // The last `seconds` of input (clamped to kMaxCaptureSeconds). Empty if nothing arrived yet.
    std::optional<DecodedAudio> takeLast (double seconds) const;

    float peakDb() const noexcept { return juce::Decibels::gainToDecibels (peak.load (std::memory_order_relaxed), -100.0f); }
    double sampleRate() const noexcept { return rate; }

private:
    std::optional<DecodedAudio> copyRange (juce::int64 from, juce::int64 to) const;

    juce::AudioBuffer<float> ring;
    double rate = 0.0;
    juce::int64 length = 0;
    std::atomic<juce::int64> writePos { 0 };       // total samples written (monotonic)
    std::atomic<juce::int64> recordStart { -1 };
    std::atomic<float> peak { 0.0f };
};

} // namespace ses
