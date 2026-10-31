#include "audio/EchoEngine.h"

#include <algorithm>
#include <cmath>

namespace ses
{

Placement placementFor (const Point& p, double sampleRate)
{
    const auto pos = clamped (p.position);
    const double pan = std::clamp ((pos.x - 50.0) / 50.0, -1.0, 1.0);
    const double depth = std::clamp (std::abs (pos.y - 50.0) / 50.0, 0.0, 1.0);
    const bool behind = pos.y > 50.0;

    // Pan: constant-power shape, normalised so the centre is unity on both
    // sides and the near side never exceeds unity.
    const double theta = (pan + 1.0) * juce::MathConstants<double>::pi / 4.0;
    const double l = std::min (1.0, std::sqrt (2.0) * std::cos (theta));
    const double r = std::min (1.0, std::sqrt (2.0) * std::sin (theta));

    // Approximate depth: up to about -5 dB and a darker tone at the edge.
    const double level = std::clamp (p.volumePercent, 0.0, 100.0) / 100.0 * (1.0 - 0.45 * depth);
    double cutoff = 20000.0 * std::pow (0.2, depth) * (behind ? 1.0 - 0.35 * depth : 1.0);

    Placement out;
    out.gainL = (float) (level * l);
    out.gainR = (float) (level * r);
    out.lowpassCoef = cutoff >= std::min (18000.0, 0.45 * sampleRate)
                          ? 1.0f
                          : (float) (1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * cutoff / sampleRate));
    return out;
}

const PlannedEcho* PlaybackPlan::find (juce::uint64 key) const noexcept
{
    for (auto& e : echoes)
        if (e.key == key)
            return &e;
    return nullptr;
}

juce::uint64 keyForPoint (const juce::String& pointId)
{
    const auto h = (juce::uint64) pointId.hashCode64();
    return h == 0 ? 1 : h;  // 0 is the fragment preview
}

SharedSample renderSample (const SourceRef& ref, const DecodedAudio* source, double hostSampleRate)
{
    if (source == nullptr || source->buffer.getNumSamples() == 0 || hostSampleRate <= 0.0)
        return nullptr;

    const auto total = (juce::int64) source->buffer.getNumSamples();
    const auto start = std::clamp<juce::int64> (ref.trimStart, 0, total - 1);
    const auto end = ref.trimEnd > start ? std::min (ref.trimEnd, total) : total;

    DecodedAudio trimmed;
    trimmed.sampleRate = source->sampleRate;
    trimmed.buffer.setSize (source->buffer.getNumChannels(), (int) (end - start));
    for (int ch = 0; ch < source->buffer.getNumChannels(); ++ch)
        trimmed.buffer.copyFrom (ch, 0, source->buffer, ch, (int) start, (int) (end - start));

    return std::make_shared<const SampleBuffer> (resampled (trimmed, hostSampleRate));
}

std::unique_ptr<PlaybackPlan> buildPlan (const Scene& scene, SharedSample sample, double hostSampleRate)
{
    auto plan = std::make_unique<PlaybackPlan>();
    plan->sampleRate = hostSampleRate;
    plan->sample = std::move (sample);
    plan->gridBeats = beatsPerStep (scene.rhythm);

    const bool anySolo = std::any_of (scene.points.begin(), scene.points.end(),
                                      [] (const Point& p) { return p.enabled && p.solo; });
    for (auto& p : scene.points)
        if (p.enabled && (! anySolo || p.solo))
            plan->echoes.push_back ({ keyForPoint (p.id), clamped (p.timing).beat, placementFor (p, hostSampleRate) });

    return plan;
}

std::unique_ptr<PlaybackPlan> buildPlan (const Scene& scene, const DecodedAudio* source, double hostSampleRate)
{
    auto sample = renderSample (scene.source, source, hostSampleRate);
    if (sample == nullptr)
        return nullptr;
    return buildPlan (scene, std::move (sample), hostSampleRate);
}

// ---------------------------------------------------------------------------

void EchoEngine::prepare (double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    fadeSamples = juce::jmax (1, (int) std::round (kFadeSeconds * sampleRate));
    releaseStep = 1.0f / (float) juce::jmax (1.0, kReleaseSeconds * sampleRate);
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

void EchoEngine::addPending (double referenceBeat, juce::uint64 key) noexcept
{
    auto slot = std::find_if (pending.begin(), pending.end(), [] (auto& p) { return ! p.active; });
    if (slot == pending.end())
    {
        ++engineStats.dropped;  // queue full: drop rather than allocate
        return;
    }
    *slot = { true, referenceBeat, key };
}

void EchoEngine::schedule (const PlaybackPlan& plan, double referenceBeat) noexcept
{
    for (auto& echo : plan.echoes)
    {
        addPending (referenceBeat, echo.key);
        engineStats.lastScheduledBeat = std::max (engineStats.lastScheduledBeat, referenceBeat + echo.beat);
    }
}

bool EchoEngine::targetFor (const Voice& v, const PlaybackPlan& plan, Placement& out) const noexcept
{
    if (v.key == 0)
    {
        out = {};  // fragment preview: centred, unity, unfiltered
        return true;
    }
    if (auto* e = plan.find (v.key))
    {
        out = e->placement;
        return true;
    }
    return false;
}

bool EchoEngine::startVoice (juce::uint64 key, int offset, juce::AudioBuffer<float>& buffer, const PlaybackPlan& plan) noexcept
{
    if (plan.sample == nullptr)
        return false;
    auto slot = std::find_if (voices.begin(), voices.end(), [] (auto& v) { return ! v.active; });
    if (slot == voices.end())
        return false;  // all voices busy: drop rather than cut a sounding voice

    Voice v;
    v.active = true;
    v.buffer = plan.sample.get();
    v.key = key;
    Placement target;
    if (! targetFor (v, plan, target))
        return false;
    v.gainL = target.gainL;
    v.gainR = target.gainR;
    v.coef = target.lowpassCoef;
    *slot = v;
    renderVoice (*slot, buffer, offset, buffer.getNumSamples(), plan);
    return true;
}

void EchoEngine::renderVoice (Voice& v, juce::AudioBuffer<float>& buffer, int start, int end,
                              const PlaybackPlan& plan) noexcept
{
    if (start >= end)
        return;

    // The buffer is only readable if the current plan still owns it.
    if (v.buffer != plan.sample.get() && v.buffer != plan.previousSample.get())
    {
        v.active = false;
        return;
    }

    Placement target;
    const bool found = v.buffer == plan.sample.get() && targetFor (v, plan, target);
    if (! found)
    {
        v.releasing = true;  // point removed/muted, or the fragment changed: fade out
        target = { v.gainL, v.gainR, v.coef };
    }

    const auto& s = *v.buffer;
    const auto length = (juce::int64) s.getNumSamples();
    const int inChannels = s.getNumChannels();
    const int outChannels = buffer.getNumChannels();
    const float fadeDen = (float) (fadeSamples + 1);
    const float span = (float) (end - start);
    const float dL = (target.gainL - v.gainL) / span;
    const float dR = (target.gainR - v.gainR) / span;
    const float dC = (target.lowpassCoef - v.coef) / span;

    for (int i = start; i < end; ++i, ++v.position)
    {
        if (v.position >= length || (v.releasing && v.release <= 0.0f))
        {
            v.active = false;
            return;
        }

        // Linear fade in/out of fadeSamples: first and last samples are near zero, never a step.
        const float fadeIn = std::min (1.0f, (float) (v.position + 1) / fadeDen);
        const float fadeOut = std::min (1.0f, (float) (length - v.position) / fadeDen);
        if (v.releasing)
            v.release -= releaseStep;
        const float env = (wetA + wetStep * (float) i) * std::min (fadeIn, fadeOut) * std::max (0.0f, v.release);
        const auto idx = (int) v.position;

        v.gainL += dL;
        v.gainR += dR;
        v.coef += dC;

        const float a = v.coef;
        if (inChannels == 1)
        {
            v.z[0] += a * (s.getSample (0, idx) - v.z[0]);
            const float x = v.z[0] * env;
            if (outChannels == 1)
                buffer.addSample (0, i, x * 0.5f * (v.gainL + v.gainR));
            else
            {
                buffer.addSample (0, i, x * v.gainL);
                buffer.addSample (1, i, x * v.gainR);
            }
        }
        else
        {
            v.z[0] += a * (s.getSample (0, idx) - v.z[0]);
            v.z[1] += a * (s.getSample (1, idx) - v.z[1]);
            if (outChannels == 1)
                buffer.addSample (0, i, 0.5f * (v.z[0] + v.z[1]) * env * 0.5f * (v.gainL + v.gainR));
            else
            {
                buffer.addSample (0, i, v.z[0] * env * v.gainL);
                buffer.addSample (1, i, v.z[1] * env * v.gainR);
            }
        }
    }

    if (v.position >= length)
        v.active = false;
}

void EchoEngine::process (juce::AudioBuffer<float>& buffer, const PlaybackPlan* plan, const TransportInfo& t,
                          bool triggerRisingEdge, bool previewScene, float wetStart, float wetEnd,
                          bool previewFragment) noexcept
{
    const int n = buffer.getNumSamples();
    wetA = wetStart;
    wetStep = n > 0 ? (wetEnd - wetStart) / (float) n : 0.0f;

    bpmUsed = std::clamp (t.bpm.value_or (kFallbackBpm), 20.0, 999.0);
    const double beatsPerSample = bpmUsed / 60.0 / sampleRate;

    // Host position: ppq if given, otherwise derived from the sample position
    // (constant-tempo assumption) so a host that omits ppq still gets a grid.
    std::optional<double> hostBeat = t.ppq;
    engineStats.ppqDerivedFromSamples = false;
    if (! hostBeat && t.isPlaying && t.timeSamples)
    {
        hostBeat = (double) *t.timeSamples * beatsPerSample;
        engineStats.ppqDerivedFromSamples = true;
    }

    const bool host = t.isPlaying && hostBeat.has_value();
    engineStats.usedHostTimeline = host;
    if (host != onHostTimeline)
    {
        cancelPending();  // the timeline changed meaning
        onHostTimeline = host;
        expectedHostBeat.reset();
    }

    const double blockStart = host ? *hostBeat : internalBeat;
    lastBlockStart = blockStart;
    const double toleranceBeats = std::max (kJumpToleranceSeconds * bpmUsed / 60.0, 1.5 * n * beatsPerSample);
    if (host && expectedHostBeat)
    {
        // B2 fix: a 1e-3 beat threshold treated host rounding and tempo
        // changes inside a block as relocations and cancelled the repetition.
        const double d = blockStart - *expectedHostBeat;
        if (std::abs (d) > toleranceBeats)
        {
            cancelPending();  // loop, jump or relocation
            ++engineStats.jumps;
            engineStats.lastJumpBeats = d;
        }
        else
        {
            engineStats.maxJitterBeats = std::max (engineStats.maxJitterBeats, std::abs (d));
        }
    }

    if (plan != nullptr && plan->sample != nullptr)
    {
        if (triggerRisingEdge)
        {
            // While the host plays, align to the next rhythm step (an automation point at
            // a grid position lands exactly on it). When stopped there is no grid: start now.
            const double grid = plan->gridBeats;
            const double ref = host ? std::ceil (blockStart / grid - 1.0e-6) * grid : blockStart;
            ++engineStats.triggers;
            engineStats.lastTriggerBeat = blockStart;
            engineStats.lastScheduledBeat = ref;
            engineStats.lastReferenceBeat = ref;
            ++engineStats.references;
            schedule (*plan, ref);
        }
        if (previewScene)
        {
            engineStats.lastReferenceBeat = blockStart;
            ++engineStats.references;
            schedule (*plan, blockStart);
        }
        if (previewFragment)
            addPending (blockStart, 0);
    }

    for (auto& v : voices)
    {
        if (! v.active)
            continue;
        if (plan == nullptr)
            v.active = false;  // nothing owns its buffer any more
        else
            renderVoice (v, buffer, 0, n, *plan);
    }

    if (plan != nullptr)
    {
        for (auto& p : pending)
        {
            if (! p.active)
                continue;
            double beat = 0.0;
            if (p.key != 0)
            {
                auto* echo = plan->find (p.key);
                if (echo == nullptr)
                {
                    p.active = false;  // point removed or muted before it played
                    continue;
                }
                beat = echo->beat;
            }
            const double startBeat = p.referenceBeat + beat;
            const double offset = (startBeat - blockStart) / beatsPerSample;
            if (offset < -0.5)
            {
                // The host moved slightly past it (jitter): play it now rather than lose it.
                p.active = false;
                if ((blockStart - startBeat) <= toleranceBeats && startVoice (p.key, 0, buffer, *plan))
                {
                    ++engineStats.fired;
                    ++engineStats.firedLate;
                    engineStats.lastFiredBeat = blockStart;
                }
                else
                {
                    ++engineStats.dropped;
                }
                continue;
            }
            const auto startSample = (int) std::llround (std::max (0.0, offset));
            if (startSample < n)
            {
                p.active = false;
                if (startVoice (p.key, startSample, buffer, *plan))
                {
                    ++engineStats.fired;
                    engineStats.lastFiredBeat = blockStart + startSample * beatsPerSample;
                }
                else
                {
                    ++engineStats.dropped;
                }
            }
        }
    }
    else
    {
        cancelPending();
    }

    const double blockEnd = blockStart + n * beatsPerSample;
    if (host)
        expectedHostBeat = blockEnd;
    else
        internalBeat = blockEnd;
}

} // namespace ses
