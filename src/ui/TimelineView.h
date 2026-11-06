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
        std::vector<double> sliceOffsets;  // stutter slices, beats after `beat`
        std::vector<double> sliceLengths;  // one per slice (acceleration makes them differ)
        double sliceBeats = 0.0;
        bool pitchPending = false;
        ShaperSettings shaper;             // drawn as a curve when enabled
        double voiceSeconds = 0.0;         // length of one voice, for the curve
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
    bool freeTime() const;      // scene in free time: the axis is seconds
    double unitsPerBeat() const; // 1 in beats mode, seconds per beat in free mode
    double currentBpm() const;

    EditorContext& ctx;
    bool pro = false;
    juce::ComboBox rhythmBox, modeBox;
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
