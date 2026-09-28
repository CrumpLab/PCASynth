# PCASynth

An experimental synthesizer (VST3 for macOS) built on a **principal
components analysis of recorded notes**. Train it on a folder of WAVs of
different instruments all playing the same note. Each sound becomes a point
in a learned space, and you can play any point: the originals, morphs between
them, or places no instrument has been.

Sounds are analysed into **harmonic amplitude envelopes** (how loud each
harmonic is, every 10 ms), and PCA runs on those. Notes are resynthesised by a
bank of sine oscillators, so any pitch plays the learned timbre. See
[`plan.md`](plan.md) for the concept, the design and the staged build plan.

By [Matthew Crump](https://crumplab.com), Brooklyn College of CUNY.

**Status: Stage 2 (version 0.2.0).** A playable VST3 (plus a Standalone
app) with a built-in space of 60 synthetic instrument notes. You can load
your own models, and a model is saved inside your DAW project. It has not
yet been played in a DAW. The custom UI (sound map, morph pad) is Stage 3.

![The plugin window](docs/screenshot.png)

## Install (macOS)

Download `PCASynth-macOS` from the latest successful
[build](../../actions/workflows/build.yml) run (or a release). Then either:

- run the `.pkg`, which installs `PCASynth.vst3` into
  `/Library/Audio/Plug-Ins/VST3`, or
- copy `PCASynth.vst3` from the zip into `~/Library/Audio/Plug-Ins/VST3`.

Builds are ad-hoc signed. If macOS blocks the plugin, run
`xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/PCASynth.vst3`.
Rescan plug-ins in your DAW; PCASynth appears under CrumpLab as an
instrument. The zip also has a Standalone app for trying it without a DAW.

## Playing it

- **Jump to** picks a training sound. The PC sliders move to its coordinates,
  and from there you can move away along any component.
- **PC1–PC16** are the point in the space, in standard deviations of the
  training sounds. ±2 covers most of the set; ±4 goes beyond it.
- **Play Mode:**
  - **Loop** (default) sustains held notes between Loop Start and Loop End.
  - **One-shot** plays the recorded envelope once.
  - **Ping-pong** plays back and forth between the loop points.
  - **Scan** holds one moment of the sound (automate Scan Position).
- **Exaggerate** scales the whole point (0 = the average sound, 2 = twice as
  far out). **Components Used** keeps only the first K components.
- **Load Model…** or drop a `.pcsm` file on the window to play your own space.

## Build

Requires CMake ≥ 3.22 and a C++20 compiler. Dependencies (JUCE,
nlohmann/json, Catch2) are fetched by CMake. On Linux JUCE needs the ALSA,
X11 and freetype development packages (see `.github/workflows/build.yml`).
`-DPCS_BUILD_PLUGIN=OFF` builds only the engine and tools.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build
```

## Tools

```sh
# Synthetic training set: 10 instrument families × 6 variations, all C4
build/tools/pcs-testgen training/

# Train a model on any folder of WAVs playing the same note (then load it in the plugin)
build/tools/pcs-train -o space.pcsm --title "My space" --note 60 training/

# Render a point: a training sound, a morph, or an offset along components
build/tools/pcs-render space.pcsm out.wav --sound reed_1 --to vowel_2 --amount 0.5 --notes 48,55,64 --mode loop
build/tools/pcs-render space.pcsm out.wav --z 2,-1,0.5 --length 4

# Everything at once: training set, model, listening examples and a map
scripts/render_examples.sh build renders
```

## Licence

AGPLv3 (see [`LICENSE`](LICENSE)).
