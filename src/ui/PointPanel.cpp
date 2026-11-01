#include "ui/PointPanel.h"

#include "model/VoicePresets.h"
#include "ui/SpatialMap.h"
#include "ui/Theme.h"

namespace ses
{
namespace
{
const juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }
constexpr StutterSettings::Region kRegions[] { StutterSettings::Region::Whole, StutterSettings::Region::Start,
                                               StutterSettings::Region::End, StutterSettings::Region::Manual };
const char* kRegionNames[] { "Fragmento completo", "Inicio", "Final", "Selecci\xc3\xb3n manual" };
constexpr StutterSettings::Division kDivisions[] { StutterSettings::Division::Quarter, StutterSettings::Division::Eighth,
                                                   StutterSettings::Division::Sixteenth, StutterSettings::Division::ThirtySecond };
}

std::optional<StutterSettings> PointPanel::stutterClipboard;

PointPanel::PointPanel (EditorContext& c) : ctx (c)
{
    ctx.addListener (this);
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    viewport.getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, theme::textDim.withAlpha (0.6f));
    addAndMakeVisible (viewport);

    title.setFont (theme::font (17.0f, true));
    content.addAndMakeVisible (title);
    triggerButton.setColour (juce::TextButton::buttonColourId, theme::blue);
    triggerButton.setTooltip (u8 ("Reproduce solo esta repetici\xc3\xb3n ahora, con su tono, ganancia, posici\xc3\xb3n y Stutter"));
    triggerButton.onClick = [this]
    {
        if (const auto* p = ctx.primaryPoint())
            ctx.processor.requestTriggerPoint (p->id);
    };
    content.addAndMakeVisible (triggerButton);

    presetL.setText ("Preset vocal", juce::dontSendNotification);
    presetL.setFont (theme::font (12.5f));
    presetL.setColour (juce::Label::textColourId, theme::textDim);
    content.addAndMakeVisible (presetL);
    int id = 1;
    for (auto& p : builtInVoicePresets())
        presetBox.addItem (p.name + (p.needsFutureProcessing ? u8 ("  (tono s\xc3\xb3lo)") : juce::String()), id++);
    presetBox.onChange = [this]
    {
        const int idx = presetBox.getSelectedItemIndex();
        const auto& presets = builtInVoicePresets();
        if (idx < 0 || idx >= (int) presets.size())
            return;
        const auto ids = ctx.selection();
        std::vector<juce::String> v (ids.begin(), ids.end());
        ctx.edit ([&] (Scene& s) { applyVoicePreset (s, v, presets[(size_t) idx]); });
    };
    content.addAndMakeVisible (presetBox);
    presetNote.setFont (theme::font (11.0f));
    presetNote.setColour (juce::Label::textColourId, theme::textDim);
    content.addAndMakeVisible (presetNote);

    setupSlider (pitch, pitchL, "Pitch", -24, 24, 1, " st");
    pitch.setDoubleClickReturnValue (true, 0.0);
    pitch.onValueChange = [this] { editSelection ([v = pitch.getValue()] (Scene& s, Point& p) { auto snd = p.sound; snd.pitchSemitones = v; setSound (s, p.id, snd); }); };
    pitchStatus.setFont (theme::font (11.0f, true));
    pitchStatus.setColour (juce::Label::textColourId, theme::amber);
    pitchStatus.setJustificationType (juce::Justification::centredRight);
    content.addAndMakeVisible (pitchStatus);

    setupSlider (gain, gainL, "Ganancia", -36, 12, 0.1, " dB");
    gain.setDoubleClickReturnValue (true, 0.0);
    gain.onValueChange = [this] { editPrimary ([v = gain.getValue()] (Scene& s, Point& p) { setGainDb (s, p.id, v); }); };
    setupSlider (posX, posXL, u8 ("Izq \xe2\x86\x94 Der"), 0, 100, 0.1, "");
    setupSlider (posY, posYL, u8 ("Delante \xe2\x86\x94 Detr\xc3\xa1s"), 0, 100, 0.1, "");
    setupSlider (beat, beatL, "Momento", 0, 32, 0.25, " t");
    posX.onValueChange = [this] { editPrimary ([v = posX.getValue()] (Scene& s, Point& p) { movePoint (s, p.id, { v, p.position.y }); }); };
    posY.onValueChange = [this] { editPrimary ([v = posY.getValue()] (Scene& s, Point& p) { movePoint (s, p.id, { p.position.x, v }); }); };
    beat.onValueChange = [this] { editPrimary ([v = beat.getValue()] (Scene& s, Point& p) { setTiming (s, p.id, { v }); }); };

    enabledToggle.onClick = [this] { editPrimary ([on = enabledToggle.getToggleState()] (Scene&, Point& p) { p.enabled = on; }); };
    soloToggle.onClick = [this] { editPrimary ([on = soloToggle.getToggleState()] (Scene&, Point& p) { p.solo = on; }); };
    duplicateButton.onClick = [this] { SpatialMap::duplicateSelection (ctx); };
    deleteButton.onClick = [this] { SpatialMap::deleteSelection (ctx); };
    deleteButton.setColour (juce::TextButton::buttonColourId, theme::danger.withAlpha (0.25f));
    for (juce::Component* comp : { (juce::Component*) &enabledToggle, (juce::Component*) &soloToggle,
                                   (juce::Component*) &duplicateButton, (juce::Component*) &deleteButton })
        content.addAndMakeVisible (comp);

    // ---- Stutter
    stutterTitle.setText ("STUTTER / BEAT REPEAT", juce::dontSendNotification);
    stutterTitle.setFont (theme::font (11.5f, true));
    stutterTitle.setColour (juce::Label::textColourId, theme::textDim);
    content.addAndMakeVisible (stutterTitle);
    stutterToggle.setTooltip (u8 ("Corta la repetici\xc3\xb3n en trozos cortos repetidos (A-A-A-A)"));
    stutterToggle.onClick = [this] { editPrimary ([on = stutterToggle.getToggleState()] (Scene&, Point& p) { p.stutter.enabled = on; }); };
    content.addAndMakeVisible (stutterToggle);
    copyButton.setTooltip (u8 ("Copia la configuraci\xc3\xb3n de Stutter de este punto"));
    copyButton.onClick = [this]
    {
        if (const auto* p = ctx.primaryPoint())
            stutterClipboard = p->stutter;
        refresh();
    };
    pasteButton.setTooltip (u8 ("Aplica la configuraci\xc3\xb3n copiada a las repeticiones seleccionadas"));
    pasteButton.onClick = [this]
    {
        if (stutterClipboard)
            editSelection ([st = *stutterClipboard] (Scene& s, Point& p) { setStutter (s, p.id, st); });
    };
    content.addAndMakeVisible (copyButton);
    content.addAndMakeVisible (pasteButton);

    for (int i = 0; i < (int) std::size (kRegions); ++i)
        regionBox.addItem (u8 (kRegionNames[i]), i + 1);
    regionBox.onChange = [this]
    {
        const int i = regionBox.getSelectedItemIndex();
        if (i >= 0)
            editPrimary ([r = kRegions[i]] (Scene&, Point& p) { p.stutter.region = r; });
    };
    for (int i = 0; i < (int) std::size (kDivisions); ++i)
        divisionBox.addItem (toString (kDivisions[i]), i + 1);
    divisionBox.onChange = [this]
    {
        const int i = divisionBox.getSelectedItemIndex();
        if (i >= 0)
            editPrimary ([d = kDivisions[i]] (Scene&, Point& p) { p.stutter.division = d; });
    };
    for (auto* b : { &regionBox, &divisionBox })
        content.addAndMakeVisible (*b);
    regionL.setText (u8 ("Qu\xc3\xa9 repetir"), juce::dontSendNotification);
    divisionL.setText (u8 ("Divisi\xc3\xb3n"), juce::dontSendNotification);
    for (auto* l : { &regionL, &divisionL })
    {
        l->setFont (theme::font (12.5f));
        l->setColour (juce::Label::textColourId, theme::textDim);
        content.addAndMakeVisible (*l);
    }

    setupSlider (repeats, repeatsL, "Repeticiones", 2, 32, 1, "");
    setupSlider (sliceLength, sliceLengthL, u8 ("Duraci\xc3\xb3n corte"), 10, 100, 1, " %");
    setupSlider (offset, offsetL, u8 ("Posici\xc3\xb3n"), 0, 8, 0.25, " t");
    setupSlider (stutterGain, stutterGainL, "Ganancia", -36, 12, 0.1, " dB");
    setupSlider (manualStart, manualStartL, "Inicio sel.", 0, 100, 1, " %");
    setupSlider (manualEnd, manualEndL, "Fin sel.", 0, 100, 1, " %");
    stutterGain.setDoubleClickReturnValue (true, 0.0);
    repeats.onValueChange = [this] { editPrimary ([v = (int) repeats.getValue()] (Scene&, Point& p) { p.stutter.repeats = v; p.stutter = clamped (p.stutter); }); };
    sliceLength.onValueChange = [this] { editPrimary ([v = sliceLength.getValue()] (Scene&, Point& p) { p.stutter.sliceLengthPercent = v; }); };
    offset.onValueChange = [this] { editPrimary ([v = offset.getValue()] (Scene&, Point& p) { p.stutter.offsetBeats = v; }); };
    stutterGain.onValueChange = [this] { editPrimary ([v = stutterGain.getValue()] (Scene&, Point& p) { p.stutter.gainDb = v; }); };
    manualStart.onValueChange = [this] { editPrimary ([v = manualStart.getValue() / 100.0] (Scene&, Point& p) { p.stutter.manualStart = v; p.stutter = clamped (p.stutter); }); };
    manualEnd.onValueChange = [this] { editPrimary ([v = manualEnd.getValue() / 100.0] (Scene&, Point& p) { p.stutter.manualEnd = v; p.stutter = clamped (p.stutter); }); };
    stutterNote.setText (u8 ("Aceleraci\xc3\xb3n progresiva: pr\xc3\xb3ximamente."), juce::dontSendNotification);
    stutterNote.setFont (theme::font (11.0f));
    stutterNote.setColour (juce::Label::textColourId, theme::textFaint);
    content.addAndMakeVisible (stutterNote);

    // Character / speed: stored only (no processing yet).
    soundInfo.setFont (theme::font (12.0f, true));
    soundInfo.setColour (juce::Label::textColourId, theme::textDim);
    content.addAndMakeVisible (soundInfo);
    pendingNote.setText (u8 ("Car\xc3\xa1" "cter y velocidad: PR\xc3\x93XIMAMENTE. Se guardan; a\xc3\xban no suenan."), juce::dontSendNotification);
    pendingNote.setFont (theme::font (11.0f));
    pendingNote.setColour (juce::Label::textColourId, theme::textFaint);
    content.addAndMakeVisible (pendingNote);

    emptyNote.setText (u8 ("Selecciona una repetici\xc3\xb3n en el mapa o en la l\xc3\xadnea temporal.\n\n"
                           "Doble clic en el mapa: a\xc3\xb1" "adir.  Shift+clic: selecci\xc3\xb3n m\xc3\xbaltiple."),
                       juce::dontSendNotification);
    emptyNote.setFont (theme::font (13.0f));
    emptyNote.setColour (juce::Label::textColourId, theme::textDim);
    emptyNote.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (emptyNote);

    refresh();
}

PointPanel::~PointPanel()
{
    ctx.removeListener (this);
}

void PointPanel::setupSlider (juce::Slider& s, juce::Label& l, const juce::String& name, double lo, double hi, double step,
                              const juce::String& suffix)
{
    s.setSliderStyle (juce::Slider::LinearHorizontal);
    s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 22);
    s.setRange (lo, hi, step);
    s.setTextValueSuffix (suffix);
    content.addAndMakeVisible (s);
    l.setText (name, juce::dontSendNotification);
    l.setFont (theme::font (12.5f));
    l.setColour (juce::Label::textColourId, theme::textDim);
    content.addAndMakeVisible (l);
}

void PointPanel::setProMode (bool shouldBePro)
{
    pro = shouldBePro;
    const double grid = beatsPerStep (ctx.scene().rhythm);
    beat.setRange (0.0, 32.0, pro ? 0.001 : grid);
    pitch.setRange (-24.0, 24.0, pro ? 0.01 : 1.0);
    posX.setNumDecimalPlacesToDisplay (pro ? 1 : 0);
    posY.setNumDecimalPlacesToDisplay (pro ? 1 : 0);
    beat.setNumDecimalPlacesToDisplay (pro ? 3 : 2);
    pitch.setNumDecimalPlacesToDisplay (pro ? 2 : 0);
    refresh();
    resized();
}

void PointPanel::editPrimary (const std::function<void (Scene&, Point&)>& fn)
{
    const auto id = ctx.primary();
    if (id.isEmpty())
        return;
    ctx.edit ([&] (Scene& s)
    {
        if (auto* p = findPoint (s, id))
            fn (s, *p);
    });
}

void PointPanel::editSelection (const std::function<void (Scene&, Point&)>& fn)
{
    const auto ids = ctx.selection();
    if (ids.isEmpty())
        return;
    ctx.edit ([&] (Scene& s)
    {
        for (auto& id : ids)
            if (auto* p = findPoint (s, id))
                fn (s, *p);
    });
}

void PointPanel::refreshPending()
{
    const auto* p = ctx.primaryPoint();
    const bool pending = p != nullptr && ctx.processor.isPitchPending (*p);
    const auto text = pending ? u8 ("procesando tono\xe2\x80\xa6") : juce::String();
    if (pitchStatus.getText() != text)
        pitchStatus.setText (text, juce::dontSendNotification);
}

void PointPanel::refresh()
{
    const auto* p = ctx.primaryPoint();
    const bool has = p != nullptr;
    viewport.setVisible (has);
    emptyNote.setVisible (! has);
    if (! has)
    {
        repaint();
        return;
    }

    if (! pro)
        beat.setRange (0.0, 32.0, beatsPerStep (ctx.scene().rhythm));

    const int count = ctx.selection().size();
    title.setText (u8 ("Repetici\xc3\xb3n ") + juce::String (p->number)
                       + (count > 1 ? "  (+" + juce::String (count - 1) + ")" : juce::String()),
                   juce::dontSendNotification);

    int presetIndex = -1;
    const auto& presets = builtInVoicePresets();
    for (int i = 0; i < (int) presets.size(); ++i)
        if (presets[(size_t) i].id == p->voice.presetId)
            presetIndex = i;
    presetBox.setSelectedItemIndex (presetIndex, juce::dontSendNotification);
    if (presetIndex < 0)
        presetBox.setText (p->voice.presetName.isNotEmpty() ? p->voice.presetName : "Original", juce::dontSendNotification);

    juce::String note = count > 1 ? u8 ("Preset y pitch se aplican a las ") + juce::String (count) + " seleccionadas. " : juce::String();
    if (presetIndex >= 0 && presets[(size_t) presetIndex].needsFutureProcessing)
        note << u8 ("Este preset s\xc3\xb3lo aplica su tono por ahora; su car\xc3\xa1" "cter llega m\xc3\xa1s adelante.");
    else
        note << u8 ("El preset es un punto de partida: ajusta el pitch libremente.");
    if (p->voice.modified)
        note = "(modificado) " + note;
    presetNote.setText (note, juce::dontSendNotification);

    auto setIfIdle = [] (juce::Slider& s, double v)
    {
        if (! s.isMouseButtonDown())
            s.setValue (v, juce::dontSendNotification);
    };
    setIfIdle (pitch, p->sound.pitchSemitones);
    setIfIdle (gain, p->gainDb);
    setIfIdle (posX, p->position.x);
    setIfIdle (posY, p->position.y);
    setIfIdle (beat, p->timing.beat);
    soundInfo.setText (u8 ("Car\xc3\xa1" "cter ") + juce::String (p->sound.character, 0)
                           + u8 ("  \xc2\xb7  Velocidad ") + juce::String (p->sound.speedPercent, 0) + " %",
                       juce::dontSendNotification);
    enabledToggle.setToggleState (p->enabled, juce::dontSendNotification);
    soloToggle.setToggleState (p->solo, juce::dontSendNotification);

    const auto& st = p->stutter;
    stutterToggle.setToggleState (st.enabled, juce::dontSendNotification);
    for (int i = 0; i < (int) std::size (kRegions); ++i)
        if (kRegions[i] == st.region)
            regionBox.setSelectedItemIndex (i, juce::dontSendNotification);
    for (int i = 0; i < (int) std::size (kDivisions); ++i)
        if (kDivisions[i] == st.division)
            divisionBox.setSelectedItemIndex (i, juce::dontSendNotification);
    setIfIdle (repeats, st.repeats);
    setIfIdle (sliceLength, st.sliceLengthPercent);
    setIfIdle (offset, st.offsetBeats);
    setIfIdle (stutterGain, st.gainDb);
    setIfIdle (manualStart, st.manualStart * 100.0);
    setIfIdle (manualEnd, st.manualEnd * 100.0);
    const bool manual = st.region == StutterSettings::Region::Manual;
    for (juce::Component* c : { (juce::Component*) &regionBox, (juce::Component*) &divisionBox, (juce::Component*) &repeats,
                                (juce::Component*) &sliceLength, (juce::Component*) &offset, (juce::Component*) &stutterGain })
        c->setEnabled (st.enabled);
    manualStart.setEnabled (st.enabled && manual);
    manualEnd.setEnabled (st.enabled && manual);
    pasteButton.setEnabled (stutterClipboard.has_value());

    refreshPending();
    resized();
    repaint();
}

void PointPanel::paint (juce::Graphics& g)
{
    theme::drawPanel (g, getLocalBounds().toFloat(), u8 ("REPETICI\xc3\x93N SELECCIONADA"));
    if (const auto* p = ctx.primaryPoint())
    {
        const auto t = title.getBounds().toFloat().translated ((float) viewport.getX(), (float) (viewport.getY() - viewport.getViewPositionY()));
        g.setColour (theme::pointColour (p->number));
        g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ t.getX() - 10.0f, t.getCentreY() }));
    }
}

int PointPanel::layoutContent (int width)
{
    auto r = juce::Rectangle<int> (0, 0, width, 10000);
    auto row = [&] (int h) { auto x = r.removeFromTop (h); r.removeFromTop (3); return x; };
    auto labelled = [&] (juce::Label& l, juce::Component& c, bool visible = true)
    {
        l.setVisible (visible);
        c.setVisible (visible);
        if (! visible)
            return;
        auto x = row (27);
        l.setBounds (x.removeFromLeft (104));
        c.setBounds (x);
    };

    auto head = row (28);
    triggerButton.setBounds (head.removeFromRight (108));
    title.setBounds (head.withTrimmedLeft (18));
    r.removeFromTop (2);

    labelled (presetL, presetBox);
    presetNote.setBounds (row (28).withTrimmedLeft (104));
    {
        auto x = row (27);
        pitchL.setBounds (x.removeFromLeft (104));
        pitchStatus.setBounds (x.removeFromRight (0));
        pitch.setBounds (x);
        pitchStatus.setBounds (pitch.getBounds().withTrimmedRight (70).removeFromRight (120).translated (0, -11).withHeight (12));
    }
    labelled (gainL, gain);
    labelled (posXL, posX);
    labelled (posYL, posY);
    labelled (beatL, beat);

    auto actions = row (30);
    const int w = actions.getWidth() / 4;
    enabledToggle.setBounds (actions.removeFromLeft (w));
    soloToggle.setBounds (actions.removeFromLeft (w - 6));
    duplicateButton.setBounds (actions.removeFromLeft (w).reduced (2, 0));
    deleteButton.setBounds (actions.reduced (2, 0));

    r.removeFromTop (8);
    auto sh = row (24);
    stutterToggle.setBounds (sh.removeFromRight (96));
    stutterTitle.setBounds (sh);
    auto cp = row (26);
    const int half = cp.getWidth() / 2;
    copyButton.setBounds (cp.removeFromLeft (half).reduced (2, 0));
    pasteButton.setBounds (cp.reduced (2, 0));
    labelled (regionL, regionBox);
    const bool manual = regionBox.getSelectedItemIndex() == 3;
    labelled (manualStartL, manualStart, pro && manual);
    labelled (manualEndL, manualEnd, pro && manual);
    labelled (divisionL, divisionBox);
    labelled (repeatsL, repeats);
    labelled (sliceLengthL, sliceLength);
    labelled (offsetL, offset, pro);
    labelled (stutterGainL, stutterGain, pro);
    stutterNote.setBounds (row (16));

    r.removeFromTop (6);
    soundInfo.setBounds (row (18));
    pendingNote.setBounds (row (16));
    return r.getY();
}

void PointPanel::resized()
{
    auto r = getLocalBounds().reduced (16, 10);
    r.removeFromTop (22);
    emptyNote.setBounds (r);
    viewport.setBounds (r);
    // Width reserved for the scrollbar so a tall layout never jumps when it appears.
    const int width = juce::jmax (100, r.getWidth() - 10);
    content.setSize (width, layoutContent (width));
}

} // namespace ses
