#pragma once

// «Procesar Mixer en vivo»: continuous processing of the plugin's input on
// the audio thread, without allocation, locks or disk access. Order:
// high-pass -> low-pass (incl. depth) -> Volume Shaper cycle -> pan/gain ->
// effect chain (character/modulation/delay/reverb in the chosen order) ->
// dry/wet. Zero latency (no look-ahead), so the host needs no compensation.
//
// Volume Shaper timing: while the host plays, the cycle is locked to the
// host position (ppq), so it lands on the same beats every time playback
// starts; while it is stopped, the cycle keeps running on an internal clock
// at the last tempo. Tails keep ringing across start/stop.

#include "audio/EchoEngine.h"

namespace ses
{

LivePlan makeLivePlan (const LiveSettings&, double sampleRate);

class LiveChain
{
public:
    void prepare (double sampleRate, int maxBlock);  // allocates; not concurrent with process()
    void reset() noexcept;

    // In place on the first 1 or 2 channels. `bpm` = tempo in force.
    void process (juce::AudioBuffer<float>&, int numSamples, const LivePlan*, const TransportInfo&, double bpm) noexcept;

    bool isProcessing() const noexcept { return active; }
    double cyclePositionBeats() const noexcept { return lastCycleBeat; }  // UI: where the shaper cycle is

private:
    double sr = 44100.0;
    int maxN = 0;
    PointFxChain chain;
    juce::AudioBuffer<float> wetBuf;
    juce::SmoothedValue<float> mix { 0.0f }, gainL { 1.0f }, gainR { 1.0f };
    float hz[2] {}, lz[2] {};
    float lpCoef = 1.0f;
    double internalBeat = 0.0;
    double lastCycleBeat = 0.0;
    bool active = false;
};

} // namespace ses
