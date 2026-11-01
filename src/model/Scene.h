#pragma once

// Scene model: pure data + editing operations. No audio, no UI.
// Not thread-safe: edit on one thread and hand immutable copies to others.

#include <juce_core/juce_core.h>

#include <optional>
#include <vector>

namespace ses
{

inline constexpr int kDefaultMaxPoints = 16;

enum class RhythmDivision
{
    Quarter,
    Eighth,
    DottedEighth,
    Sixteenth,
    QuarterTriplet,
    EighthTriplet
};

double beatsPerStep (RhythmDivision);  // length of one step in quarter notes
juce::String toString (RhythmDivision);
std::optional<RhythmDivision> rhythmFromString (const juce::String&);

// Only Stereo is implemented. Binaural is reserved for a later block and must
// not be presented to users as available.
enum class SpatialMode { Stereo };

juce::String toString (SpatialMode);
std::optional<SpatialMode> spatialModeFromString (const juce::String&);

// Map coordinates 0..100. Listener at (50, 50). Smaller y = in front.
// In Stereo mode y is an approximate depth cue only, not real front/back.
struct Position
{
    double x = 50.0;
    double y = 50.0;
    bool operator== (const Position&) const = default;
};

// When the repetition plays, after the trigger (>= 0). The scene's TimeMode
// says which field is in force: `beat` (quarter notes, follows the host tempo)
// or `seconds` (free time, unaffected by tempo changes).
struct Timing
{
    double beat = 0.0;
    double seconds = 0.0;
    bool operator== (const Timing&) const = default;
};

enum class TimeMode { Beats, Free };
juce::String toString (TimeMode);
std::optional<TimeMode> timeModeFromString (const juce::String&);

// Part of the fragment a repetition plays (non-destructive), as fractions.
struct PlayRegion
{
    double start = 0.0, end = 1.0;
    bool operator== (const PlayRegion&) const = default;
};
PlayRegion clamped (PlayRegion);

// What a voice preset controls. Volume is intentionally NOT here: applying a
// preset to several points must not change their mix balance.
struct SoundSettings
{
    double pitchSemitones = 0.0;  // -24..+24
    double character = 0.0;       // 0..100, legacy preset value (stored; not processed)
    double speedPercent = 100.0;  // 50..200, legacy (stored; not processed)
    double formantSemitones = 0.0;  // -12..+12, formant shift independent of pitch (Signalsmith, pre-rendered)
    bool operator== (const SoundSettings&) const = default;
};

namespace limits
{
    inline constexpr double minCoord = 0.0, maxCoord = 100.0;
    inline constexpr double minBeat = 0.0, maxBeat = 1024.0;
    inline constexpr double minSeconds = 0.0, maxSeconds = 600.0;
    inline constexpr int minShaperCuts = 2, maxShaperCuts = 256;
    inline constexpr double minShaperPeriodMs = 5.0, maxShaperPeriodMs = 4000.0;
    inline constexpr double minShaperRampMs = 0.1, maxShaperAttackMs = 50.0, maxShaperReleaseMs = 200.0;
    inline constexpr double maxShaperMakeupDb = 12.0, maxShaperAutoLevelDb = 6.0;
    inline constexpr int minShaperCurvePoints = 2, maxShaperCurvePoints = 512;
    inline constexpr double minPitch = -24.0, maxPitch = 24.0;
    inline constexpr double minFormant = -12.0, maxFormant = 12.0;
    inline constexpr double maxDelayMs = 2000.0, maxPredelayMs = 200.0;
    inline constexpr int maxFxPoints = 16;  // repetitions that can carry their own effect chain at once
    inline constexpr double minCharacter = 0.0, maxCharacter = 100.0;
    inline constexpr double minSpeed = 50.0, maxSpeed = 200.0;
    inline constexpr double minGainDb = -36.0, maxGainDb = 12.0;
    inline constexpr int minStutterRepeats = 2, maxStutterRepeats = 32;
    inline constexpr double minMix = 0.0, maxMix = 100.0;
    inline constexpr int minMaxPoints = 1, maxMaxPoints = 64;  // format allows growth beyond 16
}

Position clamped (Position);
Timing clamped (Timing);
SoundSettings clamped (SoundSettings);

// Which preset a point's sound came from. Presets are copied, never linked:
// editing the point afterwards only sets `modified`.
struct VoiceRef
{
    juce::String presetId;
    juce::String presetName;  // snapshot, so a point still reads well if the preset is deleted
    bool modified = false;
    bool operator== (const VoiceRef&) const = default;
};

// Stutter / beat repeat: the repetition is cut into `repeats` short slices
// played one per `division` step, starting `offsetBeats` into the point's
// moment. `accelPercent` warps the slice times: > 0 the slices get closer
// (accelerating, like a bouncing ball), < 0 they spread out; the total
// duration (repeats x division) stays the same.
struct StutterSettings
{
    enum class Region { Whole, Start, End, Manual };
    enum class Division { Quarter, Eighth, Sixteenth, ThirtySecond };

    bool enabled = false;
    Region region = Region::Start;
    double manualStart = 0.0, manualEnd = 1.0;  // 0..1 of the fragment (Manual region)
    int repeats = 8;                            // 2..32
    Division division = Division::Sixteenth;
    double sliceLengthPercent = 100.0;          // 10..100 % of one division step
    double offsetBeats = 0.0;                   // where the effect starts inside the repetition (>= 0)
    double gainDb = 0.0;                        // -36..+12, on top of the point gain
    double accelPercent = 0.0;                  // -100..100: 0 = even slices
    bool operator== (const StutterSettings&) const = default;
};

// Start of every slice in beats after the point's moment (offsetBeats included)
// and the gap to the next one (the last gap equals the previous). Shared by the
// engine and the timeline, so what is drawn is what plays.
struct StutterSlice { double beat = 0.0, gapBeats = 0.0; };
std::vector<StutterSlice> stutterSlices (const StutterSettings&);

double beatsPerDivision (StutterSettings::Division);
juce::String toString (StutterSettings::Division);
std::optional<StutterSettings::Division> stutterDivisionFromString (const juce::String&);
juce::String toString (StutterSettings::Region);
std::optional<StutterSettings::Region> stutterRegionFromString (const juce::String&);
StutterSettings clamped (StutterSettings);

// Volume Shaper: a periodic volume curve (like a drawn FL Studio automation)
// applied on top of the repetition, independent of Stutter (which repeats
// audio; this only modulates amplitude). Count mode places `cuts` cuts over
// the shaped span (tempo-independent); Milliseconds and Division modes use a
// fixed period. Every edge is a linear attack/release ramp in ms, so the open
// part keeps full level (no smoothing filter).
// One breakpoint of a manual curve: `t` is the position inside the shaped
// span (0..1), `gain` the level there (0..1). Linear between points.
struct CurvePoint
{
    double t = 0.0, gain = 1.0;
    bool operator== (const CurvePoint&) const = default;
};

struct ShaperSettings
{
    enum class Shape { Square, Triangle, Saw };
    enum class RateMode { Count, Milliseconds, Division };
    enum class Distribution { Uniform, Accelerating, Decelerating };

    bool enabled = false;
    Shape shape = Shape::Square;
    RateMode rateMode = RateMode::Count;
    int cuts = 8;                       // Count mode: 2..256 cuts over the span
    double periodMs = 125.0;            // Milliseconds mode: 10..4000
    StutterSettings::Division division = StutterSettings::Division::Sixteenth;  // Division mode
    Distribution distribution = Distribution::Uniform;  // Count mode only
    double accelPercent = 0.0;          // 0..100: strength of the acceleration / deceleration
    juce::String pattern;               // per cut: 'x' open, '.' muted, repeats; empty = all open
    double depthPercent = 100.0;        // 0..100: how far the closed part drops
    double openPercent = 50.0;          // 5..95: part of each cut that stays open (square)
    double attackMs = 1.0;              // 0.1..50: ramp up at the start of each opening
    double releaseMs = 3.0;             // 0.1..200: ramp down after each opening
    bool wholeVoice = true;             // false = only regionStart..regionEnd (fractions of the repetition)
    double regionStart = 0.0, regionEnd = 1.0;
    double lengthMs = 0.0;              // 0 = to the end of the region; else the shaped span lasts this long
    double makeupDb = 0.0;              // -12..+12 after the curve (limiter still protects the output)
    bool autoLevel = false;             // compensates the RMS lost to the closed parts (at most +6 dB)
    // Manual curve: when `custom` is true the breakpoints below replace the
    // generated shape (depth, ramps, pattern... are ignored; makeup/autoLevel,
    // region and lengthMs still apply). Sorted by t, 2..512 points.
    bool custom = false;
    std::vector<CurvePoint> curve;
    bool operator== (const ShaperSettings&) const = default;
};

juce::String toString (ShaperSettings::Shape);
std::optional<ShaperSettings::Shape> shaperShapeFromString (const juce::String&);
juce::String toString (ShaperSettings::RateMode);
std::optional<ShaperSettings::RateMode> shaperRateModeFromString (const juce::String&);
juce::String toString (ShaperSettings::Distribution);
std::optional<ShaperSettings::Distribution> shaperDistributionFromString (const juce::String&);
ShaperSettings clamped (ShaperSettings);
// Manual curve helpers. normalisedCurve sorts by t, clamps and drops excess
// points (bakeCurve, which turns the generated shape into points, lives in
// audio/EchoEngine.h next to shaperGain).
std::vector<CurvePoint> normalisedCurve (std::vector<CurvePoint>);
// Curve value at t (0..1), linear between breakpoints; 1 if fewer than 2 points.
double curveGainAt (const std::vector<CurvePoint>&, double t) noexcept;
// Shortest cut (seconds) the curve can represent usefully: attack + release + 1 ms.
double minimumCutSeconds (const ShaperSettings&);
// Largest cut count that keeps every cut at least minimumCutSeconds over a span of `spanSeconds`.
int maxUsefulCuts (const ShaperSettings&, double spanSeconds);

struct ShaperPreset
{
    const char* id;
    const char* name;
    ShaperSettings settings;
};
const std::vector<ShaperPreset>& shaperPresets();

// Per-repetition effect chain, all with real processing, by default in this order
// after the voice itself (pitch/formant, filters, Volume Shaper, fades, pan):
// character -> modulation -> delay -> reverb. Each has its own enable and mix.
struct CharacterSettings
{
    enum class Type { Saturation, Overdrive, Bitcrusher, Telephone, LoFi, Robot };
    bool enabled = false;
    Type type = Type::Saturation;
    double amountPercent = 50.0;  // 0..100 drive / crush / carrier frequency (Robot)
    double mixPercent = 100.0;
    bool operator== (const CharacterSettings&) const = default;
};

struct ModulationSettings
{
    enum class Type { Chorus, Flanger, Tremolo };
    bool enabled = false;
    Type type = Type::Chorus;
    double rateHz = 1.0;          // 0.05..20
    double depthPercent = 50.0;   // 0..100
    double mixPercent = 50.0;     // chorus / flanger only
    bool operator== (const ModulationSettings&) const = default;
};

struct DelaySettings
{
    bool enabled = false;
    bool sync = false;            // false: timeMs; true: division at the tempo in force
    double timeMs = 250.0;        // 1..2000
    StutterSettings::Division division = StutterSettings::Division::Eighth;
    double feedbackPercent = 35.0;  // 0..95
    bool pingPong = false;
    double lowpassHz = 8000.0;    // 500..20000 in the feedback path
    double mixPercent = 30.0;
    bool operator== (const DelaySettings&) const = default;
};

struct ReverbSettings
{
    enum class Type { Room, Hall, Plate };
    bool enabled = false;
    Type type = Type::Room;
    double sizePercent = 50.0;
    double decayPercent = 50.0;
    double predelayMs = 10.0;     // 0..200
    double mixPercent = 25.0;
    double outputDb = 0.0;        // -24..+6
    bool operator== (const ReverbSettings&) const = default;
};

struct PointEffects
{
    double fadeInMs = 0.0, fadeOutMs = 0.0;  // 0..2000, on top of the 2 ms anticlick fades
    bool reverse = false;                    // plays the region backwards
    double lowpassHz = 20000.0;              // 200..20000, 20000 = off
    double highpassHz = 20.0;                // 20..5000, 20 = off
    CharacterSettings character;
    ModulationSettings modulation;
    DelaySettings delay;
    ReverbSettings reverb;
    // Order of the chain: a permutation of "cmdr" (character, modulation,
    // delay, reverb). Default = the studio order. Invalid strings are repaired.
    juce::String chainOrder = "cmdr";
    bool operator== (const PointEffects&) const = default;
    bool usesChain() const noexcept { return character.enabled || modulation.enabled || delay.enabled || reverb.enabled; }
};
PointEffects clamped (PointEffects);
juce::String normalisedChainOrder (const juce::String&);  // always a permutation of "cmdr"
juce::String chainStageName (juce::juce_wchar stage);     // 'c' -> "Carácter"...
juce::String toString (CharacterSettings::Type);
std::optional<CharacterSettings::Type> characterTypeFromString (const juce::String&);
juce::String toString (ModulationSettings::Type);
std::optional<ModulationSettings::Type> modulationTypeFromString (const juce::String&);
juce::String toString (ReverbSettings::Type);
std::optional<ReverbSettings::Type> reverbTypeFromString (const juce::String&);

// Creative presets: a complete recipe for a repetition (sound, Volume Shaper
// and/or effect chain), all made of processing that exists.
struct PointPreset
{
    const char* id;
    const char* name;
    const char* description;
    std::optional<SoundSettings> sound;
    std::optional<ShaperSettings> shaper;
    std::optional<PointEffects> effects;
};
const std::vector<PointPreset>& pointPresets();

struct Point
{
    juce::String id;     // stable identity: never reused, independent of number and timing
    int number = 0;      // display label only
    bool enabled = true;   // false = muted
    bool solo = false;     // if any enabled point is soloed, only soloed points play
    Position position;
    Timing timing;
    PlayRegion region;             // which part of the fragment this repetition plays
    VoiceRef voice;
    juce::String creativePreset;   // id of the last creative preset applied (display only; edits keep it)
    SoundSettings sound;
    double gainDb = 0.0;           // -36..+12 dB, independent of pitch and of the global Output
    StutterSettings stutter;
    ShaperSettings shaper;
    PointEffects effects;
    bool operator== (const Point&) const = default;
};

// Where the source audio lives. Prepared for portability: a scene can keep a
// reference (path + hash) or embed the encoded audio bytes. B1 stores this
// data but does not load or decode audio.
struct SourceRef
{
    enum class Storage { None, Reference, Embedded };

    Storage storage = Storage::None;
    juce::String filePath;
    juce::String contentHash;   // e.g. "sha256:<hex>", used to verify/relocate files
    juce::String encoding;      // e.g. "wav" for embedded bytes
    double sampleRate = 0.0;
    int numChannels = 0;
    juce::int64 numSamples = 0;
    juce::int64 trimStart = 0;  // in samples
    juce::int64 trimEnd = 0;    // in samples, 0 = end of file
    juce::MemoryBlock embeddedData;

    bool operator== (const SourceRef&) const = default;
};

struct Scene
{
    juce::String id;
    juce::String name;
    RhythmDivision rhythm = RhythmDivision::Quarter;
    TimeMode timeMode = TimeMode::Beats;
    double mixPercent = 70.0;
    SpatialMode spatialMode = SpatialMode::Stereo;
    int maxPoints = kDefaultMaxPoints;
    SourceRef source;
    std::vector<Point> points;

    bool operator== (const Scene&) const = default;
};

// File-level container. The format holds several scenes; the first UI uses one.
struct Document
{
    std::vector<Scene> scenes;
    juce::String activeSceneId;

    bool operator== (const Document&) const = default;

    const Scene* activeScene() const;
    Scene* activeScene();
};

juce::String makeId();

struct VoicePreset;

// ---- Editing operations ---------------------------------------------------

// Id of the live-processing pseudo-point (Scene::live.fx): it is not part of
// `points`, but findPoint returns it so every editor view can edit it.
inline constexpr const char* kLivePointId = "live";
Point* findPoint (Scene&, const juce::String& id);
const Point* findPoint (const Scene&, const juce::String& id);

int nextPointNumber (const Scene&);
Timing nextFreeTiming (const Scene&);  // one step after the latest point (first point: one step)

// Returns the new point's id, or nullopt if the scene is full.
std::optional<juce::String> addPoint (Scene&, Position, std::optional<Timing> = {},
                                      const VoicePreset* preset = nullptr);
bool removePoint (Scene&, const juce::String& id);
// Copy with a new id and number, one step later, offset slightly to the right.
std::optional<juce::String> duplicatePoint (Scene&, const juce::String& id);
bool movePoint (Scene&, const juce::String& id, Position);
bool setTiming (Scene&, const juce::String& id, Timing);
bool setSound (Scene&, const juce::String& id, SoundSettings);  // marks voice as modified
bool setGainDb (Scene&, const juce::String& id, double gainDb);
bool setStutter (Scene&, const juce::String& id, StutterSettings);
bool setShaper (Scene&, const juce::String& id, ShaperSettings);
bool setRegion (Scene&, const juce::String& id, PlayRegion);
bool setEffects (Scene&, const juce::String& id, PointEffects);
void applyPointPreset (Point&, const PointPreset&);
// Switches the scene's time mode, converting every point's moment with `bpm`
// so nothing moves at that tempo.
void setTimeMode (Scene&, TimeMode, double bpm);
// The point's moment in the unit the scene is using, as seconds at `bpm`.
double momentSeconds (const Scene&, const Point&, double bpm);

// Copies the preset's sound into every listed point. Position, timing,
// volume, number and id are never touched. Returns how many points changed.
int applyVoicePreset (Scene&, const std::vector<juce::String>& pointIds, const VoicePreset&);

} // namespace ses
