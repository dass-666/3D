#pragma once

#include "ui/EditorContext.h"

namespace ses
{

class HeaderBar final : public juce::Component, private EditorContext::Listener
{
public:
    explicit HeaderBar (EditorContext&);
    ~HeaderBar() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void updateTransport();  // editor timer
    void setProMode (bool);

    std::function<void (bool pro)> onModeChange;
    std::function<void (int percent)> onScaleChange;

private:
    void sceneChanged() override;
    void selectionChanged() override { refreshHistory(); }
    void refreshHistory();
    void chooseSave();
    void chooseLoad();

    EditorContext& ctx;
    juce::TextButton easyButton { juce::String::fromUTF8 ("F\xc3\xa1" "cil") }, proButton { "Pro" };
    juce::Label sceneName;
    juce::TextButton sceneMenu { juce::String::fromUTF8 ("Escena \xe2\x96\xbe") };
    juce::TextButton triggerButton { "Disparar" }, previewButton { "Probar" };
    juce::TextButton undoButton { juce::String::fromUTF8 ("\xe2\x86\xb6") }, redoButton { juce::String::fromUTF8 ("\xe2\x86\xb7") };
    juce::Slider mix, output;
    juce::Label mixLabel, outputLabel;
    juce::ToggleButton bypass { "Bypass" }, protect { juce::String::fromUTF8 ("Protecci\xc3\xb3n") };

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SliderAttachment> mixAttachment, outputAttachment;
    std::unique_ptr<ButtonAttachment> bypassAttachment, protectAttachment;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::String bpmText = "--";
    bool playing = false, hostTimeline = false;
    float limiterDb = 0.0f;
    juce::Rectangle<int> transportArea;
    bool compactLogo = false;
};

} // namespace ses
