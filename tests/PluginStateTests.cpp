#include "model/Serialization.h"
#include "plugin/PluginProcessor.h"

#include <catch2/catch_test_macros.hpp>

using namespace ses;

namespace
{
struct JuceInit
{
    juce::ScopedJuceInitialiser_GUI init;
};

void setParam (SpatialEchoProcessor& p, const char* id, float value)
{
    auto* param = p.getParameters().getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}

float getParam (SpatialEchoProcessor& p, const char* id)
{
    return p.getParameters().getRawParameterValue (id)->load();
}
}

TEST_CASE ("Plugin state restores parameters and the scene document")
{
    JuceInit juce;
    SpatialEchoProcessor a;
    setParam (a, params::mix, 42.0f);
    setParam (a, params::output, -6.0f);

    auto doc = a.getDocument();
    REQUIRE (doc.activeScene() != nullptr);
    movePoint (*doc.activeScene(), doc.activeScene()->points[0].id, { 5, 95 });
    a.setDocument (doc);

    juce::MemoryBlock state;
    a.getStateInformation (state);

    SpatialEchoProcessor b;
    b.setStateInformation (state.getData(), (int) state.getSize());

    CHECK (getParam (b, params::mix) == 42.0f);
    CHECK (getParam (b, params::output) == -6.0f);
    CHECK (b.getDocument() == doc);
    CHECK (b.getLastLoadWarnings().isEmpty());
}

TEST_CASE ("Garbage or future state does not corrupt the plugin")
{
    JuceInit juce;
    SpatialEchoProcessor p;
    const auto before = p.getDocument();

    const char garbage[] = "definitely not a plugin state";
    p.setStateInformation (garbage, (int) sizeof (garbage));
    CHECK (p.getDocument() == before);

    juce::XmlElement future ("SpatialEchoStudioState");
    future.setAttribute ("stateVersion", 999);
    future.createNewChildElement ("SceneDocument")->addTextElement ("{}");
    juce::MemoryBlock block;
    juce::AudioProcessor::copyXmlToBinary (future, block);
    p.setStateInformation (block.getData(), (int) block.getSize());
    CHECK (p.getDocument() == before);
}

TEST_CASE ("Audio passes through unchanged at 0 dB, mono and stereo")
{
    JuceInit juce;
    for (auto layout : { juce::AudioChannelSet::mono(), juce::AudioChannelSet::stereo() })
    {
        SpatialEchoProcessor p;
        juce::AudioProcessor::BusesLayout buses;
        buses.inputBuses.add (layout);
        buses.outputBuses.add (layout);
        REQUIRE (p.setBusesLayout (buses));

        p.prepareToPlay (48000.0, 256);
        juce::AudioBuffer<float> buffer (layout.size(), 256);
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                buffer.setSample (ch, i, std::sin ((float) i * 0.05f + (float) ch));

        juce::AudioBuffer<float> original (buffer);
        juce::MidiBuffer midi;
        p.processBlock (buffer, midi);

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                REQUIRE (buffer.getSample (ch, i) == original.getSample (ch, i));
        p.releaseResources();
    }
}

TEST_CASE ("Output gain changes are smoothed without jumps")
{
    JuceInit juce;
    SpatialEchoProcessor p;
    p.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    setParam (p, params::output, -24.0f);  // sudden 24 dB drop

    float maxStep = 0.0f, previous = 1.0f;
    for (int block = 0; block < 4; ++block)
    {
        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::fill (buffer.getWritePointer (ch), 1.0f, 512);  // DC: output == gain
        p.processBlock (buffer, midi);
        for (int i = 0; i < 512; ++i)
        {
            const auto v = buffer.getSample (0, i);
            maxStep = std::max (maxStep, std::abs (v - previous));
            previous = v;
        }
    }

    CHECK (previous == juce::Decibels::decibelsToGain (-24.0f));  // ramp finished within 2048 samples
    CHECK (maxStep < 0.01f);  // no sample-to-sample jump above 1 % of full scale
}

// Priority 9 (2026-11-05): a project saved with every per-repetition control
// away from its default (Stutter, Volume Shaper with cut blocks and points,
// every effect, region, free time, live mode) comes back identical, twice, and
// re-saves byte-identically, so nothing drifts each time FL Studio saves.
TEST_CASE ("Plugin state: every repetition control, live mode, UI mode and parameters survive save/restore exactly and stably")
{
    JuceInit juce;
    SpatialEchoProcessor a;
    setParam (a, params::mix, 63.0f);
    setParam (a, params::output, -3.5f);
    setParam (a, params::protect, 0.0f);
    a.setUiMode (UiMode::Pro);
    a.setUiScalePercent (125);
    a.editScene ([] (Scene& s)
    {
        s.points.clear();
        s.timeMode = TimeMode::Free;
        s.rhythm = RhythmDivision::Eighth;
        s.mixPercent = 55.0;
        addPoint (s, { 12.5, 80.0 }, Timing { 1.5, 0.75 });
        addPoint (s, { 90.0, 10.0 }, Timing { 3.0, 2.25 });
        auto& p = s.points[0];
        p.solo = true;
        p.gainDb = -7.5;
        p.region = { 0.1, 0.8 };
        p.creativePreset = "user.abc";
        p.voice = { "creative.robot", "Robot", true };
        p.sound = { 5.5, 0.0, 100.0, -3.0 };
        p.stutter.enabled = true;
        p.stutter.region = StutterSettings::Region::Manual;
        p.stutter.manualStart = 0.2; p.stutter.manualEnd = 0.6;
        p.stutter.repeats = 12;
        p.stutter.division = StutterSettings::Division::ThirtySecond;
        p.stutter.sliceLengthPercent = 70.0;
        p.stutter.offsetBeats = 0.5;
        p.stutter.gainDb = -2.0;
        p.stutter.accelPercent = -40.0;
        auto& sh = p.shaper;
        sh.enabled = true;
        sh.shape = ShaperSettings::Shape::Triangle;
        sh.rateMode = ShaperSettings::RateMode::Milliseconds;
        sh.periodMs = 37.5;
        sh.cuts = 48;
        sh.division = StutterSettings::Division::ThirtySecond;
        sh.distribution = ShaperSettings::Distribution::Decelerating;
        sh.accelPercent = 35.0;
        sh.pattern = "xx.x";
        sh.depthPercent = 85.0;
        sh.openPercent = 30.0;
        sh.attackMs = 2.5;
        sh.releaseMs = 12.0;
        sh.wholeVoice = false;
        sh.regionStart = 0.25; sh.regionEnd = 0.75;
        sh.lengthMs = 640.0;
        sh.makeupDb = 2.0;
        sh.autoLevel = true;
        sh.custom = true;
        sh.manualKind = ShaperSettings::ManualKind::Cuts;
        sh.curve = { { 0.0, 1.0 }, { 0.3, 0.1 }, { 1.0, 0.9 } };
        sh.blocks = { { 0.0, 50.0, 1.0 }, { 125.0, 140.5, 0.4 }, { 300.0, 620.0, 0.75 } };
        auto& fx = p.effects;
        fx.fadeInMs = 40.0; fx.fadeOutMs = 120.0;
        fx.reverse = true;
        fx.lowpassHz = 6000.0; fx.highpassHz = 250.0;
        fx.character = { true, CharacterSettings::Type::Bitcrusher, 66.0, 80.0 };
        fx.modulation = { true, ModulationSettings::Type::Flanger, 3.5, 70.0, 40.0 };
        fx.delay = { true, true, 333.0, StutterSettings::Division::Sixteenth, 60.0, true, 4000.0, 45.0 };
        fx.reverb = { true, ReverbSettings::Type::Plate, 70.0, 80.0, 25.0, 35.0, -4.0 };
        fx.chainOrder = "rmdc";
        s.points[1].enabled = false;
        s.live.enabled = true;
        s.live.echoes = true;
        s.live.wetPercent = 65.0;
        s.live.cycleBeats = 2.0;
        s.live.fx.effects.delay.enabled = true;
        s.live.fx.shaper.enabled = true;
        s.live.fx.shaper.cuts = 16;
    });
    const auto doc = a.getDocument();
    juce::MemoryBlock state;
    a.getStateInformation (state);

    SpatialEchoProcessor b;
    b.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (b.getLastLoadWarnings().isEmpty());
    CHECK (b.getDocument() == doc);
    CHECK (getParam (b, params::mix) == 63.0f);
    CHECK (getParam (b, params::output) == -3.5f);
    CHECK (getParam (b, params::protect) < 0.5f);
    CHECK (b.getUiMode() == UiMode::Pro);
    CHECK (b.getUiScalePercent() == 125);

    // Saved again by the restored instance: same document, same bytes.
    juce::MemoryBlock again;
    b.getStateInformation (again);
    SpatialEchoProcessor c;
    c.setStateInformation (again.getData(), (int) again.getSize());
    CHECK (c.getDocument() == doc);
    CHECK (toJson (c.getDocument()) == toJson (doc));
}
