// UI smoke tests: the editor builds, paints and edits the same model in both
// modes. Set SES_SNAPSHOT_DIR to also write PNG screenshots.

#include "plugin/PluginProcessor.h"
#include "ui/PluginEditor.h"
#include "ui/SpatialMap.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace ses;

namespace
{
juce::MemoryBlock voiceLikeWav (double sr)
{
    // 1.2 s: three "syllables" with a moving pitch, so the waveform is not flat.
    const int n = (int) (1.2 * sr);
    juce::AudioBuffer<float> a (1, n);
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        const double env = std::pow (std::sin (juce::MathConstants<double>::pi * std::fmod (t, 0.4) / 0.4), 2.0) * std::exp (-t * 0.6);
        const double f = 180.0 + 60.0 * std::sin (t * 7.0);
        a.setSample (0, i, (float) (0.8 * env * (std::sin (2 * juce::MathConstants<double>::pi * f * t)
                                                 + 0.4 * std::sin (4 * juce::MathConstants<double>::pi * f * t))));
    }
    juce::MemoryBlock block;
    juce::WavAudioFormat wav;
    auto* stream = new juce::MemoryOutputStream (block, false);
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (stream, sr, 1, 24, {}, 0));
    w->writeFromAudioSampleBuffer (a, 0, n);
    w.reset();
    return block;
}

void snapshot (juce::Component& c, const char* name)
{
    const auto dir = juce::SystemStats::getEnvironmentVariable ("SES_SNAPSHOT_DIR", {});
    if (dir.isEmpty())
        return;
    const auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
    juce::File f (juce::File (dir).getChildFile (name));
    f.deleteFile();
    juce::FileOutputStream out (f);
    juce::PNGImageFormat().writeImageToStream (img, out);
}
} // namespace

TEST_CASE ("Editor: builds, paints, and map/panel/timeline share one model in both modes")
{
    juce::ScopedJuceInitialiser_GUI init;
    SpatialEchoProcessor p;
    p.prepareToPlay (48000.0, 512);

    {
        std::unique_ptr<juce::AudioProcessorEditor> empty (p.createEditor());
        snapshot (*empty, "ui-empty.png");
    }

    REQUIRE (p.loadSourceBytes (voiceLikeWav (48000.0), "C:/voces/campeon.wav").wasOk());
    p.editScene ([] (Scene& s) { s.source.trimStart = 9000; s.source.trimEnd = 40000; });

    std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor());
    auto* ed = dynamic_cast<SpatialEchoEditor*> (base.get());
    REQUIRE (ed != nullptr);
    auto& ctx = ed->getContext();

    const auto firstId = ctx.scene().points[0].id;
    ctx.select (firstId, false);
    ctx.select (ctx.scene().points[2].id, true);
    REQUIRE (ctx.selection().size() == 2);

    SpatialMap::duplicateSelection (ctx);
    CHECK (ctx.scene().points.size() == 6);
    CHECK (ctx.selection().size() == 2);  // the copies are selected
    SpatialMap::toggleMuteSelection (ctx);
    for (auto& id : ctx.selection())
        CHECK_FALSE (findPoint (ctx.scene(), id)->enabled);
    SpatialMap::deleteSelection (ctx);
    CHECK (ctx.scene().points.size() == 4);
    CHECK (ctx.selection().isEmpty());

    // Edits from the processor side (host restore) reach the editor's cached copy.
    p.editScene ([] (Scene& s)
    {
        s.points[1].timing.beat = 0.0;
        s.points[3].timing.beat = 2.0;
        s.points[0].solo = true;
        s.points[2].stutter.enabled = true;  // slices visible on the timeline
        s.points[2].stutter.repeats = 6;
        s.points[3].shaper = shaperPresets()[0].settings;  // Gate: curve visible on the timeline
    });
    ctx.refreshIfChanged();
    CHECK (ctx.scene().points[1].timing.beat == 0.0);

    ctx.select (ctx.scene().points[1].id, false);
    ed->setSize (1240, 840);
    snapshot (*ed, "ui-easy.png");

    // Pro mode: same state, more precision; switching back loses nothing.
    p.editScene ([] (Scene& s) { setTimeMode (s, TimeMode::Free, 120.0); });  // seconds axis in the Pro snapshot
    const auto before = p.getSceneForUi();
    p.setUiMode (UiMode::Pro);
    std::unique_ptr<juce::AudioProcessorEditor> pro (p.createEditor());
    dynamic_cast<SpatialEchoEditor*> (pro.get())->getContext().select (before.points[1].id, false);
    snapshot (*pro, "ui-pro.png");
    pro->setSize (1240, 1400);  // tall window: the whole point panel is visible
    snapshot (*pro, "ui-pro-tall.png");
    pro->setSize (1240, 840);
    CHECK (p.getSceneForUi() == before);

    juce::MemoryBlock state;
    p.getStateInformation (state);
    SpatialEchoProcessor q;
    q.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (q.getUiMode() == UiMode::Pro);
    CHECK (q.getSceneForUi() == before);

    ed->setSize (1080, 790);  // minimum size still lays out
    snapshot (*ed, "ui-min.png");
}

namespace
{
// Sends a press-drag-release gesture to a component, in its own coordinates.
void dragGesture (juce::Component& c, juce::Point<float> from, juce::Point<float> to, juce::ModifierKeys mods = {})
{
    auto source = juce::Desktop::getInstance().getMainMouseSource();
    const auto t0 = juce::Time::getCurrentTime();
    auto ev = [&] (juce::Point<float> pos, bool dragged, juce::ModifierKeys m)
    {
        return juce::MouseEvent (source, pos, m, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, t0, from, t0, 1, dragged);
    };
    const auto down = mods.withFlags (juce::ModifierKeys::leftButtonModifier);
    c.mouseDown (ev (from, false, down));
    for (int i = 1; i <= 5; ++i)
        c.mouseDrag (ev (from + (to - from) * ((float) i / 5.0f), true, down));
    c.mouseUp (ev (to, true, mods));
}
}

TEST_CASE ("Editor interactions: drag on the map pans, drag on the timeline moves in time, drag on the waveform trims")
{
    juce::ScopedJuceInitialiser_GUI init;
    SpatialEchoProcessor p;
    p.prepareToPlay (48000.0, 512);
    REQUIRE (p.loadSourceBytes (voiceLikeWav (48000.0), "C:/v.wav").wasOk());
    std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor());
    auto& ed = dynamic_cast<SpatialEchoEditor&> (*base);
    auto& ctx = ed.getContext();

    // Map: drag point 2 (x=82) to the far left.
    auto& map = ed.getMap();
    const auto id2 = ctx.scene().points[1].id;
    const auto from = map.screenPositionOf (ctx.scene().points[1].position);
    const auto to = map.screenPositionOf ({ 10.0, 49.0 });
    dragGesture (map, from, to);
    const auto* moved = findPoint (p.getSceneForUi(), id2);
    REQUIRE (moved != nullptr);
    CHECK (std::abs (moved->position.x - 10.0) < 1.0);
    CHECK (ctx.primary() == id2);
    const auto pl = placementFor (*moved, 48000.0);
    CHECK (pl.gainL > pl.gainR);  // the audio plan follows: now panned left

    // Lasso over the whole map selects every point.
    dragGesture (map, { 1.0f, 1.0f }, { (float) map.getWidth() - 1.0f, (float) map.getHeight() - 1.0f });
    CHECK (ctx.selection().size() == (int) ctx.scene().points.size());

    // Timeline: drag point 2 later by about 3 beats; Easy mode snaps to the 1/4 grid.
    ctx.select (id2, false);
    auto& tl = ed.getTimeline();
    tl.resized();
    const auto r = tl.boundsOfPoint (id2);
    REQUIRE (! r.isEmpty());
    const auto oneBeat = tl.boundsOfPoint (ctx.scene().points[0].id).getX() - r.getX();  // p1 at beat 1, p2 at beat 2 -> negative one beat
    dragGesture (tl, r.getCentre(), r.getCentre() + juce::Point<float> (-3.1f * oneBeat, 0.0f));
    const double beat = findPoint (p.getSceneForUi(), id2)->timing.beat;
    CHECK (beat == 5.0);

    // Waveform: drag a new selection; the fragment (trim) is stored in the scene.
    auto& wf = ed.getWaveform();
    const float x0 = wf.xForSample (12000), x1 = wf.xForSample (36000);
    dragGesture (wf, { x0, 30.0f }, { x1, 30.0f });
    const auto& src = p.getSceneForUi().source;
    CHECK (std::abs (src.trimStart - 12000) < 200);
    CHECK (std::abs (src.trimEnd - 36000) < 200);
}
