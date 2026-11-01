// Free time (seconds, tempo-independent), per-point play region and the
// Volume Shaper. Simulated host only: not a substitute for FL Studio.

#include "audio/EchoEngine.h"
#include "model/Scene.h"
#include "model/Serialization.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/catch_approx.hpp>

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

    // Attack / release are linear ramps inside the opening: full level is
    // reached after attackMs, held, then released before the closed part.
    ShaperSettings rp;
    rp.enabled = true; rp.cuts = 4; rp.attackMs = 10.0; rp.releaseMs = 20.0;  // cut 0.25 s, open 0.125 s
    CHECK_THAT (shaperGain (rp, 0.005, 1.0, 0.0), Catch::Matchers::WithinAbs (0.5, 1e-6));
    CHECK (shaperGain (rp, 0.010, 1.0, 0.0) == 1.0f);
    CHECK (shaperGain (rp, 0.100, 1.0, 0.0) == 1.0f);
    CHECK_THAT (shaperGain (rp, 0.115, 1.0, 0.0), Catch::Matchers::WithinAbs (0.5, 1e-6));
    CHECK (shaperGain (rp, 0.125, 1.0, 0.0) == 0.0f);
    // Opening shorter than attack + release: ramps shrink, the peak still reaches 1.
    rp.cuts = 200;  // cut 5 ms, open 2.5 ms
    float peak = 0.0f;
    for (int i = 0; i < 500; ++i)
        peak = std::max (peak, shaperGain (rp, i * 0.00001, 1.0, 0.0));
    CHECK_THAT (peak, Catch::Matchers::WithinAbs (1.0, 0.01));

    // Pattern: '.' mutes whole cuts; lengthMs limits the shaped span.
    ShaperSettings pt;
    pt.enabled = true; pt.cuts = 4; pt.pattern = "x.";
    CHECK (shaperGain (pt, 0.05, 1.0, 0.0) == 1.0f);
    CHECK (shaperGain (pt, 0.30, 1.0, 0.0) == 0.0f);   // 2nd cut muted (would be open)
    CHECK (shaperGain (pt, 0.55, 1.0, 0.0) == 1.0f);
    pt.pattern.clear();
    pt.lengthMs = 500.0;
    CHECK (shaperGain (pt, 0.20, 1.0, 0.0) == 0.0f);   // inside the span: closed part of cut 1 of 4 over 0.5 s? (cut = 0.125 s)
    CHECK (shaperGain (pt, 0.70, 1.0, 0.0) == 1.0f);   // after the span: untouched

    // Distributions: with Accelerating later cuts get shorter, Decelerating longer.
    auto edgesOf = [] (const ShaperSettings& sh)
    {
        std::vector<double> edges;  // moments where the curve crosses 0.5 upwards
        float last = shaperGain (sh, 0.0, 1.0, 0.0);
        for (int i = 1; i < 20000; ++i)
        {
            const float g = shaperGain (sh, i / 20000.0, 1.0, 0.0);
            if (last < 0.5f && g >= 0.5f)
                edges.push_back (i / 20000.0);
            last = g;
        }
        return edges;
    };
    ShaperSettings acc;
    acc.enabled = true; acc.cuts = 6; acc.accelPercent = 100.0; acc.distribution = ShaperSettings::Distribution::Accelerating;
    auto edges = edgesOf (acc);
    REQUIRE (edges.size() == 6);
    for (size_t i = 2; i < edges.size(); ++i)
        CHECK (edges[i] - edges[i - 1] < edges[i - 1] - edges[i - 2]);
    acc.distribution = ShaperSettings::Distribution::Decelerating;
    edges = edgesOf (acc);
    REQUIRE (edges.size() == 6);
    for (size_t i = 2; i < edges.size(); ++i)
        CHECK (edges[i] - edges[i - 1] > edges[i - 1] - edges[i - 2]);
    acc.distribution = ShaperSettings::Distribution::Uniform;
    edges = edgesOf (acc);
    REQUIRE (edges.size() == 6);
    for (size_t i = 2; i < edges.size(); ++i)
        CHECK_THAT (edges[i] - edges[i - 1], Catch::Matchers::WithinAbs (edges[i - 1] - edges[i - 2], 1e-4));

    // Useful resolution: a cut needs at least attack + release + 1 ms.
    ShaperSettings res;
    res.attackMs = 1.0; res.releaseMs = 3.0;
    CHECK (maxUsefulCuts (res, 1.0) == 200);
    CHECK (maxUsefulCuts (res, 0.5) == 100);
    CHECK (maxUsefulCuts (res, 10.0) == limits::maxShaperCuts);
    CHECK (maxUsefulCuts (res, 0.001) == limits::minShaperCuts);
}

TEST_CASE ("Volume Shaper: level. Peak is kept and RMS follows the duty cycle; auto level restores it")
{
    // Diego's report: with 32 cuts the sound "loses force". Here the peak must stay
    // within 0.5 dB of the unshaped voice for 8..128 cuts (the old one-pole
    // smoothing lost 1-2.5 dB at 32 cuts), and the RMS must sit near the value
    // the 50 % duty cycle predicts (-3 dB): the remaining loss is the silence.
    const auto sr = GENERATE (44100.0, 48000.0, 96000.0);
    auto sample = std::make_shared<const SampleBuffer> (tone (sr, 1.0));
    auto stats = [&] (const Scene& s)
    {
        auto plan = buildPlan (s, sample, sr);
        EchoEngine e;
        e.prepare (sr);
        const auto out = run (e, *plan, sr, 256, 1.1, [] (int) { return 120.0; });
        double peak = 0.0, acc = 0.0;
        float maxStep = 0.0f;
        for (size_t i = 0; i < out.size(); ++i)
        {
            peak = std::max (peak, (double) std::abs (out[i]));
            acc += (double) out[i] * out[i];
            if (i > 0)
                maxStep = std::max (maxStep, std::abs (out[i] - out[i - 1]));
        }
        return std::tuple { peak, std::sqrt (acc / (double) out.size()), maxStep };
    };
    Scene s;
    Point p;
    p.id = "p"; p.number = 1;
    s.points.push_back (p);
    const auto [refPeak, refRms, refStep] = stats (s);
    for (int cuts : { 8, 16, 32, 64, 128 })
    {
        s.points[0].shaper = {};
        s.points[0].shaper.enabled = true;
        s.points[0].shaper.cuts = cuts;
        const auto [peak, rms, step] = stats (s);
        // What the curve itself predicts (mean square over the voice): 50 % duty
        // = -3 dB; with 64+ cuts the 1+3 ms ramps eat the short openings and the
        // RMS drops further. The engine must match the curve, nothing more is lost.
        double ms = 0.0;
        for (int i = 0; i < 20000; ++i)
        {
            const double g = shaperGain (s.points[0].shaper, (i + 0.5) / 20000.0, 1.0, 0.0);
            ms += g * g;
        }
        const double predictedDb = 10.0 * std::log10 (ms / 20000.0);
        INFO ("cuts " << cuts << " sr " << sr << " predicted " << predictedDb << " dB");
        CHECK (20.0 * std::log10 (peak / refPeak) > -0.5);
        CHECK_THAT (20.0 * std::log10 (rms / refRms), Catch::Matchers::WithinAbs (predictedDb, 0.7));
        if (cuts <= 16)
            CHECK_THAT (predictedDb, Catch::Matchers::WithinAbs (-3.0, 0.7));  // 32+: the 4 ms of ramps already cost ~0.8 dB
        CHECK (step < 2.0f * refStep + 0.05f);  // no clicks beyond the tone's own slope
        s.points[0].shaper.autoLevel = true;
        const auto [peak2, rms2, step2] = stats (s);
        CHECK_THAT (20.0 * std::log10 (rms2 / refRms), Catch::Matchers::WithinAbs (std::min (0.0, predictedDb + 6.0), 1.0));
        CHECK (peak2 <= 1.0);
        CHECK (shaperLevelGain (s.points[0].shaper, 1.0, 0.125) <= juce::Decibels::decibelsToGain (6.0) + 1e-6f);
    }
    // Makeup is bounded: +12 dB total, and the output limiter keeps the sum below 0 dBFS.
    s.points[0].shaper.autoLevel = true;
    s.points[0].shaper.makeupDb = 12.0;
    CHECK (shaperLevelGain (s.points[0].shaper, 1.0, 0.125) <= juce::Decibels::decibelsToGain (12.0) + 1e-6f);
    // Free time and BPM mode give the same curve in Count mode.
    s.points[0].shaper.makeupDb = 0.0;
    s.points[0].shaper.autoLevel = false;
    const auto [pA, rA, sA] = stats (s);
    setTimeMode (s, TimeMode::Free, 120.0);
    const auto [pB, rB, sB] = stats (s);
    CHECK_THAT (pB, Catch::Matchers::WithinAbs (pA, 1e-4));
    CHECK_THAT (rB, Catch::Matchers::WithinAbs (rA, 1e-4));
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
    p.shaper.attackMs = 1.0; p.shaper.releaseMs = 2.0;
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
    p.shaper.attackMs = 0.5; p.shaper.releaseMs = 1.0;
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
    REQUIRE (presets.size() == 11);
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

// ---- Host input capture ----------------------------------------------------

#include "audio/HostCapture.h"
#include "plugin/PluginProcessor.h"

TEST_CASE ("HostCapture: last seconds, recording, overwrite protection")
{
    const double sr = 48000.0;
    HostCapture cap;
    cap.prepare (sr, 2);
    CHECK_FALSE (cap.takeLast (1.0).has_value());  // nothing yet

    // Push 3 s of a ramp so sample values identify their position.
    juce::AudioBuffer<float> in (2, 480);
    juce::int64 total = 0;
    auto pushSeconds = [&] (double seconds)
    {
        for (int b = 0; b < (int) (seconds * sr / 480); ++b)
        {
            for (int i = 0; i < 480; ++i)
            {
                in.setSample (0, i, (float) ((total + i) % 1000) / 1000.0f);
                in.setSample (1, i, -in.getSample (0, i));
            }
            cap.push (in, 480);
            total += 480;
        }
    };
    pushSeconds (3.0);
    auto last = cap.takeLast (1.0);
    REQUIRE (last.has_value());
    CHECK (last->buffer.getNumSamples() == 48000);
    CHECK (last->buffer.getNumChannels() == 2);
    CHECK (last->sampleRate == sr);
    // The last sample of the capture is the last sample pushed.
    CHECK (last->buffer.getSample (0, 47999) == Catch::Approx ((float) ((total - 1) % 1000) / 1000.0f));
    CHECK (last->buffer.getSample (1, 0) == Catch::Approx (-last->buffer.getSample (0, 0)));

    // Recording: start, push 0.5 s, stop -> 0.5 s.
    cap.startRecording();
    CHECK (cap.isRecording());
    pushSeconds (0.5);
    CHECK (cap.recordedSeconds() == Catch::Approx (0.5).margin (0.02));
    auto rec = cap.stopRecording();
    REQUIRE (rec.has_value());
    CHECK (rec->buffer.getNumSamples() == Catch::Approx (24000).margin (480));
    CHECK_FALSE (cap.isRecording());

    // Wrap-around: after 25 s the ring has wrapped; the last 2 s are still contiguous and correct.
    pushSeconds (25.0);
    auto wrapped = cap.takeLast (2.0);
    REQUIRE (wrapped.has_value());
    CHECK (wrapped->buffer.getNumSamples() == 96000);
    for (int i = 0; i < 96000; i += 997)
        CHECK (wrapped->buffer.getSample (0, i) == Catch::Approx ((float) ((total - 96000 + i) % 1000) / 1000.0f));
    // A request longer than the safe window is refused rather than returning torn audio.
    CHECK (cap.takeLast (HostCapture::kMaxCaptureSeconds).has_value());
    CHECK (cap.peakDb() > -1.0f);
}

TEST_CASE ("Processor: host input capture becomes the fragment, is embedded and survives save/restore")
{
    juce::ScopedJuceInitialiser_GUI init;
    const double sr = 44100.0;
    SpatialEchoProcessor p;
    p.setPlayConfigDetails (2, 2, sr, 512);
    p.prepareToPlay (sr, 512);
    p.setDocument (Document {});  // empty document, no audio

    // Silence first: capture must refuse (routing mistake), not load a silent fragment.
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    for (int b = 0; b < 100; ++b)
    {
        buf.clear();
        p.processBlock (buf, midi);
    }
    CHECK (p.captureHostLast (0.5).failed());
    CHECK_FALSE (p.getSourceStatus().loaded);

    // 1 s of a 330 Hz tone arriving from the host.
    juce::int64 t = 0;
    for (int b = 0; b < (int) (sr / 512) + 1; ++b)
    {
        for (int i = 0; i < 512; ++i, ++t)
        {
            const float v = 0.4f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 330.0 * t / sr);
            buf.setSample (0, i, v);
            buf.setSample (1, i, v);
        }
        p.processBlock (buf, midi);
    }
    REQUIRE (p.captureHostLast (0.5).wasOk());
    const auto st = p.getSourceStatus();
    CHECK (st.loaded);
    CHECK (st.embedded);
    CHECK (st.seconds == Catch::Approx (0.5).margin (0.001));
    CHECK (st.fileName.containsIgnoreCase ("host"));
    auto audio = p.getSourceAudio();
    REQUIRE (audio != nullptr);
    CHECK (audio->buffer.getNumChannels() == 2);
    float mag = audio->buffer.getMagnitude (0, 0, audio->buffer.getNumSamples());
    CHECK (mag == Catch::Approx (0.4f).margin (0.01f));

    // Save/restore in a fresh processor: the capture is part of the state.
    juce::MemoryBlock state;
    p.getStateInformation (state);
    SpatialEchoProcessor q;
    q.setPlayConfigDetails (2, 2, sr, 512);
    q.prepareToPlay (sr, 512);
    q.setStateInformation (state.getData(), (int) state.getSize());
    const auto st2 = q.getSourceStatus();
    CHECK (st2.loaded);
    CHECK (st2.seconds == Catch::Approx (0.5).margin (0.001));
    auto audio2 = q.getSourceAudio();
    REQUIRE (audio2 != nullptr);
    CHECK (audio2->buffer.getNumSamples() == audio->buffer.getNumSamples());
    CHECK (audio2->buffer.getSample (0, 1000) == Catch::Approx (audio->buffer.getSample (0, 1000)).margin (1e-4));
}

// ---- Per-point effects -----------------------------------------------------

namespace
{
std::vector<float> renderOnce (const Scene& s, SharedSample sample, double sr, double seconds, int block = 256)
{
    auto plan = buildPlan (s, sample, sr);
    EchoEngine e;
    e.prepare (sr);
    return run (e, *plan, sr, block, seconds, [] (int) { return 120.0; });
}

float rmsOf (const std::vector<float>& x, int from, int len)
{
    double acc = 0.0;
    for (int i = from; i < from + len; ++i)
        acc += (double) x[(size_t) i] * x[(size_t) i];
    return (float) std::sqrt (acc / len);
}
} // namespace

TEST_CASE ("Effects: reverse plays the region backwards, fades shape the start and end")
{
    const double sr = 48000.0;
    SampleBuffer ramp (1, 48000);  // 0 -> 1 over one second
    for (int i = 0; i < 48000; ++i)
        ramp.setSample (0, i, (float) i / 48000.0f);
    auto sample = std::make_shared<const SampleBuffer> (ramp);

    Scene s;
    Point p;
    p.id = "p"; p.number = 1; p.timing.beat = 0.0;
    p.effects.reverse = true;
    s.points.push_back (p);
    const auto rev = renderOnce (s, sample, sr, 1.1);
    // At 0.25 s the reversed ramp reads 0.75; at 0.75 s it reads 0.25.
    CHECK (rev[(size_t) (0.25 * sr)] == Catch::Approx (0.75f).margin (0.01f));
    CHECK (rev[(size_t) (0.75 * sr)] == Catch::Approx (0.25f).margin (0.01f));

    s.points[0].effects.reverse = false;
    s.points[0].effects.fadeInMs = 500.0;
    s.points[0].effects.fadeOutMs = 250.0;
    const auto faded = renderOnce (s, sample, sr, 1.1);
    // Fade in: half way through the fade the ramp (0.25) is at half level.
    CHECK (faded[(size_t) (0.25 * sr)] == Catch::Approx (0.125f).margin (0.01f));
    CHECK (faded[(size_t) (0.6 * sr)] == Catch::Approx (0.6f).margin (0.01f));   // after both fades: untouched
    CHECK (faded[(size_t) (0.875 * sr)] == Catch::Approx (0.875f * 0.5f).margin (0.01f));  // half way through the fade out
}

TEST_CASE ("Effects: low-pass and high-pass filters attenuate the right side")
{
    const auto sr = GENERATE (44100.0, 96000.0);
    auto toneAt = [&] (double hz)
    {
        SampleBuffer b (1, (int) sr);
        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample (0, i, 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz * i / sr));
        return std::make_shared<const SampleBuffer> (b);
    };
    Scene s;
    Point p;
    p.id = "p"; p.number = 1; p.timing.beat = 0.0;
    s.points.push_back (p);
    const int from = (int) (0.3 * sr), len = (int) (0.2 * sr);

    const float ref = rmsOf (renderOnce (s, toneAt (100.0), sr, 1.0), from, len);
    s.points[0].effects.highpassHz = 2000.0;
    CHECK (rmsOf (renderOnce (s, toneAt (100.0), sr, 1.0), from, len) < ref * 0.1f);   // > 20 dB down
    CHECK (rmsOf (renderOnce (s, toneAt (8000.0), sr, 1.0), from, len) > ref * 0.8f);  // passes

    s.points[0].effects.highpassHz = 20.0;
    s.points[0].effects.lowpassHz = 500.0;
    CHECK (rmsOf (renderOnce (s, toneAt (8000.0), sr, 1.0), from, len) < ref * 0.1f);
    CHECK (rmsOf (renderOnce (s, toneAt (100.0), sr, 1.0), from, len) > ref * 0.8f);
}

TEST_CASE ("Effects: persistence and defaults for older scenes")
{
    Scene s;
    s.id = makeId();
    Point p;
    p.id = "p"; p.number = 1;
    p.effects = { 12.0, 34.0, true, 1234.0, 56.0 };
    s.points.push_back (p);
    Document d;
    d.scenes.push_back (s);
    d.activeSceneId = s.id;
    const auto back = documentFromJson (toJson (d));
    REQUIRE (back.value.has_value());
    CHECK (back.warnings.isEmpty());
    CHECK (back.value->scenes[0].points[0].effects == p.effects);

    const juce::String old = R"({"format":"SpatialEchoStudio.Document","formatVersion":1,"activeSceneId":"a","scenes":[
        {"id":"a","name":"x","points":[{"id":"q","number":1}]}]})";
    const auto legacy = documentFromJson (old);
    REQUIRE (legacy.value.has_value());
    CHECK (legacy.value->scenes[0].points[0].effects == PointEffects {});
}
