#include "model/UserPresets.h"

#include "model/Serialization.h"

namespace ses
{

void applyUserPreset (Point& p, const UserPointPreset& preset)
{
    p.sound = clamped (preset.content.sound);
    p.voice = preset.content.voice;
    p.shaper = clamped (preset.content.shaper);
    p.effects = clamped (preset.content.effects);
    p.creativePreset = preset.id;
}

juce::File UserPresetStore::defaultFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("SpatialEchoStudio").getChildFile ("presets.json");
}

static juce::File& sharedOverride()
{
    static juce::File f;
    return f;
}

static bool& sharedCreated()
{
    static bool created = false;
    return created;
}

void UserPresetStore::overrideSharedFile (const juce::File& f)
{
    sharedOverride() = f;
    if (sharedCreated())
    {
        // Already in use: retarget it (tests) so nothing is written to the real user folder.
        auto& s = shared();
        s.file = f;
        s.data = {};
        s.loadBroken = false;
        s.load();
        s.sendChangeMessage();
    }
}

UserPresetStore& UserPresetStore::shared()
{
    static UserPresetStore store (sharedOverride() != juce::File() ? sharedOverride() : defaultFile());
    static bool loaded = false;
    if (! loaded)
    {
        loaded = true;
        sharedCreated() = true;
        store.load();
    }
    return store;
}

UserPresetStore::UserPresetStore (juce::File f) : file (std::move (f)) {}

juce::Result UserPresetStore::load()
{
    if (file == juce::File() || ! file.existsAsFile())
        return juce::Result::ok();
    auto r = userPresetsFromJson (file.loadFileAsString());
    if (! r)
    {
        // Keep the user's file: a copy next to it, and no saving until a load works.
        loadBroken = true;
        lastError = r.error;
        file.copyFileTo (file.getSiblingFile (file.getFileNameWithoutExtension() + "-ilegible.json"));
        return juce::Result::fail (r.error);
    }
    loadBroken = false;
    data = std::move (*r.value);
    sendChangeMessage();
    return juce::Result::ok();
}

juce::Result UserPresetStore::save() const
{
    if (file == juce::File())
        return juce::Result::ok();
    if (loadBroken)
        return juce::Result::fail ("El archivo de presets no se pudo leer; no se sobrescribe (" + lastError + ")");
    if (! file.getParentDirectory().createDirectory())
        return juce::Result::fail ("No se pudo crear " + file.getParentDirectory().getFullPathName());
    if (! file.replaceWithText (userPresetsToJson (data)))
        return juce::Result::fail ("No se pudo escribir " + file.getFullPathName());
    return juce::Result::ok();
}

const UserPointPreset* UserPresetStore::find (const juce::String& id) const
{
    for (auto& p : data.presets)
        if (p.id == id)
            return &p;
    return nullptr;
}

juce::String UserPresetStore::upsert (UserPointPreset preset)
{
    if (preset.id.isEmpty())
        preset.id = "user." + makeId();
    if (preset.name.trim().isEmpty())
        preset.name = "Preset " + juce::String ((int) data.presets.size() + 1);
    preset.content.sound = clamped (preset.content.sound);
    preset.content.shaper = clamped (preset.content.shaper);
    preset.content.effects = clamped (preset.content.effects);
    bool replaced = false;
    for (auto& p : data.presets)
        if (p.id == preset.id)
        {
            p = preset;
            replaced = true;
        }
    if (! replaced)
        data.presets.push_back (preset);
    changed();
    return preset.id;
}

juce::String UserPresetStore::addFromPoint (const juce::String& name, const juce::String& description, const Point& point)
{
    UserPointPreset p;
    p.name = name;
    p.description = description;
    p.content = point;
    p.content.id.clear();
    p.content.number = 0;
    return upsert (std::move (p));
}

bool UserPresetStore::remove (const juce::String& id)
{
    const auto before = data.presets.size();
    data.presets.erase (std::remove_if (data.presets.begin(), data.presets.end(), [&] (auto& p) { return p.id == id; }), data.presets.end());
    data.favourites.removeString (id);
    if (data.presets.size() == before)
        return false;
    changed();
    return true;
}

void UserPresetStore::setFavourite (const juce::String& id, bool on)
{
    if (on == data.favourites.contains (id))
        return;
    if (on)
        data.favourites.add (id);
    else
        data.favourites.removeString (id);
    changed();
}

void UserPresetStore::changed()
{
    if (auto r = save(); r.failed())
        lastError = r.getErrorMessage();
    sendChangeMessage();
}

} // namespace ses
