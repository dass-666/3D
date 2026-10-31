#include "audio/EchoEngine.h"

#include <algorithm>
#include <cmath>

namespace ses
{

std::unique_ptr<PlaybackPlan> buildPlan (const Scene& scene, const DecodedAudio* source, double hostSampleRate)
{
    if (source == nullptr || source->buffer.getNumSamples() == 0 || hostSampleRate <= 0.0)
        return nullptr;

    const auto total = (juce::int64) source->buffer.getNumSamples();
    const auto start = std::clamp<juce::int64> (scene.source.trimStart, 0, total - 1);
    const auto end = scene.source.trimEnd > start ? std::min (scene.source.trimEnd, total) : total;

    DecodedAudio trimmed;
    trimmed.sampleRate = source->sampleRate;
    trimmed.buffer.setSize (source->buffer.getNumChannels(), (int) (end - start));
    for (int ch = 0; ch < source->buffer.getNumChannels(); ++ch)
        trimmed.buffer.copyFrom (ch, 0, source->buffer, ch, (int) start, (int) (end - start));

    auto plan = std::make_unique<PlaybackPlan>();
    plan->sampleRate = hostSampleRate;
    plan->sample = resampled (trimmed, hostSampleRate);
    plan->gridBeats = beatsPerStep (scene.rhythm);

    const Point* first = nullptr;
    for (auto& p : scene.points)
        if (p.enabled && (first == nullptr || p.number < first->number))
            first = &p;

    if (first != nullptr)
        plan->echoes.push_back ({ first->timing.beat, (float) (first->volumePercent / 100.0) });

    return plan;
}

void EchoEngine::prepare (double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    fadeSamples = juce::jmax (1, (int) std::round (kFadeSeconds * sampleRate));
    reset();
}

void EchoEngine::reset()
{
    for (auto& v : voices)
        v.active = false;
    cancelPending();
    expectedHostBeat.reset();
    onHostTimeline = false;
    internalBeat = 0.0;
}

void EchoEngine::cancelPending() noexcept
{
    for (auto& p : pending)
        p.active = false;
}

int EchoEngine::numActiveVoices() const noexcept
{
    return (int) std::count_if (voices.begin(), voices.end(), [] (auto& v) { return v.active; });
}

int EchoEngine::numPendingEvents() const noexcept
{
    return (int) std::count_if (pending.begin(), pending.end(), [] (auto& p) { return p.active; });
}

void EchoEngine::schedule (const PlaybackPlan& plan, double referenceBeat) noexcept
{
    for (auto& echo : plan.echoes)
    {
        auto slot = std::find_if (pending.begin(), pending.end(), [] (auto& p) { return ! p.active; });
        if (slot == pending.end())
            return;  // queue full: drop rather than allocate
        *slot = { true, referenceBeat + echo.beat, echo.gain };
    }
}

void EchoEngine::startVoice (float gain, int offset, juce::AudioBuffer<float>& buffer, const PlaybackPlan& plan) noexcept
{
    auto slot = std::find_if (voices.begin(), voices.end(), [] (auto& v) { return ! v.active; });
    if (slot == voices.end())
        return;  // all voices busy: drop rather than cut a sounding voice

    *slot = { true, 0, gain };
    renderVoice (*slot, buffer, offset, buffer.getNumSamples(), plan);
}

void EchoEngine::renderVoice (Voice& v, juce::AudioBuffer<float>& buffer, int start, int end,
                              const PlaybackPlan& plan) noexcept
{
    const auto& s = plan.sample;
    const auto length = (juce::int64) s.getNumSamples();
    const int inChannels = s.getNumChannels();
    const int outChannels = buffer.getNumChannels();
    const float fadeDen = (float) (fadeSamples + 1);

    for (int i = start; i < end; ++i, ++v.position)
    {
        if (v.position >= length)
        {
            v.active = false;
            return;
        }

        // Linear fade in/out of fadeSamples: first and last samples are near zero, never a step.
        const float fadeIn = std::min (1.0f, (float) (v.position + 1) / fadeDen);
        const float fadeOut = std::min (1.0f, (float) (length - v.position) / fadeDen);
        const float g = v.gain * (wetA + wetStep * (float) i) * std::min (fadeIn, fadeOut);
        const auto idx = (int) v.position;

        if (inChannels == 1)
        {
            const float x = s.getSample (0, idx) * g;
            for (int ch = 0; ch < outChannels; ++ch)
                buffer.addSample (ch, i, x);
        }
        else if (outChannels == 1)
        {
            buffer.addSample (0, i, 0.5f * (s.getSample (0, idx) + s.getSample (1, idx)) * g);
        }
        else
        {
            for (int ch = 0; ch < outChannels; ++ch)
                buffer.addSample (ch, i, s.getSample (std::min (ch, 1), idx) * g);
        }
    }

    if (v.position >= length)
        v.active = false;
}

void EchoEngine::process (juce::AudioBuffer<float>& buffer, const PlaybackPlan* plan, const TransportInfo& t,
                          bool triggerRisingEdge, bool previewRequested, float wetStart, float wetEnd) noexcept
{
    const int n = buffer.getNumSamples();
    wetA = wetStart;
    wetStep = n > 0 ? (wetEnd - wetStart) / (float) n : 0.0f;

    if (plan != lastPlan)
    {
        // The previous plan may be freed after this block: stop everything that reads it.
        for (auto& v : voices)
            v.active = false;
        cancelPending();
        lastPlan = plan;
    }

    bpmUsed = std::clamp (t.bpm.value_or (kFallbackBpm), 20.0, 999.0);
    const double beatsPerSample = bpmUsed / 60.0 / sampleRate;

    const bool host = t.isPlaying && t.ppq.has_value();
    if (host != onHostTimeline)
    {
        cancelPending();  // the timeline changed meaning
        onHostTimeline = host;
        expectedHostBeat.reset();
    }

    const double blockStart = host ? *t.ppq : internalBeat;
    if (host && expectedHostBeat && std::abs (blockStart - *expectedHostBeat) > 1.0e-3)
        cancelPending();  // loop, jump or relocation

    if (plan != nullptr)
    {
        if (triggerRisingEdge)
        {
            // While the host plays, align to the next rhythm step (an automation point at
            // a grid position lands exactly on it). When stopped there is no grid: start now.
            const double grid = plan->gridBeats;
            const double ref = host ? std::ceil (blockStart / grid - 1.0e-6) * grid : blockStart;
            schedule (*plan, ref);
        }
        if (previewRequested)
            schedule (*plan, blockStart);

        for (auto& v : voices)
            if (v.active)
                renderVoice (v, buffer, 0, n, *plan);

        for (auto& p : pending)
        {
            if (! p.active)
                continue;
            const double offset = (p.startBeat - blockStart) / beatsPerSample;
            if (offset < -0.5)
            {
                p.active = false;  // missed (should not happen without a jump)
                continue;
            }
            const auto startSample = (int) std::llround (std::max (0.0, offset));
            if (startSample < n)
            {
                p.active = false;
                startVoice (p.gain, startSample, buffer, *plan);
            }
        }
    }

    const double blockEnd = blockStart + n * beatsPerSample;
    if (host)
        expectedHostBeat = blockEnd;
    else
        internalBeat = blockEnd;
}

} // namespace ses
