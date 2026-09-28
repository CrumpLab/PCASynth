#pragma once

#include "pcs/Harmonic.h"
#include "pcs/Model.h"
#include "pcs/Synth.h"
#include "pcs/Wav.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace pcs::tools {

struct Note
{
    double start = 0.0;   // seconds
    double length = 1.0;  // seconds until note-off
    int note = 60;
    float velocity = 0.8f;
};

// Called before every block with the block's start time; may change params
// (moves in the space, loop points, ...).
using Automation = std::function<void (double time, SynthParams& params)>;

// Renders `notes` through a Synth, offline, mono.
AudioBuffer renderNotes (std::shared_ptr<const Model> model, const SynthParams& params, const std::vector<Note>& notes,
                         double seconds, double sampleRate = 48000.0, const Automation& automation = {});

// Plays an analysed envelope back directly (no PCA): the harmonic model's own
// ceiling on quality.
std::shared_ptr<Model> singleSoundModel (const HarmonicSound& sound, const AnalysisSettings& settings);

// Audio files: every .wav in each directory (sorted), plus files given directly.
std::vector<std::string> collectWavs (const std::vector<std::string>& paths);
std::string stem (const std::string& path);

void appendSilence (AudioBuffer& dst, double seconds);
void append (AudioBuffer& dst, const AudioBuffer& src);
void normalisePeak (AudioBuffer& a, float peak);
std::vector<float> parseFloats (const std::string& csv);

} // namespace pcs::tools
