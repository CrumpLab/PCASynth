#pragma once

#include "pcs/Wav.h"

#include <cmath>
#include <string>
#include <vector>

namespace pcs {

// How a model turns envelopes into the vectors its PCA sees (Stage 7).
enum class Representation
{
    Decibels,      // levels in dB (floor-clamped): morphs blend spectral shapes
    ShapeLoudness, // per-frame loudness separated from the spectrum's shape
    Linear,        // linear amplitudes: morphs behave more like crossfades
};

enum class PitchTracking
{
    Off,  // one timbre for every note
    Auto, // on when the training sounds span 3 semitones or more
    On,   // learn how timbre changes with pitch (a direction regressed out before the PCA)
};

// How training sounds are turned into harmonic amplitude envelopes.
struct AnalysisSettings
{
    int midiNote = 60;               // nominal pitch of every training sound (60 = C4)
    bool autoPitch = false;          // detect each sound's pitch instead (sounds may differ)
    double tuneSearchCents = 60.0;   // each sound's f0 is searched within ± this of the nominal
    double duration = 4.0;           // seconds analysed, from the onset
    double frameRate = 100.0;        // envelope frames per second
    int harmonics = 64;              // harmonics tracked (above Nyquist they sit at the floor)
    double floorDb = -80.0;          // amplitudes are clamped to this (relative to the loudest frame)
    double periodsPerWindow = 8.0;   // analysis window length in periods of f0
    bool trimOnset = true;           // align sounds on their first sample above onsetThresholdDb
    double onsetThresholdDb = -40.0; // relative to the sound's peak
    bool normalizeLoudness = true;   // loudest frame of every sound at 0 dB
    // Stage 7
    int noiseBands = 16;             // residual noise bands (0: harmonics only)
    bool trackPartials = true;       // follow stretched (inharmonic) partials
    Representation representation = Representation::Decibels;
    PitchTracking pitchTracking = PitchTracking::Auto;
    // Fidelity
    bool trackPitch = true;          // follow the pitch frame by frame (vibrato, glides, pitch drops): a pitch curve
    bool sharpAttacks = true;        // onset frames also use a window half as long, for crisper attacks
    double attackSeconds = 0.15;     // how long the onset (short-window) part lasts
};

// Noise bands are fixed ratios of the fundamental (so they move with the
// played note, like the harmonics): log-spaced from 0.5 × f0 to 96 × f0.
// Edge b of `bands` (b = 0..bands).
double noiseBandEdge (int b, int bands) noexcept;

// One sound as harmonic amplitude envelopes: db[frame * harmonics + h] is the
// level (dB) of harmonic h+1 in that frame (its peak amplitude). noiseDb
// holds the residual noise (RMS, dB) per band and frame, and partialCents how
// far each partial sits from an exact harmonic.
struct HarmonicSound
{
    std::string name;
    double f0 = 0.0;      // detected fundamental (Hz)
    double gainDb = 0.0;  // normalisation gain that was applied
    double frameRate = 100.0;
    int numFrames = 0;
    int numHarmonics = 0;
    std::vector<float> db;
    int numNoiseBands = 0;
    std::vector<float> noiseDb;      // numFrames × numNoiseBands
    std::vector<float> partialCents; // numHarmonics (empty: exact harmonics)
    std::vector<float> pitchCents;   // numFrames: the pitch in each frame, cents from f0 (empty: not tracked)
    double onsetSeconds = 0.0;       // where frame 0 sits in the file (not saved in models)

    float at (int frame, int h) const { return db[static_cast<size_t> (frame * numHarmonics + h)]; }
    float noiseAt (int frame, int b) const { return noiseDb[static_cast<size_t> (frame * numNoiseBands + b)]; }
    double midiPitch() const noexcept { return 69.0 + 12.0 * std::log2 (f0 / 440.0); }
};

double midiToHz (double note) noexcept;

// Mono mix (mean of channels).
std::vector<double> monoMix (const AudioBuffer& audio);

// Refines `nominalHz` within ± searchCents by maximising the harmonic sum of
// the long-term spectrum of `mono` (from `start`, up to one second).
double estimateF0 (const std::vector<double>& mono, double sampleRate, size_t start, double nominalHz,
                   double searchCents, int harmonics);

// Pitch (Hz) between MIDI notes lowNote and highNote: the median of YIN
// estimates over windows spread across the first second after `start`.
// Returns 0 if no window is voiced.
double detectPitch (const std::vector<double>& mono, double sampleRate, size_t start, double lowNote = 24.0,
                    double highNote = 96.0);

// Frequencies (Hz) of partials 1..count, found one after another on the
// long-term spectrum: each is searched around the previous one plus the last
// spacing, so stretched (piano-like) partials are followed. Partials with no
// clear peak continue the spacing. Exact harmonics give h × f0.
std::vector<double> trackPartials (const std::vector<double>& mono, double sampleRate, size_t start, double f0, int count);

// Throws std::runtime_error if the sound is silent.
HarmonicSound analyseHarmonics (const AudioBuffer& audio, const AnalysisSettings& settings, const std::string& name = {});

} // namespace pcs
