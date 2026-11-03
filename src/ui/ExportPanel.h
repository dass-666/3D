#pragma once

// «Exportar a Playlist» / «Exportar audio»: options, background render with
// progress and cancel, then the rendered WAV as a tile that can be dragged out
// of the plugin window (to FL Studio's Playlist or any folder), «Abrir
// carpeta» and «Guardar una copia como…». The drag uses the operating
// system's file drag (JUCE performExternalDragDropOfFiles): whether FL Studio
// accepts it from a plugin window must be checked in FL Studio itself.

#include "ui/EditorContext.h"

namespace ses
{

class ExportPanel final : public juce::Component, private juce::Timer
{
public:
    explicit ExportPanel (EditorContext&);
    ~ExportPanel() override;

    void open();   // shows the options (keeps the last result if there is one)
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;  // click outside the card closes

    // Public for the tests.
    juce::ComboBox contentBox, formatBox, rateBox;
    juce::ToggleButton fragmentToggle;
    juce::TextButton exportButton, cancelButton, openFolderButton, saveAsButton, closeButton;
    SpatialEchoProcessor::ExportRequest currentRequest() const;
    void startExport();
    juce::File lastFile() const { return doneFile; }

    class DragTile;

private:
    void timerCallback() override;
    void refreshState();
    double rateFor (int itemId) const;

    EditorContext& ctx;
    juce::Rectangle<int> card;
    juce::Label title, contentL, formatL, rateL, info, status, help;
    double progressValue = 0.0;
    juce::ProgressBar progress { progressValue };
    std::unique_ptr<DragTile> tile;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::File doneFile;
    bool running = false;
};

} // namespace ses
