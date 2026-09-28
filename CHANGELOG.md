# Changelog

## [0.2.0] - 2026-09-28

### Stage 2: plugin v1
- VST3 instrument and Standalone app (JUCE, universal macOS).
- Built-in factory space (60 synthetic instrument notes), trained at build time.
- Load your own `.pcsm` models (file chooser or drag and drop). A loaded
  model is embedded in the plugin state.
- Jump to any training sound; Centre returns to the mean.
- Parameters:
  - PC1–PC16, Components Used, Exaggerate, Morph Time
  - Play Mode, Loop Start/End, Scan Position, Speed
  - Attack, Release, Brightness, Harmonics, Velocity Sensitivity, Pitch Bend
    Range, Polyphony, Gain
- MIDI: velocity, pitch bend, sustain pedal.
- Engine: models are swapped into the audio thread without allocating;
  sustain pedal; model titles (`pcs-train --title`).
- CI: pluginval (strictness 10) on Linux and macOS, and a macOS zip plus
  installer.

## [0.1.0] - 2026-09-28

### Stage 1: engine and offline tools
- Harmonic analysis: per-sound f0 refinement, onset alignment, loudness
  normalisation, 64 harmonics × 100 frames/s in dB.
- PCA through the Gram matrix (Jacobi eigen-solver); coordinates in SD units.
- Model file format (`.pcsm`): training, projection of new sounds, decoding.
- Polyphonic additive synth: any pitch, pitch bend, one-shot / loop /
  ping-pong / scan modes, smoothed moves through the space, brightness tilt.
- Tools: `pcs-testgen` (synthetic training set), `pcs-train`, `pcs-render`,
  `pcs-examples` (listening examples and a map of the space).
