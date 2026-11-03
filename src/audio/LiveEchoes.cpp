#include "audio/LiveEchoes.h"

#if defined(__GNUC__)
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wshadow"
 #pragma GCC diagnostic ignored "-Wconversion"
 #pragma GCC diagnostic ignored "-Wsign-conversion"
 #pragma GCC diagnostic ignored "-Wfloat-conversion"
 #pragma GCC diagnostic ignored "-Wfloat-equal"
 #pragma GCC diagnostic ignored "-Wold-style-cast"
 #pragma GCC diagnostic ignored "-Wunused-parameter"
 #pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
#if defined(_MSC_VER)
 #pragma warning (push, 0)
#endif
#include "signalsmith-stretch.h"
#if defined(_MSC_VER)
 #pragma warning (pop)
#endif
#if defined(__GNUC__)
 #pragma GCC diagnostic pop
#endif

#include <cmath>

namespace ses
{

LiveEchoPlan makeLiveEchoPlan (const Scene& scene, double sampleRate)
{
    LiveEchoPlan lp;
    const auto live = clamped (scene.live);
    lp.enabled = live.enabled && live.echoes;
    lp.cycleBeats = live.cycleBeats;
    if (! lp.enabled)
        return lp;

    const bool anySolo = std::any_of (scene.points.begin(), scene.points.end(),
                                      [] (const Point& p) { return p.enabled && p.solo; });
    const bool freeTime = scene.timeMode == TimeMode::Free;
    for (auto& p : scene.points)
    {
        if (! p.enabled || (anySolo && ! p.solo))
            continue;
        const auto timing = clamped (p.timing);
        const double seconds = freeTime ? timing.seconds : timing.beat * 0.5;  // beats judged at 120 BPM
        if (seconds > limits::maxLiveEchoSeconds)
        {
            ++lp.skippedTooFar;
            continue;
        }
        if (lp.numTaps >= (int) lp.taps.size())
        {
            ++lp.skippedTooMany;
            continue;
        }
        auto& t = lp.taps[(size_t) lp.numTaps++];
        t.key = keyForPoint (p.id);
        t.freeTime = freeTime;
        t.delayBeats = timing.beat;
        t.delaySeconds = timing.seconds;
        t.shaper = clamped (p.shaper);
        // Same assumption as the main live path: makeup / auto level at 120 BPM.
        t.shaperLevel = t.shaper.enabled ? shaperLevelGain (t.shaper, live.cycleBeats * 0.5, beatsPerDivision (t.shaper.division) * 0.5) : 1.0f;
        t.effects = clamped (p.effects);
        const auto order = normalisedChainOrder (t.effects.chainOrder);
        for (int i = 0; i < 4; ++i)
            t.order[i] = (char) order[i];
        t.highpassCoef = t.effects.highpassHz > 20.0 ? onePoleCoef (t.effects.highpassHz, sampleRate) : 0.0f;
        t.pitchSemitones = (float) p.sound.pitchSemitones;
        t.formantSemitones = (float) p.sound.formantSemitones;
        t.pitched = renderKeyFor (p.sound) != 0;
        if (p.stutter.enabled)
        {
            const auto st = clamped (p.stutter);
            const auto slices = stutterSlices (st);
            t.sliceCount = juce::jmin ((int) slices.size(), (int) t.sliceBeat.size());
            for (int k = 0; k < t.sliceCount; ++k)
            {
                t.sliceBeat[(size_t) k] = (float) slices[(size_t) k].beat;
                t.sliceLength[(size_t) k] = (float) (slices[(size_t) k].gapBeats * st.sliceLengthPercent / 100.0);
            }
            t.stutterRegion = st.region;
            t.manualStart = st.manualStart;
            t.placement = placementFor (p, sampleRate, st.gainDb);
        }
        else
        {
            t.placement = placementFor (p, sampleRate);
        }
    }
    return lp;
}

//==============================================================================
struct LiveEchoes::Slot
{
    juce::uint64 key = 0;           // 0 = free
    bool assigned = false;          // present in the current plan
    PointFxChain chain;
    signalsmith::stretch::SignalsmithStretch<float> stretch;
    bool pitchOn = false;
    float semis = 0.0f, formant = 0.0f;
    float hz[2] {}, lz[2] {};
    juce::SmoothedValue<float> gainL { 0.0f }, gainR { 0.0f }, presence { 0.0f };
    double curD = -1.0, newD = 0.0;  // read distances (samples) for the delay crossfade
    float xf = 1.0f;                 // 1 = settled on curD
    // Last POD settings, used while the slot fades out / its tail rings after removal.
    float hpCoef = 0.0f, lpCoef = 1.0f;
    PointEffects fx;                 // POD parts only are copied on the audio thread
    char order[4] { 'c', 'm', 'd', 'r' };
};

LiveEchoes::LiveEchoes()
{
    for (auto& s : slots)
        s = std::make_unique<Slot>();
    emptyPlan = std::make_unique<LiveEchoPlan>();
}

LiveEchoes::~LiveEchoes() = default;

void LiveEchoes::prepare (double sampleRate)
{
    sr = sampleRate;
    pitchLatency = 0;
    for (auto& s : slots)
    {
        s->chain.prepare (sampleRate, kChunk);
        s->stretch.presetCheaper (2, (float) sampleRate, true);
        pitchLatency = s->stretch.inputLatency() + s->stretch.outputLatency();
        // Warm the stretcher's internal scratch buffer to its largest size here, so
        // process() never grows it on the audio thread.
        const int warm = (int) (sampleRate * 0.25) + kChunk;
        std::vector<float> zi ((size_t) warm, 0.0f), zo ((size_t) warm, 0.0f);
        float* ins[2] { zi.data(), zi.data() };
        float* outs[2] { zo.data(), zo.data() };
        s->stretch.process (ins, warm, outs, warm);
        s->stretch.reset();
        s->gainL.reset (sampleRate, 0.02);
        s->gainR.reset (sampleRate, 0.02);
        s->presence.reset (sampleRate, 0.02);
    }
    // History: the furthest echo + the stretcher look-ahead + a cycle of Stutter reach.
    historySize = (int) std::ceil (sampleRate * (limits::maxLiveEchoSeconds + 0.5)) + pitchLatency + 2 * kChunk;
    history.setSize (2, historySize);
    tapBuf.setSize (2, kChunk);
    pitchIn.setSize (2, kChunk);
    master.reset (sampleRate, 0.02);
    reset();
}

void LiveEchoes::reset() noexcept
{
    history.clear();
    writePos = 0;
    written = 0;
    blockStartBack = 0;
    master.setCurrentAndTargetValue (0.0f);
    for (auto& s : slots)
    {
        s->key = 0;
        s->assigned = false;
        s->chain.reset();
        if (s->pitchOn)
            s->stretch.reset();
        s->pitchOn = false;
        s->hz[0] = s->hz[1] = s->lz[0] = s->lz[1] = 0.0f;
        s->gainL.setCurrentAndTargetValue (0.0f);
        s->gainR.setCurrentAndTargetValue (0.0f);
        s->presence.setCurrentAndTargetValue (0.0f);
        s->curD = -1.0;
        s->xf = 1.0f;
    }
    active = false;
}

void LiveEchoes::pushInput (const juce::AudioBuffer<float>& in, int n) noexcept
{
    blockStartBack = 0;
    if (historySize <= 0 || in.getNumChannels() < 1 || n <= 0)
        return;
    const int inCh = juce::jmin (2, in.getNumChannels());
    for (int i = 0; i < n; ++i)
    {
        history.setSample (0, writePos, in.getSample (0, i));
        history.setSample (1, writePos, in.getSample (inCh - 1, i));
        if (++writePos >= historySize)
            writePos = 0;
    }
    written += n;
    blockStartBack = n;
}

float LiveEchoes::readHistory (int ch, double samplesAgo) const noexcept
{
    // samplesAgo is measured back from the most recent sample written (0 = it).
    if (samplesAgo < 0.0)
        return 0.0f;
    const double maxBack = (double) juce::jmin<juce::int64> (written, historySize - 2) - 1.0;
    if (samplesAgo > maxBack)
        return 0.0f;
    const auto whole = (int) samplesAgo;
    const auto frac = (float) (samplesAgo - whole);
    int i0 = writePos - 1 - whole;
    if (i0 < 0)
        i0 += historySize;
    int i1 = i0 - 1;
    if (i1 < 0)
        i1 += historySize;
    const float* d = history.getReadPointer (ch);
    return d[i0] + frac * (d[i1] - d[i0]);
}

int LiveEchoes::numActiveTaps() const noexcept
{
    int k = 0;
    for (auto& s : slots)
        k += (s->assigned || s->presence.getCurrentValue() > 0.0f || s->chain.isTailActive()) ? 1 : 0;
    return k;
}

void LiveEchoes::process (juce::AudioBuffer<float>& buffer, int n, const LiveEchoPlan* plan, const TransportInfo& t,
                          double bpm, float wetStart, float wetEnd) noexcept
{
    bpm = juce::jlimit (20.0, 999.0, bpm);
    const double beatsPerSample = bpm / 60.0 / sr;
    const bool onHost = t.isPlaying && t.ppq.has_value();
    const double beatStart = onHost ? *t.ppq : internalBeat;
    internalBeat = beatStart + n * beatsPerSample;

    const bool on = plan != nullptr && plan->enabled && plan->numTaps > 0;
    master.setTargetValue (on ? 1.0f : 0.0f);

    bool anyAlive = on || master.isSmoothing();
    if (! anyAlive)
        for (auto& s : slots)
            anyAlive = anyAlive || s->chain.isTailActive();
    if (! anyAlive)
    {
        if (active)
            reset();  // everything faded and rang out: the next activation starts clean
        return;
    }
    active = true;

    // Slot assignment by point key: a point keeps its chain, stretcher and filters across edits.
    const auto& lp = on ? *plan : *emptyPlan;
    for (auto& s : slots)
        s->assigned = false;
    for (int k = 0; k < lp.numTaps; ++k)
    {
        const auto key = lp.taps[(size_t) k].key;
        Slot* found = nullptr;
        for (auto& s : slots)
            if (s->key == key)
                found = s.get();
        if (found == nullptr)
            for (auto& s : slots)
                if (s->key == 0 || (! s->assigned && s->presence.getCurrentValue() <= 0.0f && ! s->chain.isTailActive()
                                    && std::none_of (lp.taps.begin(), lp.taps.begin() + lp.numTaps,
                                                     [&] (const LiveTap& x) { return x.key == s->key; })))
                {
                    found = s.get();
                    found->key = key;
                    found->curD = -1.0;  // fresh slot: no crossfade from an old position
                    found->chain.reset();
                    found->hz[0] = found->hz[1] = found->lz[0] = found->lz[1] = 0.0f;
                    break;
                }
        if (found != nullptr)
            found->assigned = true;
    }

    const float wetStep = n > 0 ? (wetEnd - wetStart) / (float) n : 0.0f;
    for (int off = 0; off < n; off += kChunk)
    {
        const int m = juce::jmin (kChunk, n - off);
        processChunk (off, m, lp, beatStart + off * beatsPerSample, bpm, buffer, wetStart + wetStep * (float) off, wetStep);
    }
}

void LiveEchoes::processChunk (int offset, int n, const LiveEchoPlan& lp, double beatStart, double bpm,
                               juce::AudioBuffer<float>& out, float wetA, float wetStep) noexcept
{
    const double spb = 60.0 / bpm;           // seconds per beat
    const double samplesPerBeat = spb * sr;
    const double bps = 1.0 / samplesPerBeat;  // beats per sample
    const double cycle = lp.cycleBeats;
    const double cycleSeconds = cycle * spb;
    const int fadeSamples = juce::jmax (1, (int) (sr * EchoEngine::kFadeSeconds));
    const int xfSamples = juce::jmax (1, (int) (sr * 0.02));
    // samplesAgo of sample i of this chunk = back - i (0 = the newest sample in the history).
    const double back = (double) (blockStartBack - 1 - offset);
    const int outChannels = out.getNumChannels();

    // Master fade for the whole chunk (on/off), sampled per sample below.
    float masterBuf[kChunk];
    for (int i = 0; i < n; ++i)
        masterBuf[i] = master.getNextValue();

    for (auto& sp : slots)
    {
        auto& s = *sp;
        const LiveTap* tap = nullptr;
        if (s.assigned)
            for (int k = 0; k < lp.numTaps; ++k)
                if (lp.taps[(size_t) k].key == s.key)
                    tap = &lp.taps[(size_t) k];

        s.presence.setTargetValue (tap != nullptr ? 1.0f : 0.0f);
        const bool sounding = tap != nullptr || s.presence.isSmoothing() || s.presence.getCurrentValue() > 0.0f;
        if (! sounding && ! s.chain.isTailActive())
        {
            if (s.key != 0 && ! s.assigned)
            {
                s.key = 0;  // fully released
                s.pitchOn = false;
            }
            continue;
        }

        float* l = tapBuf.getWritePointer (0);
        float* r = tapBuf.getWritePointer (1);
        float peak = 0.0f;

        if (! sounding)
        {
            std::fill (l, l + n, 0.0f);
            std::fill (r, r + n, 0.0f);
        }
        else
        {
            if (tap != nullptr)
            {
                s.hpCoef = tap->highpassCoef;
                s.lpCoef = tap->placement.lowpassCoef;
                s.gainL.setTargetValue (tap->placement.gainL);
                s.gainR.setTargetValue (tap->placement.gainR);
                s.fx.character = tap->effects.character;
                s.fx.modulation = tap->effects.modulation;
                s.fx.delay = tap->effects.delay;
                s.fx.reverb = tap->effects.reverb;
                s.fx.highpassHz = tap->effects.highpassHz;
                s.fx.lowpassHz = tap->effects.lowpassHz;
                std::copy (tap->order, tap->order + 4, s.order);
            }
            const bool pitched = tap != nullptr && tap->pitched;
            if (pitched && (! s.pitchOn || std::abs (tap->pitchSemitones - s.semis) > 1.0e-6f || std::abs (tap->formantSemitones - s.formant) > 1.0e-6f))
            {
                if (! s.pitchOn)
                    s.stretch.reset();
                s.stretch.setTransposeSemitones (tap->pitchSemitones);
                s.stretch.setFormantSemitones (tap->formantSemitones, false);
                s.semis = tap->pitchSemitones;
                s.formant = tap->formantSemitones;
            }
            if (tap != nullptr)
                s.pitchOn = pitched;  // a removed pitched slot keeps its stretcher running to drain it
            const double look = pitched ? (double) pitchLatency : 0.0;  // the generator runs this far ahead

            // Delay in samples at the tempo in force; a pitched echo is at least the look-ahead away.
            double targetD = s.curD < 0.0 ? 0.0 : s.newD;
            double delayBeats = 0.0;
            if (tap != nullptr)
            {
                targetD = tap->freeTime ? tap->delaySeconds * sr : tap->delayBeats * samplesPerBeat;
                targetD = std::max (targetD, look);
                delayBeats = targetD * bps;
            }
            float* gl = s.pitchOn ? pitchIn.getWritePointer (0) : l;
            float* gr = s.pitchOn ? pitchIn.getWritePointer (1) : r;

            if (tap != nullptr && tap->sliceCount > 0)
            {
                // Stutter: slices of each cycle of the delayed signal.
                for (int i = 0; i < n; ++i)
                {
                    const double v = beatStart + (i + look) * bps - delayBeats;  // delayed beat at the (virtual) output time
                    const double cycleStart = std::floor (v / cycle) * cycle;
                    const double u = v - cycleStart;
                    float a = 0.0f, b = 0.0f;
                    for (int k = 0; k < tap->sliceCount; ++k)
                    {
                        const double sb = tap->sliceBeat[(size_t) k], len = tap->sliceLength[(size_t) k];
                        if (u < sb || u >= sb + len || sb + len > cycle + 1.0e-9)
                            continue;
                        double regionOffset = 0.0;
                        if (tap->stutterRegion == StutterSettings::Region::End)
                            regionOffset = cycle - len;
                        else if (tap->stutterRegion == StutterSettings::Region::Manual)
                            regionOffset = std::min (tap->manualStart * cycle, cycle - len);
                        const double ago = (delayBeats + sb - regionOffset) * samplesPerBeat - look + (back - i);
                        const double pos = (u - sb) * samplesPerBeat, lenS = len * samplesPerBeat;
                        const double fade = std::min ((double) fadeSamples, lenS * 0.5);
                        const float g = (float) std::min ({ 1.0, pos / fade, (lenS - pos) / fade });
                        a = readHistory (0, ago) * g;
                        b = readHistory (1, ago) * g;
                        break;
                    }
                    gl[i] = a;
                    gr[i] = b;
                }
                s.curD = -1.0;  // leaving Stutter starts the continuous echo fresh
            }
            else if (tap == nullptr && (s.pitchOn || s.curD < 0.0))
            {
                // Removed while pitched or stuttering: nothing to continue (its tail still rings).
                std::fill (gl, gl + n, 0.0f);
                std::fill (gr, gr + n, 0.0f);
            }
            else
            {
                const double genD = targetD - look;  // read distance as seen from the virtual output time
                if (s.curD < 0.0)
                {
                    s.curD = s.newD = genD;
                    s.xf = 1.0f;
                }
                else if (std::abs (genD - s.newD) > 0.5)
                {
                    if (s.xf < 1.0f)
                        s.curD = s.newD;  // a new jump during a crossfade: restart from the target
                    s.newD = genD;
                    s.xf = 0.0f;
                }
                for (int i = 0; i < n; ++i)
                {
                    const double now = back - i;
                    float a = readHistory (0, now + s.curD), b = readHistory (1, now + s.curD);
                    if (s.xf < 1.0f)
                    {
                        const float a2 = readHistory (0, now + s.newD), b2 = readHistory (1, now + s.newD);
                        a += (a2 - a) * s.xf;
                        b += (b2 - b) * s.xf;
                        s.xf = std::min (1.0f, s.xf + 1.0f / (float) xfSamples);
                        if (s.xf >= 1.0f)
                            s.curD = s.newD;
                    }
                    gl[i] = a;
                    gr[i] = b;
                }
            }

            if (s.pitchOn)
            {
                float* ins[2] { gl, gr };
                float* outs[2] { l, r };
                s.stretch.process (ins, n, outs, n);
            }

            // Filters, Volume Shaper (on the delayed timeline), position/level, Mix and fades.
            const bool shaped = tap != nullptr && tap->shaper.enabled;
            const double divisionSeconds = shaped ? beatsPerDivision (tap->shaper.division) * spb : 0.0;
            float wet = wetA;
            for (int i = 0; i < n; ++i)
            {
                float x[2] { l[i], r[i] };
                for (int ch = 0; ch < 2; ++ch)
                {
                    if (s.hpCoef > 0.0f)
                    {
                        s.hz[ch] += s.hpCoef * (x[ch] - s.hz[ch]);
                        x[ch] -= s.hz[ch];
                    }
                    s.lz[ch] += s.lpCoef * (x[ch] - s.lz[ch]);
                    x[ch] = s.lz[ch];
                }
                float g = masterBuf[i] * s.presence.getNextValue() * wet;
                wet += wetStep;
                if (shaped)
                {
                    const double v = beatStart + i * bps - delayBeats;
                    const double u = v - std::floor (v / cycle) * cycle;
                    g *= tap->shaperLevel * shaperGain (tap->shaper, u * spb, cycleSeconds, divisionSeconds);
                }
                l[i] = x[0] * g * s.gainL.getNextValue();
                r[i] = x[1] * g * s.gainR.getNextValue();
                peak = juce::jmax (peak, std::abs (l[i]), std::abs (r[i]));
            }
        }

        if (s.fx.usesChain() || s.chain.isTailActive())
        {
            s.chain.noteInput (peak, s.fx);
            s.chain.process (s.fx, s.order, l, r, n, bpm);
        }

        if (outChannels == 1)
            for (int i = 0; i < n; ++i)
                out.addSample (0, offset + i, 0.5f * (l[i] + r[i]));
        else
        {
            out.addFrom (0, offset, l, n);
            out.addFrom (1, offset, r, n);
        }
    }
}

} // namespace ses
