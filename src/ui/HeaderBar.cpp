#include "ui/HeaderBar.h"

#include "ui/Theme.h"

namespace ses
{
namespace
{
const juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }
}

HeaderBar::HeaderBar (EditorContext& c) : ctx (c)
{
    ctx.addListener (this);

    for (auto* b : { &easyButton, &proButton })
    {
        b->setClickingTogglesState (false);
        b->setColour (juce::TextButton::buttonOnColourId, theme::violet);
        addAndMakeVisible (*b);
    }
    easyButton.setConnectedEdges (juce::Button::ConnectedOnRight);
    proButton.setConnectedEdges (juce::Button::ConnectedOnLeft);
    easyButton.onClick = [this] { if (onModeChange) onModeChange (false); };
    proButton.onClick = [this] { if (onModeChange) onModeChange (true); };

    sceneName.setEditable (false, true, false);
    sceneName.setFont (theme::font (15.0f, true));
    sceneName.setTooltip (u8 ("Doble clic para cambiar el nombre de la escena"));
    sceneName.onTextChange = [this]
    {
        const auto t = sceneName.getText().trim();
        ctx.edit ([t] (Scene& s) { s.name = t.isEmpty() ? "Escena" : t; });
    };
    addAndMakeVisible (sceneName);

    sceneMenu.setTooltip ("Guardar o cargar la escena completa (.sesscene, incluye el audio si cabe)");
    sceneMenu.onClick = [this]
    {
        juce::PopupMenu m;
        m.addItem (1, u8 ("Guardar escena\xe2\x80\xa6"));
        m.addItem (2, u8 ("Cargar escena\xe2\x80\xa6"));
        m.addSeparator();
        juce::PopupMenu size;
        const int cur = ctx.processor.getUiScalePercent();
        for (int pct : { 100, 125, 150 })
            size.addItem (100 + pct, juce::String (pct) + " %", true, cur == pct);
        m.addSubMenu (u8 ("Tama\xc3\xb1o de la interfaz"), size);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&sceneMenu),
                         [safe = juce::Component::SafePointer<HeaderBar> (this)] (int r)
                         {
                             if (safe == nullptr) return;
                             if (r == 1) safe->chooseSave();
                             if (r == 2) safe->chooseLoad();
                             if (r >= 200 && safe->onScaleChange) safe->onScaleChange (r - 100);
                         });
    };
    addAndMakeVisible (sceneMenu);
    undoButton.setTooltip (u8 ("Deshacer (Ctrl+Z)"));
    redoButton.setTooltip (u8 ("Rehacer (Ctrl+Y)"));
    undoButton.onClick = [this] { ctx.undo(); };
    redoButton.onClick = [this] { ctx.redo(); };
    addAndMakeVisible (undoButton);
    addAndMakeVisible (redoButton);

    triggerButton.setColour (juce::TextButton::buttonColourId, theme::blue);
    triggerButton.setTooltip (u8 ("Dispara las repeticiones. Con FL Studio en reproducci\xc3\xb3n, entran en el siguiente paso de la rejilla."));
    triggerButton.onClick = [this] { ctx.processor.requestTrigger(); };
    previewButton.setTooltip (u8 ("Escucha todas las repeticiones ahora, sin esperar a la rejilla"));
    previewButton.onClick = [this] { ctx.processor.requestPreview(); };
    addAndMakeVisible (triggerButton);
    addAndMakeVisible (previewButton);

    for (auto* s : { &mix, &output })
    {
        s->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 58, 18);
        addAndMakeVisible (*s);
    }
    mix.setTextValueSuffix (" %");
    output.setTextValueSuffix (" dB");
    mix.setTooltip (u8 ("Nivel de las repeticiones. El audio original nunca se aten\xc3\xba" "a."));
    for (auto* l : { &mixLabel, &outputLabel })
    {
        l->setFont (theme::font (11.0f, true));
        l->setColour (juce::Label::textColourId, theme::textDim);
        l->setJustificationType (juce::Justification::centred);
        addAndMakeVisible (*l);
    }
    mixLabel.setText ("MIX", juce::dontSendNotification);
    outputLabel.setText ("SALIDA", juce::dontSendNotification);

    protect.setTooltip (u8 ("Limitador de seguridad a -0.3 dBFS, solo mientras suenan repeticiones"));
    addAndMakeVisible (bypass);
    addChildComponent (protect);

    auto& apvts = ctx.processor.getParameters();
    mixAttachment = std::make_unique<SliderAttachment> (apvts, params::mix, mix);
    outputAttachment = std::make_unique<SliderAttachment> (apvts, params::output, output);
    bypassAttachment = std::make_unique<ButtonAttachment> (apvts, params::bypass, bypass);
    protectAttachment = std::make_unique<ButtonAttachment> (apvts, params::protect, protect);

    sceneChanged();
}

HeaderBar::~HeaderBar()
{
    ctx.removeListener (this);
}

void HeaderBar::sceneChanged()
{
    if (! sceneName.isBeingEdited())
        sceneName.setText (ctx.scene().name, juce::dontSendNotification);
    refreshHistory();
}

void HeaderBar::refreshHistory()
{
    undoButton.setEnabled (ctx.canUndo());
    redoButton.setEnabled (ctx.canRedo());
}

void HeaderBar::setProMode (bool pro)
{
    easyButton.setToggleState (! pro, juce::dontSendNotification);
    proButton.setToggleState (pro, juce::dontSendNotification);
    protect.setVisible (pro);
}

void HeaderBar::updateTransport()
{
    const auto& s = ctx.processor.getHostSnapshot();
    const auto text = s.hasBpm.load() ? juce::String (s.bpm.load(), 1) : juce::String ("--");
    const bool p = s.isPlaying.load();
    const bool h = s.usedHostTimeline.load();
    const float lim = s.limiterGainDb.load();
    if (text != bpmText || p != playing || h != hostTimeline || std::abs (lim - limiterDb) > 0.1f)
    {
        bpmText = text;
        playing = p;
        hostTimeline = h;
        limiterDb = lim;
        repaint (transportArea);
    }
}

void HeaderBar::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (theme::panel);
    g.fillRect (r);
    g.setColour (theme::outline);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, r.getRight());

    // Logo: three concentric arcs around a dot.
    const juce::Point<float> c (30.0f, r.getCentreY());
    g.setColour (theme::turquoise);
    g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (c));
    for (int i = 1; i <= 3; ++i)
    {
        juce::Path arc;
        const float rr = 5.0f + 5.0f * (float) i;
        arc.addCentredArc (c.x, c.y, rr, rr, 0.0f, 0.5f, 2.6f, true);
        g.setColour ((i == 2 ? theme::blue : theme::violet).withAlpha (1.0f - 0.22f * (float) i));
        g.strokePath (arc, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    if (! compactLogo)
    {
    g.setColour (theme::text);
    g.setFont (theme::font (15.0f, true));
    g.drawText ("SPATIAL ECHO", juce::Rectangle<float> (52.0f, r.getCentreY() - 17.0f, 140.0f, 18.0f), juce::Justification::centredLeft);
    g.setColour (theme::textDim);
    g.setFont (theme::font (11.0f, true));
    g.drawText ("STUDIO", juce::Rectangle<float> (52.0f, r.getCentreY() + 1.0f, 140.0f, 14.0f), juce::Justification::centredLeft);
    }

    // Transport readout.
    auto t = transportArea.toFloat();
    g.setColour (theme::background);
    g.fillRoundedRectangle (t, 8.0f);
    g.setColour (theme::outline);
    g.drawRoundedRectangle (t.reduced (0.5f), 8.0f, 1.0f);
    auto inner = t.reduced (12.0f, 4.0f);
    auto top = inner.removeFromTop (inner.getHeight() * 0.55f);
    g.setColour (theme::text);
    g.setFont (theme::font (17.0f, true));
    const float bpmW = (float) juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), bpmText) + 6.0f;
    g.drawText (bpmText, top.removeFromLeft (bpmW), juce::Justification::centredLeft);
    g.setColour (theme::textDim);
    g.setFont (theme::font (11.0f, true));
    g.drawText ("BPM", top, juce::Justification::centredLeft);

    const auto dot = inner.removeFromLeft (12.0f).withSizeKeepingCentre (8.0f, 8.0f);
    g.setColour (playing ? theme::success : theme::textFaint);
    if (playing)
        g.fillEllipse (dot);
    else
        g.fillRect (dot);
    g.setColour (playing ? theme::text : theme::textDim);
    g.setFont (theme::font (11.5f));
    const juce::String state = playing ? (hostTimeline ? u8 ("En marcha") : u8 ("En marcha (sin posici\xc3\xb3n)"))
                                       : u8 ("Detenido");
    g.drawText (state, inner.withTrimmedLeft (4.0f), juce::Justification::centredLeft, true);
    if (limiterDb < -0.1f)
    {
        g.setColour (theme::amber);
        g.setFont (theme::font (10.5f, true));
        g.drawText ("LIM " + juce::String (limiterDb, 1) + " dB", t.reduced (8.0f, 2.0f), juce::Justification::bottomRight);
    }
}

void HeaderBar::resized()
{
    auto r = getLocalBounds().reduced (12, 8);
    compactLogo = getWidth() < 1200;
    r.removeFromLeft (compactLogo ? 50 : 160);

    auto modes = r.removeFromLeft (120).withSizeKeepingCentre (120, 30);
    easyButton.setBounds (modes.removeFromLeft (60));
    proButton.setBounds (modes);
    r.removeFromLeft (12);
    sceneName.setBounds (r.removeFromLeft (130).withSizeKeepingCentre (130, 28));
    r.removeFromLeft (4);
    sceneMenu.setBounds (r.removeFromLeft (92).withSizeKeepingCentre (92, 30));
    r.removeFromLeft (8);
    undoButton.setBounds (r.removeFromLeft (34).withSizeKeepingCentre (34, 30));
    r.removeFromLeft (3);
    redoButton.setBounds (r.removeFromLeft (34).withSizeKeepingCentre (34, 30));
    r.removeFromLeft (10);

    // Right side, from the right edge.
    auto toggles = r.removeFromRight (96);
    bypass.setBounds (toggles.removeFromTop (toggles.getHeight() / 2));
    protect.setBounds (toggles);
    r.removeFromRight (6);
    auto knob = [&] (juce::Slider& s, juce::Label& l)
    {
        auto k = r.removeFromRight (98);
        l.setBounds (k.removeFromTop (14).withTrimmedRight (58));
        s.setBounds (k);
        r.removeFromRight (2);
    };
    knob (output, outputLabel);
    knob (mix, mixLabel);
    r.removeFromRight (10);
    previewButton.setBounds (r.removeFromRight (72).withSizeKeepingCentre (72, 32));
    r.removeFromRight (6);
    triggerButton.setBounds (r.removeFromRight (96).withSizeKeepingCentre (96, 38));
    r.removeFromRight (12);
    const int w = juce::jmin (250, r.getWidth());
    transportArea = r.removeFromRight (w).withSizeKeepingCentre (w, 42);
}

void HeaderBar::chooseSave()
{
    chooser = std::make_unique<juce::FileChooser> ("Guardar escena", juce::File(), "*.sesscene");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [safe = juce::Component::SafePointer<HeaderBar> (this)] (const juce::FileChooser& fc)
                          {
                              if (safe == nullptr || fc.getResult() == juce::File())
                                  return;
                              auto f = fc.getResult().withFileExtension ("sesscene");
                              const auto r = safe->ctx.processor.saveSceneFile (f);
                              if (r.failed())
                                  juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Guardar escena", r.getErrorMessage());
                          });
}

void HeaderBar::chooseLoad()
{
    chooser = std::make_unique<juce::FileChooser> ("Cargar escena", juce::File(), "*.sesscene");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe = juce::Component::SafePointer<HeaderBar> (this)] (const juce::FileChooser& fc)
                          {
                              if (safe == nullptr || ! fc.getResult().existsAsFile())
                                  return;
                              const auto r = safe->ctx.processor.loadSceneFile (fc.getResult());
                              if (r.failed())
                                  juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Cargar escena", r.getErrorMessage());
                              safe->ctx.refreshIfChanged();
                          });
}

} // namespace ses
