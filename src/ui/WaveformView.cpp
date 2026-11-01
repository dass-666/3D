#include "ui/WaveformView.h"

#include "ui/Theme.h"

namespace ses
{

WaveformView::WaveformView (EditorContext& c) : ctx (c)
{
    ctx.addListener (this);
    syncFromScene();
}

WaveformView::~WaveformView()
{
    ctx.removeListener (this);
}

void WaveformView::sceneChanged()
{
    syncFromScene();
}

void WaveformView::syncFromScene()
{
    auto now = ctx.processor.getSourceAudio();
    if (now != audio)
    {
        audio = now;
        total = audio ? audio->buffer.getNumSamples() : 0;
        viewStart = 0;
        viewEnd = total;
        peaks.clear();
    }
    if (drag == Drag::None)
    {
        const auto& src = ctx.scene().source;
        selStart = juce::jlimit<juce::int64> (0, juce::jmax<juce::int64> (0, total - 1), src.trimStart);
        selEnd = src.trimEnd > selStart ? juce::jmin (src.trimEnd, total) : total;
    }
    if (peaks.empty())
        rebuildPeaks();
    repaint();
}

juce::Range<juce::int64> WaveformView::selectionSamples() const
{
    return { selStart, selEnd };
}

juce::Rectangle<float> WaveformView::waveArea() const
{
    return getLocalBounds().toFloat().reduced (2.0f, 4.0f);
}

void WaveformView::resized()
{
    rebuildPeaks();
}

void WaveformView::rebuildPeaks()
{
    peaks.clear();
    const auto area = waveArea();
    const int width = (int) area.getWidth();
    if (audio == nullptr || width <= 0 || viewEnd <= viewStart)
        return;
    peaks.resize ((size_t) width, { 0.0f, 0.0f });
    const auto& buf = audio->buffer;
    const double perPixel = (double) (viewEnd - viewStart) / width;
    for (int px = 0; px < width; ++px)
    {
        const auto a = viewStart + (juce::int64) (px * perPixel);
        const auto b = juce::jmin (viewEnd, juce::jmax (a + 1, viewStart + (juce::int64) ((px + 1) * perPixel)));
        float lo = 0.0f, hi = 0.0f;
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        {
            const auto r = buf.findMinMax (ch, (int) a, (int) (b - a));
            lo = juce::jmin (lo, r.getStart());
            hi = juce::jmax (hi, r.getEnd());
        }
        peaks[(size_t) px] = { lo, hi };
    }
}

float WaveformView::xFor (juce::int64 s) const
{
    const auto a = waveArea();
    if (viewEnd <= viewStart)
        return a.getX();
    return a.getX() + (float) ((double) (s - viewStart) / (double) (viewEnd - viewStart) * a.getWidth());
}

juce::int64 WaveformView::sampleAt (float x) const
{
    const auto a = waveArea();
    const double t = juce::jlimit (0.0, 1.0, (double) (x - a.getX()) / juce::jmax (1.0f, a.getWidth()));
    return juce::jlimit<juce::int64> (0, total, viewStart + (juce::int64) (t * (double) (viewEnd - viewStart)));
}

void WaveformView::paint (juce::Graphics& g)
{
    const auto area = waveArea();
    g.setColour (theme::background);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);

    if (audio == nullptr)
    {
        g.setColour (theme::outline);
        const float dashes[] { 6.0f, 5.0f };
        juce::Path border;
        border.addRoundedRectangle (getLocalBounds().toFloat().reduced (1.5f), 8.0f);
        juce::Path dashed;
        juce::PathStrokeType (1.2f).createDashedStroke (dashed, border, dashes, 2);
        g.fillPath (dashed);
        g.setColour (theme::textDim);
        g.setFont (theme::font (14.0f));
        g.drawText (juce::String::fromUTF8 ("Carga un WAV (o arr\xc3\xa1stralo aqu\xc3\xad) para empezar"),
                    getLocalBounds(), juce::Justification::centred);
        return;
    }

    const float mid = area.getCentreY();
    const float half = area.getHeight() * 0.46f;
    const float sx = xFor (selStart), ex = xFor (selEnd);

    // Selected region background.
    g.setColour (theme::blue.withAlpha (0.10f));
    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (sx, area.getY(), ex, area.getBottom()));

    g.setColour (theme::grid);
    g.drawHorizontalLine ((int) mid, area.getX(), area.getRight());

    for (size_t px = 0; px < peaks.size(); ++px)
    {
        const float x = area.getX() + (float) px;
        const bool inside = x >= sx && x <= ex;
        g.setColour (inside ? theme::turquoise : theme::textFaint.withAlpha (0.7f));
        const auto [lo, hi] = peaks[px];
        g.drawVerticalLine ((int) x, mid - hi * half, mid - lo * half + 1.0f);
    }

    // The selected repetition's own part of the fragment (play region) and its Volume Shaper curve.
    if (const auto* p = ctx.primaryPoint())
    {
        const auto region = clamped (p->region);
        const auto len = selEnd - selStart;
        const float rx0 = xFor (selStart + (juce::int64) (region.start * (double) len));
        const float rx1 = xFor (selStart + (juce::int64) (region.end * (double) len));
        if (region.start > 0.0 || region.end < 1.0)
        {
            g.setColour (theme::pointColour (p->number).withAlpha (0.18f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (rx0, area.getY(), rx1, area.getBottom()));
            g.setColour (theme::pointColour (p->number).withAlpha (0.9f));
            g.drawRect (juce::Rectangle<float>::leftTopRightBottom (rx0, area.getY(), rx1, area.getBottom()), 1.0f);
        }
        if (p->shaper.enabled && rx1 > rx0 + 4.0f)
        {
            const double voiceSeconds = (region.end - region.start) * (double) len / audio->sampleRate;
            const auto& snapH = ctx.processor.getHostSnapshot();
            const double bpm = snapH.hasBpm.load() ? snapH.bpm.load() : 120.0;
            const double divisionSeconds = beatsPerDivision (p->shaper.division) * 60.0 / bpm;
            juce::Path path;
            const int steps = juce::jmax (16, (int) (rx1 - rx0));
            for (int i = 0; i <= steps; ++i)
            {
                const double t = voiceSeconds * i / steps;
                const float gN = shaperGain (p->shaper, juce::jmin (t, voiceSeconds * 0.99999), voiceSeconds, divisionSeconds);
                const float px = rx0 + (rx1 - rx0) * (float) i / (float) steps;
                const float py = area.getBottom() - 2.0f - (area.getHeight() - 4.0f) * gN;
                i == 0 ? path.startNewSubPath (px, py) : path.lineTo (px, py);
            }
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.strokePath (path, juce::PathStrokeType (1.5f));
        }
    }

    // Time ruler (seconds) along the bottom edge.
    {
        const double viewSeconds = (double) (viewEnd - viewStart) / audio->sampleRate;
        double stepS = 1.0;
        for (double cand : { 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1.0, 2.0, 5.0 })
            if (viewSeconds / cand <= 14.0) { stepS = cand; break; }
        const double first = std::ceil ((double) viewStart / audio->sampleRate / stepS) * stepS;
        g.setFont (theme::font (10.0f));
        for (double t = first; t * audio->sampleRate <= (double) viewEnd; t += stepS)
        {
            const float x = xFor ((juce::int64) (t * audio->sampleRate));
            g.setColour (theme::outline);
            g.drawVerticalLine ((int) x, area.getBottom() - 6.0f, area.getBottom());
            g.setColour (theme::textFaint);
            g.drawText (juce::String (t, stepS < 0.1 ? 2 : (stepS < 1.0 ? 1 : 0)) + " s",
                        juce::Rectangle<float> (x + 2.0f, area.getBottom() - 14.0f, 48.0f, 12.0f), juce::Justification::left);
        }
    }

    // Handles.
    for (const float x : { sx, ex })
    {
        g.setColour (theme::violet);
        g.fillRect (juce::Rectangle<float> (x - 1.0f, area.getY(), 2.0f, area.getHeight()));
        g.fillRoundedRectangle (juce::Rectangle<float> (10.0f, 16.0f).withCentre ({ x, area.getY() + 9.0f }), 3.0f);
    }

    if (viewStart > 0 || viewEnd < total)
    {
        g.setColour (theme::textDim);
        g.setFont (theme::font (11.0f));
        g.drawText ("zoom x" + juce::String ((double) total / (double) (viewEnd - viewStart), 1),
                    area.reduced (6.0f, 2.0f), juce::Justification::topRight);
    }
}

void WaveformView::mouseMove (const juce::MouseEvent& e)
{
    if (audio == nullptr)
        return;
    const bool nearHandle = std::abs (e.position.x - xFor (selStart)) < 6.0f || std::abs (e.position.x - xFor (selEnd)) < 6.0f;
    setMouseCursor (nearHandle ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::IBeamCursor);
}

void WaveformView::mouseDown (const juce::MouseEvent& e)
{
    if (audio == nullptr)
        return;
    if (std::abs (e.position.x - xFor (selStart)) < 6.0f)
        drag = Drag::Start;
    else if (std::abs (e.position.x - xFor (selEnd)) < 6.0f)
        drag = Drag::End;
    else
    {
        drag = Drag::New;
        dragAnchor = sampleAt (e.position.x);
    }
}

void WaveformView::mouseDrag (const juce::MouseEvent& e)
{
    if (audio == nullptr || drag == Drag::None)
        return;
    const auto minLen = (juce::int64) (0.01 * audio->sampleRate);  // 10 ms
    const auto s = sampleAt (e.position.x);
    if (drag == Drag::Start)
        selStart = juce::jlimit<juce::int64> (0, selEnd - minLen, s);
    else if (drag == Drag::End)
        selEnd = juce::jlimit<juce::int64> (selStart + minLen, total, s);
    else
    {
        selStart = juce::jmin (dragAnchor, s);
        selEnd = juce::jmax (dragAnchor, s);
        if (selEnd - selStart < minLen)
            selEnd = juce::jmin (total, selStart + minLen);
    }
    repaint();
}

void WaveformView::mouseUp (const juce::MouseEvent& e)
{
    if (audio == nullptr || drag == Drag::None)
        return;
    const bool wasClick = drag == Drag::New && e.getDistanceFromDragStart() < 3;
    drag = Drag::None;
    if (wasClick)
    {
        syncFromScene();  // a click without dragging keeps the current selection
        return;
    }
    const auto s = selStart, en = selEnd;
    ctx.edit ([s, en, total = total] (Scene& scene)
    {
        scene.source.trimStart = s;
        scene.source.trimEnd = en >= total ? 0 : en;
    });
}

void WaveformView::mouseDoubleClick (const juce::MouseEvent&)
{
    viewStart = 0;
    viewEnd = total;
    rebuildPeaks();
    repaint();
}

void WaveformView::zoomBy (double factor)
{
    if (audio == nullptr || total <= 0)
        return;
    const auto len = viewEnd - viewStart;
    const auto anchor = (selStart + selEnd) / 2;
    const auto minLen = juce::jmin<juce::int64> (total, (juce::int64) (0.02 * audio->sampleRate));
    const auto newLen = juce::jlimit<juce::int64> (minLen, total, (juce::int64) ((double) len * factor));
    const double t = (double) (anchor - viewStart) / juce::jmax<juce::int64> (1, len);
    auto start = anchor - (juce::int64) (t * (double) newLen);
    viewStart = juce::jlimit<juce::int64> (0, total - newLen, start);
    viewEnd = viewStart + newLen;
    rebuildPeaks();
    repaint();
}

void WaveformView::fitView()
{
    viewStart = 0;
    viewEnd = total;
    rebuildPeaks();
    repaint();
}

void WaveformView::zoomToSelection()
{
    if (audio == nullptr || selEnd <= selStart)
        return;
    const auto pad = (selEnd - selStart) / 10;
    viewStart = juce::jmax<juce::int64> (0, selStart - pad);
    viewEnd = juce::jmin (total, selEnd + pad);
    rebuildPeaks();
    repaint();
}

void WaveformView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (audio == nullptr || total <= 0)
        return;
    const auto len = viewEnd - viewStart;
    if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
    {
        const auto delta = (juce::int64) ((double) len * (std::abs (w.deltaX) > 0.0f ? -w.deltaX : -w.deltaY) * 0.5);
        const auto start = juce::jlimit<juce::int64> (0, total - len, viewStart + delta);
        viewStart = start;
        viewEnd = start + len;
    }
    else
    {
        const auto anchor = sampleAt (e.position.x);
        const double factor = w.deltaY > 0 ? 0.8 : 1.25;
        const auto minLen = juce::jmin<juce::int64> (total, (juce::int64) (0.02 * audio->sampleRate));
        const auto newLen = juce::jlimit<juce::int64> (minLen, total, (juce::int64) ((double) len * factor));
        const double t = (double) (anchor - viewStart) / juce::jmax<juce::int64> (1, len);
        auto start = anchor - (juce::int64) (t * (double) newLen);
        start = juce::jlimit<juce::int64> (0, total - newLen, start);
        viewStart = start;
        viewEnd = start + newLen;
    }
    rebuildPeaks();
    repaint();
}

bool WaveformView::isInterestedInFileDrag (const juce::StringArray& files)
{
    return files.size() == 1 && files[0].endsWithIgnoreCase (".wav");
}

void WaveformView::filesDropped (const juce::StringArray& files, int, int)
{
    if (onFileDropped && files.size() == 1)
        onFileDropped (juce::File (files[0]));
}

} // namespace ses
