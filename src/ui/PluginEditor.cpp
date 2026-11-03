#include "ui/PluginEditor.h"

namespace ses
{
namespace
{
const juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }
const juce::String dot = juce::String::fromUTF8 ("  \xc2\xb7  ");
}

SpatialEchoEditor::SpatialEchoEditor (SpatialEchoProcessor& p)
    : AudioProcessorEditor (p), processor (p), ctx (p), header (ctx), actions (ctx), exportPanel (ctx), waveform (ctx), map (ctx), pointPanel (ctx), timeline (ctx), curveEditor (ctx), sidebar (ctx)
{
    ctx.addListener (this);

    for (juce::Component* c : { (juce::Component*) &header, (juce::Component*) &actions, (juce::Component*) &waveform, (juce::Component*) &map,
                                (juce::Component*) &pointPanel, (juce::Component*) &timeline })
        addAndMakeVisible (c);

    header.onModeChange = [this] (bool pro) { setProMode (pro); };
    header.onScaleChange = [this] (int pct) { setUiScale (pct); };
    actions.onImport = [this] (juce::Component& anchor) { showImportMenu (anchor); };
    actions.onLiveToggle = [this]
    {
        const bool on = ! ctx.scene().live.enabled;
        ctx.edit ([on] (Scene& s) { s.live.enabled = on; });
        if (on)
            ctx.select (kLivePointId, false);  // its settings appear in the panel
        else if (ctx.primary() == kLivePointId)
            ctx.clearSelection();
        refreshLive();
    };
    actions.onLiveSettings = [this]
    {
        ctx.select (kLivePointId, false);
        if (pro)
            pointPanel.showSection ("filters");
    };
    actions.onExport = [this] { exportPanel.setBounds (getLocalBounds()); exportPanel.open(); };
    addChildComponent (exportPanel);
    addChildComponent (curveEditor);
    curveEditor.onClose = [this] { showCurveEditor (false); };
    curveEditor.onExpand = [this] { showCurveEditor (true); };
    curveEditor.onStateChanged = [this] { abButton.setEnabled (ctx.primaryPoint() != nullptr && curveEditor.hasStateB()); };
    pointPanel.onOpenCurveEditor = [this] { showCurveEditor (true); };

    // Pro layout: sidebar, centre tabs, bottom bar.
    addChildComponent (sidebar);
    sidebar.onCategory = [this] (const juce::String& id)
    {
        if (id == "space")
            setCenterView (CenterView::Map);
        else if (id == "shaper")
            setCenterView (CenterView::Cuts);
        pointPanel.showSection (id);
    };
    for (auto [b, v] : { std::pair { &cutsTab, CenterView::Cuts }, std::pair { &mapTab, CenterView::Map }, std::pair { &audioTab, CenterView::Audio } })
    {
        b->setRadioGroupId (77);
        b->setClickingTogglesState (true);
        b->setConnectedEdges (juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
        b->setColour (juce::TextButton::buttonOnColourId, theme::violet.withAlpha (0.7f));
        b->onClick = [this, v] { setCenterView (v); };
        addChildComponent (*b);
    }
    listenButton.setColour (juce::TextButton::buttonColourId, theme::blue);
    listenButton.setTooltip (u8 ("Reproduce solo la repetici\xc3\xb3n seleccionada, ahora"));
    listenButton.onClick = [this] { if (const auto* pt = ctx.primaryPoint()) processor.requestTriggerPoint (pt->id); };
    soloButton.setTooltip (u8 ("Solo: si alguna repetici\xc3\xb3n est\xc3\xa1 en solo, solo suenan las que lo est\xc3\xa1n"));
    soloButton.onClick = [this]
    {
        const bool on = soloButton.getToggleState();
        const auto ids = ctx.selection();
        ctx.edit ([&] (Scene& s) { for (auto& id : ids) if (auto* pt = findPoint (s, id)) pt->solo = on; });
    };
    abButton.setTooltip (u8 ("Alterna la curva A (actual) con la B guardada en el editor de cortes"));
    abButton.onClick = [this] { curveEditor.toggleAB(); refreshBottomBar(); };
    prevPointButton.onClick = [this] { stepPoint (-1); };
    nextPointButton.onClick = [this] { stepPoint (+1); };
    pointLabel.setFont (theme::font (13.0f, true));
    pointLabel.setJustificationType (juce::Justification::centred);
    pointLabel.setColour (juce::Label::textColourId, theme::text);
    for (juce::Component* c : { (juce::Component*) &listenButton, (juce::Component*) &soloButton, (juce::Component*) &abButton,
                                (juce::Component*) &prevPointButton, (juce::Component*) &nextPointButton, (juce::Component*) &pointLabel,
                                (juce::Component*) &meter })
        addChildComponent (c);

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
    setScaleFactor (processor.getUiScalePercent() / 100.0f);
    setProMode (processor.getUiMode() == UiMode::Pro);
    refreshAudioInfo();
    refreshToolbar();
    refreshLive();
    startTimerHz (30);
}

SpatialEchoEditor::~SpatialEchoEditor()
{
    stopTimer();
    ctx.removeListener (this);
    setLookAndFeel (nullptr);
}

void SpatialEchoEditor::showCurveEditor (bool open)
{
    curveOpen = open;
    curveEditor.setEmbedded (! open && pro && centerView == CenterView::Cuts);
    curveEditor.setVisible (open || (pro && centerView == CenterView::Cuts));
    if (open)
        curveEditor.toFront (true);
    resized();
}

void SpatialEchoEditor::setCenterView (CenterView v)
{
    centerView = v;
    cutsTab.setToggleState (v == CenterView::Cuts, juce::dontSendNotification);
    mapTab.setToggleState (v == CenterView::Map, juce::dontSendNotification);
    audioTab.setToggleState (v == CenterView::Audio, juce::dontSendNotification);
    showCurveEditor (curveOpen);
}

void SpatialEchoEditor::setProMode (bool shouldBePro)
{
    pro = shouldBePro;
    processor.setUiMode (pro ? UiMode::Pro : UiMode::Easy);
    header.setProMode (pro);
    pointPanel.setProMode (pro);
    timeline.setProMode (pro);
    diagnosticsLabel.setVisible (pro);
    logToggle.setVisible (pro);
    sidebar.setVisible (pro);
    for (auto* b : { &cutsTab, &mapTab, &audioTab, &listenButton, &abButton, &prevPointButton, &nextPointButton })
        b->setVisible (pro);
    soloButton.setVisible (pro);
    pointLabel.setVisible (pro);
    meter.setVisible (pro);
    if (pro)
        refreshDiagnostics();
    refreshBottomBar();
    setCenterView (centerView);  // also shows/hides the embedded curve editor and lays out
}

bool SpatialEchoEditor::keyPressed (const juce::KeyPress& k)
{
    // Document undo/redo (the curve editor handles its own keys while it has focus).
    if (k == juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0))
    {
        ctx.undo();
        return true;
    }
    if (k == juce::KeyPress ('y', juce::ModifierKeys::commandModifier, 0)
        || k == juce::KeyPress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0))
    {
        ctx.redo();
        return true;
    }
    if (pro && k.getModifiers().isCommandDown())
    {
        if (k.getKeyCode() == '1') { setCenterView (CenterView::Cuts); return true; }
        if (k.getKeyCode() == '2') { setCenterView (CenterView::Map); return true; }
        if (k.getKeyCode() == '3') { setCenterView (CenterView::Audio); return true; }
    }
    return false;
}

void SpatialEchoEditor::setUiScale (int percent)
{
    processor.setUiScalePercent (percent);
    setScaleFactor (processor.getUiScalePercent() / 100.0f);
}

void SpatialEchoEditor::stepPoint (int delta)
{
    const auto& pts = ctx.scene().points;
    if (pts.empty())
        return;
    int idx = -1;
    const auto id = ctx.primary();
    for (size_t i = 0; i < pts.size(); ++i)
        if (pts[i].id == id)
            idx = (int) i;
    const int n = (int) pts.size();
    idx = idx < 0 ? (delta > 0 ? 0 : n - 1) : ((idx + delta) % n + n) % n;
    ctx.select (pts[(size_t) idx].id, false);
}

void SpatialEchoEditor::refreshBottomBar()
{
    const auto* pt = ctx.primaryPoint();
    const int n = (int) ctx.scene().points.size();
    int idx = 0;
    for (int i = 0; i < n; ++i)
        if (ctx.scene().points[(size_t) i].id == ctx.primary())
            idx = i + 1;
    const bool live = pt != nullptr && pt->id == kLivePointId;
    pointLabel.setText (live ? u8 ("Mixer en vivo")
                             : pt != nullptr ? "Punto " + juce::String (idx) + " / " + juce::String (n) : juce::String (n) + " puntos",
                        juce::dontSendNotification);
    listenButton.setEnabled (pt != nullptr && ! live && processor.getSourceStatus().loaded);
    soloButton.setEnabled (pt != nullptr && ! live);
    soloButton.setToggleState (pt != nullptr && pt->solo, juce::dontSendNotification);
    abButton.setEnabled (pt != nullptr && curveEditor.hasStateB());
    prevPointButton.setEnabled (n > 1);
    nextPointButton.setEnabled (n > 1);
}

void SpatialEchoEditor::timerCallback()
{
    ctx.refreshIfChanged();
    header.updateTransport();
    actions.tick();
    const float outPeak = processor.takeOutputPeakDb();  // always drained, so no stale peak-hold when the meter appears
    if (meter.isVisible())
        meter.push (outPeak);
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
        refreshLive();
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

void SpatialEchoEditor::refreshLive()
{
    const bool on = ctx.scene().live.enabled;
    juce::String detail;
    if (on)
    {
        const float inDb = processor.hostInputPeakDb();
        detail << u8 ("Procesando la entrada de esta pista del Mixer (latencia 0). Entrada: ")
               << (inDb <= -99.0f ? u8 ("sin se\xc3\xb1" "al") : juce::String (inDb, 0) + " dBFS")
               << u8 (". Pulsa para detener.");
    }
    else
    {
        detail = u8 ("Aplica filtros, cortes y efectos de forma continua al audio que entra en esta pista del Mixer. "
                     "Distinto de \xc2\xab" "Capturar \xc3\xbaltimos segundos\xc2\xbb, que convierte la entrada en fragmento.");
    }
    actions.setLiveState (on, detail);
}

void SpatialEchoEditor::showImportMenu (juce::Component& anchor)
{
    if (processor.isHostRecording())
    {
        hostCapture (processor.stopHostRecording());
        return;
    }
    juce::PopupMenu m;
    m.addItem (u8 ("Cargar archivo WAV\xe2\x80\xa6"), [this] { chooseWav(); });
    m.addSeparator();
    m.addSectionHeader (u8 ("Capturar la entrada de esta pista del Mixer"));
    for (double secs : { 1.0, 2.0, 4.0, 8.0 })
        m.addItem (u8 ("Capturar \xc3\xbaltimos ") + juce::String (secs, 0) + " s", [this, secs] { hostCapture (processor.captureHostLast (secs)); });
    m.addItem (u8 ("Empezar a grabar (pulsa de nuevo \xc2\xabImportar audio\xc2\xbb para detener y usar)"),
               [this] { processor.startHostRecording(); refreshAudioInfo(); });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (anchor));
}

void SpatialEchoEditor::hostCapture (const juce::Result& r)
{
    captureNote = r.wasOk() ? u8 ("Captura usada como fragmento") : r.getErrorMessage();
    actions.showNote (captureNote, r.failed());
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
    if (curveOpen)
        return;
    if (! pro)
    {
        theme::drawPanel (g, audioPanel.toFloat(), "AUDIO" + dot + "FRAGMENTO");
        theme::drawPanel (g, mapPanel.toFloat(), u8 ("D\xc3\x93NDE SUENA") + dot + "MAPA ESPACIAL");
        return;
    }
    if (centerView == CenterView::Audio)
        theme::drawPanel (g, centerPanel.toFloat(), "AUDIO" + dot + "FRAGMENTO");
    else if (centerView == CenterView::Map)
        theme::drawPanel (g, centerPanel.toFloat(), u8 ("D\xc3\x93NDE SUENA") + dot + "MAPA ESPACIAL");
    theme::drawPanel (g, bottomBar.toFloat());
}

void SpatialEchoEditor::layoutAudioPanel (juce::Rectangle<int> panel, bool proTab)
{
    auto a = panel.reduced (14, 10);
    a.removeFromTop (20);
    auto top = a.removeFromTop (30);
    loadButton.setBounds (top.removeFromLeft (120));
    top.removeFromLeft (8);
    hostButton.setBounds (top.removeFromLeft (190));
    top.removeFromLeft (12);
    fragmentButton.setBounds (top.removeFromRight (120));
    top.removeFromRight (8);
    expandButton.setVisible (! proTab);
    if (! proTab)
    {
        expandButton.setBounds (top.removeFromRight (76));
        top.removeFromRight (6);
    }
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

void SpatialEchoEditor::layoutMapPanel (juce::Rectangle<int> panel)
{
    auto m = panel.reduced (14, 10);
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

void SpatialEchoEditor::layoutEasy (juce::Rectangle<int> r)
{
    audioPanel = r.removeFromTop (expanded ? juce::jmin (360, r.getHeight() / 2) : 156);
    layoutAudioPanel (audioPanel, false);
    r.removeFromTop (10);
    auto timelineArea = r.removeFromBottom (juce::jlimit (140, 200, r.getHeight() / 4));
    timeline.setBounds (timelineArea);
    r.removeFromBottom (10);
    pointPanel.setBounds (r.removeFromRight (juce::jlimit (330, 420, r.getWidth() / 3)));
    r.removeFromRight (10);
    mapPanel = r;
    layoutMapPanel (mapPanel);
}

void SpatialEchoEditor::layoutPro (juce::Rectangle<int> r)
{
    bottomBar = r.removeFromBottom (44);
    {
        auto b = bottomBar.reduced (10, 7);
        listenButton.setBounds (b.removeFromLeft (190));
        b.removeFromLeft (10);
        soloButton.setBounds (b.removeFromLeft (140));
        b.removeFromLeft (6);
        abButton.setBounds (b.removeFromLeft (84));
        meter.setBounds (b.removeFromRight (juce::jmin (260, b.getWidth() / 3)));
        b.removeFromRight (12);
        auto nav = b.withSizeKeepingCentre (juce::jmin (240, b.getWidth()), b.getHeight());
        prevPointButton.setBounds (nav.removeFromLeft (40));
        nextPointButton.setBounds (nav.removeFromRight (40));
        pointLabel.setBounds (nav);
    }
    r.removeFromBottom (8);
    timeline.setBounds (r.removeFromBottom (juce::jlimit (120, 170, r.getHeight() / 5)));
    r.removeFromBottom (8);

    sidebar.setBounds (r.removeFromLeft (juce::jlimit (216, 250, r.getWidth() / 6)));
    r.removeFromLeft (8);
    pointPanel.setBounds (r.removeFromRight (juce::jlimit (330, 420, (int) (r.getWidth() * 0.38))));
    r.removeFromRight (8);

    auto tabs = r.removeFromTop (26);
    const int tw = juce::jmin (150, tabs.getWidth() / 3);
    cutsTab.setBounds (tabs.removeFromLeft (tw));
    mapTab.setBounds (tabs.removeFromLeft (tw));
    audioTab.setBounds (tabs.removeFromLeft (tw));
    r.removeFromTop (4);
    centerPanel = r;
    const bool showCuts = centerView == CenterView::Cuts, showMap = centerView == CenterView::Map, showAudio = centerView == CenterView::Audio;
    for (juce::Component* c : { (juce::Component*) &map, (juce::Component*) &addButton, (juce::Component*) &duplicateButton,
                                (juce::Component*) &muteButton, (juce::Component*) &deleteButton, (juce::Component*) &countLabel })
        c->setVisible (showMap);
    for (juce::Component* c : { (juce::Component*) &waveform, (juce::Component*) &loadButton, (juce::Component*) &hostButton,
                                (juce::Component*) &fragmentButton, (juce::Component*) &zoomInButton, (juce::Component*) &zoomOutButton,
                                (juce::Component*) &zoomSelButton, (juce::Component*) &zoomFitButton, (juce::Component*) &audioInfo,
                                (juce::Component*) &fragmentInfo })
        c->setVisible (showAudio);
    expandButton.setVisible (false);
    if (showCuts && ! curveOpen)
        curveEditor.setBounds (centerPanel);
    else if (showMap)
        layoutMapPanel (centerPanel);
    else if (showAudio)
        layoutAudioPanel (centerPanel, true);
}

void SpatialEchoEditor::resized()
{
    auto r = getLocalBounds();
    header.setBounds (r.removeFromTop (72));
    actions.setBounds (r.removeFromTop (curveOpen ? 0 : 44));
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

    exportPanel.setBounds (getLocalBounds());
    r.reduce (12, 10);
    if (curveOpen)
        curveEditor.setBounds (r);  // covers everything below the header until closed
    if (pro)
    {
        layoutPro (r);
    }
    else
    {
        for (juce::Component* c : { (juce::Component*) &map, (juce::Component*) &addButton, (juce::Component*) &duplicateButton,
                                    (juce::Component*) &muteButton, (juce::Component*) &deleteButton, (juce::Component*) &countLabel,
                                    (juce::Component*) &waveform, (juce::Component*) &loadButton, (juce::Component*) &hostButton,
                                    (juce::Component*) &fragmentButton, (juce::Component*) &zoomInButton, (juce::Component*) &zoomOutButton,
                                    (juce::Component*) &zoomSelButton, (juce::Component*) &zoomFitButton, (juce::Component*) &audioInfo,
                                    (juce::Component*) &fragmentInfo, (juce::Component*) &expandButton })
            c->setVisible (true);
        layoutEasy (r);
    }
    repaint();
}

} // namespace ses
