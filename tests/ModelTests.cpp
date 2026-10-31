#include "model/DemoScene.h"
#include "model/Scene.h"
#include "model/VoicePresets.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <set>

using namespace ses;

TEST_CASE ("Demo scene matches the agreed specification")
{
    const auto scene = makeDemoScene();
    REQUIRE (scene.points.size() == 4);
    CHECK (scene.rhythm == RhythmDivision::Quarter);
    CHECK (scene.mixPercent == 70.0);
    CHECK (scene.spatialMode == SpatialMode::Stereo);

    struct Expected { int number; double x, y; juce::String preset; double pitch, character, speed, gainDb, beat; };
    const Expected expected[] {
        { 1, 18, 49, presetIds::original, 0, 0, 100, 0.0, 1 },
        { 2, 82, 49, presetIds::agudo, 7, 25, 100, -1.5, 2 },
        { 3, 50, 18, presetIds::monster, -7, 40, 100, -1.0, 3 },
        { 4, 50, 83, presetIds::grave, -5, 25, 100, -2.5, 4 },
    };

    for (size_t i = 0; i < 4; ++i)
    {
        const auto& p = scene.points[i];
        const auto& e = expected[i];
        CHECK (p.number == e.number);
        CHECK (p.position == Position { e.x, e.y });
        CHECK (p.voice.presetId == e.preset);
        CHECK (p.sound == SoundSettings { e.pitch, e.character, e.speed });
        CHECK (p.gainDb == e.gainDb);
        CHECK (p.timing.beat == e.beat);
    }
}

TEST_CASE ("Point ids are unique and stable across edits")
{
    auto scene = makeDemoScene();
    std::set<juce::String> ids;
    for (auto& p : scene.points)
        ids.insert (p.id);
    REQUIRE (ids.size() == 4);

    const auto id2 = scene.points[1].id;
    REQUIRE (movePoint (scene, id2, { 10, 90 }));
    REQUIRE (setTiming (scene, id2, { 0.5 }));
    REQUIRE (removePoint (scene, scene.points[0].id));

    const auto* p = findPoint (scene, id2);
    REQUIRE (p != nullptr);
    CHECK (p->id == id2);
    CHECK (p->number == 2);  // number is a label: not recomputed after removal or timing changes
}

TEST_CASE ("Scene enforces its maximum number of points")
{
    Scene scene;
    scene.maxPoints = kDefaultMaxPoints;
    for (int i = 0; i < kDefaultMaxPoints; ++i)
        REQUIRE (addPoint (scene, { 50, 50 }).has_value());

    CHECK_FALSE (addPoint (scene, { 50, 50 }).has_value());
    CHECK_FALSE (duplicatePoint (scene, scene.points[0].id).has_value());
    CHECK (scene.points.size() == (size_t) kDefaultMaxPoints);
}

TEST_CASE ("Any preset can be used by any number of points")
{
    Scene scene;
    VoicePresetLibrary lib;
    const auto* monster = lib.find (presetIds::monster);
    REQUIRE (monster != nullptr);

    for (int i = 0; i < kDefaultMaxPoints; ++i)
        REQUIRE (addPoint (scene, { (double) i * 6, 50 }, std::nullopt, monster).has_value());

    for (auto& p : scene.points)
    {
        CHECK (p.voice.presetId == presetIds::monster);
        CHECK (p.sound == monster->sound);
    }
}

TEST_CASE ("Applying a preset to several points changes only their sound")
{
    auto scene = makeDemoScene();
    const auto before = scene.points;
    VoicePresetLibrary lib;
    const auto* agudo = lib.find (presetIds::agudo);

    const std::vector<juce::String> targets { scene.points[0].id, scene.points[2].id, "missing-id" };
    CHECK (applyVoicePreset (scene, targets, *agudo) == 2);

    for (size_t i = 0; i < scene.points.size(); ++i)
    {
        const auto& p = scene.points[i];
        const auto& b = before[i];
        CHECK (p.id == b.id);
        CHECK (p.number == b.number);
        CHECK (p.position == b.position);
        CHECK (p.timing == b.timing);
        CHECK (p.gainDb == b.gainDb);

        if (i == 0 || i == 2)
        {
            CHECK (p.sound == agudo->sound);
            CHECK (p.voice == VoiceRef { presetIds::agudo, "Agudo", false });
        }
        else
        {
            CHECK (p.sound == b.sound);
            CHECK (p.voice == b.voice);
        }
    }
}

TEST_CASE ("Editing a point's sound marks it as modified, values are clamped")
{
    auto scene = makeDemoScene();
    const auto id = scene.points[1].id;

    REQUIRE (setSound (scene, id, { 99.0, -5.0, 10.0 }));
    const auto* p = findPoint (scene, id);
    CHECK (p->voice.modified);
    CHECK (p->voice.presetId == presetIds::agudo);
    CHECK (p->sound == SoundSettings { 24.0, 0.0, 50.0 });

    REQUIRE (movePoint (scene, id, { -10, 250 }));
    CHECK (p->position == Position { 0, 100 });
    REQUIRE (setGainDb (scene, id, 40));
    CHECK (p->gainDb == 12.0);
}

TEST_CASE ("Duplicate creates a new identity one step later")
{
    auto scene = makeDemoScene();
    scene.rhythm = RhythmDivision::Eighth;
    const auto srcId = scene.points[1].id;
    const auto src = *findPoint (scene, srcId);

    const auto newId = duplicatePoint (scene, srcId);
    REQUIRE (newId.has_value());
    const auto* copy = findPoint (scene, *newId);
    REQUIRE (copy != nullptr);

    CHECK (copy->id != srcId);
    CHECK (copy->number == 5);
    CHECK (copy->sound == src.sound);
    CHECK (copy->voice == src.voice);
    CHECK (copy->timing.beat == Catch::Approx (src.timing.beat + 0.5));
}

TEST_CASE ("New points start one rhythm step after the latest point")
{
    Scene scene;
    scene.rhythm = RhythmDivision::DottedEighth;
    const auto a = addPoint (scene, { 50, 50 });
    const auto b = addPoint (scene, { 50, 50 });
    CHECK (findPoint (scene, *a)->timing.beat == Catch::Approx (0.75));
    CHECK (findPoint (scene, *b)->timing.beat == Catch::Approx (1.5));
    CHECK (findPoint (scene, *a)->voice.presetId == presetIds::original);
}

TEST_CASE ("Voice preset library is independent of scenes")
{
    VoicePresetLibrary lib;
    CHECK (lib.all().size() == 6);
    CHECK (lib.userPresets().empty());

    // Built-ins cannot be overwritten or removed.
    auto hacked = *lib.find (presetIds::original);
    hacked.sound.pitchSemitones = 12;
    CHECK_FALSE (lib.upsertUserPreset (hacked));
    CHECK_FALSE (lib.removeUserPreset (presetIds::original));
    CHECK (lib.find (presetIds::original)->sound.pitchSemitones == 0.0);

    // Removing a user preset never changes points that used it.
    auto scene = makeDemoScene();
    VoicePreset mine { "", "Mi voz", "", { 3.0, 10.0, 120.0 }, false, false };
    REQUIRE (lib.upsertUserPreset (mine));
    const auto userId = lib.userPresets().front().id;
    applyVoicePreset (scene, { scene.points[0].id }, *lib.find (userId));
    REQUIRE (lib.removeUserPreset (userId));

    CHECK (scene.points[0].sound == SoundSettings { 3.0, 10.0, 120.0 });
    CHECK (scene.points[0].voice.presetName == "Mi voz");
}

TEST_CASE ("Built-in presets that need unimplemented processing are flagged")
{
    VoicePresetLibrary lib;
    CHECK_FALSE (lib.find (presetIds::original)->needsFutureProcessing);
    CHECK_FALSE (lib.find (presetIds::agudo)->needsFutureProcessing);
    CHECK_FALSE (lib.find (presetIds::grave)->needsFutureProcessing);
    CHECK (lib.find (presetIds::monster)->needsFutureProcessing);
    CHECK (lib.find (presetIds::robot)->needsFutureProcessing);
    CHECK (lib.find (presetIds::fantasma)->needsFutureProcessing);
}
