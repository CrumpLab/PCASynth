# PCASynth — Concept & Multi-Stage Build Plan

An experimental synthesizer plugin (VST3, macOS) whose sounds come from a
**principal components analysis of a set of recorded notes**. Give it a folder
of WAVs of different instruments all playing the same note. It learns the
space those sounds span, and you play any point in that space: the training
sounds themselves, morphs between them, or places no instrument has been.

> **Status:** Stages 1–5 are built: the engine, the offline tools, and a
> playable VST3 with its own UI, which trains new spaces from your audio
> and moves through them (random walks, LFOs, expression). Stage 6 (MPE
> for the Osmose) is planned. Decisions are recorded in §6. Licence: open source (AGPLv3, following JUCE's open-source
> licence).

---

## 1. The idea

| Wavetable synth | PCASynth |
|---|---|
| A table of single-cycle waveforms | A **space** learned from whole recorded notes |
| Position knob scans the table | **Component sliders / 2D map** move through the space |
| Morph = crossfade between neighbouring tables | Morph = a straight line between two sounds' coordinates |
| Table built by hand | Space built by PCA from any set of WAVs you give it |

Each training sound becomes a point, its **scores** on the principal
components. Every other point in the space is a sound too.

### Why not PCA on raw waveforms?

PCA is linear. On raw samples it fails in two ways:

1. **Phase:** two instruments playing the same C don't line up sample for
   sample. The components end up describing phase noise, and reconstructions
   are smeared and comb-filtered.
2. **Morphs are crossfades:** with all components kept, the point halfway
   between A and B *is* the average of the two waveforms. You hear both
   instruments at once, not a hybrid.

So PCA runs on a representation where timbre lines up across sounds.

## 2. The harmonic model

Every training sound plays the same note, so each can be described as **how
loud each harmonic is over time**:

- **Analysis:** find the sound's exact f0 (each sound is searched within ±60
  cents of the nominal note). Then, every 10 ms, measure the level of
  harmonics 1…64 with a Blackman-Harris STFT (window = 8 periods, peak search
  ±0.35 f0 around each harmonic, parabolic interpolation).
- **Alignment:** trim each sound to its onset (first sample above −40 dB of
  its peak, minus 5 ms). Normalise loudness so every sound's loudest frame is
  at 0 dB.
- **Representation:** levels in **dB**, clamped to a floor (−80 dB). Morphing
  dB is a geometric mean of amplitudes, which sounds like a blend of spectral
  *shapes* rather than two sounds stacked.
- **Vector:** 400 frames × 64 harmonics = 25,600 numbers per sound (4 s at 100
  frames/s).
- **PCA:** mean + up to 32 components (at most N − 1 for N sounds), computed
  through the N×N Gram matrix (Jacobi eigen-solver), so thousands of
  dimensions cost nothing. Each component's sign is fixed so that + means
  "more energy overall".
- **Coordinates** are in **z units**: standard deviations of the training
  sounds along each component. ±2 covers most of the training set.

### Resynthesis

A bank of sine oscillators at multiples of the played note's frequency, one
per harmonic. Amplitudes are decoded from the model for the voice's current
frame: `dB = mean + Σ zⱼ · SDⱼ · componentⱼ`, then converted to linear
amplitude. Because the model stores *levels per harmonic number*, not
frequencies, **any MIDI note** plays the learned timbre without resampling.

- Decoding one frame costs harmonics × components multiply-adds, so the
  point can move while notes sound (automation, modulation, glides).
- Frames are cached per point; voices sharing a frame decode it once.
- Harmonics fade out between 0.40 and 0.47 × the sample rate (no aliasing).
- Oscillators are complex rotators renormalised every 32 samples. Starting
  phases are fixed and spread out, so every note starts the same way without
  a peaky waveform.
- Cost: 16 voices of a 64-harmonic sound ≈ 9 % of one core (unoptimised).

### Play modes (how a note moves through the envelope)

| Mode | Behaviour |
|---|---|
| One-shot | Plays the 4 s envelope once, like the sample |
| Loop | Plays to Loop End, then loops Loop Start…Loop End (crossfaded) while held |
| Ping-pong | Back and forth between the loop points while held |
| Scan | Holds one moment of the envelope (Scan Position). Moving it scans through the sound, wavetable style |

Looping is click-free for free: only the amplitude envelopes loop, and the
oscillators keep running.

### Known limits of the harmonic model

- **Noise is lost:** breath, bow noise and hammer thumps are not harmonics. A
  residual noise model (band energies over time) is a later stage (§6).
- **Inharmonic partials** (piano stretch, bells) are resynthesised as exact
  harmonics. Per-harmonic frequency ratios could be added to the vector later.
- **One pitch in, all pitches out:** timbre doesn't change with register as it
  does on real instruments. Multi-note training sets are a later stage.
- The −80 dB floor makes **"which harmonics exist at all"** the biggest source
  of variance. On the synthetic set PC1 (73 %) is harmonic richness: organ,
  mallet and pluck (few upper harmonics) vs bowed, brass and vowel. A higher
  floor (e.g. −60 dB) would shift weight to the shape of the audible
  harmonics. The floor is a training option.

## 3. Training data

The container can't reach NSynth, the Iowa samples or Philharmonia, so the
first training set is **synthetic and deterministic** (`pcs-testgen`): 10
families × 6 variations at C4, each detuned by up to ±8 cents like real
samples.

| Family | How it is made |
|---|---|
| pluck | Karplus-Strong (real physical-model signal, not additive) |
| bowed | Sawtooth-like spectrum × random body resonances, slow bow attack, vibrato |
| reed | Odd harmonics dominant, adjustable even harmonics, roll-off |
| brass | Brightness follows the envelope, attack overshoot |
| flute | Few harmonics + band-passed breath noise, vibrato |
| organ | Random drawbar settings (harmonics 1, 2, 3, 4, 5, 6, 8, 10) |
| mallet | Partials 1, 2, 4, 10 with fast-decaying upper partials |
| epiano | 2-operator FM with decaying index (real FM signal) |
| vowel | Glottal-like source × formants of a, e, i, o, u |
| piano | Strike-position comb, two-stage decay, inharmonicity |

Your own WAVs work the same way: `pcs-train -o mine.pcsm --note 60 folder/`.

## 4. Architecture

```
PCASynth/
  engine/   plain C++20, no framework dependency
    Harmonic   analysis: WAV → harmonic envelopes (dB)
    Pca        Gram-matrix PCA + Jacobi eigen-solver
    Model      training, projection, decoding, .pcsm file format
    Synth      polyphonic additive voices, play modes, smoothing
  tools/    pcs-testgen, pcs-train, pcs-render, pcs-examples
  tests/    Catch2 unit + regression tests
  plugin/   JUCE wrapper: parameters, state, editor, built-in factory model
```

- **Engine separate from JUCE** (as in MinervaSpaceEcho): built, tested and
  rendered in a Linux container. The same code ships in the plugin.
- **Model file (`.pcsm`):** magic, JSON header (analysis settings, names, f0s,
  variances, scores), then float32 mean and components. Synthetic model ≈
  3.4 MB.
- **Real-time rules:** no allocation, locks or I/O on the audio thread.
  Loading a model allocates, so it happens off-thread and is swapped in.

## 5. Build stages

Each stage ends with something to **hear**.

### Stage 0: Scaffolding — done
- Repo layout, CMake, engine library, Catch2 tests, CI (Linux + macOS).

### Stage 1: Engine and offline tools — done
- Harmonic analysis, PCA, model file, additive resynthesis, play modes.
- `pcs-testgen` (synthetic training set), `pcs-train` (WAVs → model + scores
  CSV), `pcs-render` (any point → WAV), `pcs-examples` (listening set + map).
- Tests:
  - Analysis measures known harmonic amplitudes within 0.2 dB and f0 within
    0.6 cents.
  - PCA components are orthonormal and training sounds reconstruct exactly.
  - Model files round-trip.
  - **A rendered training point re-analyses to its own envelope (±1.5 dB).**
  - Notes play at their own pitch; voices end; loop modes sustain; far-out
    points stay finite.
- Listening examples (`scripts/render_examples.sh`): reconstructions (original
  | harmonic model | 32 / 8 / 3 / 0 components), stepwise morphs, glides
  within a note, PC1–PC4 sweeps, melodies across 3 octaves, a chord pad
  wandering through the space, the space's extremes, and envelope scans.

### Stage 2: Plugin v1 — done
- JUCE plugin: VST3 + Standalone (for testing without a DAW), universal
  macOS (arm64 + x86_64).
- **Factory model built in:** the synthetic set is generated and trained by
  the offline tools at build time, then compiled into the plugin.
- **Loading models:** Load Model… (file chooser) or drag and drop a `.pcsm`
  onto the window. Factory Space returns to the built-in model.
- **Model swaps without allocating on the audio thread:** the message thread
  prepares a slot (model plus decode cache), the audio thread swaps it in,
  and the old one is freed back on the message thread.
- **Jump to** a training sound sets PC1–PC16 to its coordinates. Components
  17–32 ("detail") live in the plugin state, so the sound is reproduced
  exactly. **Centre** returns to the mean.
- MIDI: notes, velocity, pitch bend, sustain pedal, all notes off.
  Polyphony 1–32 (default 16).
- Parameters:
  - **The point:** PC1–PC16 (SD, ±4), Components Used, Exaggerate, Morph
    Time
  - **Playback:** Play Mode (default Loop, so held notes sustain), Loop
    Start/End, Scan Position, Speed
  - **Voice:** Attack, Release, Brightness (dB/octave), Harmonics, Velocity
    Sensitivity, Pitch Bend Range, Polyphony, Gain
- **State:** the parameters plus the detail components. A loaded model is
  embedded (≈3.4 MB for 64 harmonics × 400 frames × 32 components), so a Live
  set reopens with its space. The factory model is not embedded (a few KB).
  If an embedded model is damaged, the parameters still load and the
  factory space returns.
- Editor: a model bar (load, factory, model summary, jump to, centre) above
  JUCE's generic parameter panel (replaced in Stage 3).
- Tests (headless):
  - A note sounds and stops after its release.
  - Jumping sets the coordinates.
  - The state round trip with an embedded model renders identically.
  - A damaged state falls back to the factory space.
  - Swapping models during playback stays finite.
- pluginval passes at strictness 10 (Linux, locally); CI also runs it on
  macOS.
- CI: macOS universal VST3 + Standalone, pluginval, and a zip plus a `.pkg`
  installer (ad-hoc signed unless signing secrets are set); tags `v*` make a
  GitHub release. Linux also renders the editor snapshot and the listening
  examples.
- **Done when:** you play it in your DAW from a CI build.

### Stage 3: Custom UI — done
- **Sound map:** the training sounds on any two components (X/Y menus list
  every component with its % of variance).
  - Hover a sound for its name and coordinates. Click it to jump there.
  - Drag anywhere else to move the point along the two axes; the other
    components stay put.
  - The point is an orange ring with crosshairs.
- **Morph pad:** four corner sounds, chosen from menus. Dragging the puck
  blends their coordinates bilinearly over all 32 components; along an edge
  it is a straight morph between two sounds.
- **Envelope view:** the current point decoded as a harmonics × time heat map.
  - It shows what the synth plays: Exaggerate and Components Used are
    applied.
  - Sequential blue ramp from −60 dB (receding into the background) to 0 dB,
    with a legend.
  - Loop region (dashed, outside dimmed) or scan position, and an orange
    playhead per sounding voice.
- **Component strip:** PC1–PC16 as vertical sliders filling from 0 (the
  mean), each with a bar and % showing the variance it explains. Sliders
  beyond the model's components or Components Used are dimmed. Double-click
  returns a slider to 0.
- **Knobs** for everything else, grouped: Point, Playback (with the Play
  Mode menu), Voice, Play. Times show in ms below one second.
- **Export WAV…** renders one note (the last note played, default C4) at the
  current point and settings, on a background thread.
- Every tool moves the point by writing the PC parameters plus the detail
  components. Drags are wrapped in one automation gesture, so the host
  records and replays them.
- Map axes, morph corners and the puck position are saved with the state.
- Tests:
  - `setPoint` clamping and detail.
  - The morph pad: a corner is its sound, the centre is a blend.
  - The envelope view shows the decoded point.
  - Editor settings round-trip through the state.
  - Export renders a stereo note.
  - pluginval still passes at strictness 10.

### Stage 4: Training inside the plugin — done
- **Train…** opens a panel in place of the map, morph pad and envelope view.
- **Adding sounds:**
  - Add Files…, Add Folder… (searched recursively), or drop audio files or
    folders anywhere on the window.
  - Formats: WAV, AIFF, FLAC, Ogg, and MP3/M4A/CAF where the OS decodes them.
  - Remove (or the Delete key) and Clear edit the set. Each sound shows its
    detected pitch (note, cents, Hz), or why it failed.
- **Settings:**
  - Name.
  - Note: a fixed MIDI note, or **Auto**, which detects each sound's own
    pitch, so a set may mix notes.
  - Duration, Harmonics, Floor, Components.
  - Match loudness, Align onsets.
- **Train** runs analysis and PCA on a background thread with progress and
  a Cancel button. Files that fail (unreadable, silent, no pitch) are
  skipped and flagged; at least two must analyse. The new space replaces
  the model, and the point moves to its centre.
- **Cache:** analysed sounds are kept per file, keyed by modification time
  and analysis settings. Removing sounds or changing Components or Name
  retrains without re-analysing.
- **Save Model…** writes the current space as a `.pcsm`. The trained model
  is also embedded in the project, as in Stage 2.
- The training list (paths, not audio) and settings are saved with the
  project, so the set can be edited and retrained later if the files are
  still there.
- **Pitch detection (engine):** YIN over eight windows in the first second,
  median, then the spectral refinement (±30 cents). Finds all 10 synthetic
  families at MIDI 40, 60 and 76. That includes a vowel whose 3rd harmonic
  is 2.6× its fundamental and a bright low pluck, both of which broke the
  first, spectral-sum detector. `pcs-train --note auto` does the same
  offline.
- Tests:
  - A folder adds only audio files, and duplicates are ignored.
  - Mixed pitches are detected within 0.3 semitones.
  - A silent file fails cleanly.
  - Remove and retrain.
  - The training list, settings and model survive a state round trip.
  - The background thread finishes.

### Stage 5: Movement and expression — done, awaiting a play in a DAW
All modulation runs in the engine (`Modulation.h`), so offline renders use it
too. It is added after the point's smoothing:
- the **heard point** = home (the sliders) + shared walk + LFOs + mod wheel
  + aftertouch + macro;
- each voice may add its own walk, velocity and spread on top of that.

**Random walk** (the Random Walk tab):

| Option | What it does |
|---|---|
| Mode: **Drift** | Brownian motion (Ornstein–Uhlenbeck). With Tether 100 % its spread is Amount (SD); lower tethers wander further, bounded at ±4 SD |
| Mode: **Jumps** | A new random point (normal, SD = Amount) every step; **Glide** 0 = hard jumps … 100 % = continuous gliding |
| Mode: **Tour** | Travels from training sound to training sound in random order (Amount 1 = arrive exactly; 0.5 = halfway from home) |
| Mode: **Neighbour Tour** | Each step goes to one of the three most similar sounds (no immediate backtracking), so it drifts through families |
| Rate / **Sync** + Step | Steps per second, or tempo-synced (1/16 note to 16 bars, host tempo) |
| Components, **Focus** | Drift and Jumps move PC1..PCn. Focus: Equal (every component by Amount) or Main (in proportion to its variance) |
| **Per Voice** | 0 = one shared walk; 100 % = every note wanders on its own (its walk starts at home) |
| **Seed**, **Restart** | Same seed = same path. Restart: a note after silence restarts the walk, so each phrase repeats it |
| **Freeze** | Holds the walk where it is |

Switching the walk off glides home (100 ms); changing the mode or seed
restarts it.

**LFOs & Expression** tab:
- **Two LFOs:**
  - Shapes: sine, triangle, saw, square, sample & hold, smooth random.
  - Rate in Hz, or synced to the tempo.
  - Depth ±4 SD.
  - Target: PC1–PC16, or **Toward Sound**.
- **Velocity** (per voice), **mod wheel** (CC1) and **aftertouch**
  (channel or poly pressure), each with a destination and amount.
- **Toward Sound:** pick a training sound. Destinations set to Toward Sound
  move along the line from home to that sound (1 = all the way). The
  **Macro** knob (automatable) is that line as a single knob: the plan's
  "macro direction A→B".
- **Voice Spread:** each note starts at its own random offset (PC1–8).

**Display:**
- The sound map shows the heard point (filled orange), a 4-second fading
  trail, and a small ring for each voice with its own point.
- The envelope view shows the heard point while audio runs.

**Implementation notes:**
- Voices without their own modulation share one decoded-frame cache.
- Voices with their own point decode their frames directly, costing two
  frames per 32 samples per voice.
- Everything is allocation-free on the audio thread.

**Tests:**
- Drift's spread (≈ Amount at full tether) and its bounds; low tether
  wanders further.
- Jumps hold without glide and are continuous with it.
- Tours land exactly on training sounds; neighbour tours step only to near
  ones.
- LFO range and period.
- The synth: the walk fades home when off, per-voice points differ,
  velocity moves a voice, the LFO swings by its depth, mod wheel and macro
  reach the direction sound.
- The same seed with Restart repeats a phrase sample for sample; another
  seed differs.
- Synced steps follow the tempo.
- Plugin: parameters reach the synth, a tour moves the heard point, CC1
  reaches the sound, and the settings are saved.

**Listening examples:** 09–14 (drift, jumps, tour, neighbour tour, per-voice
walk, LFOs). `pcs-render` gained `--walk` and related options.

### Stage 6: MPE (for the Osmose)
Per-note expression from MPE controllers, aimed at the Expressive E Osmose.
Each voice can already have its own point in the space (Stage 5), so MPE
dimensions become per-note moves through the space.

**Why it's needed:** today, pitch bend is one bend for the whole
instrument, pressure is one global value, and slide (CC74) is ignored. In
MPE each note sends these on its own channel, so bending or pressing one
key would move every sounding note.

**Plan:**
- **MPE input:**
  - Notes tracked by channel, using JUCE's MPE support (`MPEInstrument`,
    zone layout: lower zone, 15 member channels by default).
  - The plugin declares itself MPE-capable to the host.
  - Settings: MPE on or off, zone, and master and per-note bend ranges.
    The per-note range defaults to 48 semitones, the Osmose default; set it
    to match the Osmose's setting.
- **Per-note pitch bend:** each voice bends on its own. Master-channel bend
  still bends everything.
- **Per-note pressure** (channel pressure on each note's channel, or poly
  aftertouch) routed to a destination for that note only: PC1–PC16 or
  Toward Sound, with an amount and a **pressure curve** (response shaping
  for the Osmose's press depth) plus light smoothing.
- **Per-note slide** (CC74) as a second routable per-note direction, with
  its own amount. It is bipolar around its centre or unipolar from 0, as a
  setting.
- **Strike velocity** uses the existing velocity routing. Release velocity
  could set per-note release time (optional).
- **Non-MPE controllers** keep working as now: with MPE off, pressure and
  CC74 stay global.
- **Display:** the sound map shows each note's point moving as you press
  and slide; the envelope view follows the most recent note.
- **Tests:** simulated MPE streams.
  - Two notes on separate channels bend and press independently.
  - Pressure moves only its own note's point.
  - Master bend moves both.
  - Non-MPE input is unchanged.
  - Voice stealing and channel reuse.
- **Not testable here:** the feel on a real Osmose (pressure curve, ranges).
  It needs a play-through, and the defaults will be tuned from that.

### Stage 7: Richer model
- **Residual noise:** band energies of what the harmonics don't explain, as
  extra dimensions in the same PCA. Resynthesised as filtered noise.
- **Partial frequency ratios** (inharmonicity) as extra dimensions.
- **Multi-note training:** several notes per instrument, with pitch as a
  model input (key-mapped spaces, or pitch as a regressor).
- Alternative representations (linear amplitude, per-frame loudness
  separated from spectral shape) to compare how the spaces sound.

### Stage 8: Polish and release
- Presets (model + point + settings), manual, CPU optimisation (SIMD
  oscillators, skipping silent harmonics), signed and notarised installer.

## 6. Decisions

| # | Question | Decision |
|---|---|---|
| 1 | Representation | **Harmonic model** (per-harmonic dB envelopes), not raw-waveform PCA |
| 2 | Framework / formats | **JUCE + CMake**, **VST3 on macOS** (plus Standalone for testing) |
| 3 | Structure | Follows MinervaSpaceEcho: framework-free engine, offline tools, Catch2, CI |
| 4 | First training data | **Synthetic**, generated deterministically (real sample libraries are blocked from the dev container; your own WAVs work any time) |
| 5 | Licence | **Open source, AGPLv3** (assumed, as for MinervaSpaceEcho) |
| 6 | Expressive controller | **MPE, for the Expressive E Osmose** (Stage 6): per-note bend, pressure and slide as per-note moves through the space |
