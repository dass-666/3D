// B2 regressions found while auditing the FL Studio failures (sync / BPM change).
// These simulate host behaviour; they do not replace a test inside FL Studio.

#include "audio/EchoEngine.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using namespace ses;

namespace
{
PlaybackPlan plan1 (double sr, double beat)
{
    PlaybackPlan p;
    p.sampleRate = sr;
    auto buf = std::make_shared<SampleBuffer> (1, 2000);
    for (int i = 0; i < 2000; ++i)
        buf->setSample (0, i, 0.5f);
    p.sample = buf;
    p.gridBeats = 1.0;
    p.echoes.push_back ({ 1, beat, {} });
    return p;
}

int firstNonZero (const std::vector<float>& x)
{
    for (size_t i = 0; i < x.size(); ++i)
        if (x[i] != 0.0f)
            return (int) i;
    return -1;
}

// Host model: per block it reports ppq = truePpq + error(block) and bpm(block).
template <typename PpqFn, typename BpmFn>
std::vector<float> run (EchoEngine& e, const PlaybackPlan& plan, double sr, int block, int blocks,
                        PpqFn reportedPpq, BpmFn bpmAt, bool withPpq = true)
{
    std::vector<float> out;
    juce::AudioBuffer<float> buf (1, block);
    for (int b = 0; b < blocks; ++b)
    {
        buf.clear();
        TransportInfo t;
        t.bpm = bpmAt (b);
        t.isPlaying = true;
        if (withPpq)
            t.ppq = reportedPpq (b);
        t.timeSamples = (juce::int64) b * block;
        e.process (buf, &plan, t, b == 0, false, 1.0f, 1.0f);
        for (int i = 0; i < block; ++i)
            out.push_back (buf.getSample (0, i));
    }
    return out;
}
} // namespace

TEST_CASE ("Host ppq rounding/jitter no longer cancels the pending repetition")
{
    const double sr = 44100.0, bpm = 120.0;
    const int block = 512;
    EchoEngine e;
    e.prepare (sr);
    const auto plan = plan1 (sr, 1.0);
    // +-0.004 beats (about 2 ms) alternating error, as a host rounding to ms would produce.
    auto ppq = [&] (int b) { return b * block * bpm / 60.0 / sr + ((b % 2) ? 0.004 : -0.004); };
    const auto out = run (e, plan, sr, block, 200, ppq, [&] (int) { return bpm; });

    const int onset = firstNonZero (out);
    REQUIRE (onset >= 0);  // old engine: -1 (cancelled every block)
    const int expected = (int) std::llround (1.0 * 60.0 / bpm * sr);  // trigger at beat 0, echo at beat 1
    CHECK (std::abs (onset - expected) <= (int) (0.005 * sr));
    CHECK (e.stats().jumps == 0);
    CHECK (e.stats().fired == 1);
}

TEST_CASE ("A tempo change inside a block does not cancel the pending repetition")
{
    const double sr = 48000.0;
    const int block = 1024;
    EchoEngine e;
    e.prepare (sr);
    const auto plan = plan1 (sr, 4.0);

    // The host changes 120 -> 150 BPM in the middle of block 10 and integrates
    // its tempo map exactly; the block-start bpm we see is the old one.
    std::vector<double> ppqAt (400);
    double beat = 0.0;
    for (int b = 0; b < 400; ++b)
    {
        ppqAt[(size_t) b] = beat;
        for (int i = 0; i < block; ++i)
            beat += ((b > 10 || (b == 10 && i >= block / 2)) ? 150.0 : 120.0) / 60.0 / sr;
    }
    const auto out = run (e, plan, sr, block, 400, [&] (int b) { return ppqAt[(size_t) b]; },
                          [&] (int b) { return b <= 10 ? 120.0 : 150.0; });

    const int onset = firstNonZero (out);
    REQUIRE (onset >= 0);  // old engine: cancelled at block 11
    // Echo at beat 4 on the host's own timeline, within one block of rounding.
    int hostBlock = 0;
    while (hostBlock + 1 < 400 && ppqAt[(size_t) hostBlock + 1] <= 4.0)
        ++hostBlock;
    CHECK (std::abs (onset - hostBlock * block) <= block);
    CHECK (e.stats().jumps == 0);
}

TEST_CASE ("A real relocation (loop back) still cancels the pending repetition")
{
    const double sr = 44100.0, bpm = 120.0;
    const int block = 256;
    EchoEngine e;
    e.prepare (sr);
    const auto plan = plan1 (sr, 2.0);
    // Plays from beat 8, loops back to beat 0 after 0.5 beats.
    auto ppq = [&] (int b)
    {
        const double p = 8.0 + b * block * bpm / 60.0 / sr;
        return p < 8.5 ? p : p - 8.5;
    };
    const auto out = run (e, plan, sr, block, 400, ppq, [&] (int) { return bpm; });
    CHECK (firstNonZero (out) == -1);
    CHECK (e.stats().jumps >= 1);
}

TEST_CASE ("Host that reports no ppq while playing still gets a musical grid from its sample position")
{
    const double sr = 44100.0, bpm = 100.0;
    const int block = 441;
    EchoEngine e;
    e.prepare (sr);
    const auto plan = plan1 (sr, 1.0);
    // Transport starts at sample 0 -> beat 0; trigger on block 0 aligns to beat 0, echo at beat 1.
    const auto out = run (e, plan, sr, block, 200, [] (int) { return 0.0; }, [&] (int) { return bpm; }, false);
    const int expected = (int) std::llround (60.0 / bpm * sr);
    CHECK (firstNonZero (out) == expected);
    CHECK (e.stats().ppqDerivedFromSamples);
    CHECK (e.stats().usedHostTimeline);
}

TEST_CASE ("A repetition the host skipped slightly past is played late instead of lost")
{
    const double sr = 44100.0, bpm = 120.0;
    const int block = 512;
    EchoEngine e;
    e.prepare (sr);
    const auto plan = plan1 (sr, 1.0);
    const double bps = bpm / 60.0 / sr;
    // From block 40 the host reports 0.02 beats ahead (10 ms), skipping over beat 1 at a block edge.
    const int edgeBlock = (int) std::floor (1.0 / (block * bps));
    auto ppq = [&] (int b) { return b * block * bps + (b >= edgeBlock ? 0.02 : 0.0); };
    const auto out = run (e, plan, sr, block, 200, ppq, [&] (int) { return bpm; });
    CHECK (firstNonZero (out) >= 0);
    CHECK (e.stats().dropped == 0);
}
