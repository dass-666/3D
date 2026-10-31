#include "ui/TimelineView.h"

#include "ui/Theme.h"

namespace ses
{
namespace
{
const juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }
constexpr RhythmDivision kDivisions[] { RhythmDivision::Quarter, RhythmDivision::Eighth, RhythmDivision::DottedEighth,
                                        RhythmDivision::Sixteenth, RhythmDivision::QuarterTriplet, RhythmDivision::EighthTriplet };
const char* kDivisionNames[] { "1/4", "1/8", "1/8 con puntillo", "1/16", "1/4 tresillo", "1/8 tresillo" };
}

TimelineView::TimelineView (EditorContext& c) : ctx (c)
{
    ctx.addListener (this);
    for (int i = 0; i < (int) std::size (kDivisions); ++i)
        rhythmBox.addItem (u8 (kDivisionNames[i]), i + 1);
    rhythmBox.onChange = [this]
    {
        const int i = rhythmBox.getSelectedItemIndex();
        if (i >= 0)
            ctx.edit ([d = kDivisions[i]] (Scene& s) { s.rhythm = d; });
    };
    rhythmBox.setTooltip (u8 ("Divisi\xc3\xb3n r\xc3\xadtmica: rejilla de la l\xc3\xadnea temporal y alineaci\xc3\xb3n del disparo"));
    addAndMakeVisible (rhythmBox);
    snapToggle.setToggleState (true, juce::dontSendNotification);
    addChildComponent (snapToggle);
    layoutBlocks();
}

TimelineView::~TimelineView()
{
    ctx.removeListener (this);
}

void TimelineView::setProMode (bool shouldBePro)
{
    pro = shouldBePro;
    snapToggle.setVisible (pro);
    if (! pro)
        snapToggle.setToggleState (true, juce::dontSendNotification);
}

void TimelineView::resized()
{
    auto top = getLocalBounds().reduced (14, 8).removeFromTop (24);
    top.removeFromLeft (230);
    rhythmBox.setBounds (top.removeFromLeft (150));
    top.removeFromLeft (12);
    snapToggle.setBounds (top.removeFromLeft (180));
}

juce::Rectangle<float> TimelineView::gridArea() const
{
    return getLocalBounds().toFloat().reduced (14.0f, 8.0f).withTrimmedTop (32.0f).withTrimmedLeft (34.0f);
}

void TimelineView::layoutBlocks()
{
    const auto& scene = ctx.scene();
    for (int i = 0; i < (int) std::size (kDivisions); ++i)
        if (kDivisions[i] == scene.rhythm)
            rhythmBox.setSelectedItemIndex (i, juce::dontSendNotification);

    // Fragment length in beats at the current tempo (visual only).
    double fragmentBeats = 0.25;
    if (auto audio = ctx.processor.getSourceAudio())
    {
        const auto& src = scene.source;
        const auto total = (juce::int64) audio->buffer.getNumSamples();
        const auto end = src.trimEnd > src.trimStart ? juce::jmin (src.trimEnd, total) : total;
        const double seconds = (double) (end - src.trimStart) / audio->sampleRate;
        const auto& snapH = ctx.processor.getHostSnapshot();
        const double bpm = snapH.hasBpm.load() ? snapH.bpm.load() : 120.0;
        fragmentBeats = juce::jmax (0.05, seconds * bpm / 60.0);
    }

    blocks.clear();
    for (auto& p : scene.points)
        blocks.push_back ({ p.id, p.number, p.enabled, p.timing.beat, fragmentBeats, 0 });
    std::sort (blocks.begin(), blocks.end(), [] (auto& a, auto& b) { return a.beat < b.beat || (juce::exactlyEqual (a.beat, b.beat) && a.number < b.number); });

    // Greedy lanes: simultaneous / overlapping repetitions stack instead of hiding each other.
    std::vector<double> laneEnd;
    double maxEnd = 0.0;
    for (auto& b : blocks)
    {
        size_t lane = 0;
        while (lane < laneEnd.size() && laneEnd[lane] > b.beat + 1e-9)
            ++lane;
        if (lane == laneEnd.size())
            laneEnd.push_back (0.0);
        laneEnd[lane] = b.beat + b.lengthBeats;
        b.lane = (int) lane;
        maxEnd = juce::jmax (maxEnd, b.beat + b.lengthBeats);
    }
    lanes = juce::jmax (1, (int) laneEnd.size());
    lengthBeats = juce::jmax (8.0, std::ceil ((maxEnd + 1.0) / 4.0) * 4.0);
}

float TimelineView::xForBeat (double b) const
{
    const auto a = gridArea();
    return a.getX() + (float) (b / lengthBeats) * a.getWidth();
}

double TimelineView::beatAt (float x) const
{
    const auto a = gridArea();
    return juce::jlimit (0.0, lengthBeats, (double) (x - a.getX()) / a.getWidth() * lengthBeats);
}

double TimelineView::snap (double b) const
{
    if (! snapToggle.getToggleState())
        return juce::jmax (0.0, b);
    const double step = beatsPerStep (ctx.scene().rhythm);
    return juce::jmax (0.0, std::round (b / step) * step);
}

juce::Rectangle<float> TimelineView::blockRect (const Block& b) const
{
    const auto a = gridArea();
    const float laneH = juce::jmin (26.0f, (a.getHeight() - 18.0f) / (float) lanes);
    const float x = xForBeat (b.beat);
    const float w = juce::jmax (22.0f, xForBeat (b.beat + b.lengthBeats) - x);
    return { x, a.getY() + 18.0f + laneH * (float) b.lane + 1.0f, w, laneH - 3.0f };
}

const TimelineView::Block* TimelineView::hit (juce::Point<float> p) const
{
    const Block* h = nullptr;
    for (auto& b : blocks)
        if (blockRect (b).contains (p))
            h = &b;
    return h;
}

void TimelineView::paint (juce::Graphics& g)
{
    theme::drawPanel (g, getLocalBounds().toFloat(), u8 ("CU\xc3\x81NDO SUENA  \xc2\xb7  L\xc3\x8dNEA TEMPORAL"));
    const auto a = gridArea();

    g.setColour (theme::background);
    g.fillRoundedRectangle (a.expanded (4.0f, 2.0f), 6.0f);

    // Grid: steps (faint), beats, bars (strong). Labels above.
    const double step = beatsPerStep (ctx.scene().rhythm);
    for (double b = 0.0; b <= lengthBeats + 1e-9; b += step)
    {
        const float x = xForBeat (b);
        const bool bar = std::fmod (b + 1e-9, 4.0) < 1e-6;
        const bool beatLine = std::fmod (b + 1e-9, 1.0) < 1e-6;
        g.setColour (bar ? theme::outline.brighter (0.3f) : beatLine ? theme::outline : theme::grid);
        g.drawVerticalLine ((int) x, a.getY() + 16.0f, a.getBottom());
        if (beatLine)
        {
            g.setColour (bar ? theme::text : theme::textFaint);
            g.setFont (theme::font (11.0f, bar));
            const auto label = b < 1e-9 ? juce::String ("T") : juce::String ((int) std::round (b));
            g.drawText (label, juce::Rectangle<float> (x - 12.0f, a.getY(), 24.0f, 14.0f), juce::Justification::centred);
        }
    }

    g.setColour (theme::textFaint);
    g.setFont (theme::font (10.5f));
    g.drawText ("beats", juce::Rectangle<float> (a.getX() - 36.0f, a.getY(), 32.0f, 14.0f), juce::Justification::centredRight);

    for (auto& b : blocks)
    {
        const auto r = blockRect (b);
        const auto col = theme::pointColour (b.number);
        const bool sel = ctx.isSelected (b.id);
        g.setColour (b.enabled ? col.withAlpha (sel ? 0.95f : 0.7f) : theme::outline);
        g.fillRoundedRectangle (r, 4.0f);
        if (sel)
        {
            g.setColour (juce::Colours::white);
            g.drawRoundedRectangle (r.expanded (1.0f), 4.0f, 1.6f);
        }
        g.setColour (b.enabled ? juce::Colours::white : theme::textFaint);
        g.setFont (theme::font (12.0f, true));
        g.drawText (juce::String (b.number), r.reduced (6.0f, 0.0f), juce::Justification::centredLeft);
    }

    if (blocks.empty())
    {
        g.setColour (theme::textDim);
        g.setFont (theme::font (12.5f));
        g.drawText (u8 ("Sin repeticiones. A\xc3\xb1\x61\x64\x65 puntos en el mapa."), a, juce::Justification::centred);
    }

    if (playheadBeat >= 0.0 && playheadBeat <= lengthBeats)
    {
        const float x = xForBeat (playheadBeat);
        g.setColour (theme::turquoise);
        g.fillRect (juce::Rectangle<float> (x - 1.0f, a.getY() + 14.0f, 2.0f, a.getHeight() - 14.0f));
    }
}

void TimelineView::updatePlayhead()
{
    const auto& s = ctx.processor.getHostSnapshot();
    const int refs = s.references.load();
    double next = -1.0;
    if (refs > 0)
    {
        const double since = ctx.processor.getCurrentBeat() - s.referenceBeat.load();
        if (since >= -1e-6 && since <= lengthBeats)
            next = juce::jmax (0.0, since);
    }
    if (refs != lastReferences)
    {
        lastReferences = refs;
        layoutBlocks();  // tempo may have changed the block lengths
    }
    if (std::abs (next - playheadBeat) > 1e-4)
    {
        playheadBeat = next;
        repaint();
    }
}

void TimelineView::mouseDown (const juce::MouseEvent& e)
{
    const bool additive = e.mods.isShiftDown() || e.mods.isCommandDown() || e.mods.isCtrlDown();
    if (auto* b = hit (e.position))
    {
        if (additive)
            ctx.select (b->id, true);
        else if (! ctx.isSelected (b->id))
            ctx.select (b->id, false);
        else
            ctx.makePrimary (b->id);
        dragging = ctx.isSelected (b->id);
        dragStartBeat = beatAt (e.position.x);
        dragOrigins.clear();
        for (auto& id : ctx.selection())
            if (auto* p = findPoint (ctx.scene(), id))
                dragOrigins.push_back ({ id, p->timing.beat });
        return;
    }
    if (! additive && gridArea().contains (e.position))
        ctx.clearSelection();
}

void TimelineView::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging)
        return;
    const double delta = beatAt (e.position.x) - dragStartBeat;
    const auto origins = dragOrigins;
    ctx.edit ([&] (Scene& s)
    {
        for (auto& [id, origin] : origins)
            setTiming (s, id, { snap (origin + delta) });
    });
}

void TimelineView::mouseUp (const juce::MouseEvent&)
{
    dragging = false;
}

} // namespace ses
