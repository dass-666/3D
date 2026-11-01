#pragma once

// Volume Shaper curve editor (Pro): the selected repetition's audio with the
// gain curve on top, a millisecond ruler, zoom, a time selection, and manual
// breakpoints (drag, add, remove, copy/paste, duplicate, mirror, undo/redo,
// A/B). Everything drawn here is the curve the engine applies (shaperGain).

#include "ui/EditorContext.h"

#include <optional>

namespace ses
{

class CurveEditor final : public juce::Component, private EditorContext::Listener
{
public:
    explicit CurveEditor (EditorContext&);
    ~CurveEditor() override;

    std::function<void()> onClose;
    std::function<void()> onExpand;        // embedded mode: "Ampliar"

    // Embedded in the Pro layout: no "Cerrar", an "Ampliar" button, toolbar wraps to the width.
    void setEmbedded (bool);
    bool isEmbedded() const noexcept { return embedded; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void visibilityChanged() override;

    // ---- actions (also used by the tests) ------------------------------
    void setManual (bool);                 // true: breakpoints (baked from the generator if there are none)
    void bakeFromGenerator();
    // «Generar cortes perfectos»: the generator (count, distribution, ramps,
    // pattern) over the time selection if there is one, else the current
    // span; the count is limited to what the ramps allow. Replaces a manual
    // curve (its points stay stored: "Manual" brings them back).
    void generatePerfectCuts();
    std::optional<std::pair<double, double>> currentSelection() const { return selection; }
    void addPoint (double t, double gain);  // t = 0..1 of the shaped span
    void removePoint (int index);
    void movePoint (int index, double t, double gain, bool commit);
    void copyCurve();
    void pasteCurve();
    void duplicateCurve();
    void mirrorCurve();
    void undo();
    void redo();
    bool canUndo() const noexcept { return historyPos > 0; }
    bool canRedo() const noexcept { return historyPos + 1 < history.size(); }
    void storeB();
    bool hasStateB() const noexcept { return stateB.has_value(); }
    void toggleAB();
    void selectionToRegion();
    void zoomBy (double factor, double aroundFraction);
    void fitView();
    void setSelection (std::optional<std::pair<double, double>>);  // fractions of the voice

    const ShaperSettings* current() const;
    int numPoints() const;
    int hoveredPoint() const noexcept { return hovered; }
    static std::optional<std::vector<CurvePoint>> curveClipboard;

    // Geometry helpers (public for the tests): fraction of the voice <-> x pixel.
    juce::Rectangle<float> canvasArea() const;
    float xForFraction (double f) const;
    double fractionAt (float x) const;
    float yForGain (double g) const;
    double gainAt (float y) const;
    double voiceSeconds() const;                              // length of the repetition's region
    std::pair<double, double> spanFractions() const;          // shaped span as fractions of the voice

private:
    void sceneChanged() override;
    void selectionChanged() override;
    void apply (const ShaperSettings&, bool record);
    void record();
    void rebuildPeaks();
    void refreshButtons();
    int pointNear (juce::Point<float>) const;
    juce::Rectangle<float> rulerArea() const;
    juce::String pointLabel (int index) const;

    EditorContext& ctx;
    bool embedded = false;
    int toolbarPx = 66;                        // height used by the toolbar rows (set in resized)
    juce::String pointId;                      // repetition being edited
    std::vector<ShaperSettings> history;
    size_t historyPos = 0;
    std::optional<ShaperSettings> stateB;
    bool showingB = false;

    double viewStart = 0.0, viewEnd = 1.0;     // fractions of the voice
    double vZoom = 1.0;
    std::optional<std::pair<double, double>> selection;
    std::vector<std::pair<float, float>> peaks;
    int hovered = -1, dragging = -1, selected = -1;
    bool draggingSelection = false;
    double dragAnchor = 0.0;
    juce::Point<float> lastMouse;

    juce::TextButton closeButton { "Cerrar" }, expandButton { "Ampliar" },
        generateButton { juce::String::fromUTF8 ("Generar cortes perfectos") }, manualButton { "Manual" }, bakeButton { "Generar puntos" },
        undoButton { "Deshacer" }, redoButton { "Rehacer" }, copyButton { "Copiar" }, pasteButton { "Pegar" },
        dupButton { "Duplicar" }, mirrorButton { "Espejo" }, previewButton { juce::String::fromUTF8 ("\xe2\x96\xb6 Escuchar") },
        storeBButton { "Guardar B" }, abButton { "A/B" }, regionButton { juce::String::fromUTF8 ("Selecci\xc3\xb3n \xe2\x86\x92 zona") },
        zoomInButton { "+" }, zoomOutButton { juce::String::fromUTF8 ("\xe2\x88\x92") }, zoomFitButton { "Todo" },
        vZoomInButton { juce::String::fromUTF8 ("\xe2\x86\x95+") }, vZoomOutButton { juce::String::fromUTF8 ("\xe2\x86\x95\xe2\x88\x92") };
    juce::Label title, info, help;
};

} // namespace ses
