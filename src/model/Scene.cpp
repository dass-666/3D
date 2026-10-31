#include "model/Scene.h"
#include "model/VoicePresets.h"

#include <algorithm>

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
    return t;
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
    double latest = 0.0;
    for (auto& p : scene.points)
        latest = std::max (latest, p.timing.beat);
    return clamped (Timing { latest + step });
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
    copy.timing = clamped (Timing { src->timing.beat + beatsPerStep (scene.rhythm) });
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
