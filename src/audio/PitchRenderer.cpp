#include "audio/PitchRenderer.h"

#if defined(_MSC_VER)
 #pragma warning (push, 0)
#elif defined(__GNUC__)
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wall"
 #pragma GCC diagnostic ignored "-Wextra"
 #pragma GCC diagnostic ignored "-Wconversion"
 #pragma GCC diagnostic ignored "-Wsign-conversion"
 #pragma GCC diagnostic ignored "-Wshadow"
 #pragma GCC diagnostic ignored "-Wfloat-equal"
 #pragma GCC diagnostic ignored "-Wunused-parameter"
 #pragma GCC diagnostic ignored "-Wpedantic"
#endif
#include "signalsmith-stretch.h"
#if defined(_MSC_VER)
 #pragma warning (pop)
#elif defined(__GNUC__)
 #pragma GCC diagnostic pop
#endif

#include <vector>

namespace ses
{

SampleBuffer renderPitched (const SampleBuffer& input, double sampleRate, double semitones, double formantSemitones)
{
    const int channels = input.getNumChannels();
    const int n = input.getNumSamples();
    SampleBuffer out (channels, n);
    out.clear();
    if (n == 0 || channels == 0)
        return out;

    signalsmith::stretch::SignalsmithStretch<float> stretch;
    stretch.presetDefault (channels, (float) sampleRate);
    stretch.setTransposeSemitones ((float) semitones);
    if (std::abs (formantSemitones) > 1.0e-6)
        stretch.setFormantSemitones ((float) formantSemitones, false);  // formants move independently of the pitch

    const int inLat = stretch.inputLatency();
    const int outLat = stretch.outputLatency();

    // Input padded with silence so the stream can be primed and drained; the
    // output then has outLat samples of pre-roll, which we skip.
    const int padded = n + inLat;
    std::vector<std::vector<float>> in ((size_t) channels, std::vector<float> ((size_t) padded, 0.0f));
    for (int ch = 0; ch < channels; ++ch)
        std::copy (input.getReadPointer (ch), input.getReadPointer (ch) + n, in[(size_t) ch].begin());
    std::vector<float*> inPtrs ((size_t) channels);
    for (int ch = 0; ch < channels; ++ch)
        inPtrs[(size_t) ch] = in[(size_t) ch].data();

    const int total = n + outLat;
    std::vector<std::vector<float>> o ((size_t) channels, std::vector<float> ((size_t) total, 0.0f));
    std::vector<float*> outPtrs ((size_t) channels);
    for (int ch = 0; ch < channels; ++ch)
        outPtrs[(size_t) ch] = o[(size_t) ch].data();

    struct Offset
    {
        float** ptrs;
        int offset;
        float* operator[] (int c) const { return ptrs[c] + offset; }
    };

    // Prime with the first inLat samples, then stream 1:1, then drain.
    stretch.seek (Offset { inPtrs.data(), 0 }, inLat, 1.0);
    stretch.process (Offset { inPtrs.data(), inLat }, n, Offset { outPtrs.data(), 0 }, n);
    stretch.flush (Offset { outPtrs.data(), n }, outLat);

    for (int ch = 0; ch < channels; ++ch)
        out.copyFrom (ch, 0, o[(size_t) ch].data() + outLat, n);
    return out;
}

PitchRenderer::PitchRenderer() : juce::Thread ("SES pitch render")
{
    startThread (juce::Thread::Priority::low);
}

PitchRenderer::~PitchRenderer()
{
    cancelPendingUpdate();
    signalThreadShouldExit();
    wake.signal();
    stopThread (5000);
}

void PitchRenderer::setBase (SharedSample newBase, double newSampleRate)
{
    const juce::ScopedLock sl (lock);
    if (newBase == base && juce::exactlyEqual (newSampleRate, sampleRate))
        return;
    base = std::move (newBase);
    sampleRate = newSampleRate;
    done.clear();
    wake.signal();
}

void PitchRenderer::request (const std::set<int>& cents)
{
    const juce::ScopedLock sl (lock);
    wanted = cents;
    wanted.erase (0);
    // Drop copies nobody needs any more (memory: up to 16 x fragment).
    for (auto it = done.begin(); it != done.end();)
        it = wanted.count (it->first) ? std::next (it) : done.erase (it);
    wake.signal();
}

std::map<int, SharedSample> PitchRenderer::ready() const
{
    const juce::ScopedLock sl (lock);
    return done;
}

int PitchRenderer::numPending() const
{
    const juce::ScopedLock sl (lock);
    int n = 0;
    for (int c : wanted)
        n += done.count (c) ? 0 : 1;
    return base != nullptr ? n : 0;
}

bool PitchRenderer::isPending (int cents) const
{
    const juce::ScopedLock sl (lock);
    return cents != 0 && base != nullptr && wanted.count (cents) && ! done.count (cents);
}

void PitchRenderer::run()
{
    while (! threadShouldExit())
    {
        SharedSample src;
        double sr = 44100.0;
        int cents = 0;
        bool found = false;
        {
            const juce::ScopedLock sl (lock);
            if (base != nullptr)
                for (int c : wanted)
                    if (! done.count (c))
                    {
                        src = base;
                        sr = sampleRate;
                        cents = c;
                        found = true;
                        break;
                    }
        }
        if (! found)
        {
            wake.wait (-1);
            continue;
        }

        double semis = 0.0, formant = 0.0;
        decodeRenderKey (cents, semis, formant);
        auto rendered = std::make_shared<const SampleBuffer> (renderPitched (*src, sr, semis, formant));
        bool stillValid = false;
        {
            const juce::ScopedLock sl (lock);
            if (src == base && wanted.count (cents))
            {
                done[cents] = rendered;
                stillValid = true;
            }
        }
        if (stillValid)
            triggerAsyncUpdate();
    }
}

void PitchRenderer::handleAsyncUpdate()
{
    if (onRenderFinished)
        onRenderFinished();
}

} // namespace ses
