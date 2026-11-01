#pragma once

// Output peak meter of the plugin (dBFS, -60..0), fed by the editor timer.

#include "ui/Theme.h"

namespace ses
{

class OutputMeter final : public juce::Component
{
public:
    OutputMeter() { setOpaque (false); }

    void push (float peakDb)
    {
        level = juce::jmax (peakDb, level - 2.5f);  // ~75 dB/s release at 30 Hz
        if (peakDb > hold || --holdTicks <= 0)
        {
            hold = peakDb;
            holdTicks = 40;
        }
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (theme::panel.darker (0.3f));
        g.fillRoundedRectangle (r, 4.0f);
        auto bar = r.reduced (3.0f, 4.0f);
        auto x = [&] (float db) { return bar.getX() + bar.getWidth() * juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f); };
        juce::ColourGradient grad (theme::success, bar.getX(), 0.0f, theme::danger, bar.getRight(), 0.0f, false);
        grad.addColour (0.8, theme::amber);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (bar.withRight (x (level)), 3.0f);
        if (hold > -60.0f)
        {
            g.setColour (hold >= -0.5f ? theme::danger : theme::text);
            g.fillRect (x (hold) - 1.0f, bar.getY(), 2.0f, bar.getHeight());
        }
        g.setFont (theme::font (9.0f));
        g.setColour (theme::textFaint);
        for (float db : { -48.0f, -36.0f, -24.0f, -12.0f, -6.0f, 0.0f })
            g.drawText (juce::String ((int) db), juce::Rectangle<float> (x (db) - 16.0f, r.getBottom() - 11.0f, 32.0f, 10.0f),
                        juce::Justification::centred);
    }

private:
    float level = -100.0f, hold = -100.0f;
    int holdTicks = 0;
};

} // namespace ses
