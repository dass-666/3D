#pragma once

// Main actions, the same in Fácil and Pro: «Importar audio», «Procesar Mixer
// en vivo», «Exportar a Playlist», plus «Mis presets» and «Guardar preset».
// The bar only routes clicks; the editor and PresetActions do the work.

#include "ui/EditorContext.h"

namespace ses
{

class ActionBar final : public juce::Component
{
public:
    explicit ActionBar (EditorContext&);

    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void (juce::Component& anchor)> onImport;
    std::function<void()> onLiveToggle, onExport;
    void showNote (const juce::String&, bool warning = false);  // short status text on the right
    void tick();                                                 // editor timer: fades the note
    void setLiveState (bool on, const juce::String& detail);     // look of the live button

    juce::TextButton importButton, liveButton, exportButton, presetsButton, savePresetButton;  // public for tests

private:
    EditorContext& ctx;
    juce::Label note;
    int noteTicks = 0;
};

} // namespace ses
