#pragma once

#include "pcs/Wav.h"

#include <string>
#include <vector>

namespace pcs {

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
};

// One sound as harmonic amplitude envelopes: db[frame * harmonics + h] is the
// level (dB) of harmonic h+1 in that frame.
struct HarmonicSound
{
    std::string name;
    double f0 = 0.0;      // detected fundamental (Hz)
    double gainDb = 0.0;  // normalisation gain that was applied
    double frameRate = 100.0;
    int numFrames = 0;
    int numHarmonics = 0;
    std::vector<float> db;

    float at (int frame, int h) const { return db[static_cast<size_t> (frame * numHarmonics + h)]; }
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

// Throws std::runtime_error if the sound is silent.
HarmonicSound analyseHarmonics (const AudioBuffer& audio, const AnalysisSettings& settings, const std::string& name = {});

} // namespace pcs
