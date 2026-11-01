#include "ui/SpatialMap.h"

#include "model/VoicePresets.h"
#include "ui/Theme.h"

namespace ses
{
namespace
{
constexpr float kPointRadius = 15.0f;
const juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }
}

SpatialMap::SpatialMap (EditorContext& c) : ctx (c)
{
    ctx.addListener (this);
    setWantsKeyboardFocus (true);
}

SpatialMap::~SpatialMap()
{
    ctx.removeListener (this);
}

juce::Rectangle<float> SpatialMap::mapArea() const
{
    auto r = getLocalBounds().toFloat().reduced (26.0f, 22.0f);
    const float side = juce::jmin (r.getWidth(), r.getHeight());
    return r.withSizeKeepingCentre (side, side);
}

juce::Point<float> SpatialMap::toScreen (Position p) const
{
    const auto a = mapArea();
    return { a.getX() + (float) (p.x / 100.0) * a.getWidth(), a.getY() + (float) (p.y / 100.0) * a.getHeight() };
}

Position SpatialMap::toModel (juce::Point<float> s) const
{
    const auto a = mapArea();
    return clamped (Position { (s.x - a.getX()) / a.getWidth() * 100.0, (s.y - a.getY()) / a.getHeight() * 100.0 });
}

const Point* SpatialMap::hitTestPoint (juce::Point<float> s) const
{
    const Point* hit = nullptr;
    for (auto& p : ctx.scene().points)  // last drawn wins
        if (toScreen (p.position).getDistanceFrom (s) <= kPointRadius + 3.0f)
            hit = &p;
    return hit;
}

void SpatialMap::updateSounding()
{
    std::vector<juce::uint64> now;
    const auto& snap = ctx.processor.getHostSnapshot();
    for (auto& p : ctx.scene().points)
        if (snap.isSounding (keyForPoint (p.id)))
            now.push_back (keyForPoint (p.id));
    pulse = std::fmod (pulse + 0.08f, 1.0f);
    if (now != sounding || ! now.empty())
    {
        sounding = std::move (now);
        repaint();
    }
}

void SpatialMap::paint (juce::Graphics& g)
{
    const auto a = mapArea();
    const auto c = a.getCentre();
    const float radius = a.getWidth() * 0.5f;

    // Field.
    juce::ColourGradient bg (theme::panelHi.brighter (0.05f), c.x, c.y, theme::background, c.x + radius, c.y + radius, true);
    g.setGradientFill (bg);
    g.fillRoundedRectangle (a, 14.0f);
    g.setColour (theme::outline);
    g.drawRoundedRectangle (a, 14.0f, 1.0f);

    // Distance rings and axes.
    for (int i = 1; i <= 4; ++i)
    {
        const float rr = radius * (float) i / 4.0f;
        g.setColour (theme::grid.brighter (i == 4 ? 0.25f : 0.1f));
        g.drawEllipse (juce::Rectangle<float> (rr * 2.0f, rr * 2.0f).withCentre (c), i == 4 ? 1.3f : 1.0f);
    }
    g.setColour (theme::grid.brighter (0.15f));
    const float dash[] { 4.0f, 4.0f };
    g.drawDashedLine ({ a.getX(), c.y, a.getRight(), c.y }, dash, 2, 1.0f);
    g.drawDashedLine ({ c.x, a.getY(), c.x, a.getBottom() }, dash, 2, 1.0f);

    // Labels.
    g.setFont (theme::font (11.5f, true));
    g.setColour (theme::textDim);
    g.drawText ("IZQ", juce::Rectangle<float> (a.getX() - 26.0f, c.y - 9.0f, 24.0f, 18.0f), juce::Justification::centredRight);
    g.drawText ("DER", juce::Rectangle<float> (a.getRight() + 3.0f, c.y - 9.0f, 26.0f, 18.0f), juce::Justification::centredLeft);
    g.drawText ("DELANTE", juce::Rectangle<float> (c.x - 50.0f, a.getY() - 20.0f, 100.0f, 18.0f), juce::Justification::centred);
    g.drawText (u8 ("DETR\xc3\x81S (aprox.)"), juce::Rectangle<float> (c.x - 70.0f, a.getBottom() + 2.0f, 140.0f, 18.0f), juce::Justification::centred);

    // Listener.
    g.setColour (theme::text.withAlpha (0.9f));
    g.fillEllipse (juce::Rectangle<float> (18.0f, 18.0f).withCentre (c));
    juce::Path nose;
    nose.addTriangle (c.x - 5.0f, c.y - 8.0f, c.x + 5.0f, c.y - 8.0f, c.x, c.y - 15.0f);
    g.fillPath (nose);
    g.setColour (theme::background);
    g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (c));

    // Points: lines to the listener first, then the discs.
    for (auto& p : ctx.scene().points)
    {
        const auto s = toScreen (p.position);
        g.setColour (theme::pointColour (p.number).withAlpha (p.enabled ? 0.25f : 0.08f));
        g.drawLine ({ c, s }, 1.0f);
    }

    for (auto& p : ctx.scene().points)
    {
        const auto s = toScreen (p.position);
        const auto col = theme::pointColour (p.number);
        const bool sel = ctx.isSelected (p.id);
        const bool isSounding = std::find (sounding.begin(), sounding.end(), keyForPoint (p.id)) != sounding.end();
        const auto disc = juce::Rectangle<float> (kPointRadius * 2.0f, kPointRadius * 2.0f).withCentre (s);

        if (isSounding)
        {
            const float rr = kPointRadius + 4.0f + 10.0f * pulse;
            g.setColour (col.withAlpha (0.45f * (1.0f - pulse)));
            g.drawEllipse (juce::Rectangle<float> (rr * 2.0f, rr * 2.0f).withCentre (s), 2.0f);
        }
        if (sel)
        {
            g.setColour (col.withAlpha (0.28f));
            g.fillEllipse (disc.expanded (7.0f));
        }
        if (p.enabled)
        {
            juce::ColourGradient grad (col.brighter (0.35f), s.x, s.y - kPointRadius, col.darker (0.25f), s.x, s.y + kPointRadius, false);
            g.setGradientFill (grad);
            g.fillEllipse (disc);
        }
        else
        {
            g.setColour (theme::panel);
            g.fillEllipse (disc);
            g.setColour (theme::textFaint);
            g.drawEllipse (disc.reduced (1.0f), 1.5f);
        }
        if (sel)
        {
            g.setColour (juce::Colours::white);
            g.drawEllipse (disc.expanded (2.5f), 2.0f);
        }
        g.setColour (p.enabled ? juce::Colours::white : theme::textFaint);
        g.setFont (theme::font (14.0f, true));
        g.drawText (juce::String (p.number), disc, juce::Justification::centred);

        if (p.solo)
        {
            const auto badge = juce::Rectangle<float> (14.0f, 14.0f).withCentre ({ s.x + kPointRadius - 2.0f, s.y - kPointRadius + 2.0f });
            g.setColour (theme::amber);
            g.fillEllipse (badge);
            g.setColour (theme::background);
            g.setFont (theme::font (10.0f, true));
            g.drawText ("S", badge, juce::Justification::centred);
        }
    }

    if (lasso)
    {
        g.setColour (theme::turquoise.withAlpha (0.12f));
        g.fillRect (lassoRect);
        g.setColour (theme::turquoise.withAlpha (0.7f));
        g.drawRect (lassoRect, 1.0f);
    }

    if (ctx.scene().points.empty())
    {
        g.setColour (theme::textDim);
        g.setFont (theme::font (13.0f));
        g.drawText (u8 ("Doble clic en el mapa para a\xc3\xb1" "adir una repetici\xc3\xb3n"), a.withTrimmedTop (a.getHeight() * 0.6f),
                    juce::Justification::centredTop);
    }
}

void SpatialMap::mouseDown (const juce::MouseEvent& e)
{
    ctx.beginGesture();  // one undo step per drag
    if (isShowing())
        grabKeyboardFocus();
    const bool additive = e.mods.isShiftDown() || e.mods.isCommandDown() || e.mods.isCtrlDown();
    if (auto* p = hitTestPoint (e.position))
    {
        if (e.mods.isPopupMenu())
        {
            if (! ctx.isSelected (p->id))
                ctx.select (p->id, false);
            showMenu (*p);
            return;
        }
        if (additive)
            ctx.select (p->id, true);
        else if (! ctx.isSelected (p->id))
            ctx.select (p->id, false);
        else
            ctx.makePrimary (p->id);  // keep the group, edit this one in the panel
        draggingPoints = ctx.isSelected (p->id);
        lastDrag = e.position;
        return;
    }
    lasso = true;
    lassoRect = { e.position, e.position };
    if (! additive)
        ctx.clearSelection();
}

void SpatialMap::mouseDrag (const juce::MouseEvent& e)
{
    if (draggingPoints)
    {
        const auto a = mapArea();
        const double dx = (e.position.x - lastDrag.x) / a.getWidth() * 100.0;
        const double dy = (e.position.y - lastDrag.y) / a.getHeight() * 100.0;
        lastDrag = e.position;
        const auto ids = ctx.selection();
        ctx.edit ([&] (Scene& s)
        {
            for (auto& id : ids)
                if (auto* p = findPoint (s, id))
                    movePoint (s, id, { p->position.x + dx, p->position.y + dy });
        });
        return;
    }
    if (lasso)
    {
        lassoRect = juce::Rectangle<float> (e.mouseDownPosition, e.position);
        repaint();
    }
}

void SpatialMap::mouseUp (const juce::MouseEvent& e)
{
    ctx.endGesture();
    draggingPoints = false;
    if (lasso)
    {
        lasso = false;
        if (lassoRect.getWidth() > 3.0f || lassoRect.getHeight() > 3.0f)
        {
            juce::StringArray ids = e.mods.isShiftDown() ? ctx.selection() : juce::StringArray();
            for (auto& p : ctx.scene().points)
                if (lassoRect.contains (toScreen (p.position)))
                    ids.addIfNotAlreadyThere (p.id);
            ctx.setSelection (ids);
        }
        repaint();
    }
}

void SpatialMap::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (hitTestPoint (e.position) == nullptr && mapArea().contains (e.position))
        addPointAt (ctx, toModel (e.position));
}

bool SpatialMap::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey)
    {
        deleteSelection (ctx);
        return true;
    }
    if (k == juce::KeyPress ('d', juce::ModifierKeys::commandModifier, 0))
    {
        duplicateSelection (ctx);
        return true;
    }
    if (k == juce::KeyPress ('m'))
    {
        toggleMuteSelection (ctx);
        return true;
    }
    if (k == juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 0))
    {
        juce::StringArray all;
        for (auto& p : ctx.scene().points)
            all.add (p.id);
        ctx.setSelection (all);
        return true;
    }
    return false;
}

void SpatialMap::showMenu (const Point& p)
{
    juce::PopupMenu m;
    m.addItem (5, "Disparar esta");
    m.addSeparator();
    m.addItem (1, "Duplicar");
    m.addItem (2, p.enabled ? "Silenciar" : "Activar");
    m.addItem (3, p.solo ? "Quitar solo" : "Solo");
    m.addSeparator();
    m.addItem (4, "Eliminar");
    const auto id = p.id;
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                     [safe = juce::Component::SafePointer<SpatialMap> (this), id] (int r)
                     {
                         if (safe == nullptr)
                             return;
                         auto& c = safe->ctx;
                         if (r == 1) duplicateSelection (c);
                         if (r == 2) toggleMuteSelection (c);
                         if (r == 3) c.edit ([id] (Scene& s) { if (auto* q = findPoint (s, id)) q->solo = ! q->solo; });
                         if (r == 4) deleteSelection (c);
                         if (r == 5) c.processor.requestTriggerPoint (id);
                     });
}

void SpatialMap::addPointAt (EditorContext& c, Position pos)
{
    std::optional<juce::String> id;
    c.edit ([&] (Scene& s)
    {
        const auto& presets = builtInVoicePresets();
        id = addPoint (s, pos, std::nullopt, presets.empty() ? nullptr : &presets.front());
    });
    if (id)
        c.select (*id, false);
}

void SpatialMap::duplicateSelection (EditorContext& c)
{
    const auto ids = c.selection();
    juce::StringArray created;
    c.edit ([&] (Scene& s)
    {
        for (auto& id : ids)
            if (auto n = duplicatePoint (s, id))
                created.add (*n);
    });
    if (! created.isEmpty())
        c.setSelection (created);
}

void SpatialMap::deleteSelection (EditorContext& c)
{
    const auto ids = c.selection();
    c.edit ([&] (Scene& s)
    {
        for (auto& id : ids)
            removePoint (s, id);
    });
}

void SpatialMap::toggleMuteSelection (EditorContext& c)
{
    const auto ids = c.selection();
    c.edit ([&] (Scene& s)
    {
        bool anyEnabled = false;
        for (auto& id : ids)
            if (auto* p = findPoint (s, id))
                anyEnabled |= p->enabled;
        for (auto& id : ids)
            if (auto* p = findPoint (s, id))
                p->enabled = ! anyEnabled;
    });
}

} // namespace ses
