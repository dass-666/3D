#pragma once

// Main window: header, audio editor (waveform + fragment), spatial map (where),
// point panel, timeline (when). Easy and Pro share the same state and engine.

#include "ui/EditorContext.h"
#include "ui/HeaderBar.h"
#include "ui/PointPanel.h"
#include "ui/SpatialMap.h"
#include "ui/Theme.h"
#include "ui/TimelineView.h"
#include "ui/WaveformView.h"

namespace ses
{

class SpatialEchoEditor final : public juce::AudioProcessorEditor,
                                private juce::Timer,
                                private EditorContext::Listener
{
public:
    explicit SpatialEchoEditor (SpatialEchoProcessor&);
    ~SpatialEchoEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    EditorContext& getContext() noexcept { return ctx; }
    SpatialMap& getMap() noexcept { return map; }
    TimelineView& getTimeline() noexcept { return timeline; }
    WaveformView& getWaveform() noexcept { return waveform; }

private:
    void timerCallback() override;
    void sceneChanged() override { refreshAudioInfo(); }
    void selectionChanged() override { refreshToolbar(); }
    void setProMode (bool);
    void chooseWav();
    void loadWav (const juce::File&);
    void refreshAudioInfo();
    void refreshToolbar();
    void refreshDiagnostics();

    SpatialEchoProcessor& processor;
    theme::LookAndFeel lnf;
    EditorContext ctx;
    juce::TooltipWindow tooltips { this, 600 };

    HeaderBar header;
    WaveformView waveform;
    SpatialMap map;
    PointPanel pointPanel;
    TimelineView timeline;

    juce::TextButton loadButton { "Cargar WAV" }, fragmentButton { juce::String::fromUTF8 ("\xe2\x96\xb6  Fragmento") },
        hostButton { juce::String::fromUTF8 ("Entrada del host \xe2\x96\xbe") };
    juce::TextButton expandButton { "Ampliar" }, zoomInButton { "+" }, zoomOutButton { juce::String::fromUTF8 ("\xe2\x88\x92") },
        zoomSelButton { "Sel." }, zoomFitButton { "Todo" };
    bool expanded = false;
    juce::String captureNote;   // last capture result, shown briefly
    int captureNoteTicks = 0;
    void showHostMenu();
    void hostCapture (const juce::Result&);
    juce::Label audioInfo, fragmentInfo;
    juce::TextButton addButton { juce::String::fromUTF8 ("+ A\xc3\xb1" "adir") }, duplicateButton { "Duplicar" },
        muteButton { "Silenciar" }, deleteButton { "Eliminar" };
    juce::Label countLabel, footer, diagnosticsLabel;
    juce::ToggleButton logToggle { juce::String::fromUTF8 ("Registro de diagn\xc3\xb3stico") };
    std::unique_ptr<juce::FileChooser> chooser;

    juce::Rectangle<int> audioPanel, mapPanel;
    int timerTicks = 0;
    int lastPendingRenders = -1;
};

} // namespace ses
