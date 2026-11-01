// B4: real pitch shifting (duration kept), per-point gain, individual trigger, stutter.

#include "audio/EchoEngine.h"
#include "audio/PitchRenderer.h"
#include "model/Serialization.h"
#include "plugin/PluginProcessor.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <complex>

using namespace ses;

namespace
{
SampleBuffer sine (double freq, double sr, double seconds, int channels = 1, float amp = 0.5f)
{
    const int n = (int) (seconds * sr);
    SampleBuffer b (channels, n);
    for (int ch = 0; ch < channels; ++ch)
        for (int i = 0; i < n; ++i)
            b.setSample (ch, i, amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * freq * i / sr));
    return b;
}

// Dominant frequency of x[from..from+len) by a plain DFT peak search around a window (Goertzel-style scan).
double dominantHz (const float* x, int len, double sr, double lo, double hi)
{
    double best = 0.0, bestMag = -1.0;
    for (double f = lo; f <= hi; f += 0.5)
    {
        std::complex<double> acc;
        for (int i = 0; i < len; ++i)
            acc += (double) x[i] * std::polar (1.0, -2.0 * juce::MathConstants<double>::pi * f * i / sr);
        const double mag = std::abs (acc);
        if (mag > bestMag)
        {
            bestMag = mag;
            best = f;
        }
    }
    return best;
}

float rms (const float* x, int len)
{
    double acc = 0.0;
    for (int i = 0; i < len; ++i)
        acc += (double) x[i] * x[i];
    return (float) std::sqrt (acc / juce::jmax (1, len));
}

juce::MemoryBlock wavOf (const SampleBuffer& a, double sr)
{
    juce::MemoryBlock block;
    juce::WavAudioFormat wav;
    auto* stream = new juce::MemoryOutputStream (block, false);
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (stream, sr, (unsigned) a.getNumChannels(), 24, {}, 0));
    w->writeFromAudioSampleBuffer (a, 0, a.getNumSamples());
    w.reset();
    return block;
}

struct Head final : juce::AudioPlayHead
{
    juce::Optional<PositionInfo> getPosition() const override { return info; }
    PositionInfo info;
};

// Onsets: sample indices where the signal goes from silence to sound
// (silence = nothing above the threshold in the previous 64 samples).
std::vector<int> onsets (const std::vector<float>& x, float thresh = 1e-4f)
{
    std::vector<int> out;
    int quiet = 64;
    for (size_t i = 0; i < x.size(); ++i)
    {
        if (std::abs (x[i]) > thresh)
        {
            if (quiet >= 64)
                out.push_back ((int) i);
            quiet = 0;
        }
        else
            ++quiet;
    }
    return out;
}
} // namespace

TEST_CASE ("Pitch render: transposes by the requested semitones and keeps the duration")
{
    const auto sr = GENERATE (44100.0, 48000.0, 96000.0);
    const auto semis = GENERATE (-12.0, -5.0, 7.0, 12.0, 24.0);
    const double f0 = 220.0;
    const auto in = sine (f0, sr, 0.8);
    const auto out = renderPitched (in, sr, semis);

    REQUIRE (out.getNumSamples() == in.getNumSamples());
    REQUIRE (out.getNumChannels() == 1);

    const double expected = f0 * std::pow (2.0, semis / 12.0);
    const int from = (int) (0.3 * sr), len = (int) (0.3 * sr);
    const double got = dominantHz (out.getReadPointer (0) + from, len, sr, expected * 0.9, expected * 1.1);
    INFO ("sr " << sr << " semis " << semis << " expected " << expected << " got " << got);
    CHECK (std::abs (got - expected) / expected < 0.01);  // within 1 % (about 17 cents)

    // Level comparable to the input, and no silence gap at the start of the sound.
    CHECK (rms (out.getReadPointer (0) + from, len) > 0.5f * rms (in.getReadPointer (0) + from, len));
    CHECK (rms (out.getReadPointer (0), (int) (0.05 * sr)) > 0.1f);
}

TEST_CASE ("Pitch render: stereo keeps both channels, zero semitones is near-identity in pitch")
{
    const double sr = 48000.0;
    auto in = sine (330.0, sr, 0.5, 2);
    const auto out = renderPitched (in, sr, 0.0);
    REQUIRE (out.getNumChannels() == 2);
    for (int ch = 0; ch < 2; ++ch)
    {
        const double got = dominantHz (out.getReadPointer (ch) + 12000, 12000, sr, 300.0, 360.0);
        CHECK (std::abs (got - 330.0) < 3.0);
    }
}

TEST_CASE ("PitchRenderer: renders in the background, caches per cents and drops stale results")
{
    juce::ScopedJuceInitialiser_GUI init;
    PitchRenderer r;
    auto base = std::make_shared<const SampleBuffer> (sine (220.0, 48000.0, 0.3));
    r.setBase (base, 48000.0);
    r.request ({ 0, 700, -500 });
    CHECK (r.numPending() == 2);
    CHECK (r.isPending (700));
    CHECK_FALSE (r.isPending (0));

    const auto t0 = juce::Time::getMillisecondCounter();
    while (r.numPending() > 0 && juce::Time::getMillisecondCounter() - t0 < 20000)
        juce::Thread::sleep (10);
    REQUIRE (r.numPending() == 0);
    auto ready = r.ready();
    REQUIRE (ready.count (700) == 1);
    REQUIRE (ready.count (-500) == 1);
    CHECK (ready[700]->getNumSamples() == base->getNumSamples());

    // A new base invalidates everything.
    r.setBase (std::make_shared<const SampleBuffer> (sine (110.0, 48000.0, 0.2)), 48000.0);
    CHECK (r.ready().empty());
    CHECK (r.numPending() == 2);
}

TEST_CASE ("Plan: pitched points use their rendered copy, others the base; missing renders are pending")
{
    Scene s;
    for (int i = 0; i < 3; ++i)
    {
        Point p;
        p.id = "p" + juce::String (i);
        p.number = i + 1;
        p.sound.pitchSemitones = i == 0 ? 0.0 : i == 1 ? 7.0 : -5.0;
        s.points.push_back (p);
    }
    auto base = std::make_shared<const SampleBuffer> (sine (220.0, 48000.0, 0.1));
    auto up = std::make_shared<const SampleBuffer> (sine (330.0, 48000.0, 0.1));
    std::map<int, SharedSample> pitched { { 700, up } };
    auto plan = buildPlan (s, base, pitched, 48000.0);
    REQUIRE (plan->echoes.size() == 3);
    CHECK (plan->find (keyForPoint ("p0"))->sample == base);
    CHECK (plan->find (keyForPoint ("p1"))->sample == up);
    CHECK (plan->find (keyForPoint ("p2"))->sample == base);
    CHECK (plan->find (keyForPoint ("p2"))->pitchPending);
    CHECK (plan->pendingPitchRenders == 1);
    CHECK (plan->owns (up.get()));
}

TEST_CASE ("Processor: two points with different pitch play different notes and their own gain, independently")
{
    juce::ScopedJuceInitialiser_GUI init;
    const double sr = 48000.0;
    const int block = 480;
    SpatialEchoProcessor p;
    p.prepareToPlay (sr, block);
    REQUIRE (p.loadSourceBytes (wavOf (sine (220.0, sr, 0.5), sr), "C:/la.wav").wasOk());
    p.editScene ([] (Scene& s)
    {
        s.points.clear();
        Point a, b;
        a.id = "a"; a.number = 1; a.timing.beat = 0.0; a.sound.pitchSemitones = 12.0; a.gainDb = -6.0;
        b.id = "b"; b.number = 2; b.timing.beat = 2.0; b.sound.pitchSemitones = 0.0; b.gainDb = 0.0;
        s.points.push_back (a);
        s.points.push_back (b);
    });
    p.getParameters().getParameter (params::mix)->setValueNotifyingHost (1.0f);
    p.getParameters().getParameter (params::protect)->setValueNotifyingHost (0.0f);

    const auto t0 = juce::Time::getMillisecondCounter();
    while (p.numPendingPitchRenders() > 0 && juce::Time::getMillisecondCounter() - t0 < 20000)
        juce::Thread::sleep (10);
    REQUIRE (p.numPendingPitchRenders() == 0);
    p.refreshPlan();  // in the plugin this happens through the message thread when a render finishes

    Head head;
    head.info.setBpm (120.0);
    head.info.setIsPlaying (true);
    p.setPlayHead (&head);
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buf (2, block);
    std::vector<float> out;
    p.requestPreview();
    for (int b = 0; b < 200; ++b)
    {
        head.info.setPpqPosition (b * block * 2.0 / sr);
        buf.clear();
        p.processBlock (buf, midi);
        for (int i = 0; i < block; ++i)
            out.push_back (buf.getSample (0, i));
    }
    p.setPlayHead (nullptr);

    // Point a: 0..0.5 s at 440 Hz and -6 dB. Point b: 1.0..1.5 s at 220 Hz, 0 dB.
    const int win = (int) (0.25 * sr);
    const double fa = dominantHz (out.data() + (int) (0.15 * sr), win, sr, 400.0, 480.0);
    const double fb = dominantHz (out.data() + (int) (1.15 * sr), win, sr, 200.0, 240.0);
    CHECK (std::abs (fa - 440.0) < 5.0);
    CHECK (std::abs (fb - 220.0) < 3.0);
    const float ra = rms (out.data() + (int) (0.15 * sr), win);
    const float rb = rms (out.data() + (int) (1.15 * sr), win);
    CHECK (rb == Catch::Approx (0.5f / std::sqrt (2.0f)).margin (0.02f));
    CHECK (ra / rb == Catch::Approx (juce::Decibels::decibelsToGain (-6.0f)).margin (0.08f));
    CHECK (rms (out.data() + (int) (0.6 * sr), (int) (0.3 * sr)) < 1e-4f);  // silence between them
}

TEST_CASE ("Processor: pitch, gain, preset and stutter survive save/restore; old volumePercent scenes still load")
{
    juce::ScopedJuceInitialiser_GUI init;
    SpatialEchoProcessor p;
    p.prepareToPlay (48000.0, 512);
    p.editScene ([] (Scene& s)
    {
        s.points[0].sound.pitchSemitones = -3.0;
        s.points[0].gainDb = -12.5;
        s.points[1].stutter.enabled = true;
        s.points[1].stutter.repeats = 6;
        s.points[1].stutter.division = StutterSettings::Division::ThirtySecond;
        s.points[1].stutter.region = StutterSettings::Region::Manual;
        s.points[1].stutter.manualStart = 0.2;
        s.points[1].stutter.manualEnd = 0.6;
        s.points[1].stutter.gainDb = 3.0;
    });
    juce::MemoryBlock state;
    p.getStateInformation (state);
    SpatialEchoProcessor q;
    q.setStateInformation (state.getData(), (int) state.getSize());
    const auto s = q.getSceneForUi();
    CHECK (s.points[0].sound.pitchSemitones == -3.0);
    CHECK (s.points[0].gainDb == -12.5);
    CHECK (s.points[1].stutter == p.getSceneForUi().points[1].stutter);
    CHECK (s == p.getSceneForUi());

    const auto json = juce::String (R"({"format":"SpatialEchoStudio.Document","formatVersion":1,"activeSceneId":"s1","scenes":[{"id":"s1","name":"old","points":[{"id":"x","number":1,"volumePercent":50}]}]})");
    auto loaded = documentFromJson (json);
    REQUIRE (loaded);
    CHECK (loaded.value->scenes[0].points[0].gainDb == Catch::Approx (20.0 * std::log10 (0.5)));
    CHECK_FALSE (loaded.value->scenes[0].points[0].stutter.enabled);
}

TEST_CASE ("Individual trigger plays only that point, now, with its own settings")
{
    EchoEngine e;
    e.prepare (48000.0);
    Scene s;
    Point a, b;
    a.id = "a"; a.number = 1; a.timing.beat = 3.0; a.position = { 0, 50 };
    b.id = "b"; b.number = 2; b.timing.beat = 0.0; b.position = { 100, 50 };
    s.points = { a, b };
    auto sample = std::make_shared<const SampleBuffer> (sine (220.0, 48000.0, 0.05));
    auto plan = buildPlan (s, sample, 48000.0);

    juce::AudioBuffer<float> buf (2, 512);
    EngineRequests req;
    req.triggerPoint = keyForPoint ("a");
    buf.clear();
    e.process (buf, plan.get(), { 120.0, 0.0, true }, req, 1.0f, 1.0f);
    CHECK (buf.getSample (0, 100) != 0.0f);                      // left: point a, right now (its beat 3 ignored)
    CHECK (buf.getSample (1, 100) == Catch::Approx (0.0f).margin (1e-6f));  // point b did not fire
    CHECK (e.stats().fired == 1);
    CHECK (e.numPendingEvents() == 0);

    // A second click while it sounds starts another voice (no lock-up, no double trigger per click).
    buf.clear();
    e.process (buf, plan.get(), { 120.0, 512 * 2.0 / 48000.0, true }, req, 1.0f, 1.0f);
    CHECK (e.stats().fired == 2);
    CHECK (e.numActiveVoices() == 2);
}

TEST_CASE ("Stutter: N slices at the division spacing, slice length limited, none dropped at 16 x 32")
{
    const auto sr = GENERATE (44100.0, 48000.0, 96000.0);
    const auto block = GENERATE (64, 512, 2048);
    const auto bpm = GENERATE (90.0, 120.0, 150.0);
    EchoEngine e;
    e.prepare (sr);
    Scene s;
    Point p;
    p.id = "p"; p.number = 1; p.timing.beat = 1.0;
    p.stutter.enabled = true;
    p.stutter.repeats = 8;
    p.stutter.division = StutterSettings::Division::Sixteenth;  // 0.25 beat (125 ms at 120 BPM)
    p.stutter.sliceLengthPercent = 50.0;                          // half sound, half gap
    p.stutter.region = StutterSettings::Region::Start;
    s.points.push_back (p);
    auto sample = std::make_shared<const SampleBuffer> (sine (220.0, sr, 1.0));  // longer than any slice
    auto plan = buildPlan (s, sample, sr);
    REQUIRE (plan->echoes.size() == 8);

    std::vector<float> out;
    juce::AudioBuffer<float> buf (1, block);
    const int total = (int) (2.0 * sr);
    int done = 0;
    while (done < total)
    {
        buf.clear();
        e.process (buf, plan.get(), { bpm, done * bpm / 60.0 / sr, true }, done == 0, false, 1.0f, 1.0f);
        for (int i = 0; i < block; ++i)
            out.push_back (buf.getSample (0, i));
        done += block;
    }
    const auto on = onsets (out);
    REQUIRE (on.size() == 8);
    const double beatSamples = 60.0 / bpm * sr;
    const double step = 0.25 * beatSamples;
    for (size_t k = 0; k < on.size(); ++k)
        CHECK (std::abs (on[k] - (beatSamples + k * step)) <= 2.0);
    // Each slice sounds for half the step: silence again before the next one.
    const int mid = (int) (beatSamples + 0.72 * step);
    CHECK (rms (out.data() + mid, (int) (0.1 * step)) < 1e-4f);
    CHECK (e.stats().dropped == 0);
    // No clicks: every slice starts and ends with a fade, so consecutive
    // samples never jump more than the tone itself moves (plus fade slope).
    float maxStep = 0.0f;
    for (size_t i = 1; i < out.size(); ++i)
        maxStep = std::max (maxStep, std::abs (out[i] - out[i - 1]));
    CHECK (maxStep < 0.08f);

    // Worst case: 16 points x 32 slices scheduled at once.
    Scene big;
    for (int i = 0; i < 16; ++i)
    {
        Point q;
        q.id = "q" + juce::String (i); q.number = i + 1; q.timing.beat = 0.0;
        q.stutter.enabled = true; q.stutter.repeats = 32; q.stutter.division = StutterSettings::Division::ThirtySecond;
        big.points.push_back (q);
    }
    auto bigPlan = buildPlan (big, sample, sr);
    REQUIRE (bigPlan->echoes.size() == 512);
    EchoEngine e2;
    e2.prepare (sr);
    done = 0;
    while (done < (int) (3.0 * sr))
    {
        buf.clear();
        e2.process (buf, bigPlan.get(), { bpm, done * bpm / 60.0 / sr, true }, done == 0, false, 1.0f, 1.0f);
        done += block;
    }
    CHECK (e2.stats().fired == 512);
    CHECK (e2.stats().dropped == 0);
}

TEST_CASE ("Stutter: End and Manual regions take the slice from the right place")
{
    const double sr = 48000.0;
    // Fragment: first half silent, second half tone.
    SampleBuffer frag (1, 48000);
    frag.clear();
    for (int i = 24000; i < 48000; ++i)
        frag.setSample (0, i, 0.5f);
    auto sample = std::make_shared<const SampleBuffer> (frag);

    auto run = [&] (StutterSettings st)
    {
        Scene s;
        Point p;
        p.id = "p"; p.number = 1; p.timing.beat = 0.0;
        p.stutter = st;
        p.stutter.enabled = true;
        p.stutter.repeats = 2;
        p.stutter.division = StutterSettings::Division::Eighth;  // 0.25 s at 120 BPM
        p.stutter.sliceLengthPercent = 40.0;                       // 0.1 s slices
        s.points.push_back (p);
        auto plan = buildPlan (s, sample, sr);
        EchoEngine e;
        e.prepare (sr);
        std::vector<float> out;
        juce::AudioBuffer<float> buf (1, 480);
        for (int b = 0; b < 100; ++b)
        {
            buf.clear();
            e.process (buf, plan.get(), { 120.0, b * 480 * 2.0 / sr, true }, b == 0, false, 1.0f, 1.0f);
            for (int i = 0; i < 480; ++i)
                out.push_back (buf.getSample (0, i));
        }
        return out;
    };

    StutterSettings st;
    st.region = StutterSettings::Region::Start;   // first 0.1 s of the fragment: silent
    CHECK (rms (run (st).data(), 24000) < 1e-5f);
    st.region = StutterSettings::Region::End;     // last 0.1 s: tone
    CHECK (rms (run (st).data() + 100, 4000) > 0.3f);
    st.region = StutterSettings::Region::Manual;  // 0.6..0.9 of the fragment: tone
    st.manualStart = 0.6;
    st.manualEnd = 0.9;
    CHECK (rms (run (st).data() + 100, 4000) > 0.3f);
}
