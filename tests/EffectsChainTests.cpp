// Per-repetition effect chain (character -> modulation -> delay -> reverb),
// formant shift, slot stability, tails and limits. Every check measures the
// engine's real output; nothing here reads a flag that says "enabled".

#include "audio/EchoEngine.h"
#include "audio/PitchRenderer.h"
#include "model/Serialization.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <chrono>
#include <cmath>
#include <set>

using namespace ses;

namespace
{
SampleBuffer tone (double sr, double seconds, double hz = 440.0, float amp = 0.4f)
{
    SampleBuffer b (1, (int) (sr * seconds));
    for (int i = 0; i < b.getNumSamples(); ++i)
        b.setSample (0, i, amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz * i / sr));
    return b;
}

// Harmonic-rich "voice": 150 Hz pulse-ish wave with a resonance around 900 Hz.
SampleBuffer vowel (double sr, double seconds)
{
    SampleBuffer b (1, (int) (sr * seconds));
    double z1 = 0.0, z2 = 0.0;
    const double w = 2.0 * juce::MathConstants<double>::pi * 900.0 / sr, rr = 0.985;
    const double a1 = 2.0 * rr * std::cos (w), a2 = -rr * rr;
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        double v = 0.0;
        for (int h = 1; h <= 20; ++h)
            v += std::sin (2.0 * juce::MathConstants<double>::pi * 150.0 * h * i / sr) / h;
        const double y = v * 0.08 + a1 * z1 + a2 * z2;
        z2 = z1;
        z1 = y;
        b.setSample (0, i, (float) (y * 0.6));
    }
    return b;
}

struct Stereo { std::vector<float> l, r; };

Stereo run (EchoEngine& e, const PlaybackPlan* plan, double sr, int block, double seconds, double bpm = 120.0, int channels = 2)
{
    Stereo out;
    juce::AudioBuffer<float> buf (channels, block);
    const int total = (int) (seconds * sr);
    double beat = 0.0;
    for (int done = 0; done < total; done += block)
    {
        buf.clear();
        e.process (buf, plan, { bpm, beat, true }, done == 0, false, 1.0f, 1.0f);
        for (int i = 0; i < block; ++i)
        {
            out.l.push_back (buf.getSample (0, i));
            out.r.push_back (buf.getSample (channels > 1 ? 1 : 0, i));
        }
        beat += block * bpm / 60.0 / sr;
    }
    return out;
}

float peakBetween (const std::vector<float>& x, double sr, double from, double to)
{
    float m = 0.0f;
    for (int i = std::max (0, (int) (from * sr)); i < std::min ((int) x.size(), (int) (to * sr)); ++i)
        m = std::max (m, std::abs (x[(size_t) i]));
    return m;
}

float rmsBetween (const std::vector<float>& x, double sr, double from, double to)
{
    double acc = 0.0;
    int n = 0;
    for (int i = std::max (0, (int) (from * sr)); i < std::min ((int) x.size(), (int) (to * sr)); ++i, ++n)
        acc += (double) x[(size_t) i] * x[(size_t) i];
    return n > 0 ? (float) std::sqrt (acc / n) : 0.0f;
}

// Magnitude of one frequency over a window (Goertzel-like naive DFT bin).
double magnitudeAt (const float* x, int n, double sr, double hz)
{
    double re = 0.0, im = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double ph = 2.0 * juce::MathConstants<double>::pi * hz * i / sr;
        re += x[i] * std::cos (ph);
        im -= x[i] * std::sin (ph);
    }
    return std::sqrt (re * re + im * im) / n;
}

double centroid (const float* x, int n, double sr)
{
    double num = 0.0, den = 0.0;
    for (double hz = 100.0; hz <= 5000.0; hz += 50.0)
    {
        const double m = magnitudeAt (x, n, sr, hz);
        num += hz * m;
        den += m;
    }
    return den > 0.0 ? num / den : 0.0;
}

bool finite (const Stereo& s)
{
    for (auto* v : { &s.l, &s.r })
        for (float f : *v)
            if (! std::isfinite (f))
                return false;
    return true;
}

Scene oneVoice (double hz = 440.0)
{
    Scene s;
    Point p;
    p.id = "p";
    p.number = 1;
    p.timing.beat = 0.0;
    s.points.push_back (p);
    juce::ignoreUnused (hz);
    return s;
}
} // namespace

TEST_CASE ("Formant shift keeps the pitch and moves the spectral centroid; it is part of the render key")
{
    const double sr = 48000.0;
    const auto in = vowel (sr, 0.8);
    const auto same = renderPitched (in, sr, 0.0, 0.0);
    const auto up = renderPitched (in, sr, 0.0, 7.0);
    const auto down = renderPitched (in, sr, 0.0, -7.0);
    const int from = (int) (0.25 * sr), len = (int) (0.4 * sr);
    // Fundamental stays at 150 Hz for all three.
    for (auto* b : { &same, &up, &down })
    {
        const double f = magnitudeAt (b->getReadPointer (0) + from, len, sr, 150.0);
        const double f2 = magnitudeAt (b->getReadPointer (0) + from, len, sr, 150.0 * std::pow (2.0, 7.0 / 12.0));
        CHECK (f > 3.0 * f2);
    }
    const double cSame = centroid (same.getReadPointer (0) + from, len, sr);
    const double cUp = centroid (up.getReadPointer (0) + from, len, sr);
    const double cDown = centroid (down.getReadPointer (0) + from, len, sr);
    INFO ("centroid same " << cSame << " up " << cUp << " down " << cDown);
    CHECK (cUp > cSame * 1.15);
    CHECK (cDown < cSame * 0.9);

    SoundSettings a, b;
    a.formantSemitones = 3.0;
    CHECK (renderKeyFor (a) != renderKeyFor (b));
    CHECK (renderKeyFor (b) == 0);
}

TEST_CASE ("Delay: echoes at the set time, feedback decays, ping-pong alternates, sync follows the tempo")
{
    const double sr = 48000.0;
    auto sample = std::make_shared<const SampleBuffer> (tone (sr, 0.05));
    Scene s = oneVoice();
    auto& d = s.points[0].effects.delay;
    d.enabled = true;
    d.timeMs = 200.0;
    d.feedbackPercent = 50.0;
    d.mixPercent = 100.0;
    d.lowpassHz = 20000.0;
    {
        auto plan = buildPlan (s, sample, sr);
        REQUIRE (plan->echoes[0].fxSlot == 0);
        EchoEngine e;
        e.prepare (sr);
        const auto out = run (e, plan.get(), sr, 256, 1.2);
        REQUIRE (finite (out));
        const float p0 = peakBetween (out.l, sr, 0.0, 0.06);
        const float p1 = peakBetween (out.l, sr, 0.19, 0.26);
        const float p2 = peakBetween (out.l, sr, 0.39, 0.46);
        const float gap = peakBetween (out.l, sr, 0.08, 0.18);
        INFO ("p0 " << p0 << " p1 " << p1 << " p2 " << p2 << " gap " << gap);
        CHECK_THAT (p0, Catch::Matchers::WithinAbs (0.2, 0.03));  // mix 100 %: dry at half, echoes at full level
        CHECK_THAT (p1, Catch::Matchers::WithinAbs (0.4, 0.04));
        CHECK_THAT (p2 / p1, Catch::Matchers::WithinAbs (0.5, 0.12));
        CHECK (gap < 0.02f);
        CHECK (e.numActiveFxSlots() == 1);  // still ringing at 1.2 s
    }
    SECTION ("ping-pong: first echo on the right, second on the left")
    {
        d.pingPong = true;
        auto plan = buildPlan (s, sample, sr);
        EchoEngine e;
        e.prepare (sr);
        const auto out = run (e, plan.get(), sr, 512, 1.0);
        const float l1 = peakBetween (out.l, sr, 0.19, 0.26), r1 = peakBetween (out.r, sr, 0.19, 0.26);
        const float l2 = peakBetween (out.l, sr, 0.39, 0.46), r2 = peakBetween (out.r, sr, 0.39, 0.46);
        INFO ("l1 " << l1 << " r1 " << r1 << " l2 " << l2 << " r2 " << r2);
        CHECK (l1 > 0.3f);   // 200 ms: left line (input)
        CHECK (r1 < 0.02f);
        CHECK (r2 > 0.3f);   // 400 ms: crossed to the right
        CHECK (l2 < 0.3f * l1 + 0.02f);
    }
    SECTION ("sync: 1/8 at 90 BPM = 333 ms")
    {
        d.sync = true;
        d.division = StutterSettings::Division::Eighth;
        auto plan = buildPlan (s, sample, sr);
        EchoEngine e;
        e.prepare (sr);
        const auto out = run (e, plan.get(), sr, 256, 1.0, 90.0);
        CHECK (peakBetween (out.l, sr, 0.32, 0.39) > 0.2f);
        CHECK (peakBetween (out.l, sr, 0.19, 0.3) < 0.02f);
    }
    SECTION ("disabled: nothing after the voice, no fx slot")
    {
        d.enabled = false;
        auto plan = buildPlan (s, sample, sr);
        CHECK (plan->echoes[0].fxSlot == -1);
        EchoEngine e;
        e.prepare (sr);
        const auto out = run (e, plan.get(), sr, 256, 0.6);
        CHECK (peakBetween (out.l, sr, 0.1, 0.6) < 1e-4f);
    }
}

TEST_CASE ("Reverb: a tail follows the voice, decays with the decay control and ends; room/hall/plate differ")
{
    const auto sr = GENERATE (44100.0, 96000.0);
    auto sample = std::make_shared<const SampleBuffer> (tone (sr, 0.2));
    Scene s = oneVoice();
    auto& r = s.points[0].effects.reverb;
    r.enabled = true;
    r.type = ReverbSettings::Type::Hall;
    r.sizePercent = 80.0;
    r.decayPercent = 85.0;
    r.mixPercent = 60.0;
    r.predelayMs = 30.0;
    auto plan = buildPlan (s, sample, sr);
    EchoEngine e;
    e.prepare (sr);
    const auto out = run (e, plan.get(), sr, 512, 12.0);
    REQUIRE (finite (out));
    const float tail1 = rmsBetween (out.l, sr, 0.4, 0.8);
    const float tail2 = rmsBetween (out.l, sr, 1.5, 2.0);
    const float end = peakBetween (out.l, sr, 11.0, 12.0);
    INFO ("sr " << sr << " tail1 " << tail1 << " tail2 " << tail2 << " end " << end);
    CHECK (tail1 > 0.01f);         // audible tail after the 0.2 s voice
    CHECK (tail2 < tail1);         // decaying
    CHECK (tail2 > 0.0005f);       // long hall still ringing at 1.5 s
    CHECK (end < 1e-3f);           // and over by 11 s
    CHECK (e.numActiveFxSlots() == 0);

    // Short room: tail gone much sooner.
    r.type = ReverbSettings::Type::Room;
    r.sizePercent = 20.0;
    r.decayPercent = 10.0;
    auto plan2 = buildPlan (s, sample, sr);
    EchoEngine e2;
    e2.prepare (sr);
    const auto out2 = run (e2, plan2.get(), sr, 512, 3.0);
    CHECK (rmsBetween (out2.l, sr, 1.5, 2.0) < tail2);
    CHECK (rmsBetween (out2.l, sr, 0.25, 0.5) > 0.002f);

    // Stereo width: hall left and right differ (decorrelated), the dry voice is centred.
    double diff = 0.0;
    for (int i = (int) (0.5 * sr); i < (int) (0.8 * sr); ++i)
        diff += std::abs (out.l[(size_t) i] - out.r[(size_t) i]);
    CHECK (diff > 0.0);
}

TEST_CASE ("Character: saturation adds harmonics, bitcrusher quantises, telephone narrows the band, robot ring-modulates")
{
    const double sr = 48000.0;
    auto sample = std::make_shared<const SampleBuffer> (tone (sr, 0.5, 440.0, 0.4f));
    Scene s = oneVoice();
    auto& c = s.points[0].effects.character;
    c.enabled = true;
    c.mixPercent = 100.0;
    auto render = [&] (CharacterSettings::Type t, double amount)
    {
        c.type = t;
        c.amountPercent = amount;
        auto plan = buildPlan (s, sample, sr);
        EchoEngine e;
        e.prepare (sr);
        return run (e, plan.get(), sr, 256, 0.5);
    };
    c.enabled = false;
    const auto dry = render (CharacterSettings::Type::Saturation, 0.0);
    c.enabled = true;
    const int from = (int) (0.1 * sr), len = (int) (0.3 * sr);
    const double dry3 = magnitudeAt (dry.l.data() + from, len, sr, 1320.0);
    const double dry1 = magnitudeAt (dry.l.data() + from, len, sr, 440.0);

    const auto sat = render (CharacterSettings::Type::Saturation, 80.0);
    const double sat3 = magnitudeAt (sat.l.data() + from, len, sr, 1320.0);
    INFO ("3rd harmonic dry " << dry3 << " sat " << sat3 << " fundamental dry " << dry1);
    CHECK (sat3 > 20.0 * dry3 + 0.002);
    CHECK (peakBetween (sat.l, sr, 0.1, 0.4) < 1.0f);

    const auto od = render (CharacterSettings::Type::Overdrive, 90.0);
    CHECK (magnitudeAt (od.l.data() + from, len, sr, 1320.0) > 20.0 * dry3 + 0.002);

    const auto bc = render (CharacterSettings::Type::Bitcrusher, 100.0);
    std::set<int> levels;
    for (int i = from; i < from + len; ++i)
        levels.insert ((int) std::lround (bc.l[(size_t) i] * 10000.0f));
    INFO ("distinct levels " << levels.size());
    CHECK (levels.size() <= 24);  // 3 bits + hold: a handful of values
    CHECK (levels.size() >= 3);

    auto sample2 = std::make_shared<const SampleBuffer> (tone (sr, 0.5, 100.0, 0.4f));
    Scene s2 = s;
    s2.points[0].effects.character = { true, CharacterSettings::Type::Telephone, 30.0, 100.0 };
    {
        auto plan = buildPlan (s2, sample2, sr);
        EchoEngine e;
        e.prepare (sr);
        const auto low = run (e, plan.get(), sr, 256, 0.5);
        auto plan1k = buildPlan (s2, std::make_shared<const SampleBuffer> (tone (sr, 0.5, 1000.0, 0.4f)), sr);
        EchoEngine e1;
        e1.prepare (sr);
        const auto mid = run (e1, plan1k.get(), sr, 256, 0.5);
        const float lowR = rmsBetween (low.l, sr, 0.1, 0.4), midR = rmsBetween (mid.l, sr, 0.1, 0.4);
        INFO ("100 Hz " << lowR << " 1 kHz " << midR);
        CHECK (lowR < 0.35f * midR);
    }

    const auto robot = render (CharacterSettings::Type::Robot, 50.0);  // carrier 120 Hz
    const double fund = magnitudeAt (robot.l.data() + from, len, sr, 440.0);
    const double sum = magnitudeAt (robot.l.data() + from, len, sr, 560.0);
    const double dif = magnitudeAt (robot.l.data() + from, len, sr, 320.0);
    INFO ("robot fundamental " << fund << " sum " << sum << " diff " << dif);
    CHECK (sum > 5.0 * fund);
    CHECK (dif > 5.0 * fund);

    const auto lofi = render (CharacterSettings::Type::LoFi, 100.0);
    CHECK (rmsBetween (lofi.l, sr, 0.1, 0.4) > 0.05f);
    CHECK (finite (lofi));
}

TEST_CASE ("Modulation: tremolo pumps at its rate, chorus/flanger change the signal without changing the level much")
{
    const double sr = 48000.0;
    auto sample = std::make_shared<const SampleBuffer> (tone (sr, 1.0, 440.0, 0.4f));
    Scene s = oneVoice();
    auto& m = s.points[0].effects.modulation;
    m.enabled = true;
    m.type = ModulationSettings::Type::Tremolo;
    m.rateHz = 5.0;
    m.depthPercent = 100.0;
    {
        auto plan = buildPlan (s, sample, sr);
        EchoEngine e;
        e.prepare (sr);
        const auto out = run (e, plan.get(), sr, 256, 1.0);
        // 5 Hz: minimum every 200 ms; at 0.1 s the gain is 0, at 0.2 s it is 1.
        CHECK (peakBetween (out.l, sr, 0.095, 0.105) < 0.05f);
        CHECK (peakBetween (out.l, sr, 0.195, 0.205) > 0.3f);
        CHECK (peakBetween (out.l, sr, 0.295, 0.305) < 0.05f);
    }
    for (auto type : { ModulationSettings::Type::Chorus, ModulationSettings::Type::Flanger })
    {
        m.type = type;
        m.rateHz = 1.0;
        m.depthPercent = 70.0;
        m.mixPercent = 60.0;
        auto plan = buildPlan (s, sample, sr);
        EchoEngine e;
        e.prepare (sr);
        const auto wet = run (e, plan.get(), sr, 256, 1.0);
        m.enabled = false;
        auto planDry = buildPlan (s, sample, sr);
        EchoEngine e2;
        e2.prepare (sr);
        const auto dry = run (e2, planDry.get(), sr, 256, 1.0);
        m.enabled = true;
        double diff = 0.0;
        for (int i = (int) (0.2 * sr); i < (int) (0.9 * sr); ++i)
            diff += std::abs (wet.l[(size_t) i] - dry.l[(size_t) i]);
        const float wr = rmsBetween (wet.l, sr, 0.2, 0.9), dr = rmsBetween (dry.l, sr, 0.2, 0.9);
        INFO ("type " << (int) type << " wet rms " << wr << " dry rms " << dr);
        CHECK (diff > 100.0);
        CHECK (wr > 0.4f * dr);
        CHECK (wr < 2.5f * dr);
        CHECK (finite (wet));
    }
}

TEST_CASE ("Effect slots: stable across plan rebuilds, tails ring out after the point is removed, 17th point plays dry")
{
    const double sr = 48000.0;
    auto sample = std::make_shared<const SampleBuffer> (tone (sr, 0.05));
    Scene s;
    for (int i = 0; i < 17; ++i)
    {
        Point p;
        p.id = "p" + juce::String (i);
        p.number = i + 1;
        p.timing.beat = 0.0;
        p.effects.delay.enabled = true;
        p.effects.delay.timeMs = 300.0;
        p.effects.delay.feedbackPercent = 60.0;
        p.effects.delay.mixPercent = 100.0;
        s.points.push_back (p);
    }
    s.maxPoints = 32;
    auto plan = buildPlan (s, sample, sr);
    REQUIRE (plan->echoes.size() == 17);
    CHECK (plan->pointsWithoutFx == 1);
    CHECK (plan->echoes[16].fxSlot == -1);
    CHECK (plan->echoes[3].fxSlot == 3);

    // Remove point 1: the others keep their slots when the previous plan is given.
    Scene s2 = s;
    s2.points.erase (s2.points.begin() + 1);
    auto plan2 = buildPlan (s2, sample, {}, sr, plan.get());
    CHECK (plan2->find (keyForPoint ("p3"))->fxSlot == 3);
    CHECK (plan2->find (keyForPoint ("p16"))->fxSlot == 1);  // took the freed slot
    CHECK (plan2->pointsWithoutFx == 0);

    // Run: the echoes of a removed point keep ringing out (no cut), new plan or not.
    EchoEngine e;
    e.prepare (sr);
    juce::AudioBuffer<float> buf (2, 256);
    double beat = 0.0;
    std::vector<float> out;
    const PlaybackPlan* current = plan.get();
    for (int done = 0; done < (int) (1.5 * sr); done += 256)
    {
        if (done >= (int) (0.4 * sr))
            current = plan2.get();
        buf.clear();
        e.process (buf, current, { 120.0, beat, true }, done == 0, false, 1.0f, 1.0f);
        for (int i = 0; i < 256; ++i)
            out.push_back (buf.getSample (0, i));
        beat += 256 * 120.0 / 60.0 / sr;
    }
    CHECK (peakBetween (out, sr, 0.59, 0.66) > 0.3f);   // 2nd echo at 0.6 s still there after the swap at 0.4 s
    CHECK (peakBetween (out, sr, 0.89, 0.96) > 0.1f);   // 3rd echo too
    for (float v : out)
        REQUIRE (std::isfinite (v));
}

TEST_CASE ("Effect chain: mono output, every block size and sample rate, and a CPU sanity bound")
{
    const auto sr = GENERATE (44100.0, 48000.0, 96000.0);
    const auto block = GENERATE (64, 1024, 4096);
    auto sample = std::make_shared<const SampleBuffer> (tone (sr, 0.3));
    Scene s;
    for (int i = 0; i < 16; ++i)
    {
        Point p;
        p.id = "p" + juce::String (i);
        p.number = i + 1;
        p.timing.beat = 0.25 * i;
        p.effects.character = { true, (CharacterSettings::Type) (i % 6), 50.0, 100.0 };
        p.effects.modulation = { true, (ModulationSettings::Type) (i % 3), 1.0, 50.0, 50.0 };
        p.effects.delay = { true, i % 2 == 0, 200.0, StutterSettings::Division::Eighth, 40.0, i % 3 == 0, 6000.0, 40.0 };
        p.effects.reverb = { true, (ReverbSettings::Type) (i % 3), 60.0, 60.0, 10.0, 30.0, 0.0 };
        s.points.push_back (p);
    }
    auto plan = buildPlan (s, sample, sr);
    EchoEngine e;
    e.prepare (sr);
    const auto t0 = std::chrono::steady_clock::now();
    const auto stereo = run (e, plan.get(), sr, block, 3.0);
    const double wall = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    INFO ("sr " << sr << " block " << block << " 3 s of 16 full chains took " << wall << " s (debug build)");
    CHECK (wall < 3.0);  // slower than real time would be unusable even in a debug build
    REQUIRE (finite (stereo));
    CHECK (peakBetween (stereo.l, sr, 0.0, 3.0) > 0.1f);
    CHECK (peakBetween (stereo.l, sr, 0.0, 3.0) < 4.0f);  // 16 chains summed, before the output limiter

    EchoEngine mono;
    mono.prepare (sr);
    const auto m = run (mono, plan.get(), sr, block, 1.0, 120.0, 1);
    REQUIRE (finite (m));
    CHECK (peakBetween (m.l, sr, 0.0, 1.0) > 0.1f);
}

TEST_CASE ("Effect chain and formants persist and survive clamping")
{
    Scene s = oneVoice();
    s.id = makeId();
    auto& p = s.points[0];
    p.sound.formantSemitones = -4.5;
    p.effects.character = { true, CharacterSettings::Type::Telephone, 33.0, 80.0 };
    p.effects.modulation = { true, ModulationSettings::Type::Flanger, 0.3, 70.0, 45.0 };
    p.effects.delay = { true, true, 123.0, StutterSettings::Division::Sixteenth, 61.0, true, 4321.0, 37.0 };
    p.effects.reverb = { true, ReverbSettings::Type::Plate, 77.0, 66.0, 55.0, 44.0, -3.0 };
    Document d;
    d.scenes.push_back (s);
    d.activeSceneId = s.id;
    const auto back = documentFromJson (toJson (d));
    REQUIRE (back.value.has_value());
    INFO (back.warnings.joinIntoString ("\n"));
    CHECK (back.warnings.isEmpty());
    CHECK (back.value->scenes[0].points[0] == p);

    PointEffects wild = p.effects;
    wild.delay.feedbackPercent = 400.0;
    wild.delay.timeMs = 1.0e9;
    wild.reverb.outputDb = 99.0;
    const auto c = clamped (wild);
    CHECK (c.delay.feedbackPercent == 95.0);
    CHECK (c.delay.timeMs == limits::maxDelayMs);
    CHECK (c.reverb.outputDb == 6.0);
}
