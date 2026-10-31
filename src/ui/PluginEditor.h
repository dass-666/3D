#pragma once

// Minimal B2 editor: load a WAV, preview, and the global controls. The Easy
// Mode map and timeline replace this in later blocks.

#include "plugin/PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace ses
{

class SpatialEchoEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SpatialEchoEditor (SpatialEchoProcessor&);
    ~SpatialEchoEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void chooseFile();
    void refreshStatus();

    SpatialEchoProcessor& processor;

    juce::TextButton loadButton { "Cargar WAV..." };
    juce::TextButton previewButton { "Vista previa" };
    juce::TextButton triggerButton { "Disparar" };
    juce::Label statusLabel, noteLabel, mixLabel, outputLabel;
    juce::Slider mixSlider, outputSlider;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<SliderAttachment> mixAttachment, outputAttachment;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpatialEchoEditor)
};

} // namespace ses
