# PCASynth manual

Version 0.10.0 · [Matthew Crump](https://crumplab.com), Brooklyn College of CUNY

PCASynth is a synthesizer that plays **a space of sounds learned from
recordings**. Give it a set of notes from different instruments and it
finds the main ways they differ. Each recording then becomes a point in a
space you can play and move through: the originals, blends of them, and
places no instrument has been.

This manual covers every part of the plugin. For installing and building,
see the [README](../README.md); for the design, see [plan.md](../plan.md).
Its sister plugin, **PCAWave**, runs the PCA on the waveforms themselves
(a linear morph synth); it has [its own manual](manual-pcawave.md).

- [1. How it works](#1-how-it-works)
- [2. Quick start](#2-quick-start)
- [3. The window](#3-the-window)
- [4. Presets](#4-presets)
- [5. The point: components and moving around](#5-the-point-components-and-moving-around)
- [6. Playback and the voice](#6-playback-and-the-voice)
- [7. Random walk](#7-random-walk)
- [8. LFOs and expression](#8-lfos-and-expression)
- [9. MPE (Osmose and other MPE controllers)](#9-mpe-osmose-and-other-mpe-controllers)
- [10. Training your own space](#10-training-your-own-space)
- [11. Inspecting the model](#11-inspecting-the-model)
- [12. Files and projects](#12-files-and-projects)
- [13. Performance](#13-performance)
- [14. Troubleshooting](#14-troubleshooting)
- [15. Parameter reference](#15-parameter-reference)

---

## 1. How it works

**Analysis.** Each training recording is one note. PCASynth runs a Fourier
analysis every 10 ms and measures how loud each harmonic is (up to 128) at
the note's pitch in that moment. Along with the levels it measures:
- the pitch itself, frame by frame (vibrato, glides, pitch drops);
- the level of the noise between the harmonics (breath, bow, hammer), in 16
  bands;
- how far each partial sits from a perfect harmonic (the stretch of a piano
  or a bell).

The sound becomes one long list of numbers: its *harmonic envelope*.

This is a model of the sound, not a recording of it. It keeps what a sum of
harmonics plus band noise can express: how the harmonics' levels change,
the noise, and the pitch. It does not keep:
- the waveform's phases or stereo image;
- anything above the highest harmonic (harmonics × pitch);
- transients that aren't harmonic, like the thump of a hammer or the click
  of a key.

§11 shows how to hear and measure what a space keeps.

**The space.** Principal components analysis (PCA) finds the directions in
which the training sounds differ most:
- **PC1** is the biggest difference across the set, often brightness or
  attack;
- **PC2** is the next biggest difference unrelated to PC1, and so on.

Every sound has a coordinate on each component. The unit is the *standard
deviation* (SD): ±1 SD is typical for the training set, and ±2 SD covers
nearly all of it. The centre (all zeros) is the average sound.

**Playing.** Any point decodes back into a harmonic envelope, which a bank
of sine oscillators and noise filters plays at whatever pitch you play. The
timbre comes from the point; the pitch comes from the keyboard.

What this means in practice:
- Moving along a component changes one learned quality of the sound.
  Moving between two training sounds morphs one into the other.
- Going beyond ±2–3 SD gives exaggerated, unfamiliar sounds (Level Lock,
  §6, keeps them at a playable level).
- The space only knows what the training set contains. A space learned
  from ten wind instruments is a very different instrument from one learned
  from ten pianos.

## 2. Quick start

1. Insert PCASynth on an instrument track. It starts in the **factory
   space**: 60 synthetic instrument notes (plucks, bowed strings, reeds,
   brass, flutes, organs, mallets, electric pianos, vowels, pianos).
2. Play. You hear the centre of the space: the average of all 60 sounds.
3. Click dots on the **sound map** (left) to jump to training sounds, or
   drag between them.
4. Open the **Preset** menu (top right) and try the factory presets. The
   **Random Walk** presets show what movement does.
5. Pick four sounds for the **Morph** pad corners and drag the puck while
   you hold a chord.

## 3. The window

![The plugin window](screenshot.png)

**Top bar, first row:**
- **Load Model…**, **Save Model…**: open or save a space (`.pcsm` file). You
  can also drop a `.pcsm` file on the window.
- **Factory Space**: go back to the built-in space.
- **Train…**: open the training panel (§10).
- **Inspect…**: see how faithfully the space reproduces its training sounds,
  and listen to them side by side (§11).
- **Export WAV…**: render the last note you played, at the current point
  and settings, to a stereo WAV file.
- **Jump to**, **Centre**: go to a training sound, or to the average sound.
- On the right: status messages and the number of notes sounding.

**Top bar, second row:**
- A summary of the space: its name, number of sounds, components and
  harmonics, what it models (noise, partial tuning, timbre following
  pitch), and how much of the variety PC1–16 capture.
- The **preset bar** (§4).

**Sound map** (left):
- Every training sound is a dot, placed on two components; choose which
  under the map. Colours group sounds by name prefix (for example `reed_1`
  … `reed_6`).
- Click a dot to jump to it. Drag anywhere else to move the point along the
  two components shown; the point is the orange ring.
- While a walk or modulation runs, a filled dot shows where you are
  actually hearing, with a trail. With per-voice movement, each note shows
  its own small dot.

**Morph pad** (middle): choose a sound for each corner, then drag the puck.
- A corner plays that sound.
- An edge morphs between its two corners.
- The middle is an even blend of all four.
- The pad sets PC1–16 and the finer components beyond.

**Harmonics over time** (right): the sound at the current point.
- Rows are harmonics (the fundamental at the bottom); time runs left to
  right; brighter is louder.
- The strip along the top is the noise level.
- Dashed lines mark the loop points; orange lines show where each sounding
  voice is.

**Tabs** (middle row):
- **Components**: sliders for PC1–16 (§5).
- **Random Walk** (§7).
- **LFOs & Expression** (§8).
- **MPE** (§9).

**Knobs** (bottom): Point, Playback, Voice and Play settings (§5, §6).

The window can be resized from the bottom-right corner.

## 4. Presets

The preset bar is on the right of the second row:
- **< / >** step through the presets.
- The menu lists the factory presets by category, then your own under
  **Your presets**.
- **Save Preset…** saves the current sound.

**Factory presets** play the factory space. Each starts from every setting
at its default, changes a few, and goes to a point.

| Category | Presets |
|---|---|
| Basic | Init |
| Instruments | Reed Lead, Brass Swell, Breathy Flute, Drawbar Organ, Mallet Keys, Soft E-Piano, Plucked String, Exaggerated Piano |
| Pads | Vowel Choir, Frozen Wavetable, Ensemble, Between Worlds |
| Random Walk | Drifting Timbre, Orchestra Tour, Neighbourhood, Timbre Sequencer, Every Note Its Own |
| MPE | MPE Breath, MPE Brass Growl, MPE Morph Pad |

Presets are chosen in the plugin window, not from the host's program list:
the host sees a single program named after the current preset. Your
project remembers which preset you started from.

**Your presets** are `.pcspreset` files. A preset holds everything about the
sound:
- the space (unless it is the factory space);
- the point, including the finer components beyond PC16;
- every setting, and the Toward Sound choice;
- the map and morph pad settings.

It does not change which tab is open, and it does not bring the training
list with it. A preset made with your own trained space therefore plays
anywhere, even without the original audio files.

Presets are saved in:
- macOS: `~/Library/Audio/Presets/CrumpLab/PCASynth`
- Linux: `~/.config/CrumpLab/PCASynth/Presets`

Sub-folders are listed too. The menu also has **Load Preset File…** (from
anywhere) and **Show Presets Folder**. To share a preset, send the
`.pcspreset` file: it carries its own space.

## 5. The point: components and moving around

**Components tab.** PC1–PC16, in SD from the average sound.
- The bar above each slider shows how much of the training set's variety
  that component captures. The first few carry most of it.
- Double-click a slider to reset it.
- Components beyond PC16 (up to 32) are **detail**: set when you jump to a
  training sound or use the morph pad, and saved with your project, but
  without sliders.

**Point knobs:**
- **Exaggerate** (0–3×): scales the whole point. 0 is the average sound; 1
  is the point as set; 2 is twice as far from the average. On a training
  sound, it gives a caricature of it.
- **Components** (0–64): uses only the first K components. Fewer components
  is a smoother, more generic version of the sound.
- **Morph Time** (0–2 s): how quickly the sound glides when the point
  changes, whether by jumps, the morph pad or automation.
- **Keytrack** (0–150 %): for spaces trained with pitch tracking (§10), how
  much each note takes on the timbre of its register. 0 plays every note
  with the timbre at the training pitch.
- **Pitch Env** (0–200 %): how much of the learned pitch movement to play:
  vibrato, glides, the pitch drop of a plucked string. 0 plays every note at
  a steady pitch; 200 % doubles the vibrato. Only spaces trained with Pitch
  curve on (the default since 0.9) have one.

All 16 PC sliders are host parameters, so you can automate them.

## 6. Playback and the voice

**Play Mode** sets how a held note moves through the sound's envelope:
- **Loop** (default): plays up to **Loop End**, then loops between **Loop
  Start** and Loop End (crossfaded) for as long as the note is held.
- **One-shot**: plays the envelope once, like the recording (plucks,
  mallets, pianos).
- **Ping-pong**: plays forwards and backwards between the loop points.
- **Scan**: freezes one moment of the envelope, set by **Scan**, like a
  wavetable. Automate Scan, or modulate the point, for movement.

**Speed** (0–4×) sets the envelope playback rate; 0 freezes it.

**Voice knobs:**
- **Attack**, **Release**: an extra fade in, and the fade out after key-up
  (to −60 dB), on top of the sound's own envelope.
- **Brightness** (±12 dB per octave): tilts the harmonics' levels.
- **Harmonics** (1–128): the most harmonics played (the space itself has up
  to 64). Harmonics above the audible range are always dropped.
- **Noise** (off to +12 dB): the learned breath, bow and hammer noise,
  relative to the model. "off" plays pure harmonics.

**Play knobs:**
- **Velocity** (0–1): how much velocity affects level (0 = every note at
  full level).
- **Bend**: pitch-bend range, in semitones.
- **Voices** (1–32): polyphony.
- **Level Lock** (0–100 %, default 100 %): holds every point near the
  loudness of the training sounds.
- **Gain**: output level.

**Why Level Lock exists.** The space stores levels in decibels. Points far
from the training set can come out tens of decibels louder or quieter than
any real sound. A random walk that strays there jumps in level. At 100 %,
every point plays at about the training sounds' loudness: cuts are as deep
as needed, and boosts go up to 12 dB. The training sounds themselves barely
change. Turn it down to hear the space's raw levels.

## 7. Random walk

![Random walk](screenshot-walk.png)

The walk moves the point by itself. Switch **On** and choose a **Mode**:
- **Drift**: smooth, Brownian wandering around the point you set.
  **Tether** pulls it back home: 0 wanders freely (bounded at 4 SD), 100 %
  stays close.
- **Jumps**: a new random point every step. **Glide** turns jumps into
  sweeps: 0 jumps instantly; 100 % glides for the whole step.
- **Tour**: travels from one training sound to another, in random order.
  **Glide** sets how smoothly.
- **Neighbour Tour**: each step goes to a *similar* training sound, so it
  wanders through families rather than leaping across the space.

**Motion:**
- **Amount**: how far it goes (SD). In tours, 1 arrives at each sound and
  0.5 goes halfway there.
- **Rate**: steps per second (Drift: its speed). Or turn on **Sync** and
  choose a **Step** length (1/16 to 16 bars) to follow the host's tempo.

**Which components:**
- **Components**: Drift and Jumps move PC1…PCn (tours move them all).
- **Focus**: *Equal* moves every component the same number of SD; *Main
  components* moves each in proportion to the variety it explains, so the
  big differences dominate.
- **Per Voice**: 0 is one walk for everything. 100 % gives every note its
  own walk, so a chord spreads out into different timbres.

**Repeat:**
- **Seed**: the same seed gives the same path.
- **Restart**: a note after silence restarts the walk from home. With a
  fixed seed, every phrase gets the same sequence of timbres (see the
  *Timbre Sequencer* preset).
- **Freeze**: holds the walk where it is.

The map shows the walk's position and trail. The walk moves the sound you
hear, not the sliders: stop it and the sound glides back home.

## 8. LFOs and expression

**LFO 1 and 2:**
- **Shape**: Sine, Triangle, Saw, Square, Sample & Hold, or Smooth Random.
- **Rate**: in Hz, or synced to the host's tempo (**Sync** plus **Cycle**).
- **Depth**: in SD (negative inverts).
- **Target**: any of PC1–16, or **Toward Sound**.

**Velocity**, **Mod wheel** and **Aftertouch** can each move the point.
Each has a destination (**To**: Off, PC1–16 or Toward Sound) and an
**Amount** in SD at full value.
- Velocity moves each note's own point, so harder notes sound different.
- The mod wheel and channel aftertouch move the shared point.

**Toward Sound** makes directions out of sounds. Choose a training sound
under **Sound**; any source set to "Toward Sound" then moves the point along
the straight line towards it. At full value with Amount 1, it arrives there.
- By default the mod wheel does this: pick a sound and the wheel morphs
  into it.
- **Macro** (0–100 %) is a knob for the same move, for automation.

**Spread** (0–2 SD) gives each new note its own small random offset on
PC1–8, so chords and repeated notes vary like an ensemble.

## 9. MPE (Osmose and other MPE controllers)

![MPE](screenshot-mpe.png)

With MPE, every note has its own pitch bend, pressure and slide.
- Open the **MPE** tab and switch **On**. Controllers that send their MPE
  configuration (MPE Configuration Message) switch it on, and set the zone
  and bend range, by themselves.
- **Zone**: *Lower* (master channel 1, notes on 2–16) is the usual setting.
  *Upper* uses master channel 16 and notes on 1–15.
- **Note Bend**: semitones for a full per-note bend. Match the controller:
  the Osmose defaults to 48.

**Pressure** (channel pressure on each note's channel):
- Moves that note's own point: by default along PC1, or along any component
  or Toward Sound.
- **Amount** is SD at full pressure.
- **Curve** shapes the response: above 0 gives more from a light touch,
  below 0 saves the change for pressing hard.

**Slide** (CC74) is a second per-note direction (default PC2).
- *Bipolar* treats the centre as "no change".
- *Unipolar* treats the bottom as "no change".

**Smoothing** steadies pressure and slide; shorter responds faster.

The **Notes** monitor shows each sounding note's channel, bend, pressure and
slide. If a gesture does nothing, check here first that the note receives
it. On the sound map, each note shows its own point as you play.

The master channel's pitch bend, mod wheel and pressure still act on every
note. With MPE off, PCASynth behaves as an ordinary synth.

**Osmose notes.** The Osmose's own presets send MPE on the lower zone with
48-semitone bends. Pressure is the key's aftertouch depth, and slide is
side-to-side movement. If pressure feels too sudden, raise **Smoothing** or
lower **Curve**.

## 10. Training your own space

![Training](screenshot-train.png)

Click **Train…**. Add sounds with **Add Files…** or **Add Folder…**, or drop
files or folders on the window.
- WAV, AIFF, FLAC and Ogg work everywhere; MP3 and M4A work on macOS.
- Each file should be **one note**. It can have silence before it, which is
  trimmed.

**Sounds.** You need at least two; more variety makes a richer space.
Around 10–100 sounds work well. The list shows each sound's detected pitch
after training, and marks files that could not be analysed (silence,
unpitched sounds).

**Settings:**
- **Name**: the space's title.
- **Note**: the note every file plays. **Auto** detects each file's pitch,
  so a set can mix notes. Auto-detection handles clear pitched notes; set
  the note by hand if it guesses an octave wrong.
- **Duration**: seconds analysed from each onset. Longer sounds are cut;
  shorter ones fade to the floor.
- **Harmonics** (8–128, default 64): how many harmonics to track. This sets
  the highest frequency the space keeps, harmonics × the note's pitch.

  | Note | 64 harmonics reach | 128 harmonics reach |
  |---|---|---|
  | C2 (65 Hz) | 4.2 kHz | 8.4 kHz |
  | C3 (131 Hz) | 8.4 kHz | 16.7 kHz |
  | C4 (262 Hz) | 16.7 kHz | 20 kHz+ |

  For notes below about C4, use 128, or the space sounds dull.
- **Floor**: the quietest level kept, in dB. A higher floor makes the space
  care more about the shape of the audible harmonics than about which faint
  ones exist.
- **Components** (up to 64, default 64): how many to keep, at most one
  fewer than the number of sounds. With as many components as that allows,
  every training sound is reproduced exactly (up to the analysis).
- **Normalize**: scale every sound so its loudest moment is at 0 dB
  (recommended: otherwise loudness dominates PC1).
- **Trim onset**: line sounds up on their first sound.

**The richer model:**
- **Noise bands** (0–32, default 16): residual noise between the harmonics
  (breath, bow, hammer). 0 is harmonics only.
- **Partial tuning**: learns each partial's tuning. Needed for pianos, bells
  and other inharmonic sounds; harmless for harmonic ones.
- **Levels as**:
  - *Decibels* (default): morphs blend spectral shapes; the most musical
    for most sets.
  - *Shape + loudness*: the loudness envelope is modelled separately from
    the spectrum's shape, which helps when sounds differ mainly in dynamics.
  - *Linear*: morphs behave more like crossfades.
- **Pitch tracking**: train on several notes per instrument (for example C3,
  C4 and C5, with Note on Auto). PCASynth then learns how timbre changes
  with pitch, and each note you play gets the timbre of its register (the
  **Keytrack** knob sets how much). *Auto* turns this on when the sounds
  span at least 3 semitones.

**Fidelity:**
- **Pitch curve** (on): follows the pitch frame by frame, so vibrato,
  glides and pitch drops are learned, morphed and played (the **Pitch Env**
  knob sets how much). Off, every sound plays at a steady pitch.
- **Sharp attacks** (on): the first 150 ms are analysed with windows half as
  long, which never reach back before the note starts. Plucks, mallets and
  pianos keep their attack instead of starting with a soft or clicky smear.
  The level in that stretch follows the waveform closely, so slow swells
  keep their shape too.
- **Frames**: 100 per second (default) or 200. At 200, faster changes
  survive, but the model is twice the size.

Click **Train**. Analysis runs in the background; you can keep playing. The
new space replaces the current one and the point moves to its centre.
- Changing settings or removing sounds retrains quickly, because analysed
  sounds are cached.
- Your project remembers the file list and settings.
- **Save Model…** keeps the space as a `.pcsm` file to load anywhere.

**Tips for good spaces:**
- Record or choose notes at the same pitch and a similar length. For pitch
  tracking, use the same set of pitches for every instrument.
- Mix families for a wide space, or use variations of one instrument
  (bowings, mutes, dynamics) for a detailed one.
- Look at the map after training. Sounds that cluster together will morph
  smoothly; outliers take up the first components.
- Then open **Inspect…** (§11) to hear whether each sound survived.

## 11. Inspecting the model

![Inspect](screenshot-inspect.png)

Click **Inspect…** to see and hear how faithfully the space reproduces its
training sounds. Each sound can be heard in three versions:

| Version | What it is | What it tells you |
|---|---|---|
| **Original** | the file, from its onset | the reference |
| **Analysis** | the file's analysis played back as it is, with no PCA | what the harmonic model keeps |
| **Model** | the sound's point in the space, played back | what you get when you play that sound |

Differences between Original and Analysis come from the analysis:
- too few harmonics;
- transients the model can't represent;
- a wrong pitch.

Differences between Analysis and Model come from the PCA, which keeps too
few components.

**The list** (left) has every training sound with its scores. All scores
are in dB: under 2 is close, 2–4 is noticeable, and above 4 is clearly
different.
- **fit**: how far the sound's harmonic envelopes are from their
  reconstruction by the components. This is what the PCA loses, computed
  when the space is trained, so it is always there.
- **analysis** and **model**: spectral differences between the original and
  the Analysis and Model versions (see below). These need the audio files.
  **Evaluate All** scores every sound in the background; clicking a sound
  scores just that one.

**Fit vs components** (bottom left): the average fit error when only the
first K components are used. The curve shows how many components the space
needs; where it flattens, more components add little.

**The sound** (right), for the selected sound:
- Three spectrograms, one per version: time runs left to right, frequency
  runs up on a log scale from 40 Hz, brighter is louder, 60 dB range.
  Compare them for missing highs, smeared attacks or lost vibrato.
- **Original / Analysis / Model** buttons play each version,
  loudness-matched. **Stop** stops playback.
- The pitch curves of the analysis and the model, in cents.
- Scores for the whole sound and for its first 150 ms (the attack).
  **PCA** compares the Analysis and Model versions directly.
- **Components**: plays and scores the Model version with only the first K
  components. Slide it down to hear what each component adds.
- **Go to Sound** moves the point there.

**The audio files** come from the training list (§10), which your project
saves. A space loaded from a `.pcsm` file shows its fit scores without
them. To hear and score its sounds, add the same files under **Train…**.

**What to do about what you find:**

| You see | Try |
|---|---|
| **analysis** high; the Analysis version is dull | more **Harmonics** (§10) |
| **analysis** high only at the attack | **Sharp attacks** on; a thump or click that remains isn't harmonic and can't be modelled |
| vibrato missing | **Pitch curve** on |
| a wrong pitch; the Analysis version sounds wrong everywhere | set **Note** by hand |
| **fit** or **model** high, **analysis** low | more **Components**, or fewer, more similar sounds |
| one sound far worse than the rest | it may not be a single pitched note: check the file |

`pcs-inspect` (in the source tools) prints the same scores for a model and a
folder of files, and can write the three versions as WAVs.

## 12. Files and projects

| File | What it holds |
|---|---|
| `.pcsm` | A space (model): analysis settings, the components, the training sounds' names and coordinates. No audio. |
| `.pcspreset` | A preset: settings, point, and the space unless it is the factory one. |
| Your DAW project | The same as a preset, plus the training file list and the editor layout. |

A project or preset made with a custom space stores that space inside it (a
few hundred KB to a few MB), so it opens on any computer. Projects using
the factory space stay small.

## 13. Performance

PCASynth runs its oscillators and noise filters as SIMD vectors (SSE or AVX
on Intel, NEON on Apple Silicon). Voices with their own point (per-voice
walks, velocity or MPE routing, spread) refresh it every ~3 ms. The pitch
curve refreshes at the same rate.

Typical load on one core of a 2020s laptop (48 kHz, factory space):

| Situation | CPU |
|---|---|
| 8 notes, no noise | ~2 % |
| 16 notes with noise | ~5 % |
| 16 notes, random walk | ~6 % |
| 16 notes, each walking on its own | ~8 % |
| 32 MPE notes, each with its own point | ~14 % |

To save CPU:
- lower **Voices**;
- lower **Harmonics** (high notes already drop inaudible ones);
- set **Noise** to off;
- set **Pitch Env** to 0 (no retuning);
- avoid per-voice movement when you don't need it.

`pcs-bench` (in the source tools) measures these cases on your machine.

## 14. Troubleshooting

- **macOS says the plugin is damaged or blocks it.** Unsigned builds carry
  a quarantine flag. Run `xattr -dr com.apple.quarantine
  ~/Library/Audio/Plug-Ins/VST3/PCASynth.vst3` and rescan. Signed releases
  don't need this.
- **No sound.** Check that the track sends MIDI to PCASynth and that
  **Gain** isn't at minimum.
  - In **One-shot** mode, notes end with their envelope.
  - At extreme points with **Level Lock** off, some sounds are nearly
    silent: press **Centre**.
- **Level jumps while walking.** Turn **Level Lock** up.
- **Clicks when moving fast.** Raise **Morph Time**, or **Glide** for walks.
- **MPE gestures do nothing.**
  - Check that the MPE tab is **On** and the **Zone** matches the
    controller.
  - Check that the **Notes** monitor shows pressure and slide arriving.
  - Some hosts need MPE enabled on the track as well.
- **Training fails on a file.** It may be silent, very short, or unpitched
  (drums, noise). Each file must be a single pitched note.
- **A trained space doesn't sound like its recordings.** Open **Inspect…**
  (§11). It shows whether the analysis or the PCA loses them, and what to
  change. Also check:
  - **Play Mode**: Loop plays the start and then loops a middle stretch;
    compare in **One-shot**.
  - **The note**: play each sound at the note it was recorded at, since
    other notes get transposed timbre.
  - **Detected pitches** in the training list: octave errors misplace every
    harmonic.

## 15. Parameter reference

All of these are host parameters (automatable). SD = standard deviations in
the space.

**Point:**

| Parameter | Range | Default |
|---|---|---|
| PC1 … PC16 | ±4 SD | 0 |
| Components Used | 0–64 | 64 |
| Exaggerate | 0–3× | 1 |
| Morph Time | 0–2 s | 0.05 s |
| Keytrack | 0–150 % | 100 % |
| Pitch Envelope | 0–200 % | 100 % |

**Playback:**

| Parameter | Range | Default |
|---|---|---|
| Play Mode | One-shot, Loop, Ping-pong, Scan | Loop |
| Loop Start, Loop End | 0–100 % of the envelope | 30 %, 70 % |
| Scan Position | 0–100 % | 20 % |
| Speed | 0–4× | 1 |

**Voice and play:**

| Parameter | Range | Default |
|---|---|---|
| Attack | 0.5 ms–2 s | 3 ms |
| Release | 5 ms–5 s | 0.3 s |
| Brightness | ±12 dB/oct | 0 |
| Harmonics | 1–128 | 128 |
| Noise | off, −60 to +12 dB | 0 dB |
| Velocity Sensitivity | 0–1 | 1 |
| Pitch Bend Range | 0–24 semitones | 2 |
| Polyphony | 1–32 | 16 |
| Level Lock | 0–100 % | 100 % |
| Gain | −48 to +12 dB | −12 dB |

**Random walk:**

| Parameter | Range | Default |
|---|---|---|
| Walk On | off/on | off |
| Walk Mode | Drift, Jumps, Tour, Neighbour Tour | Drift |
| Walk Amount | 0–4 SD | 1 |
| Walk Rate | 0.01–20 Hz | 0.25 Hz |
| Walk Sync, Walk Step | on/off; 1/16–16 bars | off; 1 bar |
| Walk Glide | 0–100 % | 70 % |
| Walk Tether | 0–100 % | 50 % |
| Walk Components | 1–32 | 4 |
| Walk Focus | Equal, Main components | Equal |
| Walk Per Voice | 0–100 % | 0 |
| Walk Seed | 1–9999 | 1 |
| Walk Restart On Note, Walk Freeze | off/on | off |

**LFOs and expression:**

| Parameter | Range | Default |
|---|---|---|
| LFO n On, Shape | off/on; 6 shapes | off; Sine |
| LFO n Rate | 0.01–20 Hz | 0.5 Hz / 0.13 Hz |
| LFO n Sync, Cycle | on/off; 1/16–16 bars | off; 1 bar |
| LFO n Depth | ±4 SD | 1 |
| LFO n Target | PC1–16, Toward Sound | PC1 / PC2 |
| Velocity, Mod Wheel, Aftertouch To | Off, PC1–16, Toward Sound | Off, Toward Sound, Off |
| … Amount | ±4 SD | 1 |
| Macro | 0–100 % | 0 |
| Voice Spread | 0–2 SD | 0 |

**MPE:**

| Parameter | Range | Default |
|---|---|---|
| MPE | off/on | off |
| MPE Zone | Lower, Upper | Lower |
| MPE Note Bend Range | 1–96 semitones | 48 |
| MPE Pressure To, Amount | Off, PC1–16, Toward Sound; ±4 SD | PC1; 1.5 |
| MPE Pressure Curve | −1 to 1 | 0 |
| MPE Smoothing | 1–200 ms | 20 ms |
| MPE Slide To, Amount | Off, PC1–16, Toward Sound; ±4 SD | PC2; 1 |
| MPE Slide Mode | Bipolar, Unipolar | Bipolar |
