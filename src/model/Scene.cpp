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
    s.curve = normalisedCurve (std::move (s.curve));
    if ((int) s.curve.size() < limits::minShaperCurvePoints)
        s.custom = false;
    return s;
}

std::vector<CurvePoint> normalisedCurve (std::vector<CurvePoint> c)
{
    for (auto& pt : c)
    {
        pt.t = std::isfinite (pt.t) ? std::clamp (pt.t, 0.0, 1.0) : 0.0;
        pt.gain = std::isfinite (pt.gain) ? std::clamp (pt.gain, 0.0, 1.0) : 1.0;
    }
    std::stable_sort (c.begin(), c.end(), [] (const CurvePoint& a, const CurvePoint& b) { return a.t < b.t; });
    if ((int) c.size() > limits::maxShaperCurvePoints)
        c.resize ((size_t) limits::maxShaperCurvePoints);
    return c;
}

double curveGainAt (const std::vector<CurvePoint>& c, double t) noexcept
{
    if (c.size() < 2)
        return 1.0;
    if (t <= c.front().t)
        return c.front().gain;
    if (t >= c.back().t)
        return c.back().gain;
    // First point with pt.t > t (sorted), then interpolate from its predecessor.
    const auto it = std::upper_bound (c.begin(), c.end(), t, [] (double v, const CurvePoint& pt) { return v < pt.t; });
    const auto& b = *it;
    const auto& a = *(it - 1);
    const double w = b.t > a.t ? (t - a.t) / (b.t - a.t) : 1.0;
    return a.gain + (b.gain - a.gain) * w;
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
    s.formantSemitones = std::clamp (s.formantSemitones, limits::minFormant, limits::maxFormant);
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
    e.character.amountPercent = std::clamp (e.character.amountPercent, 0.0, 100.0);
    e.character.mixPercent = std::clamp (e.character.mixPercent, 0.0, 100.0);
    e.modulation.rateHz = std::clamp (e.modulation.rateHz, 0.05, 20.0);
    e.modulation.depthPercent = std::clamp (e.modulation.depthPercent, 0.0, 100.0);
    e.modulation.mixPercent = std::clamp (e.modulation.mixPercent, 0.0, 100.0);
    e.delay.timeMs = std::clamp (e.delay.timeMs, 1.0, limits::maxDelayMs);
    e.delay.feedbackPercent = std::clamp (e.delay.feedbackPercent, 0.0, 95.0);
    e.delay.lowpassHz = std::clamp (e.delay.lowpassHz, 500.0, 20000.0);
    e.delay.mixPercent = std::clamp (e.delay.mixPercent, 0.0, 100.0);
    e.reverb.sizePercent = std::clamp (e.reverb.sizePercent, 0.0, 100.0);
    e.reverb.decayPercent = std::clamp (e.reverb.decayPercent, 0.0, 100.0);
    e.reverb.predelayMs = std::clamp (e.reverb.predelayMs, 0.0, limits::maxPredelayMs);
    e.reverb.mixPercent = std::clamp (e.reverb.mixPercent, 0.0, 100.0);
    e.reverb.outputDb = std::clamp (e.reverb.outputDb, -24.0, 6.0);
    return e;
}

juce::String toString (CharacterSettings::Type t)
{
    switch (t)
    {
        case CharacterSettings::Type::Saturation: return "saturation";
        case CharacterSettings::Type::Overdrive:  return "overdrive";
        case CharacterSettings::Type::Bitcrusher: return "bitcrusher";
        case CharacterSettings::Type::Telephone:  return "telephone";
        case CharacterSettings::Type::LoFi:       return "lofi";
        case CharacterSettings::Type::Robot:      return "robot";
    }
    return "saturation";
}

std::optional<CharacterSettings::Type> characterTypeFromString (const juce::String& s)
{
    for (auto t : { CharacterSettings::Type::Saturation, CharacterSettings::Type::Overdrive, CharacterSettings::Type::Bitcrusher,
                    CharacterSettings::Type::Telephone, CharacterSettings::Type::LoFi, CharacterSettings::Type::Robot })
        if (toString (t) == s)
            return t;
    return std::nullopt;
}

juce::String toString (ModulationSettings::Type t)
{
    switch (t)
    {
        case ModulationSettings::Type::Chorus:  return "chorus";
        case ModulationSettings::Type::Flanger: return "flanger";
        case ModulationSettings::Type::Tremolo: return "tremolo";
    }
    return "chorus";
}

std::optional<ModulationSettings::Type> modulationTypeFromString (const juce::String& s)
{
    for (auto t : { ModulationSettings::Type::Chorus, ModulationSettings::Type::Flanger, ModulationSettings::Type::Tremolo })
        if (toString (t) == s)
            return t;
    return std::nullopt;
}

juce::String toString (ReverbSettings::Type t)
{
    switch (t)
    {
        case ReverbSettings::Type::Room:  return "room";
        case ReverbSettings::Type::Hall:  return "hall";
        case ReverbSettings::Type::Plate: return "plate";
    }
    return "room";
}

std::optional<ReverbSettings::Type> reverbTypeFromString (const juce::String& s)
{
    for (auto t : { ReverbSettings::Type::Room, ReverbSettings::Type::Hall, ReverbSettings::Type::Plate })
        if (toString (t) == s)
            return t;
    return std::nullopt;
}

const std::vector<PointPreset>& pointPresets()
{
    static const std::vector<PointPreset> presets = []
    {
        std::vector<PointPreset> v;
        auto shaperOf = [] (const char* id)
        {
            for (auto& pr : shaperPresets())
                if (juce::String (pr.id) == id)
                    return pr.settings;
            return ShaperSettings {};
        };
        auto sound = [] (double pitch, double formant) { SoundSettings s; s.pitchSemitones = pitch; s.formantSemitones = formant; return s; };
        PointEffects fx;

        v.push_back ({ "vocal-chop", "Vocal Chop", "Cortes r\xc3\xadtmicos con huecos (patr\xc3\xb3n xx.x).", std::nullopt, shaperOf ("vocal-chop"), std::nullopt });
        v.push_back ({ "fast-gate", "Fast Gate", "24 cortes r\xc3\xa1pidos con nivel autom\xc3\xa1tico.", std::nullopt, shaperOf ("fast-gate"), std::nullopt });
        v.push_back ({ "trance-gate", "Trance Gate", "Cortes a 1/16 del BPM.", std::nullopt, shaperOf ("trance-gate"), std::nullopt });
        v.push_back ({ "machine-gun", "Machine Gun", "64 cortes muy cortos.", std::nullopt, shaperOf ("machine-gun"), std::nullopt });

        fx = {};
        fx.character = { true, CharacterSettings::Type::Bitcrusher, 35.0, 60.0 };
        v.push_back ({ "glitch", "Glitch Vocal", "Cortes irregulares y bitcrusher suave.", std::nullopt, shaperOf ("glitch"), fx });

        fx = {};
        fx.character = { true, CharacterSettings::Type::Robot, 55.0, 85.0 };
        v.push_back ({ "robot", "Robot", "Modulaci\xc3\xb3n en anillo (voz met\xc3\xa1lica), formantes planos.", sound (0.0, -1.0), std::nullopt, fx });

        fx = {};
        fx.modulation = { true, ModulationSettings::Type::Chorus, 0.6, 60.0, 50.0 };
        fx.reverb = { true, ReverbSettings::Type::Hall, 85.0, 80.0, 40.0, 55.0, 0.0 };
        v.push_back ({ "ghost", "Fantasma", "Tono y formantes arriba, chorus y hall largo.", sound (5.0, 3.0), std::nullopt, fx });

        fx = {};
        fx.character = { true, CharacterSettings::Type::Saturation, 45.0, 100.0 };
        fx.lowpassHz = 6000.0;
        v.push_back ({ "monster", "Monster", "Muy grave, formantes abajo y saturaci\xc3\xb3n.", sound (-9.0, -6.0), std::nullopt, fx });

        fx = {};
        fx.character = { true, CharacterSettings::Type::Telephone, 50.0, 100.0 };
        v.push_back ({ "telephone", "Radio / Tel\xc3\xa9" "fono", "Banda 300\xe2\x80\x93" "3400 Hz con algo de distorsi\xc3\xb3n.", std::nullopt, std::nullopt, fx });

        fx = {};
        fx.modulation = { true, ModulationSettings::Type::Chorus, 0.35, 45.0, 60.0 };
        fx.delay = { true, false, 18.0, StutterSettings::Division::Sixteenth, 0.0, true, 12000.0, 40.0 };
        v.push_back ({ "wide", "Wide Vocal", "Chorus lento y doble corto en ping-pong.", std::nullopt, std::nullopt, fx });

        fx = {};
        fx.delay = { true, true, 375.0, StutterSettings::Division::Eighth, 55.0, true, 5000.0, 45.0 };
        fx.reverb = { true, ReverbSettings::Type::Room, 40.0, 35.0, 5.0, 20.0, 0.0 };
        v.push_back ({ "echo-throw", "Echo Throw", "Delay 1/8 ping-pong con cola y sala peque\xc3\xb1" "a.", std::nullopt, std::nullopt, fx });

        v.push_back ({ "fade-stutter", "Fade Stutter", "12 cortes en diente de sierra.", std::nullopt, shaperOf ("fade-stutter"), std::nullopt });

        fx = {};
        fx.reverse = true;
        fx.fadeInMs = 120.0;
        fx.reverb = { true, ReverbSettings::Type::Plate, 60.0, 65.0, 0.0, 35.0, 0.0 };
        v.push_back ({ "reverse-tail", "Reverse Tail", "Al rev\xc3\xa9s con entrada suave y plate.", std::nullopt, std::nullopt, fx });
        return v;
    }();
    return presets;
}

void applyPointPreset (Point& p, const PointPreset& preset)
{
    if (preset.sound)
    {
        p.sound = clamped (*preset.sound);
        p.voice = { juce::String ("creative.") + preset.id, juce::String::fromUTF8 (preset.name), false };
    }
    if (preset.shaper)
        p.shaper = clamped (*preset.shaper);
    if (preset.effects)
        p.effects = clamped (*preset.effects);
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
            // Monster / Robot / Fantasma are also an effect chain (real
            // processing); the other voice presets leave the effects alone.
            const char* creative = preset.id == "builtin.monster" ? "monster"
                                 : preset.id == "builtin.robot" ? "robot"
                                 : preset.id == "builtin.fantasma" ? "ghost" : nullptr;
            if (creative != nullptr)
                for (auto& pp : pointPresets())
                    if (juce::String (pp.id) == creative && pp.effects)
                        p->effects = clamped (*pp.effects);
            ++changed;
        }
    }
    return changed;
}

} // namespace ses
