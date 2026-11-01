#include "audio/EchoEngine.h"

#include <algorithm>
#include <cmath>

namespace ses
{

Placement placementFor (const Point& p, double sampleRate, double extraGainDb)
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
    const double gainDb = std::clamp (p.gainDb + extraGainDb, limits::minGainDb, limits::maxGainDb);
    const double level = juce::Decibels::decibelsToGain (gainDb) * (1.0 - 0.45 * depth);
    double cutoff = 20000.0 * std::pow (0.2, depth) * (behind ? 1.0 - 0.35 * depth : 1.0);

    Placement out;
    out.gainL = (float) (level * l);
    out.gainR = (float) (level * r);
    out.lowpassCoef = cutoff >= std::min (18000.0, 0.45 * sampleRate)
                          ? 1.0f
                          : (float) (1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * cutoff / sampleRate));
    return out;
}

const PlannedEcho* PlaybackPlan::find (juce::uint64 key, int index) const noexcept
{
    for (auto& e : echoes)
        if (e.key == key && e.index == index)
            return &e;
    return nullptr;
}

bool PlaybackPlan::owns (const SampleBuffer* b) const noexcept
{
    if (b == nullptr)
        return false;
    if (b == sample.get())
        return true;
    for (auto& s : owned)
        if (s.get() == b)
            return true;
    return false;
}

juce::uint64 keyForPoint (const juce::String& pointId)
{
    const auto h = (juce::uint64) pointId.hashCode64();
    return h == 0 ? 1 : h;  // 0 is the fragment preview
}

int centsFor (double semitones) noexcept
{
    return (int) std::lround (std::clamp (semitones, limits::minPitch, limits::maxPitch) * 100.0);
}

float shaperGain (const ShaperSettings& sh, double t, double length, double divisionSeconds) noexcept
{
    if (! sh.enabled || length <= 0.0)
        return 1.0f;
    double start = 0.0, end = length;
    if (! sh.wholeVoice)
    {
        start = sh.regionStart * length;
        end = sh.regionEnd * length;
    }
    if (t < start || t >= end || end <= start)
        return 1.0f;

    const double span = end - start;
    const double x = (t - start) / span;  // 0..1 across the shaped region
    double phase;                          // 0..1 inside the current cut
    switch (sh.rateMode)
    {
        case ShaperSettings::RateMode::Count:
        {
            // Acceleration warps time so later cuts are shorter, cuts stay uniform in u.
            const double pw = 1.0 + 3.0 * sh.accelPercent / 100.0;
            const double u = sh.accelPercent > 0.0 ? 1.0 - std::pow (std::max (0.0, 1.0 - x), 1.0 / pw) : x;
            const double c = u * sh.cuts;
            phase = c - std::floor (c);
            break;
        }
        case ShaperSettings::RateMode::Milliseconds:
        {
            const double c = (t - start) / (sh.periodMs / 1000.0);
            phase = c - std::floor (c);
            break;
        }
        case ShaperSettings::RateMode::Division:
        default:
        {
            const double period = std::max (1.0e-3, divisionSeconds);
            const double c = (t - start) / period;
            phase = c - std::floor (c);
            break;
        }
    }

    const double depth = sh.depthPercent / 100.0;
    switch (sh.shape)
    {
        case ShaperSettings::Shape::Square:
            return phase < sh.openPercent / 100.0 ? 1.0f : (float) (1.0 - depth);
        case ShaperSettings::Shape::Triangle:
        {
            const double tri = phase < 0.5 ? 2.0 * phase : 2.0 - 2.0 * phase;  // 0 at the edges, 1 in the middle
            return (float) (1.0 - depth * (1.0 - tri));
        }
        case ShaperSettings::Shape::Saw:
        default:
            return (float) (1.0 - depth * phase);
    }
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

std::unique_ptr<PlaybackPlan> buildPlan (const Scene& scene, SharedSample base, const std::map<int, SharedSample>& pitched,
                                         double hostSampleRate)
{
    auto plan = std::make_unique<PlaybackPlan>();
    plan->sampleRate = hostSampleRate;
    plan->sample = base;
    plan->gridBeats = beatsPerStep (scene.rhythm);
    const auto length = base != nullptr ? (juce::int64) base->getNumSamples() : 0;

    const bool anySolo = std::any_of (scene.points.begin(), scene.points.end(),
                                      [] (const Point& p) { return p.enabled && p.solo; });
    for (auto& p : scene.points)
    {
        if (! p.enabled || (anySolo && ! p.solo))
            continue;

        SharedSample sample = base;
        bool pitchPending = false;
        const int cents = centsFor (p.sound.pitchSemitones);
        if (cents != 0)
        {
            if (auto it = pitched.find (cents); it != pitched.end() && it->second != nullptr)
                sample = it->second;
            else
            {
                pitchPending = true;
                ++plan->pendingPitchRenders;
            }
        }
        if (sample != nullptr && ! plan->owns (sample.get()))
            plan->owned.push_back (sample);

        const auto key = keyForPoint (p.id);
        const auto timing = clamped (p.timing);
        const bool freeTime = scene.timeMode == TimeMode::Free;
        const double pointBeat = freeTime ? 0.0 : timing.beat;
        const auto len = sample != nullptr ? (juce::int64) sample->getNumSamples() : length;
        const auto shaper = clamped (p.shaper);

        // The part of the fragment this repetition plays.
        const auto region = clamped (p.region);
        const auto playStart = std::clamp<juce::int64> ((juce::int64) std::llround (region.start * (double) len), 0, len);
        const auto playEnd = std::clamp<juce::int64> ((juce::int64) std::llround (region.end * (double) len), playStart, len);

        if (! p.stutter.enabled)
        {
            PlannedEcho e;
            e.key = key;
            e.index = 0;
            e.pointBeat = e.beat = pointBeat;
            e.freeTime = freeTime;
            e.seconds = timing.seconds;
            e.sample = sample;
            e.pitchPending = pitchPending;
            e.regionStart = playStart;
            e.regionEnd = playEnd;
            e.placement = placementFor (p, hostSampleRate);
            e.shaper = shaper;
            plan->echoes.push_back (std::move (e));
            continue;
        }

        const auto st = clamped (p.stutter);
        const double div = beatsPerDivision (st.division);
        juce::int64 regionStart = playStart, regionEnd = playEnd;
        bool alignEnd = false;
        const auto playLen = (double) (playEnd - playStart);
        switch (st.region)
        {
            case StutterSettings::Region::Whole:  break;
            case StutterSettings::Region::Start:  break;
            case StutterSettings::Region::End:    alignEnd = true; break;
            case StutterSettings::Region::Manual:
                regionStart = playStart + (juce::int64) std::llround (st.manualStart * playLen);
                regionEnd = std::max (regionStart + 1, playStart + (juce::int64) std::llround (st.manualEnd * playLen));
                regionEnd = std::min (regionEnd, playEnd);
                break;
        }
        const auto placement = placementFor (p, hostSampleRate, st.gainDb);
        for (int k = 0; k < st.repeats; ++k)
        {
            PlannedEcho e;
            e.key = key;
            e.index = 1 + k;
            e.pointBeat = pointBeat;
            e.beat = pointBeat + st.offsetBeats + k * div;
            e.freeTime = freeTime;
            e.seconds = timing.seconds;
            e.shaper = shaper;
            e.sample = sample;
            e.pitchPending = pitchPending;
            e.regionStart = regionStart;
            e.regionEnd = regionEnd;
            e.sliceBeats = div * st.sliceLengthPercent / 100.0;
            e.alignEnd = alignEnd;
            e.placement = placement;
            plan->echoes.push_back (std::move (e));
        }
    }

    return plan;
}

std::unique_ptr<PlaybackPlan> buildPlan (const Scene& scene, const DecodedAudio* source, double hostSampleRate)
{
    auto sample = renderSample (scene.source, source, hostSampleRate);
    if (sample == nullptr)
        return nullptr;
    return buildPlan (scene, std::move (sample), {}, hostSampleRate);
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
    sampleClock = 0.0;
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

void EchoEngine::addPending (double referenceBeat, double referenceSample, juce::uint64 key, int index) noexcept
{
    auto slot = std::find_if (pending.begin(), pending.end(), [] (auto& p) { return ! p.active; });
    if (slot == pending.end())
    {
        ++engineStats.dropped;  // queue full: drop rather than allocate
        return;
    }
    *slot = { true, referenceBeat, referenceSample, key, index };
}

void EchoEngine::schedule (const PlaybackPlan& plan, double referenceBeat, double referenceSample, juce::uint64 onlyKey) noexcept
{
    for (auto& echo : plan.echoes)
    {
        if (onlyKey != 0 && echo.key != onlyKey)
            continue;
        addPending (referenceBeat, referenceSample, echo.key, echo.index);
        engineStats.lastScheduledBeat = std::max (engineStats.lastScheduledBeat, referenceBeat + echo.beat);
    }
}

bool EchoEngine::startVoice (const PlannedEcho& echo, int offset, juce::AudioBuffer<float>& buffer, const PlaybackPlan& plan,
                             double beatsPerSample) noexcept
{
    if (echo.sample == nullptr)
        return false;
    auto slot = std::find_if (voices.begin(), voices.end(), [] (auto& v) { return ! v.active; });
    if (slot == voices.end())
        return false;  // all voices busy: drop rather than cut a sounding voice

    const auto len = (juce::int64) echo.sample->getNumSamples();
    auto regionStart = std::clamp<juce::int64> (echo.regionStart, 0, len);
    auto regionEnd = std::clamp<juce::int64> (echo.regionEnd > 0 ? echo.regionEnd : len, regionStart, len);
    if (echo.sliceBeats > 0.0 && beatsPerSample > 0.0)
    {
        const auto sliceSamples = std::max<juce::int64> (1, (juce::int64) std::llround (echo.sliceBeats / beatsPerSample));
        if (echo.alignEnd)
            regionStart = std::max (regionStart, regionEnd - sliceSamples);
        else
            regionEnd = std::min (regionEnd, regionStart + sliceSamples);
    }
    if (regionEnd <= regionStart)
        return false;

    Voice v;
    v.active = true;
    v.buffer = echo.sample.get();
    v.key = echo.key;
    v.index = echo.index;
    v.start = v.position = regionStart;
    v.end = regionEnd;
    v.gainL = echo.placement.gainL;
    v.gainR = echo.placement.gainR;
    v.coef = echo.placement.lowpassCoef;
    v.shaperEnv = 1.0f;
    *slot = v;
    renderVoice (*slot, buffer, offset, buffer.getNumSamples(), plan, beatsPerSample);
    return true;
}

void EchoEngine::renderVoice (Voice& v, juce::AudioBuffer<float>& buffer, int start, int end,
                              const PlaybackPlan& plan, double beatsPerSample) noexcept
{
    if (start >= end)
        return;

    // The buffer is only readable if the current plan still owns it.
    if (! plan.owns (v.buffer))
    {
        v.active = false;
        return;
    }

    Placement target { v.gainL, v.gainR, v.coef };
    const PlannedEcho* echo = v.key == 0 ? nullptr : plan.find (v.key, v.index);
    if (v.key == 0)
        target = {};  // fragment preview: centred, unity, unfiltered
    else if (echo != nullptr && echo->sample.get() == v.buffer)
        target = echo->placement;
    else
        v.releasing = true;  // point removed/muted, or its fragment/pitch changed: fade out

    const auto& s = *v.buffer;
    const auto length = std::min<juce::int64> (v.end, s.getNumSamples());
    const int inChannels = s.getNumChannels();
    const int outChannels = buffer.getNumChannels();
    const float fadeDen = (float) (fadeSamples + 1);
    const float span = (float) (end - start);
    const float dL = (target.gainL - v.gainL) / span;
    const float dR = (target.gainR - v.gainR) / span;
    const float dC = (target.lowpassCoef - v.coef) / span;

    // Volume Shaper: evaluated per sample from the voice's own clock, then
    // smoothed (one-pole, fadeMs) so every edge is a short fade, never a step.
    const ShaperSettings* shaper = (echo != nullptr && echo->shaper.enabled) ? &echo->shaper : nullptr;
    const double voiceLength = (double) (length - v.start) / sampleRate;
    const double divisionSeconds = shaper != nullptr && beatsPerSample > 0.0
                                       ? beatsPerDivision (shaper->division) / beatsPerSample / sampleRate
                                       : 0.0;
    const float shaperCoef = shaper != nullptr && shaper->fadeMs > 0.0
                                 ? (float) (1.0 - std::exp (-1.0 / (shaper->fadeMs * 0.001 * sampleRate)))
                                 : 1.0f;

    for (int i = start; i < end; ++i, ++v.position)
    {
        if (v.position >= length || (v.releasing && v.release <= 0.0f))
        {
            v.active = false;
            return;
        }

        // Linear fade in/out of fadeSamples: first and last samples are near zero, never a step.
        const float fadeIn = std::min (1.0f, (float) (v.position - v.start + 1) / fadeDen);
        const float fadeOut = std::min (1.0f, (float) (length - v.position) / fadeDen);
        if (v.releasing)
            v.release -= releaseStep;
        float env = (wetA + wetStep * (float) i) * std::min (fadeIn, fadeOut) * std::max (0.0f, v.release);
        if (shaper != nullptr)
        {
            const float g = shaperGain (*shaper, (double) (v.position - v.start) / sampleRate, voiceLength, divisionSeconds);
            v.shaperEnv += (g - v.shaperEnv) * shaperCoef;
            env *= v.shaperEnv;
        }
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
                          const EngineRequests& req, float wetStart, float wetEnd) noexcept
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
    const double clock = sampleClock;
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
        if (req.triggerRisingEdge)
        {
            // While the host plays, align to the next rhythm step (an automation point at
            // a grid position lands exactly on it). When stopped there is no grid: start now.
            const double grid = plan->gridBeats;
            const double ref = host ? std::ceil (blockStart / grid - 1.0e-6) * grid : blockStart;
            ++engineStats.triggers;
            engineStats.lastTriggerBeat = blockStart;
            engineStats.lastScheduledBeat = ref;
            engineStats.lastReferenceBeat = ref;
            const double refSample = clock + (ref - blockStart) / beatsPerSample;
            engineStats.lastReferenceSeconds = refSample / sampleRate;
            ++engineStats.references;
            schedule (*plan, ref, refSample);
        }
        if (req.previewScene)
        {
            engineStats.lastReferenceBeat = blockStart;
            engineStats.lastReferenceSeconds = clock / sampleRate;
            ++engineStats.references;
            schedule (*plan, blockStart, clock);
        }
        if (req.triggerPoint != 0)
        {
            // Play this point now: shift the reference so its own moment lands on this block.
            double pointBeat = 0.0, pointSeconds = 0.0;
            for (auto& e : plan->echoes)
                if (e.key == req.triggerPoint)
                {
                    pointBeat = e.pointBeat;
                    pointSeconds = e.freeTime ? e.seconds : 0.0;
                    break;
                }
            schedule (*plan, blockStart - pointBeat, clock - pointSeconds * sampleRate, req.triggerPoint);
        }
        if (req.previewFragment)
            addPending (blockStart, clock, 0, 0);
    }

    for (auto& v : voices)
    {
        if (! v.active)
            continue;
        if (plan == nullptr)
            v.active = false;  // nothing owns its buffer any more
        else
            renderVoice (v, buffer, 0, n, *plan, beatsPerSample);
    }

    if (plan != nullptr)
    {
        PlannedEcho preview;  // fragment preview: whole base sample, centred
        preview.sample = plan->sample;
        preview.regionEnd = plan->sample != nullptr ? plan->sample->getNumSamples() : 0;

        for (auto& p : pending)
        {
            if (! p.active)
                continue;
            const PlannedEcho* echo = &preview;
            if (p.key != 0)
            {
                echo = plan->find (p.key, p.index);
                if (echo == nullptr)
                {
                    p.active = false;  // point removed, muted or re-shaped before it played
                    continue;
                }
            }
            // Beats mode: the echo's beat counts from the reference beat (follows the
            // host tempo). Free time: its seconds count on the sample clock, plus any
            // stutter offset in beats at the tempo in force.
            const double offset = echo->freeTime
                                      ? (p.referenceSample + echo->seconds * sampleRate - clock) + echo->beat / beatsPerSample
                                      : (p.referenceBeat + echo->beat - blockStart) / beatsPerSample;
            const double startBeat = blockStart + offset * beatsPerSample;
            if (offset < -0.5)
            {
                // The host moved slightly past it (jitter): play it now rather than lose it.
                p.active = false;
                if ((blockStart - startBeat) <= toleranceBeats && startVoice (*echo, 0, buffer, *plan, beatsPerSample))
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
                if (startVoice (*echo, startSample, buffer, *plan, beatsPerSample))
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

    sampleClock += n;
    const double blockEnd = blockStart + n * beatsPerSample;
    if (host)
        expectedHostBeat = blockEnd;
    else
        internalBeat = blockEnd;
}

} // namespace ses
