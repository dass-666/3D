#include "model/Scene.h"
#include "model/VoicePresets.h"

#include <algorithm>
#include <cmath>

namespace ses
{

double beatsPerStep (RhythmDivision r)
{
    switch (r)
    {
        case RhythmDivision::Quarter:        return 1.0;
        case RhythmDivision::Eighth:         return 0.5;
        case RhythmDivision::DottedEighth:   return 0.75;
        case RhythmDivision::Sixteenth:      return 0.25;
        case RhythmDivision::QuarterTriplet: return 2.0 / 3.0;
        case RhythmDivision::EighthTriplet:  return 1.0 / 3.0;
    }
    return 1.0;
}

juce::String toString (RhythmDivision r)
{
    switch (r)
    {
        case RhythmDivision::Quarter:        return "1/4";
        case RhythmDivision::Eighth:         return "1/8";
        case RhythmDivision::DottedEighth:   return "1/8.";
        case RhythmDivision::Sixteenth:      return "1/16";
        case RhythmDivision::QuarterTriplet: return "1/4T";
        case RhythmDivision::EighthTriplet:  return "1/8T";
    }
    return "1/4";
}

std::optional<RhythmDivision> rhythmFromString (const juce::String& s)
{
    for (auto r : { RhythmDivision::Quarter, RhythmDivision::Eighth, RhythmDivision::DottedEighth,
                    RhythmDivision::Sixteenth, RhythmDivision::QuarterTriplet, RhythmDivision::EighthTriplet })
        if (toString (r) == s)
            return r;
    return std::nullopt;
}

juce::String toString (SpatialMode) { return "stereo"; }

std::optional<SpatialMode> spatialModeFromString (const juce::String& s)
{
    if (s == "stereo")
        return SpatialMode::Stereo;
    return std::nullopt;
}

Position clamped (Position p)
{
    p.x = std::clamp (p.x, limits::minCoord, limits::maxCoord);
    p.y = std::clamp (p.y, limits::minCoord, limits::maxCoord);
    return p;
}

Timing clamped (Timing t)
{
    t.beat = std::clamp (t.beat, limits::minBeat, limits::maxBeat);
    t.seconds = std::clamp (t.seconds, limits::minSeconds, limits::maxSeconds);
    return t;
}

PlayRegion clamped (PlayRegion r)
{
    r.start = std::clamp (r.start, 0.0, 1.0);
    r.end = std::clamp (r.end, r.start, 1.0);
    return r;
}

juce::String toString (TimeMode m) { return m == TimeMode::Free ? "free" : "beats"; }

std::optional<TimeMode> timeModeFromString (const juce::String& s)
{
    if (s == "beats") return TimeMode::Beats;
    if (s == "free")  return TimeMode::Free;
    return std::nullopt;
}

juce::String toString (ShaperSettings::Shape s)
{
    switch (s)
    {
        case ShaperSettings::Shape::Square:   return "square";
        case ShaperSettings::Shape::Triangle: return "triangle";
        case ShaperSettings::Shape::Saw:      return "saw";
    }
    return "square";
}

std::optional<ShaperSettings::Shape> shaperShapeFromString (const juce::String& s)
{
    if (s == "square")   return ShaperSettings::Shape::Square;
    if (s == "triangle") return ShaperSettings::Shape::Triangle;
    if (s == "saw")      return ShaperSettings::Shape::Saw;
    return std::nullopt;
}

juce::String toString (ShaperSettings::RateMode m)
{
    switch (m)
    {
        case ShaperSettings::RateMode::Count:        return "count";
        case ShaperSettings::RateMode::Milliseconds: return "ms";
        case ShaperSettings::RateMode::Division:     return "division";
    }
    return "count";
}

std::optional<ShaperSettings::RateMode> shaperRateModeFromString (const juce::String& s)
{
    if (s == "count")    return ShaperSettings::RateMode::Count;
    if (s == "ms")       return ShaperSettings::RateMode::Milliseconds;
    if (s == "division") return ShaperSettings::RateMode::Division;
    return std::nullopt;
}

juce::String toString (ShaperSettings::Distribution d)
{
    switch (d)
    {
        case ShaperSettings::Distribution::Uniform:      return "uniform";
        case ShaperSettings::Distribution::Accelerating: return "accelerating";
        case ShaperSettings::Distribution::Decelerating: return "decelerating";
    }
    return "uniform";
}

std::optional<ShaperSettings::Distribution> shaperDistributionFromString (const juce::String& s)
{
    if (s == "uniform")      return ShaperSettings::Distribution::Uniform;
    if (s == "accelerating") return ShaperSettings::Distribution::Accelerating;
    if (s == "decelerating") return ShaperSettings::Distribution::Decelerating;
    return std::nullopt;
}

ShaperSettings clamped (ShaperSettings s)
{
    s.cuts = std::clamp (s.cuts, limits::minShaperCuts, limits::maxShaperCuts);
    s.periodMs = std::clamp (s.periodMs, limits::minShaperPeriodMs, limits::maxShaperPeriodMs);
    s.depthPercent = std::clamp (s.depthPercent, 0.0, 100.0);
    s.openPercent = std::clamp (s.openPercent, 5.0, 95.0);
    s.attackMs = std::clamp (s.attackMs, limits::minShaperRampMs, limits::maxShaperAttackMs);
    s.releaseMs = std::clamp (s.releaseMs, limits::minShaperRampMs, limits::maxShaperReleaseMs);
    s.accelPercent = std::clamp (s.accelPercent, 0.0, 100.0);
    s.regionStart = std::clamp (s.regionStart, 0.0, 1.0);
    s.regionEnd = std::clamp (s.regionEnd, s.regionStart, 1.0);
    s.lengthMs = std::clamp (s.lengthMs, 0.0, 60000.0);
    s.makeupDb = std::clamp (s.makeupDb, -limits::maxShaperMakeupDb, limits::maxShaperMakeupDb);
    // Pattern: only 'x' and '.', at most 64 steps; anything else is dropped.
    juce::String pat;
    for (auto c : s.pattern)
        if ((c == 'x' || c == 'X' || c == '.') && pat.length() < 64)
            pat << (c == '.' ? '.' : 'x');
    s.pattern = pat.containsChar ('.') ? pat : juce::String();  // all open = no pattern
    return s;
}

double minimumCutSeconds (const ShaperSettings& s)
{
    return (s.attackMs + s.releaseMs + 1.0) / 1000.0;
}

int maxUsefulCuts (const ShaperSettings& s, double spanSeconds)
{
    if (spanSeconds <= 0.0)
        return limits::minShaperCuts;
    return std::clamp ((int) std::floor (spanSeconds / minimumCutSeconds (s)), limits::minShaperCuts, limits::maxShaperCuts);
}

const std::vector<ShaperPreset>& shaperPresets()
{
    static const std::vector<ShaperPreset> presets = []
    {
        using S = ShaperSettings;
        std::vector<ShaperPreset> v;
        auto add = [&] (const char* id, const char* name, auto&& fn)
        {
            S s;
            s.enabled = true;
            fn (s);
            v.push_back ({ id, name, clamped (s) });
        };
        add ("gate", "Gate", [] (S& s) { s.cuts = 8; s.openPercent = 50; s.attackMs = 1; s.releaseMs = 5; });
        add ("fast-gate", "Fast Gate", [] (S& s) { s.cuts = 24; s.openPercent = 45; s.attackMs = 0.5; s.releaseMs = 2; s.autoLevel = true; });
        add ("trance-gate", "Trance Gate", [] (S& s) { s.rateMode = S::RateMode::Division; s.division = StutterSettings::Division::Sixteenth; s.openPercent = 50; s.attackMs = 1; s.releaseMs = 8; });
        add ("machine-gun", "Machine Gun", [] (S& s) { s.cuts = 64; s.openPercent = 40; s.attackMs = 0.3; s.releaseMs = 1.5; s.autoLevel = true; });
        add ("vocal-chop", "Vocal Chop", [] (S& s) { s.cuts = 6; s.openPercent = 65; s.attackMs = 2; s.releaseMs = 12; s.pattern = "xx.x"; });
        add ("glitch", "Glitch Vocal", [] (S& s) { s.cuts = 32; s.openPercent = 35; s.attackMs = 0.3; s.releaseMs = 2; s.pattern = "x.xx.x.."; s.autoLevel = true; });
        add ("tremolo", "Tremolo", [] (S& s) { s.shape = S::Shape::Triangle; s.rateMode = S::RateMode::Milliseconds; s.periodMs = 120; s.depthPercent = 60; });
        add ("accel", "Cortes acelerados", [] (S& s) { s.cuts = 16; s.openPercent = 50; s.distribution = S::Distribution::Accelerating; s.accelPercent = 70; s.releaseMs = 4; });
        add ("decel", "Cortes desacelerados", [] (S& s) { s.cuts = 16; s.openPercent = 50; s.distribution = S::Distribution::Decelerating; s.accelPercent = 70; s.releaseMs = 4; });
        add ("fade-stutter", "Fade Stutter", [] (S& s) { s.shape = S::Shape::Saw; s.cuts = 12; s.depthPercent = 100; s.attackMs = 0.5; s.releaseMs = 2; });
        add ("fade-end", "Fade final", [] (S& s) { s.shape = S::Shape::Saw; s.cuts = 2; s.depthPercent = 100; s.wholeVoice = false; s.regionStart = 0.5; s.regionEnd = 1.0; s.attackMs = 5; s.releaseMs = 20; });
        return v;
    }();
    return presets;
}

SoundSettings clamped (SoundSettings s)
{
    s.pitchSemitones = std::clamp (s.pitchSemitones, limits::minPitch, limits::maxPitch);
    s.character = std::clamp (s.character, limits::minCharacter, limits::maxCharacter);
    s.speedPercent = std::clamp (s.speedPercent, limits::minSpeed, limits::maxSpeed);
    return s;
}

const Scene* Document::activeScene() const
{
    for (auto& s : scenes)
        if (s.id == activeSceneId)
            return &s;
    return scenes.empty() ? nullptr : &scenes.front();
}

Scene* Document::activeScene()
{
    return const_cast<Scene*> (std::as_const (*this).activeScene());
}

juce::String makeId()
{
    return juce::Uuid().toDashedString();
}

Point* findPoint (Scene& scene, const juce::String& id)
{
    auto it = std::find_if (scene.points.begin(), scene.points.end(), [&] (auto& p) { return p.id == id; });
    return it == scene.points.end() ? nullptr : &*it;
}

const Point* findPoint (const Scene& scene, const juce::String& id)
{
    return findPoint (const_cast<Scene&> (scene), id);
}

int nextPointNumber (const Scene& scene)
{
    int maxNumber = 0;
    for (auto& p : scene.points)
        maxNumber = std::max (maxNumber, p.number);
    return maxNumber + 1;
}

Timing nextFreeTiming (const Scene& scene)
{
    const auto step = beatsPerStep (scene.rhythm);
    double latest = 0.0, latestSeconds = 0.0;
    for (auto& p : scene.points)
    {
        latest = std::max (latest, p.timing.beat);
        latestSeconds = std::max (latestSeconds, p.timing.seconds);
    }
    // Free time has no tempo: one step at 120 BPM.
    return clamped (Timing { latest + step, latestSeconds + step * 0.5 });
}

std::optional<juce::String> addPoint (Scene& scene, Position pos, std::optional<Timing> timing,
                                      const VoicePreset* preset)
{
    if ((int) scene.points.size() >= scene.maxPoints)
        return std::nullopt;

    Point p;
    p.id = makeId();
    p.number = nextPointNumber (scene);
    p.position = clamped (pos);
    p.timing = clamped (timing.value_or (nextFreeTiming (scene)));

    if (preset != nullptr)
    {
        p.voice = { preset->id, preset->name, false };
        p.sound = clamped (preset->sound);
    }
    else
    {
        p.voice = { presetIds::original, "Original", false };
    }

    scene.points.push_back (p);
    return p.id;
}

bool removePoint (Scene& scene, const juce::String& id)
{
    const auto before = scene.points.size();
    std::erase_if (scene.points, [&] (auto& p) { return p.id == id; });
    return scene.points.size() != before;
}

std::optional<juce::String> duplicatePoint (Scene& scene, const juce::String& id)
{
    if ((int) scene.points.size() >= scene.maxPoints)
        return std::nullopt;

    const auto* src = findPoint (scene, id);
    if (src == nullptr)
        return std::nullopt;

    Point copy = *src;
    copy.id = makeId();
    copy.number = nextPointNumber (scene);
    const auto step = beatsPerStep (scene.rhythm);
    copy.timing = clamped (Timing { src->timing.beat + step, src->timing.seconds + step * 0.5 });
    copy.position = clamped (Position { src->position.x + 5.0, src->position.y });

    scene.points.push_back (copy);  // may invalidate src; not used after this
    return copy.id;
}

bool movePoint (Scene& scene, const juce::String& id, Position pos)
{
    if (auto* p = findPoint (scene, id))
    {
        p->position = clamped (pos);
        return true;
    }
    return false;
}

bool setTiming (Scene& scene, const juce::String& id, Timing t)
{
    if (auto* p = findPoint (scene, id))
    {
        p->timing = clamped (t);
        return true;
    }
    return false;
}

bool setSound (Scene& scene, const juce::String& id, SoundSettings s)
{
    if (auto* p = findPoint (scene, id))
    {
        const auto c = clamped (s);
        if (c != p->sound)
        {
            p->sound = c;
            p->voice.modified = true;
        }
        return true;
    }
    return false;
}

bool setGainDb (Scene& scene, const juce::String& id, double gainDb)
{
    if (auto* p = findPoint (scene, id))
    {
        p->gainDb = std::clamp (gainDb, limits::minGainDb, limits::maxGainDb);
        return true;
    }
    return false;
}

bool setShaper (Scene& scene, const juce::String& id, ShaperSettings sh)
{
    if (auto* p = findPoint (scene, id))
    {
        p->shaper = clamped (sh);
        return true;
    }
    return false;
}

PointEffects clamped (PointEffects e)
{
    e.fadeInMs = std::clamp (e.fadeInMs, 0.0, 2000.0);
    e.fadeOutMs = std::clamp (e.fadeOutMs, 0.0, 2000.0);
    e.lowpassHz = std::clamp (e.lowpassHz, 200.0, 20000.0);
    e.highpassHz = std::clamp (e.highpassHz, 20.0, 5000.0);
    return e;
}

bool setEffects (Scene& scene, const juce::String& id, PointEffects e)
{
    if (auto* p = findPoint (scene, id))
    {
        p->effects = clamped (e);
        return true;
    }
    return false;
}

bool setRegion (Scene& scene, const juce::String& id, PlayRegion r)
{
    if (auto* p = findPoint (scene, id))
    {
        p->region = clamped (r);
        return true;
    }
    return false;
}

void setTimeMode (Scene& scene, TimeMode mode, double bpm)
{
    if (scene.timeMode == mode)
        return;
    const double secondsPerBeat = 60.0 / std::clamp (bpm, 20.0, 999.0);
    for (auto& p : scene.points)
    {
        if (mode == TimeMode::Free)
            p.timing.seconds = p.timing.beat * secondsPerBeat;
        else
            p.timing.beat = p.timing.seconds / secondsPerBeat;
        p.timing = clamped (p.timing);
    }
    scene.timeMode = mode;
}

double momentSeconds (const Scene& scene, const Point& p, double bpm)
{
    return scene.timeMode == TimeMode::Free ? p.timing.seconds
                                            : p.timing.beat * 60.0 / std::clamp (bpm, 20.0, 999.0);
}

bool setStutter (Scene& scene, const juce::String& id, StutterSettings st)
{
    if (auto* p = findPoint (scene, id))
    {
        p->stutter = clamped (st);
        return true;
    }
    return false;
}

double beatsPerDivision (StutterSettings::Division d)
{
    switch (d)
    {
        case StutterSettings::Division::Quarter:      return 1.0;
        case StutterSettings::Division::Eighth:       return 0.5;
        case StutterSettings::Division::Sixteenth:    return 0.25;
        case StutterSettings::Division::ThirtySecond: return 0.125;
    }
    return 0.25;
}

juce::String toString (StutterSettings::Division d)
{
    switch (d)
    {
        case StutterSettings::Division::Quarter:      return "1/4";
        case StutterSettings::Division::Eighth:       return "1/8";
        case StutterSettings::Division::Sixteenth:    return "1/16";
        case StutterSettings::Division::ThirtySecond: return "1/32";
    }
    return "1/16";
}

std::optional<StutterSettings::Division> stutterDivisionFromString (const juce::String& s)
{
    if (s == "1/4")  return StutterSettings::Division::Quarter;
    if (s == "1/8")  return StutterSettings::Division::Eighth;
    if (s == "1/16") return StutterSettings::Division::Sixteenth;
    if (s == "1/32") return StutterSettings::Division::ThirtySecond;
    return std::nullopt;
}

juce::String toString (StutterSettings::Region r)
{
    switch (r)
    {
        case StutterSettings::Region::Whole:  return "whole";
        case StutterSettings::Region::Start:  return "start";
        case StutterSettings::Region::End:    return "end";
        case StutterSettings::Region::Manual: return "manual";
    }
    return "start";
}

std::optional<StutterSettings::Region> stutterRegionFromString (const juce::String& s)
{
    if (s == "whole")  return StutterSettings::Region::Whole;
    if (s == "start")  return StutterSettings::Region::Start;
    if (s == "end")    return StutterSettings::Region::End;
    if (s == "manual") return StutterSettings::Region::Manual;
    return std::nullopt;
}

StutterSettings clamped (StutterSettings s)
{
    s.manualStart = std::clamp (s.manualStart, 0.0, 1.0);
    s.manualEnd = std::clamp (s.manualEnd, s.manualStart, 1.0);
    s.repeats = std::clamp (s.repeats, limits::minStutterRepeats, limits::maxStutterRepeats);
    s.sliceLengthPercent = std::clamp (s.sliceLengthPercent, 10.0, 100.0);
    s.offsetBeats = std::clamp (s.offsetBeats, 0.0, limits::maxBeat);
    s.gainDb = std::clamp (s.gainDb, limits::minGainDb, limits::maxGainDb);
    return s;
}

int applyVoicePreset (Scene& scene, const std::vector<juce::String>& pointIds, const VoicePreset& preset)
{
    int changed = 0;
    for (auto& id : pointIds)
    {
        if (auto* p = findPoint (scene, id))
        {
            p->sound = clamped (preset.sound);
            p->voice = { preset.id, preset.name, false };
            ++changed;
        }
    }
    return changed;
}

} // namespace ses
