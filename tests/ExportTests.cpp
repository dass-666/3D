// «Exportar audio»: offline render of the repetitions and WAV writing.

#include "audio/OfflineRender.h"
#include "plugin/PluginProcessor.h"
#include "audio/SafetyLimiter.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>

using namespace ses;

namespace
{
std::shared_ptr<const DecodedAudio> tone (double sr, double seconds, int channels = 1)
{
    auto a = std::make_shared<DecodedAudio>();
    a->sampleRate = sr;
    const int n = (int) std::lround (seconds * sr);
    a->buffer.setSize (channels, n);
    for (int ch = 0; ch < channels; ++ch)
        for (int i = 0; i < n; ++i)
            a->buffer.setSample (ch, i, (float) (0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * (220.0 + 110.0 * ch) * i / sr)));
    return a;
}

juce::MemoryBlock wavBytes (const DecodedAudio& a)
{
    return encodeWav (a.buffer, a.sampleRate);
}

Scene onePointScene (double beat = 0.0)
{
    Scene s;
    s.points.clear();
    addPoint (s, { 50.0, 50.0 }, Timing { beat, 0.0 });
    return s;
}

float maxDiff (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int n)
{
    float d = 0.0f;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < n; ++i)
            d = juce::jmax (d, std::abs (a.getSample (ch, i) - b.getSample (ch, i)));
    return d;
}

float rms (const juce::AudioBuffer<float>& a, int from, int n)
{
    double acc = 0.0;
    for (int ch = 0; ch < a.getNumChannels(); ++ch)
        for (int i = from; i < from + n; ++i)
            acc += a.getSample (ch, i) * a.getSample (ch, i);
    return (float) std::sqrt (acc / juce::jmax (1, n * a.getNumChannels()));
}
} // namespace

TEST_CASE ("Export: exact duration, stereo, starts at the trigger")
{
    const auto src = tone (48000.0, 0.5);
    OfflineRenderSettings rs;
    rs.sampleRate = 48000.0;
    rs.mixPercent = 100.0f;

    auto r = renderOffline (onePointScene (0.0), src.get(), rs);
    REQUIRE (r.error.isEmpty());
    REQUIRE (r.audio.getNumChannels() == 2);
    const int n = src->buffer.getNumSamples();
    const int fade = (int) std::ceil (EchoEngine::kFadeSeconds * 48000.0) + 1;
    CHECK (r.audio.getNumSamples() <= n);
    CHECK (r.audio.getNumSamples() >= n - fade);
    CHECK (std::abs (r.audio.getSample (0, 1000)) > 0.0f);  // sound from the start
    CHECK (r.pointsRendered == 1);
    CHECK (r.peak <= SafetyLimiter::kCeiling + 1.0e-4f);

    // A repetition two beats later at 120 BPM starts 1 s later: the file keeps that silence.
    auto later = renderOffline (onePointScene (2.0), src.get(), rs);
    REQUIRE (later.error.isEmpty());
    CHECK (later.audio.getNumSamples() >= 48000 + n - fade);
    CHECK (later.audio.getNumSamples() <= 48000 + n);
    CHECK (later.audio.getMagnitude (0, 47000) == 0.0f);

    // At 96 kHz the duration in seconds is the same.
    rs.sampleRate = 96000.0;
    auto hi = renderOffline (onePointScene (2.0), src.get(), rs);
    REQUIRE (hi.error.isEmpty());
    CHECK_THAT (hi.audio.getNumSamples() / 96000.0, Catch::Matchers::WithinAbs (later.audio.getNumSamples() / 48000.0, 0.003));

    // Pan: a point on the left is louder on the left channel.
    Scene left = onePointScene();
    left.points[0].position.x = 10.0;
    rs.sampleRate = 48000.0;
    auto l = renderOffline (left, src.get(), rs);
    CHECK (l.audio.getRMSLevel (0, 0, l.audio.getNumSamples()) > 2.0f * l.audio.getRMSLevel (1, 0, l.audio.getNumSamples()));
}

TEST_CASE ("Export: delay and reverb tails are included, never cut")
{
    const auto src = tone (48000.0, 0.3);
    OfflineRenderSettings rs;
    rs.sampleRate = 48000.0;
    const int n = src->buffer.getNumSamples();

    Scene rev = onePointScene();
    rev.points[0].effects.reverb.enabled = true;
    rev.points[0].effects.reverb.decayPercent = 60.0;
    rev.points[0].effects.reverb.mixPercent = 50.0;
    auto r = renderOffline (rev, src.get(), rs);
    REQUIRE (r.error.isEmpty());
    CHECK (r.audio.getNumSamples() > n + 48000 / 2);
    const int len = r.audio.getNumSamples();
    CHECK (rms (r.audio, len - 4800, 4800) < rms (r.audio, n, 4800));  // decaying, not cut at full level
    CHECK (std::abs (r.audio.getSample (0, len - 1)) < 1.0e-3f);
    CHECK_FALSE (r.truncated);

    Scene dl = onePointScene();
    dl.points[0].effects.delay.enabled = true;
    dl.points[0].effects.delay.sync = false;
    dl.points[0].effects.delay.timeMs = 250.0;
    dl.points[0].effects.delay.feedbackPercent = 50.0;
    dl.points[0].effects.delay.mixPercent = 50.0;
    auto d = renderOffline (dl, src.get(), rs);
    REQUIRE (d.error.isEmpty());
    CHECK (d.audio.getNumSamples() > n + 3 * 12000);  // several echoes of 250 ms
}

TEST_CASE ("Export: identical to the real-time output, and reproducible")
{
    juce::ScopedJuceInitialiser_GUI init;
    const double sr = 48000.0;
    const auto src = tone (sr, 0.6, 2);
    SpatialEchoProcessor p;
    p.prepareToPlay (sr, 512);
    REQUIRE (p.loadSourceBytes (wavBytes (*src), "C:/t.wav").wasOk());
    p.editScene ([] (Scene& s)
    {
        s.points.clear();
        addPoint (s, { 20.0, 30.0 }, Timing { 0.0, 0.0 });
        addPoint (s, { 80.0, 70.0 }, Timing { 1.0, 0.0 });
        addPoint (s, { 50.0, 50.0 }, Timing { 2.5, 0.0 });
        auto& a = s.points[0];
        a.shaper.enabled = true;
        a.shaper.cuts = 32;
        a.shaper.depthPercent = 80.0;
        auto& b = s.points[1];
        b.stutter.enabled = true;
        b.stutter.repeats = 6;
        b.stutter.accelPercent = 50.0;
        b.effects.delay.enabled = true;
        b.effects.delay.feedbackPercent = 40.0;
        b.effects.character.enabled = true;
        auto& c = s.points[2];
        c.effects.reverb.enabled = true;
        c.effects.modulation.enabled = true;
        c.effects.lowpassHz = 3000.0;
        c.gainDb = -4.0;
    });
    const auto scene = p.getSceneForUi();

    OfflineRenderSettings rs;
    rs.sampleRate = sr;
    rs.mixPercent = p.getParameters().getRawParameterValue ("mix")->load();
    rs.outputDb = p.getParameters().getRawParameterValue ("output")->load();
    rs.protect = p.getParameters().getRawParameterValue ("protect")->load() > 0.5f;
    auto off = renderOffline (scene, p.getSourceAudio().get(), rs);
    REQUIRE (off.error.isEmpty());
    REQUIRE (off.pointsRendered == 3);
    const int total = off.audio.getNumSamples();

    // Real time: «Probar» (all points now), host stopped, 512-sample blocks of silence in.
    juce::AudioBuffer<float> live (2, total + 512);
    juce::MidiBuffer midi;
    p.requestPreview();
    for (int pos = 0; pos < total; pos += 512)
    {
        juce::AudioBuffer<float> blk (2, 512);
        blk.clear();
        p.processBlock (blk, midi);
        for (int ch = 0; ch < 2; ++ch)
            live.copyFrom (ch, pos, blk, ch, 0, 512);
    }
    CHECK (maxDiff (off.audio, live, total) < 1.0e-6f);

    auto again = renderOffline (scene, p.getSourceAudio().get(), rs);
    REQUIRE (again.audio.getNumSamples() == total);
    CHECK (maxDiff (off.audio, again.audio, total) == 0.0f);

    // Only the selected repetition: shorter, starts at once (like «Escuchar este punto»).
    rs.onlyPointId = scene.points[2].id;
    auto one = renderOffline (scene, p.getSourceAudio().get(), rs);
    REQUIRE (one.error.isEmpty());
    CHECK (one.pointsRendered == 1);
    CHECK (one.audio.getMagnitude (0, 0, 2400) > 0.0f);
}

TEST_CASE ("Export: pitch keeps the duration; cancel stops; silence and missing audio are errors")
{
    const auto src = tone (48000.0, 0.5);
    OfflineRenderSettings rs;
    rs.sampleRate = 48000.0;
    Scene s = onePointScene();
    auto plain = renderOffline (s, src.get(), rs);
    s.points[0].sound.pitchSemitones = 5.0;
    auto up = renderOffline (s, src.get(), rs);
    REQUIRE (up.error.isEmpty());
    CHECK (std::abs (up.audio.getNumSamples() - plain.audio.getNumSamples()) < 200);
    CHECK (maxDiff (up.audio, plain.audio, juce::jmin (up.audio.getNumSamples(), plain.audio.getNumSamples())) > 0.05f);

    int calls = 0;
    auto cancelled = renderOffline (s, src.get(), rs, [&] (double) { return ++calls < 1; });
    CHECK (cancelled.cancelled);

    rs.mixPercent = 0.0f;
    CHECK (renderOffline (onePointScene(), src.get(), rs).error.isNotEmpty());
    CHECK (renderOffline (onePointScene(), nullptr, rs).error.isNotEmpty());
    Scene muted = onePointScene();
    muted.points[0].enabled = false;
    rs.mixPercent = 100.0f;
    CHECK (renderOffline (muted, src.get(), rs).error.isNotEmpty());
}

TEST_CASE ("Export: WAV 16/24-bit PCM and 32-bit float read back with the right format")
{
    const auto src = tone (44100.0, 0.25, 2);
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ses-export-test");
    dir.deleteRecursively();
    for (auto [fmt, bits, tol] : { std::tuple { WavFormat::Pcm16, 16, 1.0e-4f }, std::tuple { WavFormat::Pcm24, 24, 1.0e-6f },
                                   std::tuple { WavFormat::Float32, 32, 0.0f } })
    {
        const auto f = dir.getChildFile ("t" + juce::String (bits) + ".wav");
        REQUIRE (writeWavFile (f, src->buffer, 44100.0, fmt).wasOk());
        std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (f));
        REQUIRE (rd != nullptr);
        CHECK (rd->bitsPerSample == (unsigned) bits);
        CHECK (rd->usesFloatingPointData == (fmt == WavFormat::Float32));
        CHECK (rd->sampleRate == 44100.0);
        CHECK (rd->numChannels == 2);
        CHECK (rd->lengthInSamples == src->buffer.getNumSamples());
        juce::AudioBuffer<float> back (2, (int) rd->lengthInSamples);
        rd->read (&back, 0, back.getNumSamples(), 0, true, true);
        CHECK (maxDiff (back, src->buffer, back.getNumSamples()) <= tol);
    }
    // Same input, same bytes (dither has a fixed seed).
    const auto a = dir.getChildFile ("a.wav"), b = dir.getChildFile ("b.wav");
    REQUIRE (writeWavFile (a, src->buffer, 44100.0, WavFormat::Pcm16).wasOk());
    REQUIRE (writeWavFile (b, src->buffer, 44100.0, WavFormat::Pcm16).wasOk());
    juce::MemoryBlock ma, mb;
    a.loadFileAsData (ma);
    b.loadFileAsData (mb);
    CHECK (ma == mb);
    dir.deleteRecursively();
}

TEST_CASE ("Export job: runs off the message thread, reports progress and the file")
{
    juce::ScopedJuceInitialiser_GUI init;
    SpatialEchoProcessor p;
    p.prepareToPlay (44100.0, 256);
    REQUIRE (p.loadSourceBytes (wavBytes (*tone (44100.0, 0.4)), "C:/t.wav").wasOk());
    p.editScene ([] (Scene& s) { s.points.clear(); addPoint (s, { 30.0, 50.0 }, Timing { 0.0, 0.0 }); addPoint (s, { 70.0, 50.0 }, Timing { 1.0, 0.0 }); });

    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ses-export-job");
    dir.deleteRecursively();
    SpatialEchoProcessor::ExportRequest rq;
    rq.file = dir.getChildFile ("mix.wav");
    rq.format = WavFormat::Pcm24;
    REQUIRE (p.startExport (rq).wasOk());
    for (int i = 0; i < 1000 && p.getExportStatus().state == SpatialEchoProcessor::ExportStatus::State::Running; ++i)
        juce::Thread::sleep (10);
    const auto st = p.getExportStatus();
    REQUIRE (st.state == SpatialEchoProcessor::ExportStatus::State::Done);
    CHECK (st.file.existsAsFile());
    CHECK (st.sampleRate == 44100.0);
    CHECK (st.points == 2);
    CHECK (st.seconds > 0.5 + 0.4 - 0.01);  // second point at beat 1 (0.5 s at 120 BPM) + 0.4 s
    CHECK_FALSE (st.clipped);

    // The suggested name lives in Documents/Spatial Echo Studio/Exportaciones and never overwrites.
    const auto sug = p.suggestExportFile ({});
    CHECK (sug.getParentDirectory() == SpatialEchoProcessor::exportFolder());
    CHECK (sug.hasFileExtension ("wav"));
    CHECK_FALSE (sug.exists());
    dir.deleteRecursively();
}
