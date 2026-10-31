#pragma once

// Editor of the selected repetition (the primary selection). Controls whose
// processing does not exist yet are disabled and labelled, never faked.

#include "ui/EditorContext.h"

namespace ses
{

class PointPanel final : public juce::Component, private EditorContext::Listener
{
public:
    explicit PointPanel (EditorContext&);
    ~PointPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void setProMode (bool);

private:
    void sceneChanged() override { refresh(); }
    void selectionChanged() override { refresh(); }
    void refresh();
    void editPrimary (const std::function<void (Scene&, Point&)>&);
    void setupSlider (juce::Slider&, juce::Label&, const juce::String& name, double lo, double hi, double step,
                      const juce::String& suffix);

    EditorContext& ctx;
    bool pro = false;

    juce::Label title, presetNote, pendingNote, emptyNote;
    juce::ComboBox presetBox;
    juce::Slider volume, posX, posY, beat;
    juce::Label volumeL, posXL, posYL, beatL, presetL, soundInfo;
    juce::ToggleButton enabledToggle { "Activa" }, soloToggle { "Solo" };
    juce::TextButton duplicateButton { "Duplicar" }, deleteButton { "Eliminar" };
};

} // namespace ses
