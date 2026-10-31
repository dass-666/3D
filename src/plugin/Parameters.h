#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace ses::params
{

// Parameter ids are part of saved projects and host automation:
// never rename or reuse them.
inline constexpr const char* mix = "mix";          // 0..100 %, wet amount (no wet signal yet in B0)
inline constexpr const char* output = "output";    // -24..+6 dB, applied now
inline constexpr const char* trigger = "trigger";  // on/off, starts repetitions (not processed yet in B0)

inline constexpr int version = 1;  // juce::ParameterID version hint for hosts

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

} // namespace ses::params
