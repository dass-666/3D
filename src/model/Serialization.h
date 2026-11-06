#pragma once

// Versioned JSON formats. Scenes and voice presets are separate formats so a
// preset library can be shared without any scene, and vice versa.

#include "model/Scene.h"
#include "model/VoicePresets.h"

namespace ses
{

inline constexpr const char* kDocumentFormatName = "SpatialEchoStudio.Document";
inline constexpr const char* kVoicePresetsFormatName = "SpatialEchoStudio.VoicePresets";
inline constexpr int kDocumentFormatVersion = 1;
inline constexpr int kVoicePresetsFormatVersion = 1;

template <typename T>
struct LoadResult
{
    std::optional<T> value;        // empty on failure
    juce::String error;            // reason when value is empty
    juce::StringArray warnings;    // repaired problems (clamped values, regenerated ids...)

    explicit operator bool() const noexcept { return value.has_value(); }
};

juce::String toJson (const Document&);
LoadResult<Document> documentFromJson (const juce::String& json);

// User creative presets + favourites file.
struct UserPresetFile;
juce::String userPresetsToJson (const UserPresetFile&);
LoadResult<UserPresetFile> userPresetsFromJson (const juce::String& json);

// Only user presets are written; built-ins ship with the plugin.
juce::String voicePresetsToJson (const std::vector<VoicePreset>& userPresets);
LoadResult<std::vector<VoicePreset>> voicePresetsFromJson (const juce::String& json);

} // namespace ses
