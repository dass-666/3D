#include "model/DemoScene.h"
#include "model/VoicePresets.h"

namespace ses
{

Scene makeDemoScene()
{
    Scene scene;
    scene.id = makeId();
    scene.name = "Demo";
    scene.rhythm = RhythmDivision::Quarter;
    scene.mixPercent = 70.0;

    struct Spec { double x, y; const juce::String& preset; const char* name; SoundSettings sound; double volume; };
    const Spec specs[] {
        { 18, 49, presetIds::original, "Original", { 0.0, 0.0, 100.0 },  100.0 },
        { 82, 49, presetIds::agudo,    "Agudo",    { 7.0, 25.0, 100.0 },  85.0 },
        { 50, 18, presetIds::monster,  "Monster",  { -7.0, 40.0, 100.0 }, 90.0 },
        { 50, 83, presetIds::grave,    "Grave",    { -5.0, 25.0, 100.0 }, 75.0 },
    };

    int number = 1;
    for (auto& s : specs)
    {
        Point p;
        p.id = makeId();
        p.number = number;
        p.position = { s.x, s.y };
        p.timing = { (double) number * beatsPerStep (scene.rhythm) };
        p.voice = { s.preset, s.name, false };
        p.sound = s.sound;
        p.volumePercent = s.volume;
        scene.points.push_back (p);
        ++number;
    }
    return scene;
}

Document makeDefaultDocument()
{
    Document doc;
    doc.scenes.push_back (makeDemoScene());
    doc.activeSceneId = doc.scenes.front().id;
    return doc;
}

} // namespace ses
