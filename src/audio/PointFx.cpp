#include "audio/PointFx.h"

#include "audio/EchoEngine.h"

#include <algorithm>
#include <cmath>

namespace ses
{
namespace
{
constexpr double kModMaxSeconds = 0.05;   // chorus/flanger line
constexpr float kFlangerFeedback = 0.6f;

// Per-sample step that takes `current` to `target` over the block. Before the
// chain has processed anything (`primed` false) the value jumps straight to
// the target: nothing is sounding yet, so there is nothing to click.
inline float ramp (float& current, float target, int n, bool primed) noexcept
{
    if (! primed)
    {
        current = target;
        return 0.0f;
    }
    return (target - current) / (float) std::max (1, n);
}

inline float readLine (const std::vector<float>& buf, int writePos, double delaySamples) noexcept
{
    const int size = (int) buf.size();
    const double d = std::clamp (delaySamples, 1.0, (double) size - 2.0);
    const int i0 = (int) d;
    const float frac = (float) (d - i0);
    int p0 = writePos - i0;
    int p1 = p0 - 1;
    if (p0 < 0) p0 += size;
    if (p1 < 0) p1 += size;
    return buf[(size_t) p0] + (buf[(size_t) p1] - buf[(size_t) p0]) * frac;
}
}  // namespace

void PointFxChain::prepare (double sampleRate, int)
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    for (auto& b : modBuf)
        b.assign ((size_t) std::ceil (kModMaxSeconds * sr) + 4, 0.0f);
    for (auto& b : dlBuf)
        b.assign ((size_t) std::ceil (limits::maxDelayMs * 0.001 * sr) + 4, 0.0f);
    for (auto& b : preBuf)
        b.assign ((size_t) std::ceil (limits::maxPredelayMs * 0.001 * sr) + 4, 0.0f);
    reverb.setSampleRate (sr);
    for (int ch = 0; ch < 2; ++ch)
    {
        telHp[ch].setCoefficients (juce::IIRCoefficients::makeHighPass (sr, 300.0));
        telLp[ch].setCoefficients (juce::IIRCoefficients::makeLowPass (sr, 3400.0));
    }
    reset();
}

void PointFxChain::reset() noexcept
{
    // The 2 s delay line is not cleared (16 slots x 1.5 MB at 96 kHz took
    // milliseconds on the audio thread): `dlValid` makes older history read as 0.
    for (auto* set : { modBuf, preBuf })
        for (int ch = 0; ch < 2; ++ch)
            std::fill (set[ch].begin(), set[ch].end(), 0.0f);
    modWrite = dlWrite = preWrite = 0;
    dlValid = 0;
    lfoPhase = ringPhase = 0.0;
    for (int ch = 0; ch < 2; ++ch)
    {
        lofiLp[ch] = hold[ch] = flangeFb[ch] = dlLp[ch] = 0.0f;
        telHp[ch].reset();
        telLp[ch].reset();
    }
    holdCount = 0;
    reverb.reset();
    reverbActive = false;
    dlSamples = 0.0;
    mixC = mixM = mixD = mixR = fbD = depthM = 0.0f;
    outR = 1.0f;
    tailLeft = 0;
    primed = false;
}

double PointFxChain::tailSeconds (const PointEffects& fx) noexcept
{
    double t = 0.0;
    if (fx.reverb.enabled)
        t = std::max (t, 0.5 + 9.5 * fx.reverb.decayPercent / 100.0 + fx.reverb.predelayMs * 0.001);
    if (fx.delay.enabled)
    {
        const double fb = std::clamp (fx.delay.feedbackPercent / 100.0, 0.0, 0.95);
        const double repeats = fb > 0.001 ? std::ceil (std::log (1.0e-4) / std::log (fb)) : 1.0;
        t = std::max (t, (fx.delay.sync ? 2.0 : fx.delay.timeMs * 0.001) * std::clamp (repeats, 1.0, 60.0));
    }
    if (fx.modulation.enabled)
        t = std::max (t, 0.1);
    return std::min (t, 20.0);
}

void PointFxChain::noteInput (float peak, const PointEffects& fx) noexcept
{
    if (peak > 1.0e-6f)
        tailLeft = std::max (tailLeft, (int) (tailSeconds (fx) * sr) + 1024);
}

void PointFxChain::process (const PointEffects& fx, float* l, float* r, int n, double bpm) noexcept
{
    char order[4] { 'c', 'm', 'd', 'r' };
    const auto s = normalisedChainOrder (fx.chainOrder);  // message thread / tests only
    for (int i = 0; i < 4; ++i)
        order[i] = (char) s[i];
    process (fx, order, l, r, n, bpm);
}

void PointFxChain::process (const PointEffects& fx, const char order[4], float* l, float* r, int n, double bpm) noexcept
{
    // Stages run in the point's order (a permutation of "cmdr", validated on
    // the message thread; unknown characters are ignored here).
    for (int i = 0; i < 4; ++i)
    {
        switch (order[i])
        {
            case 'c':
                if (fx.character.enabled)
                    processCharacter (fx.character, l, r, n);
                break;
            case 'm':
                if (fx.modulation.enabled)
                    processModulation (fx.modulation, l, r, n);
                break;
            case 'd':
                if (fx.delay.enabled)
                    processDelay (fx.delay, l, r, n, bpm);
                else
                    fbD = 0.0f;
                break;
            case 'r':
                if (fx.reverb.enabled)
                    processReverb (fx.reverb, l, r, n);
                else if (reverbActive)
                {
                    reverb.reset();  // switched off: the tail stops (the voice itself still sounds)
                    reverbActive = false;
                }
                break;
            default: break;
        }
    }
    tailLeft = std::max (0, tailLeft - n);
    primed = true;
}

// ---- character --------------------------------------------------------------

void PointFxChain::processCharacter (const CharacterSettings& c, float* l, float* r, int n) noexcept
{
    const float a = (float) std::clamp (c.amountPercent / 100.0, 0.0, 1.0);
    const float targetMix = (float) std::clamp (c.mixPercent / 100.0, 0.0, 1.0);
    const float mixStep = ramp (mixC, targetMix, n, primed);
    float* ch[2] { l, r };

    switch (c.type)
    {
        case CharacterSettings::Type::Saturation:
        {
            // tanh drive, trimmed so a typical 0.25-peak voice keeps its level.
            const float d = 1.0f + 9.0f * a;
            const float trim = 0.25f / std::tanh (0.25f * d);
            for (int i = 0; i < n; ++i)
            {
                mixC += mixStep;
                for (auto* x : ch)
                {
                    const float wet = std::tanh (x[i] * d) * trim;
                    x[i] += (wet - x[i]) * mixC;
                }
            }
            break;
        }
        case CharacterSettings::Type::Overdrive:
        {
            const float d = 1.0f + 19.0f * a;
            auto shape = [] (float u) noexcept { u = std::clamp (u, -1.5f, 1.5f); return u - u * u * u / 6.75f; };
            const float trim = 0.25f / shape (0.25f * d);
            for (int i = 0; i < n; ++i)
            {
                mixC += mixStep;
                for (int k = 0; k < 2; ++k)
                {
                    float wet = shape (ch[k][i] * d) * trim;
                    lofiLp[k] += 0.35f * (wet - lofiLp[k]);  // takes the fizz off the hard edge
                    wet = lofiLp[k];
                    ch[k][i] += (wet - ch[k][i]) * mixC;
                }
            }
            break;
        }
        case CharacterSettings::Type::Bitcrusher:
        {
            const float bits = 12.0f - 9.0f * a;
            const float step = std::pow (2.0f, 1.0f - bits);
            const int holdEvery = 1 + (int) (15.0f * a);
            for (int i = 0; i < n; ++i)
            {
                mixC += mixStep;
                if (holdCount <= 0)
                {
                    for (int k = 0; k < 2; ++k)
                        hold[k] = std::round (ch[k][i] / step) * step;
                    holdCount = holdEvery;
                }
                --holdCount;
                for (int k = 0; k < 2; ++k)
                    ch[k][i] += (hold[k] - ch[k][i]) * mixC;
            }
            break;
        }
        case CharacterSettings::Type::Telephone:
        {
            const float d = 1.0f + 4.0f * a;
            const float trim = 0.25f / std::tanh (0.25f * d);
            for (int i = 0; i < n; ++i)
            {
                mixC += mixStep;
                for (int k = 0; k < 2; ++k)
                {
                    float wet = telLp[k].processSingleSampleRaw (telHp[k].processSingleSampleRaw (ch[k][i]));
                    wet = std::tanh (wet * d) * trim * 1.4f;
                    ch[k][i] += (wet - ch[k][i]) * mixC;
                }
            }
            break;
        }
        case CharacterSettings::Type::LoFi:
        {
            const int holdEvery = 1 + (int) (7.0f * a);
            const float cutoff = 9000.0f - 7000.0f * a;
            const float coef = onePoleCoef (cutoff, sr);
            const float d = 1.0f + 2.0f * a;
            const float trim = 0.25f / std::tanh (0.25f * d);
            for (int i = 0; i < n; ++i)
            {
                mixC += mixStep;
                if (holdCount <= 0)
                {
                    for (int k = 0; k < 2; ++k)
                        hold[k] = ch[k][i];
                    holdCount = holdEvery;
                }
                --holdCount;
                for (int k = 0; k < 2; ++k)
                {
                    lofiLp[k] += coef * (hold[k] - lofiLp[k]);
                    const float wet = std::tanh (lofiLp[k] * d) * trim;
                    ch[k][i] += (wet - ch[k][i]) * mixC;
                }
            }
            break;
        }
        case CharacterSettings::Type::Robot:
        default:
        {
            // Ring modulation: the voice times a sine carrier. The fundamental
            // disappears and sum/difference tones give the metallic, pitch-less voice.
            const double carrierHz = 20.0 + 200.0 * a;
            const double inc = juce::MathConstants<double>::twoPi * carrierHz / sr;
            for (int i = 0; i < n; ++i)
            {
                mixC += mixStep;
                const float m = (float) std::sin (ringPhase);
                ringPhase += inc;
                if (ringPhase > juce::MathConstants<double>::twoPi)
                    ringPhase -= juce::MathConstants<double>::twoPi;
                for (auto* x : ch)
                    x[i] += (x[i] * m - x[i]) * mixC;
            }
            break;
        }
    }
}

// ---- modulation --------------------------------------------------------------

void PointFxChain::processModulation (const ModulationSettings& m, float* l, float* r, int n) noexcept
{
    const float depthTarget = (float) std::clamp (m.depthPercent / 100.0, 0.0, 1.0);
    const float depthStep = ramp (depthM, depthTarget, n, primed);
    const float mixTarget = m.type == ModulationSettings::Type::Tremolo ? 1.0f : (float) std::clamp (m.mixPercent / 100.0, 0.0, 1.0);
    const float mixStep = ramp (mixM, mixTarget, n, primed);
    const double inc = juce::MathConstants<double>::twoPi * std::clamp (m.rateHz, 0.05, 20.0) / sr;
    const int size = (int) modBuf[0].size();
    float* ch[2] { l, r };

    if (m.type == ModulationSettings::Type::Tremolo)
    {
        for (int i = 0; i < n; ++i)
        {
            depthM += depthStep;
            const float lfo = 0.5f - 0.5f * (float) std::cos (lfoPhase);
            lfoPhase += inc;
            if (lfoPhase > juce::MathConstants<double>::twoPi)
                lfoPhase -= juce::MathConstants<double>::twoPi;
            const float g = 1.0f - depthM * lfo;
            l[i] *= g;
            r[i] *= g;
        }
        return;
    }

    const bool flanger = m.type == ModulationSettings::Type::Flanger;
    const double centreMs = flanger ? 2.5 : 15.0;
    const double depthMs = flanger ? 2.0 : 8.0;
    for (int i = 0; i < n; ++i)
    {
        depthM += depthStep;
        mixM += mixStep;
        const float lfoL = (float) std::sin (lfoPhase);
        const float lfoR = (float) std::sin (lfoPhase + juce::MathConstants<double>::halfPi);
        lfoPhase += inc;
        if (lfoPhase > juce::MathConstants<double>::twoPi)
            lfoPhase -= juce::MathConstants<double>::twoPi;
        const float lfo[2] { lfoL, lfoR };
        for (int k = 0; k < 2; ++k)
        {
            const float in = ch[k][i] + (flanger ? kFlangerFeedback * flangeFb[k] : 0.0f);
            modBuf[k][(size_t) modWrite] = in;
            const double delayMs = centreMs + depthMs * depthM * (0.5 + 0.5 * lfo[k]);
            const float wet = readLine (modBuf[k], modWrite, delayMs * 0.001 * sr);
            flangeFb[k] = wet;
            ch[k][i] += (wet - ch[k][i]) * mixM * (flanger ? 1.0f : 0.5f) + (flanger ? 0.0f : wet * mixM * 0.5f);
        }
        if (++modWrite >= size)
            modWrite = 0;
    }
}

// ---- delay ---------------------------------------------------------------------

void PointFxChain::processDelay (const DelaySettings& d, float* l, float* r, int n, double bpm) noexcept
{
    const double seconds = d.sync ? beatsPerDivision (d.division) * 60.0 / std::clamp (bpm, 20.0, 999.0)
                                  : d.timeMs * 0.001;
    const int size = (int) dlBuf[0].size();
    const double target = std::clamp (seconds * sr, 1.0, (double) size - 2.0);
    if (dlSamples <= 0.0)
        dlSamples = target;  // first use: no glide from zero
    const float fbTarget = (float) std::clamp (d.feedbackPercent / 100.0, 0.0, 0.95);
    const float fbStep = ramp (fbD, fbTarget, n, primed);
    const float mixTarget = (float) std::clamp (d.mixPercent / 100.0, 0.0, 1.0);
    const float mixStep = ramp (mixD, mixTarget, n, primed);
    const float lpCoef = onePoleCoef (d.lowpassHz, sr);
    constexpr double glide = 0.0008;  // per-sample approach: time changes slide instead of clicking

    for (int i = 0; i < n; ++i)
    {
        dlSamples += (target - dlSamples) * glide;
        fbD += fbStep;
        mixD += mixStep;
        const bool history = dlSamples + 1.0 <= (double) dlValid;
        const float outL = history ? readLine (dlBuf[0], dlWrite, dlSamples) : 0.0f;
        const float outR = history ? readLine (dlBuf[1], dlWrite, dlSamples) : 0.0f;
        if (dlValid < size)
            ++dlValid;
        dlLp[0] += lpCoef * (outL - dlLp[0]);
        dlLp[1] += lpCoef * (outR - dlLp[1]);
        if (d.pingPong)
        {
            // Input (mono) into the left line; left feeds right, right feeds left.
            const float in = 0.5f * (l[i] + r[i]);
            dlBuf[0][(size_t) dlWrite] = in + fbD * dlLp[1];
            dlBuf[1][(size_t) dlWrite] = dlLp[0];  // every left bounce crosses to the right; feedback closes the loop
        }
        else
        {
            dlBuf[0][(size_t) dlWrite] = l[i] + fbD * dlLp[0];
            dlBuf[1][(size_t) dlWrite] = r[i] + fbD * dlLp[1];
        }
        if (++dlWrite >= size)
            dlWrite = 0;
        l[i] += (outL - l[i]) * mixD * 0.5f + outL * mixD * 0.5f;
        r[i] += (outR - r[i]) * mixD * 0.5f + outR * mixD * 0.5f;
    }
}

// ---- reverb ---------------------------------------------------------------------

void PointFxChain::processReverb (const ReverbSettings& rv, float* l, float* r, int n) noexcept
{
    juce::Reverb::Parameters p;
    const float size = (float) std::clamp (rv.sizePercent / 100.0, 0.0, 1.0);
    const float decay = (float) std::clamp (rv.decayPercent / 100.0, 0.0, 1.0);
    switch (rv.type)
    {
        case ReverbSettings::Type::Room:
            p.roomSize = 0.25f + 0.45f * size + 0.2f * decay;
            p.damping = 0.6f - 0.3f * decay;
            p.width = 0.8f;
            break;
        case ReverbSettings::Type::Hall:
            p.roomSize = 0.6f + 0.25f * size + 0.14f * decay;
            p.damping = 0.4f - 0.3f * decay;
            p.width = 1.0f;
            break;
        case ReverbSettings::Type::Plate:
        default:
            p.roomSize = 0.5f + 0.3f * size + 0.15f * decay;
            p.damping = 0.15f;
            p.width = 1.0f;
            break;
    }
    p.wetLevel = 1.0f;
    p.dryLevel = 0.0f;
    p.freezeMode = 0.0f;
    if (! reverbActive || std::abs (p.roomSize - lastParams.roomSize) > 1.0e-6f || std::abs (p.damping - lastParams.damping) > 1.0e-6f
        || std::abs (p.width - lastParams.width) > 1.0e-6f)
    {
        reverb.setParameters (p);
        lastParams = p;
    }
    reverbActive = true;

    const float mixTarget = (float) std::clamp (rv.mixPercent / 100.0, 0.0, 1.0);
    const float mixStep = ramp (mixR, mixTarget, n, primed);
    const float outTarget = juce::Decibels::decibelsToGain ((float) rv.outputDb);
    const float outStep = ramp (outR, outTarget, n, primed);
    const int preSize = (int) preBuf[0].size();
    const double preSamples = std::clamp (rv.predelayMs * 0.001 * sr, 1.0, (double) preSize - 2.0);

    // Wet path in place on a scratch copy: the dry stays in l/r until mixed.
    float wetL[1024], wetR[1024];
    for (int done = 0; done < n;)
    {
        const int m = std::min (1024, n - done);
        for (int i = 0; i < m; ++i)
        {
            preBuf[0][(size_t) preWrite] = l[done + i];
            preBuf[1][(size_t) preWrite] = r[done + i];
            wetL[i] = readLine (preBuf[0], preWrite, preSamples);
            wetR[i] = readLine (preBuf[1], preWrite, preSamples);
            if (++preWrite >= preSize)
                preWrite = 0;
        }
        reverb.processStereo (wetL, wetR, m);
        for (int i = 0; i < m; ++i)
        {
            mixR += mixStep;
            outR += outStep;
            l[done + i] = (l[done + i] * (1.0f - mixR) + wetL[i] * mixR) * outR;
            r[done + i] = (r[done + i] * (1.0f - mixR) + wetR[i] * mixR) * outR;
        }
        done += m;
    }
}

} // namespace ses
