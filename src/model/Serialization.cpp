#include "model/Serialization.h"
#include "model/UserPresets.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace ses
{
namespace
{
using juce::var;

var obj() { return var (new juce::DynamicObject()); }

void set (var& o, const char* key, const var& value)
{
    o.getDynamicObject()->setProperty (key, value);
}

juce::String storageToString (SourceRef::Storage s)
{
    switch (s)
    {
        case SourceRef::Storage::None:      return "none";
        case SourceRef::Storage::Reference: return "reference";
        case SourceRef::Storage::Embedded:  return "embedded";
    }
    return "none";
}

std::optional<SourceRef::Storage> storageFromString (const juce::String& s)
{
    if (s == "none")      return SourceRef::Storage::None;
    if (s == "reference") return SourceRef::Storage::Reference;
    if (s == "embedded")  return SourceRef::Storage::Embedded;
    return std::nullopt;
}

// ---- writing --------------------------------------------------------------

var soundToVar (const SoundSettings& s)
{
    auto o = obj();
    set (o, "pitchSemitones", s.pitchSemitones);
    set (o, "character", s.character);
    set (o, "speedPercent", s.speedPercent);
    set (o, "formantSemitones", s.formantSemitones);
    return o;
}

var pointToVar (const Point& p)
{
    auto o = obj();
    set (o, "id", p.id);
    set (o, "number", p.number);
    set (o, "enabled", p.enabled);
    set (o, "solo", p.solo);

    auto pos = obj();
    set (pos, "x", p.position.x);
    set (pos, "y", p.position.y);
    set (o, "position", pos);

    auto timing = obj();
    set (timing, "beat", p.timing.beat);
    set (timing, "seconds", p.timing.seconds);
    set (o, "timing", timing);

    auto region = obj();
    set (region, "start", p.region.start);
    set (region, "end", p.region.end);
    set (o, "region", region);

    auto voice = obj();
    set (voice, "presetId", p.voice.presetId);
    set (voice, "presetName", p.voice.presetName);
    set (voice, "modified", p.voice.modified);
    set (o, "voice", voice);
    if (p.creativePreset.isNotEmpty())
        set (o, "creativePreset", p.creativePreset);

    set (o, "sound", soundToVar (p.sound));
    set (o, "gainDb", p.gainDb);

    auto st = obj();
    set (st, "enabled", p.stutter.enabled);
    set (st, "region", toString (p.stutter.region));
    set (st, "manualStart", p.stutter.manualStart);
    set (st, "manualEnd", p.stutter.manualEnd);
    set (st, "repeats", p.stutter.repeats);
    set (st, "division", toString (p.stutter.division));
    set (st, "sliceLengthPercent", p.stutter.sliceLengthPercent);
    set (st, "offsetBeats", p.stutter.offsetBeats);
    set (st, "gainDb", p.stutter.gainDb);
    set (st, "accelPercent", p.stutter.accelPercent);
    set (o, "stutter", st);

    auto sh = obj();
    set (sh, "enabled", p.shaper.enabled);
    set (sh, "shape", toString (p.shaper.shape));
    set (sh, "rateMode", toString (p.shaper.rateMode));
    set (sh, "cuts", p.shaper.cuts);
    set (sh, "periodMs", p.shaper.periodMs);
    set (sh, "division", toString (p.shaper.division));
    set (sh, "depthPercent", p.shaper.depthPercent);
    set (sh, "openPercent", p.shaper.openPercent);
    set (sh, "attackMs", p.shaper.attackMs);
    set (sh, "releaseMs", p.shaper.releaseMs);
    set (sh, "distribution", toString (p.shaper.distribution));
    set (sh, "accelPercent", p.shaper.accelPercent);
    set (sh, "pattern", p.shaper.pattern);
    set (sh, "wholeVoice", p.shaper.wholeVoice);
    set (sh, "regionStart", p.shaper.regionStart);
    set (sh, "regionEnd", p.shaper.regionEnd);
    set (sh, "lengthMs", p.shaper.lengthMs);
    set (sh, "makeupDb", p.shaper.makeupDb);
    set (sh, "autoLevel", p.shaper.autoLevel);
    set (sh, "custom", p.shaper.custom);
    if (! p.shaper.curve.empty())
    {
        juce::Array<var> curve;
        for (auto& pt : p.shaper.curve)
        {
            juce::Array<var> xy { pt.t, pt.gain };
            curve.add (var (xy));
        }
        set (sh, "curve", var (curve));
    }
    set (o, "shaper", sh);

    auto fx = obj();
    set (fx, "fadeInMs", p.effects.fadeInMs);
    set (fx, "fadeOutMs", p.effects.fadeOutMs);
    set (fx, "reverse", p.effects.reverse);
    set (fx, "lowpassHz", p.effects.lowpassHz);
    set (fx, "highpassHz", p.effects.highpassHz);
    {
        const auto& c = p.effects.character;
        auto x = obj();
        set (x, "enabled", c.enabled);
        set (x, "type", toString (c.type));
        set (x, "amountPercent", c.amountPercent);
        set (x, "mixPercent", c.mixPercent);
        set (fx, "character", x);
    }
    {
        const auto& m = p.effects.modulation;
        auto x = obj();
        set (x, "enabled", m.enabled);
        set (x, "type", toString (m.type));
        set (x, "rateHz", m.rateHz);
        set (x, "depthPercent", m.depthPercent);
        set (x, "mixPercent", m.mixPercent);
        set (fx, "modulation", x);
    }
    {
        const auto& d = p.effects.delay;
        auto x = obj();
        set (x, "enabled", d.enabled);
        set (x, "sync", d.sync);
        set (x, "timeMs", d.timeMs);
        set (x, "division", toString (d.division));
        set (x, "feedbackPercent", d.feedbackPercent);
        set (x, "pingPong", d.pingPong);
        set (x, "lowpassHz", d.lowpassHz);
        set (x, "mixPercent", d.mixPercent);
        set (fx, "delay", x);
    }
    {
        const auto& r = p.effects.reverb;
        auto x = obj();
        set (x, "enabled", r.enabled);
        set (x, "type", toString (r.type));
        set (x, "sizePercent", r.sizePercent);
        set (x, "decayPercent", r.decayPercent);
        set (x, "predelayMs", r.predelayMs);
        set (x, "mixPercent", r.mixPercent);
        set (x, "outputDb", r.outputDb);
        set (fx, "reverb", x);
    }
    set (fx, "chainOrder", p.effects.chainOrder);
    set (o, "effects", fx);
    return o;
}

var sourceToVar (const SourceRef& s)
{
    auto o = obj();
    set (o, "storage", storageToString (s.storage));
    set (o, "filePath", s.filePath);
    set (o, "contentHash", s.contentHash);
    set (o, "encoding", s.encoding);
    set (o, "sampleRate", s.sampleRate);
    set (o, "numChannels", s.numChannels);
    set (o, "numSamples", s.numSamples);
    set (o, "trimStart", s.trimStart);
    set (o, "trimEnd", s.trimEnd);
    set (o, "embeddedData", s.embeddedData.isEmpty() ? juce::String()
                                                     : juce::Base64::toBase64 (s.embeddedData.getData(), s.embeddedData.getSize()));
    return o;
}

var sceneToVar (const Scene& s)
{
    auto o = obj();
    set (o, "id", s.id);
    set (o, "name", s.name);
    set (o, "rhythm", toString (s.rhythm));
    set (o, "timeMode", toString (s.timeMode));
    set (o, "mixPercent", s.mixPercent);
    set (o, "spatialMode", toString (s.spatialMode));
    set (o, "maxPoints", s.maxPoints);
    set (o, "source", sourceToVar (s.source));

    juce::Array<var> points;
    for (auto& p : s.points)
        points.add (pointToVar (p));
    set (o, "points", points);

    auto live = obj();
    set (live, "enabled", s.live.enabled);
    set (live, "wetPercent", s.live.wetPercent);
    set (live, "cycleBeats", s.live.cycleBeats);
    set (live, "point", pointToVar (s.live.fx));
    set (o, "live", live);
    return o;
}

// ---- reading (tolerant: repair and warn, fail only on unusable input) -----

struct Reader
{
    juce::StringArray& warnings;

    double number (const var& o, const char* key, double fallback, double lo, double hi, const juce::String& where)
    {
        const auto v = o.getProperty (key, var());
        if (v.isVoid())
            return fallback;
        if (! (v.isDouble() || v.isInt() || v.isInt64()))
        {
            warnings.add (where + "." + key + ": not a number, using default");
            return fallback;
        }
        const auto d = (double) v;
        if (! std::isfinite (d))
        {
            warnings.add (where + "." + key + ": not finite, using default");
            return fallback;
        }
        if (d < lo || d > hi)
        {
            warnings.add (where + "." + key + ": out of range, clamped");
            return std::clamp (d, lo, hi);
        }
        return d;
    }

    juce::int64 integer (const var& o, const char* key, juce::int64 fallback, juce::int64 lo, juce::int64 hi, const juce::String& where)
    {
        const auto v = o.getProperty (key, var());
        if (v.isVoid())
            return fallback;
        if (! (v.isDouble() || v.isInt() || v.isInt64()))
        {
            warnings.add (where + "." + key + ": not a number, using default");
            return fallback;
        }
        const auto i = (juce::int64) v;
        if (i < lo || i > hi)
        {
            warnings.add (where + "." + key + ": out of range, clamped");
            return std::clamp (i, lo, hi);
        }
        return i;
    }

    static juce::String text (const var& o, const char* key, const juce::String& fallback = {})
    {
        const auto v = o.getProperty (key, var());
        return v.isString() ? v.toString() : fallback;
    }

    static bool flag (const var& o, const char* key, bool fallback)
    {
        const auto v = o.getProperty (key, var());
        return v.isBool() ? (bool) v : fallback;
    }

    SoundSettings sound (const var& o, const juce::String& where)
    {
        SoundSettings s;
        s.pitchSemitones = number (o, "pitchSemitones", 0.0, limits::minPitch, limits::maxPitch, where);
        s.character = number (o, "character", 0.0, limits::minCharacter, limits::maxCharacter, where);
        s.speedPercent = number (o, "speedPercent", 100.0, limits::minSpeed, limits::maxSpeed, where);
        s.formantSemitones = number (o, "formantSemitones", 0.0, limits::minFormant, limits::maxFormant, where);
        return s;
    }

    Point point (const var& o, const juce::String& where)
    {
        Point p;
        p.id = text (o, "id");
        p.number = (int) integer (o, "number", 0, 0, 9999, where);
        p.enabled = flag (o, "enabled", true);
        p.solo = flag (o, "solo", false);  // added after v1 shipped internally; optional

        const auto pos = o.getProperty ("position", var());
        p.position.x = number (pos, "x", 50.0, limits::minCoord, limits::maxCoord, where + ".position");
        p.position.y = number (pos, "y", 50.0, limits::minCoord, limits::maxCoord, where + ".position");

        const auto timing = o.getProperty ("timing", var());
        p.timing.beat = number (timing, "beat", 0.0, limits::minBeat, limits::maxBeat, where + ".timing");
        p.timing.seconds = number (timing, "seconds", p.timing.beat * 0.5, limits::minSeconds, limits::maxSeconds, where + ".timing");

        const auto region = o.getProperty ("region", var());
        if (region.isObject())
        {
            p.region.start = number (region, "start", 0.0, 0.0, 1.0, where + ".region");
            p.region.end = number (region, "end", 1.0, 0.0, 1.0, where + ".region");
            p.region = clamped (p.region);
        }

        const auto voice = o.getProperty ("voice", var());
        p.voice.presetId = text (voice, "presetId", presetIds::original);
        p.voice.presetName = text (voice, "presetName", "Original");
        p.voice.modified = flag (voice, "modified", false);
        p.creativePreset = text (o, "creativePreset", {});

        p.sound = sound (o.getProperty ("sound", var()), where + ".sound");
        if (o.hasProperty ("gainDb"))
            p.gainDb = number (o, "gainDb", 0.0, limits::minGainDb, limits::maxGainDb, where);
        else
        {
            // Scenes written before B4 stored a 0..100 % volume.
            const double pct = number (o, "volumePercent", 100.0, 0.0, 100.0, where);
            p.gainDb = pct <= 0.0 ? limits::minGainDb
                                  : std::clamp (20.0 * std::log10 (pct / 100.0), limits::minGainDb, limits::maxGainDb);
        }

        const auto st = o.getProperty ("stutter", var());
        if (st.isObject())
        {
            StutterSettings x;
            x.enabled = flag (st, "enabled", false);
            if (auto r = stutterRegionFromString (text (st, "region", "start")))
                x.region = *r;
            else
                warnings.add (where + ".stutter.region: unknown, using start");
            x.manualStart = number (st, "manualStart", 0.0, 0.0, 1.0, where + ".stutter");
            x.manualEnd = number (st, "manualEnd", 1.0, 0.0, 1.0, where + ".stutter");
            x.repeats = (int) integer (st, "repeats", 8, limits::minStutterRepeats, limits::maxStutterRepeats, where + ".stutter");
            if (auto d = stutterDivisionFromString (text (st, "division", "1/16")))
                x.division = *d;
            else
                warnings.add (where + ".stutter.division: unknown, using 1/16");
            x.sliceLengthPercent = number (st, "sliceLengthPercent", 100.0, 10.0, 100.0, where + ".stutter");
            x.offsetBeats = number (st, "offsetBeats", 0.0, 0.0, limits::maxBeat, where + ".stutter");
            x.accelPercent = number (st, "accelPercent", 0.0, -100.0, 100.0, where + ".stutter");
            x.gainDb = number (st, "gainDb", 0.0, limits::minGainDb, limits::maxGainDb, where + ".stutter");
            p.stutter = clamped (x);
        }

        const auto sh = o.getProperty ("shaper", var());
        if (sh.isObject())
        {
            ShaperSettings x;
            x.enabled = flag (sh, "enabled", false);
            if (auto v = shaperShapeFromString (text (sh, "shape", "square")))
                x.shape = *v;
            else
                warnings.add (where + ".shaper.shape: unknown, using square");
            if (auto v = shaperRateModeFromString (text (sh, "rateMode", "count")))
                x.rateMode = *v;
            else
                warnings.add (where + ".shaper.rateMode: unknown, using count");
            x.cuts = (int) integer (sh, "cuts", 8, limits::minShaperCuts, limits::maxShaperCuts, where + ".shaper");
            x.periodMs = number (sh, "periodMs", 125.0, limits::minShaperPeriodMs, limits::maxShaperPeriodMs, where + ".shaper");
            if (auto d = stutterDivisionFromString (text (sh, "division", "1/16")))
                x.division = *d;
            else
                warnings.add (where + ".shaper.division: unknown, using 1/16");
            x.depthPercent = number (sh, "depthPercent", 100.0, 0.0, 100.0, where + ".shaper");
            x.openPercent = number (sh, "openPercent", 50.0, 5.0, 95.0, where + ".shaper");
            if (sh.hasProperty ("attackMs") || sh.hasProperty ("releaseMs"))
            {
                x.attackMs = number (sh, "attackMs", 1.0, limits::minShaperRampMs, limits::maxShaperAttackMs, where + ".shaper");
                x.releaseMs = number (sh, "releaseMs", 3.0, limits::minShaperRampMs, limits::maxShaperReleaseMs, where + ".shaper");
            }
            else
            {
                // Earlier scenes had one smoothing time: use it for both ramps.
                const double fade = number (sh, "fadeMs", 3.0, 0.0, 50.0, where + ".shaper");
                x.attackMs = x.releaseMs = std::max (limits::minShaperRampMs, fade);
            }
            if (auto d = shaperDistributionFromString (text (sh, "distribution", "uniform")))
                x.distribution = *d;
            else
                warnings.add (where + ".shaper.distribution: unknown, using uniform");
            x.accelPercent = number (sh, "accelPercent", 0.0, 0.0, 100.0, where + ".shaper");
            if (! sh.hasProperty ("distribution") && x.accelPercent > 0.0)
                x.distribution = ShaperSettings::Distribution::Accelerating;  // earlier meaning of accelPercent
            x.pattern = text (sh, "pattern");
            x.wholeVoice = flag (sh, "wholeVoice", true);
            x.regionStart = number (sh, "regionStart", 0.0, 0.0, 1.0, where + ".shaper");
            x.regionEnd = number (sh, "regionEnd", 1.0, 0.0, 1.0, where + ".shaper");
            x.lengthMs = number (sh, "lengthMs", 0.0, 0.0, 60000.0, where + ".shaper");
            x.makeupDb = number (sh, "makeupDb", 0.0, -limits::maxShaperMakeupDb, limits::maxShaperMakeupDb, where + ".shaper");
            x.autoLevel = flag (sh, "autoLevel", false);
            x.custom = flag (sh, "custom", false);
            if (const auto* curve = sh.getProperty ("curve", var()).getArray())
            {
                for (const auto& pt : *curve)
                {
                    const auto* xy = pt.getArray();
                    if (xy == nullptr || xy->size() != 2 || ! (*xy)[0].isDouble() && ! (*xy)[0].isInt() || ! (*xy)[1].isDouble() && ! (*xy)[1].isInt())
                    {
                        warnings.add (where + ".shaper.curve: bad point ignored");
                        continue;
                    }
                    x.curve.push_back ({ (double) (*xy)[0], (double) (*xy)[1] });
                }
            }
            else if (x.custom)
            {
                warnings.add (where + ".shaper.curve: missing, using the generated shape");
                x.custom = false;
            }
            p.shaper = clamped (x);
        }

        const auto fx = o.getProperty ("effects", var());
        if (fx.isObject())
        {
            PointEffects e;
            e.fadeInMs = number (fx, "fadeInMs", 0.0, 0.0, 2000.0, where + ".effects");
            e.fadeOutMs = number (fx, "fadeOutMs", 0.0, 0.0, 2000.0, where + ".effects");
            e.reverse = flag (fx, "reverse", false);
            e.lowpassHz = number (fx, "lowpassHz", 20000.0, 200.0, 20000.0, where + ".effects");
            e.highpassHz = number (fx, "highpassHz", 20.0, 20.0, 5000.0, where + ".effects");
            e.chainOrder = normalisedChainOrder (text (fx, "chainOrder", "cmdr"));
            if (const auto c = fx.getProperty ("character", var()); c.isObject())
            {
                e.character.enabled = flag (c, "enabled", false);
                if (auto t = characterTypeFromString (text (c, "type", "saturation")))
                    e.character.type = *t;
                else
                    warnings.add (where + ".effects.character.type: unknown, using saturation");
                e.character.amountPercent = number (c, "amountPercent", 50.0, 0.0, 100.0, where + ".effects.character");
                e.character.mixPercent = number (c, "mixPercent", 100.0, 0.0, 100.0, where + ".effects.character");
            }
            if (const auto m = fx.getProperty ("modulation", var()); m.isObject())
            {
                e.modulation.enabled = flag (m, "enabled", false);
                if (auto t = modulationTypeFromString (text (m, "type", "chorus")))
                    e.modulation.type = *t;
                else
                    warnings.add (where + ".effects.modulation.type: unknown, using chorus");
                e.modulation.rateHz = number (m, "rateHz", 1.0, 0.05, 20.0, where + ".effects.modulation");
                e.modulation.depthPercent = number (m, "depthPercent", 50.0, 0.0, 100.0, where + ".effects.modulation");
                e.modulation.mixPercent = number (m, "mixPercent", 50.0, 0.0, 100.0, where + ".effects.modulation");
            }
            if (const auto d = fx.getProperty ("delay", var()); d.isObject())
            {
                e.delay.enabled = flag (d, "enabled", false);
                e.delay.sync = flag (d, "sync", false);
                e.delay.timeMs = number (d, "timeMs", 250.0, 1.0, limits::maxDelayMs, where + ".effects.delay");
                if (auto t = stutterDivisionFromString (text (d, "division", "1/8")))
                    e.delay.division = *t;
                else
                    warnings.add (where + ".effects.delay.division: unknown, using 1/8");
                e.delay.feedbackPercent = number (d, "feedbackPercent", 35.0, 0.0, 95.0, where + ".effects.delay");
                e.delay.pingPong = flag (d, "pingPong", false);
                e.delay.lowpassHz = number (d, "lowpassHz", 8000.0, 500.0, 20000.0, where + ".effects.delay");
                e.delay.mixPercent = number (d, "mixPercent", 30.0, 0.0, 100.0, where + ".effects.delay");
            }
            if (const auto r = fx.getProperty ("reverb", var()); r.isObject())
            {
                e.reverb.enabled = flag (r, "enabled", false);
                if (auto t = reverbTypeFromString (text (r, "type", "room")))
                    e.reverb.type = *t;
                else
                    warnings.add (where + ".effects.reverb.type: unknown, using room");
                e.reverb.sizePercent = number (r, "sizePercent", 50.0, 0.0, 100.0, where + ".effects.reverb");
                e.reverb.decayPercent = number (r, "decayPercent", 50.0, 0.0, 100.0, where + ".effects.reverb");
                e.reverb.predelayMs = number (r, "predelayMs", 10.0, 0.0, limits::maxPredelayMs, where + ".effects.reverb");
                e.reverb.mixPercent = number (r, "mixPercent", 25.0, 0.0, 100.0, where + ".effects.reverb");
                e.reverb.outputDb = number (r, "outputDb", 0.0, -24.0, 6.0, where + ".effects.reverb");
            }
            p.effects = clamped (e);
        }
        return p;
    }

    SourceRef source (const var& o, const juce::String& where)
    {
        SourceRef s;
        if (! o.isObject())
            return s;

        if (auto st = storageFromString (text (o, "storage", "none")))
            s.storage = *st;
        else
            warnings.add (where + ".storage: unknown value, using none");

        s.filePath = text (o, "filePath");
        s.contentHash = text (o, "contentHash");
        s.encoding = text (o, "encoding");
        s.sampleRate = number (o, "sampleRate", 0.0, 0.0, 1.0e6, where);
        s.numChannels = (int) integer (o, "numChannels", 0, 0, 64, where);
        s.numSamples = integer (o, "numSamples", 0, 0, std::numeric_limits<juce::int64>::max(), where);
        s.trimStart = integer (o, "trimStart", 0, 0, std::numeric_limits<juce::int64>::max(), where);
        s.trimEnd = integer (o, "trimEnd", 0, 0, std::numeric_limits<juce::int64>::max(), where);

        const auto b64 = text (o, "embeddedData");
        if (b64.isNotEmpty())
        {
            juce::MemoryOutputStream out;
            if (juce::Base64::convertFromBase64 (out, b64))
                s.embeddedData = out.getMemoryBlock();
            else
                warnings.add (where + ".embeddedData: invalid base64, dropped");
        }

        if (s.storage == SourceRef::Storage::Embedded && s.embeddedData.isEmpty())
        {
            warnings.add (where + ": embedded storage without data, treated as reference");
            s.storage = s.filePath.isNotEmpty() ? SourceRef::Storage::Reference : SourceRef::Storage::None;
        }
        return s;
    }

    Scene scene (const var& o, const juce::String& where)
    {
        Scene s;
        s.id = text (o, "id");
        s.name = text (o, "name");

        if (auto r = rhythmFromString (text (o, "rhythm", "1/4")))
            s.rhythm = *r;
        else
            warnings.add (where + ".rhythm: unknown value, using 1/4");

        if (auto m = timeModeFromString (text (o, "timeMode", "beats")))
            s.timeMode = *m;
        else
            warnings.add (where + ".timeMode: unknown value, using beats");

        s.mixPercent = number (o, "mixPercent", 70.0, limits::minMix, limits::maxMix, where);

        if (auto m = spatialModeFromString (text (o, "spatialMode", "stereo")))
            s.spatialMode = *m;
        else
            warnings.add (where + ".spatialMode: not available in this version, using stereo");

        s.maxPoints = (int) integer (o, "maxPoints", kDefaultMaxPoints, limits::minMaxPoints, limits::maxMaxPoints, where);
        s.source = source (o.getProperty ("source", var()), where + ".source");

        if (const auto* arr = o.getProperty ("points", var()).getArray())
        {
            for (int i = 0; i < arr->size(); ++i)
            {
                if ((int) s.points.size() >= s.maxPoints)
                {
                    warnings.add (where + ".points: more than maxPoints, extra points dropped");
                    break;
                }
                if (arr->getReference (i).isObject())
                    s.points.push_back (point (arr->getReference (i), where + ".points[" + juce::String (i) + "]"));
            }
        }
        // Optional (B9): scenes saved before have no live block and load with it off.
        if (const auto live = o.getProperty ("live", var()); live.isObject())
        {
            s.live.enabled = flag (live, "enabled", false);
            s.live.wetPercent = number (live, "wetPercent", 100.0, 0.0, 100.0, where + ".live");
            s.live.cycleBeats = number (live, "cycleBeats", 4.0, 0.25, 16.0, where + ".live");
            if (const auto lp = live.getProperty ("point", var()); lp.isObject())
                s.live.fx = point (lp, where + ".live.point");
            s.live = clamped (s.live);
        }
        return s;
    }
};

// Ids must be non-empty and unique; numbers must be positive and unique.
void repairIdentity (Scene& scene, juce::StringArray& warnings)
{
    std::set<juce::String> seenIds;
    std::set<int> seenNumbers;

    for (auto& p : scene.points)
    {
        if (p.id.isEmpty() || seenIds.count (p.id) > 0 || p.id == kLivePointId)
        {
            warnings.add ("scene " + scene.id + ": point with missing/duplicate id got a new id");
            p.id = makeId();
        }
        seenIds.insert (p.id);
    }

    std::vector<Point*> needNumber;
    for (auto& p : scene.points)
        if (p.number <= 0 || ! seenNumbers.insert (p.number).second)
            needNumber.push_back (&p);

    for (auto* p : needNumber)
    {
        int n = 1;
        while (seenNumbers.count (n) > 0)
            ++n;
        p->number = n;
        seenNumbers.insert (n);
        warnings.add ("scene " + scene.id + ": point with missing/duplicate number renumbered");
    }
}

LoadResult<var> parseAndCheckHeader (const juce::String& json, const char* formatName, int currentVersion)
{
    LoadResult<var> r;
    var root;
    const auto parsed = juce::JSON::parse (json, root);
    if (parsed.failed() || ! root.isObject())
    {
        r.error = "invalid JSON: " + parsed.getErrorMessage();
        return r;
    }
    if (root.getProperty ("format", var()).toString() != formatName)
    {
        r.error = "not a " + juce::String (formatName) + " file";
        return r;
    }
    const auto version = root.getProperty ("formatVersion", var());
    if (! (version.isInt() || version.isInt64() || version.isDouble()) || (int) version < 1)
    {
        r.error = "missing or invalid formatVersion";
        return r;
    }
    if ((int) version > currentVersion)
    {
        r.error = "file was written by a newer version (formatVersion " + version.toString()
                  + ", supported " + juce::String (currentVersion) + ")";
        return r;
    }
    // Older versions would be migrated here. Version 1 is the first format.
    r.value = root;
    return r;
}

} // namespace

juce::String toJson (const Document& doc)
{
    auto root = obj();
    set (root, "format", kDocumentFormatName);
    set (root, "formatVersion", kDocumentFormatVersion);
    set (root, "activeSceneId", doc.activeSceneId);

    juce::Array<var> scenes;
    for (auto& s : doc.scenes)
        scenes.add (sceneToVar (s));
    set (root, "scenes", scenes);

    return juce::JSON::toString (root, juce::JSON::FormatOptions{}.withSpacing (juce::JSON::Spacing::multiLine)
                                                                  .withMaxDecimalPlaces (17));
}

LoadResult<Document> documentFromJson (const juce::String& json)
{
    LoadResult<Document> result;
    auto header = parseAndCheckHeader (json, kDocumentFormatName, kDocumentFormatVersion);
    if (! header)
    {
        result.error = header.error;
        return result;
    }

    const auto& root = *header.value;
    Reader reader { result.warnings };
    Document doc;

    if (const auto* arr = root.getProperty ("scenes", var()).getArray())
        for (int i = 0; i < arr->size(); ++i)
            if (arr->getReference (i).isObject())
                doc.scenes.push_back (reader.scene (arr->getReference (i), "scenes[" + juce::String (i) + "]"));

    if (doc.scenes.empty())
    {
        result.error = "document has no scenes";
        return result;
    }

    std::set<juce::String> sceneIds;
    for (auto& s : doc.scenes)
    {
        if (s.id.isEmpty() || sceneIds.count (s.id) > 0)
        {
            result.warnings.add ("scene with missing/duplicate id got a new id");
            s.id = makeId();
        }
        sceneIds.insert (s.id);
        repairIdentity (s, result.warnings);
    }

    doc.activeSceneId = Reader::text (root, "activeSceneId");
    if (sceneIds.count (doc.activeSceneId) == 0)
    {
        if (doc.activeSceneId.isNotEmpty())
            result.warnings.add ("activeSceneId not found, using first scene");
        doc.activeSceneId = doc.scenes.front().id;
    }

    result.value = std::move (doc);
    return result;
}

juce::String voicePresetsToJson (const std::vector<VoicePreset>& userPresets)
{
    auto root = obj();
    set (root, "format", kVoicePresetsFormatName);
    set (root, "formatVersion", kVoicePresetsFormatVersion);

    juce::Array<var> arr;
    for (auto& p : userPresets)
    {
        if (p.builtIn)
            continue;
        auto o = obj();
        set (o, "id", p.id);
        set (o, "name", p.name);
        set (o, "description", p.description);
        set (o, "sound", soundToVar (p.sound));
        set (o, "needsFutureProcessing", p.needsFutureProcessing);
        arr.add (o);
    }
    set (root, "presets", arr);

    return juce::JSON::toString (root, juce::JSON::FormatOptions{}.withSpacing (juce::JSON::Spacing::multiLine)
                                                                  .withMaxDecimalPlaces (17));
}

LoadResult<std::vector<VoicePreset>> voicePresetsFromJson (const juce::String& json)
{
    LoadResult<std::vector<VoicePreset>> result;
    auto header = parseAndCheckHeader (json, kVoicePresetsFormatName, kVoicePresetsFormatVersion);
    if (! header)
    {
        result.error = header.error;
        return result;
    }

    Reader reader { result.warnings };
    std::vector<VoicePreset> presets;
    std::set<juce::String> ids;

    if (const auto* arr = header.value->getProperty ("presets", var()).getArray())
    {
        for (int i = 0; i < arr->size(); ++i)
        {
            const auto& o = arr->getReference (i);
            if (! o.isObject())
                continue;

            VoicePreset p;
            p.id = Reader::text (o, "id");
            p.name = Reader::text (o, "name");
            p.description = Reader::text (o, "description");
            p.sound = reader.sound (o.getProperty ("sound", var()), "presets[" + juce::String (i) + "].sound");
            p.needsFutureProcessing = Reader::flag (o, "needsFutureProcessing", false);
            p.builtIn = false;

            if (p.id.isEmpty() || p.id.startsWith ("builtin.") || ids.count (p.id) > 0)
            {
                result.warnings.add ("preset with missing/reserved/duplicate id got a new id");
                p.id = makeId();
            }
            ids.insert (p.id);
            presets.push_back (std::move (p));
        }
    }

    result.value = std::move (presets);
    return result;
}

juce::String userPresetsToJson (const UserPresetFile& f)
{
    auto root = obj();
    set (root, "format", "spatial-echo-user-presets");
    set (root, "formatVersion", 1);
    juce::Array<var> arr;
    for (auto& p : f.presets)
    {
        auto o = obj();
        set (o, "id", p.id);
        set (o, "name", p.name);
        set (o, "description", p.description);
        set (o, "point", pointToVar (p.content));
        arr.add (o);
    }
    set (root, "presets", arr);
    juce::Array<var> fav;
    for (auto& id : f.favourites)
        fav.add (id);
    set (root, "favourites", fav);
    return juce::JSON::toString (root, juce::JSON::FormatOptions{}.withSpacing (juce::JSON::Spacing::multiLine).withMaxDecimalPlaces (17));
}

LoadResult<UserPresetFile> userPresetsFromJson (const juce::String& json)
{
    LoadResult<UserPresetFile> result;
    auto header = parseAndCheckHeader (json, "spatial-echo-user-presets", 1);
    if (! header)
    {
        result.error = header.error;
        return result;
    }
    Reader reader { result.warnings };
    UserPresetFile f;
    std::set<juce::String> ids;
    if (const auto* arr = header.value->getProperty ("presets", var()).getArray())
    {
        int i = 0;
        for (auto& v : *arr)
        {
            const auto where = "presets[" + juce::String (i++) + "]";
            if (! v.isObject())
            {
                result.warnings.add (where + ": not an object, skipped");
                continue;
            }
            UserPointPreset p;
            p.id = Reader::text (v, "id");
            if (p.id.isEmpty() || ids.count (p.id) > 0)
            {
                p.id = "user." + makeId();
                result.warnings.add (where + ": missing or duplicate id, regenerated");
            }
            ids.insert (p.id);
            p.name = Reader::text (v, "name", "Preset");
            p.description = Reader::text (v, "description");
            const auto pt = v.getProperty ("point", var());
            if (pt.isObject())
                p.content = reader.point (pt, where + ".point");
            f.presets.push_back (std::move (p));
        }
    }
    if (const auto* fav = header.value->getProperty ("favourites", var()).getArray())
        for (auto& v : *fav)
            if (v.isString())
                f.favourites.addIfNotAlreadyThere (v.toString());
    result.value = std::move (f);
    return result;
}

} // namespace ses
