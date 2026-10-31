#pragma once

// Voice presets: reusable sound settings, independent of scenes. A scene only
// stores a copy of the values plus a VoiceRef; deleting or editing a preset
// never changes existing points.

#include "model/Scene.h"

namespace ses
{

struct VoicePreset
{
    juce::String id;
    juce::String name;
    juce::String description;
    SoundSettings sound;
    bool builtIn = false;
    // True when the preset's intended sound needs processing that is not
    // implemented yet (e.g. formants, modulation). The UI must say so.
    bool needsFutureProcessing = false;

    bool operator== (const VoicePreset&) const = default;
};

namespace presetIds
{
    inline const juce::String original { "builtin.original" };
    inline const juce::String agudo    { "builtin.agudo" };
    inline const juce::String grave    { "builtin.grave" };
    inline const juce::String monster  { "builtin.monster" };
    inline const juce::String robot    { "builtin.robot" };
    inline const juce::String fantasma { "builtin.fantasma" };
}

const std::vector<VoicePreset>& builtInVoicePresets();

class VoicePresetLibrary
{
public:
    VoicePresetLibrary();  // starts with the built-ins

    const VoicePreset* find (const juce::String& id) const;
    const std::vector<VoicePreset>& all() const noexcept { return presets; }
    std::vector<VoicePreset> userPresets() const;

    // Adds or replaces a user preset. Built-ins cannot be overwritten.
    bool upsertUserPreset (VoicePreset);
    bool removeUserPreset (const juce::String& id);

private:
    std::vector<VoicePreset> presets;
};

} // namespace ses
