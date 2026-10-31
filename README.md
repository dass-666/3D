# Spatial Echo Studio

VST3 plugin (C++20, JUCE 8.0.15, CMake). Current state: B0 + B1 only.

- B0: VST3 effect that passes audio through. Parameters: Mix (stored, no wet signal yet), Output (applied, smoothed), Trigger (stored, not processed yet). Generic editor.
- B1: versioned scene model (`src/model`), voice presets in a separate format, JSON save/load, plugin state save/restore.

Not implemented yet: WAV loading, pitch engine, repetitions, spatial processing, Easy Mode editor.

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The VST3 is written to `build/SpatialEchoStudio_artefacts/<Config>/VST3/`.
