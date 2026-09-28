# Changelog

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
