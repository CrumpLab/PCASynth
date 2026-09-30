#!/usr/bin/env bash
# Generates the synthetic training set, trains a harmonic model and a waveform
# space (PCAWave) on it, and renders the listening examples.
#   scripts/render_examples.sh [build-dir] [output-dir]
set -euo pipefail
BUILD="${1:-build}"
OUT="${2:-renders}"
mkdir -p "$OUT"
"$BUILD/tools/pcs-testgen" "$OUT/training" > /dev/null
"$BUILD/tools/pcs-train" -o "$OUT/synthetic.pcsm" "$OUT/training" | tee "$OUT/training.txt"
"$BUILD/tools/pcs-examples" "$OUT/synthetic.pcsm" "$OUT/training" "$OUT/examples"
# How faithfully the model reproduces each training sound (see pcs-inspect).
"$BUILD/tools/pcs-inspect" "$OUT/synthetic.pcsm" "$OUT/training" | tee "$OUT/inspect.txt"

# PCAWave: a waveform space of the same sounds (as the plugin's factory space,
# at the full rate and all components), how faithfully it reproduces them, and
# a few morphs.
"$BUILD/tools/pcs-train" --waveform -o "$OUT/synthetic.pcsw" "$OUT/training" | tee "$OUT/training-wave.txt"
"$BUILD/tools/pcs-inspect" "$OUT/synthetic.pcsw" "$OUT/training" | tee "$OUT/inspect-wave.txt"
mkdir -p "$OUT/examples/wave"
R="$BUILD/tools/pcs-render"
"$R" "$OUT/synthetic.pcsw" "$OUT/examples/wave/01_mean.wav" --length 2
"$R" "$OUT/synthetic.pcsw" "$OUT/examples/wave/02_reed_1.wav" --sound reed_1 --length 2
"$R" "$OUT/synthetic.pcsw" "$OUT/examples/wave/03_reed_to_vowel.wav" --sound reed_1 --to vowel_1 --amount 0.5 --length 2
"$R" "$OUT/synthetic.pcsw" "$OUT/examples/wave/04_bowed_chord_loop.wav" --sound bowed_1 --notes 48,55,64 --mode loop --loop 0.3,0.7 --length 4
"$R" "$OUT/synthetic.pcsw" "$OUT/examples/wave/05_scan_grain.wav" --sound vowel_1 --mode scan --scan 0.4 --length 3
"$R" "$OUT/synthetic.pcsw" "$OUT/examples/wave/06_tour.wav" --walk tour --walk-amount 1 --walk-rate 0.7 --walk-glide 0.6 --mode loop --length 8
"$R" "$OUT/synthetic.pcsw" "$OUT/examples/wave/07_exaggerated.wav" --sound epiano_2 --exaggerate 1.6 --level-lock 1 --length 2
