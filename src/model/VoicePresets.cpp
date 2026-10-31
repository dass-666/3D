#include "model/VoicePresets.h"

#include <algorithm>

namespace ses
{

const std::vector<VoicePreset>& builtInVoicePresets()
{
    // Values are starting points, not restrictions. Monster/Robot/Fantasma are
    // flagged: with pitch alone they cannot yet sound as their names promise.
    const auto t = [] (const char* utf8) { return juce::String::fromUTF8 (utf8); };
    static const std::vector<VoicePreset> presets {
        { presetIds::original, "Original", t ("La voz tal cual."),                        { 0.0, 0.0, 100.0 },  true, false },
        { presetIds::agudo,    "Agudo",    t ("M\xc3\xa1s aguda, misma duraci\xc3\xb3n."), { 7.0, 25.0, 100.0 }, true, false },
        { presetIds::grave,    "Grave",    t ("M\xc3\xa1s grave, misma duraci\xc3\xb3n."), { -5.0, 25.0, 100.0 }, true, false },
        { presetIds::monster,  "Monster",  t ("Muy grave con car\xc3\xa1" "cter oscuro."),   { -7.0, 40.0, 100.0 }, true, true },
        { presetIds::robot,    "Robot",    t ("Timbre met\xc3\xa1lico."),                  { 0.0, 50.0, 100.0 }, true, true },
        { presetIds::fantasma, "Fantasma", t ("Lejana y et\xc3\xa9rea."),                  { 3.0, 50.0, 100.0 }, true, true },
    };
    return presets;
}

VoicePresetLibrary::VoicePresetLibrary() : presets (builtInVoicePresets()) {}

const VoicePreset* VoicePresetLibrary::find (const juce::String& id) const
{
    auto it = std::find_if (presets.begin(), presets.end(), [&] (auto& p) { return p.id == id; });
    return it == presets.end() ? nullptr : &*it;
}

std::vector<VoicePreset> VoicePresetLibrary::userPresets() const
{
    std::vector<VoicePreset> out;
    std::copy_if (presets.begin(), presets.end(), std::back_inserter (out), [] (auto& p) { return ! p.builtIn; });
    return out;
}

bool VoicePresetLibrary::upsertUserPreset (VoicePreset preset)
{
    if (preset.id.isEmpty())
        preset.id = makeId();

    preset.builtIn = false;
    preset.sound = clamped (preset.sound);

    if (auto it = std::find_if (presets.begin(), presets.end(), [&] (auto& p) { return p.id == preset.id; });
        it != presets.end())
    {
        if (it->builtIn)
            return false;
        *it = preset;
        return true;
    }

    presets.push_back (preset);
    return true;
}

bool VoicePresetLibrary::removeUserPreset (const juce::String& id)
{
    const auto before = presets.size();
    std::erase_if (presets, [&] (auto& p) { return p.id == id && ! p.builtIn; });
    return presets.size() != before;
}

} // namespace ses
