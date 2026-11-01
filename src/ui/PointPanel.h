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
    juce::Slider pitch, gain, posX, posY, beat, regionStart, regionEnd;
    juce::Label pitchL, gainL, posXL, posYL, beatL, regionStartL, regionEndL, presetL, soundInfo;
    bool freeTime = false;
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

    // Effects with real processing
    juce::Label effectsTitle, effectsNote, fadeInL, fadeOutL, lowpassL, highpassL;
    juce::Slider fadeIn, fadeOut, lowpass, highpass;
    juce::ToggleButton reverseToggle { juce::String::fromUTF8 ("Reverse (al rev\xc3\xa9s)") };

    // Volume Shaper
    juce::Label shaperTitle, shaperNote;
    juce::ToggleButton shaperToggle { "Activar" }, shaperWholeToggle { juce::String::fromUTF8 ("Toda la repetici\xc3\xb3n") },
                       autoLevelToggle { juce::String::fromUTF8 ("Nivel autom\xc3\xa1tico (hasta +6 dB)") };
    juce::ComboBox shaperPresetBox, shapeBox, rateBox, shaperDivisionBox, distBox;
    juce::Slider cuts, periodMs, depth, openWidth, shaperAttack, shaperRelease, accel, shaperLength, makeup,
                 shaperRegionStart, shaperRegionEnd;
    juce::TextEditor patternEdit;
    juce::Label shaperPresetL, shapeL, rateL, shaperDivisionL, distL, cutsL, periodMsL, depthL, openWidthL, shaperAttackL,
                shaperReleaseL, accelL, shaperLengthL, makeupL, patternL, shaperHint, shaperRegionStartL, shaperRegionEndL;

};

} // namespace ses
