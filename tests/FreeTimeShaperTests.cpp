// Free time (seconds, tempo-independent), per-point play region and the
// Volume Shaper. Simulated host only: not a substitute for FL Studio.

#include "audio/EchoEngine.h"
#include "model/Scene.h"
#include "model/Serialization.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>

using namespace ses;

namespace
{
SampleBuffer tone (double sr, double seconds, float amp = 0.5f)
{
    SampleBuffer b (1, (int) (sr * seconds));
    for (int i = 0; i < b.getNumSamples(); ++i)
        b.setSample (0, i, amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * i / sr));
    return b;
}

std::vector<int> bursts (const std::vector<float>& x, int quietSamples, float thresh = 1e-4f)
{
    std::vector<int> out;
    int quiet = quietSamples;
    for (size_t i = 0; i < x.size(); ++i)
    {
        if (std::abs (x[i]) > thresh)
        {
            if (quiet >= quietSamples)
                out.push_back ((int) i);
            quiet = 0;
        }
        else
            ++quiet;
    }
    return out;
}

// Runs the engine for `seconds`, triggering at the first block, with the tempo
// taken from `bpmAt (blockIndex)`.
template <typename BpmFn>
std::vector<float> run (EchoEngine& e, const PlaybackPlan& plan, double sr, int block, double seconds, BpmFn&& bpmAt)
{
    std::vector<float> out;
    juce::AudioBuffer<float> buf (1, block);
    const int total = (int) (seconds * sr);
    double beat = 0.0;
    for (int done = 0, b = 0; done < total; done += block, ++b)
    {
        const double bpm = bpmAt (b);
        buf.clear();
        e.process (buf, &plan, { bpm, beat, true }, done == 0, false, 1.0f, 1.0f);
        for (int i = 0; i < block; ++i)
            out.push_back (buf.getSample (0, i));
        beat += block * bpm / 60.0 / sr;
    }
    return out;
}
} // namespace

TEST_CASE ("Free time: a point at 0.5 s fires at 0.5 s whatever the tempo, even if it changes while pending")
{
    const auto sr = GENERATE (44100.0, 48000.0, 96000.0);
    const auto block = GENERATE (64, 512);
    Scene s;
    s.timeMode = TimeMode::Free;
    Point p;
    p.id = "p"; p.number = 1;
    p.timing.beat = 99.0;      // ignored in free mode
    p.timing.seconds = 0.5;
    s.points.push_back (p);
    auto sample = std::make_shared<const SampleBuffer> (tone (sr, 0.2));
    auto plan = buildPlan (s, sample, sr);
    REQUIRE (plan->echoes.size() == 1);
    CHECK (plan->echoes[0].freeTime);

    for (double bpm : { 60.0, 120.0, 150.0 })
    {
        EchoEngine e;
        e.prepare (sr);
        const auto out = run (e, *plan, sr, block, 1.0, [bpm] (int) { return bpm; });
        const auto on = bursts (out, 64);
        REQUIRE (on.size() == 1);
        CHECK (std::abs (on[0] - 0.5 * sr) <= 2.0);
    }

    // Tempo doubles after 0.2 s: still 0.5 s.
    EchoEngine e;
    e.prepare (sr);
    const int blocksPer200ms = (int) (0.2 * sr) / block;
    const auto out = run (e, *plan, sr, block, 1.0, [=] (int b) { return b < blocksPer200ms ? 120.0 : 240.0; });
    const auto on = bursts (out, 64);
    REQUIRE (on.size() == 1);
    CHECK (std::abs (on[0] - 0.5 * sr) <= 2.0);

    // The same point in Beats mode moves with the tempo (1 beat = 1 s at 60, 0.5 s at 120).
    Scene sb = s;
    sb.timeMode = TimeMode::Beats;
    sb.points[0].timing.beat = 1.0;
    auto planB = buildPlan (sb, sample, sr);
    EchoEngine e2;
    e2.prepare (sr);
    const auto outB = run (e2, *planB, sr, block, 2.0, [] (int) { return 60.0; });
    const auto onB = bursts (outB, 64);
    REQUIRE (onB.size() == 1);
    CHECK (std::abs (onB[0] - 1.0 * sr) <= 2.0);
}

TEST_CASE ("Free time: individual trigger and stutter slices keep working")
{
    const double sr = 48000.0;
    Scene s;
    s.timeMode = TimeMode::Free;
    Point p;
    p.id = "p"; p.number = 1; p.timing.seconds = 0.3;
    p.stutter.enabled = true; p.stutter.repeats = 4;
    p.stutter.division = StutterSettings::Division::Sixteenth;  // 0.25 beat = 125 ms at 120
    p.stutter.sliceLengthPercent = 40.0;
    s.points.push_back (p);
    auto sample = std::make_shared<const SampleBuffer> (tone (sr, 1.0));
    auto plan = buildPlan (s, sample, sr);
    REQUIRE (plan->echoes.size() == 4);

    EchoEngine e;
    e.prepare (sr);
    const auto out = run (e, *plan, sr, 256, 1.2, [] (int) { return 120.0; });
    const auto on = bursts (out, 64);
    REQUIRE (on.size() == 4);
    for (size_t k = 0; k < 4; ++k)
        CHECK (std::abs (on[k] - (0.3 * sr + k * 0.125 * sr)) <= 2.0);

    // "Disparar esta": now, not 0.3 s later.
    EchoEngine e2;
    e2.prepare (sr);
    juce::AudioBuffer<float> buf (1, 256);
    std::vector<float> out2;
    for (int done = 0; done < (int) sr; done += 256)
    {
        buf.clear();
        EngineRequests req;
        req.triggerPoint = done == 0 ? keyForPoint ("p") : 0;
        e2.process (buf, plan.get(), { 120.0, done * 2.0 / sr, true }, req, 1.0f, 1.0f);
        for (int i = 0; i < 256; ++i)
            out2.push_back (buf.getSample (0, i));
    }
    const auto on2 = bursts (out2, 64);
    REQUIRE (on2.size() == 4);
    CHECK (on2[0] <= 2);
}

TEST_CASE ("Play region: a repetition plays only its part of the fragment")
{
    const double sr = 48000.0;
    SampleBuffer frag (1, 48000);  // first half silent, second half tone
    frag.clear();
    for (int i = 24000; i < 48000; ++i)
        frag.setSample (0, i, 0.5f);
    auto sample = std::make_shared<const SampleBuffer> (frag);

    Scene s;
    Point p;
    p.id = "p"; p.number = 1; p.timing.beat = 0.0;
    p.region = { 0.5, 0.75 };
    s.points.push_back (p);
    auto plan = buildPlan (s, sample, sr);
    CHECK (plan->echoes[0].regionStart == 24000);
    CHECK (plan->echoes[0].regionEnd == 36000);

    EchoEngine e;
    e.prepare (sr);
    const auto out = run (e, *plan, sr, 256, 1.0, [] (int) { return 120.0; });
    const auto on = bursts (out, 64);
    REQUIRE (on.size() == 1);
    CHECK (on[0] <= 2);                                      // sound from the first sample (region starts in the tone)
    CHECK (std::abs (out[(int) (0.2 * sr)]) > 0.1f);
    CHECK (std::abs (out[(int) (0.3 * sr)]) < 1e-4f);        // 0.25 s long, then nothing
}

TEST_CASE ("Volume Shaper: pure gain function")
{
    ShaperSettings sh;
    sh.enabled = true;
    sh.cuts = 4;
    sh.depthPercent = 100.0;
    sh.openPercent = 50.0;
    // Square, 4 cuts over 1 s: open 0..0.125, closed 0.125..0.25, ...
    CHECK (shaperGain (sh, 0.05, 1.0, 0.0) == 1.0f);
    CHECK (shaperGain (sh, 0.20, 1.0, 0.0) == 0.0f);
    CHECK (shaperGain (sh, 0.30, 1.0, 0.0) == 1.0f);
    sh.depthPercent = 40.0;
    CHECK_THAT (shaperGain (sh, 0.20, 1.0, 0.0), Catch::Matchers::WithinAbs (0.6, 1e-6));

    sh.shape = ShaperSettings::Shape::Triangle;
    sh.depthPercent = 100.0;
    CHECK_THAT (shaperGain (sh, 0.125, 1.0, 0.0), Catch::Matchers::WithinAbs (1.0, 1e-6));  // middle of a cut
    CHECK_THAT (shaperGain (sh, 0.0, 1.0, 0.0), Catch::Matchers::WithinAbs (0.0, 1e-6));

    sh.shape = ShaperSettings::Shape::Saw;
    CHECK (shaperGain (sh, 0.01, 1.0, 0.0) > shaperGain (sh, 0.2, 1.0, 0.0));

    // Region: untouched outside it.
    sh.wholeVoice = false;
    sh.regionStart = 0.5;
    sh.regionEnd = 1.0;
    CHECK (shaperGain (sh, 0.3, 1.0, 0.0) == 1.0f);
    CHECK (shaperGain (sh, 0.6, 1.0, 0.0) < 1.0f);
    CHECK (shaperGain (sh, 0.3, 0.0, 0.0) == 1.0f);  // zero length: no effect, no NaN
    sh.enabled = false;
    CHECK (shaperGain (sh, 0.6, 1.0, 0.0) == 1.0f);

    // Acceleration: later cuts are shorter (edges get closer together).
    ShaperSettings acc;
    acc.enabled = true; acc.cuts = 4; acc.accelPercent = 100.0; acc.depthPercent = 100.0;
    std::vector<double> edges;
    float last = shaperGain (acc, 0.0, 1.0, 0.0);
    for (int i = 1; i < 10000; ++i)
    {
        const float g = shaperGain (acc, i / 10000.0, 1.0, 0.0);
        if (g != last)
            edges.push_back (i / 10000.0);
        last = g;
    }
    REQUIRE (edges.size() >= 6);
    for (size_t i = 2; i < edges.size(); ++i)
        CHECK (edges[i] - edges[i - 1] < edges[i - 1] - edges[i - 2]);
}

TEST_CASE ("Volume Shaper: square cuts are uniform, tempo-independent in Count mode, no clicks")
{
    const auto sr = GENERATE (44100.0, 48000.0, 96000.0);
    const auto block = GENERATE (64, 2048);
    const auto bpm = GENERATE (90.0, 140.0);
    Scene s;
    Point p;
    p.id = "p"; p.number = 1; p.timing.beat = 0.0;
    p.shaper.enabled = true;
    p.shaper.cuts = 8;
    p.shaper.depthPercent = 100.0;
    p.shaper.openPercent = 50.0;
    p.shaper.fadeMs = 2.0;
    s.points.push_back (p);
    auto sample = std::make_shared<const SampleBuffer> (tone (sr, 1.0));
    auto plan = buildPlan (s, sample, sr);

    EchoEngine e;
    e.prepare (sr);
    const auto out = run (e, *plan, sr, block, 1.2, [bpm] (int) { return bpm; });
    const int quiet = (int) (0.02 * sr);
    const auto on = bursts (out, quiet, 2e-3f);
    REQUIRE (on.size() == 8);
    for (size_t k = 0; k < 8; ++k)
        CHECK (std::abs (on[k] - k * 0.125 * sr) <= 0.004 * sr);  // within the 2 ms fade
    // Closed part is (nearly) silent: 62.5 ms open, 62.5 ms closed.
    CHECK (std::abs (out[(int) (0.1 * sr)]) < 1e-3f);
    CHECK (std::abs (out[(int) (0.03 * sr)]) > 0.1f);
    float maxStep = 0.0f;
    for (size_t i = 1; i < out.size(); ++i)
        maxStep = std::max (maxStep, std::abs (out[i] - out[i - 1]));
    CHECK (maxStep < 0.08f);
}

TEST_CASE ("Volume Shaper: Milliseconds and Division rates")
{
    const double sr = 48000.0;
    Scene s;
    Point p;
    p.id = "p"; p.number = 1; p.timing.beat = 0.0;
    p.shaper.enabled = true;
    p.shaper.rateMode = ShaperSettings::RateMode::Milliseconds;
    p.shaper.periodMs = 100.0;
    p.shaper.depthPercent = 100.0;
    p.shaper.fadeMs = 1.0;
    s.points.push_back (p);
    auto sample = std::make_shared<const SampleBuffer> (tone (sr, 1.0));
    {
        auto plan = buildPlan (s, sample, sr);
        EchoEngine e;
        e.prepare (sr);
        const auto out = run (e, *plan, sr, 256, 1.1, [] (int) { return 77.0; });
        CHECK (bursts (out, (int) (0.02 * sr), 2e-3f).size() == 10);
    }
    s.points[0].shaper.rateMode = ShaperSettings::RateMode::Division;
    s.points[0].shaper.division = StutterSettings::Division::Eighth;  // 0.5 beat = 200 ms at 150 BPM
    {
        auto plan = buildPlan (s, sample, sr);
        EchoEngine e;
        e.prepare (sr);
        const auto out = run (e, *plan, sr, 256, 1.1, [] (int) { return 150.0; });
        const auto on = bursts (out, (int) (0.02 * sr), 2e-3f);
        REQUIRE (on.size() == 5);
        CHECK (std::abs (on[1] - 0.2 * sr) <= 0.003 * sr);
    }
}

TEST_CASE ("Volume Shaper presets exist and are valid")
{
    const auto& presets = shaperPresets();
    REQUIRE (presets.size() == 5);
    for (auto& pr : presets)
    {
        CHECK (pr.settings.enabled);
        CHECK (pr.settings == clamped (pr.settings));
    }
}

TEST_CASE ("Time mode switch converts moments at the given tempo; persistence and old scenes")
{
    Scene s;
    s.id = makeId();
    Point p;
    p.id = "p"; p.number = 1; p.timing.beat = 2.0;
    s.points.push_back (p);
    setTimeMode (s, TimeMode::Free, 120.0);
    CHECK (s.timeMode == TimeMode::Free);
    CHECK_THAT (s.points[0].timing.seconds, Catch::Matchers::WithinAbs (1.0, 1e-9));
    CHECK_THAT (momentSeconds (s, s.points[0], 60.0), Catch::Matchers::WithinAbs (1.0, 1e-9));
    setTimeMode (s, TimeMode::Beats, 60.0);
    CHECK_THAT (s.points[0].timing.beat, Catch::Matchers::WithinAbs (1.0, 1e-9));

    s.timeMode = TimeMode::Free;
    s.points[0].timing.seconds = 0.321;
    s.points[0].region = { 0.25, 0.5 };
    s.points[0].shaper = shaperPresets()[3].settings;
    Document d;
    d.scenes.push_back (s);
    d.activeSceneId = s.id;
    const auto back = documentFromJson (toJson (d));
    REQUIRE (back.value.has_value());
    INFO (back.warnings.joinIntoString ("\n"));
    CHECK (back.warnings.isEmpty());
    CHECK (*back.value == d);

    // A B4 scene has no timeMode/seconds/region/shaper: defaults, no warnings.
    const juce::String old = R"({"format":"SpatialEchoStudio.Document","formatVersion":1,"activeSceneId":"a","scenes":[
        {"id":"a","name":"x","rhythm":"1/4","points":[{"id":"q","number":1,"timing":{"beat":2.0}}]}]})";
    const auto legacyResult = documentFromJson (old);
    INFO (legacyResult.error);
    REQUIRE (legacyResult.value.has_value());
    CHECK (legacyResult.warnings.isEmpty());
    const auto& legacy = *legacyResult.value;
    const auto& q = legacy.scenes[0].points[0];
    CHECK (legacy.scenes[0].timeMode == TimeMode::Beats);
    CHECK (q.timing.seconds == 1.0);  // 2 beats at 120
    CHECK (q.region == PlayRegion {});
    CHECK_FALSE (q.shaper.enabled);
}
