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
        s.saveOk = true;
        s.diskText.clear();
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
    return readFile (true);
}

juce::Result UserPresetStore::readFile (bool notify)
{
    if (file == juce::File() || ! file.existsAsFile())
        return juce::Result::ok();
    const auto text = file.loadFileAsString();
    auto r = userPresetsFromJson (text);
    if (! r)
    {
        // Keep the user's file: a copy next to it, and no saving until a load works.
        loadBroken = true;
        lastError = r.error;
        file.copyFileTo (file.getSiblingFile (file.getFileNameWithoutExtension() + "-ilegible.json"));
        return juce::Result::fail (r.error);
    }
    loadBroken = false;
    diskText = text;
    data = std::move (*r.value);
    if (notify)
        sendChangeMessage();
    return juce::Result::ok();
}

bool UserPresetStore::refreshFromDisk()
{
    if (file == juce::File() || ! file.existsAsFile())
        return false;
    const auto text = file.loadFileAsString();
    if (text == diskText && ! loadBroken)
        return false;
    const auto before = data;
    if (readFile (false).failed())
        return false;
    if (data == before)
        return false;
    sendChangeMessage();
    return true;
}

juce::Result UserPresetStore::save() const
{
    if (file == juce::File())
        return juce::Result::ok();
    if (loadBroken)
        return juce::Result::fail ("El archivo de presets no se pudo leer; no se sobrescribe (" + lastError + ")");
    if (! file.getParentDirectory().createDirectory())
        return juce::Result::fail ("No se pudo crear la carpeta " + file.getParentDirectory().getFullPathName());
    const auto text = userPresetsToJson (data);
    if (! file.replaceWithText (text))
        return juce::Result::fail ("No se pudo escribir " + file.getFullPathName());
    diskText = file.loadFileAsString();  // as stored (line endings may differ)
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
    refreshFromDisk();
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

bool UserPresetStore::remove (const juce::String& idRef)
{
    // A copy: callers may pass presets()[i].id, which the erase (or the refresh) frees.
    const juce::String id = idRef;
    refreshFromDisk();
    const auto before = data.presets.size();
    data.presets.erase (std::remove_if (data.presets.begin(), data.presets.end(), [&] (auto& p) { return p.id == id; }), data.presets.end());
    const bool wasFavourite = data.favourites.contains (id);
    data.favourites.removeString (id);
    if (data.presets.size() == before)
    {
        if (wasFavourite)
            changed();
        return false;
    }
    changed();
    return true;
}

void UserPresetStore::setFavourite (const juce::String& idRef, bool on)
{
    const juce::String id = idRef;  // may point into `data`, which the refresh replaces
    refreshFromDisk();
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
    auto r = save();
    saveOk = r.wasOk();
    if (r.failed())
        lastError = r.getErrorMessage();
    sendChangeMessage();
}

} // namespace ses
