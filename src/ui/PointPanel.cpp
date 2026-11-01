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
constexpr ShaperSettings::Shape kShapes[] { ShaperSettings::Shape::Square, ShaperSettings::Shape::Triangle, ShaperSettings::Shape::Saw };
const char* kShapeNames[] { "Cuadrada", "Triangular", "Diente de sierra" };
constexpr ShaperSettings::RateMode kRates[] { ShaperSettings::RateMode::Count, ShaperSettings::RateMode::Milliseconds,
                                              ShaperSettings::RateMode::Division };
const char* kRateNames[] { "N\xc2\xba de cortes (uniformes)", "Milisegundos", "Divisi\xc3\xb3n del BPM" };
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
    setupSlider (regionStart, regionStartL, "Inicio fragm.", 0, 100, 0.1, " %");
    setupSlider (regionEnd, regionEndL, "Fin fragm.", 0, 100, 0.1, " %");
    regionStart.setTooltip (u8 ("Parte del fragmento que reproduce esta repetici\xc3\xb3n (no destructivo)"));
    regionStart.onValueChange = [this] { editPrimary ([v = regionStart.getValue() / 100.0] (Scene& s, Point& p) { setRegion (s, p.id, { v, juce::jmax (v, p.region.end) }); }); };
    regionEnd.onValueChange = [this] { editPrimary ([v = regionEnd.getValue() / 100.0] (Scene& s, Point& p) { setRegion (s, p.id, { juce::jmin (v, p.region.start), v }); }); };
    posX.onValueChange = [this] { editPrimary ([v = posX.getValue()] (Scene& s, Point& p) { movePoint (s, p.id, { v, p.position.y }); }); };
    posY.onValueChange = [this] { editPrimary ([v = posY.getValue()] (Scene& s, Point& p) { movePoint (s, p.id, { p.position.x, v }); }); };
    beat.onValueChange = [this]
    {
        editPrimary ([v = beat.getValue(), free = freeTime] (Scene& s, Point& p)
        {
            auto t = p.timing;
            if (free)
                t.seconds = v / 1000.0;
            else
                t.beat = v;
            setTiming (s, p.id, t);
        });
    };

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

    // ---- Effects (real processing only)
    effectsTitle.setText (u8 ("EFECTOS DE LA REPETICI\xc3\x93N"), juce::dontSendNotification);
    effectsTitle.setFont (theme::font (11.5f, true));
    effectsTitle.setColour (juce::Label::textColourId, theme::textDim);
    content.addAndMakeVisible (effectsTitle);
    setupSlider (fadeIn, fadeInL, "Fade in", 0, 2000, 1, " ms");
    setupSlider (fadeOut, fadeOutL, "Fade out", 0, 2000, 1, " ms");
    setupSlider (lowpass, lowpassL, "Paso bajo", 200, 20000, 1, " Hz");
    setupSlider (highpass, highpassL, "Paso alto", 20, 5000, 1, " Hz");
    fadeIn.setSkewFactorFromMidPoint (300.0);
    fadeOut.setSkewFactorFromMidPoint (300.0);
    lowpass.setSkewFactorFromMidPoint (2000.0);
    highpass.setSkewFactorFromMidPoint (300.0);
    lowpass.setDoubleClickReturnValue (true, 20000.0);
    highpass.setDoubleClickReturnValue (true, 20.0);
    fadeIn.onValueChange = [this] { editPrimary ([v = fadeIn.getValue()] (Scene& s, Point& p) { auto e = p.effects; e.fadeInMs = v; setEffects (s, p.id, e); }); };
    fadeOut.onValueChange = [this] { editPrimary ([v = fadeOut.getValue()] (Scene& s, Point& p) { auto e = p.effects; e.fadeOutMs = v; setEffects (s, p.id, e); }); };
    lowpass.onValueChange = [this] { editPrimary ([v = lowpass.getValue()] (Scene& s, Point& p) { auto e = p.effects; e.lowpassHz = v; setEffects (s, p.id, e); }); };
    highpass.onValueChange = [this] { editPrimary ([v = highpass.getValue()] (Scene& s, Point& p) { auto e = p.effects; e.highpassHz = v; setEffects (s, p.id, e); }); };
    reverseToggle.onClick = [this] { editPrimary ([on = reverseToggle.getToggleState()] (Scene& s, Point& p) { auto e = p.effects; e.reverse = on; setEffects (s, p.id, e); }); };
    content.addAndMakeVisible (reverseToggle);
    effectsNote.setText (u8 ("Gate = Volume Shaper. Delay, reverb, modulaci\xc3\xb3n y formantes: pr\xc3\xb3ximamente."), juce::dontSendNotification);
    effectsNote.setFont (theme::font (11.0f));
    effectsNote.setColour (juce::Label::textColourId, theme::textFaint);
    content.addAndMakeVisible (effectsNote);

    // ---- Volume Shaper
    shaperTitle.setText ("VOLUME SHAPER / CORTES", juce::dontSendNotification);
    shaperTitle.setFont (theme::font (11.5f, true));
    shaperTitle.setColour (juce::Label::textColourId, theme::textDim);
    content.addAndMakeVisible (shaperTitle);
    shaperToggle.setTooltip (u8 ("Curva de volumen autom\xc3\xa1tica sobre la repetici\xc3\xb3n (arriba-abajo-arriba), independiente del Stutter"));
    shaperToggle.onClick = [this] { editPrimary ([on = shaperToggle.getToggleState()] (Scene&, Point& p) { p.shaper.enabled = on; }); };
    content.addAndMakeVisible (shaperToggle);
    shaperPresetBox.setTextWhenNothingSelected (u8 ("Preset de cortes\xe2\x80\xa6"));
    {
        int pid = 1;
        for (auto& pr : shaperPresets())
            shaperPresetBox.addItem (u8 (pr.name), pid++);
    }
    shaperPresetBox.onChange = [this]
    {
        const int i = shaperPresetBox.getSelectedItemIndex();
        if (i < 0)
            return;
        const auto settings = shaperPresets()[(size_t) i].settings;
        editSelection ([settings] (Scene& s, Point& p) { setShaper (s, p.id, settings); });
    };
    content.addAndMakeVisible (shaperPresetBox);
    for (int i = 0; i < (int) std::size (kShapes); ++i)
        shapeBox.addItem (u8 (kShapeNames[i]), i + 1);
    shapeBox.onChange = [this]
    {
        const int i = shapeBox.getSelectedItemIndex();
        if (i >= 0)
            editPrimary ([v = kShapes[i]] (Scene&, Point& p) { p.shaper.shape = v; });
    };
    for (int i = 0; i < (int) std::size (kRates); ++i)
        rateBox.addItem (u8 (kRateNames[i]), i + 1);
    rateBox.onChange = [this]
    {
        const int i = rateBox.getSelectedItemIndex();
        if (i >= 0)
            editPrimary ([v = kRates[i]] (Scene&, Point& p) { p.shaper.rateMode = v; });
    };
    for (int i = 0; i < (int) std::size (kDivisions); ++i)
        shaperDivisionBox.addItem (toString (kDivisions[i]), i + 1);
    shaperDivisionBox.onChange = [this]
    {
        const int i = shaperDivisionBox.getSelectedItemIndex();
        if (i >= 0)
            editPrimary ([d = kDivisions[i]] (Scene&, Point& p) { p.shaper.division = d; });
    };
    shaperPresetL.setText ("Preset", juce::dontSendNotification);
    shapeL.setText ("Forma", juce::dontSendNotification);
    rateL.setText ("Ritmo", juce::dontSendNotification);
    shaperDivisionL.setText (u8 ("Divisi\xc3\xb3n"), juce::dontSendNotification);
    for (auto* l : { &shaperPresetL, &shapeL, &rateL, &shaperDivisionL })
    {
        l->setFont (theme::font (12.5f));
        l->setColour (juce::Label::textColourId, theme::textDim);
        content.addAndMakeVisible (*l);
    }
    for (auto* b : { &shapeBox, &rateBox, &shaperDivisionBox })
        content.addAndMakeVisible (*b);
    setupSlider (cuts, cutsL, "Cortes", 2, 32, 1, "");
    setupSlider (periodMs, periodMsL, "Periodo", 10, 4000, 1, " ms");
    periodMs.setSkewFactorFromMidPoint (200.0);
    setupSlider (depth, depthL, "Profundidad", 0, 100, 1, " %");
    setupSlider (openWidth, openWidthL, "Apertura", 5, 95, 1, " %");
    setupSlider (shaperFade, shaperFadeL, "Fundido", 0, 50, 0.5, " ms");
    setupSlider (accel, accelL, u8 ("Aceleraci\xc3\xb3n"), 0, 100, 1, " %");
    setupSlider (shaperRegionStart, shaperRegionStartL, "Inicio zona", 0, 100, 1, " %");
    setupSlider (shaperRegionEnd, shaperRegionEndL, "Fin zona", 0, 100, 1, " %");
    openWidth.setTooltip (u8 ("Parte de cada corte que queda abierta (forma cuadrada)"));
    shaperFade.setTooltip (u8 ("Suavizado anticlic de cada borde"));
    accel.setTooltip (u8 ("Solo con N\xc2\xba de cortes: los cortes se acortan progresivamente"));
    cuts.onValueChange = [this] { editPrimary ([v = (int) cuts.getValue()] (Scene& s, Point& p) { auto x = p.shaper; x.cuts = v; setShaper (s, p.id, x); }); };
    periodMs.onValueChange = [this] { editPrimary ([v = periodMs.getValue()] (Scene& s, Point& p) { auto x = p.shaper; x.periodMs = v; setShaper (s, p.id, x); }); };
    depth.onValueChange = [this] { editPrimary ([v = depth.getValue()] (Scene& s, Point& p) { auto x = p.shaper; x.depthPercent = v; setShaper (s, p.id, x); }); };
    openWidth.onValueChange = [this] { editPrimary ([v = openWidth.getValue()] (Scene& s, Point& p) { auto x = p.shaper; x.openPercent = v; setShaper (s, p.id, x); }); };
    shaperFade.onValueChange = [this] { editPrimary ([v = shaperFade.getValue()] (Scene& s, Point& p) { auto x = p.shaper; x.fadeMs = v; setShaper (s, p.id, x); }); };
    accel.onValueChange = [this] { editPrimary ([v = accel.getValue()] (Scene& s, Point& p) { auto x = p.shaper; x.accelPercent = v; setShaper (s, p.id, x); }); };
    shaperWholeToggle.onClick = [this] { editPrimary ([on = shaperWholeToggle.getToggleState()] (Scene& s, Point& p) { auto x = p.shaper; x.wholeVoice = on; setShaper (s, p.id, x); }); };
    content.addAndMakeVisible (shaperWholeToggle);
    shaperRegionStart.onValueChange = [this] { editPrimary ([v = shaperRegionStart.getValue() / 100.0] (Scene& s, Point& p) { auto x = p.shaper; x.regionStart = v; x.regionEnd = juce::jmax (v, x.regionEnd); setShaper (s, p.id, x); }); };
    shaperRegionEnd.onValueChange = [this] { editPrimary ([v = shaperRegionEnd.getValue() / 100.0] (Scene& s, Point& p) { auto x = p.shaper; x.regionEnd = v; x.regionStart = juce::jmin (v, x.regionStart); setShaper (s, p.id, x); }); };
    shaperNote.setText (u8 ("Edici\xc3\xb3n manual de puntos de la curva: pr\xc3\xb3ximamente. La curva se ve en la l\xc3\xadnea temporal."), juce::dontSendNotification);
    shaperNote.setFont (theme::font (11.0f));
    shaperNote.setColour (juce::Label::textColourId, theme::textFaint);
    content.addAndMakeVisible (shaperNote);

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
    pitch.setRange (-24.0, 24.0, pro ? 0.01 : 1.0);
    posX.setNumDecimalPlacesToDisplay (pro ? 1 : 0);
    posY.setNumDecimalPlacesToDisplay (pro ? 1 : 0);
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

    freeTime = ctx.scene().timeMode == TimeMode::Free;
    if (freeTime)
    {
        // Milliseconds; the grid (optional) is applied on the timeline, not here.
        beat.setRange (0.0, 10000.0, pro ? 0.1 : 1.0);
        beat.setTextValueSuffix (" ms");
        beat.setNumDecimalPlacesToDisplay (pro ? 1 : 0);
        beatL.setText ("Momento (libre)", juce::dontSendNotification);
    }
    else
    {
        beat.setRange (0.0, 32.0, pro ? 0.001 : beatsPerStep (ctx.scene().rhythm));
        beat.setTextValueSuffix (" t");
        beat.setNumDecimalPlacesToDisplay (pro ? 3 : 2);
        beatL.setText ("Momento (BPM)", juce::dontSendNotification);
    }

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
    setIfIdle (beat, freeTime ? p->timing.seconds * 1000.0 : p->timing.beat);
    setIfIdle (regionStart, p->region.start * 100.0);
    setIfIdle (regionEnd, p->region.end * 100.0);
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

    const auto& fx = p->effects;
    setIfIdle (fadeIn, fx.fadeInMs);
    setIfIdle (fadeOut, fx.fadeOutMs);
    setIfIdle (lowpass, fx.lowpassHz);
    setIfIdle (highpass, fx.highpassHz);
    reverseToggle.setToggleState (fx.reverse, juce::dontSendNotification);

    const auto& sh = p->shaper;
    shaperToggle.setToggleState (sh.enabled, juce::dontSendNotification);
    {
        int match = -1;
        const auto& presets = shaperPresets();
        for (int i = 0; i < (int) presets.size(); ++i)
            if (presets[(size_t) i].settings == sh)
                match = i;
        shaperPresetBox.setSelectedItemIndex (match, juce::dontSendNotification);
        if (match < 0)
            shaperPresetBox.setText (sh.enabled ? "Personalizado" : juce::String(), juce::dontSendNotification);
    }
    for (int i = 0; i < (int) std::size (kShapes); ++i)
        if (kShapes[i] == sh.shape)
            shapeBox.setSelectedItemIndex (i, juce::dontSendNotification);
    for (int i = 0; i < (int) std::size (kRates); ++i)
        if (kRates[i] == sh.rateMode)
            rateBox.setSelectedItemIndex (i, juce::dontSendNotification);
    for (int i = 0; i < (int) std::size (kDivisions); ++i)
        if (kDivisions[i] == sh.division)
            shaperDivisionBox.setSelectedItemIndex (i, juce::dontSendNotification);
    setIfIdle (cuts, sh.cuts);
    setIfIdle (periodMs, sh.periodMs);
    setIfIdle (depth, sh.depthPercent);
    setIfIdle (openWidth, sh.openPercent);
    setIfIdle (shaperFade, sh.fadeMs);
    setIfIdle (accel, sh.accelPercent);
    shaperWholeToggle.setToggleState (sh.wholeVoice, juce::dontSendNotification);
    setIfIdle (shaperRegionStart, sh.regionStart * 100.0);
    setIfIdle (shaperRegionEnd, sh.regionEnd * 100.0);
    for (juce::Component* c : { (juce::Component*) &shapeBox, (juce::Component*) &rateBox, (juce::Component*) &shaperDivisionBox,
                                (juce::Component*) &cuts, (juce::Component*) &periodMs, (juce::Component*) &depth,
                                (juce::Component*) &openWidth, (juce::Component*) &shaperFade, (juce::Component*) &accel,
                                (juce::Component*) &shaperWholeToggle, (juce::Component*) &shaperRegionStart,
                                (juce::Component*) &shaperRegionEnd })
        c->setEnabled (sh.enabled);

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
    labelled (regionStartL, regionStart, pro);
    labelled (regionEndL, regionEnd, pro);

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

    r.removeFromTop (8);
    effectsTitle.setBounds (row (24));
    labelled (fadeInL, fadeIn);
    labelled (fadeOutL, fadeOut);
    reverseToggle.setBounds (row (24));
    labelled (lowpassL, lowpass);
    labelled (highpassL, highpass);
    effectsNote.setBounds (row (16));

    r.removeFromTop (8);
    auto shh = row (24);
    shaperToggle.setBounds (shh.removeFromRight (96));
    shaperTitle.setBounds (shh);
    labelled (shaperPresetL, shaperPresetBox);
    labelled (shapeL, shapeBox);
    labelled (rateL, rateBox);
    const int rate = rateBox.getSelectedItemIndex();
    labelled (cutsL, cuts, rate <= 0);
    labelled (periodMsL, periodMs, rate == 1);
    labelled (shaperDivisionL, shaperDivisionBox, rate == 2);
    labelled (depthL, depth);
    labelled (openWidthL, openWidth, shapeBox.getSelectedItemIndex() <= 0);
    labelled (accelL, accel, pro && rate <= 0);
    labelled (shaperFadeL, shaperFade, pro);
    shaperWholeToggle.setVisible (pro);
    if (pro)
        shaperWholeToggle.setBounds (row (24));
    const bool zone = pro && ! shaperWholeToggle.getToggleState();
    labelled (shaperRegionStartL, shaperRegionStart, zone);
    labelled (shaperRegionEndL, shaperRegionEnd, zone);
    shaperNote.setBounds (row (30));

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
