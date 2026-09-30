# Changelog

## [0.7.0] - 2026-09-30

### Stage 7: richer model
- **Residual noise:** breath, bow and hammer noise measured in 16 bands
  between the partials, resynthesised with calibrated band-pass filters per
  voice. New **Noise** knob.
- **Partial tuning:** stretched, inharmonic partials are tracked, learned and
  played at their frequencies.
- **Timbre follows pitch:** train on several notes per instrument and each
  note gets its register's timbre. Pitch tracking is Auto, On or Off; new
  **Keytrack** knob.
- **Representations:** decibels, shape + loudness, or linear.
- The Train panel has the new options. The envelope view shows noise. The
  model summary lists what a space contains.
- Model file format 2; version 1 files still load.
- `pcs-train --noise-bands --no-partials --representation --pitch-tracking`,
  `pcs-render --noise --keytrack`. Listening examples 15–18.

### Fixed
- A model swapped in could keep the previous model's partial tuning.
- Noise gains could be corrupted during loop crossfades.

## [0.6.0] - 2026-09-30

### Stage 6: MPE (for the Osmose)
- Per-note pitch bend (Note Bend Range, default 48 semitones); the master
  channel bends everything. Lower or upper zone.
- Per-note pressure (channel pressure or poly aftertouch) and slide (CC74),
  each routed to a component or toward a chosen sound. Pressure has a curve
  and smoothing; slide is bipolar or unipolar.
- Values sent before a note-on apply to that note. Released notes keep
  their expression when their channel is reused.
- Declares MPE support to the host. Reads the controller's MPE
  Configuration Message and bend-range RPN.
- MPE tab with a live per-note monitor. The envelope view follows the most
  recent note.
- With MPE off, MIDI behaves exactly as before.

### Fixed
- **Neighbour Tour now goes to sounds that really are similar.** It measured
  distance in z units, where every component counts equally. With all
  components kept, whitening makes the training sounds exactly
  equidistant (a regular simplex), so "the three nearest" were ties, broken
  by rounding (differently on Apple Silicon, which failed the macOS CI).
  Distances are now measured in the model's own units (each component
  weighted by its SD), so the main components decide what is near.
- Random-walk step timing is snapped to whole steps so it can't slip a tick
  through rounding.

## [0.5.0] - 2026-09-30

### Stage 5: movement and expression
- **Random walk** through the space. Modes: Drift (with Tether), Jumps (with
  Glide), Tour (between training sounds) and Neighbour Tour (to similar
  sounds).
  - Amount, and Rate or tempo Sync.
  - Components and Focus (equal, or by variance).
  - Per Voice (every note wanders on its own).
  - Seed, Restart on note (repeatable paths), Freeze.
- **Two LFOs** (six shapes, Hz or synced), targeting any component or the
  direction towards a chosen sound.
- **Velocity, mod wheel and aftertouch** routing to a component or towards
  the chosen sound. The **Macro** knob moves towards it too.
- **Voice Spread:** each note starts at its own offset.
- The sound map shows the heard point, its trail and each voice's point; the
  envelope view follows the heard point.
- New tabs under the map: Components, Random Walk, LFOs & Expression.
- `pcs-render --walk ...` and listening examples 09–14.

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
