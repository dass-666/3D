#include "plugin/Diagnostics.h"

namespace ses
{

bool HostSnapshot::isSounding (juce::uint64 key) const noexcept
{
    for (auto& k : soundingKeys)
        if (k.load (std::memory_order_relaxed) == key)
            return true;
    return false;
}

void HostSnapshot::store (const TransportInfo& t, int numSamples, const EngineStats& s, const EchoEngine& engine) noexcept
{
    const int voices = engine.numActiveVoices();
    int k = 0;
    engine.forEachActiveKey ([&] (juce::uint64 key)
    {
        if (k < kMaxKeys)
            soundingKeys[(size_t) k++].store (key == 0 ? ~juce::uint64 (0) : key, std::memory_order_relaxed);
    });
    for (; k < kMaxKeys; ++k)
        soundingKeys[(size_t) k].store (0, std::memory_order_relaxed);
    referenceBeat.store (s.lastReferenceBeat, std::memory_order_relaxed);
    references.store (s.references, std::memory_order_relaxed);
    constexpr auto r = std::memory_order_relaxed;
    hasBpm.store (t.bpm.has_value(), r);
    bpm.store (t.bpm.value_or (0.0), r);
    hasPpq.store (t.ppq.has_value(), r);
    ppq.store (t.ppq.value_or (0.0), r);
    hasTimeSamples.store (t.timeSamples.has_value(), r);
    isPlaying.store (t.isPlaying, r);
    blockSize.store (numSamples, r);
    if (numSamples > maxBlockSize.load (r))
        maxBlockSize.store (numSamples, r);
    blocks.fetch_add (1, r);
    triggers.store (s.triggers, r);
    fired.store (s.fired, r);
    firedLate.store (s.firedLate, r);
    dropped.store (s.dropped, r);
    jumps.store (s.jumps, r);
    lastJumpBeats.store (s.lastJumpBeats, r);
    maxJitterBeats.store (s.maxJitterBeats, r);
    lastTriggerBeat.store (s.lastTriggerBeat, r);
    lastScheduledBeat.store (s.lastScheduledBeat, r);
    lastFiredBeat.store (s.lastFiredBeat, r);
    usedHostTimeline.store (s.usedHostTimeline, r);
    ppqDerived.store (s.ppqDerivedFromSamples, r);
    activeVoices.store (voices, r);
}

DiagnosticLog::DiagnosticLog (const HostSnapshot& s) : snapshot (s)
{
    startTimerHz (5);
}

DiagnosticLog::~DiagnosticLog()
{
    stopTimer();
    timerCallback();
}

juce::File DiagnosticLog::getFile() const
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("Spatial Echo Studio")
        .getChildFile ("diagnostico.log");
}

void DiagnosticLog::setEnabled (bool shouldBeEnabled)
{
    const juce::ScopedLock sl (lock);
    enabled = shouldBeEnabled;
    if (! enabled)
        out.reset();
}

void DiagnosticLog::log (const juce::String& line)
{
    const juce::ScopedLock sl (lock);
    if (enabled && queued.size() < 1000)
        queued.add (juce::Time::getCurrentTime().formatted ("%H:%M:%S ") + line);
}

void DiagnosticLog::write (const juce::String& line)
{
    if (out == nullptr)
    {
        const auto file = getFile();
        file.getParentDirectory().createDirectory();
        if (file.getSize() > 2 * 1024 * 1024)
            file.deleteFile();  // keep it small: one test session is enough
        out = std::make_unique<juce::FileOutputStream> (file);
        if (out->failedToOpen())
        {
            out.reset();
            return;
        }
        out->writeText ("\n==== " + juce::Time::getCurrentTime().toString (true, true) + " ====\n", false, false, nullptr);
    }
    out->writeText (line + "\n", false, false, nullptr);
}

void DiagnosticLog::timerCallback()
{
    constexpr auto r = std::memory_order_relaxed;
    const bool playing = snapshot.isPlaying.load (r);
    const bool hasPpq = snapshot.hasPpq.load (r);
    const double bpm = snapshot.bpm.load (r);
    const int triggers = snapshot.triggers.load (r), fired = snapshot.fired.load (r);
    const int dropped = snapshot.dropped.load (r), jumps = snapshot.jumps.load (r);
    const int maxBlock = snapshot.maxBlockSize.load (r);

    const auto host = [&]
    {
        return juce::String ("bpm=") + (snapshot.hasBpm.load (r) ? juce::String (bpm, 3) : "n/a")
             + " ppq=" + (hasPpq ? juce::String (snapshot.ppq.load (r), 4) : "n/a")
             + " playing=" + (playing ? "1" : "0")
             + " samplePos=" + (snapshot.hasTimeSamples.load (r) ? "yes" : "n/a")
             + " block=" + juce::String (snapshot.blockSize.load (r))
             + " hostTimeline=" + (snapshot.usedHostTimeline.load (r) ? (snapshot.ppqDerived.load (r) ? "derived" : "ppq") : "internal");
    };

    if (playing != lastPlaying || hasPpq != lastHasPpq)
        log (juce::String ("transport ") + (playing ? "PLAY " : "STOP ") + host());
    if (std::abs (bpm - lastBpm) > 1.0e-6)
        log ("tempo " + host());
    if (maxBlock != lastMaxBlock)
        log ("max block size now " + juce::String (maxBlock));
    if (triggers != lastTriggers)
        log ("trigger #" + juce::String (triggers) + " at beat " + juce::String (snapshot.lastTriggerBeat.load (r), 4)
             + " -> scheduled up to beat " + juce::String (snapshot.lastScheduledBeat.load (r), 4) + " | " + host());
    if (fired != lastFired)
        log ("repetition started at beat " + juce::String (snapshot.lastFiredBeat.load (r), 4) + " (total "
             + juce::String (fired) + ", late " + juce::String (snapshot.firedLate.load (r)) + ")");
    if (dropped != lastDropped)
        log ("WARNING dropped repetitions: " + juce::String (dropped));
    if (jumps != lastJumps)
        log ("transport jump " + juce::String (snapshot.lastJumpBeats.load (r), 4) + " beats (pending cancelled) | " + host());

    lastPlaying = playing; lastHasPpq = hasPpq; lastBpm = bpm;
    lastTriggers = triggers; lastFired = fired; lastDropped = dropped; lastJumps = jumps; lastMaxBlock = maxBlock;

    juce::StringArray lines;
    {
        const juce::ScopedLock sl (lock);
        lines.swapWith (queued);
        if (! enabled)
            return;
        for (auto& l : lines)
            write (l);
        if (out != nullptr)
            out->flush();
    }
}

} // namespace ses
