#pragma once

// «Ecos en vivo» (B11): the repetitions (points) applied to the audio that
// arrives on the plugin's Mixer track, in real time.
//
// The raw input is written to a history buffer (limits::maxLiveEchoSeconds).
// Each enabled point becomes a tap that reads that history at the point's
// moment (beats at the tempo in force, or milliseconds in free time) and then
// goes through the same per-repetition processing as the fragment repetitions:
// pitch/formants (Signalsmith, streaming), high-pass, depth low-pass, position
// and level, Volume Shaper and its own effect chain. The taps are added to the
// output scaled by Mix, like the fragment repetitions.
//
// Timing rules (documented in the guide):
//   * Volume Shaper: the curve repeats every live cycle (LiveSettings::cycleBeats)
//     and follows the delayed signal, so each echo carries the same cut pattern.
//   * Stutter: in every cycle of the delayed signal the slices replay a piece
//     of that cycle (Start/Whole = its beginning, Manual = from manualStart,
//     End = its end). A slice that would need audio that has not arrived yet
//     stays silent.
//   * Pitch/formants need the stretcher's look-ahead (pitchLatencySamples()):
//     a pitched echo can never come earlier than that; closer points are
//     played at that minimum. No latency is reported to the host, so the dry
//     signal and the unpitched echoes are not delayed.
//
// Audio thread: no allocation, locks or disk access in process().

#include "audio/EchoEngine.h"

#include <memory>

namespace ses
{

// Message thread. Same point selection as the fragment (enabled; solo wins).
LiveEchoPlan makeLiveEchoPlan (const Scene&, double sampleRate);

class LiveEchoes
{
public:
    static constexpr int kChunk = 512;  // internal processing step: any host block size works

    LiveEchoes();
    ~LiveEchoes();

    void prepare (double sampleRate);  // allocates; not concurrent with the other calls
    void reset() noexcept;

    // Records the raw input (first 1 or 2 channels). Call every block, before anything modifies the buffer.
    void pushInput (const juce::AudioBuffer<float>&, int numSamples) noexcept;
    // Adds the taps to `buffer` (n samples, the block just pushed), scaled by the
    // Mix ramp wetStart..wetEnd. `bpm` = tempo in force.
    void process (juce::AudioBuffer<float>&, int numSamples, const LiveEchoPlan*, const TransportInfo&, double bpm,
                  float wetStart, float wetEnd) noexcept;

    bool isProcessing() const noexcept { return active; }
    int pitchLatencySamples() const noexcept { return pitchLatency; }
    int numActiveTaps() const noexcept;

private:
    struct Slot;
    void processChunk (int offset, int n, const LiveEchoPlan&, double beatStart, double bpm,
                       juce::AudioBuffer<float>& out, float wetA, float wetStep) noexcept;
    float readHistory (int ch, double samplesAgo) const noexcept;

    double sr = 44100.0;
    juce::AudioBuffer<float> history;
    int historySize = 0;
    int writePos = 0;          // next sample to write
    juce::int64 written = 0;   // samples written since reset (older history reads as silence)
    int blockStartBack = 0;    // samples between the start of the block being processed and writePos
    int pitchLatency = 0;
    double internalBeat = 0.0;
    juce::SmoothedValue<float> master { 0.0f };
    bool active = false;

    std::array<std::unique_ptr<Slot>, limits::maxFxPoints> slots;
    std::unique_ptr<LiveEchoPlan> emptyPlan;  // used while fading out (built off the audio thread)
    juce::AudioBuffer<float> tapBuf;  // stereo kChunk scratch
    juce::AudioBuffer<float> pitchIn;  // stereo kChunk scratch
};

} // namespace ses
