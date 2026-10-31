#include "plugin/PluginProcessor.h"

#include "model/DemoScene.h"
#include "model/Serialization.h"

namespace ses
{
namespace
{
const juce::Identifier stateRootTag { "SpatialEchoStudioState" };
const juce::Identifier stateVersionAttr { "stateVersion" };
const juce::Identifier documentTag { "SceneDocument" };
}

SpatialEchoProcessor::SpatialEchoProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "PARAMS", params::createLayout()),
      document (makeDefaultDocument())
{
    outputDb = parameters.getRawParameterValue (params::output);
}

bool SpatialEchoProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    const bool supported = out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
    return supported && in == out;
}

void SpatialEchoProcessor::prepareToPlay (double sampleRate, int)
{
    outputGain.reset (sampleRate, 0.02);  // 20 ms ramp to avoid zipper noise
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outputDb->load()));
}

void SpatialEchoProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (auto ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    outputGain.setTargetValue (juce::Decibels::decibelsToGain (outputDb->load()));

    if (! outputGain.isSmoothing())
    {
        buffer.applyGain (outputGain.getTargetValue());
        return;
    }

    const auto numChannels = buffer.getNumChannels();
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const auto g = outputGain.getNextValue();
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.getWritePointer (ch)[i] *= g;
    }
}

juce::AudioProcessorEditor* SpatialEchoProcessor::createEditor()
{
    // Placeholder until the Easy Mode editor exists.
    return new juce::GenericAudioProcessorEditor (*this);
}

Document SpatialEchoProcessor::getDocument() const
{
    const juce::ScopedLock sl (documentLock);
    return document;
}

void SpatialEchoProcessor::setDocument (Document doc)
{
    const juce::ScopedLock sl (documentLock);
    document = std::move (doc);
}

juce::StringArray SpatialEchoProcessor::getLastLoadWarnings() const
{
    const juce::ScopedLock sl (documentLock);
    return lastLoadWarnings;
}

void SpatialEchoProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement root (stateRootTag);
    root.setAttribute (stateVersionAttr, kStateVersion);

    if (auto paramsXml = parameters.copyState().createXml())
        root.addChildElement (paramsXml.release());

    auto* docXml = root.createNewChildElement (documentTag);
    docXml->addTextElement (toJson (getDocument()));

    copyXmlToBinary (root, destData);
}

void SpatialEchoProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (stateRootTag))
        return;  // unknown data: keep current state

    if (xml->getIntAttribute (stateVersionAttr, 0) > kStateVersion)
        return;  // written by a newer plugin version: do not guess

    if (auto* paramsXml = xml->getChildByName (parameters.state.getType()))
        parameters.replaceState (juce::ValueTree::fromXml (*paramsXml));

    if (auto* docXml = xml->getChildByName (documentTag))
    {
        auto loaded = documentFromJson (docXml->getAllSubText());
        const juce::ScopedLock sl (documentLock);
        if (loaded)
        {
            document = std::move (*loaded.value);
            lastLoadWarnings = loaded.warnings;
        }
        else
        {
            lastLoadWarnings = juce::StringArray { "scene document not restored: " + loaded.error };
        }
    }
}

} // namespace ses

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ses::SpatialEchoProcessor();
}
