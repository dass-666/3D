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

// When the repetition plays, in quarter notes after the trigger (>= 0).
struct Timing
{
    double beat = 0.0;
    bool operator== (const Timing&) const = default;
};

// What a voice preset controls. Volume is intentionally NOT here: applying a
// preset to several points must not change their mix balance.
struct SoundSettings
{
    double pitchSemitones = 0.0;  // -24..+24
    double character = 0.0;       // 0..100, preset character intensity (stored; not processed yet)
    double speedPercent = 100.0;  // 50..200, duration, independent of pitch (stored; not processed yet)
    bool operator== (const SoundSettings&) const = default;
};

namespace limits
{
    inline constexpr double minCoord = 0.0, maxCoord = 100.0;
    inline constexpr double minBeat = 0.0, maxBeat = 1024.0;
    inline constexpr double minPitch = -24.0, maxPitch = 24.0;
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
// moment. Acceleration is deliberately not implemented yet.
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
    bool operator== (const StutterSettings&) const = default;
};

double beatsPerDivision (StutterSettings::Division);
juce::String toString (StutterSettings::Division);
std::optional<StutterSettings::Division> stutterDivisionFromString (const juce::String&);
juce::String toString (StutterSettings::Region);
std::optional<StutterSettings::Region> stutterRegionFromString (const juce::String&);
StutterSettings clamped (StutterSettings);

struct Point
{
    juce::String id;     // stable identity: never reused, independent of number and timing
    int number = 0;      // display label only
    bool enabled = true;   // false = muted
    bool solo = false;     // if any enabled point is soloed, only soloed points play
    Position position;
    Timing timing;
    VoiceRef voice;
    SoundSettings sound;
    double gainDb = 0.0;           // -36..+12 dB, independent of pitch and of the global Output
    StutterSettings stutter;
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

// Copies the preset's sound into every listed point. Position, timing,
// volume, number and id are never touched. Returns how many points changed.
int applyVoicePreset (Scene&, const std::vector<juce::String>& pointIds, const VoicePreset&);

} // namespace ses
