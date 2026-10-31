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
