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
    manualButton.setTooltip (u8 ("Puntos manuales en lugar de la forma generada (se parte de la forma actual)"));
    manualButton.onClick = [this] { setManual (manualButton.getToggleState()); };
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
        fitView();
        if (p != nullptr)
            record();
    }
    title.setText (p != nullptr ? u8 ("Curva de volumen \xc2\xb7 repetici\xc3\xb3n ") + juce::String (p->number)
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
    const bool manual = has && sh->custom;
    manualButton.setToggleState (manual, juce::dontSendNotification);
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
    generateButton.setButtonText (selection ? u8 ("Generar cortes en la selecci\xc3\xb3n") : u8 ("Generar cortes perfectos"));

    juce::String text;
    if (has)
    {
        const double len = voiceSeconds();
        if (manual)
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
    info.setText (text, juce::dontSendNotification);
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
    if (on && x.curve.size() < 2)
        x.curve = bakeCurve (x, (spanFractions().second - spanFractions().first) * voiceSeconds(), 0.125);
    x.custom = on;
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
    const double span = juce::jlimit (0.002, 1.0, (viewEnd - viewStart) * factor);
    const double centre = viewStart + (viewEnd - viewStart) * around;
    viewStart = juce::jlimit (0.0, 1.0 - span, centre - span * around);
    viewEnd = viewStart + span;
    repaint();
}

void CurveEditor::fitView()
{
    viewStart = 0.0;
    viewEnd = 1.0;
    repaint();
}

void CurveEditor::setSelection (std::optional<std::pair<double, double>> s)
{
    if (s && s->second - s->first < 1.0e-4)
        s.reset();
    selection = s;
    refreshButtons();
    repaint();
}

// ---- geometry ----------------------------------------------------------------

juce::Rectangle<float> CurveEditor::canvasArea() const
{
    auto r = getLocalBounds().toFloat().reduced (12.0f, 8.0f);
    r.removeFromTop ((float) toolbarPx);
    r.removeFromBottom (18.0f);
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
    if (sh == nullptr || ! sh->custom)
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
    place (manualButton, 70);
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
    toolbarPx = (y + bh + 8) - top;
    help.setBounds (getLocalBounds().reduced (12, 8).removeFromBottom (16));
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
    if (rulerArea().contains (e.position) || (e.mods.isShiftDown() && canvasArea().contains (e.position)))
    {
        draggingSelection = true;
        dragAnchor = fractionAt (e.position.x);
        setSelection (std::nullopt);
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
    if (dragging >= 0)
    {
        const auto [a, b] = spanFractions();
        const double t = b > a ? (fractionAt (e.position.x) - a) / (b - a) : 0.0;
        movePoint (dragging, t, gainAt (e.position.y), false);
    }
}

void CurveEditor::mouseUp (const juce::MouseEvent&)
{
    ctx.endGesture();
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
        repaint();
    }
    rebuildPeaks();
}

bool CurveEditor::keyPressed (const juce::KeyPress& k)
{
    const bool ctrl = k.getModifiers().isCtrlDown() || k.getModifiers().isCommandDown();
    if (ctrl && k.getKeyCode() == 'Z' && k.getModifiers().isShiftDown()) { redo(); return true; }
    if (ctrl && k.getKeyCode() == 'Z') { undo(); return true; }
    if (ctrl && k.getKeyCode() == 'Y') { redo(); return true; }
    if (ctrl && k.getKeyCode() == 'C') { copyCurve(); return true; }
    if (ctrl && k.getKeyCode() == 'V') { pasteCurve(); return true; }
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
