#include "audio/LiveChain.h"

namespace ses
{

LivePlan makeLivePlan (const LiveSettings& settings, double sampleRate)
{
    const auto live = clamped (settings);
    LivePlan lp;
    lp.enabled = live.enabled;
    lp.wet = (float) (live.wetPercent / 100.0);
    lp.cycleBeats = live.cycleBeats;
    lp.shaper = clamped (live.fx.shaper);
    // Same assumption as the repetitions: makeup / auto level at 120 BPM.
    lp.shaperLevel = lp.shaper.enabled ? shaperLevelGain (lp.shaper, live.cycleBeats * 0.5, beatsPerDivision (lp.shaper.division) * 0.5) : 1.0f;
    lp.effects = clamped (live.fx.effects);
    const auto order = normalisedChainOrder (lp.effects.chainOrder);
    for (int i = 0; i < 4; ++i)
        lp.order[i] = (char) order[i];
    lp.placement = placementFor (live.fx, sampleRate);
    lp.highpassCoef = lp.effects.highpassHz > 20.0 ? onePoleCoef (lp.effects.highpassHz, sampleRate) : 0.0f;
    return lp;
}

void LiveChain::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    maxN = juce::jmax (1, maxBlock);
    chain.prepare (sampleRate, juce::jmin (maxN, EchoEngine::kMaxFxBlock));
    wetBuf.setSize (2, maxN);
    mix.reset (sampleRate, 0.02);
    gainL.reset (sampleRate, 0.02);
    gainR.reset (sampleRate, 0.02);
    reset();
}

void LiveChain::reset() noexcept
{
    chain.reset();
    mix.setCurrentAndTargetValue (0.0f);
    gainL.setCurrentAndTargetValue (1.0f);
    gainR.setCurrentAndTargetValue (1.0f);
    hz[0] = hz[1] = lz[0] = lz[1] = 0.0f;
    active = false;
}

void LiveChain::process (juce::AudioBuffer<float>& buffer, int n, const LivePlan* plan, const TransportInfo& t, double bpm) noexcept
{
    const double beatsPerSample = juce::jmax (1.0, bpm) / 60.0 / sr;
    // Shaper clock: host position while playing, internal clock otherwise.
    const bool onHost = t.isPlaying && t.ppq.has_value();
    const double beatStart = onHost ? *t.ppq : internalBeat;
    internalBeat = beatStart + n * beatsPerSample;

    const bool on = plan != nullptr && plan->enabled;
    mix.setTargetValue (on ? plan->wet : 0.0f);
    if (! on && ! mix.isSmoothing() && mix.getCurrentValue() <= 0.0f)
    {
        if (active)
            reset();  // fully faded out: next activation starts clean
        return;
    }
    if (plan == nullptr || n > maxN || buffer.getNumChannels() < 1)
        return;  // larger blocks than announced pass dry (documented limit)
    active = true;

    const int inCh = juce::jmin (2, buffer.getNumChannels());
    auto* wl = wetBuf.getWritePointer (0);
    auto* wr = wetBuf.getWritePointer (1);
    std::copy (buffer.getReadPointer (0), buffer.getReadPointer (0) + n, wl);
    std::copy (buffer.getReadPointer (inCh - 1), buffer.getReadPointer (inCh - 1) + n, wr);

    const auto& lp = *plan;
    gainL.setTargetValue (lp.placement.gainL);
    gainR.setTargetValue (lp.placement.gainR);
    const float hpc = lp.highpassCoef;
    const float lpc = lp.placement.lowpassCoef;
    const bool shaped = lp.shaper.enabled;
    const double spb = 60.0 / juce::jmax (1.0, bpm);
    const double cycleSeconds = lp.cycleBeats * spb;
    const double divisionSeconds = beatsPerDivision (lp.shaper.division) * spb;
    float peak = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        float x[2] { wl[i], wr[i] };
        for (int ch = 0; ch < 2; ++ch)
        {
            if (hpc > 0.0f)
            {
                hz[ch] += hpc * (x[ch] - hz[ch]);
                x[ch] -= hz[ch];
            }
            lz[ch] += lpc * (x[ch] - lz[ch]);
            x[ch] = lz[ch];
        }
        float g = 1.0f;
        if (shaped)
        {
            const double beat = beatStart + i * beatsPerSample;
            const double inCycle = beat - std::floor (beat / lp.cycleBeats) * lp.cycleBeats;
            g = lp.shaperLevel * shaperGain (lp.shaper, inCycle * spb, cycleSeconds, divisionSeconds);
        }
        wl[i] = x[0] * g * gainL.getNextValue();
        wr[i] = x[1] * g * gainR.getNextValue();
        peak = juce::jmax (peak, std::abs (wl[i]), std::abs (wr[i]));
    }
    lastCycleBeat = beatStart - std::floor (beatStart / lp.cycleBeats) * lp.cycleBeats;

    if (lp.effects.usesChain() || chain.isTailActive())
    {
        chain.noteInput (peak, lp.effects);
        chain.process (lp.effects, lp.order, wl, wr, n, bpm);
    }

    // Dry/wet. A mono track gets the mid of the processed pair.
    for (int i = 0; i < n; ++i)
    {
        const float w = mix.getNextValue();
        if (inCh == 1)
        {
            auto* d = buffer.getWritePointer (0);
            d[i] = d[i] * (1.0f - w) + 0.5f * (wl[i] + wr[i]) * w;
        }
        else
        {
            auto* dl = buffer.getWritePointer (0);
            auto* dr = buffer.getWritePointer (1);
            dl[i] = dl[i] * (1.0f - w) + wl[i] * w;
            dr[i] = dr[i] * (1.0f - w) + wr[i] * w;
        }
    }
}

} // namespace ses
