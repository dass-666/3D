#include "audio/SourceAudio.h"

#include <juce_cryptography/juce_cryptography.h>

namespace ses
{

DecodeResult decodeWav (const juce::MemoryBlock& fileBytes)
{
    DecodeResult r;
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader (
        wav.createReaderFor (new juce::MemoryInputStream (fileBytes, false), true));

    if (reader == nullptr)
    {
        r.error = "not a readable WAV file";
        return r;
    }
    if (reader->numChannels < 1 || reader->numChannels > 2)
    {
        r.error = "only mono or stereo WAV files are supported";
        return r;
    }
    if (reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
    {
        r.error = "WAV file is empty";
        return r;
    }
    if ((double) reader->lengthInSamples / reader->sampleRate > kMaxSourceSeconds)
    {
        r.error = "WAV file is longer than " + juce::String (kMaxSourceSeconds, 0) + " s";
        return r;
    }

    auto audio = std::make_shared<DecodedAudio>();
    audio->sampleRate = reader->sampleRate;
    audio->buffer.setSize ((int) reader->numChannels, (int) reader->lengthInSamples);
    if (! reader->read (&audio->buffer, 0, (int) reader->lengthInSamples, 0, true, true))
    {
        r.error = "could not decode WAV data";
        return r;
    }
    r.audio = std::move (audio);
    return r;
}

juce::String sha256Hex (const juce::MemoryBlock& data)
{
    return juce::SHA256 (data).toHexString();
}

juce::AudioBuffer<float> resampled (const DecodedAudio& src, double targetSampleRate)
{
    if (targetSampleRate <= 0.0 || std::abs (src.sampleRate - targetSampleRate) < 1.0e-9)
        return src.buffer;

    const double ratio = src.sampleRate / targetSampleRate;  // input samples per output sample
    const int outLength = juce::jmax (1, (int) std::floor (src.buffer.getNumSamples() / ratio));
    juce::AudioBuffer<float> out (src.buffer.getNumChannels(), outLength);

    for (int ch = 0; ch < src.buffer.getNumChannels(); ++ch)
    {
        juce::LagrangeInterpolator interp;
        // Pad the input so the interpolator never reads past the end.
        juce::HeapBlock<float> padded ((size_t) src.buffer.getNumSamples() + 8, true);
        std::copy_n (src.buffer.getReadPointer (ch), src.buffer.getNumSamples(), padded.get());
        interp.process (ratio, padded.get(), out.getWritePointer (ch), outLength);
    }
    return out;
}

} // namespace ses
