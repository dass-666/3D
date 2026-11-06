// Measurement probe (prints a table): peak and RMS of a shaped voice versus
// the unshaped one, for several cut counts. Run with: ses_tests "[probe]"
#include "audio/EchoEngine.h"
#include "model/Scene.h"
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdio>

using namespace ses;

namespace
{
SampleBuffer voiceLike (double sr, double seconds)
{
    // Tone with harmonics and a slow amplitude wobble: closer to a sung note than a pure sine.
    SampleBuffer b (1, (int) (sr * seconds));
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const double t = i / sr;
        double v = 0.0;
        for (int h = 1; h <= 6; ++h)
            v += std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * h * t) / h;
        b.setSample (0, i, (float) (0.35 * v * (0.85 + 0.15 * std::sin (2.0 * juce::MathConstants<double>::pi * 5.0 * t))));
    }
    return b;
}

struct Stats { float peak = 0.0f, rms = 0.0f; };
Stats measure (const Scene& s, SharedSample sample, double sr, double seconds, double bpm = 120.0)
{
    auto plan = buildPlan (s, sample, sr);
    EchoEngine e;
    e.prepare (sr);
    juce::AudioBuffer<float> buf (1, 512);
    Stats st;
    double acc = 0.0;
    int n = 0;
    double beat = 0.0;
    for (int done = 0; done < (int) (seconds * sr); done += 512)
    {
        buf.clear();
        e.process (buf, plan.get(), { bpm, beat, true }, done == 0, false, 1.0f, 1.0f);
        for (int i = 0; i < 512; ++i)
        {
            const float v = buf.getSample (0, i);
            st.peak = std::max (st.peak, std::abs (v));
            acc += (double) v * v;
            ++n;
        }
        beat += 512 * bpm / 60.0 / sr;
    }
    st.rms = (float) std::sqrt (acc / n);
    return st;
}
} // namespace

TEST_CASE ("probe: shaper level vs cuts", "[.probe]")
{
    for (double sr : { 44100.0, 96000.0 })
    {
        auto sample = std::make_shared<const SampleBuffer> (voiceLike (sr, 0.65));
        Scene s;
        Point p;
        p.id = "p"; p.number = 1;
        s.points.push_back (p);
        const auto ref = measure (s, sample, sr, 0.7);
        std::printf ("sr=%.0f  reference: peak %.3f rms %.3f\n", sr, ref.peak, ref.rms);
        for (double ramp : { 1.0, 3.0, 10.0 })
            for (bool autoLevel : { false, true })
                for (int cuts : { 8, 16, 32, 64, 128 })
                {
                    s.points[0].shaper = {};
                    s.points[0].shaper.enabled = true;
                    s.points[0].shaper.cuts = cuts;
                    s.points[0].shaper.attackMs = ramp;
                    s.points[0].shaper.releaseMs = ramp;
                    s.points[0].shaper.autoLevel = autoLevel;
                    const auto st = measure (s, sample, sr, 0.7);
                    const double period = 650.0 / cuts;
                    std::printf ("  ramp %4.1f ms auto %d cuts %3d (period %5.1f ms, open %5.1f ms): peak %.3f (%+.1f dB)  rms %.3f (%+.1f dB)\n",
                                 ramp, (int) autoLevel, cuts, period, period / 2, st.peak, 20 * std::log10 (st.peak / ref.peak),
                                 st.rms, 20 * std::log10 (st.rms / ref.rms));
                }
    }
}
