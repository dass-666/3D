// «Procesar Mixer en vivo»: continuous processing of the plugin input.

#include "model/Serialization.h"
#include "plugin/PluginProcessor.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <chrono>
#include <cmath>

using namespace ses;

namespace
{
struct Host final : juce::AudioPlayHead
{
    double bpm = 120.0, ppq = 0.0;
    bool playing = false;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm (bpm);
        p.setIsPlaying (playing);
        if (playing)
            p.setPpqPosition (ppq);
        return p;
    }
};

// Runs `seconds` of a sine (or silence after `soundSeconds`) through the processor; returns the output.
juce::AudioBuffer<float> run (SpatialEchoProcessor& p, double sr, double seconds, double hz, double soundSeconds = 1.0e9,
                              Host* host = nullptr, const std::function<void (int)>& perBlock = {})
{
    const int total = (int) (seconds * sr), bs = 512;
    juce::AudioBuffer<float> out (2, total);
    juce::MidiBuffer midi;
    int k = 0;
    for (int pos = 0; pos < total; pos += bs, ++k)
    {
        const int n = juce::jmin (bs, total - pos);
        juce::AudioBuffer<float> b (2, n);
        for (int i = 0; i < n; ++i)
        {
            const double t = (pos + i) / sr;
            const float x = t < soundSeconds ? (float) (0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * hz * t)) : 0.0f;
            b.setSample (0, i, x);
            b.setSample (1, i, x);
        }
        if (perBlock)
            perBlock (k);
        p.processBlock (b, midi);
        if (host != nullptr)
            host->ppq += n / sr * host->bpm / 60.0;
        for (int ch = 0; ch < 2; ++ch)
            out.copyFrom (ch, pos, b, ch, 0, n);
    }
    return out;
}

float rmsOf (const juce::AudioBuffer<float>& a, double sr, double from, double to)
{
    return a.getRMSLevel (0, (int) (from * sr), (int) ((to - from) * sr));
}

void noRepetitions (SpatialEchoProcessor& p)
{
    p.editScene ([] (Scene& s) { s.points.clear(); });
}
} // namespace

TEST_CASE ("Live: off by default, the input passes bit-exact; on with nothing enabled it is transparent")
{
    juce::ScopedJuceInitialiser_GUI init;
    const double sr = 48000.0;
    SpatialEchoProcessor p;
    p.prepareToPlay (sr, 512);
    noRepetitions (p);
    CHECK_FALSE (p.getSceneForUi().live.enabled);
    auto dry = run (p, sr, 0.5, 220.0);
    CHECK_FALSE (p.isLiveProcessing());
    for (int i = 0; i < dry.getNumSamples(); i += 97)
        REQUIRE (std::abs (dry.getSample (0, i) - (float) (0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * (i / sr)))) < 1.0e-7f);

    p.editScene ([] (Scene& s) { s.live.enabled = true; });
    auto out = run (p, sr, 0.5, 220.0);
    CHECK (p.isLiveProcessing());
    // After the 20 ms fade-in it equals the input (no filter, centre, 0 dB, 100 % wet).
    float d = 0.0f;
    for (int i = 4800; i < out.getNumSamples(); ++i)
        d = juce::jmax (d, std::abs (out.getSample (0, i) - dry.getSample (0, i)));
    CHECK (d < 1.0e-4f);
    CHECK (p.getLatencySamples() == 0);
}

TEST_CASE ("Live: filters, pan, dry/wet and bypass act on the incoming audio")
{
    juce::ScopedJuceInitialiser_GUI init;
    const double sr = 48000.0;
    SpatialEchoProcessor p;
    p.prepareToPlay (sr, 512);
    noRepetitions (p);
    p.editScene ([] (Scene& s) { s.live.enabled = true; s.live.fx.effects.highpassHz = 2000.0; });
    auto hp = run (p, sr, 0.6, 100.0);
    CHECK (rmsOf (hp, sr, 0.2, 0.6) < 0.05f * 0.354f);  // 100 Hz mostly removed

    p.editScene ([] (Scene& s) { s.live.fx.effects.highpassHz = 20.0; s.live.fx.position.x = 0.0; });
    auto left = run (p, sr, 0.6, 300.0);
    CHECK (left.getRMSLevel (0, 9600, 9600) > 0.3f);
    CHECK (left.getRMSLevel (1, 9600, 9600) < 0.01f);

    // 0 % wet: the dry signal again.
    p.editScene ([] (Scene& s) { s.live.wetPercent = 0.0; });
    auto dryAgain = run (p, sr, 0.6, 300.0);
    CHECK_THAT (dryAgain.getRMSLevel (1, 9600, 9600), Catch::Matchers::WithinAbs (0.3536, 0.002));

    // Bypass: exact input even with live processing on.
    p.editScene ([] (Scene& s) { s.live.wetPercent = 100.0; s.live.fx.effects.character.enabled = true; });
    p.getParameters().getParameter ("bypass")->setValueNotifyingHost (1.0f);
    auto by = run (p, sr, 0.3, 300.0);
    for (int i = 4800; i < by.getNumSamples(); i += 53)
        REQUIRE (std::abs (by.getSample (1, i) - (float) (0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * 300.0 * (i / sr)))) < 1.0e-7f);
}

TEST_CASE ("Live: the Volume Shaper runs as a cycle locked to the host beat")
{
    juce::ScopedJuceInitialiser_GUI init;
    const double sr = 48000.0;
    SpatialEchoProcessor p;
    p.prepareToPlay (sr, 512);
    noRepetitions (p);
    p.editScene ([] (Scene& s)
    {
        s.live.enabled = true;
        s.live.cycleBeats = 4.0;  // 2 s at 120 BPM
        auto& sh = s.live.fx.shaper;
        sh.enabled = true;
        sh.shape = ShaperSettings::Shape::Square;
        sh.rateMode = ShaperSettings::RateMode::Count;
        sh.cuts = 4;              // 0.5 s cuts, open for the first half of each
        sh.openPercent = 50.0;
        sh.depthPercent = 100.0;
    });
    Host host;
    host.playing = true;
    p.setPlayHead (&host);
    auto out = run (p, sr, 2.0, 400.0, 1.0e9, &host);
    // Open 0.05-0.2 s, closed 0.3-0.45 s of every 0.5 s cut.
    CHECK (rmsOf (out, sr, 0.55, 0.7) > 0.25f);
    CHECK (rmsOf (out, sr, 0.8, 0.95) < 0.02f);

    // Starting playback at beat 1 (0.5 s into the cycle) gives the same gate at the same beats.
    p.prepareToPlay (sr, 512);
    host.ppq = 1.0;
    auto shifted = run (p, sr, 1.0, 400.0, 1.0e9, &host);
    CHECK (rmsOf (shifted, sr, 0.05, 0.2) > 0.25f);   // beat 1.1-1.4: open
    CHECK (rmsOf (shifted, sr, 0.3, 0.45) < 0.02f);   // beat 1.6-1.9: closed

    // Stopped: keeps cutting on its own clock.
    host.playing = false;
    auto stopped = run (p, sr, 2.0, 400.0, 1.0e9, &host);
    float lo = 1.0f, hi = 0.0f;
    for (double t = 0.1; t < 1.9; t += 0.1)
    {
        const float r = rmsOf (stopped, sr, t, t + 0.08);
        lo = juce::jmin (lo, r);
        hi = juce::jmax (hi, r);
    }
    CHECK (hi > 0.25f);
    CHECK (lo < 0.02f);
    p.setPlayHead (nullptr);
}

TEST_CASE ("Live: reverb and delay tails ring after the input stops; edits while playing stay clean")
{
    juce::ScopedJuceInitialiser_GUI init;
    const double sr = 48000.0;
    SpatialEchoProcessor p;
    p.prepareToPlay (sr, 512);
    noRepetitions (p);
    p.editScene ([] (Scene& s)
    {
        s.live.enabled = true;
        s.live.fx.effects.reverb.enabled = true;
        s.live.fx.effects.reverb.decayPercent = 70.0;
        s.live.fx.effects.reverb.mixPercent = 60.0;
        s.live.fx.effects.delay.enabled = true;
        s.live.fx.effects.delay.feedbackPercent = 50.0;
    });
    auto out = run (p, sr, 2.0, 300.0, 0.5);
    CHECK (rmsOf (out, sr, 0.7, 1.2) > 0.005f);  // tail after the input stopped at 0.5 s

    // Settings changed on every block while audio flows: finite, bounded output.
    int k = 0;
    auto moving = run (p, sr, 2.0, 300.0, 1.0e9, nullptr, [&] (int block)
    {
        k = block;
        p.editScene ([k] (Scene& s)
        {
            s.live.wetPercent = 50.0 + 50.0 * std::sin (k * 0.3);
            s.live.fx.position.x = 50.0 + 50.0 * std::sin (k * 0.2);
            s.live.fx.effects.lowpassHz = 2000.0 + 1500.0 * std::sin (k * 0.1);
            s.live.fx.effects.chainOrder = (k % 2) ? "rdmc" : "cmdr";
        });
    });
    float peak = 0.0f;
    bool finite = true;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < moving.getNumSamples(); ++i)
        {
            finite &= std::isfinite (moving.getSample (ch, i));
            peak = juce::jmax (peak, std::abs (moving.getSample (ch, i)));
        }
    CHECK (finite);
    CHECK (peak <= 1.0f);  // the safety limiter also protects live processing
}

TEST_CASE ("Live: saved with the scene; older scenes load with it off; repetitions still play on top")
{
    juce::ScopedJuceInitialiser_GUI init;
    Document doc;
    Scene s;
    s.id = "a";
    s.name = "x";
    s.live.enabled = true;
    s.live.wetPercent = 42.0;
    s.live.cycleBeats = 2.0;
    s.live.fx.effects.reverb.enabled = true;
    s.live.fx.shaper.enabled = true;
    s.live.fx.shaper.cuts = 16;
    doc.scenes.push_back (s);
    doc.activeSceneId = "a";
    auto back = documentFromJson (toJson (doc));
    REQUIRE (back);
    const auto& l = back.value->scenes[0].live;
    CHECK (l.enabled);
    CHECK (l.wetPercent == 42.0);
    CHECK (l.cycleBeats == 2.0);
    CHECK (l.fx.effects.reverb.enabled);
    CHECK (l.fx.shaper.cuts == 16);
    CHECK (l.fx.id == kLivePointId);

    auto json = toJson (doc);
    auto parsed = juce::JSON::parse (json);
    parsed["scenes"][0].getDynamicObject()->removeProperty ("live");
    auto old = documentFromJson (juce::JSON::toString (parsed));
    REQUIRE (old);
    CHECK_FALSE (old.value->scenes[0].live.enabled);
    CHECK (findPoint (old.value->scenes[0], kLivePointId) != nullptr);  // editable even when off
    // A repetition can never take the reserved id.
    CHECK_FALSE (duplicatePoint (old.value->scenes[0], kLivePointId).has_value());
}

TEST_CASE ("Live: full chain plus 16 repetitions keeps up with real time")
{
    juce::ScopedJuceInitialiser_GUI init;
    const double sr = 48000.0;
    SpatialEchoProcessor p;
    p.prepareToPlay (sr, 256);
    p.editScene ([] (Scene& s)
    {
        s.live.enabled = true;
        auto& fx = s.live.fx.effects;
        fx.character.enabled = fx.modulation.enabled = fx.delay.enabled = fx.reverb.enabled = true;
        fx.highpassHz = 80.0;
        fx.lowpassHz = 9000.0;
        s.live.fx.shaper.enabled = true;
        s.live.fx.shaper.cuts = 64;
    });
    const auto t0 = std::chrono::steady_clock::now();
    run (p, sr, 5.0, 220.0);
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
#if NDEBUG
    CHECK (secs < 5.0 * 0.25);
#else
    CHECK (secs < 5.0 * 2.0);
#endif
}
