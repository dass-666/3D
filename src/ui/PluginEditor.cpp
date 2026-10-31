#include "ui/PluginEditor.h"

namespace ses
{

SpatialEchoEditor::SpatialEchoEditor (SpatialEchoProcessor& p)
    : AudioProcessorEditor (p), processor (p)
{
    loadButton.onClick = [this] { chooseFile(); };
    previewButton.onClick = [this] { processor.requestPreview(); };
    triggerButton.onClick = [this] { processor.requestTrigger(); };

    for (auto* s : { &mixSlider, &outputSlider })
    {
        s->setSliderStyle (juce::Slider::LinearHorizontal);
        s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 22);
        addAndMakeVisible (*s);
    }
    mixLabel.setText ("Mix (nivel de la repeticion)", juce::dontSendNotification);
    outputLabel.setText ("Salida", juce::dontSendNotification);
    noteLabel.setText ("El audio original pasa sin cambios. Estereo: la posicion espacial llega en B3.",
                       juce::dontSendNotification);
    noteLabel.setFont (juce::FontOptions (12.0f));
    noteLabel.setColour (juce::Label::textColourId, juce::Colours::grey);

    for (juce::Component* c : { (juce::Component*) &loadButton, (juce::Component*) &previewButton,
                                (juce::Component*) &triggerButton, (juce::Component*) &statusLabel,
                                (juce::Component*) &noteLabel, (juce::Component*) &mixLabel,
                                (juce::Component*) &outputLabel })
        addAndMakeVisible (c);

    auto& apvts = processor.getParameters();
    mixAttachment = std::make_unique<SliderAttachment> (apvts, params::mix, mixSlider);
    outputAttachment = std::make_unique<SliderAttachment> (apvts, params::output, outputSlider);

    setSize (480, 240);
    refreshStatus();
    startTimerHz (4);
}

SpatialEchoEditor::~SpatialEchoEditor()
{
    stopTimer();
}

void SpatialEchoEditor::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void SpatialEchoEditor::resized()
{
    auto area = getLocalBounds().reduced (12);
    auto row = area.removeFromTop (30);
    loadButton.setBounds (row.removeFromLeft (130));
    row.removeFromLeft (8);
    previewButton.setBounds (row.removeFromLeft (110));
    row.removeFromLeft (8);
    triggerButton.setBounds (row.removeFromLeft (100));

    area.removeFromTop (8);
    statusLabel.setBounds (area.removeFromTop (40));
    area.removeFromTop (6);
    mixLabel.setBounds (area.removeFromTop (18));
    mixSlider.setBounds (area.removeFromTop (28));
    outputLabel.setBounds (area.removeFromTop (18));
    outputSlider.setBounds (area.removeFromTop (28));
    noteLabel.setBounds (area.removeFromTop (24));
}

void SpatialEchoEditor::timerCallback()
{
    refreshStatus();
}

void SpatialEchoEditor::refreshStatus()
{
    const auto st = processor.getSourceStatus();
    juce::String text;
    if (st.loaded)
        text << st.fileName << "  |  " << juce::String (st.seconds, 2) << " s, "
             << juce::String (st.sampleRate / 1000.0, 1) << " kHz, " << (st.numChannels == 1 ? "mono" : "estereo")
             << (st.embedded ? "  |  guardado en el proyecto" : "  |  referencia al archivo");
    else
        text << "Sin audio cargado.";
    if (st.message.isNotEmpty())
        text << "\n" << st.message;
    statusLabel.setText (text, juce::dontSendNotification);
    previewButton.setEnabled (st.loaded);
}

void SpatialEchoEditor::chooseFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Elegir un archivo WAV", juce::File(), "*.wav");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe = juce::Component::SafePointer<SpatialEchoEditor> (this)] (const juce::FileChooser& fc)
                          {
                              if (safe == nullptr)
                                  return;
                              const auto file = fc.getResult();
                              if (file.existsAsFile())
                                  safe->processor.loadSourceFile (file);  // errors appear in the status line
                              safe->refreshStatus();
                          });
}

} // namespace ses
