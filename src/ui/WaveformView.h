#pragma once

// Real waveform of the loaded WAV with a draggable start/end selection (the
// fragment that every repetition plays). Mouse wheel zooms, Shift+wheel scrolls.

#include "ui/EditorContext.h"

namespace ses
{

class WaveformView final : public juce::Component,
                           public juce::FileDragAndDropTarget,
                           private EditorContext::Listener
{
public:
    explicit WaveformView (EditorContext&);
    ~WaveformView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

    std::function<void (const juce::File&)> onFileDropped;

    // Current selection in source samples (end exclusive).
    juce::Range<juce::int64> selectionSamples() const;
    float xForSample (juce::int64 s) const { return xFor (s); }

private:
    enum class Drag { None, Start, End, New };

    void sceneChanged() override;
    void syncFromScene();
    void rebuildPeaks();
    float xFor (juce::int64 sample) const;
    juce::int64 sampleAt (float x) const;
    juce::Rectangle<float> waveArea() const;

    EditorContext& ctx;
    std::shared_ptr<const DecodedAudio> audio;
    juce::int64 total = 0;
    juce::int64 viewStart = 0, viewEnd = 0;
    juce::int64 selStart = 0, selEnd = 0;
    std::vector<std::pair<float, float>> peaks;  // per pixel, min/max of all channels
    Drag drag = Drag::None;
    juce::int64 dragAnchor = 0;
};

} // namespace ses
