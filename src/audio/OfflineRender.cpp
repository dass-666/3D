#include "audio/OfflineRender.h"

#include "audio/PitchRenderer.h"
#include "audio/SafetyLimiter.h"

#include <set>

namespace ses
{

double estimateRenderSeconds (const PlaybackPlan& plan, const OfflineRenderSettings& s, juce::uint64 onlyKey)
{
    const double spb = 60.0 / juce::jmax (1.0, s.bpm);
    double end = 0.0;
    for (const auto& e : plan.echoes)
    {
        if (onlyKey != 0 && e.key != onlyKey)
            continue;
        const double start = e.freeTime ? e.seconds + (e.beat - e.pointBeat) * spb : e.beat * spb;
        const double len = e.sliceBeats > 0.0 ? e.sliceBeats * spb
                                              : (double) juce::jmax<juce::int64> (0, e.regionEnd - e.regionStart) / plan.sampleRate;
        end = juce::jmax (end, start + len + PointFxChain::tailSeconds (e.effects));
    }
    if (s.includeFragment && plan.sample != nullptr)
        end = juce::jmax (end, plan.sample->getNumSamples() / plan.sampleRate);
    return end;
}

OfflineRenderResult renderOffline (const Scene& scene, const DecodedAudio* source, const OfflineRenderSettings& s,
                                   const std::function<bool (double)>& progress)
{
    juce::ScopedNoDenormals noDenormals;
    OfflineRenderResult out;
    out.sampleRate = s.sampleRate;
    if (source == nullptr || s.sampleRate <= 0.0)
    {
        out.error = juce::String::fromUTF8 ("No hay audio cargado: importa un WAV o captura el Mixer primero");
        return out;
    }

    // Same fragment and pitched copies as the real-time path, at the export rate.
    const auto base = renderSample (scene.source, source, s.sampleRate);
    if (base == nullptr || base->getNumSamples() == 0)
    {
        out.error = juce::String::fromUTF8 ("El fragmento est\xc3\xa1 vac\xc3\xad" "o");
        return out;
    }
    const juce::uint64 onlyKey = s.onlyPointId.isNotEmpty() ? keyForPoint (s.onlyPointId) : 0;
    std::set<int> keys;
    for (const auto& p : scene.points)
        if (p.enabled && (onlyKey == 0 || p.id == s.onlyPointId))
            if (const int k = renderKeyFor (p.sound); k != 0)
                keys.insert (k);
    std::map<int, SharedSample> pitched;
    int doneKeys = 0;
    const double pitchShare = keys.empty() ? 0.0 : 0.3;  // progress share of the pitch stage
    for (int k : keys)
    {
        double semis = 0.0, formant = 0.0;
        decodeRenderKey (k, semis, formant);
        pitched[k] = std::make_shared<const SampleBuffer> (renderPitched (*base, s.sampleRate, semis, formant));
        if (progress && ! progress (pitchShare * ++doneKeys / (double) keys.size()))
        {
            out.cancelled = true;
            return out;
        }
    }

    auto plan = buildPlan (scene, base, pitched, s.sampleRate);
    std::set<juce::uint64> pointKeys;
    for (const auto& e : plan->echoes)
        if (onlyKey == 0 || e.key == onlyKey)
            pointKeys.insert (e.key);
    out.pointsRendered = (int) pointKeys.size();
    if (out.pointsRendered == 0 && ! s.includeFragment)
    {
        out.error = onlyKey != 0 ? juce::String::fromUTF8 ("La repetici\xc3\xb3n seleccionada est\xc3\xa1 silenciada o no existe")
                                 : juce::String::fromUTF8 ("No hay repeticiones activas que exportar");
        return out;
    }

    auto engine = std::make_unique<EchoEngine>();
    engine->prepare (s.sampleRate);
    SafetyLimiter limiter;
    limiter.prepare (s.sampleRate);

    const double estimate = juce::jmax (0.1, estimateRenderSeconds (*plan, s, onlyKey));
    const auto cap = (int) std::ceil (juce::jmin (s.maxSeconds, estimate + 30.0) * s.sampleRate);
    out.audio.setSize (2, juce::jmin (cap, (int) std::ceil (estimate * s.sampleRate) + s.blockSize), false, true, false);
    out.audio.clear();

    TransportInfo transport;
    transport.bpm = s.bpm;  // stopped: the engine runs on its own clock at this tempo
    const float wet = juce::jlimit (0.0f, 1.0f, s.mixPercent / 100.0f);
    const float gain = juce::Decibels::decibelsToGain (s.outputDb);
    const int bs = juce::jlimit (16, EchoEngine::kMaxFxBlock, s.blockSize);
    juce::AudioBuffer<float> block (2, bs);

    int written = 0;
    bool first = true;
    while (true)
    {
        if (written >= cap)
        {
            out.truncated = true;
            break;
        }
        const int n = juce::jmin (bs, cap - written);
        block.clear();
        EngineRequests req;
        if (first)
        {
            req.previewScene = onlyKey == 0;
            req.triggerPoint = onlyKey;
            req.previewFragment = s.includeFragment;
        }
        juce::AudioBuffer<float> view (block.getArrayOfWritePointers(), 2, n);
        engine->process (view, plan.get(), transport, req, wet, wet);
        first = false;
        view.applyGain (gain);
        if (s.protect && (engine->numActiveVoices() > 0 || limiter.isReducing()))
            limiter.process (view);
        else
            limiter.idle();

        if (written + n > out.audio.getNumSamples())
            out.audio.setSize (2, juce::jmin (cap, juce::jmax (written + n, out.audio.getNumSamples() * 3 / 2)), true, true, false);
        for (int ch = 0; ch < 2; ++ch)
            out.audio.copyFrom (ch, written, view, ch, 0, n);
        written += n;

        if (engine->numActiveVoices() == 0 && engine->numPendingEvents() == 0 && engine->numActiveFxSlots() == 0)
            break;
        if (progress && (written / bs) % 16 == 0
            && ! progress (pitchShare + (1.0 - pitchShare) * juce::jmin (0.99, written / (estimate * s.sampleRate))))
        {
            out.cancelled = true;
            out.audio.setSize (2, 0);
            return out;
        }
    }

    // End = the last sample above -120 dBFS (fades and tails included; the
    // trailing digital silence of the last block is dropped).
    int last = -1;
    for (int ch = 0; ch < 2; ++ch)
    {
        const auto* d = out.audio.getReadPointer (ch);
        for (int i = written - 1; i > last; --i)
            if (std::abs (d[i]) > 1.0e-6f)
            {
                last = i;
                break;
            }
    }
    if (last < 0)
    {
        out.error = juce::String::fromUTF8 ("La exportaci\xc3\xb3n est\xc3\xa1 en silencio (\xc2\xbfMix al 0 %?)");
        out.audio.setSize (2, 0);
        return out;
    }
    out.audio.setSize (2, last + 1, true, false, false);
    for (int ch = 0; ch < 2; ++ch)
        out.peak = juce::jmax (out.peak, out.audio.getMagnitude (ch, 0, out.audio.getNumSamples()));
    if (progress)
        progress (1.0);
    return out;
}

int bitsFor (WavFormat f) noexcept
{
    return f == WavFormat::Pcm16 ? 16 : f == WavFormat::Pcm24 ? 24 : 32;
}

juce::Result writeWavFile (const juce::File& file, const juce::AudioBuffer<float>& audio, double sampleRate, WavFormat format)
{
    if (audio.getNumSamples() == 0)
        return juce::Result::fail ("Nada que escribir");
    if (! file.getParentDirectory().createDirectory())
        return juce::Result::fail ("No se pudo crear la carpeta " + file.getParentDirectory().getFullPathName());

    const juce::AudioBuffer<float>* toWrite = &audio;
    juce::AudioBuffer<float> dithered;
    if (format == WavFormat::Pcm16)
    {
        // TPDF dither of 1 LSB, fixed seed: reproducible bytes.
        dithered.makeCopyOf (audio);
        juce::Random rng (0x5e5);
        const float lsb = 1.0f / 32768.0f;
        for (int ch = 0; ch < dithered.getNumChannels(); ++ch)
        {
            auto* d = dithered.getWritePointer (ch);
            for (int i = 0; i < dithered.getNumSamples(); ++i)
                d[i] += (rng.nextFloat() - rng.nextFloat()) * lsb;
        }
        toWrite = &dithered;
    }

    juce::TemporaryFile temp (file);
    {
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (temp.getFile());
        if (! static_cast<juce::FileOutputStream*> (stream.get())->openedOk())
            return juce::Result::fail ("No se pudo escribir en " + file.getParentDirectory().getFullPathName());
        juce::WavAudioFormat wav;
        using SF = juce::AudioFormatWriterOptions::SampleFormat;
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                        .withSampleRate (sampleRate)
                                                        .withNumChannels (toWrite->getNumChannels())
                                                        .withBitsPerSample (bitsFor (format))
                                                        .withSampleFormat (format == WavFormat::Float32 ? SF::floatingPoint : SF::integral));
        if (writer == nullptr)
            return juce::Result::fail ("Formato WAV no soportado");
        if (! writer->writeFromAudioSampleBuffer (*toWrite, 0, toWrite->getNumSamples()))
            return juce::Result::fail ("Error al escribir el WAV");
    }
    if (! temp.overwriteTargetFileWithTemporary())
        return juce::Result::fail ("No se pudo reemplazar " + file.getFullPathName() + " (\xc2\xbf" "abierto en otro programa?)");
    return juce::Result::ok();
}

} // namespace ses
