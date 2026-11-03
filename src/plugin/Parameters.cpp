#include "plugin/Parameters.h"

namespace ses::params
{

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { mix, version }, "Mix",
        NormalisableRange<float> (0.0f, 100.0f, 0.1f), 70.0f,
        AudioParameterFloatAttributes().withLabel ("%")));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { output, version }, "Output",
        NormalisableRange<float> (-24.0f, 6.0f, 0.01f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB")));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { trigger, version }, "Trigger", false));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { bypass, version }, "Bypass", false));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { protect, version }, "Protection", true));

    return layout;
}

} // namespace ses::params
