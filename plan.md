# PCASynth — Concept & Multi-Stage Build Plan

An experimental synthesizer plugin (VST3, macOS) whose sounds come from a
**principal components analysis of a set of recorded notes**. Give it a folder
of WAVs of different instruments all playing the same note. It learns the
space those sounds span, and you play any point in that space: the training
sounds themselves, morphs between them, or places no instrument has been.

> **Status:** All eight stages are built, plus Stage 9, fidelity and
> inspection (version 0.9.0): the engine, the
> offline tools, and a playable VST3 with its own UI. It trains new spaces
> from your audio, moves through them (random walks, LFOs, expression),
> takes MPE (aimed at the Osmose), and models noise, inharmonic partials and
> pitch-dependent timbre. Stage 8 added presets, Level Lock, SIMD rendering,
> a manual and the release process. Stage 9 added pitch curves, sharp
> attacks, 64 components and the Inspect panel. What remains is playing it in a DAW and
> on an Osmose, and a signed release (§5, Stage 8). Decisions are recorded
> in §6. Licence: open source (AGPLv3, following JUCE's open-source
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

### Stage 5: Movement and expression — done
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
| Mode: **Neighbour Tour** | Each step goes to one of the three most similar sounds (no immediate backtracking), so it drifts through families. "Similar" is distance in the model's own units (components weighted by their SD). In z units every whitened component counts equally and the training sounds are exactly equidistant |
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

### Stage 6: MPE (for the Osmose) — done, awaiting a play on the Osmose
Per-note expression from MPE controllers, aimed at the Expressive E Osmose.
Each voice already had its own point in the space (Stage 5), so pressure and
slide became per-note moves through the space.

**Built:**
- **Engine** (`MpeParams`, events carry their channel):
  - With MPE on, each member channel keeps its latest bend, pressure and
    slide, so values sent just before a note-on apply to that note.
  - A note follows its channel until key-up, then keeps its last values. A
    released note is never bent or pressed by the next note on the same
    channel.
  - The master channel's bend and pressure stay global (bend uses the
    ordinary Pitch Bend Range). Lower zone (master 1, notes 2–16) or upper
    zone (master 16, notes 1–15).
- **Pitch:**

  `note + master bend × Pitch Bend Range + note bend × Note Bend Range`

  The Note Bend Range defaults to 48 semitones.
- **Pressure** (channel pressure on a member channel, or poly aftertouch):
  - Routed per note to PC1–PC16 or Toward Sound. The default is PC1,
    +1.5 SD at full pressure.
  - Curve −1..1: the pressure is raised to the power 3^−curve, so above 0
    responds more to a light touch.
  - Smoothing: 20 ms by default.
- **Slide (CC74):** routed per note (default PC2, ±1 SD), either bipolar
  around 64 or unipolar from 0.
- **With MPE off**, everything behaves as in Stage 5:
  - Channel bends are global.
  - Channel and poly pressure feed the global aftertouch routing.
  - CC74 is ignored.
- **Plugin:**
  - Declares MPE support to the host (`supportsMPE`).
  - Reads the controller's **MPE Configuration Message** (RPN 6 on channel 1
    or 16), which turns MPE on and sets the zone, and **RPN 0** on a member
    channel, which sets the Note Bend Range. So a controller that sends its
    setup configures the plugin.
- **UI:** an **MPE** tab with the zone, Note Bend Range, the Pressure and
  Slide routing, and a **note monitor**: each sounding note's channel, bend
  (semitones), pressure and slide, live. The sound map shows each note's
  point as it moves; the envelope view follows the most recent note.
- **Engine tests:**
  - Member-channel bends are independent, checked by the rendered pitch.
  - The master bend moves every note.
  - Pressure, slide and poly pressure move only their own note.
  - Pressure curve and smoothing.
  - A released note keeps its expression when the channel is reused.
  - Upper zone.
  - MPE off leaves channels alone.
- **Plugin tests:**
  - Declares MPE support.
  - The MPE Configuration Message and RPN 0 set the parameters.
  - Per-note bend and pressure go through the processor.
  - Default routing moves notes 1.5 SD on PC1 and 1 SD on PC2.
  - MPE off behaves as before.

**Still to check on a real Osmose:** the feel (pressure curve, smoothing,
amounts); whether it sends the MPE Configuration Message (if not, switch MPE
on by hand); and its pitch-bend range setting (match Note Bend).

### Stage 7: Richer model — done, awaiting a listen

Each sound's vector now has **sections**:

| Section | Values | Weight in the PCA |
|---|---|---|
| Harmonics | frames × harmonics, in the chosen representation | 1 |
| Loudness | frames (Shape + loudness only) | √(harmonics + bands): like shifting every band |
| Noise | frames × 16 bands | 1 |
| Partials | harmonics: cents from exact | 0.1·√frames: 10 cents ≈ 1 dB held all note |

File format version 2. Version 1 files load unchanged (harmonics in dB
only).

**Residual noise** (breath, bow, hammer):
- **Where it is measured:** 16 bands at fixed ratios of f0 (0.5–96 × f0), so
  they move with the played note like the harmonics. The noise is measured
  with a 16-period window (twice the harmonic window) in "quiet" bins at
  least 0.32 of the local spacing from every partial, where the partials'
  main lobes have ended.
- **How the estimate is made robust:**
  - The median / ln 2 estimates the noise level, bins above 3× that are
    dropped (partials smeared by vibrato), and the rest are averaged with the
    exact correction for the trim.
  - Bands inside the fundamental's main lobe take their neighbours' noise
    density.
  - Pure tones read at the floor.
  - White noise is measured within about 1 dB per band.
- **Resynthesis:** per voice, an RBJ band-pass filter per band, each on its
  own white-noise source, so the band powers add rather than correlate.
  Each filter is normalised by its exact noise gain (α / (1 + α)); the analog
  π/2 × bandwidth rule was up to 5.7 dB off near Nyquist. A noise-only sound
  renders back within 2.5 dB per band, and total power within 0.02 dB.
- **Level:** the **Noise** knob (dB, "off" at −60). Decoded noise is clamped
  at the training set's loudest band + 6 dB. The noise range across a set is
  wide (the floor for pure tones up to the breathy sounds), so walks would
  otherwise extrapolate it far past anything real.

**Partial tuning** (inharmonicity):
- **Tracking:** partials are tracked one after another on the long-term
  spectrum (predicted from the last spacing, accepted only as a clear peak).
  The fundamental is partial 1 itself, since the harmonic-sum estimate is
  pulled sharp by stretched partials.
- **Accuracy:** a stretched tone (B = 0.0004, C3) is measured within 3 cents
  and 0.5 dB up to partial 30. The old fixed-harmonic search lost them past
  about partial 10.
- **Playback:** oscillators run at `h × f × 2^(cents/1200)`, re-tuned only
  when the decoded cents change; rendered partials are within 3 cents.

**Timbre follows pitch** (multi-note training):
- **When:** pitch tracking is Auto (on when the set spans 3+ semitones), On
  or Off.
- **How:** every dimension is regressed on pitch (least squares) and that
  direction is taken out before the PCA. Each note adds it back:

  `(note − reference, clamped to the training range ± an octave) × Keytrack`
- **Result:** a note renders its predicted timbre within 0.02 dB.
- **Honest measure:** for an unseen pitch of a training instrument, the
  error drops from 3.3 to 2.9 dB. One shared direction captures the common
  trend (spectral features fixed in Hz sliding across harmonic numbers);
  each instrument's own pitch behaviour stays in the PCA.

**Representations** (a training option):
- **Decibels** (the default).
- **Shape + loudness:** the per-frame loudness is its own section.
- **Linear:** amplitudes, so morphs behave more like crossfades.

All three reproduce their training sounds.

**Synth and UI:**
- Voices without their own point share the frame caches (harmonics and
  noise). Keytrack voices decode their own. 16 voices with noise use about
  9 % of a core.
- The Train panel gains Noise bands, Partial tuning, Levels as, and Pitch
  tracking.
- The envelope view shows the noise bands above the harmonics.
- The model summary lists what a space contains.
- New knobs: Noise and Keytrack.

**Bugs found on the way:**
- A scratch buffer shared by the frame cache and the loop crossfade turned
  noise gains into dB values, and output blew up to about 250 × full scale.
- After a model swap, the point-version counter restarted at 1, so the
  partial-tuning cache could match an old model's version and keep its
  tuning. The counter now only increases, and a regression test swaps
  models and compares with a fresh synth.

**Tests:** noise level per band, pure tones stay clean, stretched partials,
round trips for every representation, v1 files, pitch tracking,
noise-render calibration, partial rendering, keytrack, model swaps, and
the plugin's options and state.

**Listening examples 15–18:**
- 15: noise (original | harmonics only | with noise | +9 dB).
- 16: partials (original | exact harmonics | stretched).
- 17: keytrack off/on over three octaves.
- 18: one morph in three representations.

### Stage 8: Polish and release — done, awaiting a signed release and a play in a DAW
- Presets (model + point + settings), manual, CPU optimisation (SIMD
  oscillators, skipping silent harmonics), signed and notarised installer.

**Built:**
- **Presets.**
  - 21 factory presets, which are recipes on the factory space: every
    parameter at its default except a few, plus a point (a training sound
    and offsets).
  - User presets are `.pcspreset` files holding the full plugin state
    (model included) minus the editor layout and the training list. They
    live in `~/Library/Audio/Presets/CrumpLab/PCASynth`.
  - A preset bar in the editor.
  - The host sees one program, named after the current preset. Exposing the
    presets as VST3 programs adds a program-change parameter, and pluginval's
    state-restoration test then failed (restoring it fought the saved
    state), so presets are chosen in the editor only.
- **CPU.** First, a benchmark (`pcs-bench`, five stress cases). Profiling
  showed the oscillator loop at ~50 %, then per-voice decoding and
  retuning. Changes:
  - Oscillators and noise filters now run 8 partials or bands at a time as
    GCC/Clang vector types. One source gives SSE/AVX on x86 and NEON on
    arm64, since GCC would not auto-vectorise the lane loops. Lanes past the
    top partial are silent, and all-silent groups are skipped.
  - Every voice mixes into shared per-lane accumulators, reduced to mono
    once per sub-block.
  - A voice's own point is decoded into a per-voice cache every 4
    sub-blocks (~2.7 ms), or when the frame changes. Partial retuning
    follows at the same rate, counted from note-on so repeated phrases stay
    bit-identical.
  - Noise-band edges are cached per model, and noise filters are redesigned
    only when the pitch changes.
  - `pow` became `exp2`, and the tilt gains are a table.
  - Result: 3–5× less CPU (16 walking voices: 20.5 % → 6 % of a core).
  - A test checks that the cached per-voice path renders the same as the
    shared path; it fails when the cache is broken on purpose.
- **Level Lock** (found while benchmarking).
  - The problem: in the dB representation, points a few SD off the training
    set are up to 35 dB louder than the centre, so random walks jump in
    level.
  - The fix: `Model::levelDb` measures a point's loudness as the loudest of
    8 log-spaced probe frames. This is the loudest-frame energy the analysis
    normalises every training sound by.
  - The synth applies `lock × clamp(ref − level, −48, +12)` dB against the
    training sounds' average. It is computed per shared-point change and
    every ~11 ms per voice, with a glide.
  - The plugin defaults to 100 % and a Gain of −12 dB; the engine defaults
    to 0.
- **Manual** (`docs/manual.md`, also in the zip) and **release process**
  (`docs/RELEASING.md`):
  - The signing and notarization secrets.
  - Notarization only on tags.
  - An installer choice for the Standalone app.
  - Signature checks in CI.

**Not done here** (needs the user's hardware and accounts):
- a signed, notarized release, which needs the Developer ID secrets and a
  tag;
- listening in a DAW, and MPE on a real Osmose.

### Stage 9: Fidelity and inspection — done, awaiting a listen on real samples

Asked for after training on 60 real, diverse samples gave poor
reproductions. There are two separate losses: the harmonic analysis and the
PCA. Inspection came first, so each change could be measured.

**Inspection:**
- Engine `Inspect`:
  - an ear-scale spectrogram (1/6 octave from 40 Hz, 100 frames/s);
  - a level-independent spectral distance (dB RMS over cells within 60 dB
    of each sound's peak);
  - `inspectSound`: the original, the analysis played back without PCA,
    and the model at the sound's point (optionally with K components),
    each loudness-matched and scored whole and over the first 150 ms.
- **Fit report**, computed at training and saved in the model: each
  sound's harmonic-envelope error, and the mean error with the first K
  components.
- `pcs-inspect` CLI.
- **Plugin Inspect panel:**
  - a scored list and a fit-vs-components chart;
  - three spectrograms with loudness-matched playback through the output
    (audition clips, handed to the audio thread without allocating);
  - pitch curves and a Components slider;
  - a background `Inspector` that finds the files through the training
    list.

**Fidelity:**
- **Pitch curve.**
  - Each frame's pitch is the amplitude-weighted mean of its clear low
    partials' peak frequencies, each divided by its place in the series.
    It is tracked from the previous frame.
  - A partial counts only if it stands 15 dB above the mean level halfway
    to its neighbours; a frame counts only with two such partials (or one
    30 dB proud). Without this, pure noise steered the pitch.
  - Harmonics and noise bands (whose quiet-bin mask is scaled) are
    measured where the partials are in each frame.
  - The curve is a PCA section weighted so 10 cents in a frame counts like
    1 dB on every harmonic of it. The synth decodes it at each voice's
    position every refresh; the Pitch Env knob scales it.
- **Sharp attacks.**
  - Windows never reach back before the onset (a straddling window smears
    the onset into a click), and the first 150 ms blend in a Hann window
    half as long.
  - Clamped windows measure a little *later* than their frame, so slow
    swells came out too loud. The level in the attack therefore comes from
    a one-period RMS (above 0.6 × f0) at the frame's time.
  - The pre-roll is 1 ms instead of 5.
- **Components:** up to 64 (`kMaxComponents`), and training keeps all the
  sounds allow. The factory space keeps 32, so the binary stays at ~4 MB.
- **Training options:** Pitch curve, Sharp attacks, Frames (100/200).

**Measured on the 60 synthetic sounds** (spectral error vs the originals,
dB):

| | Before | After |
|---|---|---|
| Analysis only | 1.92 | 1.39 |
| Analysis, first 150 ms | 3.63 | 3.25 |
| Model, 32 components | 2.67 | 2.48 |
| Model, all components | 2.67 | 1.39 |

- Vibrato sounds gained most (vowel_1 3.2 → 0.6).
- Percussive attacks improved (pluck 3.8 → 1.7) but stay the largest error
  (mallet 6.4, epiano 8.5 at the attack). What remains there is
  non-harmonic transient energy (thumps, clicks).
- 200 frames/s showed no gain on this metric, whose own time resolution is
  ~40 ms.
- CPU rose ~5–10 % from retuning partials as the curve moves.

**Suggested next** (inspection first, then fidelity):
1. **Component explorer**: for PC k, show where in time and in which
   harmonics its loading acts, and play −2/0/+2 SD. This answers "what
   does PC3 do?".
2. **Leave-one-out score**: how well the space predicts a training sound it
   was not trained on, i.e. whether it generalises between sounds rather
   than memorising them.
3. **Export A/B**: write every sound's three versions as WAVs from the
   plugin.
4. **Transient layer**: keep each sound's non-harmonic onset residual
   (original minus analysis, first ~100 ms) as a short sample, blended by
   nearest training sounds. This targets the largest remaining error.
5. Per-frame partial tuning (inharmonicity that changes over time), and
   stereo.

## 6. Decisions

| # | Question | Decision |
|---|---|---|
| 1 | Representation | **Harmonic model** (per-harmonic dB envelopes), not raw-waveform PCA |
| 2 | Framework / formats | **JUCE + CMake**, **VST3 on macOS** (plus Standalone for testing) |
| 3 | Structure | Follows MinervaSpaceEcho: framework-free engine, offline tools, Catch2, CI |
| 4 | First training data | **Synthetic**, generated deterministically (real sample libraries are blocked from the dev container; your own WAVs work any time) |
| 5 | Licence | **Open source, AGPLv3** (assumed, as for MinervaSpaceEcho) |
| 6 | Expressive controller | **MPE, for the Expressive E Osmose** (Stage 6): per-note bend, pressure and slide as per-note moves through the space |
