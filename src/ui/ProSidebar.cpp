#include "ui/ProSidebar.h"

#include "plugin/Parameters.h"
#include "ui/PresetActions.h"
#include "ui/Theme.h"

namespace ses
{
namespace
{
juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }

struct CategoryInfo { const char* id; const char* name; };
const CategoryInfo kCategoryInfo[] {
    { "shaper", "Cortes / Volume Shaper" }, { "stutter", "Stutter / Repetici\xc3\xb3n" }, { "pitch", "Pitch y formantes" },
    { "filters", "Filtros y fundidos" }, { "chain", "Cadena de efectos (orden)" }, { "delay", "Delay" }, { "reverb", "Reverb" }, { "modulation", "Modulaci\xc3\xb3n" },
    { "character", "Car\xc3\xa1" "cter (Robot, Monster, etc.)" }, { "space", "Espacio / Paneo" }, { "time", "Tiempo del punto" },
};
}  // namespace

ProSidebar::ProSidebar (EditorContext& c) : ctx (c)
{
    prevButton.setTooltip (u8 ("Preset creativo anterior (se aplica a la selecci\xc3\xb3n)"));
    nextButton.setTooltip (u8 ("Preset creativo siguiente (se aplica a la selecci\xc3\xb3n)"));
    prevButton.onClick = [this] { stepPreset (-1); };
    nextButton.onClick = [this] { stepPreset (+1); };
    addAndMakeVisible (prevButton);
    addAndMakeVisible (nextButton);
    menuButton.setTooltip (u8 ("Elegir preset, guardar la repetici\xc3\xb3n como preset propio, favoritos"));
    menuButton.onClick = [this] { showPresetMenu(); };
    favButton.setTooltip (u8 ("Marcar / quitar favorito"));
    favButton.onClick = [this]
    {
        if (const auto* p = ctx.primaryPoint(); p != nullptr && p->creativePreset.isNotEmpty())
            UserPresetStore::shared().setFavourite (p->creativePreset, ! UserPresetStore::shared().isFavourite (p->creativePreset));
    };
    addAndMakeVisible (menuButton);
    addAndMakeVisible (favButton);
    UserPresetStore::shared().addChangeListener (this);

    auto knob = [this] (juce::Slider& s, juce::Label& l, const char* name, double lo, double hi, double step, const juce::String& suffix)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
        s.setRange (lo, hi, step);
        s.setTextValueSuffix (suffix);
        s.setName (u8 (name));
        s.onDragStart = [this] { ctx.beginGesture(); };
        s.onDragEnd = [this] { ctx.endGesture(); };
        addAndMakeVisible (s);
        l.setText (u8 (name), juce::dontSendNotification);
        l.setFont (theme::font (12.0f, true));
        l.setJustificationType (juce::Justification::centred);
        l.setColour (juce::Label::textColourId, theme::text);
        addAndMakeVisible (l);
    };
    knob (speed, speedL, "Velocidad", limits::minShaperCuts, limits::maxShaperCuts, 1.0, " cortes");
    speed.setSkewFactorFromMidPoint (32.0);
    speed.setTooltip (u8 ("N\xc3\xbamero de cortes del Volume Shaper en la zona (2\xe2\x80\x93" "256). Act\xc3\xba" "a sobre el generador."));
    speed.onValueChange = [this]
    {
        if (updating) return;
        const int n = (int) speed.getValue();
        editSelection ([n] (Point& p)
        {
            p.shaper.enabled = true;
            p.shaper.custom = false;
            p.shaper.rateMode = ShaperSettings::RateMode::Count;
            p.shaper.cuts = n;
            p.shaper = clamped (p.shaper);
        });
    };
    knob (depth, depthL, "Intensidad", 0.0, 100.0, 1.0, " %");
    depth.setTooltip (u8 ("Profundidad de los cortes: cu\xc3\xa1nto baja el volumen en la parte cerrada. Act\xc3\xba" "a sobre el generador."));
    depth.onValueChange = [this]
    {
        if (updating) return;
        const double d = depth.getValue();
        editSelection ([d] (Point& p)
        {
            p.shaper.enabled = true;
            p.shaper.custom = false;
            p.shaper.depthPercent = d;
            p.shaper = clamped (p.shaper);
        });
    };
    knob (mix, mixL, "Mezcla", 0.0, 100.0, 1.0, " %");
    mix.setTooltip (u8 ("Nivel de las repeticiones frente al audio original (par\xc3\xa1metro Mezcla del plugin). El original nunca se aten\xc3\xba" "a."));
    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (ctx.processor.getParameters(), params::mix, mix);

    for (auto* l : { &quickTitle, &advancedTitle })
    {
        l->setFont (theme::font (12.5f, true));
        l->setColour (juce::Label::textColourId, theme::textDim);
        addAndMakeVisible (*l);
    }
    quickTitle.setText (u8 ("Controles del preset (r\xc3\xa1pidos)"), juce::dontSendNotification);
    advancedTitle.setText (u8 ("Ajustes avanzados (Pro)"), juce::dontSendNotification);
    quickNote.setFont (theme::font (10.5f));
    quickNote.setColour (juce::Label::textColourId, theme::textFaint);
    quickNote.setJustificationType (juce::Justification::centredTop);
    addAndMakeVisible (quickNote);

    for (const auto& ci : kCategoryInfo)
    {
        auto* b = categoryButtons.add (new juce::TextButton (u8 (ci.name)));
        b->setConnectedEdges (juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight | juce::Button::ConnectedOnTop | juce::Button::ConnectedOnBottom);
        b->setClickingTogglesState (false);
        b->onClick = [this, id = juce::String (ci.id)]
        {
            setActiveCategory (id);
            if (onCategory)
                onCategory (id);
        };
        addAndMakeVisible (*b);
    }
    setActiveCategory (activeCategory);
    ctx.addListener (this);
    refresh();
}

ProSidebar::~ProSidebar()
{
    UserPresetStore::shared().removeChangeListener (this);
    ctx.removeListener (this);
}

void ProSidebar::setActiveCategory (const juce::String& id)
{
    activeCategory = id;
    for (int i = 0; i < categoryButtons.size(); ++i)
    {
        const bool on = juce::String (kCategoryInfo[i].id) == id;
        auto* b = categoryButtons[i];
        b->setColour (juce::TextButton::buttonColourId, on ? theme::violet.withAlpha (0.55f) : theme::panel);
        b->setColour (juce::TextButton::textColourOffId, on ? theme::text : theme::textDim);
    }
}

void ProSidebar::editSelection (const std::function<void (Point&)>& fn)
{
    const auto ids = ctx.selection();
    if (ids.isEmpty())
        return;
    ctx.edit ([&] (Scene& s)
    {
        for (auto& id : ids)
            if (auto* p = findPoint (s, id))
                fn (*p);
    });
}

std::vector<ProSidebar::PresetEntry> ProSidebar::allPresets()
{
    std::vector<PresetEntry> out;
    for (auto& e : presets::all())
        out.push_back ({ e.id, e.name, e.description, e.user });
    return out;
}

void ProSidebar::applyPreset (const juce::String& id)
{
    if (! ctx.selection().isEmpty())
        presets::applyToSelection (ctx, id);
}

void ProSidebar::stepPreset (int delta)
{
    const auto presets = allPresets();
    if (presets.empty() || ctx.selection().isEmpty())
        return;
    int idx = -1;
    if (const auto* p = ctx.primaryPoint())
        for (size_t i = 0; i < presets.size(); ++i)
            if (p->creativePreset == presets[i].id)
                idx = (int) i;
    const int n = (int) presets.size();
    idx = idx < 0 ? (delta > 0 ? 0 : n - 1) : ((idx + delta) % n + n) % n;
    applyPreset (presets[(size_t) idx].id);
}

juce::String ProSidebar::saveSelectionAsPreset (const juce::String& name)
{
    if (ctx.primaryPoint() == nullptr)
        return {};
    return presets::saveFromSelection (ctx, name);
}

void ProSidebar::showPresetMenu()
{
    presets::buildMenu (ctx, *this).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (menuButton));
}

void ProSidebar::refresh()
{
    const auto* p = ctx.primaryPoint();
    const int count = ctx.selection().size();
    const bool has = p != nullptr;
    updating = true;
    if (has)
    {
        speed.setValue (p->shaper.rateMode == ShaperSettings::RateMode::Count ? p->shaper.cuts : (double) speed.getValue(), juce::dontSendNotification);
        depth.setValue (p->shaper.depthPercent, juce::dontSendNotification);
    }
    updating = false;
    for (auto* s : { &speed, &depth })
        s->setEnabled (has);
    prevButton.setEnabled (has);
    nextButton.setEnabled (has);

    presetTag = ! has ? juce::String() : p->id == kLivePointId ? u8 ("Mixer en vivo")
              : count > 1 ? juce::String (count) + u8 (" repeticiones") : u8 ("Repetici\xc3\xb3n ") + juce::String (p->number);
    presetName = u8 ("Sin preset");
    presetDesc = has ? u8 ("Ajustes propios de la repetici\xc3\xb3n") : u8 ("Selecciona una repetici\xc3\xb3n");
    if (has && p->creativePreset.isNotEmpty())
    {
        if (const auto* up = UserPresetStore::shared().find (p->creativePreset))
        {
            presetName = up->name;
            presetDesc = up->description;
        }
        else
            for (const auto& cp : pointPresets())
                if (p->creativePreset == cp.id)
                {
                    presetName = u8 (cp.name);
                    presetDesc = u8 (cp.description);
                }
    }
    const bool fav = has && UserPresetStore::shared().isFavourite (p->creativePreset);
    favButton.setButtonText (fav ? u8 ("\xe2\x98\x85") : u8 ("\xe2\x98\x86"));
    favButton.setColour (juce::TextButton::textColourOffId, fav ? theme::amber : theme::textDim);
    favButton.setEnabled (has && p->creativePreset.isNotEmpty());
    menuButton.setEnabled (has);

    juce::String note;
    if (has && p->shaper.custom)
        note = u8 ("Curva manual activa: Velocidad e Intensidad vuelven al generador");
    else if (has && p->shaper.rateMode != ShaperSettings::RateMode::Count)
        note = u8 ("Cortes por tiempo/divisi\xc3\xb3n: Velocidad pasa a n\xc3\xbamero de cortes");
    else if (has && ! p->shaper.enabled)
        note = u8 ("Volume Shaper apagado: mover un control lo activa");
    quickNote.setText (note, juce::dontSendNotification);
    repaint();
}

void ProSidebar::paint (juce::Graphics& g)
{
    theme::drawPanel (g, getLocalBounds().toFloat());
    // Preset card
    auto card = presetCard.toFloat();
    g.setColour (theme::panelHi);
    g.fillRoundedRectangle (card, 8.0f);
    g.setColour (theme::violet.withAlpha (0.7f));
    g.drawRoundedRectangle (card, 8.0f, 1.2f);
    auto inner = card.reduced (10.0f, 8.0f);
    g.setFont (theme::font (10.5f, true));
    g.setColour (theme::violet);
    g.drawText (presetTag.isEmpty() ? u8 ("Preset activo") : presetTag, inner.removeFromTop (14.0f), juce::Justification::centredLeft);
    inner.removeFromTop (2.0f);
    g.setFont (theme::font (17.0f, true));
    g.setColour (theme::text);
    g.drawText (presetName, inner.removeFromTop (22.0f).withTrimmedRight (34.0f), juce::Justification::centredLeft, true);
    g.setFont (theme::font (11.5f));
    g.setColour (theme::textDim);
    g.drawFittedText (presetDesc, inner.withTrimmedBottom (26.0f).toNearestInt(), juce::Justification::topLeft, 3);
}

void ProSidebar::resized()
{
    auto r = getLocalBounds().reduced (10, 10);
    presetCard = r.removeFromTop (108);
    {
        auto nav = presetCard.reduced (10, 8).removeFromBottom (24);
        prevButton.setBounds (nav.removeFromLeft (34));
        nextButton.setBounds (nav.removeFromRight (34));
        nav.reduce (6, 0);
        menuButton.setBounds (nav.removeFromRight (30));
        favButton.setBounds (nav.removeFromLeft (30));
    }
    r.removeFromTop (10);
    quickTitle.setBounds (r.removeFromTop (18));
    auto knobs = r.removeFromTop (92);
    const int w = knobs.getWidth() / 3;
    for (auto [s, l] : { std::pair { &speed, &speedL }, std::pair { &depth, &depthL }, std::pair { &mix, &mixL } })
    {
        auto k = knobs.removeFromLeft (w);
        l->setBounds (k.removeFromTop (16));
        s->setBounds (k);
    }
    quickNote.setBounds (r.removeFromTop (26));
    r.removeFromTop (2);
    advancedTitle.setBounds (r.removeFromTop (18));
    r.removeFromTop (4);
    const int bh = juce::jlimit (20, 30, r.getHeight() / juce::jmax (1, categoryButtons.size()) - 2);
    for (auto* b : categoryButtons)
    {
        b->setBounds (r.removeFromTop (bh));
        r.removeFromTop (2);
    }
}

} // namespace ses
