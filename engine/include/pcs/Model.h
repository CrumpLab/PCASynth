#pragma once

#include "pcs/Harmonic.h"
#include "pcs/Pca.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace pcs {

constexpr int kMaxComponents = 32;

// A trained PCA sound space: the mean harmonic envelope plus principal
// components, and where every training sound sits in the space. A point in
// the space is given in z units (standard deviations of the training sounds'
// scores along each component), so ±2 covers most of the training set.
struct Model
{
    AnalysisSettings analysis;
    int numFrames = 0;
    int numHarmonics = 0;
    double frameRate = 100.0;
    float floorDb = -80.0f;
    std::vector<std::string> names; // training sounds
    std::vector<double> f0s;
    std::vector<double> gainsDb;
    PcaResult pca;

    int dims() const noexcept { return numFrames * numHarmonics; }
    int numSounds() const noexcept { return static_cast<int> (names.size()); }
    int numComponents() const noexcept { return pca.numComponents; }
    double sd (int j) const { return std::sqrt (pca.variance[static_cast<size_t> (j)]); }
    double varianceExplained (int j) const { return pca.variance[static_cast<size_t> (j)] / pca.totalVariance; }
    double durationSeconds() const noexcept { return numFrames / frameRate; }
    int soundIndex (const std::string& name) const; // -1 if absent

    // Training sound i's coordinates (z units), numComponents() values.
    std::vector<float> soundZ (int i) const;
    // z coordinates of any analysed sound (projection onto the components).
    std::vector<float> project (const HarmonicSound& sound) const;

    // Harmonic levels (dB, clamped to [floorDb, +12]) of `frame` at the point
    // `z` (numZ values; missing components count as 0). Real-time safe.
    void decodeFrame (int frame, const float* z, int numZ, float* outDb) const noexcept;
    // The whole envelope at `z`.
    HarmonicSound decode (const std::vector<float>& z) const;

    // Components pre-scaled by their SD (built by finalize()).
    std::vector<float> loadings;
    void finalize();
};

// Throws std::invalid_argument for fewer than two sounds or mismatched shapes.
Model trainModel (const std::vector<HarmonicSound>& sounds, const AnalysisSettings& settings, int maxComponents = kMaxComponents);

// Single-file binary format (.pcsm): magic, JSON header, float32 arrays.
std::vector<uint8_t> serializeModel (const Model& model);
Model deserializeModel (const uint8_t* data, size_t size); // throws std::runtime_error
void saveModel (const Model& model, const std::string& path);
Model loadModel (const std::string& path);

} // namespace pcs
