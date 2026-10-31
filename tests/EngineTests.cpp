#include "audio/EchoEngine.h"
#include "audio/PlanExchange.h"
#include "audio/SourceAudio.h"
#include "model/DemoScene.h"
#include "plugin/PluginProcessor.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

using namespace ses;

namespace
{
juce::MemoryBlock makeWav (const juce::AudioBuffer<float>& audio, double sampleRate)
{
    juce::MemoryBlock block;
    {
        juce::WavAudioFormat wav;
        auto* stream = new juce::MemoryOutputStream (block, false);
        std::unique_ptr<juce::AudioFormatWriter> writer (
            wav.createWriterFor (stream, sampleRate, (unsigned) audio.getNumChannels(), 24, {}, 0));
        REQUIRE (writer != nullptr);
        writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
    }
    return block;
}

juce::AudioBuffer<float> constant (int channels, int length, float value)
{
    juce::AudioBuffer<float> b (channels, length);
    for (int ch = 0; ch < channels; ++ch)
        juce::FloatVectorOperations::fill (b.getWritePointer (ch), value, length);
    return b;
}

// One echo of a constant-0.5 fragment, `beat` quarter notes after the trigger.
PlaybackPlan makePlan (double sr, int length, double beat, double grid = 1.0, float gain = 1.0f, int channels = 1)
{
    PlaybackPlan p;
    p.sampleRate = sr;
    p.sample = constant (channels, length, 0.5f);
    p.gridBeats = grid;
    p.echoes.push_back ({ beat, gain });
    return p;
}

int firstNonZero (const std::vector<float>& x)
{
    for (size_t i = 0; i < x.size(); ++i)
        if (x[i] != 0.0f)
            return (int) i;
    return -1;
}

// Runs the engine on silence with the host playing from ppq0, trigger on the first block.
std::vector<float> runHost (EchoEngine& e, const PlaybackPlan& plan, double sr, double bpm, double ppq0,
                            int blockSize, int totalSamples, bool trigger = true)
{
    std::vector<float> out;
    juce::AudioBuffer<float> buf (1, blockSize);
    int done = 0;
    while (done < totalSamples)
    {
        buf.clear();
        TransportInfo t { bpm, ppq0 + done * bpm / 60.0 / sr, true };
        e.process (buf, &plan, t, trigger && done == 0, false, 1.0f, 1.0f);
        for (int i = 0; i < blockSize; ++i)
            out.push_back (buf.getSample (0, i));
        done += blockSize;
    }
    return out;
}
} // namespace

TEST_CASE ("Echo starts on the exact sample for any BPM, sample rate and block size")
{
    const auto sr = GENERATE (44100.0, 48000.0, 96000.0);
    const auto bpm = GENERATE (60.0, 90.0, 120.0, 140.0, 174.5);
    const auto block = GENERATE (32, 333, 512, 1024, 4096);

    EchoEngine e;
    e.prepare (sr);
    const auto plan = makePlan (sr, 2000, 1.0);
    const double ppq0 = 8.0;  // trigger exactly on a beat
    const int total = (int) (sr * 60.0 / bpm * 1.5) + block;

    const auto out = runHost (e, plan, sr, bpm, ppq0, block, total);
    const double expected = 1.0 * 60.0 / bpm * sr;  // one beat later
    const int onset = firstNonZero (out);

    INFO ("sr=" << sr << " bpm=" << bpm << " block=" << block);
    REQUIRE (onset >= 0);
    CHECK (std::abs (onset - expected) <= 1.0);
}

TEST_CASE ("Trigger between grid steps aligns to the next step")
{
    EchoEngine e;
    const double sr = 48000.0, bpm = 120.0;
    e.prepare (sr);
    const auto plan = makePlan (sr, 1000, 0.0, 0.5);  // grid 1/8, echo at the trigger itself
    const auto out = runHost (e, plan, sr, bpm, 4.3, 256, 48000);

    const double expected = (4.5 - 4.3) * 60.0 / bpm * sr;  // next 1/8 step = beat 4.5
    CHECK (std::abs (firstNonZero (out) - expected) <= 1.0);
}

TEST_CASE ("Trigger exactly on the grid is not pushed to the next step")
{
    EchoEngine e;
    e.prepare (44100.0);
    const auto plan = makePlan (44100.0, 1000, 0.0, 1.0);
    const auto out = runHost (e, plan, 44100.0, 100.0, 12.0, 128, 4096);
    CHECK (firstNonZero (out) == 0);
}

TEST_CASE ("Fades: the echo never jumps by more than one fade step")
{
    const double sr = 48000.0;
    EchoEngine e;
    e.prepare (sr);
    const auto plan = makePlan (sr, 4800, 0.0);
    const auto out = runHost (e, plan, sr, 120.0, 0.0, 64, 9600);

    float maxStep = 0.0f, prev = 0.0f;
    for (auto v : out)
    {
        maxStep = std::max (maxStep, std::abs (v - prev));
        prev = v;
    }
    const float fadeStep = 0.5f / (float) (std::round (EchoEngine::kFadeSeconds * sr) + 1);
    CHECK (maxStep <= fadeStep * 1.001f);
    CHECK (out.back() == 0.0f);  // finished
    CHECK (e.numActiveVoices() == 0);
}

TEST_CASE ("Dry signal is preserved exactly; the echo is only added on top")
{
    const double sr = 44100.0;
    const auto plan = makePlan (sr, 3000, 0.5);
    const int block = 512, blocks = 40;  // echo starts at 11025 samples

    auto render = [&] (bool withInput)
    {
        EchoEngine e;
        e.prepare (sr);
        std::vector<float> out;
        juce::AudioBuffer<float> buf (2, block);
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < block; ++i)
                for (int ch = 0; ch < 2; ++ch)
                    buf.setSample (ch, i, withInput ? std::sin (0.01f * (float) (b * block + i) + (float) ch) : 0.0f);
            TransportInfo t { 120.0, b * block * 2.0 / sr, true };
            e.process (buf, &plan, t, b == 0, false, 0.7f, 0.7f);
            for (int i = 0; i < block; ++i)
                out.push_back (buf.getSample (1, i));
        }
        return out;
    };

    const auto wetOnly = render (false);
    const auto total = render (true);
    REQUIRE (firstNonZero (wetOnly) > 0);
    for (size_t k = 0; k < total.size(); ++k)
    {
        const float dry = std::sin (0.01f * (float) k + 1.0f);
        REQUIRE (total[k] - wetOnly[k] == Catch::Approx (dry).margin (1.0e-6));
    }
}

TEST_CASE ("Without a trigger, or with Mix at 0, the output equals the input")
{
    EchoEngine e;
    e.prepare (48000.0);
    const auto plan = makePlan (48000.0, 1000, 0.0);
    juce::AudioBuffer<float> buf (2, 256);
    for (int i = 0; i < 256; ++i)
        buf.setSample (0, i, 0.25f), buf.setSample (1, i, -0.25f);
    juce::AudioBuffer<float> original (buf);

    e.process (buf, &plan, { 120.0, 0.0, true }, false, false, 1.0f, 1.0f);
    for (int i = 0; i < 256; ++i)
        REQUIRE (buf.getSample (0, i) == original.getSample (0, i));

    e.process (buf, &plan, { 120.0, 0.5, true }, true, false, 0.0f, 0.0f);
    for (int i = 0; i < 256; ++i)
        REQUIRE (buf.getSample (1, i) == original.getSample (1, i));
}

TEST_CASE ("Point volume scales the echo")
{
    EchoEngine e;
    e.prepare (48000.0);
    const auto plan = makePlan (48000.0, 2000, 0.0, 1.0, 0.25f);
    const auto out = runHost (e, plan, 48000.0, 120.0, 0.0, 1024, 2048);
    CHECK (*std::max_element (out.begin(), out.end()) == Catch::Approx (0.5f * 0.25f));
}

TEST_CASE ("A transport jump cancels a pending echo")
{
    EchoEngine e;
    const double sr = 48000.0;
    e.prepare (sr);
    const auto plan = makePlan (sr, 1000, 2.0);
    juce::AudioBuffer<float> buf (1, 512);

    e.process (buf, &plan, { 120.0, 0.0, true }, true, false, 1.0f, 1.0f);
    CHECK (e.numPendingEvents() == 1);
    buf.clear();
    e.process (buf, &plan, { 120.0, 64.0, true }, false, false, 1.0f, 1.0f);  // user relocated
    CHECK (e.numPendingEvents() == 0);
}

TEST_CASE ("Preview plays with the transport stopped, using the host tempo")
{
    EchoEngine e;
    const double sr = 44100.0, bpm = 90.0;
    e.prepare (sr);
    const auto plan = makePlan (sr, 1000, 1.0);
    std::vector<float> out;
    juce::AudioBuffer<float> buf (1, 256);
    for (int b = 0; b < 200; ++b)
    {
        buf.clear();
        e.process (buf, &plan, { bpm, std::nullopt, false }, false, b == 0, 1.0f, 1.0f);
        for (int i = 0; i < 256; ++i)
            out.push_back (buf.getSample (0, i));
    }
    CHECK (std::abs (firstNonZero (out) - 60.0 / bpm * sr) <= 1.0);
    CHECK (e.lastBpmUsed() == bpm);
}

TEST_CASE ("Stereo fragment keeps its channels; mono output gets the average")
{
    EchoEngine e;
    e.prepare (48000.0);
    auto plan = makePlan (48000.0, 2000, 0.0, 1.0, 1.0f, 2);
    plan.sample.applyGain (1, 0, 2000, 0.0f);  // right channel silent

    juce::AudioBuffer<float> stereo (2, 1024);
    stereo.clear();
    e.process (stereo, &plan, { 120.0, 0.0, true }, true, false, 1.0f, 1.0f);
    CHECK (stereo.getSample (0, 500) == Catch::Approx (0.5f));
    CHECK (stereo.getSample (1, 500) == 0.0f);

    EchoEngine m;
    m.prepare (48000.0);
    juce::AudioBuffer<float> mono (1, 1024);
    mono.clear();
    m.process (mono, &plan, { 120.0, 0.0, true }, true, false, 1.0f, 1.0f);
    CHECK (mono.getSample (0, 500) == Catch::Approx (0.25f));
}

TEST_CASE ("WAV decoding accepts mono/stereo and rejects bad input")
{
    const auto stereo = decodeWav (makeWav (constant (2, 4410, 0.5f), 44100.0));
    REQUIRE (stereo.audio != nullptr);
    CHECK (stereo.audio->buffer.getNumChannels() == 2);
    CHECK (stereo.audio->buffer.getNumSamples() == 4410);
    CHECK (stereo.audio->buffer.getSample (1, 100) == Catch::Approx (0.5f).margin (1.0e-6));

    juce::MemoryBlock junk ("not a wav file", 14);
    CHECK (decodeWav (junk).audio == nullptr);

    const auto tooLong = decodeWav (makeWav (constant (1, 8000 * 31, 0.1f), 8000.0));
    CHECK (tooLong.audio == nullptr);
    CHECK (tooLong.error.contains ("longer"));
}

TEST_CASE ("Resampling keeps the duration")
{
    DecodedAudio src;
    src.sampleRate = 44100.0;
    src.buffer = constant (1, 44100, 0.5f);
    const auto out = resampled (src, 48000.0);
    CHECK (std::abs (out.getNumSamples() - 48000) <= 1);
    CHECK (out.getSample (0, 24000) == Catch::Approx (0.5f).margin (1.0e-4));
}

TEST_CASE ("Plan uses the first enabled point and the scene trim")
{
    auto scene = makeDemoScene();
    scene.points[0].enabled = false;  // point 2 (Agudo, 85 %, beat 2) becomes the echo
    scene.source.trimStart = 100;
    scene.source.trimEnd = 600;

    DecodedAudio src;
    src.sampleRate = 48000.0;
    src.buffer = constant (1, 1000, 0.5f);

    const auto plan = buildPlan (scene, &src, 48000.0);
    REQUIRE (plan != nullptr);
    REQUIRE (plan->echoes.size() == 1);
    CHECK (plan->echoes[0].beat == 2.0);
    CHECK (plan->echoes[0].gain == Catch::Approx (0.85f));
    CHECK (plan->sample.getNumSamples() == 500);
    CHECK (buildPlan (scene, nullptr, 48000.0) == nullptr);
}

TEST_CASE ("PlanExchange frees retired plans only when the audio thread is done")
{
    PlanExchange<int> ex;
    ex.publish (std::make_unique<int> (1));
    const int* a = ex.acquire();
    ex.publish (std::make_unique<int> (2));
    CHECK (ex.numRetired() == 1);  // 1 still in use
    CHECK (*a == 1);
    ex.release();
    ex.collect();
    CHECK (ex.numRetired() == 0);
    CHECK (*ex.acquire() == 2);
    ex.release();
}

namespace
{
struct FakePlayHead final : juce::AudioPlayHead
{
    juce::Optional<PositionInfo> getPosition() const override { return info; }
    PositionInfo info;
};
}

TEST_CASE ("Processor: loaded WAV is embedded, survives save/restore and plays the same")
{
    juce::ScopedJuceInitialiser_GUI init;
    const double sr = 48000.0;
    const int block = 480;

    auto renderTriggered = [&] (SpatialEchoProcessor& p)
    {
        FakePlayHead head;
        head.info.setBpm (120.0);
        head.info.setIsPlaying (true);
        p.setPlayHead (&head);
        p.prepareToPlay (sr, block);

        auto* trig = p.getParameters().getParameter (params::trigger);
        std::vector<float> out;
        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer midi;
        for (int b = 0; b < 150; ++b)
        {
            if (b == 1)
                trig->setValueNotifyingHost (1.0f);
            head.info.setPpqPosition (b * block * 2.0 / sr);
            buf.clear();
            p.processBlock (buf, midi);
            for (int i = 0; i < block; ++i)
                out.push_back (buf.getSample (0, i));
        }
        trig->setValueNotifyingHost (0.0f);
        p.setPlayHead (nullptr);
        return out;
    };

    SpatialEchoProcessor a;
    a.prepareToPlay (sr, block);
    auto wavAudio = constant (1, 4800, 0.5f);
    REQUIRE (a.loadSourceBytes (makeWav (wavAudio, 44100.0), "C:/x/campeon.wav").wasOk());
    REQUIRE (a.getSourceStatus().embedded);

    const auto first = renderTriggered (a);
    REQUIRE (firstNonZero (first) > 0);

    juce::MemoryBlock state;
    a.getStateInformation (state);

    SpatialEchoProcessor b;
    b.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (b.getSourceStatus().loaded);
    CHECK (b.getSourceStatus().fileName == "campeon.wav");
    CHECK (b.getLastLoadWarnings().isEmpty());
    CHECK (renderTriggered (b) == first);
}

TEST_CASE ("Processor: a non-WAV file is rejected and nothing changes")
{
    juce::ScopedJuceInitialiser_GUI init;
    SpatialEchoProcessor p;
    juce::MemoryBlock junk ("RIFFjunk", 8);
    CHECK (p.loadSourceBytes (junk, "bad.wav").failed());
    CHECK_FALSE (p.getSourceStatus().loaded);
    CHECK (p.getSourceStatus().message.isNotEmpty());
}

TEST_CASE ("Processor: state restores in either host order and at another sample rate")
{
    juce::ScopedJuceInitialiser_GUI init;
    SpatialEchoProcessor a;
    a.prepareToPlay (44100.0, 512);
    REQUIRE (a.loadSourceBytes (makeWav (constant (2, 9000, 0.25f), 44100.0), "C:/voz/hey.wav").wasOk());
    a.getParameters().getParameter (params::mix)->setValueNotifyingHost (0.3f);
    juce::MemoryBlock state;
    a.getStateInformation (state);

    for (const bool prepareFirst : { true, false })
    {
        SpatialEchoProcessor b;
        if (prepareFirst)
            b.prepareToPlay (96000.0, 64);
        b.setStateInformation (state.getData(), (int) state.getSize());
        if (! prepareFirst)
            b.prepareToPlay (96000.0, 64);
        const auto st = b.getSourceStatus();
        CHECK (st.loaded);
        CHECK (st.numChannels == 2);
        CHECK (st.fileName == "hey.wav");
        CHECK (b.getParameters().getParameter (params::mix)->getValue() == Catch::Approx (0.3f).margin (0.002));

        // A second save from the restored instance is identical in content.
        juce::MemoryBlock again;
        b.getStateInformation (again);
        SpatialEchoProcessor c;
        c.setStateInformation (again.getData(), (int) again.getSize());
        CHECK (c.getSourceStatus().loaded);
        CHECK (c.getDocument() == b.getDocument());
    }
}

TEST_CASE ("Processor: the UI trigger button fires once per click, even with the Trigger parameter left on")
{
    juce::ScopedJuceInitialiser_GUI init;
    const double sr = 48000.0;
    const int block = 480;
    SpatialEchoProcessor p;
    p.prepareToPlay (sr, block);
    REQUIRE (p.loadSourceBytes (makeWav (constant (1, 2400, 0.5f), sr), "C:/a.wav").wasOk());
    p.getParameters().getParameter (params::trigger)->setValueNotifyingHost (1.0f);  // left on

    FakePlayHead head;
    head.info.setBpm (120.0);
    head.info.setIsPlaying (true);
    p.setPlayHead (&head);
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    int b = 0;
    auto runBlocks = [&] (int count)
    {
        for (int i = 0; i < count; ++i, ++b)
        {
            head.info.setPpqPosition (b * block * 2.0 / sr);
            buf.clear();
            p.processBlock (buf, midi);
        }
    };
    runBlocks (10);
    const int afterParam = p.getHostSnapshot().triggers.load();
    p.requestTrigger();
    runBlocks (1);
    p.requestTrigger();
    runBlocks (1);
    CHECK (p.getHostSnapshot().triggers.load() == afterParam + 2);
    p.setPlayHead (nullptr);
}
