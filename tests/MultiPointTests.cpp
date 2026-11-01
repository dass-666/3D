// B3: several independent repetitions, live edits, protection and bypass.

#include "audio/EchoEngine.h"
#include "audio/SafetyLimiter.h"
#include "model/DemoScene.h"
#include "plugin/PluginProcessor.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>

using namespace ses;

namespace
{
SharedSample constSample (int channels, int length, float v)
{
    auto b = std::make_shared<SampleBuffer> (channels, length);
    for (int ch = 0; ch < channels; ++ch)
        juce::FloatVectorOperations::fill (b->getWritePointer (ch), v, length);
    return b;
}

Scene sceneWith (std::initializer_list<std::pair<double, Position>> points)
{
    Scene s;
    s.id = "s";
    int n = 1;
    for (auto& [beat, pos] : points)
    {
        Point p;
        p.id = "p" + juce::String (n);
        p.number = n++;
        p.timing.beat = beat;
        p.position = pos;
        s.points.push_back (p);
    }
    return s;
}

struct Runner
{
    double sr;
    int block;
    EchoEngine e;
    int done = 0;
    double bpm = 120.0;

    Runner (double s, int b) : sr (s), block (b) { e.prepare (sr); }

    // Returns left and right of `blocks` blocks of silence processed with the host playing from beat 0.
    std::pair<std::vector<float>, std::vector<float>> run (const PlaybackPlan* plan, int blocks, bool triggerFirst)
    {
        std::vector<float> l, r;
        juce::AudioBuffer<float> buf (2, block);
        for (int b = 0; b < blocks; ++b)
        {
            buf.clear();
            TransportInfo t { bpm, done * bpm / 60.0 / sr, true };
            e.process (buf, plan, t, triggerFirst && b == 0, false, 1.0f, 1.0f);
            for (int i = 0; i < block; ++i)
            {
                l.push_back (buf.getSample (0, i));
                r.push_back (buf.getSample (1, i));
            }
            done += block;
        }
        return { l, r };
    }
};

int firstNonZeroFrom (const std::vector<float>& x, int from)
{
    for (size_t i = (size_t) from; i < x.size(); ++i)
        if (x[i] != 0.0f)
            return (int) i;
    return -1;
}

float maxStep (const std::vector<float>& x)
{
    float m = 0.0f;
    for (size_t i = 1; i < x.size(); ++i)
        m = std::max (m, std::abs (x[i] - x[i - 1]));
    return m;
}
} // namespace

TEST_CASE ("Several points play independently at their own beats, sample-exact")
{
    const auto sr = GENERATE (44100.0, 48000.0, 96000.0);
    const auto block = GENERATE (64, 500, 2048);
    Runner run (sr, block);
    const auto scene = sceneWith ({ { 1.0, { 50, 50 } }, { 2.5, { 0, 50 } }, { 2.5, { 100, 50 } } });
    const auto sampleLen = (int) (0.1 * sr);
    auto plan = buildPlan (scene, constSample (1, sampleLen, 0.5f), sr);

    const int total = (int) (2.0 * sr) + sampleLen + block;
    const auto [l, r] = run.run (plan.get(), total / block + 1, true);

    const int b1 = (int) std::llround (1.0 * 0.5 * sr);   // beat 1 at 120 BPM
    const int b25 = (int) std::llround (2.5 * 0.5 * sr);
    CHECK (firstNonZeroFrom (l, 0) == b1);
    CHECK (firstNonZeroFrom (r, 0) == b1);
    // After point 1 ends, only the two hard-panned points remain: left from p2, right from p3.
    CHECK (firstNonZeroFrom (l, b1 + sampleLen) == b25);
    CHECK (firstNonZeroFrom (r, b1 + sampleLen) == b25);
    CHECK (l[(size_t) b25 + 1000] == Catch::Approx (0.5f));
    CHECK (r[(size_t) b25 + 1000] == Catch::Approx (0.5f));
    CHECK (run.e.stats().fired == 3);
    CHECK (run.e.stats().dropped == 0);
}

TEST_CASE ("16 points triggered at once: none dropped")
{
    Runner run (48000.0, 256);
    Scene s;
    for (int i = 0; i < 16; ++i)
    {
        Point p;
        p.id = "id" + juce::String (i);
        p.number = i + 1;
        p.timing.beat = 0.25 * i;
        p.position = { (double) (i * 6), 50 };
        s.points.push_back (p);
    }
    auto plan = buildPlan (s, constSample (2, 48000, 0.1f), 48000.0);
    run.run (plan.get(), 600, true);
    CHECK (run.e.stats().fired == 16);
    CHECK (run.e.stats().dropped == 0);
}

TEST_CASE ("Moving a point while it sounds changes pan smoothly, without a cut")
{
    Runner run (48000.0, 128);
    auto scene = sceneWith ({ { 0.0, { 0, 50 } } });
    const auto sample = constSample (1, 48000, 0.5f);
    auto planA = buildPlan (scene, sample, 48000.0);
    auto [l1, r1] = run.run (planA.get(), 50, true);
    REQUIRE (l1.back() == Catch::Approx (0.5f));
    REQUIRE (r1.back() == Catch::Approx (0.0f).margin (1e-6));

    scene.points[0].position = { 100, 50 };  // drag hard right
    auto planB = buildPlan (scene, sample, 48000.0);
    auto [l2, r2] = run.run (planB.get(), 20, false);
    CHECK (run.e.numActiveVoices() == 1);
    CHECK (l2.back() == Catch::Approx (0.0f).margin (1e-6));
    CHECK (r2.back() == Catch::Approx (0.5f));
    l1.insert (l1.end(), l2.begin(), l2.end());
    CHECK (maxStep (l1) < 0.5f / 96.0f);  // no larger than the 2 ms onset fade: never a jump
}

TEST_CASE ("Muting a sounding point fades it out in about 10 ms")
{
    Runner run (48000.0, 256);
    auto scene = sceneWith ({ { 0.0, { 50, 50 } } });
    const auto sample = constSample (1, 48000, 0.5f);
    auto planA = buildPlan (scene, sample, 48000.0);
    auto [l1, r1] = run.run (planA.get(), 10, true);
    scene.points[0].enabled = false;
    auto planB = buildPlan (scene, sample, 48000.0);
    auto [l2, r2] = run.run (planB.get(), 4, false);
    CHECK (run.e.numActiveVoices() == 0);
    CHECK (maxStep (l2) < 0.5f / 400.0f);
    CHECK (l2[(size_t) 600] == 0.0f);  // silent after 10 ms (480 samples)
}

TEST_CASE ("Changing the fragment fades out voices of the old one instead of cutting them")
{
    Runner run (48000.0, 256);
    const auto scene = sceneWith ({ { 0.0, { 50, 50 } } });
    const auto oldSample = constSample (1, 48000, 0.5f);
    auto planA = buildPlan (scene, oldSample, 48000.0);
    run.run (planA.get(), 10, true);
    auto planB = buildPlan (scene, constSample (1, 48000, 0.2f), 48000.0);
    planB->owned.push_back (oldSample);
    auto [l, r] = run.run (planB.get(), 4, false);
    CHECK (maxStep (l) < 0.5f / 400.0f);
    CHECK (run.e.numActiveVoices() == 0);

    // Without the previous sample in the plan the voice stops (it may not read freed memory).
    run.run (planA.get(), 10, true);
    auto planC = buildPlan (scene, constSample (1, 48000, 0.2f), 48000.0);
    run.run (planC.get(), 1, false);
    CHECK (run.e.numActiveVoices() == 0);
}

TEST_CASE ("A pending repetition follows a live timing edit")
{
    Runner run (48000.0, 480);
    auto scene = sceneWith ({ { 4.0, { 50, 50 } } });
    const auto sample = constSample (1, 4800, 0.5f);
    auto planA = buildPlan (scene, sample, 48000.0);
    auto [l1, r1] = run.run (planA.get(), 10, true);  // 0.2 beats in, echo pending at beat 4
    scene.points[0].timing.beat = 1.0;
    auto planB = buildPlan (scene, sample, 48000.0);
    auto [l2, r2] = run.run (planB.get(), 100, false);
    l1.insert (l1.end(), l2.begin(), l2.end());
    CHECK (firstNonZeroFrom (l1, 0) == 24000);  // beat 1 at 120 BPM
}

TEST_CASE ("Fragment preview plays centred at unity, independent of points")
{
    EchoEngine e;
    e.prepare (48000.0);
    const auto scene = sceneWith ({ { 1.0, { 0, 0 } } });
    auto plan = buildPlan (scene, constSample (1, 1000, 0.5f), 48000.0);
    juce::AudioBuffer<float> buf (2, 512);
    buf.clear();
    e.process (buf, plan.get(), { 120.0, std::nullopt, false }, false, false, 1.0f, 1.0f, true);
    CHECK (buf.getSample (0, 0) != 0.0f);
    CHECK (buf.getSample (0, 300) == Catch::Approx (0.5f));
    CHECK (buf.getSample (1, 300) == Catch::Approx (0.5f));
}

TEST_CASE ("Safety limiter: never above the ceiling, transparent below it")
{
    SafetyLimiter lim;
    lim.prepare (48000.0);
    juce::AudioBuffer<float> buf (2, 4800);
    for (int i = 0; i < 4800; ++i)
    {
        const float v = (float) (std::sin (i * 0.05) * (i < 2400 ? 2.5 : 0.3));
        buf.setSample (0, i, v);
        buf.setSample (1, i, -v);
    }
    lim.process (buf);
    for (int i = 0; i < 4800; ++i)
        REQUIRE (std::abs (buf.getSample (0, i)) <= SafetyLimiter::kCeiling + 1e-6f);

    SafetyLimiter quiet;
    quiet.prepare (48000.0);
    juce::AudioBuffer<float> q (1, 1000), ref (1, 1000);
    for (int i = 0; i < 1000; ++i)
        q.setSample (0, i, (float) std::sin (i * 0.1) * 0.9f);
    ref.makeCopyOf (q);
    quiet.process (q);
    for (int i = 0; i < 1000; ++i)
        REQUIRE (q.getSample (0, i) == ref.getSample (0, i));
}

namespace
{
struct Head final : juce::AudioPlayHead
{
    juce::Optional<PositionInfo> getPosition() const override { return info; }
    PositionInfo info;
};
}

TEST_CASE ("Processor: 16 loud repetitions over a hot input stay under 0 dBFS; bypass returns the exact input")
{
    juce::ScopedJuceInitialiser_GUI init;
    const double sr = 48000.0;
    const int block = 256;
    SpatialEchoProcessor p;
    p.prepareToPlay (sr, block);

    juce::MemoryBlock wav;
    {
        juce::AudioBuffer<float> a (1, 9600);
        for (int i = 0; i < 9600; ++i)
            a.setSample (0, i, 0.9f * (float) std::sin (i * 0.03));
        juce::WavAudioFormat fmt;
        auto* stream = new juce::MemoryOutputStream (wav, false);
        std::unique_ptr<juce::AudioFormatWriter> w (fmt.createWriterFor (stream, sr, 1, 24, {}, 0));
        w->writeFromAudioSampleBuffer (a, 0, 9600);
    }
    REQUIRE (p.loadSourceBytes (wav, "C:/loud.wav").wasOk());
    p.editScene ([] (Scene& s)
    {
        s.points.clear();
        for (int i = 0; i < 16; ++i)
        {
            Point pt;
            pt.id = "x" + juce::String (i);
            pt.number = i + 1;
            pt.timing.beat = 0.0;  // all at once
            s.points.push_back (pt);
        }
    });
    p.getParameters().getParameter (params::mix)->setValueNotifyingHost (1.0f);

    Head head;
    head.info.setBpm (120.0);
    head.info.setIsPlaying (true);
    p.setPlayHead (&head);
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buf (2, block), in (2, block);
    float peak = 0.0f;
    p.requestTrigger();
    for (int b = 0; b < 100; ++b)
    {
        head.info.setPpqPosition (b * block * 2.0 / sr);
        for (int i = 0; i < block; ++i)
            for (int ch = 0; ch < 2; ++ch)
                buf.setSample (ch, i, 0.5f);
        p.processBlock (buf, midi);
        peak = std::max (peak, buf.getMagnitude (0, block));
    }
    CHECK (p.getHostSnapshot().fired.load() == 16);
    CHECK (peak <= SafetyLimiter::kCeiling + 1e-5f);

    p.getParameters().getParameter (params::bypass)->setValueNotifyingHost (1.0f);
    for (int b = 0; b < 20; ++b)
    {
        head.info.setPpqPosition ((100 + b) * block * 2.0 / sr);
        for (int i = 0; i < block; ++i)
            for (int ch = 0; ch < 2; ++ch)
                buf.setSample (ch, i, 0.01f * (float) i);
        in.makeCopyOf (buf);
        p.requestTrigger();
        p.processBlock (buf, midi);
    }
    for (int i = 0; i < block; ++i)
        REQUIRE (buf.getSample (0, i) == in.getSample (0, i));
    p.setPlayHead (nullptr);
}

TEST_CASE ("Processor: editing the scene is cheap for the UI and the edits are saved")
{
    juce::ScopedJuceInitialiser_GUI init;
    SpatialEchoProcessor p;
    p.prepareToPlay (48000.0, 512);
    const auto v0 = p.getDocumentVersion();
    p.editScene ([] (Scene& s) { s.points[0].position = { 10, 20 }; s.points[1].solo = true; });
    CHECK (p.getDocumentVersion() != v0);

    juce::MemoryBlock state;
    p.getStateInformation (state);
    SpatialEchoProcessor q;
    q.setStateInformation (state.getData(), (int) state.getSize());
    const auto s = q.getSceneForUi();
    CHECK (s.points[0].position == Position { 10, 20 });
    CHECK (s.points[1].solo);
}
