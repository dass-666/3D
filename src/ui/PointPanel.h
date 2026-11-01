#pragma once

// Editor of the selected repetition (the primary selection). Controls whose
// processing does not exist yet are shown as stored values and labelled,
// never as working controls. Scrolls when the window is small.

#include "ui/EditorContext.h"

#include <map>

namespace ses
{

// Clickable section title with a chevron: click folds/unfolds the section.
class SectionHeader final : public juce::Component
{
public:
    SectionHeader() { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void setText (const juce::String& t, juce::NotificationType = juce::dontSendNotification) { text = t; repaint(); }
    void setFont (const juce::Font& f) { font = f; }
    void setColour (int, juce::Colour c) { colour = c; }
    void setOpen (bool o) { open = o; repaint(); }
    bool isOpen() const noexcept { return open; }
    std::function<void()> onToggle;
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colours::white.withAlpha (isMouseOver() ? 0.06f : 0.03f));
        g.fillRoundedRectangle (r, 4.0f);
        juce::Path chev;
        const auto c = r.removeFromLeft (16.0f).getCentre();
        if (open)
            chev.addTriangle (c.x - 4.0f, c.y - 2.0f, c.x + 4.0f, c.y - 2.0f, c.x, c.y + 3.0f);
        else
            chev.addTriangle (c.x - 2.0f, c.y - 4.0f, c.x - 2.0f, c.y + 4.0f, c.x + 3.0f, c.y);
        g.setColour (colour);
        g.fillPath (chev);
        g.setFont (font);
        g.drawText (text, r.withTrimmedLeft (4.0f), juce::Justification::centredLeft, true);
    }
    void mouseDown (const juce::MouseEvent&) override { open = ! open; repaint(); if (onToggle) onToggle(); }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
private:
    juce::String text;
    juce::Font font { juce::FontOptions (12.0f) };
    juce::Colour colour { juce::Colours::white };
    bool open = true;
};

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
    std::function<void()> onOpenCurveEditor;                 // "Editor de curva" button (Pro)
    void showSection (const juce::String& categoryId);     // expands the section and scrolls to it

private:
    void sceneChanged() override { refresh(); }
    void selectionChanged() override { refresh(); }
    void refresh();
    void editPrimary (const std::function<void (Scene&, Point&)>&);
    void editSelection (const std::function<void (Scene&, Point&)>&);
    void setupSlider (juce::Slider&, juce::Label&, const juce::String& name, double lo, double hi, double step,
                      const juce::String& suffix);
    int layoutContent (int width);  // returns the height used
    SectionHeader* headerFor (const juce::String& categoryId);
    std::map<juce::String, int> sectionY;   // content y of each section header (for showSection)

    EditorContext& ctx;
    bool pro = false;

    juce::Viewport viewport;
    juce::Component content;

    juce::Label title, presetNote, pendingNote, emptyNote, pitchStatus;
    juce::ComboBox presetBox, creativeBox;
    juce::Slider pitch, formant, gain, posX, posY, beat, regionStart, regionEnd;
    juce::Label pitchL, formantL, gainL, posXL, posYL, beatL, regionStartL, regionEndL, presetL, creativeL, soundInfo;
    bool freeTime = false;
    juce::TextButton triggerButton { "Disparar esta" };
    juce::ToggleButton enabledToggle { "Activa" }, soloToggle { "Solo" };
    juce::TextButton duplicateButton { "Duplicar" }, deleteButton { "Eliminar" };

    // Stutter
    SectionHeader soundTitle, timeTitle, spaceTitle, stutterTitle;
    juce::Label stutterNote;
    juce::ToggleButton stutterToggle { "Activar" };
    juce::ComboBox regionBox, divisionBox;
    juce::Slider repeats, sliceLength, offset, stutterGain, stutterAccel, manualStart, manualEnd;
    juce::Label regionL, divisionL, repeatsL, sliceLengthL, offsetL, stutterGainL, stutterAccelL, manualStartL, manualEndL;
    juce::TextButton copyButton { "Copiar Stutter" }, pasteButton { "Pegar Stutter" };

    // Effects with real processing
    SectionHeader effectsTitle;
    juce::Label effectsNote, fadeInL, fadeOutL, lowpassL, highpassL;
    juce::Slider fadeIn, fadeOut, lowpass, highpass;
    juce::ToggleButton reverseToggle { juce::String::fromUTF8 ("Reverse (al rev\xc3\xa9s)") };
    // Effect chain (character -> modulation -> delay -> reverb)
    SectionHeader charTitle, modTitle, delayTitle, reverbTitle;
    juce::Label chainNote;
    juce::ToggleButton charToggle { "Activar" }, modToggle { "Activar" }, delayToggle { "Activar" }, reverbToggle { "Activar" },
                       delaySyncToggle { "Sincronizar al BPM" }, pingPongToggle { "Ping-pong" };
    juce::ComboBox charTypeBox, modTypeBox, delayDivisionBox, reverbTypeBox;
    juce::Slider charAmount, charMix, modRate, modDepth, modMix, delayTime, delayFeedback, delayLowpass, delayMix,
                 reverbSize, reverbDecay, reverbPredelay, reverbMix, reverbOutput;
    juce::Label charTypeL, charAmountL, charMixL, modTypeL, modRateL, modDepthL, modMixL, delayTimeL, delayDivisionL,
                delayFeedbackL, delayLowpassL, delayMixL, reverbTypeL, reverbSizeL, reverbDecayL, reverbPredelayL,
                reverbMixL, reverbOutputL;

    // Volume Shaper
    SectionHeader shaperTitle;
    juce::Label shaperNote;
    juce::TextButton curveEditorButton { juce::String::fromUTF8 ("Editor de curva\xe2\x80\xa6") };
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
