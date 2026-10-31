#include "plugin/PluginProcessor.h"

#include "model/DemoScene.h"
#include "model/Serialization.h"
#include "ui/PluginEditor.h"

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
    mixPercent = parameters.getRawParameterValue (params::mix);
    triggerValue = parameters.getRawParameterValue (params::trigger);
}

SpatialEchoProcessor::~SpatialEchoProcessor() = default;

bool SpatialEchoProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    const bool supported = out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
    return supported && in == out;
}

void SpatialEchoProcessor::prepareToPlay (double sampleRate, int)
{
    // Not concurrent with processBlock, so the engine and plan can be reset here.
    hostSampleRate.store (sampleRate);
    engine.prepare (sampleRate);
    lastTrigger = triggerValue->load() > 0.5f;

    outputGain.reset (sampleRate, 0.02);  // 20 ms ramps avoid zipper noise
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outputDb->load()));
    wetLevel.reset (sampleRate, 0.02);
    wetLevel.setCurrentAndTargetValue (mixPercent->load() / 100.0f);

    rebuildPlan();
}

void SpatialEchoProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();

    for (auto ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, n);

    TransportInfo transport;
    if (auto* head = getPlayHead())
    {
        if (auto pos = head->getPosition())
        {
            if (auto bpm = pos->getBpm())
                transport.bpm = *bpm;
            if (auto ppq = pos->getPpqPosition())
                transport.ppq = *ppq;
            transport.isPlaying = pos->getIsPlaying();
        }
    }

    const bool trig = triggerValue->load() > 0.5f;
    const bool rising = trig && ! lastTrigger;
    lastTrigger = trig;
    const bool preview = previewRequested.exchange (false);

    wetLevel.setTargetValue (mixPercent->load() / 100.0f);
    const float wetStart = wetLevel.getCurrentValue();
    wetLevel.skip (n);
    const float wetEnd = wetLevel.getCurrentValue();

    const auto* plan = plans.acquire();
    engine.process (buffer, plan, transport, rising, preview, wetStart, wetEnd);
    plans.release();

    outputGain.setTargetValue (juce::Decibels::decibelsToGain (outputDb->load()));
    if (! outputGain.isSmoothing())
    {
        buffer.applyGain (outputGain.getTargetValue());
        return;
    }
    for (int i = 0; i < n; ++i)
    {
        const auto g = outputGain.getNextValue();
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.getWritePointer (ch)[i] *= g;
    }
}

juce::AudioProcessorEditor* SpatialEchoProcessor::createEditor()
{
    return new SpatialEchoEditor (*this);
}

Document SpatialEchoProcessor::getDocument() const
{
    const juce::ScopedLock sl (documentLock);
    return document;
}

void SpatialEchoProcessor::setDocument (Document doc)
{
    {
        const juce::ScopedLock sl (documentLock);
        document = std::move (doc);
    }
    rebuildPlan();
}

juce::StringArray SpatialEchoProcessor::getLastLoadWarnings() const
{
    const juce::ScopedLock sl (documentLock);
    return lastLoadWarnings;
}

void SpatialEchoProcessor::rebuildPlan()
{
    std::unique_ptr<PlaybackPlan> plan;
    {
        const juce::ScopedLock sl (documentLock);
        if (const auto* scene = document.activeScene())
            plan = buildPlan (*scene, source.get(), hostSampleRate.load());
    }
    plans.publish (std::move (plan));
}

juce::Result SpatialEchoProcessor::loadSourceFile (const juce::File& file)
{
    juce::MemoryBlock bytes;
    if (! file.loadFileAsData (bytes))
        return juce::Result::fail ("could not read " + file.getFullPathName());
    return loadSourceBytes (bytes, file.getFullPathName());
}

juce::Result SpatialEchoProcessor::loadSourceBytes (const juce::MemoryBlock& bytes, const juce::String& originalPath)
{
    auto decoded = decodeWav (bytes);
    if (decoded.audio == nullptr)
    {
        const juce::ScopedLock sl (documentLock);
        sourceMessage = decoded.error;
        return juce::Result::fail (decoded.error);
    }

    {
        const juce::ScopedLock sl (documentLock);
        if (auto* scene = document.activeScene())
        {
            SourceRef ref;
            const bool embed = (juce::int64) bytes.getSize() <= kMaxEmbeddedBytes;
            ref.storage = embed ? SourceRef::Storage::Embedded : SourceRef::Storage::Reference;
            ref.filePath = originalPath;
            ref.contentHash = "sha256:" + sha256Hex (bytes);
            ref.encoding = "wav";
            ref.sampleRate = decoded.audio->sampleRate;
            ref.numChannels = decoded.audio->buffer.getNumChannels();
            ref.numSamples = decoded.audio->buffer.getNumSamples();
            if (embed)
                ref.embeddedData = bytes;
            scene->source = std::move (ref);
        }
        source = decoded.audio;
        sourceMessage = {};
    }
    rebuildPlan();
    return juce::Result::ok();
}

void SpatialEchoProcessor::restoreSourceFromDocument()
{
    std::shared_ptr<const DecodedAudio> restored;
    juce::String message;
    {
        const juce::ScopedLock sl (documentLock);
        const auto* scene = document.activeScene();
        if (scene == nullptr || scene->source.storage == SourceRef::Storage::None)
        {
            source = nullptr;
            sourceMessage = {};
            return;
        }
        const auto& ref = scene->source;
        juce::MemoryBlock bytes;
        if (ref.storage == SourceRef::Storage::Embedded)
            bytes = ref.embeddedData;
        else if (! juce::File (ref.filePath).loadFileAsData (bytes))
            message = "source file not found: " + ref.filePath;

        if (bytes.getSize() > 0)
        {
            if (ref.contentHash.isNotEmpty() && ref.contentHash != "sha256:" + sha256Hex (bytes))
                message = "source file changed since it was loaded";
            auto decoded = decodeWav (bytes);
            restored = decoded.audio;
            if (restored == nullptr)
                message = decoded.error;
        }
        source = restored;
        sourceMessage = message;
        if (message.isNotEmpty())
            lastLoadWarnings.add (message);
    }
}

SpatialEchoProcessor::SourceStatus SpatialEchoProcessor::getSourceStatus() const
{
    const juce::ScopedLock sl (documentLock);
    SourceStatus st;
    st.message = sourceMessage;
    if (source == nullptr)
        return st;
    st.loaded = true;
    st.sampleRate = source->sampleRate;
    st.numChannels = source->buffer.getNumChannels();
    st.seconds = source->buffer.getNumSamples() / source->sampleRate;
    if (const auto* scene = document.activeScene())
    {
        st.fileName = juce::File (scene->source.filePath).getFileName();
        st.embedded = scene->source.storage == SourceRef::Storage::Embedded;
    }
    return st;
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
        {
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
        restoreSourceFromDocument();
        rebuildPlan();
    }
}

} // namespace ses

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ses::SpatialEchoProcessor();
}
