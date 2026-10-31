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
const juce::Identifier diagnosticsAttr { "diagnostics" };
const juce::Identifier uiModeAttr { "uiMode" };

// File name without touching juce::File (relative paths assert there).
juce::String fileNameOf (const juce::String& path)
{
    return path.fromLastOccurrenceOf ("/", false, false).fromLastOccurrenceOf ("\\", false, false);
}

juce::String storageName (SourceRef::Storage s)
{
    return s == SourceRef::Storage::Embedded ? "embedded" : s == SourceRef::Storage::Reference ? "reference" : "none";
}
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
    bypassValue = parameters.getRawParameterValue (params::bypass);
    protectValue = parameters.getRawParameterValue (params::protect);
    diagnostics.log (juce::String ("plugin created (") + juce::PluginHostType().getHostDescription() + ")");
}

SpatialEchoProcessor::~SpatialEchoProcessor() = default;

juce::AudioProcessorParameter* SpatialEchoProcessor::getBypassParameter() const
{
    return parameters.getParameter (params::bypass);
}

bool SpatialEchoProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    const bool supported = out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
    return supported && in == out;
}

void SpatialEchoProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Not concurrent with processBlock, so the engine and plan can be reset here.
    hostSampleRate.store (sampleRate);
    diagnostics.log ("prepareToPlay sr=" + juce::String (sampleRate) + " block=" + juce::String (samplesPerBlock));
    engine.prepare (sampleRate);
    limiter.prepare (sampleRate);
    lastTrigger = triggerValue->load() > 0.5f;

    outputGain.reset (sampleRate, 0.02);  // 20 ms ramps avoid zipper noise
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outputDb->load()));
    wetLevel.reset (sampleRate, 0.02);
    wetLevel.setCurrentAndTargetValue (mixPercent->load() / 100.0f);
    bypassMix.reset (sampleRate, 0.01);
    bypassMix.setCurrentAndTargetValue (bypassValue->load() > 0.5f ? 1.0f : 0.0f);
    dryCopy.setSize (2, juce::jmax (1, samplesPerBlock) * 2);

    rebuildPlan();
}

void SpatialEchoProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int channels = juce::jmin (buffer.getNumChannels(), getTotalNumOutputChannels());

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
            if (auto samples = pos->getTimeInSamples())
                transport.timeSamples = *samples;
        }
    }

    // Bypass crossfade needs the untouched input; blocks larger than announced are
    // still processed, they just switch without the crossfade.
    bypassMix.setTargetValue (bypassValue->load() > 0.5f ? 1.0f : 0.0f);
    const bool canCrossfade = n <= dryCopy.getNumSamples() && channels <= dryCopy.getNumChannels();
    const bool needDry = bypassMix.isSmoothing() || bypassMix.getTargetValue() > 0.0f;
    if (needDry && canCrossfade)
        for (int ch = 0; ch < channels; ++ch)
            dryCopy.copyFrom (ch, 0, buffer, ch, 0, n);

    const bool trig = triggerValue->load() > 0.5f;
    const bool rising = (trig && ! lastTrigger) | triggerRequested.exchange (false);
    lastTrigger = trig;
    const bool preview = previewRequested.exchange (false);
    const bool fragment = fragmentRequested.exchange (false);

    wetLevel.setTargetValue (mixPercent->load() / 100.0f);
    const float wetStart = wetLevel.getCurrentValue();
    wetLevel.skip (n);
    const float wetEnd = wetLevel.getCurrentValue();

    const auto* plan = plans.acquire();
    engine.process (buffer, plan, transport, rising, preview, wetStart, wetEnd, fragment);
    plans.release();
    currentBeat.store (engine.currentBeat());

    outputGain.setTargetValue (juce::Decibels::decibelsToGain (outputDb->load()));
    if (! outputGain.isSmoothing())
    {
        buffer.applyGain (outputGain.getTargetValue());
    }
    else
    {
        for (int i = 0; i < n; ++i)
        {
            const auto g = outputGain.getNextValue();
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.getWritePointer (ch)[i] *= g;
        }
    }

    if (protectValue->load() > 0.5f && (engine.numActiveVoices() > 0 || limiter.isReducing()))
        limiter.process (buffer);
    else
        limiter.idle();
    hostSnapshot.limiterGainDb.store (juce::Decibels::gainToDecibels (limiter.lastMinGain(), -60.0f));

    if (needDry && canCrossfade)
    {
        if (! bypassMix.isSmoothing())
        {
            for (int ch = 0; ch < channels; ++ch)
                buffer.copyFrom (ch, 0, dryCopy, ch, 0, n);  // fully bypassed: exact input
        }
        else
        {
            for (int i = 0; i < n; ++i)
            {
                const float b = bypassMix.getNextValue();
                for (int ch = 0; ch < channels; ++ch)
                {
                    auto* d = buffer.getWritePointer (ch);
                    d[i] = d[i] * (1.0f - b) + dryCopy.getSample (ch, i) * b;
                }
            }
        }
    }
    else if (needDry)
    {
        bypassMix.skip (n);
    }

    hostSnapshot.store (transport, n, engine.stats(), engine);
}

juce::AudioProcessorEditor* SpatialEchoProcessor::createEditor()
{
    return new SpatialEchoEditor (*this);
}

// ---- document ---------------------------------------------------------------

void SpatialEchoProcessor::takeEmbeddedBytes()
{
    embeddedBytes.reset();
    if (auto* scene = document.activeScene())
        if (scene->source.storage == SourceRef::Storage::Embedded)
            std::swap (embeddedBytes, scene->source.embeddedData);
}

Document SpatialEchoProcessor::getDocument() const
{
    const juce::ScopedLock sl (documentLock);
    Document copy = document;
    if (auto* scene = copy.activeScene())
        if (scene->source.storage == SourceRef::Storage::Embedded)
            scene->source.embeddedData = embeddedBytes;
    return copy;
}

void SpatialEchoProcessor::setDocument (Document doc)
{
    {
        const juce::ScopedLock sl (documentLock);
        document = std::move (doc);
        if (document.activeScene() == nullptr)
            document = makeDefaultDocument();
        takeEmbeddedBytes();
        lastLoadWarnings.clear();
    }
    restoreSourceFromDocument();
    ++documentVersion;
    rebuildPlan();
}

juce::StringArray SpatialEchoProcessor::getLastLoadWarnings() const
{
    const juce::ScopedLock sl (documentLock);
    return lastLoadWarnings;
}

Scene SpatialEchoProcessor::getSceneForUi() const
{
    const juce::ScopedLock sl (documentLock);
    if (auto* scene = document.activeScene())
        return *scene;  // embedded bytes live outside the document
    return {};
}

void SpatialEchoProcessor::editScene (const std::function<void (Scene&)>& fn)
{
    {
        const juce::ScopedLock sl (documentLock);
        if (auto* scene = document.activeScene())
            fn (*scene);
    }
    ++documentVersion;
    rebuildPlan();
}

void SpatialEchoProcessor::rebuildPlan()
{
    Scene scene;
    std::shared_ptr<const DecodedAudio> src;
    {
        const juce::ScopedLock sl (documentLock);
        if (auto* s = document.activeScene())
            scene = *s;
        src = source;
    }
    const double rate = hostSampleRate.load();

    const juce::ScopedLock pl (planLock);
    const bool needRender = src.get() != renderedFrom || scene.source.trimStart != renderedTrimStart
                            || scene.source.trimEnd != renderedTrimEnd || ! juce::exactlyEqual (rate, renderedRate);
    const auto now = juce::Time::getMillisecondCounter();
    if (needRender)
    {
        previousSample = renderedSample;  // voices still reading it fade out instead of stopping dead
        renderedSample = renderSample (scene.source, src.get(), rate);
        renderedFrom = src.get();
        renderedTrimStart = scene.source.trimStart;
        renderedTrimEnd = scene.source.trimEnd;
        renderedRate = rate;
        sampleChangedAtMs = now;
    }
    else if (previousSample != nullptr && now - sampleChangedAtMs > 2000)
    {
        previousSample.reset();  // fade-outs take 10 ms; free the memory
    }

    auto plan = buildPlan (scene, renderedSample, rate);
    plan->previousSample = previousSample;
    plans.publish (std::move (plan));
}

juce::Result SpatialEchoProcessor::saveSceneFile (const juce::File& file) const
{
    if (! file.replaceWithText (toJson (getDocument())))
        return juce::Result::fail ("could not write " + file.getFullPathName());
    return juce::Result::ok();
}

juce::Result SpatialEchoProcessor::loadSceneFile (const juce::File& file)
{
    auto loaded = documentFromJson (file.loadFileAsString());
    if (! loaded)
        return juce::Result::fail (loaded.error);
    const auto warnings = loaded.warnings;
    setDocument (std::move (*loaded.value));
    {
        const juce::ScopedLock sl (documentLock);
        lastLoadWarnings.addArray (warnings);
    }
    diagnostics.log ("scene file loaded " + file.getFileName());
    return juce::Result::ok();
}

// ---- source audio ---------------------------------------------------------------

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

    const bool embed = (juce::int64) bytes.getSize() <= kMaxEmbeddedBytes;
    {
        const juce::ScopedLock sl (documentLock);
        if (auto* scene = document.activeScene())
        {
            SourceRef ref;
            ref.storage = embed ? SourceRef::Storage::Embedded : SourceRef::Storage::Reference;
            ref.filePath = originalPath;
            ref.contentHash = "sha256:" + sha256Hex (bytes);
            ref.encoding = "wav";
            ref.sampleRate = decoded.audio->sampleRate;
            ref.numChannels = decoded.audio->buffer.getNumChannels();
            ref.numSamples = decoded.audio->buffer.getNumSamples();
            scene->source = std::move (ref);  // trim reset: the whole file is the fragment
        }
        embeddedBytes = embed ? bytes : juce::MemoryBlock();
        source = decoded.audio;
        sourceMessage = {};
    }
    ++documentVersion;
    rebuildPlan();
    diagnostics.log ("WAV loaded " + fileNameOf (originalPath) + " bytes=" + juce::String ((juce::int64) bytes.getSize())
                     + (embed ? " (embedded)" : " (reference only: larger than 16 MB)"));
    return juce::Result::ok();
}

void SpatialEchoProcessor::restoreSourceFromDocument()
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
    juce::String message;
    if (ref.storage == SourceRef::Storage::Embedded)
        bytes = embeddedBytes;
    else if (! juce::File::isAbsolutePath (ref.filePath) || ! juce::File (ref.filePath).loadFileAsData (bytes))
        message = "source file not found: " + ref.filePath;

    std::shared_ptr<const DecodedAudio> restored;
    if (bytes.getSize() > 0)
    {
        if (ref.contentHash.isNotEmpty() && ref.contentHash != "sha256:" + sha256Hex (bytes))
            message = "source file changed since it was loaded";
        auto decoded = decodeWav (bytes);
        restored = decoded.audio;
        if (restored == nullptr)
            message = decoded.error;
    }
    else if (message.isEmpty())
    {
        message = "embedded audio missing from the saved state";
    }
    source = restored;
    sourceMessage = message;
    if (message.isNotEmpty())
        lastLoadWarnings.add (message);
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
        st.fileName = fileNameOf (scene->source.filePath);
        st.embedded = scene->source.storage == SourceRef::Storage::Embedded;
    }
    return st;
}

std::shared_ptr<const DecodedAudio> SpatialEchoProcessor::getSourceAudio() const
{
    const juce::ScopedLock sl (documentLock);
    return source;
}

// ---- host state -------------------------------------------------------------------

void SpatialEchoProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement root (stateRootTag);
    root.setAttribute (stateVersionAttr, kStateVersion);
    root.setAttribute (diagnosticsAttr, diagnostics.isEnabled());
    root.setAttribute (uiModeAttr, getUiMode() == UiMode::Pro ? "pro" : "easy");

    if (auto paramsXml = parameters.copyState().createXml())
        root.addChildElement (paramsXml.release());

    const auto doc = getDocument();
    auto* docXml = root.createNewChildElement (documentTag);
    docXml->addTextElement (toJson (doc));

    copyXmlToBinary (root, destData);

    const auto* scene = doc.activeScene();
    diagnostics.log ("getState " + juce::String ((juce::int64) destData.getSize()) + " bytes, source="
                     + (scene ? storageName (scene->source.storage) : "none")
                     + (scene ? " embeddedBytes=" + juce::String ((juce::int64) scene->source.embeddedData.getSize()) : ""));
}

void SpatialEchoProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    diagnostics.log ("setState " + juce::String (sizeInBytes) + " bytes, thread="
                     + (juce::MessageManager::existsAndIsCurrentThread() ? "message" : "OTHER"));
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (stateRootTag))
    {
        diagnostics.log ("setState IGNORED: not our state format");
        return;  // unknown data: keep current state
    }

    if (xml->getIntAttribute (stateVersionAttr, 0) > kStateVersion)
    {
        diagnostics.log ("setState IGNORED: newer state version");
        return;  // written by a newer plugin version: do not guess
    }
    diagnostics.setEnabled (xml->getBoolAttribute (diagnosticsAttr, true));
    setUiMode (xml->getStringAttribute (uiModeAttr) == "pro" ? UiMode::Pro : UiMode::Easy);

    if (auto* paramsXml = xml->getChildByName (parameters.state.getType()))
        parameters.replaceState (juce::ValueTree::fromXml (*paramsXml));

    auto* docXml = xml->getChildByName (documentTag);
    if (docXml == nullptr)
    {
        diagnostics.log ("setState: no scene document in state");
        return;
    }

    auto loaded = documentFromJson (docXml->getAllSubText());
    if (loaded)
    {
        const auto warnings = loaded.warnings;
        setDocument (std::move (*loaded.value));
        const juce::ScopedLock sl (documentLock);
        lastLoadWarnings.addArray (warnings);
    }
    else
    {
        const juce::ScopedLock sl (documentLock);
        lastLoadWarnings = juce::StringArray { "scene document not restored: " + loaded.error };
    }

    const auto st = getSourceStatus();
    diagnostics.log ("setState restored: source " + (st.loaded ? "OK " + st.fileName + " " + juce::String (st.seconds, 2) + " s"
                                                                 : juce::String ("NOT loaded"))
                     + (st.message.isNotEmpty() ? " | " + st.message : juce::String())
                     + " | warnings: " + getLastLoadWarnings().joinIntoString ("; "));
}

} // namespace ses

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ses::SpatialEchoProcessor();
}
