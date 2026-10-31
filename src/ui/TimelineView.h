#pragma once

// WHEN each repetition sounds, in beats after the trigger (T). Order in time
// is independent of point numbers. Same model and selection as the map.

#include "ui/EditorContext.h"

namespace ses
{

class TimelineView final : public juce::Component, private EditorContext::Listener
{
public:
    explicit TimelineView (EditorContext&);
    ~TimelineView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    void setProMode (bool);
    void updatePlayhead();  // editor timer
    juce::Rectangle<float> boundsOfPoint (const juce::String& id) const
    {
        for (auto& b : blocks)
            if (b.id == id)
                return blockRect (b);
        return {};
    }

private:
    struct Block
    {
        juce::String id;
        int number = 0;
        bool enabled = true;
        double beat = 0.0, lengthBeats = 0.0;
        int lane = 0;
    };

    void sceneChanged() override { layoutBlocks(); repaint(); }
    void selectionChanged() override { repaint(); }
    void layoutBlocks();
    juce::Rectangle<float> gridArea() const;
    float xForBeat (double) const;
    double beatAt (float x) const;
    juce::Rectangle<float> blockRect (const Block&) const;
    const Block* hit (juce::Point<float>) const;
    double snap (double beat) const;

    EditorContext& ctx;
    bool pro = false;
    juce::ComboBox rhythmBox;
    juce::ToggleButton snapToggle { "Ajustar a la rejilla" };
    std::vector<Block> blocks;
    int lanes = 1;
    double lengthBeats = 8.0;

    bool dragging = false;
    double dragStartBeat = 0.0;
    std::vector<std::pair<juce::String, double>> dragOrigins;
    double playheadBeat = -1.0;
    int lastReferences = -1;
};

} // namespace ses
