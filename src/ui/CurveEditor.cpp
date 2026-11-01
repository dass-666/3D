#include "ui/CurveEditor.h"

#include "audio/EchoEngine.h"
#include "ui/Theme.h"

#include <cmath>

namespace ses
{
namespace
{
juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }
constexpr float kRulerHeight = 20.0f;
constexpr float kToolbarHeight = 30.0f;
constexpr float kHitRadius = 9.0f;
}  // namespace

std::optional<std::vector<CurvePoint>> CurveEditor::curveClipboard;

CurveEditor::CurveEditor (EditorContext& c) : ctx (c)
{
    setWantsKeyboardFocus (true);
    setOpaque (true);

    title.setFont (theme::font (15.0f, true));
    title.setColour (juce::Label::textColourId, theme::text);
    addAndMakeVisible (title);
    info.setFont (theme::font (11.5f));
    info.setColour (juce::Label::textColourId, theme::textDim);
    info.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (info);
    help.setFont (theme::font (11.0f));
    help.setColour (juce::Label::textColourId, theme::textFaint);
    help.setText (u8 ("Doble clic: a\xc3\xb1" "adir punto \xc2\xb7 arrastrar: mover \xc2\xb7 clic derecho o Supr: quitar \xc2\xb7 "
                      "arrastrar en la regla: seleccionar \xc2\xb7 rueda: desplazar, Ctrl+rueda: zoom \xc2\xb7 Ctrl+Z / Ctrl+Y"),
                  juce::dontSendNotification);
    addAndMakeVisible (help);

    closeButton.onClick = [this] { if (onClose) onClose(); };
    expandButton.setTooltip (u8 ("Editor a pantalla completa"));
    expandButton.onClick = [this] { if (onExpand) onExpand(); };
    generateButton.setColour (juce::TextButton::buttonColourId, theme::violet);
    generateButton.setTooltip (u8 ("Reparte los cortes del generador (n\xc3\xbamero, distribuci\xc3\xb3n, rampas, patr\xc3\xb3n) "
                                   "en la selecci\xc3\xb3n o en toda la zona. Lo que se dibuja es lo que suena."));
    generateButton.onClick = [this] { generatePerfectCuts(); };
    manualButton.setClickingTogglesState (true);
    manualButton.setTooltip (u8 ("Envolvente continua con puntos: arr\xc3\xa1stralos en el tiempo (X) y en volumen (Y). Se parte de la forma actual."));
    manualButton.onClick = [this] { setManual (manualButton.getToggleState()); };
    cutsButton.setClickingTogglesState (true);
    cutsButton.setTooltip (u8 ("Cortes como bloques: mu\xc3\xa9velos, estira sus bordes, cambia su intensidad (borde superior), "
                               "a\xc3\xb1" "ade arrastrando en vac\xc3\xado. Se parte de la forma actual."));
    cutsButton.onClick = [this] { setCutsMode (cutsButton.getToggleState()); };
    gridButton.setClickingTogglesState (true);
    gridButton.setTooltip (u8 ("Ajustar a la rejilla del BPM. Apagada: posici\xc3\xb3n libre en ms. May\xc3\xba" "s al arrastrar: ajuste fino sin rejilla."));
    gridButton.onClick = [this] { setGrid (gridButton.getToggleState(), gridBeats); };
    for (auto [beats, name] : { std::pair { 1.0, "1/4" }, std::pair { 0.5, "1/8" }, std::pair { 0.25, "1/16" }, std::pair { 0.125, "1/32" },
                                std::pair { 0.0625, "1/64" }, std::pair { 1.0 / 3.0, "1/8 T" }, std::pair { 1.0 / 6.0, "1/16 T" } })
        gridBox.addItem (name, juce::roundToInt (beats * 3000.0));
    gridBox.setSelectedId (750, juce::dontSendNotification);
    gridBox.setTooltip (u8 ("Divisi\xc3\xb3n de la rejilla (al BPM del proyecto)"));
    gridBox.onChange = [this] { setGrid (gridOn, gridBox.getSelectedId() / 3000.0); };
    addAndMakeVisible (cutsButton);
    addAndMakeVisible (gridButton);
    addAndMakeVisible (gridBox);
    hScroll.setAutoHide (false);
    hScroll.setRangeLimits (0.0, 1.0);
    hScroll.addListener (this);
    addAndMakeVisible (hScroll);
    bakeButton.setTooltip (u8 ("Convierte la forma generada (cortes, rampas, patr\xc3\xb3n) en puntos editables"));
    bakeButton.onClick = [this] { bakeFromGenerator(); };
    undoButton.onClick = [this] { undo(); };
    redoButton.onClick = [this] { redo(); };
    copyButton.onClick = [this] { copyCurve(); };
    pasteButton.onClick = [this] { pasteCurve(); };
    dupButton.setTooltip (u8 ("Repite la curva dos veces en el mismo tiempo"));
    dupButton.onClick = [this] { duplicateCurve(); };
    mirrorButton.setTooltip (u8 ("Invierte la curva en el tiempo"));
    mirrorButton.onClick = [this] { mirrorCurve(); };
    previewButton.setColour (juce::TextButton::buttonColourId, theme::blue);
    previewButton.setTooltip (u8 ("Reproduce solo esta repetici\xc3\xb3n con la curva actual"));
    previewButton.onClick = [this]
    {
        if (const auto* p = ctx.primaryPoint())
            ctx.processor.requestTriggerPoint (p->id);
    };
    storeBButton.setTooltip (u8 ("Guarda la curva actual como B para compararla"));
    storeBButton.onClick = [this] { storeB(); };
    abButton.setTooltip (u8 ("Alterna entre la curva A (actual) y la B guardada"));
    abButton.onClick = [this] { toggleAB(); };
    regionButton.setTooltip (u8 ("Aplica la curva solo dentro de la selecci\xc3\xb3n"));
    regionButton.onClick = [this] { selectionToRegion(); };
    zoomInButton.onClick = [this] { zoomBy (0.6, 0.5); };
    zoomOutButton.onClick = [this] { zoomBy (1.0 / 0.6, 0.5); };
    zoomFitButton.onClick = [this] { fitView(); };
    vZoomInButton.onClick = [this] { vZoom = juce::jmin (16.0, vZoom * 1.5); repaint(); };
    vZoomOutButton.onClick = [this] { vZoom = juce::jmax (1.0, vZoom / 1.5); repaint(); };
    for (auto* b : { &closeButton, &expandButton, &generateButton, &manualButton, &bakeButton, &undoButton, &redoButton, &copyButton, &pasteButton, &dupButton,
                     &mirrorButton, &previewButton, &storeBButton, &abButton, &regionButton, &zoomInButton, &zoomOutButton,
                     &zoomFitButton, &vZoomInButton, &vZoomOutButton })
        addAndMakeVisible (*b);

    ctx.addListener (this);
    selectionChanged();
}

CurveEditor::~CurveEditor()
{
    ctx.removeListener (this);
}

// ---- model access ----------------------------------------------------------

const ShaperSettings* CurveEditor::current() const
{
    const auto* p = ctx.primaryPoint();
    return p != nullptr ? &p->shaper : nullptr;
}

int CurveEditor::numPoints() const
{
    const auto* sh = current();
    return sh != nullptr ? (int) sh->curve.size() : 0;
}

double CurveEditor::voiceSeconds() const
{
    const auto* p = ctx.primaryPoint();
    if (p != nullptr && p->id == kLivePointId)  // live: one cycle at the project tempo
        return clamped (ctx.scene().live).cycleBeats * 60.0 / ctx.processor.getProjectBpm();
    auto audio = ctx.processor.getSourceAudio();
    if (p == nullptr || audio == nullptr || audio->sampleRate <= 0.0)
        return 0.0;
    const auto& src = ctx.scene().source;
    const auto total = (juce::int64) audio->buffer.getNumSamples();
    const auto end = src.trimEnd > src.trimStart ? juce::jmin (src.trimEnd, total) : total;
    const double fragment = (double) juce::jmax<juce::int64> (0, end - src.trimStart) / audio->sampleRate;
    const auto region = clamped (p->region);
    return fragment * (region.end - region.start);
}

std::pair<double, double> CurveEditor::spanFractions() const
{
    const auto* sh = current();
    const double len = voiceSeconds();
    double a = 0.0, b = 0.0;
    if (sh == nullptr || len <= 0.0 || ! shaperSpan (*sh, len, a, b))
        return { 0.0, 1.0 };
    return { a / len, b / len };
}

void CurveEditor::apply (const ShaperSettings& sh, bool rec)
{
    const auto* p = ctx.primaryPoint();
    if (p == nullptr)
        return;
    ctx.edit ([id = p->id, sh] (Scene& s) { setShaper (s, id, sh); });
    if (rec)
        record();
    refreshButtons();
    repaint();
}

void CurveEditor::record()
{
    const auto* sh = current();
    if (sh == nullptr)
        return;
    if (! history.empty() && history[historyPos] == *sh)
        return;
    if (! history.empty())
        history.resize (historyPos + 1);
    history.push_back (*sh);
    if (history.size() > 200)
        history.erase (history.begin());
    historyPos = history.size() - 1;
}

void CurveEditor::sceneChanged()
{
    const auto* p = ctx.primaryPoint();
    if (p == nullptr || p->id != pointId)
    {
        selectionChanged();
        return;
    }
    rebuildPeaks();
    refreshButtons();
    repaint();
}

void CurveEditor::selectionChanged()
{
    const auto* p = ctx.primaryPoint();
    const auto id = p != nullptr ? p->id : juce::String();
    if (id != pointId)
    {
        pointId = id;
        history.clear();
        historyPos = 0;
        stateB.reset();
        showingB = false;
        selection.reset();
        selected = hovered = dragging = -1;
        selBlock = hoverBlock = dragBlock = -1;
        note.clear();
        fitView();
        if (p != nullptr)
            record();
    }
    const bool live = p != nullptr && p->id == kLivePointId;
    title.setText (live ? u8 ("Curva de volumen \xc2\xb7 Mixer en vivo (un ciclo)")
                        : p != nullptr ? u8 ("Curva de volumen \xc2\xb7 repetici\xc3\xb3n ") + juce::String (p->number)
                                       : u8 ("Curva de volumen \xc2\xb7 sin repetici\xc3\xb3n seleccionada"),
                   juce::dontSendNotification);
    rebuildPeaks();
    refreshButtons();
    repaint();
}

void CurveEditor::visibilityChanged()
{
    if (isVisible())
    {
        selectionChanged();
        if (isShowing())
            grabKeyboardFocus();
    }
}

void CurveEditor::refreshButtons()
{
    if (onStateChanged)
        onStateChanged();
    const auto* sh = current();
    const bool has = sh != nullptr;
    const bool manual = isPointsMode();
    manualButton.setToggleState (manual, juce::dontSendNotification);
    cutsButton.setToggleState (isCutsMode(), juce::dontSendNotification);
    cutsButton.setEnabled (has && voiceSeconds() > 0.0);
    undoButton.setEnabled (canUndo());
    redoButton.setEnabled (canRedo());
    copyButton.setEnabled (has);
    pasteButton.setEnabled (has && curveClipboard.has_value());
    dupButton.setEnabled (has);
    mirrorButton.setEnabled (has);
    abButton.setEnabled (stateB.has_value());
    abButton.setButtonText (showingB ? "B > A" : "A > B");
    regionButton.setEnabled (has && selection.has_value());
    for (auto* b : { &manualButton, &bakeButton, &previewButton, &storeBButton, &generateButton })
        b->setEnabled (has && voiceSeconds() > 0.0);
    if (const auto* p = ctx.primaryPoint(); p != nullptr && p->id == kLivePointId)
        previewButton.setEnabled (false);  // live: the curve plays on the incoming audio
    generateButton.setButtonText (selection ? u8 ("Generar cortes en la selecci\xc3\xb3n") : u8 ("Generar cortes perfectos"));

    juce::String text;
    if (has)
    {
        const double len = voiceSeconds();
        if (isCutsMode())
            text << u8 ("Cortes manuales: ") << juce::String (sh->blocks.size()) << u8 (" \xc2\xb7 cerrado ")
                 << juce::String (100.0 - sh->depthPercent, 0) << " %"
                 << (selBlock >= 0 ? u8 (" \xc2\xb7 ") + blockLabel (selBlock) : juce::String());
        else if (manual)
            text << u8 ("Curva manual: ") << juce::String (sh->curve.size()) << u8 (" puntos \xc2\xb7 los controles de cortes no se aplican");
        else
            text << u8 ("Generador: ") << (sh->rateMode == ShaperSettings::RateMode::Count ? juce::String (sh->cuts) + " cortes"
                                        : sh->rateMode == ShaperSettings::RateMode::Milliseconds ? juce::String (sh->periodMs, 0) + " ms"
                                                                                                  : toString (sh->division));
        if (! sh->enabled)
            text << u8 (" \xc2\xb7 Volume Shaper desactivado");
        if (selection)
            text << u8 (" \xc2\xb7 selecci\xc3\xb3n ") << juce::String (selection->first * len * 1000.0, 0) << u8 ("\xe2\x80\x93")
                 << juce::String (selection->second * len * 1000.0, 0) << " ms";
        if (hovered >= 0)
            text << u8 (" \xc2\xb7 ") << pointLabel (hovered);
        text << u8 (" \xc2\xb7 nivel ") << juce::String (juce::Decibels::gainToDecibels (shaperLevelGain (*sh, len, 0.125)), 1) << " dB";
    }
    if (note.isNotEmpty())
        text = note + u8 ("  \xc2\xb7  ") + text;
    info.setText (text, juce::dontSendNotification);
    help.setText (isCutsMode()
                      ? u8 ("Arrastrar en vac\xc3\xado: nuevo corte \xc2\xb7 cuerpo: mover \xc2\xb7 bordes: estirar \xc2\xb7 borde superior: intensidad \xc2\xb7 "
                            "May\xc3\xba" "s: fino sin rejilla \xc2\xb7 \xe2\x86\x90\xe2\x86\x92: 1 ms \xc2\xb7 Ctrl+D duplicar \xc2\xb7 Supr borrar \xc2\xb7 clic derecho: men\xc3\xba \xc2\xb7 Espacio: escuchar")
                      : u8 ("Doble clic: a\xc3\xb1" "adir punto \xc2\xb7 arrastrar: mover en X e Y \xc2\xb7 May\xc3\xba" "s: fino \xc2\xb7 clic derecho o Supr: quitar \xc2\xb7 "
                            "regla: seleccionar \xc2\xb7 rueda: desplazar, Ctrl+rueda: zoom \xc2\xb7 Espacio: escuchar \xc2\xb7 Ctrl+Z / Ctrl+Y"),
                  juce::dontSendNotification);
}

juce::String CurveEditor::pointLabel (int index) const
{
    const auto* sh = current();
    if (sh == nullptr || index < 0 || index >= (int) sh->curve.size())
        return {};
    const auto [a, b] = spanFractions();
    const double ms = (a + sh->curve[(size_t) index].t * (b - a)) * voiceSeconds() * 1000.0;
    return juce::String (ms, 1) + " ms, " + juce::String (sh->curve[(size_t) index].gain * 100.0, 0) + " %";
}

// ---- actions ---------------------------------------------------------------

void CurveEditor::setManual (bool on)
{
    const auto* sh = current();
    if (sh == nullptr)
        return;
    auto x = *sh;
    if (! on && x.manualKind != ShaperSettings::ManualKind::Points)
        return;  // «Editar puntos» was not the active tool
    if (on && x.curve.size() < 2)
    {
        // Start from what sounds now (generator or cuts).
        auto active = x;
        active.custom = x.custom;
        x.curve = active.custom ? bakeActiveCurve (active, (spanFractions().second - spanFractions().first) * voiceSeconds(), 0.125)
                                : bakeCurve (x, (spanFractions().second - spanFractions().first) * voiceSeconds(), 0.125);
    }
    x.custom = on;
    x.manualKind = ShaperSettings::ManualKind::Points;
    x.enabled = x.enabled || on;
    apply (x, true);
}

void CurveEditor::bakeFromGenerator()
{
    const auto* sh = current();
    if (sh == nullptr)
        return;
    auto x = *sh;
    const auto [a, b] = spanFractions();
    x.curve = bakeCurve (x, (b - a) * voiceSeconds(), 0.125);
    x.custom = true;
    x.manualKind = ShaperSettings::ManualKind::Points;
    x.enabled = true;
    apply (x, true);
}

void CurveEditor::addPoint (double t, double gain)
{
    const auto* sh = current();
    if (sh == nullptr)
        return;
    auto x = *sh;
    if (! x.custom)
    {
        const auto [a, b] = spanFractions();
        x.curve = bakeCurve (x, (b - a) * voiceSeconds(), 0.125);
        x.custom = true;
        x.enabled = true;
    }
    if ((int) x.curve.size() >= limits::maxShaperCurvePoints)
        return;
    x.curve.push_back ({ juce::jlimit (0.0, 1.0, t), juce::jlimit (0.0, 1.0, gain) });
    x.curve = normalisedCurve (x.curve);
    apply (x, true);
    for (size_t i = 0; i < x.curve.size(); ++i)
        if (x.curve[i].t == juce::jlimit (0.0, 1.0, t) && x.curve[i].gain == juce::jlimit (0.0, 1.0, gain))
            selected = (int) i;
}

void CurveEditor::removePoint (int index)
{
    const auto* sh = current();
    if (sh == nullptr || ! sh->custom || index < 0 || index >= (int) sh->curve.size() || sh->curve.size() <= 2)
        return;
    auto x = *sh;
    x.curve.erase (x.curve.begin() + index);
    selected = hovered = -1;
    apply (x, true);
}

void CurveEditor::movePoint (int index, double t, double gain, bool commit)
{
    const auto* sh = current();
    if (sh == nullptr || ! sh->custom || index < 0 || index >= (int) sh->curve.size())
        return;
    auto x = *sh;
    // A point never crosses its neighbours: the order of breakpoints is stable while dragging.
    const double lo = index > 0 ? x.curve[(size_t) index - 1].t : 0.0;
    const double hi = index + 1 < (int) x.curve.size() ? x.curve[(size_t) index + 1].t : 1.0;
    x.curve[(size_t) index].t = juce::jlimit (lo, hi, t);
    x.curve[(size_t) index].gain = juce::jlimit (0.0, 1.0, gain);
    apply (x, commit);
}

void CurveEditor::copyCurve()
{
    const auto* sh = current();
    if (sh == nullptr)
        return;
    const auto [a, b] = spanFractions();
    curveClipboard = sh->custom && sh->curve.size() >= 2 ? sh->curve : bakeCurve (*sh, (b - a) * voiceSeconds(), 0.125);
    refreshButtons();
}

void CurveEditor::pasteCurve()
{
    const auto* sh = current();
    if (sh == nullptr || ! curveClipboard || curveClipboard->size() < 2)
        return;
    auto x = *sh;
    x.curve = *curveClipboard;
    x.custom = true;
    x.enabled = true;
    apply (x, true);
}

void CurveEditor::duplicateCurve()
{
    const auto* sh = current();
    if (sh == nullptr)
        return;
    auto x = *sh;
    const auto [a, b] = spanFractions();
    auto base = x.custom && x.curve.size() >= 2 ? x.curve : bakeCurve (x, (b - a) * voiceSeconds(), 0.125);
    if ((int) base.size() * 2 > limits::maxShaperCurvePoints)
        base = bakeCurve (x, (b - a) * voiceSeconds(), 0.125, limits::maxShaperCurvePoints / 2);
    std::vector<CurvePoint> out;
    for (int rep = 0; rep < 2; ++rep)
        for (auto pt : base)
            out.push_back ({ 0.5 * rep + 0.5 * pt.t, pt.gain });
    x.curve = normalisedCurve (out);
    x.custom = true;
    x.enabled = true;
    apply (x, true);
}

void CurveEditor::mirrorCurve()
{
    const auto* sh = current();
    if (sh == nullptr)
        return;
    auto x = *sh;
    const auto [a, b] = spanFractions();
    auto base = x.custom && x.curve.size() >= 2 ? x.curve : bakeCurve (x, (b - a) * voiceSeconds(), 0.125);
    for (auto& pt : base)
        pt.t = 1.0 - pt.t;
    x.curve = normalisedCurve (base);
    x.custom = true;
    x.enabled = true;
    apply (x, true);
}

void CurveEditor::undo()
{
    if (! canUndo())
        return;
    --historyPos;
    apply (history[historyPos], false);
}

void CurveEditor::redo()
{
    if (! canRedo())
        return;
    ++historyPos;
    apply (history[historyPos], false);
}

void CurveEditor::storeB()
{
    if (const auto* sh = current())
    {
        stateB = *sh;
        showingB = false;
        refreshButtons();
    }
}

void CurveEditor::toggleAB()
{
    const auto* sh = current();
    if (sh == nullptr || ! stateB)
        return;
    const auto other = *stateB;
    stateB = *sh;
    showingB = ! showingB;
    apply (other, true);
}

void CurveEditor::setEmbedded (bool on)
{
    embedded = on;
    closeButton.setVisible (! on);
    expandButton.setVisible (on);
    resized();
}

void CurveEditor::generatePerfectCuts()
{
    const auto* sh = current();
    const double len = voiceSeconds();
    if (sh == nullptr || len <= 0.0)
        return;
    auto x = *sh;
    if (selection && selection->second - selection->first > 0.001)
    {
        x.wholeVoice = false;
        x.regionStart = selection->first;
        x.regionEnd = selection->second;
        x.lengthMs = 0.0;
    }
    x.custom = false;
    x.enabled = true;
    double a = 0.0, b = 0.0;
    if (shaperSpan (x, len, a, b) && x.rateMode == ShaperSettings::RateMode::Count)
        x.cuts = juce::jlimit (limits::minShaperCuts, juce::jmax (limits::minShaperCuts, maxUsefulCuts (x, b - a)), x.cuts);
    apply (clamped (x), true);
}

void CurveEditor::selectionToRegion()
{
    const auto* sh = current();
    if (sh == nullptr || ! selection)
        return;
    auto x = *sh;
    x.wholeVoice = false;
    x.regionStart = selection->first;
    x.regionEnd = selection->second;
    x.lengthMs = 0.0;
    x.enabled = true;
    apply (x, true);
}

void CurveEditor::zoomBy (double factor, double around)
{
    // Down to a 5 ms window (or 1/1000 of the repetition) so single milliseconds can be edited.
    const double len = voiceSeconds();
    const double minSpan = juce::jlimit (1.0e-4, 0.05, len > 0.0 ? 0.005 / len : 0.002);
    const double span = juce::jlimit (minSpan, 1.0, (viewEnd - viewStart) * factor);
    const double centre = viewStart + (viewEnd - viewStart) * around;
    viewStart = juce::jlimit (0.0, 1.0 - span, centre - span * around);
    viewEnd = viewStart + span;
    viewChanged();
}

void CurveEditor::fitView()
{
    viewStart = 0.0;
    viewEnd = 1.0;
    viewChanged();
}

void CurveEditor::setSelection (std::optional<std::pair<double, double>> s)
{
    if (s && s->second - s->first < 1.0e-4)
        s.reset();
    selection = s;
    refreshButtons();
    repaint();
}

// ---- tools: points / cuts ----------------------------------------------------

bool CurveEditor::isCutsMode() const
{
    const auto* sh = current();
    return sh != nullptr && sh->custom && sh->manualKind == ShaperSettings::ManualKind::Cuts;
}

bool CurveEditor::isPointsMode() const
{
    const auto* sh = current();
    return sh != nullptr && sh->custom && sh->manualKind == ShaperSettings::ManualKind::Points;
}

double CurveEditor::spanMs() const
{
    const auto [a, b] = spanFractions();
    return (b - a) * voiceSeconds() * 1000.0;
}

void CurveEditor::setCutsMode (bool on)
{
    const auto* sh = current();
    const double len = voiceSeconds();
    if (sh == nullptr || len <= 0.0)
        return;
    auto x = *sh;
    if (! on)
    {
        if (x.manualKind == ShaperSettings::ManualKind::Cuts)
            x.custom = false;  // back to the generator; the blocks stay stored
        apply (x, true);
        return;
    }
    if (x.blocks.empty())
    {
        // Start from what sounds now (generator or points): nothing is lost, the points stay stored.
        double closed = 0.0;
        int found = 0;
        const auto [a, b] = spanFractions();
        auto probe = x;
        probe.custom = x.custom && x.manualKind == ShaperSettings::ManualKind::Points;
        probe.manualKind = ShaperSettings::ManualKind::Points;
        x.blocks = detectBlocks (probe, (b - a) * len, 0.125, closed, found);
        x.depthPercent = juce::jlimit (0.0, 100.0, (1.0 - closed) * 100.0);
        note = found > (int) x.blocks.size()
                   ? juce::String (found) + u8 (" aperturas detectadas: se usan las primeras ") + juce::String ((int) x.blocks.size())
                   : juce::String ((int) x.blocks.size()) + u8 (" cortes creados a partir de la forma actual");
    }
    x.custom = true;
    x.enabled = true;
    x.manualKind = ShaperSettings::ManualKind::Cuts;
    apply (x, true);
}

bool CurveEditor::cutsToPoints()
{
    const auto* sh = current();
    if (sh == nullptr || sh->blocks.empty())
        return false;
    // Each opening needs up to 4 breakpoints (closed, ramp up, ramp down, closed).
    if ((int) sh->blocks.size() * 4 + 2 > limits::maxShaperCurvePoints)
    {
        note = u8 ("No caben como puntos: m\xc3\xa1ximo ") + juce::String ((limits::maxShaperCurvePoints - 2) / 4)
               + u8 (" cortes. Los cortes no se han tocado.");
        refreshButtons();
        return false;
    }
    auto x = *sh;
    const auto [a, b] = spanFractions();
    auto active = x;
    active.custom = true;
    active.manualKind = ShaperSettings::ManualKind::Cuts;
    x.curve = bakeActiveCurve (active, (b - a) * voiceSeconds(), 0.125);
    x.custom = true;
    x.manualKind = ShaperSettings::ManualKind::Points;
    note = juce::String ((int) x.curve.size()) + u8 (" puntos creados desde los cortes (los cortes siguen guardados)");
    apply (x, true);
    return true;
}

void CurveEditor::redetectCuts()
{
    const auto* sh = current();
    if (sh == nullptr)
        return;
    auto x = *sh;
    x.blocks.clear();
    if (x.manualKind == ShaperSettings::ManualKind::Cuts)
        x.custom = false;  // detect from the generator, unless points are active
    apply (x, false);
    setCutsMode (true);
}

int CurveEditor::numBlocks() const
{
    const auto* sh = current();
    return sh != nullptr ? (int) sh->blocks.size() : 0;
}

void CurveEditor::selectBlock (int index)
{
    selBlock = index >= 0 && index < numBlocks() ? index : -1;
    refreshButtons();
    repaint();
}

int CurveEditor::addBlock (double startMs, double endMs, double level)
{
    const auto* sh = current();
    if (sh == nullptr || numBlocks() >= limits::maxCutBlocks)
        return -1;
    const double total = spanMs();
    startMs = juce::jlimit (0.0, total, juce::jmin (startMs, endMs));
    endMs = juce::jlimit (0.0, total, juce::jmax (startMs, endMs));
    auto x = *sh;
    // Fit into the gap that contains startMs.
    double lo = 0.0, hi = total;
    for (const auto& bl : x.blocks)
    {
        if (startMs >= bl.startMs && startMs < bl.endMs)
            return -1;  // inside an existing opening
        if (bl.endMs <= startMs) lo = juce::jmax (lo, bl.endMs);
        if (bl.startMs >= startMs) hi = juce::jmin (hi, bl.startMs);
    }
    endMs = juce::jmin (endMs, hi);
    if (endMs - startMs < limits::minCutBlockMs)
    {
        endMs = juce::jmin (hi, startMs + limits::minCutBlockMs);
        startMs = juce::jmax (lo, endMs - limits::minCutBlockMs);
        if (endMs - startMs < limits::minCutBlockMs - 1.0e-9)
            return -1;
    }
    x.blocks.push_back ({ startMs, endMs, juce::jlimit (0.0, 1.0, level) });
    x.blocks = normalisedBlocks (x.blocks);
    x.custom = true;
    x.enabled = true;
    x.manualKind = ShaperSettings::ManualKind::Cuts;
    apply (x, true);
    for (int i = 0; i < numBlocks(); ++i)
        if (std::abs (current()->blocks[(size_t) i].startMs - startMs) < 1.0e-6)
            selBlock = i;
    refreshButtons();
    return selBlock;
}

void CurveEditor::moveBlock (int index, double startMs, bool commit)
{
    const auto* sh = current();
    if (sh == nullptr || index < 0 || index >= (int) sh->blocks.size())
        return;
    auto x = *sh;
    auto& bl = x.blocks[(size_t) index];
    const double len = bl.endMs - bl.startMs;
    const double lo = index > 0 ? x.blocks[(size_t) index - 1].endMs : 0.0;
    const double hi = (index + 1 < (int) x.blocks.size() ? x.blocks[(size_t) index + 1].startMs : spanMs()) - len;
    bl.startMs = juce::jlimit (lo, juce::jmax (lo, hi), startMs);
    bl.endMs = bl.startMs + len;
    apply (x, commit);
}

void CurveEditor::resizeBlock (int index, double startMs, double endMs, bool commit)
{
    const auto* sh = current();
    if (sh == nullptr || index < 0 || index >= (int) sh->blocks.size())
        return;
    auto x = *sh;
    auto& bl = x.blocks[(size_t) index];
    const double lo = index > 0 ? x.blocks[(size_t) index - 1].endMs : 0.0;
    const double hi = index + 1 < (int) x.blocks.size() ? x.blocks[(size_t) index + 1].startMs : spanMs();
    const double newStart = juce::jlimit (lo, juce::jmax (lo, hi - limits::minCutBlockMs), startMs);
    const double newEnd = juce::jlimit (juce::jmin (hi, newStart + limits::minCutBlockMs), hi, endMs);
    if (newEnd - newStart < limits::minCutBlockMs - 1.0e-9)
        return;  // no room, or shorter than the minimum: keep it as it was
    bl.startMs = newStart;
    bl.endMs = newEnd;
    apply (x, commit);
}

void CurveEditor::setBlockLevel (int index, double level, bool commit)
{
    const auto* sh = current();
    if (sh == nullptr || index < 0 || index >= (int) sh->blocks.size())
        return;
    auto x = *sh;
    x.blocks[(size_t) index].level = juce::jlimit (0.0, 1.0, level);
    apply (x, commit);
}

int CurveEditor::duplicateBlock (int index)
{
    const auto* sh = current();
    if (sh == nullptr || index < 0 || index >= (int) sh->blocks.size())
        return -1;
    const auto bl = sh->blocks[(size_t) index];
    const double len = bl.endMs - bl.startMs;
    const double next = index + 1 < (int) sh->blocks.size() ? sh->blocks[(size_t) index + 1].startMs : spanMs();
    // Right after it, keeping the same gap it has to the previous one when there is room.
    const double gap = index > 0 ? juce::jmax (0.0, bl.startMs - sh->blocks[(size_t) index - 1].endMs) : 0.0;
    double start = bl.endMs + gap;
    if (start + len > next)
        start = bl.endMs;
    if (start + limits::minCutBlockMs > next)
    {
        note = u8 ("No hay sitio a la derecha para duplicar este corte");
        refreshButtons();
        return -1;
    }
    return addBlock (start, juce::jmin (next, start + len), bl.level);
}

void CurveEditor::removeBlock (int index)
{
    const auto* sh = current();
    if (sh == nullptr || index < 0 || index >= (int) sh->blocks.size())
        return;
    auto x = *sh;
    x.blocks.erase (x.blocks.begin() + index);
    selBlock = -1;
    apply (x, true);
}

// ---- grid -----------------------------------------------------------------------

void CurveEditor::setGrid (bool on, double beats)
{
    gridOn = on;
    gridBeats = juce::jlimit (1.0 / 64.0, 4.0, beats);
    gridButton.setToggleState (on, juce::dontSendNotification);
    repaint();
}

double CurveEditor::snapMs (double ms) const
{
    if (! gridOn)
        return ms;
    // Grid relative to the start of the repetition (its trigger moment), at the project tempo.
    const double step = gridBeats * 60000.0 / ctx.processor.getProjectBpm();
    const double offset = spanFractions().first * voiceSeconds() * 1000.0;
    return std::round ((ms + offset) / step) * step - offset;
}

float CurveEditor::xForMs (double ms) const
{
    const double len = voiceSeconds();
    return xForFraction (spanFractions().first + (len > 0.0 ? ms / 1000.0 / len : 0.0));
}

double CurveEditor::msAt (float x) const
{
    return (fractionAt (x) - spanFractions().first) * voiceSeconds() * 1000.0;
}

std::pair<int, CurveEditor::BlockHit> CurveEditor::blockAt (juce::Point<float> pos) const
{
    const auto* sh = current();
    const auto c = canvasArea();
    if (sh == nullptr || ! isCutsMode() || ! c.expanded (4.0f, 0.0f).contains (pos))
        return { -1, BlockHit::None };
    for (int i = 0; i < (int) sh->blocks.size(); ++i)
    {
        const auto& bl = sh->blocks[(size_t) i];
        const float x0 = xForMs (bl.startMs), x1 = xForMs (bl.endMs);
        const float edge = juce::jmin (6.0f, (x1 - x0) / 3.0f);
        if (pos.x < x0 - 3.0f || pos.x > x1 + 3.0f)
            continue;
        if (std::abs (pos.x - x0) <= juce::jmax (3.0f, edge)) return { i, BlockHit::Left };
        if (std::abs (pos.x - x1) <= juce::jmax (3.0f, edge)) return { i, BlockHit::Right };
        if (std::abs (pos.y - yForGain (bl.level)) <= 6.0f) return { i, BlockHit::Top };
        return { i, BlockHit::Body };
    }
    return { -1, BlockHit::None };
}

juce::String CurveEditor::blockLabel (int i) const
{
    const auto* sh = current();
    if (sh == nullptr || i < 0 || i >= (int) sh->blocks.size())
        return {};
    const auto& bl = sh->blocks[(size_t) i];
    return u8 ("corte ") + juce::String (i + 1) + ": " + juce::String (bl.startMs, 1) + u8 ("\xe2\x80\x93") + juce::String (bl.endMs, 1)
           + " ms (" + juce::String (bl.endMs - bl.startMs, 1) + " ms), " + juce::String (bl.level * 100.0, 0) + " %";
}

void CurveEditor::nudge (double ms)
{
    if (isCutsMode() && selBlock >= 0)
        moveBlock (selBlock, current()->blocks[(size_t) selBlock].startMs + ms, true);
    else if (isPointsMode() && selected >= 0 && voiceSeconds() > 0.0)
    {
        const auto& pt = current()->curve[(size_t) selected];
        const double span = spanMs();
        movePoint (selected, pt.t + (span > 0.0 ? ms / span : 0.0), pt.gain, true);
    }
}

void CurveEditor::showBlockMenu (int index)
{
    juce::PopupMenu m;
    m.addSectionHeader (blockLabel (index));
    m.addItem ("Duplicar (Ctrl+D)", [this, index] { selectBlock (duplicateBlock (index)); });
    m.addItem ("Eliminar (Supr)", [this, index] { removeBlock (index); });
    juce::PopupMenu lv;
    for (int pct : { 100, 75, 50, 25 })
        lv.addItem (juce::String (pct) + " %", [this, index, pct] { setBlockLevel (index, pct / 100.0, true); });
    m.addSubMenu ("Intensidad", lv);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

void CurveEditor::showCanvasMenu()
{
    juce::PopupMenu m;
    const auto* sh = current();
    if (sh == nullptr)
        return;
    m.addItem (u8 ("Convertir cortes \xe2\x86\x92 puntos (reemplaza los puntos)"), ! sh->blocks.empty(), false, [this] { cutsToPoints(); });
    m.addItem (u8 ("Crear cortes desde la curva actual (reemplaza los cortes)"), true, false, [this] { redetectCuts(); });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

void CurveEditor::viewChanged()
{
    hScroll.setCurrentRange (viewStart, viewEnd - viewStart, juce::dontSendNotification);
    rebuildPeaks();
    repaint();
}

void CurveEditor::scrollBarMoved (juce::ScrollBar*, double start)
{
    const double span = viewEnd - viewStart;
    viewStart = juce::jlimit (0.0, 1.0 - span, start);
    viewEnd = viewStart + span;
    rebuildPeaks();
    repaint();
}

// ---- geometry ----------------------------------------------------------------

juce::Rectangle<float> CurveEditor::canvasArea() const
{
    auto r = getLocalBounds().toFloat().reduced (12.0f, 8.0f);
    r.removeFromTop ((float) toolbarPx);
    r.removeFromBottom (18.0f + 14.0f);  // help line + horizontal scroll bar
    r.removeFromTop (kRulerHeight);
    return r;
}

juce::Rectangle<float> CurveEditor::rulerArea() const
{
    auto c = canvasArea();
    return { c.getX(), c.getY() - kRulerHeight, c.getWidth(), kRulerHeight };
}

float CurveEditor::xForFraction (double f) const
{
    const auto c = canvasArea();
    return c.getX() + (float) ((f - viewStart) / juce::jmax (1.0e-9, viewEnd - viewStart) * c.getWidth());
}

double CurveEditor::fractionAt (float x) const
{
    const auto c = canvasArea();
    return viewStart + (viewEnd - viewStart) * juce::jlimit (0.0, 1.0, (double) (x - c.getX()) / juce::jmax (1.0f, c.getWidth()));
}

float CurveEditor::yForGain (double g) const
{
    const auto c = canvasArea();
    return c.getBottom() - 4.0f - (float) g * (c.getHeight() - 8.0f);
}

double CurveEditor::gainAt (float y) const
{
    const auto c = canvasArea();
    return juce::jlimit (0.0, 1.0, (double) ((c.getBottom() - 4.0f - y) / juce::jmax (1.0f, c.getHeight() - 8.0f)));
}

int CurveEditor::pointNear (juce::Point<float> pos) const
{
    const auto* sh = current();
    if (sh == nullptr || ! isPointsMode())
        return -1;
    const auto [a, b] = spanFractions();
    int best = -1;
    float bestD = kHitRadius;
    for (size_t i = 0; i < sh->curve.size(); ++i)
    {
        const juce::Point<float> p (xForFraction (a + sh->curve[i].t * (b - a)), yForGain (sh->curve[i].gain));
        const float d = p.getDistanceFrom (pos);
        if (d < bestD)
        {
            bestD = d;
            best = (int) i;
        }
    }
    return best;
}

void CurveEditor::rebuildPeaks()
{
    peaks.clear();
    const auto* p = ctx.primaryPoint();
    auto audio = ctx.processor.getSourceAudio();
    const auto c = canvasArea();
    const int w = (int) c.getWidth();
    if (p == nullptr || audio == nullptr || w <= 0)
        return;
    const auto& src = ctx.scene().source;
    const auto total = (juce::int64) audio->buffer.getNumSamples();
    const auto end = src.trimEnd > src.trimStart ? juce::jmin (src.trimEnd, total) : total;
    const auto fragLen = juce::jmax<juce::int64> (0, end - src.trimStart);
    const auto region = clamped (p->region);
    const auto voiceStart = src.trimStart + (juce::int64) std::llround (region.start * (double) fragLen);
    const auto voiceLen = (juce::int64) std::llround ((region.end - region.start) * (double) fragLen);
    if (voiceLen <= 0)
        return;
    peaks.resize ((size_t) w, { 0.0f, 0.0f });
    for (int px = 0; px < w; ++px)
    {
        const double f0 = viewStart + (viewEnd - viewStart) * px / w;
        const double f1 = viewStart + (viewEnd - viewStart) * (px + 1) / w;
        const auto s0 = juce::jlimit<juce::int64> (0, total, voiceStart + (juce::int64) (f0 * (double) voiceLen));
        const auto s1 = juce::jlimit<juce::int64> (s0 + 1, total, voiceStart + (juce::int64) (f1 * (double) voiceLen) + 1);
        float lo = 0.0f, hi = 0.0f;
        for (int ch = 0; ch < audio->buffer.getNumChannels(); ++ch)
        {
            const auto r = audio->buffer.findMinMax (ch, (int) s0, (int) (s1 - s0));
            lo = juce::jmin (lo, r.getStart());
            hi = juce::jmax (hi, r.getEnd());
        }
        peaks[(size_t) px] = { lo, hi };
    }
}

// ---- painting ------------------------------------------------------------------

void CurveEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
    theme::drawPanel (g, getLocalBounds().toFloat().reduced (4.0f));
    const auto c = canvasArea();
    const auto ruler = rulerArea();
    g.setColour (theme::panel.darker (0.2f));
    g.fillRoundedRectangle (c.expanded (0.0f, kRulerHeight).withY (ruler.getY()), 6.0f);

    const auto* sh = current();
    const double len = voiceSeconds();
    if (sh == nullptr || len <= 0.0)
    {
        g.setColour (theme::textDim);
        g.setFont (theme::font (14.0f));
        g.drawText (u8 ("Selecciona una repetici\xc3\xb3n con audio cargado para editar su curva"), c, juce::Justification::centred);
        return;
    }
    if ((int) peaks.size() != (int) c.getWidth())
        const_cast<CurveEditor*> (this)->rebuildPeaks();

    // Millisecond ruler.
    {
        const double viewMs = (viewEnd - viewStart) * len * 1000.0;
        double stepMs = 1000.0;
        for (double cand : { 1.0, 2.0, 5.0, 10.0, 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0 })
            if (viewMs / cand <= juce::jmax (4.0, c.getWidth() / 70.0)) { stepMs = cand; break; }
        const double first = std::ceil (viewStart * len * 1000.0 / stepMs) * stepMs;
        g.setFont (theme::font (10.0f));
        for (double ms = first; ms <= viewEnd * len * 1000.0 + 1.0e-6; ms += stepMs)
        {
            const float x = xForFraction (ms / 1000.0 / len);
            g.setColour (theme::grid);
            g.drawVerticalLine ((int) x, c.getY(), c.getBottom());
            g.setColour (theme::outline);
            g.drawVerticalLine ((int) x, ruler.getBottom() - 6.0f, ruler.getBottom());
            g.setColour (theme::textFaint);
            g.drawText (juce::String (ms, stepMs < 1.0 ? 1 : 0) + " ms", juce::Rectangle<float> (x + 3.0f, ruler.getY() + 2.0f, 64.0f, 12.0f),
                        juce::Justification::left);
        }
        // Gain grid: 0, 25, 50, 75, 100 %.
        for (int k = 0; k <= 4; ++k)
        {
            const float y = yForGain (k / 4.0);
            g.setColour (theme::grid);
            g.drawHorizontalLine ((int) y, c.getX(), c.getRight());
            g.setColour (theme::textFaint);
            g.drawText (juce::String (k * 25) + " %", juce::Rectangle<float> (c.getRight() - 40.0f, y - 12.0f, 36.0f, 11.0f),
                        juce::Justification::right);
        }
    }

    // Shaped span (outside it the curve is 1) and the selection.
    const auto [sa, sb] = spanFractions();
    if (sa > 0.0 || sb < 1.0)
    {
        g.setColour (theme::violet.withAlpha (0.10f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (xForFraction (sa), c.getY(), xForFraction (sb), c.getBottom()));
    }
    if (selection)
    {
        g.setColour (theme::blue.withAlpha (0.16f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (xForFraction (selection->first), ruler.getY(),
                                                                xForFraction (selection->second), c.getBottom()));
        g.setColour (theme::blue.withAlpha (0.8f));
        g.drawVerticalLine ((int) xForFraction (selection->first), ruler.getY(), c.getBottom());
        g.drawVerticalLine ((int) xForFraction (selection->second), ruler.getY(), c.getBottom());
    }

    // The curve, sampled per pixel from shaperGain (exactly what the engine applies),
    // the audio under it coloured by that gain: bright where it sounds, dark where muted.
    const auto& snapH = ctx.processor.getHostSnapshot();
    const double bpm = snapH.hasBpm.load() ? snapH.bpm.load() : 120.0;
    const double divisionSeconds = beatsPerDivision (sh->division) * 60.0 / bpm;
    // Beat grid (1/16 of the host tempo) when the host reports one: faint violet ticks under the ms ruler.
    if (snapH.hasBpm.load())
    {
        const double step = 60.0 / bpm / 4.0;
        const double first = std::ceil (viewStart * len / step) * step;
        g.setColour (theme::violet.withAlpha (0.25f));
        for (double t = first; t <= viewEnd * len; t += step)
            g.drawVerticalLine ((int) xForFraction (t / len), ruler.getBottom() - 3.0f, c.getY() + 4.0f);
    }
    const float mid = c.getCentreY();
    const float half = c.getHeight() * 0.42f;
    juce::Path curve;
    const int w = (int) c.getWidth();
    const float stripY = c.getBottom() - 4.0f;  // audible (turquoise) / silent (dark) strip
    for (int px = 0; px <= w; ++px)
    {
        const double f = viewStart + (viewEnd - viewStart) * px / juce::jmax (1, w);
        const float gN = sh->enabled ? shaperGain (*sh, juce::jmin (f * len, len * 0.999999), len, divisionSeconds) : 1.0f;
        const float x = c.getX() + (float) px;
        if (px < w)
        {
            g.setColour (gN >= 0.5f ? theme::turquoise.withAlpha (0.8f) : theme::background.brighter (0.15f));
            g.drawVerticalLine ((int) x, stripY, c.getBottom());
        }
        if (px < w && px < (int) peaks.size())
        {
            const auto [lo, hi] = peaks[(size_t) px];
            g.setColour (theme::turquoise.withAlpha (0.25f + 0.65f * gN));
            g.drawVerticalLine ((int) x, mid - juce::jlimit (0.0f, half, hi * half * (float) vZoom),
                                mid - juce::jlimit (-half, 0.0f, lo * half * (float) vZoom) + 1.0f);
        }
        const float y = yForGain (gN);
        px == 0 ? curve.startNewSubPath (x, y) : curve.lineTo (x, y);
    }
    {
        juce::Path fill (curve);
        fill.lineTo (c.getRight(), yForGain (0.0));
        fill.lineTo (c.getX(), yForGain (0.0));
        fill.closeSubPath();
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.fillPath (fill);
    }
    g.setColour (sh->enabled ? juce::Colours::white.withAlpha (0.9f) : theme::textFaint);
    g.strokePath (curve, juce::PathStrokeType (1.6f));

    // Breakpoints.
    if (sh->custom)
    {
        for (size_t i = 0; i < sh->curve.size(); ++i)
        {
            const float x = xForFraction (sa + sh->curve[i].t * (sb - sa));
            const float y = yForGain (sh->curve[i].gain);
            if (x < c.getX() - 6.0f || x > c.getRight() + 6.0f)
                continue;
            const bool hot = (int) i == hovered || (int) i == dragging;
            const bool sel = (int) i == selected;
            g.setColour (hot ? theme::amber : (sel ? theme::blue : juce::Colours::white));
            g.fillEllipse (x - (hot ? 5.5f : 4.0f), y - (hot ? 5.5f : 4.0f), hot ? 11.0f : 8.0f, hot ? 11.0f : 8.0f);
            g.setColour (theme::background);
            g.drawEllipse (x - 4.0f, y - 4.0f, 8.0f, 8.0f, 1.0f);
        }
        if (hovered >= 0 || dragging >= 0)
        {
            const int i = dragging >= 0 ? dragging : hovered;
            g.setColour (theme::text);
            g.setFont (theme::font (11.0f));
            g.drawText (pointLabel (i), juce::Rectangle<float> (lastMouse.x + 10.0f, lastMouse.y - 18.0f, 120.0f, 14.0f),
                        juce::Justification::left);
        }
    }

    // Cut blocks: borders, level handle, selection; time readout while dragging.
    if (isCutsMode())
    {
        for (int i = 0; i < (int) sh->blocks.size(); ++i)
        {
            const auto& bl = sh->blocks[(size_t) i];
            const float x0 = xForMs (bl.startMs), x1 = xForMs (bl.endMs);
            if (x1 < c.getX() || x0 > c.getRight())
                continue;
            const bool sel = i == selBlock, hot = i == hoverBlock || i == dragBlock;
            const auto box = juce::Rectangle<float>::leftTopRightBottom (juce::jmax (c.getX(), x0), yForGain (bl.level),
                                                                         juce::jmin (c.getRight(), x1), c.getBottom());
            g.setColour ((sel ? theme::amber : theme::violet).withAlpha (sel ? 0.22f : (hot ? 0.18f : 0.10f)));
            g.fillRect (box);
            g.setColour (sel ? theme::amber : theme::violet.brighter (0.3f));
            const bool hl = hot && (hoverHit == BlockHit::Left || (dragBlock == i && dragHit == BlockHit::Left));
            const bool hr = hot && (hoverHit == BlockHit::Right || (dragBlock == i && dragHit == BlockHit::Right));
            g.fillRect (x0 - (hl ? 1.5f : 0.75f), box.getY(), hl ? 3.0f : 1.5f, box.getHeight());
            g.fillRect (x1 - (hr ? 1.5f : 0.75f), box.getY(), hr ? 3.0f : 1.5f, box.getHeight());
            if (x1 - x0 > 10.0f)
                g.fillRoundedRectangle (juce::Rectangle<float> (juce::jmin (16.0f, (x1 - x0) * 0.5f), 4.0f).withCentre ({ (x0 + x1) * 0.5f, box.getY() }), 2.0f);
        }
        if (dragBlock >= 0 || hoverBlock >= 0)
        {
            const int i = dragBlock >= 0 ? dragBlock : hoverBlock;
            g.setColour (theme::text);
            g.setFont (theme::font (11.0f, true));
            const auto t = blockLabel (i);
            const float tw = (float) juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), t) + 12.0f;
            auto bx = juce::Rectangle<float> (lastMouse.x + 12.0f, lastMouse.y - 22.0f, tw, 16.0f);
            if (bx.getRight() > c.getRight()) bx.setX (lastMouse.x - tw - 12.0f);
            g.setColour (theme::background.withAlpha (0.85f));
            g.fillRoundedRectangle (bx, 3.0f);
            g.setColour (theme::text);
            g.drawText (t, bx, juce::Justification::centred);
        }
    }
    if (dragging >= 0 || dragBlock >= 0)
    {
        // Time indicator: a line at the mouse with its exact ms in the ruler.
        const double ms = dragBlock >= 0 && current() != nullptr && dragBlock < (int) current()->blocks.size()
                              ? (dragHit == BlockHit::Right ? current()->blocks[(size_t) dragBlock].endMs : current()->blocks[(size_t) dragBlock].startMs)
                              : msAt (lastMouse.x);
        const float x = xForMs (ms);
        g.setColour (theme::amber.withAlpha (0.8f));
        g.drawVerticalLine ((int) x, ruler.getY(), c.getBottom());
        g.setFont (theme::font (10.5f, true));
        g.drawText (juce::String (ms, 1) + " ms", juce::Rectangle<float> (x + 3.0f, ruler.getY() + 2.0f, 70.0f, 12.0f), juce::Justification::left);
    }

    g.setColour (theme::outline);
    g.drawRoundedRectangle (c.expanded (0.0f, kRulerHeight).withY (ruler.getY()), 6.0f, 1.0f);
}

void CurveEditor::resized()
{
    auto r = getLocalBounds().reduced (12, 8);
    const int top = r.getY();
    auto row1 = r.removeFromTop ((int) kToolbarHeight);
    if (embedded)
        expandButton.setBounds (row1.removeFromRight (80));
    else
        closeButton.setBounds (row1.removeFromRight (80));
    row1.removeFromRight (8);
    previewButton.setBounds (row1.removeFromRight (104));
    title.setBounds (row1.removeFromLeft (juce::jmin (280, row1.getWidth() / 2)));
    info.setBounds (row1);
    r.removeFromTop (6);

    // Toolbar buttons flow left to right and wrap when the width runs out.
    const int bh = (int) kToolbarHeight - 4;
    const int right = r.getRight();
    int x = r.getX(), y = r.getY();
    auto place = [&] (juce::Component& b, int w, int gapAfter = 4)
    {
        if (x + w > right && x > r.getX())
        {
            x = r.getX();
            y += bh + 4;
        }
        b.setBounds (x, y, w, bh);
        x += w + gapAfter;
    };
    place (generateButton, embedded ? 200 : 210, 10);
    place (manualButton, 104);
    place (cutsButton, 104, 10);
    place (bakeButton, 110, 10);
    place (undoButton, 76);
    place (redoButton, 72, 10);
    place (copyButton, 62);
    place (pasteButton, 58);
    place (dupButton, 72);
    place (mirrorButton, 66, 10);
    place (storeBButton, 84);
    place (abButton, 62, 10);
    place (regionButton, 120, 10);
    for (auto* b : { &zoomInButton, &zoomOutButton, &zoomFitButton, &vZoomInButton, &vZoomOutButton })
        place (*b, 36);
    x += 6;
    place (gridButton, 70);
    place (gridBox, 78);
    toolbarPx = (y + bh + 8) - top;
    help.setBounds (getLocalBounds().reduced (12, 8).removeFromBottom (16));
    {
        const auto c = canvasArea();
        hScroll.setBounds ((int) c.getX(), (int) c.getBottom() + 2, (int) c.getWidth(), 10);
        hScroll.setCurrentRange (viewStart, viewEnd - viewStart, juce::dontSendNotification);
    }
    rebuildPeaks();
}

// ---- mouse / keys ----------------------------------------------------------------

void CurveEditor::mouseDown (const juce::MouseEvent& e)
{
    ctx.beginGesture();  // one undo step per drag
    if (isShowing())
        grabKeyboardFocus();
    lastMouse = e.position;
    const auto* sh = current();
    if (sh == nullptr)
        return;
    if (rulerArea().contains (e.position)
        || (e.mods.isShiftDown() && canvasArea().contains (e.position) && pointNear (e.position) < 0 && blockAt (e.position).first < 0))
    {
        draggingSelection = true;
        dragAnchor = fractionAt (e.position.x);
        setSelection (std::nullopt);
        return;
    }
    if (isCutsMode())
    {
        const auto [bi, hit] = blockAt (e.position);
        if (e.mods.isPopupMenu())
        {
            if (bi >= 0) { selectBlock (bi); showBlockMenu (bi); }
            else showCanvasMenu();
            return;
        }
        note.clear();
        if (bi >= 0)
        {
            selectBlock (bi);
            dragBlock = bi;
            dragHit = hit;
            const auto& bl = current()->blocks[(size_t) bi];
            dragMs0 = msAt (e.position.x);
            dragStart0 = bl.startMs;
            dragEnd0 = bl.endMs;
            dragLevel0 = bl.level;
            dragY0 = e.position.y;
            return;
        }
        if (canvasArea().contains (e.position))
        {
            // Drag in an empty gap: a new opening from here to where the mouse goes.
            const double ms = juce::jlimit (0.0, spanMs(), snapMs (msAt (e.position.x)));
            const int idx = addBlock (ms, ms + limits::minCutBlockMs, 1.0);
            if (idx >= 0)
            {
                dragBlock = idx;
                dragHit = BlockHit::Right;
                creatingBlock = true;
                dragMs0 = ms;
                dragStart0 = current()->blocks[(size_t) idx].startMs;
                dragEnd0 = current()->blocks[(size_t) idx].endMs;
            }
            else
            {
                selectBlock (-1);
            }
        }
        return;
    }
    if (e.mods.isPopupMenu() && pointNear (e.position) < 0 && canvasArea().contains (e.position))
    {
        showCanvasMenu();
        return;
    }
    const int near = pointNear (e.position);
    if (near >= 0 && (e.mods.isPopupMenu() || e.mods.isAltDown()))
    {
        removePoint (near);
        return;
    }
    if (near >= 0)
    {
        dragging = selected = near;
        lastDragPos = e.position;
        repaint();
        return;
    }
    if (canvasArea().contains (e.position))
    {
        selected = -1;
        draggingSelection = true;
        dragAnchor = fractionAt (e.position.x);
        setSelection (std::nullopt);
    }
}

void CurveEditor::mouseDrag (const juce::MouseEvent& e)
{
    lastMouse = e.position;
    if (draggingSelection)
    {
        const double f = fractionAt (e.position.x);
        setSelection (std::pair { juce::jmin (dragAnchor, f), juce::jmax (dragAnchor, f) });
        return;
    }
    // Shift: fine adjustment (a tenth of the mouse movement, no grid).
    const bool fine = e.mods.isShiftDown();
    if (dragBlock >= 0 && current() != nullptr && dragBlock < numBlocks())
    {
        const double mouseMs = msAt (e.position.x);
        const double delta = (mouseMs - dragMs0) * (fine ? 0.1 : 1.0);
        auto snap = [&] (double ms) { return fine ? ms : snapMs (ms); };
        switch (dragHit)
        {
            case BlockHit::Body: moveBlock (dragBlock, snap (dragStart0 + delta), false); break;
            case BlockHit::Left: resizeBlock (dragBlock, snap (dragStart0 + delta), dragEnd0, false); break;
            case BlockHit::Right: resizeBlock (dragBlock, dragStart0, snap (dragEnd0 + delta), false); break;
            case BlockHit::Top:
            {
                const double dy = (double) (dragY0 - e.position.y) / juce::jmax (1.0f, canvasArea().getHeight() - 8.0f);
                setBlockLevel (dragBlock, dragLevel0 + dy * (fine ? 0.1 : 1.0), false);
                break;
            }
            case BlockHit::None: break;
        }
        return;
    }
    if (dragging >= 0)
    {
        const auto [a, b] = spanFractions();
        const auto* sh = current();
        const double span = spanMs();
        if (sh == nullptr || dragging >= (int) sh->curve.size() || span <= 0.0)
            return;
        double ms = msAt (e.position.x);
        double g = gainAt (e.position.y);
        if (fine)
        {
            const auto& pt = sh->curve[(size_t) dragging];
            const double ms0 = pt.t * span;
            ms = ms0 + (ms - msAt (lastDragPos.x)) * 0.1;
            g = pt.gain + (g - gainAt (lastDragPos.y)) * 0.1;
        }
        else
        {
            ms = snapMs (ms);
        }
        lastDragPos = e.position;
        movePoint (dragging, ms / span, g, false);
    }
}

void CurveEditor::mouseUp (const juce::MouseEvent&)
{
    ctx.endGesture();
    if (dragBlock >= 0)
    {
        record();
        dragBlock = -1;
        creatingBlock = false;
        refreshButtons();
        repaint();
    }
    if (dragging >= 0)
    {
        record();
        dragging = -1;
        refreshButtons();
        repaint();
    }
    draggingSelection = false;
}

void CurveEditor::mouseMove (const juce::MouseEvent& e)
{
    lastMouse = e.position;
    if (isCutsMode())
    {
        const auto [bi, hit] = blockAt (e.position);
        hoverBlock = bi;
        hoverHit = hit;
        setMouseCursor (hit == BlockHit::Left || hit == BlockHit::Right ? juce::MouseCursor::LeftRightResizeCursor
                        : hit == BlockHit::Top ? juce::MouseCursor::UpDownResizeCursor
                        : hit == BlockHit::Body ? juce::MouseCursor::DraggingHandCursor
                        : canvasArea().contains (e.position) ? juce::MouseCursor::CrosshairCursor : juce::MouseCursor::NormalCursor);
        repaint();
        return;
    }
    const int h = pointNear (e.position);
    if (h != hovered)
    {
        hovered = h;
        setMouseCursor (h >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
        refreshButtons();
    }
    repaint();
}

void CurveEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! canvasArea().contains (e.position) || current() == nullptr)
        return;
    if (isCutsMode())
    {
        if (blockAt (e.position).first >= 0)
            return;
        // A new opening one grid step long (free: 50 ms), centred on the click.
        const double len = gridOn ? gridBeats * 60000.0 / ctx.processor.getProjectBpm() : 50.0;
        const double at = gridOn ? snapMs (msAt (e.position.x) - len * 0.5) : msAt (e.position.x) - len * 0.5;
        selectBlock (addBlock (juce::jmax (0.0, at), juce::jmax (0.0, at) + len, 1.0));
        return;
    }
    if (pointNear (e.position) >= 0)
        return;
    const auto [a, b] = spanFractions();
    const double t = b > a ? (fractionAt (e.position.x) - a) / (b - a) : 0.0;
    addPoint (juce::jlimit (0.0, 1.0, t), gainAt (e.position.y));
}

void CurveEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCtrlDown() || e.mods.isCommandDown())
    {
        const auto c = canvasArea();
        const double around = juce::jlimit (0.0, 1.0, (double) (e.position.x - c.getX()) / juce::jmax (1.0f, c.getWidth()));
        zoomBy (wheel.deltaY > 0 ? 0.8 : 1.25, around);
    }
    else
    {
        const double span = viewEnd - viewStart;
        const double delta = (wheel.deltaX != 0.0f ? -wheel.deltaX : wheel.deltaY) * span * 0.5;
        viewStart = juce::jlimit (0.0, 1.0 - span, viewStart - delta);
        viewEnd = viewStart + span;
    }
    viewChanged();
}

bool CurveEditor::keyPressed (const juce::KeyPress& k)
{
    const bool ctrl = k.getModifiers().isCtrlDown() || k.getModifiers().isCommandDown();
    if (ctrl && k.getKeyCode() == 'Z' && k.getModifiers().isShiftDown()) { redo(); return true; }
    if (ctrl && k.getKeyCode() == 'Z') { undo(); return true; }
    if (ctrl && k.getKeyCode() == 'Y') { redo(); return true; }
    if (ctrl && k.getKeyCode() == 'C') { copyCurve(); return true; }
    if (ctrl && k.getKeyCode() == 'V') { pasteCurve(); return true; }
    if (k.getKeyCode() == juce::KeyPress::spaceKey)
    {
        if (const auto* p = ctx.primaryPoint(); p != nullptr && p->id != kLivePointId)
            ctx.processor.requestTriggerPoint (p->id);
        return true;
    }
    if (k.getKeyCode() == juce::KeyPress::leftKey || k.getKeyCode() == juce::KeyPress::rightKey)
    {
        const double step = k.getModifiers().isShiftDown() ? 0.1 : 1.0;
        nudge (k.getKeyCode() == juce::KeyPress::leftKey ? -step : step);
        return true;
    }
    if (ctrl && k.getKeyCode() == 'D' && isCutsMode() && selBlock >= 0)
    {
        selectBlock (duplicateBlock (selBlock));
        return true;
    }
    if ((k.getKeyCode() == juce::KeyPress::deleteKey || k.getKeyCode() == juce::KeyPress::backspaceKey) && isCutsMode() && selBlock >= 0)
    {
        removeBlock (selBlock);
        return true;
    }
    if (k.getKeyCode() == juce::KeyPress::deleteKey || k.getKeyCode() == juce::KeyPress::backspaceKey)
    {
        if (selected >= 0)
        {
            removePoint (selected);
            return true;
        }
    }
    if (k.getKeyCode() == juce::KeyPress::escapeKey && onClose)
    {
        onClose();
        return true;
    }
    return false;
}

} // namespace ses
