#include "ui/PointPanel.h"

#include "model/VoicePresets.h"
#include "ui/SpatialMap.h"
#include "ui/Theme.h"

namespace ses
{
namespace
{
const juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }
}

PointPanel::PointPanel (EditorContext& c) : ctx (c)
{
    ctx.addListener (this);

    title.setFont (theme::font (17.0f, true));
    addAndMakeVisible (title);

    presetL.setText ("Preset vocal", juce::dontSendNotification);
    presetL.setFont (theme::font (12.5f));
    presetL.setColour (juce::Label::textColourId, theme::textDim);
    addAndMakeVisible (presetL);
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
    addAndMakeVisible (presetBox);

    presetNote.setFont (theme::font (11.0f));
    presetNote.setColour (juce::Label::textColourId, theme::amber);
    addAndMakeVisible (presetNote);

    setupSlider (volume, volumeL, "Volumen", 0, 100, 1, " %");
    setupSlider (posX, posXL, u8 ("Izq \xe2\x86\x94 Der"), 0, 100, 0.1, "");
    setupSlider (posY, posYL, u8 ("Delante \xe2\x86\x94 Detr\xc3\xa1s"), 0, 100, 0.1, "");
    setupSlider (beat, beatL, "Momento", 0, 32, 0.25, " t");

    volume.onValueChange = [this] { editPrimary ([v = volume.getValue()] (Scene& s, Point& p) { setVolume (s, p.id, v); }); };
    posX.onValueChange = [this] { editPrimary ([v = posX.getValue()] (Scene& s, Point& p) { movePoint (s, p.id, { v, p.position.y }); }); };
    posY.onValueChange = [this] { editPrimary ([v = posY.getValue()] (Scene& s, Point& p) { movePoint (s, p.id, { p.position.x, v }); }); };
    beat.onValueChange = [this] { editPrimary ([v = beat.getValue()] (Scene& s, Point& p) { setTiming (s, p.id, { v }); }); };

    // B4: pitch / character / speed processing does not exist yet, so they are
    // shown as stored values only, never as working controls.
    soundInfo.setFont (theme::font (12.5f, true));
    soundInfo.setColour (juce::Label::textColourId, theme::textDim);
    addAndMakeVisible (soundInfo);
    pendingNote.setText (u8 ("Pitch, car\xc3\xa1" "cter y velocidad: PR\xc3\x93XIMAMENTE (B4). Se guardan; a\xc3\xban no suenan."),
                         juce::dontSendNotification);
    pendingNote.setFont (theme::font (11.5f));
    pendingNote.setColour (juce::Label::textColourId, theme::textFaint);
    pendingNote.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (pendingNote);

    enabledToggle.onClick = [this] { editPrimary ([on = enabledToggle.getToggleState()] (Scene&, Point& p) { p.enabled = on; }); };
    soloToggle.onClick = [this] { editPrimary ([on = soloToggle.getToggleState()] (Scene&, Point& p) { p.solo = on; }); };
    duplicateButton.onClick = [this] { SpatialMap::duplicateSelection (ctx); };
    deleteButton.onClick = [this] { SpatialMap::deleteSelection (ctx); };
    deleteButton.setColour (juce::TextButton::buttonColourId, theme::danger.withAlpha (0.25f));
    for (juce::Component* comp : { (juce::Component*) &enabledToggle, (juce::Component*) &soloToggle,
                                   (juce::Component*) &duplicateButton, (juce::Component*) &deleteButton })
        addAndMakeVisible (comp);

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
    addAndMakeVisible (s);
    l.setText (name, juce::dontSendNotification);
    l.setFont (theme::font (12.5f));
    l.setColour (juce::Label::textColourId, theme::textDim);
    addAndMakeVisible (l);
}

void PointPanel::setProMode (bool shouldBePro)
{
    pro = shouldBePro;
    const double grid = beatsPerStep (ctx.scene().rhythm);
    beat.setRange (0.0, 32.0, pro ? 0.001 : grid);
    posX.setNumDecimalPlacesToDisplay (pro ? 1 : 0);
    posY.setNumDecimalPlacesToDisplay (pro ? 1 : 0);
    beat.setNumDecimalPlacesToDisplay (pro ? 3 : 2);
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

void PointPanel::refresh()
{
    const auto* p = ctx.primaryPoint();
    const bool has = p != nullptr;
    for (juce::Component* c : getChildren())
        c->setVisible (has);
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

    juce::String note = u8 ("El sonido de los presets llega en B4; hoy se guarda la elecci\xc3\xb3n.");
    if (count > 1)
        note = u8 ("El preset se aplica a las ") + juce::String (count) + u8 (" seleccionadas. ") + note;
    if (p->voice.modified)
        note = u8 ("(modificado) ") + note;
    presetNote.setText (note, juce::dontSendNotification);

    auto setIfIdle = [] (juce::Slider& s, double v)
    {
        if (! s.isMouseButtonDown())
            s.setValue (v, juce::dontSendNotification);
    };
    setIfIdle (volume, p->volumePercent);
    setIfIdle (posX, p->position.x);
    setIfIdle (posY, p->position.y);
    setIfIdle (beat, p->timing.beat);
    soundInfo.setText ("Pitch " + juce::String (p->sound.pitchSemitones > 0 ? "+" : "") + juce::String (p->sound.pitchSemitones, 0)
                           + u8 (" st  \xc2\xb7  Car\xc3\xa1" "cter ") + juce::String (p->sound.character, 0)
                           + u8 ("  \xc2\xb7  Velocidad ") + juce::String (p->sound.speedPercent, 0) + " %",
                       juce::dontSendNotification);
    enabledToggle.setToggleState (p->enabled, juce::dontSendNotification);
    soloToggle.setToggleState (p->solo, juce::dontSendNotification);
    repaint();
}

void PointPanel::paint (juce::Graphics& g)
{
    theme::drawPanel (g, getLocalBounds().toFloat(), u8 ("REPETICI\xc3\x93N SELECCIONADA"));
    if (const auto* p = ctx.primaryPoint())
    {
        const auto t = title.getBounds().toFloat();
        g.setColour (theme::pointColour (p->number));
        g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ t.getX() - 10.0f, t.getCentreY() }));
    }
}

void PointPanel::resized()
{
    auto r = getLocalBounds().reduced (16, 10);
    r.removeFromTop (22);
    emptyNote.setBounds (r);

    title.setBounds (r.removeFromTop (26).withTrimmedLeft (18));
    r.removeFromTop (2);

    auto row = [&] (int h) { auto x = r.removeFromTop (h); r.removeFromTop (3); return x; };
    auto labelled = [&] (juce::Label& l, juce::Component& c)
    {
        auto x = row (27);
        l.setBounds (x.removeFromLeft (112));
        c.setBounds (x);
    };

    labelled (presetL, presetBox);
    presetNote.setBounds (row (26).withTrimmedLeft (112));
    labelled (volumeL, volume);
    labelled (posXL, posX);
    labelled (posYL, posY);
    labelled (beatL, beat);

    auto actions = row (30);
    const int w = actions.getWidth() / 4;
    enabledToggle.setBounds (actions.removeFromLeft (w));
    soloToggle.setBounds (actions.removeFromLeft (w - 6));
    duplicateButton.setBounds (actions.removeFromLeft (w).reduced (2, 0));
    deleteButton.setBounds (actions.reduced (2, 0));

    r.removeFromTop (4);
    soundInfo.setBounds (row (20));
    pendingNote.setBounds (row (32));
}

} // namespace ses
