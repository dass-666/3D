#pragma once

// User creative presets (a repetition's sound, Volume Shaper and effect chain)
// and favourites, kept in one JSON file in the user's application data folder,
// shared by every plugin instance. Message thread only.

#include "model/Scene.h"

#include <juce_events/juce_events.h>

namespace ses
{

struct UserPointPreset
{
    juce::String id;           // "user.<id>"
    juce::String name;
    juce::String description;
    Point content;             // only sound, voice, shaper and effects are used
    bool operator== (const UserPointPreset&) const = default;
};

struct UserPresetFile
{
    std::vector<UserPointPreset> presets;
    juce::StringArray favourites;  // ids of built-in or user presets
    bool operator== (const UserPresetFile&) const = default;
};

// Copies the preset's sound, voice, Volume Shaper and effects into the point
// (position, timing, gain, number and id are never touched).
void applyUserPreset (Point&, const UserPointPreset&);

class UserPresetStore final : public juce::ChangeBroadcaster
{
public:
    static UserPresetStore& shared();            // loads the default file on first use
    static juce::File defaultFile();
    static void overrideSharedFile (const juce::File&);  // tests: before the first shared() call

    explicit UserPresetStore (juce::File = {});  // empty = in memory only (tests)
    juce::Result load();
    juce::Result save() const;

    const std::vector<UserPointPreset>& presets() const noexcept { return data.presets; }
    const UserPointPreset* find (const juce::String& id) const;
    // Adds (empty id) or replaces; returns the id. Saves and notifies.
    juce::String upsert (UserPointPreset);
    juce::String addFromPoint (const juce::String& name, const juce::String& description, const Point&);
    bool remove (const juce::String& id);

    bool isFavourite (const juce::String& id) const { return data.favourites.contains (id); }
    void setFavourite (const juce::String& id, bool on);
    const juce::StringArray& favourites() const noexcept { return data.favourites; }

    juce::String lastError;
    bool loadFailed() const noexcept { return loadBroken; }  // the file could not be read: saving is refused so it is not overwritten

private:
    void changed();
    juce::File file;
    UserPresetFile data;
    bool loadBroken = false;
};

} // namespace ses
