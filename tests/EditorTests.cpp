// UI smoke tests: the editor builds, paints and edits the same model in both
// modes. Set SES_SNAPSHOT_DIR to also write PNG screenshots.

#include "plugin/PluginProcessor.h"
#include "ui/PluginEditor.h"
#include "model/UserPresets.h"
#include "ui/SpatialMap.h"
#include "ui/PresetActions.h"
#include "audio/EchoEngine.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

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
    pro->setSize (1240, 1700);  // tall window: the whole point panel is visible (shaped point: curve controls)
    dynamic_cast<SpatialEchoEditor*> (pro.get())->getContext().select (before.points[3].id, false);
    snapshot (*pro, "ui-pro-tall.png");
    dynamic_cast<SpatialEchoEditor*> (pro.get())->getContext().select (before.points[1].id, false);
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

TEST_CASE ("Curve editor: opens over the editor, points are added, dragged, removed, undone, A/B and copied")
{
    juce::ScopedJuceInitialiser_GUI init;
    SpatialEchoProcessor p;
    p.prepareToPlay (48000.0, 512);
    REQUIRE (p.loadSourceBytes (voiceLikeWav (48000.0), "C:/v.wav").wasOk());
    p.setUiMode (UiMode::Pro);
    std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor());
    auto& ed = dynamic_cast<SpatialEchoEditor&> (*base);
    ed.setSize (1240, 840);
    auto& ctx = ed.getContext();
    const auto id = ctx.scene().points[0].id;
    ctx.edit ([&] (Scene& s) { auto sh = shaperPresets()[0].settings; setShaper (s, id, sh); });  // Gate, 8 cuts
    ctx.select (id, false);

    auto& ce = ed.getCurveEditor();
    CHECK (ce.isVisible());      // Pro: embedded in the centre column
    CHECK (ce.isEmbedded());
    const int embeddedWidth = ce.getBounds().getWidth();
    ed.showCurveEditor (true);   // "Ampliar": full window
    CHECK (ce.isVisible());
    CHECK (! ce.isEmbedded());
    CHECK (ce.getBounds().getWidth() > 1000);
    CHECK (ce.getBounds().getWidth() > embeddedWidth);
    CHECK (ce.voiceSeconds() > 0.5);
    snapshot (ed, "ui-curve-generated.png");

    // Generated shape -> manual points (baked from the same curve).
    const auto* sh = ce.current();
    REQUIRE (sh != nullptr);
    CHECK (! sh->custom);
    ce.setManual (true);
    sh = ce.current();
    CHECK (sh->custom);
    const int baked = (int) sh->curve.size();
    CHECK (baked >= 8 * 4);
    CHECK (findPoint (p.getSceneForUi(), id)->shaper.custom);  // reached the processor's model

    // Double-click adds a point where the mouse is; the engine's gain there follows.
    const auto c = ce.canvasArea();
    const juce::Point<float> at (c.getX() + c.getWidth() * 0.37f, ce.yForGain (0.8));
    const auto fractionBefore = ce.fractionAt (at.x);
    juce::MouseEvent down (juce::Desktop::getInstance().getMainMouseSource(), at, {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &ce, &ce,
                           juce::Time::getCurrentTime(), at, juce::Time::getCurrentTime(), 2, false);
    ce.mouseDoubleClick (down);
    sh = ce.current();
    CHECK ((int) sh->curve.size() == baked + 1);
    {
        const double len = ce.voiceSeconds();
        const double t = fractionBefore * len;
        CHECK_THAT (shaperGain (*sh, t, len, 0.125), Catch::Matchers::WithinAbs (0.8, 0.05));
    }

    // Dragging the new point down to 0 at the same time.
    int idx = -1;
    for (int i = 0; i < (int) sh->curve.size(); ++i)
        if (std::abs (sh->curve[(size_t) i].gain - 0.8) < 1e-6)
            idx = i;
    REQUIRE (idx >= 0);
    ce.movePoint (idx, sh->curve[(size_t) idx].t, 0.0, true);
    sh = ce.current();
    CHECK (sh->curve[(size_t) idx].gain == 0.0);
    snapshot (ed, "ui-curve-manual.png");

    // Undo twice: back to the baked curve, then to the generated shape. Redo returns.
    CHECK (ce.canUndo());
    ce.undo();
    CHECK (std::abs (ce.current()->curve[(size_t) idx].gain - 0.8) < 1e-6);
    ce.undo();
    ce.undo();
    CHECK (! ce.current()->custom);
    ce.redo();
    CHECK (ce.current()->custom);
    CHECK (findPoint (p.getSceneForUi(), id)->shaper.custom);

    // A/B: store B, change, toggle back and forth.
    ce.storeB();
    const auto stateA = *ce.current();
    ce.mirrorCurve();
    CHECK (! (*ce.current() == stateA));
    ce.toggleAB();
    CHECK (*ce.current() == stateA);
    ce.toggleAB();
    CHECK (! (*ce.current() == stateA));

    // Copy to another point.
    ce.copyCurve();
    REQUIRE (CurveEditor::curveClipboard.has_value());
    const auto id2 = ctx.scene().points[1].id;
    ctx.select (id2, false);
    CHECK (! ce.current()->custom);
    ce.pasteCurve();
    CHECK (ce.current()->custom);
    CHECK (ce.current()->curve == *CurveEditor::curveClipboard);

    // Selection -> region; duplicate; remove keeps at least 2 points.
    ce.setSelection (std::pair { 0.25, 0.75 });
    ce.selectionToRegion();
    CHECK (! ce.current()->wholeVoice);
    CHECK_THAT (ce.current()->regionStart, Catch::Matchers::WithinAbs (0.25, 1e-9));
    const int n = (int) ce.current()->curve.size();
    ce.duplicateCurve();
    CHECK ((int) ce.current()->curve.size() == 2 * n);
    while (ce.numPoints() > 2)
        ce.removePoint (0);
    ce.removePoint (0);
    CHECK (ce.numPoints() == 2);

    // Zoom keeps the view inside the voice; closing restores the layout.
    ce.zoomBy (0.25, 0.5);
    CHECK (ce.fractionAt (ce.canvasArea().getX()) >= 0.0);
    CHECK (ce.fractionAt (ce.canvasArea().getRight()) <= 1.0);
    ce.fitView();
    ed.showCurveEditor (false);
    CHECK (ce.isVisible());
    CHECK (ce.isEmbedded());
    CHECK (ce.getBounds().getWidth() == embeddedWidth);

    // Everything the editor did survives a state round trip.
    juce::MemoryBlock state;
    p.getStateInformation (state);
    SpatialEchoProcessor q;
    q.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (q.getSceneForUi() == p.getSceneForUi());
}

TEST_CASE ("Pro layout: quick controls edit the generator, generate perfect cuts, sections, navigation, mode switch")
{
    juce::ScopedJuceInitialiser_GUI init;
    // User presets go to a temporary file in the tests, never to the real user folder.
    const auto presetFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ses-ui-presets.json");
    presetFile.deleteFile();
    UserPresetStore::overrideSharedFile (presetFile);
    SpatialEchoProcessor p;
    p.prepareToPlay (48000.0, 512);
    REQUIRE (p.loadSourceBytes (voiceLikeWav (48000.0), "C:/v.wav").wasOk());
    p.editScene ([] (Scene& s) { addPoint (s, { 50.0, 50.0 }); addPoint (s, { 30.0, 50.0 }); addPoint (s, { 70.0, 50.0 }); });
    p.setUiMode (UiMode::Pro);
    std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor());
    auto& ed = dynamic_cast<SpatialEchoEditor&> (*base);
    ed.setSize (1240, 840);
    auto& ctx = ed.getContext();
    auto& sb = ed.getSidebar();
    auto& ce = ed.getCurveEditor();
    const auto id0 = ctx.scene().points[0].id;
    ctx.select (id0, false);

    // Velocidad = cuts of the generator; Intensidad = depth; the shaper turns on.
    sb.velocidad().setValue (24.0, juce::sendNotificationSync);
    sb.intensidad().setValue (60.0, juce::sendNotificationSync);
    {
        const auto& sh = ctx.scene().points[0].shaper;
        CHECK (sh.enabled);
        CHECK (sh.cuts == 24);
        CHECK (sh.rateMode == ShaperSettings::RateMode::Count);
        CHECK_THAT (sh.depthPercent, Catch::Matchers::WithinAbs (60.0, 1e-9));
    }
    // Mezcla is the plugin's mix parameter.
    sb.mezcla().setValue (35.0, juce::sendNotificationSync);
    CHECK_THAT (p.getParameters().getRawParameterValue ("mix")->load(), Catch::Matchers::WithinAbs (35.0f, 0.01f));

    // Unrelated advanced settings survive a quick control move.
    ctx.edit ([&] (Scene& s) { auto* pt = findPoint (s, id0); pt->shaper.attackMs = 7.0; pt->effects.reverb.enabled = true; pt->shaper.pattern = "x.x"; });
    sb.velocidad().setValue (12.0, juce::sendNotificationSync);
    {
        const auto& pt = ctx.scene().points[0];
        CHECK (pt.shaper.cuts == 12);
        CHECK_THAT (pt.shaper.attackMs, Catch::Matchers::WithinAbs (7.0, 1e-9));
        CHECK (pt.shaper.pattern == "x.x");
        CHECK (pt.effects.reverb.enabled);
    }

    // A manual curve: Velocidad returns to the generator, the manual points stay stored.
    ce.setManual (true);
    REQUIRE (ctx.scene().points[0].shaper.custom);
    const auto manualPoints = ctx.scene().points[0].shaper.curve;
    REQUIRE (manualPoints.size() >= 2);
    sb.velocidad().setValue (16.0, juce::sendNotificationSync);
    CHECK (! ctx.scene().points[0].shaper.custom);
    CHECK (ctx.scene().points[0].shaper.curve == manualPoints);
    CHECK (ctx.scene().points[0].shaper.cuts == 16);

    // «Generar cortes perfectos» inside a selection: region = selection, generator on,
    // the count is limited to what the ramps allow, and what is drawn is shaperGain.
    ce.setManual (true);
    ce.setSelection (std::pair { 0.25, 0.5 });
    sb.velocidad().setValue (256.0, juce::sendNotificationSync);
    ce.setSelection (std::pair { 0.25, 0.5 });
    ce.generatePerfectCuts();
    {
        const auto& sh = ctx.scene().points[0].shaper;
        CHECK (sh.enabled);
        CHECK (! sh.custom);
        CHECK (! sh.wholeVoice);
        CHECK_THAT (sh.regionStart, Catch::Matchers::WithinAbs (0.25, 1e-9));
        CHECK_THAT (sh.regionEnd, Catch::Matchers::WithinAbs (0.5, 1e-9));
        const double span = (sh.regionEnd - sh.regionStart) * ce.voiceSeconds();
        CHECK (sh.cuts <= maxUsefulCuts (sh, span));
        CHECK (sh.cuts >= 2);
        // Outside the region the curve is 1; inside, the closed parts drop.
        const double len = ce.voiceSeconds();
        CHECK_THAT (shaperGain (sh, 0.1 * len, len, 0.125), Catch::Matchers::WithinAbs (1.0, 1e-6));
        float minInside = 1.0f;
        for (double t = 0.25 * len; t < 0.5 * len; t += 0.0005)
            minInside = juce::jmin (minInside, shaperGain (sh, t, len, 0.125));
        CHECK (minInside < 0.5f);
    }
    // Multiple selection: the quick control edits every selected repetition.
    ctx.select (ctx.scene().points[1].id, true);
    sb.intensidad().setValue (40.0, juce::sendNotificationSync);
    CHECK_THAT (ctx.scene().points[0].shaper.depthPercent, Catch::Matchers::WithinAbs (40.0, 1e-9));
    CHECK_THAT (ctx.scene().points[1].shaper.depthPercent, Catch::Matchers::WithinAbs (40.0, 1e-9));
    CHECK_THAT (ctx.scene().points[2].shaper.depthPercent, Catch::Matchers::WithinAbs (100.0, 1e-9));

    // Creative preset prev/next on the selection, recorded for the preset card and saved.
    ctx.select (id0, false);
    sb.stepPreset (+1);
    CHECK (ctx.scene().points[0].creativePreset == pointPresets()[0].id);
    sb.stepPreset (-1);
    const auto lastId = ctx.scene().points[0].creativePreset;
    CHECK ((lastId == pointPresets().back().id || UserPresetStore::shared().find (lastId) != nullptr));

    // Save the repetition as a user preset: it joins the cycle, applies to another point, and is a favourite.
    sb.stepPreset (+1);
    ctx.edit ([&] (Scene& s) { findPoint (s, id0)->shaper.cuts = 77; });
    const auto userId = sb.saveSelectionAsPreset ("Mi preset");
    REQUIRE (userId.startsWith ("user."));
    CHECK (ctx.scene().points[0].creativePreset == userId);
    CHECK (UserPresetStore::shared().find (userId) != nullptr);
    ctx.select (ctx.scene().points[2].id, false);
    sb.applyPreset (userId);
    CHECK (ctx.scene().points[2].shaper.cuts == 77);
    CHECK (ctx.scene().points[2].creativePreset == userId);
    UserPresetStore::shared().setFavourite (userId, true);
    CHECK (UserPresetStore::shared().isFavourite (userId));
    CHECK (UserPresetStore::shared().remove (userId));
    ctx.select (id0, false);

    // Sections: a category opens its section and shows its controls; the centre tabs switch views.
    ed.getPointPanel().showSection ("reverb");
    sb.onCategory ("space");
    CHECK (ed.getCenterView() == SpatialEchoEditor::CenterView::Map);
    CHECK (ed.getMap().isVisible());
    CHECK (! ce.isVisible());
    sb.onCategory ("shaper");
    CHECK (ed.getCenterView() == SpatialEchoEditor::CenterView::Cuts);
    CHECK (ce.isVisible());
    ed.setCenterView (SpatialEchoEditor::CenterView::Audio);
    CHECK (ed.getWaveform().isVisible());
    CHECK (! ce.isVisible());
    ed.setCenterView (SpatialEchoEditor::CenterView::Cuts);

    // Switching modes changes nothing in the scene; the state round-trips with the preset id.
    const auto before = p.getSceneForUi();
    ed.setSize (1080, 790);
    ed.setSize (1240, 840);
    juce::MemoryBlock state;
    p.getStateInformation (state);
    SpatialEchoProcessor q;
    q.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (q.getSceneForUi() == before);
    CHECK (q.getSceneForUi().points[0].creativePreset == before.points[0].creativePreset);
    CHECK (p.getSceneForUi() == before);

    // Interface size: saved with the project, applied as the editor's scale factor.
    ed.setUiScale (125);
    CHECK (p.getUiScalePercent() == 125);
    CHECK_THAT (ed.getTransform().getScaleFactor(), Catch::Matchers::WithinAbs (1.25f, 1e-5f));
    p.getStateInformation (state);
    SpatialEchoProcessor q2;
    q2.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (q2.getUiScalePercent() == 125);
    ed.setUiScale (100);
    snapshot (ed, "ui-pro-cuts.png");
}

TEST_CASE ("Document undo/redo: steps, coalesced drags, redo cleared by a new edit, external changes clear history, source kept")
{
    juce::ScopedJuceInitialiser_GUI init;
    SpatialEchoProcessor p;
    p.prepareToPlay (48000.0, 512);
    REQUIRE (p.loadSourceBytes (voiceLikeWav (48000.0), "C:/v.wav").wasOk());
    EditorContext ctx (p);
    CHECK (! ctx.canUndo());
    const auto s0 = ctx.scene();
    const auto id = s0.points[0].id;

    ctx.edit ([&] (Scene& s) { setGainDb (s, id, -6.0); });
    CHECK (ctx.canUndo());
    ctx.breakUndoGroup();
    ctx.edit ([&] (Scene& s) { movePoint (s, id, { 10.0, 10.0 }); });
    CHECK_THAT (ctx.scene().points[0].gainDb, Catch::Matchers::WithinAbs (-6.0, 1e-9));
    ctx.undo();
    CHECK (ctx.scene().points[0].position == s0.points[0].position);
    CHECK_THAT (ctx.scene().points[0].gainDb, Catch::Matchers::WithinAbs (-6.0, 1e-9));
    CHECK (ctx.canRedo());
    ctx.undo();
    CHECK (ctx.scene() == s0);
    CHECK (! ctx.canUndo());
    ctx.redo();
    CHECK_THAT (ctx.scene().points[0].gainDb, Catch::Matchers::WithinAbs (-6.0, 1e-9));
    ctx.redo();
    CHECK (ctx.scene().points[0].position == Position { 10.0, 10.0 });
    CHECK (! ctx.canRedo());

    // A drag (edits between beginGesture and endGesture) is one step.
    ctx.breakUndoGroup();
    ctx.beginGesture();
    for (int i = 1; i <= 20; ++i)
        ctx.edit ([&, i] (Scene& s) { setGainDb (s, id, -6.0 - i * 0.5); });
    ctx.endGesture();
    CHECK_THAT (ctx.scene().points[0].gainDb, Catch::Matchers::WithinAbs (-16.0, 1e-9));
    ctx.undo();
    CHECK_THAT (ctx.scene().points[0].gainDb, Catch::Matchers::WithinAbs (-6.0, 1e-9));

    // A new edit after undo discards the redo branch.
    ctx.breakUndoGroup();
    ctx.edit ([&] (Scene& s) { addPoint (s, { 50.0, 20.0 }); });
    CHECK (! ctx.canRedo());

    // The host restoring state (or a WAV load) is not undoable and clears the history.
    juce::MemoryBlock state;
    p.getStateInformation (state);
    p.setStateInformation (state.getData(), (int) state.getSize());
    ctx.refreshIfChanged();
    CHECK (! ctx.canUndo());

    // Undo never touches the source audio reference.
    ctx.breakUndoGroup();
    ctx.edit ([&] (Scene& s) { setGainDb (s, id, 3.0); });
    const auto src = ctx.scene().source;
    ctx.undo();
    CHECK (ctx.scene().source == src);
    CHECK (p.getSourceStatus().loaded);
}

TEST_CASE ("Action bar: presets are saved, applied and deleted from both modes, without a selection too")
{
    juce::ScopedJuceInitialiser_GUI init;
    const auto presetFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ses-actionbar-presets.json");
    presetFile.deleteFile();
    UserPresetStore::overrideSharedFile (presetFile);
    auto& store = UserPresetStore::shared();
    for (const auto& up : std::vector<UserPointPreset> (store.presets()))
        store.remove (up.id);

    SpatialEchoProcessor p;
    p.prepareToPlay (48000.0, 512);
    REQUIRE (p.loadSourceBytes (voiceLikeWav (48000.0), "C:/v.wav").wasOk());
    p.editScene ([] (Scene& s) { s.points.clear(); addPoint (s, { 40.0, 50.0 }); addPoint (s, { 60.0, 50.0 }); });
    for (auto mode : { UiMode::Easy, UiMode::Pro })
    {
        p.setUiMode (mode);
        std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor());
        auto& ed = dynamic_cast<SpatialEchoEditor&> (*base);
        ed.setSize (1080, 790);  // minimum size: every action stays visible
        auto& bar = ed.getActionBar();
        for (auto* b : { &bar.importButton, &bar.liveButton, &bar.exportButton, &bar.presetsButton, &bar.savePresetButton })
        {
            CHECK (b->isVisible());
            CHECK (bar.isVisible());
            CHECK (b->getBottom() <= bar.getHeight());
            CHECK (b->getRight() <= bar.getWidth());
            CHECK (b->getWidth() >= 110);
        }
        snapshot (ed, mode == UiMode::Pro ? "b9-actionbar-pro.png" : "b9-actionbar-easy.png");
    }

    std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor());
    auto& ed = dynamic_cast<SpatialEchoEditor&> (*base);
    auto& ctx = ed.getContext();
    ctx.clearSelection();

    // Without a selection the first repetition is the source of the preset.
    ctx.edit ([] (Scene& s) { s.points[0].effects.reverb.enabled = true; s.points[0].effects.reverb.decayPercent = 77.0; s.points[0].shaper.enabled = true; s.points[0].shaper.cuts = 48; });
    const auto id = presets::saveFromSelection (ctx, "Mi lluvia");
    REQUIRE (id.isNotEmpty());
    REQUIRE (store.find (id) != nullptr);
    CHECK (store.find (id)->name == "Mi lluvia");
    CHECK (ctx.scene().points[0].creativePreset == id);
    CHECK (presetFile.existsAsFile());  // written at once, survives the project

    // Listed under «Mis presets» and applied to the selected repetition only.
    bool listed = false;
    for (auto& e : presets::all())
        listed |= e.user && e.id == id;
    CHECK (listed);
    ctx.select (ctx.scene().points[1].id, false);
    REQUIRE (presets::applyToSelection (ctx, id));
    CHECK (ctx.scene().points[1].effects.reverb.enabled);
    CHECK (ctx.scene().points[1].shaper.cuts == 48);
    CHECK (ctx.scene().points[1].position.x == 60.0);  // position is never part of a preset
    CHECK (presets::buildMenu (ctx, ed).getNumItems() > 4);

    // A second store reading the same file sees it (another project / plugin instance).
    UserPresetStore other (presetFile);
    REQUIRE (other.load().wasOk());
    REQUIRE (other.find (id) != nullptr);

    CHECK (store.remove (id));
    CHECK (store.find (id) == nullptr);
    UserPresetStore after (presetFile);
    REQUIRE (after.load().wasOk());
    CHECK (after.find (id) == nullptr);
    presetFile.deleteFile();
}

TEST_CASE ("Export panel: options, background render, draggable result")
{
    juce::ScopedJuceInitialiser_GUI init;
    SpatialEchoProcessor p;
    p.prepareToPlay (48000.0, 512);
    REQUIRE (p.loadSourceBytes (voiceLikeWav (48000.0), "C:/v.wav").wasOk());
    p.editScene ([] (Scene& s) { s.points.clear(); addPoint (s, { 40.0, 50.0 }, Timing { 0.0, 0.0 }); addPoint (s, { 60.0, 50.0 }, Timing { 1.0, 0.0 }); });
    std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor());
    auto& ed = dynamic_cast<SpatialEchoEditor&> (*base);
    ed.setSize (1240, 840);
    auto& bar = ed.getActionBar();
    auto& panel = ed.getExportPanel();
    CHECK_FALSE (panel.isVisible());
    bar.exportButton.onClick();
    REQUIRE (panel.isVisible());
    CHECK (panel.exportButton.isEnabled());

    // 32-bit float at 44.1 kHz, only the selected repetition.
    ed.getContext().select (ed.getContext().scene().points[1].id, false);
    panel.formatBox.setSelectedId (32);
    panel.rateBox.setSelectedId (44100);
    panel.contentBox.setSelectedId (2);
    auto rq = panel.currentRequest();
    CHECK (rq.format == WavFormat::Float32);
    CHECK (rq.sampleRate == 44100.0);
    CHECK (rq.onlyPointId == ed.getContext().scene().points[1].id);
    CHECK (rq.file.getParentDirectory() == SpatialEchoProcessor::exportFolder());

    panel.contentBox.setSelectedId (1);
    panel.startExport();
    for (int i = 0; i < 500 && p.getExportStatus().state == SpatialEchoProcessor::ExportStatus::State::Running; ++i)
        juce::Thread::sleep (10);
    panel.open();  // refreshes from the status (the panel's timer does this in the plugin)
    const auto st = p.getExportStatus();
    REQUIRE (st.state == SpatialEchoProcessor::ExportStatus::State::Done);
    CHECK (panel.lastFile() == st.file);
    CHECK (panel.saveAsButton.isEnabled());
    snapshot (ed, "b9-export.png");
    st.file.deleteFile();
}

TEST_CASE ("Live mode UI: the action bar turns it on and the panel edits the live chain in both modes")
{
    juce::ScopedJuceInitialiser_GUI init;
    SpatialEchoProcessor p;
    p.prepareToPlay (48000.0, 512);
    p.editScene ([] (Scene& s) { s.points.clear(); addPoint (s, { 40.0, 50.0 }); });
    for (auto mode : { UiMode::Easy, UiMode::Pro })
    {
        p.setUiMode (mode);
        p.editScene ([] (Scene& s) { s.live.enabled = false; });
        std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor());
        auto& ed = dynamic_cast<SpatialEchoEditor&> (*base);
        ed.setSize (1240, 840);
        auto& ctx = ed.getContext();
        auto& bar = ed.getActionBar();
        CHECK_FALSE (bar.liveSettingsButton.isVisible());
        bar.liveButton.onClick();
        CHECK (ctx.scene().live.enabled);
        CHECK (ctx.primary() == kLivePointId);
        CHECK (bar.liveSettingsButton.isVisible());
        CHECK (bar.liveButton.getToggleState());

        // The panel edits the live point through the usual controls (same model as the repetitions).
        ctx.edit ([] (Scene& s) { if (auto* lp = findPoint (s, kLivePointId)) lp->effects.reverb.enabled = true; });
        CHECK (p.getSceneForUi().live.fx.effects.reverb.enabled);
        CHECK (ctx.scene().points.size() == 1);  // never added as a repetition
        snapshot (ed, mode == UiMode::Pro ? "b9-live-pro.png" : "b9-live-easy.png");

        // A repetition can be selected and edited while live stays on; «Ajustes» returns to it.
        ctx.select (ctx.scene().points[0].id, false);
        CHECK (ctx.scene().live.enabled);
        bar.liveSettingsButton.onClick();
        CHECK (ctx.primary() == kLivePointId);

        bar.liveButton.onClick();
        CHECK_FALSE (ctx.scene().live.enabled);
        CHECK (ctx.primary().isEmpty());
        CHECK_FALSE (bar.liveSettingsButton.isVisible());
    }
}
