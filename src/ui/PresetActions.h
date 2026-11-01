#pragma once

// Creative presets as one set of actions shared by every view (action bar
// «Mis presets», Pro sidebar card, point panel combo): list, apply to the
// selection, save the selected repetition as a user preset, delete, favourites.
// Message thread only.

#include "model/UserPresets.h"
#include "ui/EditorContext.h"

namespace ses::presets
{

struct Entry { juce::String id, name, description; bool user = false; };

// Built-in creative presets first, then the user's.
std::vector<Entry> all();
// Applies a built-in or user preset to every selected repetition. False if the id is unknown.
bool applyToSelection (EditorContext&, const juce::String& id);
// The repetition a new preset is taken from: the primary selection, else the first point.
const Point* sourcePoint (const EditorContext&);
// Saves the source point as a user preset named `name`; marks the point with it. Returns the id (empty on failure).
juce::String saveFromSelection (EditorContext&, const juce::String& name);
// Shows the name dialog, then saves. `done` receives the new id (empty = cancelled or failed).
void askNameAndSave (EditorContext&, juce::Component& parent, std::function<void (const juce::String& id)> done = {});
// Full «Mis presets» menu: apply (Favoritos / Propios / Integrados), save, delete, favourite, open folder.
juce::PopupMenu buildMenu (EditorContext&, juce::Component& parent, std::function<void (const juce::String& note)> onNote = {});

} // namespace ses::presets
