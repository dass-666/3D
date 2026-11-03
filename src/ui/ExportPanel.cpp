#include "ui/ExportPanel.h"

#include "ui/Theme.h"

namespace ses
{
namespace
{
juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }
using State = SpatialEchoProcessor::ExportStatus::State;
}

// The rendered file, draggable out of the plugin window.
class ExportPanel::DragTile final : public juce::Component
{
public:
    void setFile (const juce::File& f, const juce::String& detail)
    {
        file = f;
        text = detail;
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (theme::blue.withAlpha (hover ? 0.35f : 0.22f));
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (theme::blue);
        g.drawRoundedRectangle (r, 8.0f, 1.5f);
        // Little waveform icon.
        auto icon = r.removeFromLeft (r.getHeight()).reduced (12.0f);
        g.setColour (theme::turquoise);
        for (int i = 0; i < 7; ++i)
        {
            const float h = icon.getHeight() * (0.3f + 0.7f * std::abs (std::sin ((float) i * 1.3f)));
            g.fillRoundedRectangle (icon.getX() + (float) i * icon.getWidth() / 7.0f, icon.getCentreY() - h / 2.0f, icon.getWidth() / 10.0f, h, 1.5f);
        }
        g.setColour (theme::text);
        g.setFont (theme::font (14.0f, true));
        g.drawText (u8 ("\xe2\x87\xb2  Arrastra este clip a la Playlist"), r.removeFromTop (r.getHeight() * 0.5f).withTrimmedLeft (4.0f),
                    juce::Justification::bottomLeft);
        g.setColour (theme::textDim);
        g.setFont (theme::font (12.0f));
        g.drawText (file.getFileName() + "   " + text, r.withTrimmedLeft (4.0f), juce::Justification::topLeft, true);
    }
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging || ! file.existsAsFile() || e.getDistanceFromDragStart() < 6)
            return;
        dragging = true;
        // Copy semantics (canMove = false): the file stays in the export folder,
        // which FL Studio then references from the project.
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this,
                                                                   [safe = juce::Component::SafePointer<DragTile> (this)]
                                                                   { if (safe != nullptr) safe->dragging = false; });
    }
    void mouseUp (const juce::MouseEvent&) override { dragging = false; }

private:
    juce::File file;
    juce::String text;
    bool hover = false, dragging = false;
};

ExportPanel::ExportPanel (EditorContext& c) : ctx (c)
{
    setInterceptsMouseClicks (true, true);
    title.setText (u8 ("Exportar audio procesado (.wav)"), juce::dontSendNotification);
    title.setFont (theme::font (17.0f, true));
    addAndMakeVisible (title);

    auto label = [this] (juce::Label& l, const char* t)
    {
        l.setText (u8 (t), juce::dontSendNotification);
        l.setColour (juce::Label::textColourId, theme::textDim);
        addAndMakeVisible (l);
    };
    label (contentL, "Contenido");
    label (formatL, "Formato");
    label (rateL, "Frecuencia");

    contentBox.addItem (u8 ("Mezcla completa (todas las repeticiones activas)"), 1);
    contentBox.addItem (u8 ("Solo la repetici\xc3\xb3n seleccionada"), 2);
    contentBox.setSelectedId (1, juce::dontSendNotification);
    formatBox.addItem (u8 ("WAV 24 bits PCM (recomendado)"), 24);
    formatBox.addItem (u8 ("WAV 16 bits PCM (con dither)"), 16);
    formatBox.addItem (u8 ("WAV 32 bits coma flotante"), 32);
    formatBox.setSelectedId (24, juce::dontSendNotification);
    rateBox.addItem ("Proyecto", 1);
    for (int r : { 44100, 48000, 88200, 96000 })
        rateBox.addItem (juce::String (r / 1000.0, 1) + " kHz", r);
    rateBox.setSelectedId (1, juce::dontSendNotification);
    for (auto* b : { &contentBox, &formatBox, &rateBox })
    {
        b->onChange = [this] { refreshState(); };
        addAndMakeVisible (*b);
    }
    fragmentToggle.setButtonText (u8 ("Incluir el fragmento original al inicio (si no, solo las repeticiones)"));
    fragmentToggle.onClick = [this] { refreshState(); };
    addAndMakeVisible (fragmentToggle);

    info.setFont (theme::font (12.0f));
    info.setColour (juce::Label::textColourId, theme::textDim);
    info.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (info);

    exportButton.setButtonText ("Exportar");
    exportButton.setColour (juce::TextButton::buttonColourId, theme::blue);
    exportButton.onClick = [this] { startExport(); };
    cancelButton.setButtonText ("Cancelar");
    cancelButton.onClick = [this] { ctx.processor.cancelExport(); };
    openFolderButton.setButtonText (u8 ("Abrir carpeta del audio"));
    openFolderButton.setTooltip (u8 ("Muestra el archivo en el Explorador: desde ah\xc3\xad se puede arrastrar a la Playlist"));
    openFolderButton.onClick = [this]
    {
        if (doneFile.existsAsFile())
            doneFile.revealToUser();
        else
            SpatialEchoProcessor::exportFolder().startAsProcess();
    };
    saveAsButton.setButtonText (u8 ("Guardar una copia como\xe2\x80\xa6"));
    saveAsButton.onClick = [this]
    {
        if (! doneFile.existsAsFile())
            return;
        chooser = std::make_unique<juce::FileChooser> (u8 ("Guardar una copia del audio"), doneFile, "*.wav");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                              [safe = juce::Component::SafePointer<ExportPanel> (this)] (const juce::FileChooser& fc)
                              {
                                  if (safe == nullptr || fc.getResult() == juce::File())
                                      return;
                                  const auto dest = fc.getResult().withFileExtension ("wav");
                                  if (dest != safe->doneFile && ! safe->doneFile.copyFileTo (dest))
                                      safe->status.setText (u8 ("No se pudo copiar a ") + dest.getFullPathName(), juce::dontSendNotification);
                              });
    };
    closeButton.setButtonText ("Cerrar");
    closeButton.onClick = [this] { setVisible (false); };
    for (auto* b : { &exportButton, &cancelButton, &openFolderButton, &saveAsButton, &closeButton })
        addAndMakeVisible (*b);

    progress.setColour (juce::ProgressBar::foregroundColourId, theme::turquoise);
    progress.setColour (juce::ProgressBar::backgroundColourId, theme::background);
    addAndMakeVisible (progress);
    status.setFont (theme::font (12.5f, true));
    status.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (status);

    tile = std::make_unique<DragTile>();
    addChildComponent (*tile);

    help.setFont (theme::font (11.5f));
    help.setColour (juce::Label::textColourId, theme::textDim);
    help.setJustificationType (juce::Justification::topLeft);
    help.setText (u8 ("El archivo empieza en el momento del disparo: col\xc3\xb3" "calo en la Playlist donde pulsas \xc2\xab" "Disparar\xc2\xbb. "
                      "Si FL Studio no acepta el arrastre desde esta ventana, pulsa \xc2\xab" "Abrir carpeta del audio\xc2\xbb y arrastra el archivo "
                      "desde el Explorador de Windows a la Playlist. No borres la carpeta Exportaciones: FL Studio enlaza el archivo, no lo copia."),
                  juce::dontSendNotification);
    addAndMakeVisible (help);
}

ExportPanel::~ExportPanel()
{
    stopTimer();
}

void ExportPanel::open()
{
    setVisible (true);
    toFront (true);
    refreshState();
    startTimerHz (15);
}

double ExportPanel::rateFor (int id) const
{
    return id <= 1 ? ctx.processor.getProjectSampleRate() : (double) id;
}

SpatialEchoProcessor::ExportRequest ExportPanel::currentRequest() const
{
    SpatialEchoProcessor::ExportRequest rq;
    const int f = formatBox.getSelectedId();
    rq.format = f == 16 ? WavFormat::Pcm16 : f == 32 ? WavFormat::Float32 : WavFormat::Pcm24;
    rq.sampleRate = rateFor (rateBox.getSelectedId());
    rq.onlyPointId = contentBox.getSelectedId() == 2 ? ctx.primary() : juce::String();
    if (rq.onlyPointId == kLivePointId)
        rq.onlyPointId = {};
    rq.includeFragment = fragmentToggle.getToggleState();
    rq.file = ctx.processor.suggestExportFile (rq.onlyPointId);
    return rq;
}

void ExportPanel::startExport()
{
    const auto rq = currentRequest();
    const auto r = ctx.processor.startExport (rq);
    if (r.failed())
    {
        status.setColour (juce::Label::textColourId, theme::amber);
        status.setText (r.getErrorMessage(), juce::dontSendNotification);
        return;
    }
    doneFile = juce::File();
    tile->setVisible (false);
    running = true;
    startTimerHz (15);
    refreshState();
}

void ExportPanel::timerCallback()
{
    if (! isVisible() && ! running)
    {
        stopTimer();
        return;
    }
    refreshState();
}

void ExportPanel::refreshState()
{
    const auto st = ctx.processor.getExportStatus();
    running = st.state == State::Running;
    progressValue = running ? st.progress : (st.state == State::Done ? 1.0 : 0.0);

    const bool single = contentBox.getSelectedId() == 2;
    const auto* pt = ctx.primaryPoint();
    const bool singleOk = ! single || (pt != nullptr && pt->id != kLivePointId);
    juce::String i;
    i << "Tempo: " << juce::String (ctx.processor.getProjectBpm(), 1) << " BPM"
      << (ctx.processor.getHostSnapshot().hasBpm.load() ? u8 (" (del proyecto)") : u8 (" (sin tempo del host: 120 por defecto)"))
      << "   \xc2\xb7   " << juce::String (rateFor (rateBox.getSelectedId()) / 1000.0, 1) << " kHz" << u8 ("   \xc2\xb7   est\xc3\xa9reo\n");
    i << u8 ("Incluye: pitch, formantes, cortes, Stutter, efectos, paneo, Mix, Salida y Protecci\xc3\xb3n, con las colas de Delay y Reverb.\n");
    if (single)
        i << (singleOk ? u8 ("Repetici\xc3\xb3n ") + juce::String (pt->number) + u8 (": empieza en su propio momento, como \xc2\xab" "Escuchar este punto\xc2\xbb.")
                       : u8 ("Selecciona una repetici\xc3\xb3n en el mapa o en la l\xc3\xadnea temporal."));
    else
        i << u8 ("Carpeta: ") << SpatialEchoProcessor::exportFolder().getFullPathName();
    info.setText (i, juce::dontSendNotification);

    exportButton.setEnabled (! running && singleOk && ctx.processor.getSourceStatus().loaded);
    cancelButton.setEnabled (running);
    for (auto* c : { (juce::Component*) &contentBox, (juce::Component*) &formatBox, (juce::Component*) &rateBox, (juce::Component*) &fragmentToggle })
        c->setEnabled (! running);

    juce::String s;
    auto colour = theme::textDim;
    switch (st.state)
    {
        case State::Idle: s = ctx.processor.getSourceStatus().loaded ? juce::String() : u8 ("Primero importa audio (WAV o captura del Mixer)."); break;
        case State::Running: s = u8 ("Renderizando\xe2\x80\xa6 ") + juce::String (juce::roundToInt (st.progress * 100.0)) + " %"; break;
        case State::Cancelled: s = st.message; colour = theme::amber; break;
        case State::Failed: s = st.message; colour = theme::danger; break;
        case State::Done:
            s = u8 ("Listo: ") + juce::String (st.seconds, 2) + " s, pico " + juce::String (st.peakDb, 1) + " dBFS";
            if (st.clipped)
            {
                s << u8 ("  \xe2\x9a\xa0 picos por encima de 0 dBFS recortados: activa Protecci\xc3\xb3n, baja Salida o usa 32 bits float");
                colour = theme::amber;
            }
            else
            {
                colour = theme::success;
            }
            if (st.truncated)
                s << u8 ("  (cortado en el l\xc3\xadmite de duraci\xc3\xb3n)");
            if (doneFile != st.file)
            {
                doneFile = st.file;
                tile->setFile (doneFile, juce::String (st.seconds, 2) + " s");
            }
            break;
    }
    status.setColour (juce::Label::textColourId, colour);
    status.setText (s, juce::dontSendNotification);
    const bool done = st.state == State::Done && doneFile.existsAsFile();
    tile->setVisible (done);
    openFolderButton.setEnabled (true);
    saveAsButton.setEnabled (done);
    repaint();
}

void ExportPanel::mouseDown (const juce::MouseEvent& e)
{
    if (! card.contains (e.getPosition()) && ! running)
        setVisible (false);
}

void ExportPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.55f));
    theme::drawPanel (g, card.toFloat());
}

void ExportPanel::resized()
{
    card = getLocalBounds().withSizeKeepingCentre (juce::jmin (640, getWidth() - 40), juce::jmin (520, getHeight() - 40));
    auto r = card.reduced (20, 16);
    title.setBounds (r.removeFromTop (28));
    r.removeFromTop (8);
    auto row = [&] (juce::Label& l, juce::Component& c)
    {
        auto x = r.removeFromTop (28);
        l.setBounds (x.removeFromLeft (100));
        c.setBounds (x);
        r.removeFromTop (6);
    };
    row (contentL, contentBox);
    row (formatL, formatBox);
    row (rateL, rateBox);
    fragmentToggle.setBounds (r.removeFromTop (26));
    r.removeFromTop (4);
    info.setBounds (r.removeFromTop (62));
    r.removeFromTop (6);
    {
        auto b = r.removeFromTop (34);
        exportButton.setBounds (b.removeFromLeft (140));
        b.removeFromLeft (8);
        cancelButton.setBounds (b.removeFromLeft (100));
        b.removeFromLeft (12);
        progress.setBounds (b.withSizeKeepingCentre (b.getWidth(), 18));
    }
    r.removeFromTop (6);
    status.setBounds (r.removeFromTop (34));
    tile->setBounds (r.removeFromTop (62));
    r.removeFromTop (8);
    {
        auto b = r.removeFromTop (30);
        openFolderButton.setBounds (b.removeFromLeft (190));
        b.removeFromLeft (8);
        saveAsButton.setBounds (b.removeFromLeft (190));
        closeButton.setBounds (b.removeFromRight (90));
    }
    r.removeFromTop (8);
    help.setBounds (r);
}

} // namespace ses
