#include "model/VoicePresets.h"

#include <algorithm>

namespace ses
{

const std::vector<VoicePreset>& builtInVoicePresets()
{
    // Values are starting points, not restrictions. Pitch and formants are
    // pre-rendered (Signalsmith); Monster/Robot/Fantasma also carry an effect
    // chain (see pointPresets) so they differ by real processing.
    const auto t = [] (const char* utf8) { return juce::String::fromUTF8 (utf8); };
    auto snd = [] (double pitch, double formant) { SoundSettings s; s.pitchSemitones = pitch; s.formantSemitones = formant; return s; };
    static const std::vector<VoicePreset> presets {
        { presetIds::original, "Original", t ("La voz tal cual."),                                   snd (0.0, 0.0),  true, false },
        { presetIds::agudo,    "Agudo",    t ("M\xc3\xa1s aguda, misma duraci\xc3\xb3n."),            snd (7.0, 0.0),  true, false },
        { presetIds::grave,    "Grave",    t ("M\xc3\xa1s grave, misma duraci\xc3\xb3n."),            snd (-5.0, 0.0), true, false },
        { presetIds::profundo, "Profundo", t ("Formantes abajo: voz m\xc3\xa1s grande, mismo tono."), snd (-2.0, -4.0), true, false },
        { presetIds::brillante, "Brillante", t ("Formantes arriba: voz m\xc3\xa1s fina y clara."),    snd (1.0, 3.0),  true, false },
        { presetIds::monster,  "Monster",  t ("Muy grave, formantes abajo y saturaci\xc3\xb3n."),    snd (-9.0, -6.0), true, false },
        { presetIds::robot,    "Robot",    t ("Modulaci\xc3\xb3n en anillo met\xc3\xa1lica."),         snd (0.0, -1.0), true, false },
        { presetIds::fantasma, "Fantasma", t ("Arriba, con chorus y sala larga."),                   snd (5.0, 3.0),  true, false },
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
