#pragma once

#include "pcs/Pca.h"

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace pcs {

constexpr int kMaxComponents = 64;

// What every trained sound space has, whatever its sounds are made of: the
// training sounds and where they sit, the principal components, and how well
// the components reproduce the sounds. A point in a space is given in z units
// (standard deviations of the training sounds' scores along each component),
// so ±2 covers most of the training set.
//
// Two kinds exist: Model (harmonic envelopes, the PCASynth plugin) and
// WaveModel (the waveforms themselves, the PCAWave plugin).
struct Space
{
    virtual ~Space() = default;

    std::string title;              // shown in the plugin, e.g. the file name
    std::vector<std::string> names; // training sounds
    std::vector<double> f0s;        // each training sound's fundamental (Hz)
    std::vector<double> gainsDb;    // normalisation gain applied to each
    PcaResult pca;

    // How well the components reproduce the training sounds, from training:
    // an error (dB) for each sound with all the components (fitErrorDb), and
    // the mean over sounds with the first K components (fitByComponents[K],
    // K = 0..numComponents). What the error measures depends on the kind
    // (fitMeasure). Empty for models trained before they existed.
    std::vector<float> fitErrorDb, fitByComponents;
    virtual const char* fitMeasure() const noexcept = 0;

    virtual double durationSeconds() const noexcept = 0;

    int numSounds() const noexcept { return static_cast<int> (names.size()); }
    int numComponents() const noexcept { return pca.numComponents; }
    double sd (int j) const { return std::sqrt (pca.variance[static_cast<size_t> (j)]); }
    double varianceExplained (int j) const { return pca.variance[static_cast<size_t> (j)] / pca.totalVariance; }
    int soundIndex (const std::string& name) const; // -1 if absent
    double soundPitch (int i) const { return 69.0 + 12.0 * std::log2 (f0s[static_cast<size_t> (i)] / 440.0); }

    // Training sound i's coordinates (z units), numComponents() values.
    std::vector<float> soundZ (int i) const;
};

// Either kind from its file bytes (.pcsm or .pcsw), told apart by the magic
// at the start. Throws std::runtime_error.
std::shared_ptr<Space> deserializeSpace (const uint8_t* data, size_t size);
std::vector<uint8_t> serializeSpace (const Space& space);
std::shared_ptr<Space> loadSpace (const std::string& path);
void saveSpace (const Space& space, const std::string& path);

} // namespace pcs
