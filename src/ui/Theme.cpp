#include "ui/Theme.h"

namespace ses::theme
{

juce::Colour pointColour (int number)
{
    static const juce::Colour palette[] {
        juce::Colour (0xff3d8bff), juce::Colour (0xff1fd3c4), juce::Colour (0xff8b5cf6), juce::Colour (0xffff6bb5),
        juce::Colour (0xfff5a524), juce::Colour (0xff35d07f), juce::Colour (0xff5ec8ff), juce::Colour (0xffc084fc),
    };
    return palette[(size_t) (juce::jmax (1, number) - 1) % std::size (palette)];
}

juce::Font font (float height, bool bold)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

void drawPanel (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title)
{
    g.setColour (panel);
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (outline);
    g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);
    if (title.isNotEmpty())
    {
        g.setColour (textDim);
        g.setFont (font (11.5f, true));
        g.drawText (title, r.reduced (14.0f, 8.0f).removeFromTop (16.0f), juce::Justification::centredLeft);
    }
}

LookAndFeel::LookAndFeel()
{
   #if JUCE_WINDOWS
    setDefaultSansSerifTypefaceName ("Segoe UI");
   #endif
    setColour (juce::ResizableWindow::backgroundColourId, background);
    setColour (juce::TextButton::buttonColourId, panelHi);
    setColour (juce::TextButton::buttonOnColourId, blue);
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    setColour (juce::Label::textColourId, text);
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, background.withAlpha (0.6f));
    setColour (juce::Slider::thumbColourId, turquoise);
    setColour (juce::Slider::trackColourId, blue);
    setColour (juce::Slider::backgroundColourId, outline);
    setColour (juce::Slider::rotarySliderFillColourId, turquoise);
    setColour (juce::Slider::rotarySliderOutlineColourId, outline);
    setColour (juce::ComboBox::backgroundColourId, panelHi);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::outlineColourId, outline);
    setColour (juce::ComboBox::arrowColourId, textDim);
    setColour (juce::PopupMenu::backgroundColourId, panelHi);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, blue.withAlpha (0.35f));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::ToggleButton::textColourId, text);
    setColour (juce::ToggleButton::tickColourId, turquoise);
    setColour (juce::TextEditor::backgroundColourId, background);
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::TextEditor::outlineColourId, outline);
    setColour (juce::TextEditor::focusedOutlineColourId, blue);
    setColour (juce::TextEditor::highlightColourId, blue.withAlpha (0.4f));
    setColour (juce::CaretComponent::caretColourId, turquoise);
    setColour (juce::TooltipWindow::backgroundColourId, panelHi);
    setColour (juce::TooltipWindow::textColourId, text);
    setColour (juce::TooltipWindow::outlineColourId, outline);
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    auto base = b.findColour (on ? juce::TextButton::buttonOnColourId : juce::TextButton::buttonColourId);
    if (! b.isEnabled())
        base = base.withMultipliedAlpha (0.4f);
    else if (down)
        base = base.brighter (0.25f);
    else if (highlighted)
        base = base.brighter (0.12f);
    g.setColour (base);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (on ? base.brighter (0.4f) : outline.brighter (highlighted ? 0.3f : 0.0f));
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
}

juce::Font LookAndFeel::getTextButtonFont (juce::TextButton&, int h)
{
    return font (juce::jmin (14.0f, (float) h * 0.48f), true);
}

void LookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    auto r = b.getLocalBounds().toFloat();
    const bool on = b.getToggleState();
    auto pill = r.removeFromLeft (34.0f).withSizeKeepingCentre (30.0f, 16.0f);
    g.setColour (on ? turquoise.withAlpha (b.isEnabled() ? 0.9f : 0.3f) : outline.brighter (highlighted ? 0.2f : 0.0f));
    g.fillRoundedRectangle (pill, 8.0f);
    g.setColour (juce::Colours::white.withAlpha (b.isEnabled() ? 1.0f : 0.4f));
    g.fillEllipse ((on ? pill.getRight() - 14.0f : pill.getX() + 2.0f), pill.getY() + 2.0f, 12.0f, 12.0f);
    g.setColour (b.isEnabled() ? text : textFaint);
    g.setFont (font (13.0f));
    g.drawText (b.getButtonText(), r.withTrimmedLeft (4.0f), juce::Justification::centredLeft);
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (4.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
    const auto c = bounds.getCentre();
    const float angle = start + pos * (end - start);
    const float stroke = 4.0f;

    juce::Path track;
    track.addCentredArc (c.x, c.y, radius - stroke, radius - stroke, 0.0f, start, end, true);
    g.setColour (outline);
    g.strokePath (track, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path value;
    value.addCentredArc (c.x, c.y, radius - stroke, radius - stroke, 0.0f, start, angle, true);
    juce::ColourGradient grad (blue, c.x - radius, c.y, turquoise, c.x + radius, c.y, false);
    g.setGradientFill (grad);
    if (s.isEnabled())
        g.strokePath (value, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour (panelHi);
    g.fillEllipse (juce::Rectangle<float> (radius * 1.2f, radius * 1.2f).withCentre (c));
    juce::Path tick;
    tick.addRoundedRectangle (-1.5f, -radius * 0.58f, 3.0f, radius * 0.3f, 1.5f);
    g.setColour (text);
    g.fillPath (tick, juce::AffineTransform::rotation (angle).translated (c.x, c.y));
}

void LookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                    juce::Slider::SliderStyle style, juce::Slider& s)
{
    if (style != juce::Slider::LinearHorizontal)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0, 0, style, s);
        return;
    }
    const auto r = juce::Rectangle<int> (x, y, w, h).toFloat();
    const float cy = r.getCentreY();
    auto track = juce::Rectangle<float> (r.getX(), cy - 2.0f, r.getWidth(), 4.0f);
    g.setColour (outline);
    g.fillRoundedRectangle (track, 2.0f);
    if (s.isEnabled())
    {
        juce::ColourGradient grad (blue, r.getX(), cy, turquoise, r.getRight(), cy, false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (track.withRight (pos), 2.0f);
    }
    g.setColour (s.isEnabled() ? juce::Colours::white : textFaint);
    g.fillEllipse (juce::Rectangle<float> (14.0f, 14.0f).withCentre ({ pos, cy }));
    if (s.isEnabled())
    {
        g.setColour (turquoise);
        g.drawEllipse (juce::Rectangle<float> (14.0f, 14.0f).withCentre ({ pos, cy }), 2.0f);
    }
}

void LookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<int> (0, 0, w, h).toFloat().reduced (0.5f);
    g.setColour (panelHi.withMultipliedAlpha (box.isEnabled() ? 1.0f : 0.5f));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (outline);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
    juce::Path arrow;
    const float ax = (float) w - 16.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (textDim);
    g.fillPath (arrow);
}

juce::Label* LookAndFeel::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::textColourId, text);
    l->setFont (font (12.5f, true));
    return l;
}

juce::Font LookAndFeel::getComboBoxFont (juce::ComboBox&) { return font (13.5f); }
juce::Font LookAndFeel::getLabelFont (juce::Label& l) { return l.getFont(); }
juce::Font LookAndFeel::getPopupMenuFont() { return font (14.0f); }

} // namespace ses::theme
