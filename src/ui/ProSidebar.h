#pragma once

// Left column of the Pro layout: the active creative preset of the selected
// repetition (with prev/next), the three quick controls (Velocidad,
// Intensidad, Mezcla) and the list of parameter categories that scroll the
// parameter panel. Every control edits the real model: Velocidad = number of
// cuts of the Volume Shaper, Intensidad = depth of the cuts, Mezcla = the
// plugin's "mix" parameter. Quick controls act on the cut generator; if the
// repetition has a manual curve they return to the generator (the manual
// points stay stored and "Manual" in the editor brings them back).

#include "ui/EditorContext.h"

namespace ses
{

class ProSidebar final : public juce::Component, private EditorContext::Listener
{
public:
    explicit ProSidebar (EditorContext&);
    ~ProSidebar() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void (const juce::String& categoryId)> onCategory;
    void setActiveCategory (const juce::String& id);

    static constexpr const char* kCategories[] { "shaper", "stutter", "pitch", "filters", "chain", "delay", "reverb",
                                                 "modulation", "character", "space", "time" };

    // Public for the tests.
    juce::Slider& velocidad() noexcept { return speed; }
    juce::Slider& intensidad() noexcept { return depth; }
    juce::Slider& mezcla() noexcept { return mix; }
    void stepPreset (int delta);  // prev / next creative preset on the selection

private:
    void sceneChanged() override { refresh(); }
    void selectionChanged() override { refresh(); }
    void refresh();
    void editSelection (const std::function<void (Point&)>&);

    EditorContext& ctx;
    juce::Rectangle<int> presetCard;
    juce::String presetName, presetDesc, presetTag;
    juce::TextButton prevButton { juce::String::fromUTF8 ("\xe2\x97\x80") }, nextButton { juce::String::fromUTF8 ("\xe2\x96\xb6") };
    juce::Slider speed, depth, mix;
    juce::Label speedL, depthL, mixL, quickTitle, advancedTitle, quickNote;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment;
    juce::OwnedArray<juce::TextButton> categoryButtons;
    juce::String activeCategory = "shaper";
    bool updating = false;
};

} // namespace ses
