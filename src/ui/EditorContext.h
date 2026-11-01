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
            if (! expectingChange)
            {
                // Changed behind our back (host state restore, scene/WAV load): history no longer applies.
                undoStack.clear();
                redoStack.clear();
            }
            expectingChange = false;
            pull();
            listeners.call ([] (Listener& l) { l.sceneChanged(); });
        }
    }

    void edit (const std::function<void (Scene&)>& fn)
    {
        pushUndo();
        expectingChange = true;
        processor.editScene (fn);
        refreshIfChanged();
    }

    // ---- document undo / redo (message thread). Every edit() is a step; edits
    // closer than kCoalesceMs (slider drags) merge into one. The source audio
    // is never part of a step: undo restores points and settings only.
    static constexpr int kCoalesceMs = 500;
    static constexpr size_t kMaxHistory = 200;
    bool canUndo() const noexcept { return ! undoStack.empty(); }
    bool canRedo() const noexcept { return ! redoStack.empty(); }
    void undo()
    {
        if (undoStack.empty())
            return;
        redoStack.push_back (cached);
        auto snap = std::move (undoStack.back());
        undoStack.pop_back();
        applySnapshot (snap);
    }
    void redo()
    {
        if (redoStack.empty())
            return;
        undoStack.push_back (cached);
        auto snap = std::move (redoStack.back());
        redoStack.pop_back();
        applySnapshot (snap);
    }
    void breakUndoGroup() noexcept { lastEditMs = 0; }  // the next edit starts a new step

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
    void pushUndo()
    {
        const auto now = juce::Time::getMillisecondCounter();
        if (! undoStack.empty() && now - lastEditMs < (juce::uint32) kCoalesceMs)
        {
            lastEditMs = now;
            return;  // part of the same step
        }
        lastEditMs = now;
        undoStack.push_back (cached);
        if (undoStack.size() > kMaxHistory)
            undoStack.erase (undoStack.begin());
        redoStack.clear();
    }

    void applySnapshot (const Scene& snap)
    {
        expectingChange = true;
        processor.editScene ([snap] (Scene& s)
        {
            auto src = s.source;
            s = snap;
            s.source = src;
        });
        lastEditMs = 0;
        refreshIfChanged();
    }

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
    std::vector<Scene> undoStack, redoStack;
    juce::uint32 lastEditMs = 0;
    bool expectingChange = false;
    juce::StringArray selected;
    juce::ListenerList<Listener> listeners;
};

} // namespace ses
