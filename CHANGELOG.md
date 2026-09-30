# Changelog

## [0.4.0] - 2026-09-30

### Stage 4: training inside the plugin
- **Train…** panel:
  - Add files or folders (or drop them on the window), remove, clear.
  - Settings: name, note (or Auto), duration, harmonics, floor,
    components, loudness matching, onset alignment.
  - Train in the background with progress and cancel.
- Failed files are skipped and flagged with the reason.
- Analysed sounds are cached, so edits to the set retrain quickly.
- The training list and settings are saved with the project.
- **Save Model…** writes the current space to a `.pcsm` file.
- Engine: automatic pitch detection (YIN, then spectral refinement), so a
  training set may mix notes. `pcs-train --note auto` works offline too.

## [0.3.0] - 2026-09-28

### Stage 3: custom UI
- **Sound map:** training sounds on any two components. Click a sound to jump
  to it; drag to move the point.
- **Morph pad:** four corner sounds, blended over all components.
- **Envelope view:** harmonics × time heat map of the current point, with
  loop/scan markers and voice playheads.
- **Component strip:** PC1–PC16 sliders with variance-explained bars.
- Grouped knobs for every other parameter; times shown in ms below 1 s.
- **Export WAV…** of the current point.
- Moves from the map and pad are recorded as automation gestures. Map axes
  and morph corners are saved with the state.

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
