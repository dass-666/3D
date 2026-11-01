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
constexpr CharacterSettings::Type kCharTypes[] { CharacterSettings::Type::Saturation, CharacterSettings::Type::Overdrive,
                                                 CharacterSettings::Type::Bitcrusher, CharacterSettings::Type::Telephone,
                                                 CharacterSettings::Type::LoFi, CharacterSettings::Type::Robot };
const char* kCharNames[] { "Saturaci\xc3\xb3n suave", "Overdrive", "Bitcrusher", "Tel\xc3\xa9" "fono", "Lo-fi", "Robot (anillo)" };
constexpr ModulationSettings::Type kModTypes[] { ModulationSettings::Type::Chorus, ModulationSettings::Type::Flanger,
                                                 ModulationSettings::Type::Tremolo };
const char* kModNames[] { "Chorus", "Flanger", "Tr\xc3\xa9molo" };
constexpr ReverbSettings::Type kReverbTypes[] { ReverbSettings::Type::Room, ReverbSettings::Type::Hall, ReverbSettings::Type::Plate };
const char* kReverbNames[] { "Sala (room)", "Hall", "Plate" };
constexpr ShaperSettings::Distribution kDists[] { ShaperSettings::Distribution::Uniform, ShaperSettings::Distribution::Accelerating,
                                                  ShaperSettings::Distribution::Decelerating };
const char* kDistNames[] { "Uniforme", "Acelerando", "Desacelerando" };
constexpr ShaperSettings::Shape kShapes[] { ShaperSettings::Shape::Square, ShaperSettings::Shape::Triangle, ShaperSettings::Shape::Saw };
const char* kShapeNames[] { "Cuadrada", "Triangular", "Diente de sierra" };
constexpr ShaperSettings::RateMode kRates[] { ShaperSettings::RateMode::Count, ShaperSettings::RateMode::Milliseconds,
                                              ShaperSettings::RateMode::Division };
const char* kRateNames[] { "N\xc2\xba de cortes", "Milisegundos", "Divisi\xc3\xb3n del BPM" };
}

std::optional<StutterSettings> PointPanel::stutterClipboard;

ChainOrderView::ChainOrderView()
{
    for (int i = 0; i < 4; ++i)
    {
        auto& row = rows[i];
        row.enable.onClick = [this, i] { if (onEnableChange) onEnableChange (rows[i].stage, rows[i].enable.getToggleState()); };
        row.up.onClick = [this, i] { move (i, -1); };
        row.down.onClick = [this, i] { move (i, +1); };
        addAndMakeVisible (row.enable);
        addAndMakeVisible (row.up);
        addAndMakeVisible (row.down);
    }
    setState ({});
}

void ChainOrderView::setState (const PointEffects& fx)
{
    current = normalisedChainOrder (fx.chainOrder);
    for (int i = 0; i < 4; ++i)
    {
        auto& row = rows[i];
        row.stage = current[i];
        const bool on = row.stage == 'c' ? fx.character.enabled : row.stage == 'm' ? fx.modulation.enabled
                      : row.stage == 'd' ? fx.delay.enabled : fx.reverb.enabled;
        row.enable.setButtonText (juce::String (i + 1) + ".  " + chainStageName (row.stage));
        row.enable.setToggleState (on, juce::dontSendNotification);
        row.up.setEnabled (i > 0);
        row.down.setEnabled (i < 3);
    }
    repaint();
}

void ChainOrderView::move (int index, int delta)
{
    const int j = index + delta;
    if (j < 0 || j > 3)
        return;
    auto chars = current.toStdString();
    std::swap (chars[(size_t) index], chars[(size_t) j]);
    if (onOrderChange)
        onOrderChange (juce::String (chars));
}

void ChainOrderView::resized()
{
    auto r = getLocalBounds();
    for (auto& row : rows)
    {
        auto line = r.removeFromTop (kRowHeight);
        row.down.setBounds (line.removeFromRight (28).reduced (1));
        row.up.setBounds (line.removeFromRight (28).reduced (1));
        row.enable.setBounds (line);
    }
}

void ChainOrderView::paint (juce::Graphics& g)
{
    g.setColour (juce::Colours::white.withAlpha (0.03f));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
}

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
        presetBox.addItem (p.name, id++);
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

    // Creative presets: a whole recipe (sound, curve and/or effect chain) for the selection.
    creativeL.setText ("Preset creativo", juce::dontSendNotification);
    creativeL.setFont (theme::font (12.5f));
    creativeL.setColour (juce::Label::textColourId, theme::textDim);
    content.addAndMakeVisible (creativeL);
    creativeBox.setTextWhenNothingSelected (u8 ("Elegir\xe2\x80\xa6"));
    rebuildCreativeBox();
    UserPresetStore::shared().addChangeListener (this);
    creativeBox.setTooltip (u8 ("Aplica cortes, efectos y/o voz a las repeticiones seleccionadas. Todo con procesamiento real."));
    creativeBox.onChange = [this]
    {
        const int id = creativeBox.getSelectedId();
        const auto& presets = pointPresets();
        if (id >= 1 && id <= (int) presets.size())
            editSelection ([&preset = presets[(size_t) id - 1]] (Scene&, Point& p) { applyPointPreset (p, preset); });
        else if (id > 1000)
        {
            const auto& user = UserPresetStore::shared().presets();
            if (id - 1001 < (int) user.size())
                editSelection ([up = user[(size_t) id - 1001]] (Scene&, Point& p) { applyUserPreset (p, up); });
        }
        creativeBox.setSelectedId (0, juce::dontSendNotification);
    };
    content.addAndMakeVisible (creativeBox);

    setupSlider (pitch, pitchL, "Pitch", -24, 24, 1, " st");
    pitch.setDoubleClickReturnValue (true, 0.0);
    pitch.onValueChange = [this] { editSelection ([v = pitch.getValue()] (Scene& s, Point& p) { auto snd = p.sound; snd.pitchSemitones = v; setSound (s, p.id, snd); }); };
    setupSlider (formant, formantL, "Formantes", limits::minFormant, limits::maxFormant, 0.5, " st");
    formant.setDoubleClickReturnValue (true, 0.0);
    formant.setTooltip (u8 ("Mueve el timbre (formantes) sin cambiar el tono: abajo = voz m\xc3\xa1s grande, arriba = m\xc3\xa1s fina. Se pre-renderiza."));
    formant.onValueChange = [this] { editSelection ([v = formant.getValue()] (Scene& s, Point& p) { auto snd = p.sound; snd.formantSemitones = v; setSound (s, p.id, snd); }); };
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
    soundTitle.setText ("PITCH Y FORMANTES", juce::dontSendNotification);
    timeTitle.setText ("TIEMPO DEL PUNTO", juce::dontSendNotification);
    spaceTitle.setText ("NIVEL Y ESPACIO", juce::dontSendNotification);
    for (auto* h : { &soundTitle, &timeTitle, &spaceTitle })
    {
        h->setFont (theme::font (11.5f, true));
        h->setColour (juce::Label::textColourId, theme::textDim);
        content.addAndMakeVisible (*h);
    }
    for (auto* h : { &soundTitle, &timeTitle, &spaceTitle, &stutterTitle, &effectsTitle, &chainTitle, &charTitle, &modTitle, &delayTitle,
                     &reverbTitle, &shaperTitle })
        h->onToggle = [this] { resized(); };
    for (auto* h : { &stutterTitle, &effectsTitle, &chainTitle, &charTitle, &modTitle, &delayTitle, &reverbTitle })
        h->setOpen (false);
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
    setupSlider (stutterAccel, stutterAccelL, u8 ("Aceleraci\xc3\xb3n"), -100, 100, 1, " %");
    stutterAccel.setDoubleClickReturnValue (true, 0.0);
    stutterAccel.setTooltip (u8 ("> 0: los cortes se acercan progresivamente (como una pelota que bota); < 0: se separan. La duraci\xc3\xb3n total no cambia."));
    stutterAccel.onValueChange = [this] { editPrimary ([v = stutterAccel.getValue()] (Scene&, Point& p) { p.stutter.accelPercent = v; }); };
    manualStart.onValueChange = [this] { editPrimary ([v = manualStart.getValue() / 100.0] (Scene&, Point& p) { p.stutter.manualStart = v; p.stutter = clamped (p.stutter); }); };
    manualEnd.onValueChange = [this] { editPrimary ([v = manualEnd.getValue() / 100.0] (Scene&, Point& p) { p.stutter.manualEnd = v; p.stutter = clamped (p.stutter); }); };
    stutterNote.setText (u8 ("Lo que se dibuja en la l\xc3\xadnea temporal es lo que suena."), juce::dontSendNotification);
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
    effectsNote.setText (u8 ("Gate = Volume Shaper (abajo). Cadena por repetici\xc3\xb3n, en este orden: car\xc3\xa1" "cter \xe2\x86\x92 modulaci\xc3\xb3n \xe2\x86\x92 delay \xe2\x86\x92 reverb."), juce::dontSendNotification);
    effectsNote.setFont (theme::font (11.0f));
    effectsNote.setColour (juce::Label::textColourId, theme::textFaint);
    content.addAndMakeVisible (effectsNote);

    auto fxTitle = [this] (SectionHeader& l, const char* text, juce::ToggleButton& t)
    {
        l.setText (u8 (text), juce::dontSendNotification);
        l.setFont (theme::font (11.5f, true));
        l.setColour (juce::Label::textColourId, theme::textDim);
        content.addAndMakeVisible (l);
        content.addAndMakeVisible (t);
    };
    auto fxCombo = [this] (juce::ComboBox& b, juce::Label& l, const char* text, const char* const* names, int count)
    {
        l.setText (u8 (text), juce::dontSendNotification);
        l.setFont (theme::font (12.5f));
        l.setColour (juce::Label::textColourId, theme::textDim);
        content.addAndMakeVisible (l);
        for (int i = 0; i < count; ++i)
            b.addItem (u8 (names[i]), i + 1);
        content.addAndMakeVisible (b);
    };
    auto fxEdit = [this] (auto fn) { return [this, fn] { editPrimary ([fn] (Scene& s, Point& p) { auto e = p.effects; fn (e); setEffects (s, p.id, e); }); }; };

    // Chain order
    chainTitle.setText (u8 ("CADENA DE EFECTOS (ORDEN)"), juce::dontSendNotification);
    chainTitle.setFont (theme::font (11.5f, true));
    chainTitle.setColour (juce::Label::textColourId, theme::textDim);
    content.addAndMakeVisible (chainTitle);
    content.addAndMakeVisible (chainOrder);
    chainOrder.onOrderChange = [this, fxEdit] (const juce::String& order) { fxEdit ([order] (PointEffects& e) { e.chainOrder = order; })(); };
    chainOrder.onEnableChange = [this, fxEdit] (juce::juce_wchar stage, bool on)
    {
        fxEdit ([stage, on] (PointEffects& e)
        {
            if (stage == 'c') e.character.enabled = on;
            else if (stage == 'm') e.modulation.enabled = on;
            else if (stage == 'd') e.delay.enabled = on;
            else e.reverb.enabled = on;
        })();
    };

    // Character
    fxTitle (charTitle, "CAR\xc3\x81" "CTER / DISTORSI\xc3\x93N", charToggle);
    charToggle.onClick = [this, fxEdit] { fxEdit ([on = charToggle.getToggleState()] (PointEffects& e) { e.character.enabled = on; })(); };
    fxCombo (charTypeBox, charTypeL, "Tipo", kCharNames, (int) std::size (kCharNames));
    charTypeBox.onChange = [this, fxEdit] { if (const int i = charTypeBox.getSelectedItemIndex(); i >= 0) fxEdit ([t = kCharTypes[i]] (PointEffects& e) { e.character.type = t; })(); };
    setupSlider (charAmount, charAmountL, "Cantidad", 0, 100, 1, " %");
    setupSlider (charMix, charMixL, "Mezcla", 0, 100, 1, " %");
    charAmount.setTooltip (u8 ("Drive / resoluci\xc3\xb3n / frecuencia de la portadora (Robot: 20\xe2\x80\x93" "220 Hz)"));
    charAmount.onValueChange = [this, fxEdit] { fxEdit ([v = charAmount.getValue()] (PointEffects& e) { e.character.amountPercent = v; })(); };
    charMix.onValueChange = [this, fxEdit] { fxEdit ([v = charMix.getValue()] (PointEffects& e) { e.character.mixPercent = v; })(); };

    // Modulation
    fxTitle (modTitle, "MODULACI\xc3\x93N", modToggle);
    modToggle.onClick = [this, fxEdit] { fxEdit ([on = modToggle.getToggleState()] (PointEffects& e) { e.modulation.enabled = on; })(); };
    fxCombo (modTypeBox, modTypeL, "Tipo", kModNames, (int) std::size (kModNames));
    modTypeBox.onChange = [this, fxEdit] { if (const int i = modTypeBox.getSelectedItemIndex(); i >= 0) fxEdit ([t = kModTypes[i]] (PointEffects& e) { e.modulation.type = t; })(); };
    setupSlider (modRate, modRateL, "Velocidad", 0.05, 20, 0.05, " Hz");
    modRate.setSkewFactorFromMidPoint (2.0);
    setupSlider (modDepth, modDepthL, "Profundidad", 0, 100, 1, " %");
    setupSlider (modMix, modMixL, "Mezcla", 0, 100, 1, " %");
    modRate.onValueChange = [this, fxEdit] { fxEdit ([v = modRate.getValue()] (PointEffects& e) { e.modulation.rateHz = v; })(); };
    modDepth.onValueChange = [this, fxEdit] { fxEdit ([v = modDepth.getValue()] (PointEffects& e) { e.modulation.depthPercent = v; })(); };
    modMix.onValueChange = [this, fxEdit] { fxEdit ([v = modMix.getValue()] (PointEffects& e) { e.modulation.mixPercent = v; })(); };

    // Delay
    fxTitle (delayTitle, "DELAY", delayToggle);
    delayToggle.onClick = [this, fxEdit] { fxEdit ([on = delayToggle.getToggleState()] (PointEffects& e) { e.delay.enabled = on; })(); };
    delaySyncToggle.onClick = [this, fxEdit] { fxEdit ([on = delaySyncToggle.getToggleState()] (PointEffects& e) { e.delay.sync = on; })(); };
    content.addAndMakeVisible (delaySyncToggle);
    setupSlider (delayTime, delayTimeL, "Tiempo", 1, limits::maxDelayMs, 1, " ms");
    delayTime.setSkewFactorFromMidPoint (250.0);
    fxCombo (delayDivisionBox, delayDivisionL, "Divisi\xc3\xb3n", nullptr, 0);
    for (int i = 0; i < (int) std::size (kDivisions); ++i)
        delayDivisionBox.addItem (toString (kDivisions[i]), i + 1);
    delayDivisionBox.onChange = [this, fxEdit] { if (const int i = delayDivisionBox.getSelectedItemIndex(); i >= 0) fxEdit ([d = kDivisions[i]] (PointEffects& e) { e.delay.division = d; })(); };
    setupSlider (delayFeedback, delayFeedbackL, "Feedback", 0, 95, 1, " %");
    pingPongToggle.onClick = [this, fxEdit] { fxEdit ([on = pingPongToggle.getToggleState()] (PointEffects& e) { e.delay.pingPong = on; })(); };
    content.addAndMakeVisible (pingPongToggle);
    setupSlider (delayLowpass, delayLowpassL, "Filtro (LP)", 500, 20000, 10, " Hz");
    delayLowpass.setSkewFactorFromMidPoint (4000.0);
    setupSlider (delayMix, delayMixL, "Mezcla", 0, 100, 1, " %");
    delayTime.onValueChange = [this, fxEdit] { fxEdit ([v = delayTime.getValue()] (PointEffects& e) { e.delay.timeMs = v; })(); };
    delayFeedback.onValueChange = [this, fxEdit] { fxEdit ([v = delayFeedback.getValue()] (PointEffects& e) { e.delay.feedbackPercent = v; })(); };
    delayLowpass.onValueChange = [this, fxEdit] { fxEdit ([v = delayLowpass.getValue()] (PointEffects& e) { e.delay.lowpassHz = v; })(); };
    delayMix.onValueChange = [this, fxEdit] { fxEdit ([v = delayMix.getValue()] (PointEffects& e) { e.delay.mixPercent = v; })(); };

    // Reverb
    fxTitle (reverbTitle, "REVERB", reverbToggle);
    reverbToggle.onClick = [this, fxEdit] { fxEdit ([on = reverbToggle.getToggleState()] (PointEffects& e) { e.reverb.enabled = on; })(); };
    fxCombo (reverbTypeBox, reverbTypeL, "Tipo", kReverbNames, (int) std::size (kReverbNames));
    reverbTypeBox.onChange = [this, fxEdit] { if (const int i = reverbTypeBox.getSelectedItemIndex(); i >= 0) fxEdit ([t = kReverbTypes[i]] (PointEffects& e) { e.reverb.type = t; })(); };
    setupSlider (reverbSize, reverbSizeL, u8 ("Tama\xc3\xb1o"), 0, 100, 1, " %");
    setupSlider (reverbDecay, reverbDecayL, "Decay", 0, 100, 1, " %");
    setupSlider (reverbPredelay, reverbPredelayL, "Predelay", 0, limits::maxPredelayMs, 1, " ms");
    setupSlider (reverbMix, reverbMixL, "Mezcla", 0, 100, 1, " %");
    setupSlider (reverbOutput, reverbOutputL, "Salida", -24, 6, 0.5, " dB");
    reverbSize.onValueChange = [this, fxEdit] { fxEdit ([v = reverbSize.getValue()] (PointEffects& e) { e.reverb.sizePercent = v; })(); };
    reverbDecay.onValueChange = [this, fxEdit] { fxEdit ([v = reverbDecay.getValue()] (PointEffects& e) { e.reverb.decayPercent = v; })(); };
    reverbPredelay.onValueChange = [this, fxEdit] { fxEdit ([v = reverbPredelay.getValue()] (PointEffects& e) { e.reverb.predelayMs = v; })(); };
    reverbMix.onValueChange = [this, fxEdit] { fxEdit ([v = reverbMix.getValue()] (PointEffects& e) { e.reverb.mixPercent = v; })(); };
    reverbOutput.onValueChange = [this, fxEdit] { fxEdit ([v = reverbOutput.getValue()] (PointEffects& e) { e.reverb.outputDb = v; })(); };
    chainNote.setText (u8 ("Hasta 16 repeticiones con cadena a la vez; las dem\xc3\xa1s suenan sin efectos. Las colas siguen al editar."), juce::dontSendNotification);
    chainNote.setFont (theme::font (11.0f));
    chainNote.setColour (juce::Label::textColourId, theme::textFaint);
    content.addAndMakeVisible (chainNote);

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
    for (int i = 0; i < (int) std::size (kDists); ++i)
        distBox.addItem (u8 (kDistNames[i]), i + 1);
    distBox.onChange = [this]
    {
        const int i = distBox.getSelectedItemIndex();
        if (i >= 0)
            editPrimary ([v = kDists[i]] (Scene& s, Point& p) { auto x = p.shaper; x.distribution = v; setShaper (s, p.id, x); });
    };
    shaperPresetL.setText ("Preset", juce::dontSendNotification);
    shapeL.setText ("Forma", juce::dontSendNotification);
    rateL.setText ("Ritmo", juce::dontSendNotification);
    shaperDivisionL.setText (u8 ("Divisi\xc3\xb3n"), juce::dontSendNotification);
    distL.setText (u8 ("Distribuci\xc3\xb3n"), juce::dontSendNotification);
    patternL.setText (u8 ("Patr\xc3\xb3n"), juce::dontSendNotification);
    for (auto* l : { &shaperPresetL, &shapeL, &rateL, &shaperDivisionL, &distL, &patternL })
    {
        l->setFont (theme::font (12.5f));
        l->setColour (juce::Label::textColourId, theme::textDim);
        content.addAndMakeVisible (*l);
    }
    for (auto* b : { &shapeBox, &rateBox, &shaperDivisionBox, &distBox })
        content.addAndMakeVisible (*b);
    setupSlider (cuts, cutsL, "Cortes", limits::minShaperCuts, limits::maxShaperCuts, 1, "");
    cuts.setSkewFactorFromMidPoint (32.0);
    setupSlider (periodMs, periodMsL, "Periodo", limits::minShaperPeriodMs, limits::maxShaperPeriodMs, 1, " ms");
    periodMs.setSkewFactorFromMidPoint (200.0);
    setupSlider (depth, depthL, "Profundidad", 0, 100, 1, " %");
    setupSlider (openWidth, openWidthL, "Apertura", 5, 95, 1, " %");
    setupSlider (shaperAttack, shaperAttackL, "Ataque", limits::minShaperRampMs, limits::maxShaperAttackMs, 0.1, " ms");
    shaperAttack.setSkewFactorFromMidPoint (3.0);
    setupSlider (shaperRelease, shaperReleaseL, u8 ("Liberaci\xc3\xb3n"), limits::minShaperRampMs, limits::maxShaperReleaseMs, 0.1, " ms");
    shaperRelease.setSkewFactorFromMidPoint (10.0);
    setupSlider (accel, accelL, "Intensidad", 0, 100, 1, " %");
    setupSlider (shaperLength, shaperLengthL, u8 ("Duraci\xc3\xb3n"), 0, 4000, 1, " ms");
    shaperLength.setSkewFactorFromMidPoint (500.0);
    shaperLength.textFromValueFunction = [] (double v) { return v <= 0.0 ? juce::String ("hasta el final") : juce::String ((int) v) + " ms"; };
    setupSlider (makeup, makeupL, "Ganancia post", -limits::maxShaperMakeupDb, limits::maxShaperMakeupDb, 0.5, " dB");
    setupSlider (shaperRegionStart, shaperRegionStartL, "Inicio zona", 0, 100, 1, " %");
    setupSlider (shaperRegionEnd, shaperRegionEndL, "Fin zona", 0, 100, 1, " %");
    openWidth.setTooltip (u8 ("Parte de cada corte que queda abierta (forma cuadrada). Menos apertura = m\xc3\xa1s silencio y menos nivel medio."));
    shaperAttack.setTooltip (u8 ("Rampa lineal al abrir cada corte. Corta conserva el transitorio; larga suaviza."));
    shaperRelease.setTooltip (u8 ("Rampa lineal al cerrar cada corte."));
    accel.setTooltip (u8 ("Cu\xc3\xa1nto se acortan o alargan los cortes seg\xc3\xban la distribuci\xc3\xb3n (solo N\xc2\xba de cortes)"));
    shaperLength.setTooltip (u8 ("Duraci\xc3\xb3n de la zona cortada; 0 = hasta el final de la zona"));
    makeup.setTooltip (u8 ("Ganancia tras la curva. El limitador de salida sigue protegiendo."));
    autoLevelToggle.setTooltip (u8 ("Recupera el nivel RMS que quitan los silencios (m\xc3\xa1ximo +6 dB)"));
    patternEdit.setTooltip (u8 ("x = corte abierto, . = corte mudo; se repite. Vac\xc3\xado = todos abiertos."));
    patternEdit.setInputRestrictions (64, "x.X");
    patternEdit.setFont (theme::font (13.0f));
    patternEdit.onReturnKey = [this] { patternEdit.giveAwayKeyboardFocus(); };
    patternEdit.onFocusLost = [this]
    {
        editPrimary ([v = patternEdit.getText().toLowerCase()] (Scene& s, Point& p) { auto x = p.shaper; x.pattern = v; setShaper (s, p.id, x); });
    };
    content.addAndMakeVisible (patternEdit);
    shaperHint.setFont (theme::font (11.0f));
    shaperHint.setColour (juce::Label::textColourId, theme::textDim);
    content.addAndMakeVisible (shaperHint);
    cuts.onValueChange = [this] { editPrimary ([v = (int) cuts.getValue()] (Scene& s, Point& p) { auto x = p.shaper; x.cuts = v; setShaper (s, p.id, x); }); };
    periodMs.onValueChange = [this] { editPrimary ([v = periodMs.getValue()] (Scene& s, Point& p) { auto x = p.shaper; x.periodMs = v; setShaper (s, p.id, x); }); };
    depth.onValueChange = [this] { editPrimary ([v = depth.getValue()] (Scene& s, Point& p) { auto x = p.shaper; x.depthPercent = v; setShaper (s, p.id, x); }); };
    openWidth.onValueChange = [this] { editPrimary ([v = openWidth.getValue()] (Scene& s, Point& p) { auto x = p.shaper; x.openPercent = v; setShaper (s, p.id, x); }); };
    shaperAttack.onValueChange = [this] { editPrimary ([v = shaperAttack.getValue()] (Scene& s, Point& p) { auto x = p.shaper; x.attackMs = v; setShaper (s, p.id, x); }); };
    shaperRelease.onValueChange = [this] { editPrimary ([v = shaperRelease.getValue()] (Scene& s, Point& p) { auto x = p.shaper; x.releaseMs = v; setShaper (s, p.id, x); }); };
    shaperLength.onValueChange = [this] { editPrimary ([v = shaperLength.getValue()] (Scene& s, Point& p) { auto x = p.shaper; x.lengthMs = v; setShaper (s, p.id, x); }); };
    makeup.onValueChange = [this] { editPrimary ([v = makeup.getValue()] (Scene& s, Point& p) { auto x = p.shaper; x.makeupDb = v; setShaper (s, p.id, x); }); };
    autoLevelToggle.onClick = [this] { editPrimary ([on = autoLevelToggle.getToggleState()] (Scene& s, Point& p) { auto x = p.shaper; x.autoLevel = on; setShaper (s, p.id, x); }); };
    content.addAndMakeVisible (autoLevelToggle);
    accel.onValueChange = [this] { editPrimary ([v = accel.getValue()] (Scene& s, Point& p) { auto x = p.shaper; x.accelPercent = v; setShaper (s, p.id, x); }); };
    shaperWholeToggle.onClick = [this] { editPrimary ([on = shaperWholeToggle.getToggleState()] (Scene& s, Point& p) { auto x = p.shaper; x.wholeVoice = on; setShaper (s, p.id, x); }); };
    content.addAndMakeVisible (shaperWholeToggle);
    shaperRegionStart.onValueChange = [this] { editPrimary ([v = shaperRegionStart.getValue() / 100.0] (Scene& s, Point& p) { auto x = p.shaper; x.regionStart = v; x.regionEnd = juce::jmax (v, x.regionEnd); setShaper (s, p.id, x); }); };
    shaperRegionEnd.onValueChange = [this] { editPrimary ([v = shaperRegionEnd.getValue() / 100.0] (Scene& s, Point& p) { auto x = p.shaper; x.regionEnd = v; x.regionStart = juce::jmin (v, x.regionStart); setShaper (s, p.id, x); }); };
    shaperNote.setText (u8 ("La curva se ve en la l\xc3\xadnea temporal y en la onda. El editor permite zoom, regla en ms y puntos manuales."), juce::dontSendNotification);
    curveEditorButton.setColour (juce::TextButton::buttonColourId, theme::violet);
    curveEditorButton.setTooltip (u8 ("Abre el editor visual de la curva (zoom, regla en ms, puntos manuales, deshacer, A/B)"));
    curveEditorButton.onClick = [this] { if (onOpenCurveEditor) onOpenCurveEditor(); };
    content.addAndMakeVisible (curveEditorButton);
    shaperNote.setFont (theme::font (11.0f));
    shaperNote.setColour (juce::Label::textColourId, theme::textFaint);
    content.addAndMakeVisible (shaperNote);

    // Character / speed: stored only (no processing yet).
    soundInfo.setFont (theme::font (12.0f, true));
    soundInfo.setColour (juce::Label::textColourId, theme::textDim);
    content.addAndMakeVisible (soundInfo);
    soundInfo.setVisible (false);   // legacy stored values (character/speed): not processed, not shown
    pendingNote.setVisible (false);

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
    UserPresetStore::shared().removeChangeListener (this);
    ctx.removeListener (this);
}

void PointPanel::rebuildCreativeBox()
{
    creativeBox.clear (juce::dontSendNotification);
    int cid = 1;
    for (auto& cp : pointPresets())
        creativeBox.addItem (u8 (cp.name), cid++);
    const auto& user = UserPresetStore::shared().presets();
    if (! user.empty())
    {
        creativeBox.addSeparator();
        creativeBox.addSectionHeading (u8 ("Propios"));
        int uid = 1001;
        for (auto& up : user)
            creativeBox.addItem (up.name, uid++);
    }
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
    note << u8 ("Punto de partida: ajusta pitch y formantes libremente.");
    if (p->voice.modified)
        note = "(modificado) " + note;
    presetNote.setText (note, juce::dontSendNotification);

    auto setIfIdle = [] (juce::Slider& s, double v)
    {
        if (! s.isMouseButtonDown())
            s.setValue (v, juce::dontSendNotification);
    };
    setIfIdle (pitch, p->sound.pitchSemitones);
    setIfIdle (formant, p->sound.formantSemitones);
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
    setIfIdle (stutterAccel, st.accelPercent);
    setIfIdle (manualStart, st.manualStart * 100.0);
    setIfIdle (manualEnd, st.manualEnd * 100.0);
    const bool manual = st.region == StutterSettings::Region::Manual;
    for (juce::Component* c : { (juce::Component*) &regionBox, (juce::Component*) &divisionBox, (juce::Component*) &repeats,
                                (juce::Component*) &sliceLength, (juce::Component*) &offset, (juce::Component*) &stutterGain, (juce::Component*) &stutterAccel })
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
    charToggle.setToggleState (fx.character.enabled, juce::dontSendNotification);
    chainOrder.setState (fx);
    modToggle.setToggleState (fx.modulation.enabled, juce::dontSendNotification);
    delayToggle.setToggleState (fx.delay.enabled, juce::dontSendNotification);
    reverbToggle.setToggleState (fx.reverb.enabled, juce::dontSendNotification);
    delaySyncToggle.setToggleState (fx.delay.sync, juce::dontSendNotification);
    pingPongToggle.setToggleState (fx.delay.pingPong, juce::dontSendNotification);
    for (int i = 0; i < (int) std::size (kCharTypes); ++i)
        if (kCharTypes[i] == fx.character.type)
            charTypeBox.setSelectedItemIndex (i, juce::dontSendNotification);
    for (int i = 0; i < (int) std::size (kModTypes); ++i)
        if (kModTypes[i] == fx.modulation.type)
            modTypeBox.setSelectedItemIndex (i, juce::dontSendNotification);
    for (int i = 0; i < (int) std::size (kReverbTypes); ++i)
        if (kReverbTypes[i] == fx.reverb.type)
            reverbTypeBox.setSelectedItemIndex (i, juce::dontSendNotification);
    for (int i = 0; i < (int) std::size (kDivisions); ++i)
        if (kDivisions[i] == fx.delay.division)
            delayDivisionBox.setSelectedItemIndex (i, juce::dontSendNotification);
    setIfIdle (charAmount, fx.character.amountPercent);
    setIfIdle (charMix, fx.character.mixPercent);
    setIfIdle (modRate, fx.modulation.rateHz);
    setIfIdle (modDepth, fx.modulation.depthPercent);
    setIfIdle (modMix, fx.modulation.mixPercent);
    setIfIdle (delayTime, fx.delay.timeMs);
    setIfIdle (delayFeedback, fx.delay.feedbackPercent);
    setIfIdle (delayLowpass, fx.delay.lowpassHz);
    setIfIdle (delayMix, fx.delay.mixPercent);
    setIfIdle (reverbSize, fx.reverb.sizePercent);
    setIfIdle (reverbDecay, fx.reverb.decayPercent);
    setIfIdle (reverbPredelay, fx.reverb.predelayMs);
    setIfIdle (reverbMix, fx.reverb.mixPercent);
    setIfIdle (reverbOutput, fx.reverb.outputDb);
    for (auto* c : std::initializer_list<juce::Component*> { &charTypeBox, &charAmount, &charMix })
        c->setEnabled (fx.character.enabled);
    for (auto* c : std::initializer_list<juce::Component*> { &modTypeBox, &modRate, &modDepth, &modMix })
        c->setEnabled (fx.modulation.enabled);
    for (auto* c : std::initializer_list<juce::Component*> { &delaySyncToggle, &delayTime, &delayDivisionBox, &delayFeedback,
                                                             &pingPongToggle, &delayLowpass, &delayMix })
        c->setEnabled (fx.delay.enabled);
    for (auto* c : std::initializer_list<juce::Component*> { &reverbTypeBox, &reverbSize, &reverbDecay, &reverbPredelay,
                                                             &reverbMix, &reverbOutput })
        c->setEnabled (fx.reverb.enabled);

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
    setIfIdle (shaperAttack, sh.attackMs);
    setIfIdle (shaperRelease, sh.releaseMs);
    setIfIdle (accel, sh.accelPercent);
    setIfIdle (shaperLength, sh.lengthMs);
    setIfIdle (makeup, sh.makeupDb);
    for (int i = 0; i < (int) std::size (kDists); ++i)
        if (kDists[i] == sh.distribution)
            distBox.setSelectedItemIndex (i, juce::dontSendNotification);
    if (! patternEdit.hasKeyboardFocus (true))
        patternEdit.setText (sh.pattern, juce::dontSendNotification);
    autoLevelToggle.setToggleState (sh.autoLevel, juce::dontSendNotification);
    {
        // What the curve really does to the level, so nobody has to guess.
        double fragmentSeconds = 0.0;
        if (auto audio = ctx.processor.getSourceAudio())
        {
            const auto& src = ctx.scene().source;
            const auto total = (juce::int64) audio->buffer.getNumSamples();
            const auto end = src.trimEnd > src.trimStart ? juce::jmin (src.trimEnd, total) : total;
            fragmentSeconds = juce::jmax (0.0, (double) (end - src.trimStart) / audio->sampleRate);
        }
        const double voiceSeconds = fragmentSeconds * juce::jmax (0.0, p->region.end - p->region.start);
        juce::String hint;
        if (sh.enabled && sh.rateMode == ShaperSettings::RateMode::Count && voiceSeconds > 0.0)
        {
            double a = 0.0, b = 0.0;
            shaperSpan (sh, voiceSeconds, a, b);
            const int useful = maxUsefulCuts (sh, b - a);
            hint << u8 ("M\xc3\xa1x. \xc3\xbatil aqu\xc3\xad: ") << useful << " cortes";
            if (sh.cuts > useful)
                hint << u8 (" (los sobrantes ser\xc3\xa1n s\xc3\xb3lo chasquidos)");
            hint << ". ";
        }
        if (sh.enabled)
        {
            const float level = shaperLevelGain (sh, voiceSeconds, 0.125);
            hint << "Nivel: " << juce::String (juce::Decibels::gainToDecibels (level), 1) << " dB";
        }
        shaperHint.setText (hint, juce::dontSendNotification);
    }
    shaperWholeToggle.setToggleState (sh.wholeVoice, juce::dontSendNotification);
    setIfIdle (shaperRegionStart, sh.regionStart * 100.0);
    setIfIdle (shaperRegionEnd, sh.regionEnd * 100.0);
    for (juce::Component* c : { (juce::Component*) &shapeBox, (juce::Component*) &rateBox, (juce::Component*) &shaperDivisionBox,
                                (juce::Component*) &cuts, (juce::Component*) &periodMs, (juce::Component*) &depth,
                                (juce::Component*) &openWidth, (juce::Component*) &shaperAttack, (juce::Component*) &shaperRelease,
                                (juce::Component*) &accel, (juce::Component*) &distBox, (juce::Component*) &patternEdit,
                                (juce::Component*) &shaperLength, (juce::Component*) &makeup, (juce::Component*) &autoLevelToggle,
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

SectionHeader* PointPanel::headerFor (const juce::String& id)
{
    if (id == "shaper") return &shaperTitle;
    if (id == "stutter") return &stutterTitle;
    if (id == "pitch") return &soundTitle;
    if (id == "filters") return &effectsTitle;
    if (id == "delay") return &delayTitle;
    if (id == "reverb") return &reverbTitle;
    if (id == "modulation") return &modTitle;
    if (id == "character") return &charTitle;
    if (id == "chain") return &chainTitle;
    if (id == "space") return &spaceTitle;
    if (id == "time") return &timeTitle;
    return nullptr;
}

void PointPanel::showSection (const juce::String& id)
{
    auto* h = headerFor (id);
    if (h == nullptr)
        return;
    h->setOpen (true);
    resized();
    if (const auto it = sectionY.find (id); it != sectionY.end())
        viewport.setViewPosition (0, juce::jmax (0, it->second - 4));
}

int PointPanel::layoutContent (int width)
{
    auto r = juce::Rectangle<int> (0, 0, width, 10000);
    bool open = true;  // of the section being laid out
    auto row = [&] (int h) { auto x = r.removeFromTop (h); r.removeFromTop (3); return x; };
    auto place = [&] (juce::Component& c, int h, bool visible = true)
    {
        c.setVisible (visible && open);
        if (visible && open)
            c.setBounds (row (h));
    };
    auto labelled = [&] (juce::Label& l, juce::Component& c, bool visible = true)
    {
        l.setVisible (visible && open);
        c.setVisible (visible && open);
        if (! (visible && open))
            return;
        auto x = row (27);
        l.setBounds (x.removeFromLeft (104));
        c.setBounds (x);
    };
    auto section = [&] (const char* id, SectionHeader& h, juce::ToggleButton* toggle = nullptr)
    {
        r.removeFromTop (6);
        sectionY[id] = r.getY();
        auto x = row (24);
        if (toggle != nullptr)
        {
            toggle->setVisible (true);
            toggle->setBounds (x.removeFromRight (96));
        }
        h.setBounds (x);
        open = h.isOpen();
    };

    auto head = row (28);
    triggerButton.setBounds (head.removeFromRight (108));
    title.setBounds (head.withTrimmedLeft (18));
    r.removeFromTop (2);
    labelled (presetL, presetBox);
    presetNote.setBounds (row (28).withTrimmedLeft (104));
    labelled (creativeL, creativeBox);
    {
        auto actions = row (30);
        const int w = actions.getWidth() / 4;
        enabledToggle.setBounds (actions.removeFromLeft (w));
        soloToggle.setBounds (actions.removeFromLeft (w - 6));
        duplicateButton.setBounds (actions.removeFromLeft (w).reduced (2, 0));
        deleteButton.setBounds (actions.reduced (2, 0));
    }

    const auto* pt = ctx.primaryPoint();
    const PointEffects fxNow = pt != nullptr ? pt->effects : PointEffects {};

    auto layoutSound = [&]
    {
        section ("pitch", soundTitle);
        pitchL.setVisible (open); pitch.setVisible (open); pitchStatus.setVisible (open);
        if (open)
        {
            auto x = row (27);
            pitchL.setBounds (x.removeFromLeft (104));
            pitch.setBounds (x);
            pitchStatus.setBounds (pitch.getBounds().withTrimmedRight (70).removeFromRight (120).translated (0, -11).withHeight (12));
        }
        labelled (formantL, formant);
    };
    auto layoutTime = [&]
    {
        section ("time", timeTitle);
        labelled (beatL, beat);
        labelled (regionStartL, regionStart, pro);
        labelled (regionEndL, regionEnd, pro);
    };
    auto layoutSpace = [&]
    {
        section ("space", spaceTitle);
        labelled (gainL, gain);
        labelled (posXL, posX);
        labelled (posYL, posY);
    };
    auto layoutStutter = [&]
    {
        section ("stutter", stutterTitle, &stutterToggle);
        copyButton.setVisible (open); pasteButton.setVisible (open);
        if (open)
        {
            auto cp = row (26);
            const int half = cp.getWidth() / 2;
            copyButton.setBounds (cp.removeFromLeft (half).reduced (2, 0));
            pasteButton.setBounds (cp.reduced (2, 0));
        }
        labelled (regionL, regionBox);
        const bool manual = regionBox.getSelectedItemIndex() == 3;
        labelled (manualStartL, manualStart, pro && manual);
        labelled (manualEndL, manualEnd, pro && manual);
        labelled (divisionL, divisionBox);
        labelled (repeatsL, repeats);
        labelled (sliceLengthL, sliceLength);
        labelled (offsetL, offset, pro);
        labelled (stutterAccelL, stutterAccel);
        labelled (stutterGainL, stutterGain, pro);
        place (stutterNote, 16);
    };
    auto layoutFilters = [&]
    {
        section ("filters", effectsTitle);
        labelled (fadeInL, fadeIn);
        labelled (fadeOutL, fadeOut);
        place (reverseToggle, 24);
        labelled (lowpassL, lowpass);
        labelled (highpassL, highpass);
        place (effectsNote, 16);
    };
    auto layoutChain = [&]
    {
        section ("chain", chainTitle);
        place (chainOrder, ChainOrderView::kRowHeight * 4);
        place (chainNote, 16);
    };
    auto layoutChar = [&]
    {
        section ("character", charTitle, &charToggle);
        labelled (charTypeL, charTypeBox);
        labelled (charAmountL, charAmount);
        labelled (charMixL, charMix, pro);
    };
    auto layoutMod = [&]
    {
        section ("modulation", modTitle, &modToggle);
        labelled (modTypeL, modTypeBox);
        labelled (modRateL, modRate);
        labelled (modDepthL, modDepth);
        labelled (modMixL, modMix, pro && fxNow.modulation.type != ModulationSettings::Type::Tremolo);
    };
    auto layoutDelay = [&]
    {
        section ("delay", delayTitle, &delayToggle);
        place (delaySyncToggle, 24);
        labelled (delayTimeL, delayTime, ! fxNow.delay.sync);
        labelled (delayDivisionL, delayDivisionBox, fxNow.delay.sync);
        labelled (delayFeedbackL, delayFeedback);
        place (pingPongToggle, 24);
        labelled (delayLowpassL, delayLowpass, pro);
        labelled (delayMixL, delayMix);
    };
    auto layoutReverb = [&]
    {
        section ("reverb", reverbTitle, &reverbToggle);
        labelled (reverbTypeL, reverbTypeBox);
        labelled (reverbSizeL, reverbSize);
        labelled (reverbDecayL, reverbDecay);
        labelled (reverbPredelayL, reverbPredelay, pro);
        labelled (reverbMixL, reverbMix);
        labelled (reverbOutputL, reverbOutput, pro);
    };
    auto layoutShaper = [&]
    {
        section ("shaper", shaperTitle, &shaperToggle);
        labelled (shaperPresetL, shaperPresetBox);
        labelled (shapeL, shapeBox);
        labelled (rateL, rateBox);
        const int rate = rateBox.getSelectedItemIndex();
        labelled (cutsL, cuts, rate <= 0);
        labelled (periodMsL, periodMs, rate == 1);
        labelled (shaperDivisionL, shaperDivisionBox, rate == 2);
        labelled (depthL, depth);
        labelled (openWidthL, openWidth, shapeBox.getSelectedItemIndex() <= 0);
        labelled (distL, distBox, rate <= 0);
        labelled (accelL, accel, rate <= 0 && distBox.getSelectedItemIndex() > 0);
        labelled (shaperAttackL, shaperAttack, pro);
        labelled (shaperReleaseL, shaperRelease, pro);
        labelled (patternL, patternEdit, pro);
        labelled (shaperLengthL, shaperLength, pro);
        labelled (makeupL, makeup, pro);
        place (autoLevelToggle, 24);
        place (shaperHint, 16);
        place (shaperWholeToggle, 24, pro);
        const bool zone = pro && ! shaperWholeToggle.getToggleState();
        labelled (shaperRegionStartL, shaperRegionStart, zone);
        labelled (shaperRegionEndL, shaperRegionEnd, zone);
        place (curveEditorButton, 26);
        place (shaperNote, 30);
    };

    if (pro)
    {
        layoutShaper(); layoutStutter(); layoutSound(); layoutFilters(); layoutChain(); layoutChar(); layoutMod(); layoutDelay(); layoutReverb();
        layoutTime(); layoutSpace();
    }
    else
    {
        layoutSound(); layoutSpace(); layoutTime(); layoutShaper(); layoutStutter(); layoutFilters(); layoutChain(); layoutChar(); layoutMod();
        layoutDelay(); layoutReverb();
    }
    r.removeFromTop (6);
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
