#!/usr/bin/env bash
# Generates the synthetic training set, trains a model on it and renders the
# listening examples.
#   scripts/render_examples.sh [build-dir] [output-dir]
set -euo pipefail
BUILD="${1:-build}"
OUT="${2:-renders}"
mkdir -p "$OUT"
"$BUILD/tools/pcs-testgen" "$OUT/training" > /dev/null
"$BUILD/tools/pcs-train" -o "$OUT/synthetic.pcsm" "$OUT/training" | tee "$OUT/training.txt"
"$BUILD/tools/pcs-examples" "$OUT/synthetic.pcsm" "$OUT/training" "$OUT/examples"
