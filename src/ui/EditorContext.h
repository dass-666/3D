#pragma once

// Shared state of the editor: a cached copy of the active scene, the point
// selection, and change notifications. Map, panel and timeline all read and
// edit the same model through it, so they never disagree.

#include "plugin/PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace ses
{

class EditorContext
{
public:
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void sceneChanged() {}
        virtual void selectionChanged() {}
    };

    explicit EditorContext (SpatialEchoProcessor& p) : processor (p) { pull(); }

    SpatialEchoProcessor& processor;

    const Scene& scene() const noexcept { return cached; }

    // Call from the editor's timer: picks up changes made by the host (state restore) or another view.
    void refreshIfChanged()
    {
        if (processor.getDocumentVersion() != cachedVersion)
        {
            pull();
            listeners.call ([] (Listener& l) { l.sceneChanged(); });
        }
    }

    void edit (const std::function<void (Scene&)>& fn)
    {
        processor.editScene (fn);
        refreshIfChanged();
    }

    // ---- selection --------------------------------------------------------
    const juce::StringArray& selection() const noexcept { return selected; }
    juce::String primary() const { return selected.isEmpty() ? juce::String() : selected[selected.size() - 1]; }
    bool isSelected (const juce::String& id) const { return selected.contains (id); }

    void select (const juce::String& id, bool additive)
    {
        if (! additive)
            selected.clearQuick();
        if (additive && selected.contains (id))
            selected.removeString (id);
        else if (id.isNotEmpty())
        {
            selected.removeString (id);
            selected.add (id);  // last = primary
        }
        notifySelection();
    }

    void makePrimary (const juce::String& id)
    {
        if (! selected.contains (id) || primary() == id)
            return;
        selected.removeString (id);
        selected.add (id);
        notifySelection();
    }

    void setSelection (juce::StringArray ids)
    {
        selected = std::move (ids);
        notifySelection();
    }

    void clearSelection()
    {
        if (selected.isEmpty())
            return;
        selected.clearQuick();
        notifySelection();
    }

    const Point* primaryPoint() const { return findPoint (cached, primary()); }

    void addListener (Listener* l) { listeners.add (l); }
    void removeListener (Listener* l) { listeners.remove (l); }

private:
    void pull()
    {
        cachedVersion = processor.getDocumentVersion();
        cached = processor.getSceneForUi();
        // Drop selected ids that no longer exist.
        juce::StringArray kept;
        for (auto& id : selected)
            if (findPoint (cached, id) != nullptr)
                kept.add (id);
        if (kept != selected)
        {
            selected = kept;
            notifySelection();
        }
    }

    void notifySelection()
    {
        listeners.call ([] (Listener& l) { l.selectionChanged(); });
    }

    Scene cached;
    juce::uint32 cachedVersion = 0;
    juce::StringArray selected;
    juce::ListenerList<Listener> listeners;
};

} // namespace ses
