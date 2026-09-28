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

**Status: Stage 1.** The engine and offline tools are built and tested. The
plugin is Stage 2.

## Build

Requires CMake ≥ 3.22 and a C++20 compiler. Dependencies (nlohmann/json,
Catch2) are fetched by CMake.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build
```

## Tools

```sh
# Synthetic training set: 10 instrument families × 6 variations, all C4
build/tools/pcs-testgen training/

# Train a model on any folder of WAVs playing the same note
build/tools/pcs-train -o space.pcsm --note 60 training/

# Render a point: a training sound, a morph, or an offset along components
build/tools/pcs-render space.pcsm out.wav --sound reed_1 --to vowel_2 --amount 0.5 --notes 48,55,64 --mode loop
build/tools/pcs-render space.pcsm out.wav --z 2,-1,0.5 --length 4

# Everything at once: training set, model, listening examples and a map
scripts/render_examples.sh build renders
```

## Licence

AGPLv3 (see [`LICENSE`](LICENSE)).
