#pragma once

// Per-repetition effect chain, run on the audio thread without allocation:
// character -> modulation -> delay -> reverb by default (distortion before time
// effects keeps echoes and tails clean); PointEffects::chainOrder can reorder.
// One chain per repetition slot; the engine feeds it the voice's stereo
// signal and adds the result to the output.

#include "model/Scene.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <vector>

namespace ses
{

class PointFxChain
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset() noexcept;

    // In place on `left`/`right` (n samples). `bpm` for the synced delay.
    void process (const PointEffects&, float* left, float* right, int n, double bpm) noexcept;

    // True while a delay/reverb tail may still be sounding after the last input.
    bool isTailActive() const noexcept { return tailLeft > 0; }
    void noteInput (float peak, const PointEffects&) noexcept;  // call before process() with the block's input peak

    // Seconds a tail can last with these settings (bounded).
    static double tailSeconds (const PointEffects&) noexcept;

private:
    void processCharacter (const CharacterSettings&, float* l, float* r, int n) noexcept;
    void processModulation (const ModulationSettings&, float* l, float* r, int n) noexcept;
    void processDelay (const DelaySettings&, float* l, float* r, int n, double bpm) noexcept;
    void processReverb (const ReverbSettings&, float* l, float* r, int n) noexcept;

    double sr = 44100.0;
    int tailLeft = 0;
    bool primed = false;  // false until the first processed block (parameters jump, no ramp)

    // character
    juce::IIRFilter telHp[2], telLp[2];
    float lofiLp[2] {};
    float hold[2] {};
    int holdCount = 0;
    double ringPhase = 0.0;
    float mixC = 0.0f;

    // modulation
    std::vector<float> modBuf[2];
    int modWrite = 0;
    double lfoPhase = 0.0;
    float flangeFb[2] {};
    float mixM = 0.0f, depthM = 0.0f;

    // delay
    std::vector<float> dlBuf[2];
    int dlWrite = 0;
    double dlSamples = 0.0;  // smoothed read distance
    float dlLp[2] {};
    float mixD = 0.0f, fbD = 0.0f;

    // reverb
    std::vector<float> preBuf[2];
    int preWrite = 0;
    juce::Reverb reverb;
    juce::Reverb::Parameters lastParams;
    bool reverbActive = false;
    float mixR = 0.0f, outR = 1.0f;
};

} // namespace ses
