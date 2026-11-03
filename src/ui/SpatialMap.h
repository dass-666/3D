#pragma once

// WHERE each repetition sounds. Listener in the centre, up to 16 numbered
// points. Stereo: X = left/right pan; Y = approximate depth only.

#include "ui/EditorContext.h"

namespace ses
{

class SpatialMap final : public juce::Component, private EditorContext::Listener
{
public:
    explicit SpatialMap (EditorContext&);
    ~SpatialMap() override;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

    juce::Point<float> screenPositionOf (Position p) const { return toScreen (p); }

    // Called by the editor timer with the processor's sounding voices.
    void updateSounding();

    // Shared actions (also used by the toolbar and the point panel).
    static void addPointAt (EditorContext&, Position);
    static void duplicateSelection (EditorContext&);
    static void deleteSelection (EditorContext&);
    static void toggleMuteSelection (EditorContext&);

private:
    void sceneChanged() override { repaint(); }
    void selectionChanged() override { repaint(); }

    juce::Rectangle<float> mapArea() const;
    juce::Point<float> toScreen (Position) const;
    Position toModel (juce::Point<float>) const;
    const Point* hitTestPoint (juce::Point<float>) const;
    void showMenu (const Point&);

    EditorContext& ctx;
    bool draggingPoints = false;
    bool lasso = false;
    juce::Point<float> lastDrag;
    juce::Rectangle<float> lassoRect;
    std::vector<juce::uint64> sounding;
    float pulse = 0.0f;
};

} // namespace ses
