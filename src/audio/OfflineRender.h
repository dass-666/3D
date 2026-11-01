#pragma once

// Offline rendering of the repetitions to audio, for «Exportar audio» /
// «Exportar a Playlist». It runs the SAME engine, plan, pitch renderer,
// effect chains, output gain and safety limiter as the real-time path, on a
// private EchoEngine, faster than real time, on whatever thread calls it
// (never the audio thread). The result starts at the trigger moment (t = 0)
// and ends when the last repetition and its delay/reverb tails have died out.

#include "audio/EchoEngine.h"

#include <functional>

namespace ses
{

struct OfflineRenderSettings
{
    double sampleRate = 48000.0;
    double bpm = EchoEngine::kFallbackBpm;  // tempo the beats are rendered at (the project's)
    juce::String onlyPointId;               // empty = every enabled repetition (respects mute/solo)
    bool includeFragment = false;           // also the original fragment at t = 0 (centred, unity)
    float mixPercent = 70.0f;               // the plugin's Mix (level of the repetitions)
    float outputDb = 0.0f;                  // the plugin's Salida
    bool protect = true;                    // safety limiter at -0.3 dBFS, as in real time
    int blockSize = 512;
    double maxSeconds = 600.0;              // hard cap (also bounds memory)
};

struct OfflineRenderResult
{
    juce::AudioBuffer<float> audio;  // stereo
    double sampleRate = 0.0;
    bool cancelled = false;
    bool truncated = false;          // hit maxSeconds before the tails ended
    juce::String error;              // empty = ok
    float peak = 0.0f;               // linear
    int pointsRendered = 0;
};

// Seconds the render is expected to last (latest repetition end + its tail).
double estimateRenderSeconds (const PlaybackPlan&, const OfflineRenderSettings&, juce::uint64 onlyKey);

// `progress` (0..1) is called between blocks; returning false cancels.
OfflineRenderResult renderOffline (const Scene&, const DecodedAudio* source, const OfflineRenderSettings&,
                                   const std::function<bool (double)>& progress = {});

enum class WavFormat { Pcm16, Pcm24, Float32 };
int bitsFor (WavFormat) noexcept;
// Writes through a temporary file next to `file` and then replaces it, so a
// failed or cancelled write never leaves half a file. 16-bit gets TPDF dither
// with a fixed seed (the same render always gives the same bytes).
juce::Result writeWavFile (const juce::File&, const juce::AudioBuffer<float>&, double sampleRate, WavFormat);

} // namespace ses
