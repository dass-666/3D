#pragma once

// Decoding and preparing the source fragment. Message thread only.

#include <juce_audio_formats/juce_audio_formats.h>

#include <memory>

namespace ses
{

inline constexpr double kMaxSourceSeconds = 30.0;
inline constexpr juce::int64 kMaxEmbeddedBytes = 16 * 1024 * 1024;

struct DecodedAudio
{
    juce::AudioBuffer<float> buffer;  // 1 or 2 channels
    double sampleRate = 0.0;
};

struct DecodeResult
{
    std::shared_ptr<const DecodedAudio> audio;  // null on failure
    juce::String error;
};

// Accepts WAV only (mono or stereo, at most kMaxSourceSeconds).
DecodeResult decodeWav (const juce::MemoryBlock& fileBytes);

juce::String sha256Hex (const juce::MemoryBlock&);

// 24-bit PCM WAV bytes (for embedding captured audio in the project).
juce::MemoryBlock encodeWav (const juce::AudioBuffer<float>&, double sampleRate);

// Band-limited resampling is not needed for B2's purpose (same-content
// playback); Lagrange interpolation is used. Returns a copy when rates match.
juce::AudioBuffer<float> resampled (const DecodedAudio&, double targetSampleRate);

} // namespace ses
