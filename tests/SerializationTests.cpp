#include "model/DemoScene.h"
#include "model/Serialization.h"

#include <catch2/catch_test_macros.hpp>

using namespace ses;

namespace
{
Document roundTrip (const Document& doc)
{
    auto loaded = documentFromJson (toJson (doc));
    REQUIRE (loaded);
    CHECK (loaded.warnings.isEmpty());
    return *loaded.value;
}
}

TEST_CASE ("Document round-trips without loss")
{
    const auto doc = makeDefaultDocument();
    const auto back = roundTrip (doc);
    CHECK (back == doc);
    CHECK (toJson (back) == toJson (doc));  // byte-identical re-save
}

TEST_CASE ("Non-trivial values survive a round trip exactly")
{
    auto doc = makeDefaultDocument();
    auto& scene = doc.scenes.front();
    scene.rhythm = RhythmDivision::EighthTriplet;
    addPoint (scene, { 33.333333333333336, 0.1 }, Timing { 2.0 / 3.0 });
    setSound (scene, scene.points.back().id, { -11.75, 12.5, 137.25 });
    scene.points.back().enabled = false;

    CHECK (roundTrip (doc) == doc);
}

TEST_CASE ("Multiple scenes and the active scene are preserved")
{
    auto doc = makeDefaultDocument();
    auto second = makeDemoScene();
    second.name = juce::String::fromUTF8 ("Estribillo con acentos: campe\xc3\xb3n");
    second.points.pop_back();
    doc.scenes.push_back (second);
    doc.activeSceneId = second.id;

    const auto back = roundTrip (doc);
    REQUIRE (back.scenes.size() == 2);
    CHECK (back.activeScene()->id == second.id);
    CHECK (back.scenes[1].name == second.name);
    CHECK (back == doc);
}

TEST_CASE ("Source audio can be referenced or embedded portably")
{
    auto doc = makeDefaultDocument();
    auto& src = doc.scenes.front().source;
    src.storage = SourceRef::Storage::Embedded;
    src.filePath = "C:/Users/diego/Music/campeon.wav";
    src.contentHash = "sha256:00ff";
    src.encoding = "wav";
    src.sampleRate = 48000;
    src.numChannels = 2;
    src.numSamples = 96000;
    src.trimStart = 1200;
    src.trimEnd = 50000;
    const unsigned char bytes[] { 'R', 'I', 'F', 'F', 0, 1, 2, 255, 128, 7 };
    src.embeddedData = juce::MemoryBlock (bytes, sizeof (bytes));

    const auto back = roundTrip (doc);
    CHECK (back.scenes.front().source == src);
}

TEST_CASE ("Ids survive save and load")
{
    const auto doc = makeDefaultDocument();
    const auto back = roundTrip (doc);
    for (size_t i = 0; i < doc.scenes[0].points.size(); ++i)
        CHECK (back.scenes[0].points[i].id == doc.scenes[0].points[i].id);
    CHECK (back.scenes[0].id == doc.scenes[0].id);
}

TEST_CASE ("Invalid input is rejected with a reason")
{
    CHECK_FALSE (documentFromJson ("not json"));
    CHECK_FALSE (documentFromJson ("[]"));
    CHECK_FALSE (documentFromJson (R"({"format":"Other","formatVersion":1,"scenes":[{}]})"));
    CHECK_FALSE (documentFromJson (R"({"format":"SpatialEchoStudio.Document","scenes":[{}]})"));
    CHECK_FALSE (documentFromJson (R"({"format":"SpatialEchoStudio.Document","formatVersion":1,"scenes":[]})"));

    const auto future = documentFromJson (R"({"format":"SpatialEchoStudio.Document","formatVersion":99,"scenes":[{}]})");
    CHECK_FALSE (future);
    CHECK (future.error.contains ("newer version"));
}

TEST_CASE ("Damaged values are repaired and reported")
{
    const auto json = R"({
      "format": "SpatialEchoStudio.Document", "formatVersion": 1, "activeSceneId": "nope",
      "scenes": [{
        "id": "s1", "rhythm": "1/5", "mixPercent": 250, "spatialMode": "binaural", "maxPoints": 2,
        "points": [
          { "id": "a", "number": 1, "position": { "x": 150, "y": -3 }, "sound": { "pitchSemitones": 40 }, "unknownField": true },
          { "id": "a", "number": 1, "timing": { "beat": "x" } },
          { "id": "c", "number": 3 }
        ]
      }]
    })";

    const auto loaded = documentFromJson (json);
    REQUIRE (loaded);
    const auto& scene = loaded.value->scenes.front();

    CHECK (loaded.value->activeSceneId == "s1");
    CHECK (scene.rhythm == RhythmDivision::Quarter);
    CHECK (scene.mixPercent == 100.0);
    CHECK (scene.spatialMode == SpatialMode::Stereo);  // binaural not available: never silently promised
    REQUIRE (scene.points.size() == 2);                // maxPoints respected
    CHECK (scene.points[0].position == Position { 100, 0 });
    CHECK (scene.points[0].sound.pitchSemitones == 24.0);
    CHECK (scene.points[1].id != "a");                 // duplicate id regenerated
    CHECK (scene.points[1].number == 2);               // duplicate number renumbered
    CHECK (scene.points[1].timing.beat == 0.0);
    CHECK (loaded.warnings.size() >= 7);
}

TEST_CASE ("User voice presets use their own format")
{
    VoicePresetLibrary lib;
    REQUIRE (lib.upsertUserPreset ({ "user.1", "Susurro", "Suave", { -2.0, 15.0, 90.0 }, false, false }));

    const auto json = voicePresetsToJson (lib.userPresets());
    CHECK_FALSE (json.contains ("builtin."));  // built-ins are not exported
    CHECK_FALSE (documentFromJson (json));     // not interchangeable with scenes

    const auto loaded = voicePresetsFromJson (json);
    REQUIRE (loaded);
    REQUIRE (loaded.value->size() == 1);
    CHECK (loaded.value->front() == lib.userPresets().front());

    const auto reserved = voicePresetsFromJson (R"({"format":"SpatialEchoStudio.VoicePresets","formatVersion":1,
        "presets":[{"id":"builtin.original","name":"Fake"}]})");
    REQUIRE (reserved);
    CHECK (reserved.value->front().id != "builtin.original");
}
