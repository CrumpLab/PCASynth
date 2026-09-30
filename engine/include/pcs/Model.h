#pragma once

#include "pcs/Harmonic.h"
#include "pcs/Pca.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace pcs {

constexpr int kMaxComponents = 64;
constexpr int kMaxModelHarmonics = 128;

// A trained PCA sound space: the mean sound plus principal components, and
// where every training sound sits in the space. A point in the space is given
// in z units (standard deviations of the training sounds' scores along each
// component), so ±2 covers most of the training set.
//
// Each sound is one vector with sections (Stage 7):
//   harmonics  numFrames × numHarmonics   levels in the chosen representation
//   loudness   numFrames                   (ShapeLoudness only) frame loudness × loudnessWeight
//   noise      numFrames × numNoiseBands   residual noise per band, same representation
//   partials   numHarmonics                (if hasPartials) cents from exact harmonics × partialWeight
//   pitch      numFrames                   (if hasPitchCurve) each frame's pitch, cents from f0 × pitchCurveWeight
// With pitch tracking, a direction (pitchSlope, per semitone) is regressed out
// before the PCA and added back for each note: timbre follows pitch.
struct Model
{
    std::string title;              // shown in the plugin, e.g. the file name
    AnalysisSettings analysis;
    int numFrames = 0;
    int numHarmonics = 0;
    int numNoiseBands = 0;
    bool hasPartials = false;
    Representation representation = Representation::Decibels;
    float loudnessWeight = 1.0f;
    float partialWeight = 1.0f;
    bool hasPitchCurve = false;
    float pitchCurveWeight = 1.0f;
    double frameRate = 100.0;
    float floorDb = -80.0f;
    float noiseCeilingDb = 0.0f;    // decoded noise is clamped here: the loudest noise in training + 6 dB
    std::vector<std::string> names; // training sounds
    std::vector<double> f0s;
    std::vector<double> gainsDb;
    PcaResult pca;

    // Pitch tracking.
    bool pitchTracking = false;
    double pitchRef = 60.0, pitchMin = 60.0, pitchMax = 60.0; // MIDI
    std::vector<float> pitchSlope;  // dims(), change per semitone

    // Layout.
    int harmonicOffset() const noexcept { return 0; }
    int loudnessOffset() const noexcept { return numFrames * numHarmonics; }
    int noiseOffset() const noexcept { return loudnessOffset() + (representation == Representation::ShapeLoudness ? numFrames : 0); }
    int partialOffset() const noexcept { return noiseOffset() + numFrames * numNoiseBands; }
    int pitchCurveOffset() const noexcept { return partialOffset() + (hasPartials ? numHarmonics : 0); }
    int dims() const noexcept { return pitchCurveOffset() + (hasPitchCurve ? numFrames : 0); }

    // How well the components reproduce the training sounds, from training:
    // the RMS difference (dB) between each sound's harmonic envelopes and its
    // reconstruction from all the components (fitErrorDb, per sound), and the
    // mean over sounds with the first K components (fitByComponents[K], K =
    // 0..numComponents). Empty for models trained before they existed.
    std::vector<float> fitErrorDb, fitByComponents;

    int numSounds() const noexcept { return static_cast<int> (names.size()); }
    int numComponents() const noexcept { return pca.numComponents; }
    double sd (int j) const { return std::sqrt (pca.variance[static_cast<size_t> (j)]); }
    double varianceExplained (int j) const { return pca.variance[static_cast<size_t> (j)] / pca.totalVariance; }
    double durationSeconds() const noexcept { return numFrames / frameRate; }
    int soundIndex (const std::string& name) const; // -1 if absent
    double soundPitch (int i) const { return 69.0 + 12.0 * std::log2 (f0s[static_cast<size_t> (i)] / 440.0); }

    // Semitones from pitchRef for a played note, as used by the pitch
    // direction: clamped to the training range ± an octave, times keytrack.
    float pitchDelta (double midiNote, float keytrack = 1.0f) const noexcept;

    // Training sound i's coordinates (z units), numComponents() values.
    std::vector<float> soundZ (int i) const;
    // z coordinates of any analysed sound (projection onto the components).
    std::vector<float> project (const HarmonicSound& sound) const;

    // The vector a sound becomes (the layout above).
    std::vector<float> encode (const HarmonicSound& sound) const;

    // Real-time safe decoders at the point `z` (numZ values; missing
    // components count as 0), `pitchDelta` semitones from pitchRef:
    // harmonic levels (dB, clamped to [floorDb, +12]) of `frame` ...
    void decodeFrame (int frame, const float* z, int numZ, float* outDb, float pitchDelta = 0.0f) const noexcept;
    // ... noise band levels (dB RMS, clamped) of `frame` (numNoiseBands values) ...
    void decodeNoiseFrame (int frame, const float* z, int numZ, float* outDb, float pitchDelta = 0.0f) const noexcept;
    // ... partial offsets in cents (numHarmonics values; zeros without partials) ...
    void decodePartials (const float* z, int numZ, float* outCents, float pitchDelta = 0.0f) const noexcept;
    // ... and the pitch in `frame`, cents from the fundamental (0 without a pitch curve).
    float decodePitch (int frame, const float* z, int numZ, float pitchDelta = 0.0f) const noexcept;
    // The whole sound at `z`.
    HarmonicSound decode (const std::vector<float>& z, float pitchDelta = 0.0f) const;

    // Loudness at `z` (dB): the loudest of kLevelProbes frames, in the units
    // analysis normalises by (harmonic amplitudes² + 2 × noise RMS²). For
    // level compensation; real-time safe with scratch buffers of numHarmonics
    // and numNoiseBands floats.
    static constexpr int kLevelProbes = 8;
    float levelDb (const float* z, int numZ, float pitchDelta, float* scratchH, float* scratchN) const noexcept;

    // Components pre-scaled by their SD (built by finalize()).
    std::vector<float> loadings;
    void finalize();

private:
    // mean + Σ z·loading + pitchDelta·slope over [offset, offset + count).
    void accumulate (int offset, int count, const float* z, int numZ, float pitchDelta, float* out) const noexcept;
    float toDb (float value) const noexcept; // a harmonic or noise value in the representation -> dB (before loudness)
};

// A model of one sound (no components): plays the analysed sound back as it is.
Model singleSoundModel (const HarmonicSound& sound, const AnalysisSettings& settings);

// Throws std::invalid_argument for fewer than two sounds or mismatched shapes.
Model trainModel (const std::vector<HarmonicSound>& sounds, const AnalysisSettings& settings, int maxComponents = kMaxComponents);

// Single-file binary format (.pcsm): magic, JSON header, float32 arrays.
std::vector<uint8_t> serializeModel (const Model& model);
Model deserializeModel (const uint8_t* data, size_t size); // throws std::runtime_error
void saveModel (const Model& model, const std::string& path);
Model loadModel (const std::string& path);

} // namespace pcs
