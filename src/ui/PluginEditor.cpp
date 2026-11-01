#include "ui/PluginEditor.h"

namespace ses
{
namespace
{
const juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }
const juce::String dot = juce::String::fromUTF8 ("  \xc2\xb7  ");
}

SpatialEchoEditor::SpatialEchoEditor (SpatialEchoProcessor& p)
    : AudioProcessorEditor (p), processor (p), ctx (p), header (ctx), waveform (ctx), map (ctx), pointPanel (ctx), timeline (ctx)
{
    ctx.addListener (this);

    for (juce::Component* c : { (juce::Component*) &header, (juce::Component*) &waveform, (juce::Component*) &map,
                                (juce::Component*) &pointPanel, (juce::Component*) &timeline })
        addAndMakeVisible (c);

    header.onModeChange = [this] (bool pro) { setProMode (pro); };

    loadButton.setColour (juce::TextButton::buttonColourId, theme::blue.withAlpha (0.85f));
    loadButton.onClick = [this] { chooseWav(); };
    fragmentButton.setTooltip (u8 ("Escucha solo el fragmento seleccionado, una vez y al centro"));
    fragmentButton.onClick = [this] { processor.requestFragmentPreview(); };
    waveform.onFileDropped = [this] (const juce::File& f) { loadWav (f); };
    for (auto* l : { &audioInfo, &fragmentInfo, &countLabel })
    {
        l->setFont (theme::font (12.5f));
        l->setColour (juce::Label::textColourId, theme::textDim);
        addAndMakeVisible (*l);
    }
    fragmentInfo.setJustificationType (juce::Justification::centredRight);
    countLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (loadButton);
    hostButton.setTooltip (u8 ("Usa como fragmento el audio que llega a esta pista del Mixer: los \xc3\xbaltimos segundos o una grabaci\xc3\xb3n"));
    hostButton.onClick = [this]
    {
        if (processor.isHostRecording())
            hostCapture (processor.stopHostRecording());
        else
            showHostMenu();
    };
    addAndMakeVisible (hostButton);
    addAndMakeVisible (fragmentButton);
    expandButton.setClickingTogglesState (true);
    expandButton.setTooltip (u8 ("Editor de audio m\xc3\xa1s alto para seleccionar con precisi\xc3\xb3n"));
    expandButton.onClick = [this] { expanded = expandButton.getToggleState(); resized(); };
    zoomInButton.onClick = [this] { waveform.zoomBy (0.6); };
    zoomOutButton.onClick = [this] { waveform.zoomBy (1.0 / 0.6); };
    zoomSelButton.setTooltip (u8 ("Zoom al fragmento seleccionado"));
    zoomSelButton.onClick = [this] { waveform.zoomToSelection(); };
    zoomFitButton.setTooltip ("Ver todo el archivo");
    zoomFitButton.onClick = [this] { waveform.fitView(); };
    for (auto* b : { &expandButton, &zoomInButton, &zoomOutButton, &zoomSelButton, &zoomFitButton })
        addAndMakeVisible (*b);

    addButton.onClick = [this]
    {
        // New points go round the listener so they do not stack on each other.
        const int n = (int) ctx.scene().points.size();
        const double angle = n * 2.399963;  // golden angle
        SpatialMap::addPointAt (ctx, { 50.0 + 32.0 * std::sin (angle), 50.0 - 32.0 * std::cos (angle) });
    };
    duplicateButton.onClick = [this] { SpatialMap::duplicateSelection (ctx); };
    muteButton.onClick = [this] { SpatialMap::toggleMuteSelection (ctx); };
    deleteButton.onClick = [this] { SpatialMap::deleteSelection (ctx); };
    for (auto* b : { &addButton, &duplicateButton, &muteButton, &deleteButton })
        addAndMakeVisible (*b);

    footer.setFont (theme::font (11.5f));
    footer.setColour (juce::Label::textColourId, theme::textFaint);
    footer.setText (u8 ("Est\xc3\xa9reo: X = izquierda/derecha \xc2\xb7 Y = profundidad aproximada (m\xc3\xa1s lejos = m\xc3\xa1s suave y oscuro). "
                        "No es audio 3D ni binaural. El audio original pasa sin cambios."),
                    juce::dontSendNotification);
    addAndMakeVisible (footer);

    diagnosticsLabel.setFont (theme::font (11.0f));
    diagnosticsLabel.setColour (juce::Label::textColourId, theme::textDim);
    addChildComponent (diagnosticsLabel);
    logToggle.setToggleState (processor.getDiagnostics().isEnabled(), juce::dontSendNotification);
    logToggle.onClick = [this] { processor.getDiagnostics().setEnabled (logToggle.getToggleState()); };
    logToggle.setTooltip (processor.getDiagnostics().getFile().getFullPathName());
    addChildComponent (logToggle);

    // After the children exist, so they rebuild their parts (text boxes) with it.
    setLookAndFeel (&lnf);
    setResizable (true, true);
    setResizeLimits (1080, 790, 2000, 1400);
    setSize (1240, 840);
    setProMode (processor.getUiMode() == UiMode::Pro);
    refreshAudioInfo();
    refreshToolbar();
    startTimerHz (30);
}

SpatialEchoEditor::~SpatialEchoEditor()
{
    stopTimer();
    ctx.removeListener (this);
    setLookAndFeel (nullptr);
}

void SpatialEchoEditor::setProMode (bool pro)
{
    processor.setUiMode (pro ? UiMode::Pro : UiMode::Easy);
    header.setProMode (pro);
    pointPanel.setProMode (pro);
    timeline.setProMode (pro);
    diagnosticsLabel.setVisible (pro);
    logToggle.setVisible (pro);
    if (pro)
        refreshDiagnostics();
    resized();
}

void SpatialEchoEditor::timerCallback()
{
    ctx.refreshIfChanged();
    header.updateTransport();
    map.updateSounding();
    timeline.updatePlayhead();
    if (++timerTicks % 8 == 0)
    {
        pointPanel.refreshPending();
        const int pend = processor.numPendingPitchRenders();
        if (pend != lastPendingRenders)
        {
            lastPendingRenders = pend;
            timeline.setProMode (processor.getUiMode() == UiMode::Pro);  // relayout blocks (pending markers)
        }
        refreshAudioInfo();
        if (diagnosticsLabel.isVisible())
            refreshDiagnostics();
    }
}

void SpatialEchoEditor::showHostMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader (u8 ("Capturar la entrada del Mixer"));
    for (double secs : { 1.0, 2.0, 4.0, 8.0 })
        m.addItem (u8 ("\xc3\x9altimos ") + juce::String (secs, 0) + " s", [this, secs] { hostCapture (processor.captureHostLast (secs)); });
    m.addSeparator();
    m.addItem (u8 ("Empezar a grabar (pulsa de nuevo para detener y usar)"), [this] { processor.startHostRecording(); refreshAudioInfo(); });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (hostButton));
}

void SpatialEchoEditor::hostCapture (const juce::Result& r)
{
    captureNote = r.wasOk() ? u8 ("Captura usada como fragmento") : r.getErrorMessage();
    captureNoteTicks = 60;  // ~ 12 s at the info refresh rate
    refreshAudioInfo();
}

void SpatialEchoEditor::refreshAudioInfo()
{
    if (processor.isHostRecording())
    {
        hostButton.setButtonText (u8 ("\xe2\x97\x8f Grabando ") + juce::String (processor.hostRecordedSeconds(), 1) + " s  (detener)");
        hostButton.setColour (juce::TextButton::buttonColourId, theme::danger.withAlpha (0.6f));
    }
    else
    {
        const float inDb = processor.hostInputPeakDb();
        hostButton.setButtonText (u8 ("Entrada del host  ") + (inDb <= -99.0f ? juce::String ("--") : juce::String (inDb, 0) + " dB")
                                  + u8 ("  \xe2\x96\xbe"));
        hostButton.removeColour (juce::TextButton::buttonColourId);
    }

    const auto st = processor.getSourceStatus();
    juce::String info;
    if (st.loaded)
        info << st.fileName << "   " << juce::String (st.seconds, 2) << " s" << dot << juce::String (st.sampleRate / 1000.0, 1)
             << "k " << (st.numChannels == 1 ? juce::String ("mono") : u8 ("est\xc3\xa9reo"))
             << (st.embedded ? juce::String() : dot + u8 ("referencia al archivo"));
    else
        info = "Sin audio cargado";
    if (st.message.isNotEmpty())
        info << "   (" << st.message << ")";
    if (const int pend = processor.numPendingPitchRenders(); pend > 0)
        info << u8 ("   \xc2\xb7  procesando tono (") << pend << ")";
    if (captureNoteTicks > 0 && --captureNoteTicks >= 0 && captureNote.isNotEmpty())
        info << "   (" << captureNote << ")";
    audioInfo.setText (info, juce::dontSendNotification);
    audioInfo.setColour (juce::Label::textColourId, st.message.isNotEmpty() || captureNoteTicks > 0 ? theme::amber : theme::textDim);

    if (auto audio = processor.getSourceAudio())
    {
        const auto sel = waveform.selectionSamples();
        const double secs = (double) sel.getLength() / audio->sampleRate;
        fragmentInfo.setText ("Fragm. " + juce::String (secs, 3) + " s  (" + juce::String ((double) sel.getStart() / audio->sampleRate, 3)
                                  + u8 (" \xe2\x80\x93 ") + juce::String ((double) sel.getEnd() / audio->sampleRate, 3) + " s)",
                              juce::dontSendNotification);
    }
    else
    {
        fragmentInfo.setText ({}, juce::dontSendNotification);
    }
    fragmentButton.setEnabled (st.loaded);
    countLabel.setText (juce::String ((int) ctx.scene().points.size()) + " / " + juce::String (ctx.scene().maxPoints),
                        juce::dontSendNotification);
    refreshToolbar();
}

void SpatialEchoEditor::refreshToolbar()
{
    const bool any = ! ctx.selection().isEmpty();
    duplicateButton.setEnabled (any);
    muteButton.setEnabled (any);
    deleteButton.setEnabled (any);
    addButton.setEnabled ((int) ctx.scene().points.size() < ctx.scene().maxPoints);
}

void SpatialEchoEditor::refreshDiagnostics()
{
    const auto& s = processor.getHostSnapshot();
    juce::String t;
    t << "Host: ppq " << (s.hasPpq.load() ? juce::String (s.ppq.load(), 3) : juce::String ("n/a"))
      << dot << "bloque " << s.blockSize.load() << u8 (" (m\xc3\xa1x ") << s.maxBlockSize.load() << ")"
      << dot << "disparos " << s.triggers.load() << dot << "sonaron " << s.fired.load()
      << " (tarde " << s.firedLate.load() << ")" << dot << "perdidas " << s.dropped.load()
      << dot << "saltos " << s.jumps.load() << dot << "voces " << s.activeVoices.load()
      << dot << "limitador " << juce::String (s.limiterGainDb.load(), 1) << " dB";
    diagnosticsLabel.setText (t, juce::dontSendNotification);
}

void SpatialEchoEditor::chooseWav()
{
    chooser = std::make_unique<juce::FileChooser> ("Elegir un archivo WAV", juce::File(), "*.wav");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe = juce::Component::SafePointer<SpatialEchoEditor> (this)] (const juce::FileChooser& fc)
                          {
                              if (safe != nullptr && fc.getResult().existsAsFile())
                                  safe->loadWav (fc.getResult());
                          });
}

void SpatialEchoEditor::loadWav (const juce::File& f)
{
    processor.loadSourceFile (f);  // errors appear in the audio info line
    ctx.refreshIfChanged();
    refreshAudioInfo();
}

void SpatialEchoEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
    theme::drawPanel (g, audioPanel.toFloat(), "AUDIO" + dot + "FRAGMENTO");
    theme::drawPanel (g, mapPanel.toFloat(), u8 ("D\xc3\x93NDE SUENA") + dot + "MAPA ESPACIAL");
}

void SpatialEchoEditor::resized()
{
    auto r = getLocalBounds();
    header.setBounds (r.removeFromTop (72));
    auto bottom = r.removeFromBottom (diagnosticsLabel.isVisible() ? 44 : 24);
    {
        auto line = bottom.reduced (14, 2);
        if (diagnosticsLabel.isVisible())
        {
            auto d = line.removeFromTop (20);
            logToggle.setBounds (d.removeFromRight (220));
            diagnosticsLabel.setBounds (d);
        }
        footer.setBounds (line);
    }

    r.reduce (12, 10);
    audioPanel = r.removeFromTop (expanded ? juce::jmin (360, r.getHeight() / 2) : 156);
    {
        auto a = audioPanel.reduced (14, 10);
        a.removeFromTop (20);
        auto top = a.removeFromTop (30);
        loadButton.setBounds (top.removeFromLeft (120));
        top.removeFromLeft (8);
        hostButton.setBounds (top.removeFromLeft (190));
        top.removeFromLeft (12);
        fragmentButton.setBounds (top.removeFromRight (120));
        top.removeFromRight (8);
        expandButton.setBounds (top.removeFromRight (76));
        top.removeFromRight (6);
        zoomFitButton.setBounds (top.removeFromRight (46));
        zoomSelButton.setBounds (top.removeFromRight (42));
        zoomOutButton.setBounds (top.removeFromRight (30));
        zoomInButton.setBounds (top.removeFromRight (30));
        top.removeFromRight (12);
        fragmentInfo.setBounds (top.removeFromRight (236));
        audioInfo.setBounds (top);
        a.removeFromTop (8);
        waveform.setBounds (a);
    }
    r.removeFromTop (10);

    auto timelineArea = r.removeFromBottom (juce::jlimit (140, 200, r.getHeight() / 4));
    timeline.setBounds (timelineArea);
    r.removeFromBottom (10);

    pointPanel.setBounds (r.removeFromRight (juce::jlimit (330, 420, r.getWidth() / 3)));
    r.removeFromRight (10);
    mapPanel = r;
    {
        auto m = mapPanel.reduced (14, 10);
        auto tools = m.removeFromTop (26);
        tools.removeFromLeft (200);
        countLabel.setBounds (tools.removeFromRight (60));
        for (auto* b : { &addButton, &duplicateButton, &muteButton, &deleteButton })
        {
            b->setBounds (tools.removeFromLeft (86));
            tools.removeFromLeft (5);
        }
        map.setBounds (m);
    }
}

} // namespace ses
