#include "audio/HostCapture.h"

#include <algorithm>

namespace ses
{

void HostCapture::prepare (double sampleRate, int numChannels)
{
    rate = sampleRate > 0.0 ? sampleRate : 44100.0;
    length = (juce::int64) (kRingSeconds * rate);
    ring.setSize (juce::jlimit (1, 2, numChannels), (int) length);
    ring.clear();
    writePos.store (0);
    recordStart.store (-1);
    peak.store (0.0f);
}

void HostCapture::push (const juce::AudioBuffer<float>& input, int n) noexcept
{
    if (length == 0 || n <= 0)
        return;
    const int channels = ring.getNumChannels();
    const int inChannels = input.getNumChannels();
    auto pos = writePos.load (std::memory_order_relaxed);
    float blockPeak = 0.0f;
    for (int ch = 0; ch < channels; ++ch)
    {
        const int src = inChannels == 0 ? -1 : std::min (ch, inChannels - 1);
        int done = 0;
        while (done < n)
        {
            const int at = (int) ((pos + done) % length);
            const int chunk = std::min (n - done, (int) (length - at));
            if (src < 0)
                ring.clear (ch, at, chunk);
            else
            {
                ring.copyFrom (ch, at, input, src, done, chunk);
                blockPeak = std::max (blockPeak, input.getMagnitude (src, done, chunk));
            }
            done += chunk;
        }
    }
    writePos.store (pos + n, std::memory_order_release);
    // Simple peak hold with decay for the UI meter.
    const float old = peak.load (std::memory_order_relaxed);
    peak.store (std::max (blockPeak, old * 0.9f), std::memory_order_relaxed);
}

void HostCapture::startRecording() noexcept
{
    recordStart.store (writePos.load (std::memory_order_acquire));
}

double HostCapture::recordedSeconds() const noexcept
{
    const auto start = recordStart.load();
    if (start < 0 || rate <= 0.0)
        return 0.0;
    return (double) (writePos.load (std::memory_order_acquire) - start) / rate;
}

std::optional<DecodedAudio> HostCapture::stopRecording()
{
    const auto start = recordStart.exchange (-1);
    if (start < 0)
        return std::nullopt;
    const auto end = writePos.load (std::memory_order_acquire);
    const auto maxLen = (juce::int64) (kMaxCaptureSeconds * rate);
    return copyRange (std::max (start, end - maxLen), end);
}

std::optional<DecodedAudio> HostCapture::takeLast (double seconds) const
{
    const auto end = writePos.load (std::memory_order_acquire);
    const auto want = (juce::int64) (std::clamp (seconds, 0.0, kMaxCaptureSeconds) * rate);
    return copyRange (std::max<juce::int64> (0, end - want), end);
}

std::optional<DecodedAudio> HostCapture::copyRange (juce::int64 from, juce::int64 to) const
{
    if (length == 0 || to <= from)
        return std::nullopt;
    const auto n = to - from;
    if (n > length - (juce::int64) (kMarginSeconds * rate))
        return std::nullopt;  // would include samples the audio thread may overwrite while we copy

    DecodedAudio out;
    out.sampleRate = rate;
    out.buffer.setSize (ring.getNumChannels(), (int) n);
    for (int ch = 0; ch < ring.getNumChannels(); ++ch)
    {
        juce::int64 done = 0;
        while (done < n)
        {
            const int at = (int) ((from + done) % length);
            const int chunk = (int) std::min (n - done, length - at);
            out.buffer.copyFrom (ch, (int) done, ring, ch, at, chunk);
            done += chunk;
        }
    }
    // Validate: the write head must not have run into the range we read.
    const auto after = writePos.load (std::memory_order_acquire);
    if (after - from > length)
        return std::nullopt;
    return out;
}

} // namespace ses
