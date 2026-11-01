#pragma once

// Editor of the selected repetition (the primary selection). Controls whose
// processing does not exist yet are shown as stored values and labelled,
// never as working controls. Scrolls when the window is small.

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
    void refreshPending();  // editor timer: pitch render status

    static std::optional<StutterSettings> stutterClipboard;  // "copy settings between points"

private:
    void sceneChanged() override { refresh(); }
    void selectionChanged() override { refresh(); }
    void refresh();
    void editPrimary (const std::function<void (Scene&, Point&)>&);
    void editSelection (const std::function<void (Scene&, Point&)>&);
    void setupSlider (juce::Slider&, juce::Label&, const juce::String& name, double lo, double hi, double step,
                      const juce::String& suffix);
    int layoutContent (int width);  // returns the height used

    EditorContext& ctx;
    bool pro = false;

    juce::Viewport viewport;
    juce::Component content;

    juce::Label title, presetNote, pendingNote, emptyNote, pitchStatus;
    juce::ComboBox presetBox;
    juce::Slider pitch, gain, posX, posY, beat;
    juce::Label pitchL, gainL, posXL, posYL, beatL, presetL, soundInfo;
    juce::TextButton triggerButton { "Disparar esta" };
    juce::ToggleButton enabledToggle { "Activa" }, soloToggle { "Solo" };
    juce::TextButton duplicateButton { "Duplicar" }, deleteButton { "Eliminar" };

    // Stutter
    juce::Label stutterTitle, stutterNote;
    juce::ToggleButton stutterToggle { "Activar" };
    juce::ComboBox regionBox, divisionBox;
    juce::Slider repeats, sliceLength, offset, stutterGain, manualStart, manualEnd;
    juce::Label regionL, divisionL, repeatsL, sliceLengthL, offsetL, stutterGainL, manualStartL, manualEndL;
    juce::TextButton copyButton { "Copiar Stutter" }, pasteButton { "Pegar Stutter" };

};

} // namespace ses
