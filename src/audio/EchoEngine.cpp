#include "audio/EchoEngine.h"

#include <algorithm>
#include <cmath>

namespace ses
{

float onePoleCoef (double cutoffHz, double sampleRate) noexcept
{
    return cutoffHz >= std::min (18000.0, 0.45 * sampleRate)
               ? 1.0f
               : (float) (1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * cutoffHz / sampleRate));
}

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
    cutoff = std::min (cutoff, clamped (p.effects).lowpassHz);  // point low-pass effect

    Placement out;
    out.gainL = (float) (level * l);
    out.gainR = (float) (level * r);
    out.lowpassCoef = onePoleCoef (cutoff, sampleRate);
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

int renderKeyFor (const SoundSettings& s) noexcept
{
    const int formantCents = (int) std::lround (std::clamp (s.formantSemitones, limits::minFormant, limits::maxFormant) * 100.0);
    return centsFor (s.pitchSemitones) * 10000 + formantCents;  // |formant| <= 1200 < 5000: unambiguous
}

void decodeRenderKey (int key, double& pitchSemitones, double& formantSemitones) noexcept
{
    const int pitchCents = (int) std::lround (key / 10000.0);
    pitchSemitones = pitchCents / 100.0;
    formantSemitones = (key - pitchCents * 10000) / 100.0;
}

namespace
{
// Count mode: maps a uniform cut position u (0..1) to time x (0..1) and back.
// Accelerating: later cuts shorter. Decelerating: later cuts longer. p >= 1.
double shaperWarp (ShaperSettings::Distribution d, double u, double p) noexcept
{
    switch (d)
    {
        case ShaperSettings::Distribution::Accelerating: return 1.0 - std::pow (std::max (0.0, 1.0 - u), p);
        case ShaperSettings::Distribution::Decelerating: return std::pow (std::max (0.0, u), p);
        default: return u;
    }
}
double shaperUnwarp (ShaperSettings::Distribution d, double x, double p) noexcept
{
    switch (d)
    {
        case ShaperSettings::Distribution::Accelerating: return 1.0 - std::pow (std::max (0.0, 1.0 - x), 1.0 / p);
        case ShaperSettings::Distribution::Decelerating: return std::pow (std::max (0.0, x), 1.0 / p);
        default: return x;
    }
}
}  // namespace

bool shaperSpan (const ShaperSettings& sh, double length, double& start, double& end) noexcept
{
    if (! sh.enabled || length <= 0.0)
        return false;
    start = 0.0;
    end = length;
    if (! sh.wholeVoice)
    {
        start = sh.regionStart * length;
        end = sh.regionEnd * length;
    }
    if (sh.lengthMs > 0.0)
        end = std::min (end, start + sh.lengthMs * 0.001);
    return end > start;
}

float shaperGain (const ShaperSettings& sh, double t, double length, double divisionSeconds) noexcept
{
    double start = 0.0, end = 0.0;
    if (! shaperSpan (sh, length, start, end) || t < start || t >= end)
        return 1.0f;

    const double span = end - start;
    const double local = t - start;  // seconds into the shaped span
    if (sh.custom && sh.curve.size() >= 2)
    {
        double g = curveGainAt (sh.curve, local / span);
        if (! sh.wholeVoice || sh.lengthMs > 0.0)
        {
            const double edge = 0.002;  // 2 ms back to unity at the edges of a partial span
            if (local < edge)     g = std::max (g, 1.0 - local / edge);
            if (end - t < edge)   g = std::max (g, 1.0 - (end - t) / edge);
        }
        return (float) g;
    }
    int cut = 0;
    double cutStart = 0.0, cutEnd = span;
    switch (sh.rateMode)
    {
        case ShaperSettings::RateMode::Count:
        {
            const int n = std::max (2, sh.cuts);
            const double p = 1.0 + 3.0 * std::clamp (sh.accelPercent, 0.0, 100.0) / 100.0;
            const double u = shaperUnwarp (sh.distribution, local / span, p);
            cut = std::clamp ((int) std::floor (u * n), 0, n - 1);
            cutStart = shaperWarp (sh.distribution, (double) cut / n, p) * span;
            cutEnd = shaperWarp (sh.distribution, (double) (cut + 1) / n, p) * span;
            break;
        }
        case ShaperSettings::RateMode::Milliseconds:
        case ShaperSettings::RateMode::Division:
        default:
        {
            const double period = sh.rateMode == ShaperSettings::RateMode::Milliseconds
                                      ? std::max (1.0e-3, sh.periodMs / 1000.0)
                                      : std::max (1.0e-3, divisionSeconds);
            cut = (int) std::floor (local / period);
            cutStart = cut * period;
            cutEnd = std::min (span, cutStart + period);
            break;
        }
    }
    const double cutLen = std::max (1.0e-6, cutEnd - cutStart);
    const double x = std::clamp (local - cutStart, 0.0, cutLen);  // seconds into the cut
    const double phase = x / cutLen;

    // Pattern: 'x' open, '.' muted, repeating over the cuts.
    bool muted = false;
    if (const int n = sh.pattern.length(); n > 0)
        muted = sh.pattern[cut % n] == '.';

    const double attack = std::max (1.0e-5, sh.attackMs * 0.001);
    const double release = std::max (1.0e-5, sh.releaseMs * 0.001);
    double shape = 0.0;  // 0 = closed, 1 = fully open
    if (! muted)
    {
        switch (sh.shape)
        {
            case ShaperSettings::Shape::Square:
            {
                // Open part with linear attack and release ramps inside it. When the
                // opening is shorter than attack + release, both shrink proportionally
                // so the ramps still meet (the cut is then just a short blip).
                const double openLen = cutLen * std::clamp (sh.openPercent, 1.0, 100.0) / 100.0;
                double a = attack, r = release;
                if (a + r > openLen)
                {
                    const double k = openLen / (a + r);
                    a *= k;
                    r *= k;
                }
                if (x >= openLen)            shape = 0.0;
                else if (x < a)              shape = x / a;
                else if (x > openLen - r)    shape = (openLen - x) / r;
                else                         shape = 1.0;
                break;
            }
            case ShaperSettings::Shape::Triangle:
                shape = phase < 0.5 ? 2.0 * phase : 2.0 - 2.0 * phase;  // 0 at the edges, 1 in the middle
                break;
            case ShaperSettings::Shape::Saw:
            default:
            {
                const double a = std::min (attack, 0.5 * cutLen);
                shape = (1.0 - phase) * std::min (1.0, x / a);  // ramps up, then decays to 0
                break;
            }
        }
    }

    const double depth = std::clamp (sh.depthPercent, 0.0, 100.0) / 100.0;
    double g = 1.0 - depth * (1.0 - shape);
    // A span that does not cover the whole voice meets unity gain at its
    // edges: ramp away from 1 at the start and back to 1 at the end.
    if (! sh.wholeVoice || sh.lengthMs > 0.0)
    {
        if (local < attack)       g = std::max (g, 1.0 - local / attack);
        if (end - t < release)    g = std::max (g, 1.0 - (end - t) / release);
    }
    return (float) g;
}

std::vector<CurvePoint> bakeCurve (const ShaperSettings& settings, double spanSeconds, double divisionSeconds, int maxPoints)
{
    // Sample the generated curve densely over the shaped span (region/length
    // already apply to the span, so the samples cover exactly 0..1 of it),
    // then keep only the breakpoints that matter (Douglas-Peucker).
    ShaperSettings s = settings;
    s.enabled = true;
    s.custom = false;
    s.wholeVoice = true;
    s.lengthMs = 0.0;
    const int n = 8192;
    std::vector<CurvePoint> dense ((size_t) n + 1);
    for (int i = 0; i <= n; ++i)
    {
        const double t = (double) i / n;
        dense[(size_t) i] = { t, (double) shaperGain (s, std::min (t, 0.999999) * std::max (1.0e-3, spanSeconds),
                                                      std::max (1.0e-3, spanSeconds), divisionSeconds) };
    }
    std::vector<char> keep ((size_t) n + 1, 0);
    keep.front() = keep.back() = 1;
    std::vector<std::pair<int, int>> stack { { 0, n } };
    const double eps = 0.004;
    while (! stack.empty())
    {
        const auto [a, b] = stack.back();
        stack.pop_back();
        if (b - a < 2)
            continue;
        double worst = 0.0;
        int at = -1;
        for (int i = a + 1; i < b; ++i)
        {
            const double w = (double) (i - a) / (b - a);
            const double lin = dense[(size_t) a].gain + (dense[(size_t) b].gain - dense[(size_t) a].gain) * w;
            const double d = std::abs (dense[(size_t) i].gain - lin);
            if (d > worst) { worst = d; at = i; }
        }
        if (at >= 0 && worst > eps)
        {
            keep[(size_t) at] = 1;
            stack.push_back ({ a, at });
            stack.push_back ({ at, b });
        }
    }
    std::vector<CurvePoint> out;
    for (int i = 0; i <= n; ++i)
        if (keep[(size_t) i])
            out.push_back (dense[(size_t) i]);
    // Too many breakpoints: thin out by keeping the largest deviations only
    // (coarser steps). Rare: 256 square cuts need ~1024 points, so the first
    // pass keeps every ramp and the second merges the shortest segments.
    while ((int) out.size() > std::max (2, maxPoints))
    {
        size_t victim = 1;
        double best = 1.0e9;
        for (size_t i = 1; i + 1 < out.size(); ++i)
        {
            const double d = out[i + 1].t - out[i - 1].t;
            if (d < best) { best = d; victim = i; }
        }
        out.erase (out.begin() + (long) victim);
    }
    return out;
}

float shaperLevelGain (const ShaperSettings& sh, double voiceSeconds, double divisionSeconds)
{
    if (! sh.enabled)
        return 1.0f;
    double gain = juce::Decibels::decibelsToGain (std::clamp (sh.makeupDb, -limits::maxShaperMakeupDb, limits::maxShaperMakeupDb));
    if (sh.autoLevel && voiceSeconds > 0.0)
    {
        // Mean square of the curve over the whole voice (numeric, 4096 steps):
        // the RMS the closed parts remove is restored, up to +6 dB.
        constexpr int steps = 4096;
        double ms = 0.0;
        for (int i = 0; i < steps; ++i)
        {
            const double g = shaperGain (sh, (i + 0.5) / steps * voiceSeconds, voiceSeconds, divisionSeconds);
            ms += g * g;
        }
        ms /= steps;
        const double maxAuto = juce::Decibels::decibelsToGain (limits::maxShaperAutoLevelDb);
        gain *= ms > 1.0e-6 ? std::min (maxAuto, 1.0 / std::sqrt (ms)) : maxAuto;
    }
    // Never more than +12 dB in total: the limiter protects the output, but a
    // curve must not become a dangerous amplifier.
    return (float) std::min (gain, juce::Decibels::decibelsToGain (limits::maxShaperMakeupDb));
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
                                         double hostSampleRate, const PlaybackPlan* previous)
{
    auto plan = std::make_unique<PlaybackPlan>();
    // Effect slots: keep the slot a point had in the previous plan, then fill the free ones.
    auto slotFor = [&] (juce::uint64 key, const PointEffects& fx) -> int
    {
        if (! fx.usesChain())
            return -1;
        for (int i = 0; i < limits::maxFxPoints; ++i)
            if (plan->fx[(size_t) i].key == key)
                return i;
        if (previous != nullptr)
            for (int i = 0; i < limits::maxFxPoints; ++i)
                if (previous->fx[(size_t) i].key == key && plan->fx[(size_t) i].key == 0)
                {
                    plan->fx[(size_t) i] = { key, fx };
                    return i;
                }
        for (int i = 0; i < limits::maxFxPoints; ++i)
            if (plan->fx[(size_t) i].key == 0 && (previous == nullptr || previous->fx[(size_t) i].key == 0
                                                  || std::none_of (scene.points.begin(), scene.points.end(), [&] (const Point& q)
                                                                   { return q.enabled && keyForPoint (q.id) == previous->fx[(size_t) i].key && clamped (q.effects).usesChain(); })))
            {
                plan->fx[(size_t) i] = { key, fx };
                return i;
            }
        for (int i = 0; i < limits::maxFxPoints; ++i)  // all slots belong to live points: take any free one
            if (plan->fx[(size_t) i].key == 0)
            {
                plan->fx[(size_t) i] = { key, fx };
                return i;
            }
        ++plan->pointsWithoutFx;
        return -1;
    };
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
        const int renderKey = renderKeyFor (p.sound);
        if (renderKey != 0)
        {
            if (auto it = pitched.find (renderKey); it != pitched.end() && it->second != nullptr)
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
        const auto effects = clamped (p.effects);
        const float highpassCoef = effects.highpassHz > 20.0 ? onePoleCoef (effects.highpassHz, hostSampleRate) : 0.0f;
        const int fxSlot = slotFor (key, effects);

        // The part of the fragment this repetition plays.
        const auto region = clamped (p.region);
        const auto playStart = std::clamp<juce::int64> ((juce::int64) std::llround (region.start * (double) len), 0, len);
        const auto playEnd = std::clamp<juce::int64> ((juce::int64) std::llround (region.end * (double) len), playStart, len);
        // Makeup / auto level of the curve. Division mode and stutter slices depend
        // on the tempo in force, unknown here: 120 BPM and the full region are used
        // (the duty cycle of a square curve barely depends on either).
        const float shaperLevel = shaperLevelGain (shaper, (double) (playEnd - playStart) / hostSampleRate,
                                                   beatsPerDivision (shaper.division) * 0.5);

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
            e.shaperLevel = shaperLevel;
            e.effects = effects;
            e.highpassCoef = highpassCoef;
            e.fxSlot = fxSlot;
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
        const auto slices = stutterSlices (st);
        for (int k = 0; k < st.repeats; ++k)
        {
            PlannedEcho e;
            e.key = key;
            e.index = 1 + k;
            e.pointBeat = pointBeat;
            e.beat = pointBeat + slices[(size_t) k].beat;
            e.freeTime = freeTime;
            e.seconds = timing.seconds;
            e.shaper = shaper;
            e.shaperLevel = shaperLevel;
            e.effects = effects;
            e.highpassCoef = highpassCoef;
            e.fxSlot = fxSlot;
            e.sample = sample;
            e.pitchPending = pitchPending;
            e.regionStart = regionStart;
            e.regionEnd = regionEnd;
            e.sliceBeats = slices[(size_t) k].gapBeats * st.sliceLengthPercent / 100.0;
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
    for (auto& f : fxSlots)
    {
        f.chain.prepare (sampleRate, kMaxFxBlock);
        f.send.setSize (2, kMaxFxBlock);
        f.send.clear();
        f.key = 0;
    }
    reset();
}

void EchoEngine::reset()
{
    for (auto& v : voices)
        v.active = false;
    for (auto& f : fxSlots)
    {
        f.chain.reset();
        f.key = 0;
        f.fedThisBlock = false;
    }
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

void EchoEngine::renderFx (juce::AudioBuffer<float>& out, const PlaybackPlan* plan, int n) noexcept
{
    if (n > kMaxFxBlock || n <= 0)
        return;
    const int outChannels = out.getNumChannels();
    for (auto& f : fxSlots)
    {
        if (! f.fedThisBlock && ! f.chain.isTailActive())
            continue;
        float* l = f.send.getWritePointer (0);
        float* r = f.send.getWritePointer (1);
        if (f.fedThisBlock)
        {
            float peak = 0.0f;
            for (int i = 0; i < n; ++i)
                peak = std::max (peak, std::max (std::abs (l[i]), std::abs (r[i])));
            f.chain.noteInput (peak, f.effects);
        }
        f.chain.process (f.effects, l, r, n, bpmUsed);
        if (outChannels == 1)
            for (int i = 0; i < n; ++i)
                out.addSample (0, i, 0.5f * (l[i] + r[i]));
        else
        {
            out.addFrom (0, 0, l, n);
            out.addFrom (1, 0, r, n);
        }
    }
    juce::ignoreUnused (plan);
}

int EchoEngine::numActiveFxSlots() const noexcept
{
    int k = 0;
    for (auto& f : fxSlots)
        k += (f.fedThisBlock || f.chain.isTailActive()) ? 1 : 0;
    return k;
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
    v.hpCoef = echo.highpassCoef;
    v.reverse = echo.effects.reverse;
    v.fxSlot = echo.fxSlot;
    *slot = v;
    renderVoice (*slot, buffer, offset, buffer.getNumSamples(), plan, beatsPerSample);
    return true;
}

void EchoEngine::renderVoice (Voice& v, juce::AudioBuffer<float>& output, int start, int end,
                              const PlaybackPlan& plan, double beatsPerSample) noexcept
{
    // Repetitions with an effect chain render into their slot's send buffer
    // (stereo), mixed to the output after the chain. Blocks larger than the
    // send capacity play dry: the chain is skipped, never half-applied.
    const bool toFx = v.fxSlot >= 0 && v.fxSlot < limits::maxFxPoints && output.getNumSamples() <= kMaxFxBlock
                      && plan.fx[(size_t) v.fxSlot].key == v.key;
    auto& buffer = toFx ? fxSlots[(size_t) v.fxSlot].send : output;
    if (toFx)
        fxSlots[(size_t) v.fxSlot].fedThisBlock = true;
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

    // Volume Shaper: evaluated per sample from the voice's own clock. The curve
    // carries its own attack/release ramps, so no extra smoothing is applied
    // (a one-pole here never reached full level inside short openings).
    const ShaperSettings* shaper = (echo != nullptr && echo->shaper.enabled) ? &echo->shaper : nullptr;
    const double voiceLength = (double) (length - v.start) / sampleRate;
    const double divisionSeconds = shaper != nullptr && beatsPerSample > 0.0
                                       ? beatsPerDivision (shaper->division) / beatsPerSample / sampleRate
                                       : 0.0;
    const float shaperLevel = echo != nullptr ? echo->shaperLevel : 1.0f;
    // Point fades (ms), on top of the anticlick fades. Reverse reads the region backwards.
    const float fadeInDen = echo != nullptr ? (float) std::max (1.0, echo->effects.fadeInMs * 0.001 * sampleRate) : 1.0f;
    const float fadeOutDen = echo != nullptr ? (float) std::max (1.0, echo->effects.fadeOutMs * 0.001 * sampleRate) : 1.0f;
    const bool hasFades = echo != nullptr && (echo->effects.fadeInMs > 0.0 || echo->effects.fadeOutMs > 0.0);
    const bool hp = v.hpCoef > 0.0f;

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
        if (hasFades)
            env *= std::min (1.0f, (float) (v.position - v.start + 1) / fadeInDen)
                   * std::min (1.0f, (float) (length - v.position) / fadeOutDen);
        if (shaper != nullptr)
        {
            env *= shaperLevel * shaperGain (*shaper, (double) (v.position - v.start) / sampleRate, voiceLength, divisionSeconds);
        }
        const auto idx = (int) (v.reverse ? v.start + (length - 1) - (v.position - v.start) : v.position);

        v.gainL += dL;
        v.gainR += dR;
        v.coef += dC;

        const float a = v.coef;
        auto in = [&] (int ch) noexcept
        {
            float x = s.getSample (ch, idx);
            if (hp)
            {
                v.hz[ch] += v.hpCoef * (x - v.hz[ch]);
                x -= v.hz[ch];
            }
            return x;
        };
        if (inChannels == 1)
        {
            v.z[0] += a * (in (0) - v.z[0]);
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
            v.z[0] += a * (in (0) - v.z[0]);
            v.z[1] += a * (in (1) - v.z[1]);
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

    // Effect slots follow the plan: a slot whose repetition changed is reset
    // (its old tail stops); one that lost its repetition keeps ringing out.
    for (size_t i = 0; i < fxSlots.size(); ++i)
    {
        auto& f = fxSlots[i];
        f.fedThisBlock = false;
        if (plan != nullptr && plan->fx[i].key != 0)
        {
            if (f.key != plan->fx[i].key)
            {
                f.chain.reset();
                f.key = plan->fx[i].key;
            }
            f.effects = plan->fx[i].effects;
        }
        else if (plan == nullptr)
        {
            f.chain.reset();
            f.key = 0;
        }
        if (n <= kMaxFxBlock && (f.key != 0 || f.chain.isTailActive()))
            f.send.clear (0, n);
    }

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

    renderFx (buffer, plan, n);

    sampleClock += n;
    const double blockEnd = blockStart + n * beatsPerSample;
    if (host)
        expectedHostBeat = blockEnd;
    else
        internalBeat = blockEnd;
}

} // namespace ses
