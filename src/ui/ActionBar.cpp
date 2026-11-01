#include "ui/ActionBar.h"

#include "ui/PresetActions.h"
#include "ui/Theme.h"

namespace ses
{
namespace
{
juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }
}

ActionBar::ActionBar (EditorContext& c) : ctx (c)
{
    importButton.setButtonText (u8 ("\xe2\xa4\x93  Importar audio \xe2\x96\xbe"));
    importButton.setTooltip (u8 ("Cargar un WAV o capturar el audio que llega a esta pista del Mixer (\xc3\xbaltimos segundos o grabaci\xc3\xb3n)"));
    importButton.onClick = [this] { if (onImport) onImport (importButton); };

    liveButton.setButtonText (u8 ("\xe2\x97\x8f  Procesar Mixer en vivo"));
    liveButton.setTooltip (u8 ("Aplica filtros, cortes y efectos de forma continua al audio que entra en esta pista del Mixer. "
                               "Distinto de \xc2\xab" "Capturar \xc3\xbaltimos segundos\xc2\xbb, que convierte la entrada en fragmento."));
    liveButton.onClick = [this] { if (onLiveToggle) onLiveToggle(); };

    exportButton.setButtonText (u8 ("\xe2\x87\xaa  Exportar a Playlist"));
    exportButton.setColour (juce::TextButton::buttonColourId, theme::blue.withAlpha (0.85f));
    exportButton.setTooltip (u8 ("Renderiza las repeticiones a un archivo WAV para arrastrarlo a la Playlist de FL Studio"));
    exportButton.onClick = [this] { if (onExport) onExport(); };

    presetsButton.setButtonText (u8 ("\xe2\x98\x85  Mis presets \xe2\x96\xbe"));
    presetsButton.setTooltip (u8 ("Aplicar, eliminar o marcar presets (los tuyos y los integrados). Se guardan en tu usuario, no en el proyecto."));
    presetsButton.onClick = [this]
    {
        presets::buildMenu (ctx, *this, [this] (const juce::String& t) { showNote (t); })
            .showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetsButton));
    };

    savePresetButton.setButtonText (u8 ("+  Guardar preset"));
    savePresetButton.setTooltip (u8 ("Guarda el sonido, los cortes y los efectos de la repetici\xc3\xb3n seleccionada en \xc2\xabMis presets\xc2\xbb"));
    savePresetButton.onClick = [this]
    {
        presets::askNameAndSave (ctx, *this, [safe = juce::Component::SafePointer<ActionBar> (this)] (const juce::String& id)
        {
            if (safe == nullptr || id.isEmpty())
                return;
            if (id == "!")
                safe->showNote (u8 ("No se pudo guardar el preset: ") + UserPresetStore::shared().lastError, true);
            else if (const auto* up = UserPresetStore::shared().find (id))
                safe->showNote (u8 ("Preset \xc2\xab") + up->name + u8 ("\xc2\xbb guardado en Mis presets"));
        });
    };

    for (auto* b : { &importButton, &liveButton, &exportButton, &presetsButton, &savePresetButton })
        addAndMakeVisible (*b);

    note.setFont (theme::font (12.0f));
    note.setJustificationType (juce::Justification::centredRight);
    note.setColour (juce::Label::textColourId, theme::success);
    addAndMakeVisible (note);
}

void ActionBar::showNote (const juce::String& t, bool warning)
{
    note.setText (t, juce::dontSendNotification);
    note.setColour (juce::Label::textColourId, warning ? theme::amber : theme::success);
    noteTicks = 30 * 8;  // ~8 s at the editor's 30 Hz timer
}

void ActionBar::tick()
{
    if (noteTicks > 0 && --noteTicks == 0)
        note.setText ({}, juce::dontSendNotification);
}

void ActionBar::setLiveState (bool on, const juce::String& detail)
{
    liveButton.setToggleState (on, juce::dontSendNotification);
    liveButton.setColour (juce::TextButton::buttonColourId, on ? theme::success.withAlpha (0.55f) : theme::panel.brighter (0.15f));
    liveButton.setButtonText (on ? u8 ("\xe2\x97\x8f  Mixer en vivo: ACTIVO") : u8 ("\xe2\x97\x8b  Procesar Mixer en vivo"));
    liveButton.setTooltip (detail);
}

void ActionBar::paint (juce::Graphics& g)
{
    g.setColour (theme::panel.darker (0.15f));
    g.fillRect (getLocalBounds());
    g.setColour (theme::outline);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
    const int sep = presetsButton.getX() - 10;
    g.drawVerticalLine (sep, 8.0f, (float) getHeight() - 8.0f);
}

void ActionBar::resized()
{
    auto r = getLocalBounds().reduced (12, 6);
    const bool compact = getWidth() < 1200;
    auto put = [&] (juce::Button& b, int w) { b.setBounds (r.removeFromLeft (w)); r.removeFromLeft (8); };
    put (importButton, compact ? 150 : 170);
    put (liveButton, compact ? 190 : 210);
    put (exportButton, compact ? 160 : 180);
    r.removeFromLeft (12);
    put (presetsButton, compact ? 128 : 140);
    put (savePresetButton, compact ? 130 : 140);
    note.setBounds (r);
}

} // namespace ses
