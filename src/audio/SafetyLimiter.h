#pragma once

// Output protection: stereo-linked peak limiter with instant attack and a
// smooth release. Below the ceiling the gain is exactly 1 (bit-transparent).
// The processor runs it only while repetitions sound (or it is still
// releasing), so the dry signal alone is never touched.
// It prevents overs; it is not a mastering limiter (no look-ahead).

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>

namespace ses
{

class SafetyLimiter
{
public:
    static constexpr float kCeiling = 0.9661f;  // -0.3 dBFS

    void prepare (double sampleRate)
    {
        releaseCoef = (float) (1.0 - std::exp (-1.0 / (0.08 * sampleRate)));
        gain = 1.0f;
    }

    void process (juce::AudioBuffer<float>& buffer) noexcept
    {
        const int n = buffer.getNumSamples();
        const int channels = buffer.getNumChannels();
        minGain = 1.0f;
        for (int i = 0; i < n; ++i)
        {
            float peak = 0.0f;
            for (int ch = 0; ch < channels; ++ch)
                peak = std::max (peak, std::abs (buffer.getSample (ch, i)));
            const float needed = peak > kCeiling ? kCeiling / peak : 1.0f;
            gain = needed < gain ? needed : std::min (needed, gain + (1.0f - gain) * releaseCoef);
            if (gain > 0.99999f && needed >= 1.0f)
                gain = 1.0f;
            if (gain < 1.0f)
                for (int ch = 0; ch < channels; ++ch)
                    buffer.getWritePointer (ch)[i] *= gain;
            minGain = std::min (minGain, gain);
        }
    }

    float lastMinGain() const noexcept { return minGain; }
    bool isReducing() const noexcept { return gain < 1.0f; }
    void idle() noexcept { minGain = 1.0f; }

private:
    float releaseCoef = 0.0003f;
    float gain = 1.0f;
    float minGain = 1.0f;
};

} // namespace ses
