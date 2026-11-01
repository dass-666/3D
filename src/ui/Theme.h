#pragma once

// Visual identity: very dark navy, electric blue / turquoise / violet accents.

#include <juce_gui_basics/juce_gui_basics.h>

namespace ses::theme
{
inline const juce::Colour background { 0xff0a0f1e };
inline const juce::Colour panel      { 0xff111a2e };
inline const juce::Colour panelHi    { 0xff172240 };
inline const juce::Colour outline    { 0xff253252 };
inline const juce::Colour grid       { 0xff1d2945 };
inline const juce::Colour text       { 0xffe7edf9 };
inline const juce::Colour textDim    { 0xff8d9ab8 };
inline const juce::Colour textFaint  { 0xff5a6788 };
inline const juce::Colour blue       { 0xff3d8bff };
inline const juce::Colour turquoise  { 0xff1fd3c4 };
inline const juce::Colour violet     { 0xff8b5cf6 };
inline const juce::Colour amber      { 0xfff5a524 };
inline const juce::Colour danger     { 0xfff2545b };
inline const juce::Colour success    { 0xff35d07f };

// Display colour of a point, by its number (purely visual).
juce::Colour pointColour (int number);

juce::Font font (float height, bool bold = false);

void drawPanel (juce::Graphics&, juce::Rectangle<float>, const juce::String& title = {});

class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float min, float max,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getPopupMenuFont() override;
};

} // namespace ses::theme
