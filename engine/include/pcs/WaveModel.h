#pragma once

#include "pcs/Space.h"
#include "pcs/Wav.h"

#include <string>
#include <vector>

// A sound space made from the waveforms themselves (the PCAWave plugin): every
// training sound, lined up in pitch, onset and phase, is one long vector of
// samples, and PCA runs on those. Any point is a weighted sum of the training
// waveforms, so a morph between two sounds is a mix of them: a linear morph
// synthesizer. With all components every training sound comes back exactly.
namespace pcs {

struct WaveSettings
{
    int midiNote = 60;              // what every sound plays (with autoPitch: detected, and they may differ)
    bool autoPitch = false;
    double tuneSearchCents = 60.0;  // each sound's pitch is refined within ± this of midiNote
    double duration = 3.0;          // seconds kept from each onset
    double sampleRate = 48000.0;    // the model's rate (lower = smaller model, less top end)
    bool alignPitch = true;         // resample every sound to one common pitch
    bool trimOnset = true;          // start every sound at its onset
    double onsetThresholdDb = -40.0;
    bool alignPhase = true;         // shift each sound (within a period) to line up with the ones before it
    bool normalizeLoudness = true;  // the loudest 50 ms of every sound at the same level
    double fadeSeconds = 0.05;      // fade-out at the end of the kept duration
};

// One training sound as the PCA sees it: at the model's rate and common pitch,
// aligned, normalised, `duration` long.
struct PreparedWave
{
    std::string name;
    double f0 = 0.0;        // the sound's own pitch (Hz)
    double gainDb = 0.0;    // normalisation applied
    double onsetSeconds = 0.0;
    int shift = 0;          // phase alignment (samples, at the model's rate)
    std::vector<float> samples;
};

struct WaveModel final : Space
{
    WaveSettings settings;
    double sampleRate = 48000.0;
    int numSamples = 0;
    double refHz = 261.63;  // the common pitch every sound was brought to
    std::vector<int> shifts; // each sound's phase alignment (samples), as training found it
    double refMidi() const noexcept { return 69.0 + 12.0 * std::log2 (refHz / 440.0); }

    const char* fitMeasure() const noexcept override { return "waveform residual (dB below the sound)"; }
    double durationSeconds() const noexcept override { return numSamples / sampleRate; }

    // The waveform at the point `z` (numZ values; missing components count as 0).
    std::vector<float> decode (const float* z, int numZ) const;
    // Training sound i as a weighted sum of all the training waveforms: the
    // weights that make the point `z` (numSounds() values, summing to 1).
    std::vector<double> mixWeights (const float* z, int numZ) const;

    // Loudness at `z` (dB): the energy of its first levelSeconds, from a
    // quadratic form precomputed by finalize(), so it costs O(K²).
    static constexpr double kLevelSeconds = 0.5;
    float levelDb (const float* z, int numZ) const noexcept;
    float refLevelDb = 0.0f; // the training sounds' mean levelDb

    // Built by finalize(): the level quadratic form over the first kLevelSeconds.
    std::vector<double> levelGram, levelLinear; // K×K, K
    double levelConst = 0.0;
    void finalize();
};

// Lines a sound up for training: mono, onset, pitch (resampled to targetHz at
// the model's rate), loudness. `phaseReference` (may be empty) is what the
// phase alignment matches against. Throws std::runtime_error if the sound is
// silent or has no pitch (with autoPitch).
// `forcedShift` (if not null) replaces the search: re-preparing a training sound as it was.
PreparedWave prepareWave (const AudioBuffer& audio, const WaveSettings& settings, double targetHz,
                          const std::vector<float>& phaseReference, const std::string& name, const int* forcedShift = nullptr);

// A space of one prepared sound (no components): plays it back as it is.
WaveModel singleWaveModel (const PreparedWave& sound, const WaveModel& like);

// Each sound's pitch as training would find it (Hz): detected with autoPitch,
// else refined near midiNote. Throws like prepareWave.
double wavePitch (const AudioBuffer& audio, const WaveSettings& settings);

// Trains on the files' audio (at least two). The common pitch is midiNote, or
// with autoPitch the median of the sounds' pitches.
WaveModel trainWaveModel (const std::vector<AudioBuffer>& sounds, const std::vector<std::string>& names,
                          const WaveSettings& settings, int maxComponents = kMaxComponents);

std::vector<uint8_t> serializeWaveModel (const WaveModel& model);
WaveModel deserializeWaveModel (const uint8_t* data, size_t size); // throws std::runtime_error

} // namespace pcs
