// «Ecos en vivo» (B11): the points repeating the live input.

#include "audio/LiveEchoes.h"
#include "model/Serialization.h"
#include "plugin/PluginProcessor.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <chrono>
#include <cmath>
#include <functional>

using namespace ses;

namespace
{
constexpr double kSr = 48000.0;

Point pointAt (const juce::String& id, double beat, double seconds = 0.0)
{
    Point p;
    p.id = id;
    p.timing.beat = beat;
    p.timing.seconds = seconds;
    return p;
}

Scene liveScene (std::vector<Point> points, TimeMode mode = TimeMode::Beats)
{
    Scene s;
    s.timeMode = mode;
    s.points = std::move (points);
    s.live.enabled = true;
    s.live.echoes = true;
    return s;
}

using Input = std::function<float (juce::int64)>;

// Runs `seconds` of input through LiveEchoes at 120 BPM (host stopped: internal clock),
// with `block` samples per call; returns only the echoes (dry input not included).
juce::AudioBuffer<float> runEchoes (LiveEchoes& le, const LiveEchoPlan* plan, double seconds, const Input& in,
                                    int block = 512, const std::function<void (juce::int64)>& before = {})
{
    const auto total = (int) (seconds * kSr);
    juce::AudioBuffer<float> out (2, total);
    out.clear();
    TransportInfo t;
    t.bpm = 120.0;
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = juce::jmin (block, total - pos);
        if (before)
            before (pos);
        juce::AudioBuffer<float> b (2, n);
        for (int i = 0; i < n; ++i)
            b.setSample (0, i, in (pos + i)), b.setSample (1, i, in (pos + i));
        le.pushInput (b, n);
        b.clear();
        le.process (b, n, plan, t, 120.0, 1.0f, 1.0f);
        for (int ch = 0; ch < 2; ++ch)
            out.copyFrom (ch, pos, b, ch, 0, n);
    }
    return out;
}

int peakIndex (const juce::AudioBuffer<float>& a, int ch, int from, int to)
{
    int best = from;
    for (int i = from; i < to; ++i)
        if (std::abs (a.getSample (ch, i)) > std::abs (a.getSample (ch, best)))
            best = i;
    return best;
}

float rms (const juce::AudioBuffer<float>& a, int ch, double from, double to)
{
    return a.getRMSLevel (ch, (int) (from * kSr), (int) ((to - from) * kSr));
}

int zeroCrossings (const juce::AudioBuffer<float>& a, double from, double to)
{
    int z = 0;
    for (int i = (int) (from * kSr) + 1; i < (int) (to * kSr); ++i)
        z += (a.getSample (0, i - 1) < 0.0f) != (a.getSample (0, i) < 0.0f) ? 1 : 0;
    return z;
}

float sine (juce::int64 i, double hz) { return (float) (0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * hz * (double) i / kSr)); }
} // namespace

TEST_CASE ("Live echoes: off by default, saved with the scene, older scenes load them off")
{
    Scene s;
    CHECK_FALSE (s.live.echoes);
    CHECK_FALSE (makeLiveEchoPlan (s, kSr).enabled);
    s.live.enabled = true;
    CHECK_FALSE (makeLiveEchoPlan (s, kSr).enabled);  // the main live path alone does not add echoes

    s = liveScene ({ pointAt ("a", 1.0) });
    s.id = "a";
    Document doc;
    doc.scenes.push_back (s);
    doc.activeSceneId = "a";
    const auto json = toJson (doc);
    const auto back = documentFromJson (json);
    REQUIRE (back);
    CHECK (back.value->scenes[0].live.echoes);

    auto parsed = juce::JSON::parse (json);
    parsed["scenes"][0]["live"].getDynamicObject()->removeProperty ("echoes");
    const auto old = documentFromJson (juce::JSON::toString (parsed));
    REQUIRE (old);
    CHECK_FALSE (old.value->scenes[0].live.echoes);
}

TEST_CASE ("Live echoes: each point repeats the input at its moment (beats and free time), any block size")
{
    LiveEchoes le;
    le.prepare (kSr);
    const auto impulse = [] (juce::int64 i) { return i == 1000 ? 1.0f : 0.0f; };

    for (int block : { 512, 64, 3000 })
    {
        le.reset();
        const auto plan = makeLiveEchoPlan (liveScene ({ pointAt ("a", 1.0) }), kSr);  // 1 beat at 120 BPM = 0.5 s
        REQUIRE (plan.numTaps == 1);
        const auto out = runEchoes (le, &plan, 0.8, impulse, block);
        const int at = peakIndex (out, 0, 0, out.getNumSamples());
        CHECK (std::abs (at - (1000 + 24000)) <= 1);
        CHECK (std::abs (out.getSample (0, at)) > 0.8f);
        CHECK (out.getMagnitude (0, 0, 1000 + 23900) < 1.0e-6f);  // nothing before the echo
    }

    le.reset();
    auto free = liveScene ({ pointAt ("a", 0.0, 0.030) }, TimeMode::Free);  // 30 ms
    const auto plan = makeLiveEchoPlan (free, kSr);
    const auto out = runEchoes (le, &plan, 0.3, impulse);
    CHECK (std::abs (peakIndex (out, 0, 0, out.getNumSamples()) - (1000 + 1440)) <= 1);
}

TEST_CASE ("Live echoes: position, level and Mix are applied per point; no feedback of the output")
{
    LiveEchoes le;
    le.prepare (kSr);
    auto left = pointAt ("l", 0.0, 0.010);
    left.position.x = 0.0;
    auto right = pointAt ("r", 0.0, 0.020);
    right.position.x = 100.0;
    right.gainDb = -6.0;
    const auto plan = makeLiveEchoPlan (liveScene ({ left, right }, TimeMode::Free), kSr);
    REQUIRE (plan.numTaps == 2);
    const auto out = runEchoes (le, &plan, 0.2, [] (juce::int64 i) { return i == 2000 ? 1.0f : 0.0f; });
    CHECK (std::abs (out.getSample (0, 2000 + 480)) > 0.8f);   // left echo, left channel only
    CHECK (std::abs (out.getSample (1, 2000 + 480)) < 1.0e-3f);
    CHECK_THAT (std::abs (out.getSample (1, 2000 + 960)), Catch::Matchers::WithinAbs (0.5, 0.08));  // right, -6 dB
    CHECK (std::abs (out.getSample (0, 2000 + 960)) < 1.0e-3f);
    // No repeats of repeats: nothing after the two echoes.
    CHECK (out.getMagnitude (0, 2000 + 1000, out.getNumSamples() - 3100) < 1.0e-4f);
    CHECK (out.getMagnitude (1, 2000 + 1500, out.getNumSamples() - 3600) < 1.0e-4f);
}

TEST_CASE ("Live echoes: removing a point fades it out instead of cutting it; points too far are reported")
{
    LiveEchoes le;
    le.prepare (kSr);
    const auto plan = makeLiveEchoPlan (liveScene ({ pointAt ("a", 0.0, 0.050) }, TimeMode::Free), kSr);
    const auto off = makeLiveEchoPlan (liveScene ({}, TimeMode::Free), kSr);
    const LiveEchoPlan* current = &plan;
    juce::AudioBuffer<float> res (2, 512 * 94);
    res.clear();
    TransportInfo t;
    for (int pos = 0; pos < res.getNumSamples(); pos += 512)
    {
        if (pos >= 24064)
            current = &off;
        juce::AudioBuffer<float> b (2, 512);
        for (int i = 0; i < 512; ++i)
            b.setSample (0, i, sine (pos + i, 200.0)), b.setSample (1, i, sine (pos + i, 200.0));
        le.pushInput (b, 512);
        b.clear();
        le.process (b, 512, current, t, 120.0, 1.0f, 1.0f);
        res.copyFrom (0, pos, b, 0, 0, 512);
    }
    // Largest step between samples stays that of the sine itself (no click).
    float step = 0.0f;
    for (int i = 24000; i < 26000; ++i)
        step = juce::jmax (step, std::abs (res.getSample (0, i) - res.getSample (0, i - 1)));
    CHECK (step < 0.5f * 2.0f * 3.1416f * 200.0f / (float) kSr * 1.2f);
    CHECK (res.getMagnitude (0, 24064 + 2000, 4000) < 1.0e-5f);  // gone after the 20 ms fade

    const auto far = makeLiveEchoPlan (liveScene ({ pointAt ("a", 0.0, 9.0), pointAt ("b", 0.0, 1.0) }, TimeMode::Free), kSr);
    CHECK (far.numTaps == 1);
    CHECK (far.skippedTooFar == 1);
}

TEST_CASE ("Live echoes: Stutter replays the start of every cycle of the delayed signal")
{
    LiveEchoes le;
    le.prepare (kSr);
    auto p = pointAt ("s", 0.0);
    p.stutter.enabled = true;
    p.stutter.region = StutterSettings::Region::Start;
    p.stutter.repeats = 4;
    p.stutter.division = StutterSettings::Division::Eighth;  // 0.5 beat = 0.25 s at 120 BPM
    p.stutter.sliceLengthPercent = 40.0;                      // 0.1 s slices
    auto scene = liveScene ({ p });
    scene.live.cycleBeats = 4.0;  // 2 s
    const auto plan = makeLiveEchoPlan (scene, kSr);
    REQUIRE (plan.taps[0].sliceCount == 4);
    // A 0.08 s tone at the start of each 2 s cycle, silence otherwise.
    const auto out = runEchoes (le, &plan, 4.0, [] (juce::int64 i) { return (i % 96000) < 3840 ? sine (i, 300.0) : 0.0f; });
    for (int k = 0; k < 4; ++k)
    {
        const double at = 2.0 + 0.25 * k;  // second cycle
        CHECK (rms (out, 0, at + 0.01, at + 0.07) > 0.25f);   // the replayed start
        CHECK (rms (out, 0, at + 0.11, at + 0.24) < 1.0e-4f);  // gap between slices
    }
    CHECK (rms (out, 0, 3.05, 3.95) < 1.0e-4f);  // after the last slice: nothing until the next cycle
}

TEST_CASE ("Live echoes: pitch shifts the repetition; a pitched echo is never earlier than the look-ahead")
{
    LiveEchoes le;
    le.prepare (kSr);
    const double look = le.pitchLatencySamples() / kSr;
    CHECK (look > 0.12);  // the guide and the panel say «about 140 ms»
    CHECK (look < 0.16);

    auto p = pointAt ("p", 1.0);  // 0.5 s
    p.sound.pitchSemitones = 12.0;
    auto plan = makeLiveEchoPlan (liveScene ({ p }), kSr);
    auto out = runEchoes (le, &plan, 2.0, [] (juce::int64 i) { return sine (i, 220.0); });
    const int z = zeroCrossings (out, 1.0, 2.0);  // 440 Hz -> about 880 crossings per second
    CHECK (z > 820);
    CHECK (z < 940);
    // Timing: the pitched echo starts at its moment (0.5 s), not later.
    CHECK (rms (out, 0, 0.0, 0.45) < 1.0e-3f);
    CHECK (rms (out, 0, 0.6, 0.9) > 0.15f);

    // A pitched point at 0 ms sounds at the look-ahead.
    le.reset();
    auto near = pointAt ("n", 0.0);
    near.sound.pitchSemitones = 5.0;
    plan = makeLiveEchoPlan (liveScene ({ near }), kSr);
    out = runEchoes (le, &plan, 1.0, [] (juce::int64 i) { return sine (i, 220.0); });
    // Only the stretcher window's faint pre-ring (under -40 dB of the echo) comes before it.
    CHECK (rms (out, 0, 0.0, look * 0.8) < 3.5e-3f);
    CHECK (rms (out, 0, look + 0.1, look + 0.3) > 0.15f);
}

TEST_CASE ("Live echoes: the Volume Shaper cycle follows each echo")
{
    LiveEchoes le;
    le.prepare (kSr);
    auto p = pointAt ("v", 1.0);  // 0.5 s
    auto& sh = p.shaper;
    sh.enabled = true;
    sh.shape = ShaperSettings::Shape::Square;
    sh.rateMode = ShaperSettings::RateMode::Count;
    sh.cuts = 4;
    sh.depthPercent = 100.0;
    auto scene = liveScene ({ p });
    scene.live.cycleBeats = 4.0;  // 2 s cycle, 0.5 s cuts open for their first half
    const auto plan = makeLiveEchoPlan (scene, kSr);
    const auto out = runEchoes (le, &plan, 3.0, [] (juce::int64 i) { return sine (i, 300.0); });
    // The pattern starts with the echo (0.5 s later than the input's cycle): open 0.55..0.70, closed 0.80..0.95.
    CHECK (rms (out, 0, 0.55, 0.70) > 0.2f);
    CHECK (rms (out, 0, 0.80, 0.95) < 0.02f);
}

TEST_CASE ("Live echoes: in the plugin they follow Mix, report no latency and leave the dry input intact")
{
    juce::ScopedJuceInitialiser_GUI init;
    SpatialEchoProcessor proc;
    proc.prepareToPlay (kSr, 512);
    proc.editScene ([] (Scene& s)
    {
        s.timeMode = TimeMode::Free;
        s.points = { pointAt ("a", 0.0, 0.1) };
        s.live.enabled = true;
        s.live.wetPercent = 0.0;  // main path: dry
        s.live.echoes = true;
    });
    proc.getParameters().getParameter ("mix")->setValueNotifyingHost (0.5f);  // 50 %
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> out (2, 512 * 47);
    for (int pos = 0; pos < out.getNumSamples(); pos += 512)
    {
        juce::AudioBuffer<float> b (2, 512);
        b.clear();
        if (pos == 9728)
            b.setSample (0, 272, 0.5f), b.setSample (1, 272, 0.5f);  // impulse at 10000 (below the limiter)
        proc.processBlock (b, midi);
        out.copyFrom (0, pos, b, 0, 0, 512);
        out.copyFrom (1, pos, b, 1, 0, 512);
    }
    CHECK (proc.getLatencySamples() == 0);
    CHECK (proc.isLiveProcessing());
    CHECK_THAT (out.getSample (0, 10000), Catch::Matchers::WithinAbs (0.5, 1.0e-6));  // dry, untouched
    const int at = peakIndex (out, 0, 10100, out.getNumSamples());
    CHECK (std::abs (at - 14800) <= 1);
    CHECK_THAT (std::abs (out.getSample (0, at)), Catch::Matchers::WithinAbs (0.25, 0.04));  // 50 % Mix
}

TEST_CASE ("Live echoes stress: 16 pitched points with Stutter, shaper and effects in real time", "[.stress]")
{
    LiveEchoes le;
    le.prepare (kSr);
    std::vector<Point> pts;
    for (int k = 0; k < 16; ++k)
    {
        auto p = pointAt ("p" + juce::String (k), 0.25 * k);
        p.sound.pitchSemitones = (k % 5) - 2.0;
        p.stutter.enabled = (k % 3) == 0;
        p.shaper.enabled = true;
        p.effects.reverb.enabled = (k % 2) == 0;
        p.effects.delay.enabled = (k % 2) == 1;
        p.effects.modulation.enabled = true;
        pts.push_back (p);
    }
    const auto plan = makeLiveEchoPlan (liveScene (pts), kSr);
    REQUIRE (plan.numTaps == 16);
    const double seconds = 10.0;
    const auto t0 = std::chrono::steady_clock::now();
    runEchoes (le, &plan, seconds, [] (juce::int64 i) { return sine (i, 220.0); });
    const double used = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    WARN ("16 live echoes (pitch, stutter, shaper, fx): " << (100.0 * used / seconds) << " % of real time");
}
