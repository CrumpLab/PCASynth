#pragma once

#include "pcs/Model.h"
#include "pcs/Wav.h"

#include <string>
#include <vector>

// Inspection: how faithfully a model reproduces its training sounds, and
// where fidelity is lost, in the harmonic analysis or in the PCA.
namespace pcs {

// A spectrogram on an ear-like scale for comparing sounds: power in bands a
// sixth of an octave wide from 40 Hz up, 100 frames per second, in dB.
struct Spectrogram
{
    double frameRate = 100.0;
    double loHz = 40.0;
    int bandsPerOctave = 6;
    int numFrames = 0, numBands = 0;
    std::vector<float> db; // numFrames × numBands

    float at (int t, int b) const { return db[static_cast<size_t> (t * numBands + b)]; }
    double bandHz (int b) const; // band centre
};

Spectrogram spectrogram (const std::vector<float>& mono, double sampleRate, double frameRate = 100.0);

// How different two sounds are, roughly as heard: both spectrograms are set
// relative to their own loudest cell and floored 60 dB below it, then the RMS
// difference (dB) is taken over the cells where either is above the floor,
// in frames [fromSeconds, toSeconds) (toSeconds < 0: to the end).
// 0 = identical; a few dB is close; above ~8 dB the sounds clearly differ.
double spectralDistance (const Spectrogram& a, const Spectrogram& b, double fromSeconds = 0.0, double toSeconds = -1.0);

// RMS difference (dB) between two sounds' harmonic envelopes, over the cells
// where `reference` is within 60 dB of its loudest (both floored there).
double envelopeDistance (const HarmonicSound& reference, const HarmonicSound& other);

struct InspectOptions
{
    int components = -1;         // components used for the model's version (-1: all)
    double attackSeconds = 0.15; // the "attack" part of the scores
};

struct SoundInspection
{
    std::string name;
    double pitch = 0.0;           // MIDI (the analysed fundamental)
    HarmonicSound analysed;       // the file analysed with the model's settings
    // Mono, the same length, loudness-matched to the original:
    AudioBuffer original;         // the file from its onset, for the analysed duration
    AudioBuffer analysis;         // the analysis played back as it is (no PCA)
    AudioBuffer model;            // the sound's point in the model, played back
    Spectrogram specOriginal, specAnalysis, specModel;
    // Spectral distances (dB, see spectralDistance), whole sound and attack:
    double analysisError = 0.0, analysisAttackError = 0.0; // original vs analysis: what the harmonic model loses
    double modelError = 0.0, modelAttackError = 0.0;       // original vs model: everything
    double pcaError = 0.0;                                 // analysis vs model: what the PCA loses
    double envelopeError = 0.0;   // harmonic envelopes, analysis vs model (dB)
    std::vector<float> pitchCents, modelPitchCents; // pitch curves (cents from the fundamental; empty if not tracked)
};

// Inspects training sound `index` of `model`, given its audio file. Throws
// std::runtime_error if the file cannot be analysed with the model's settings.
SoundInspection inspectSound (const Model& model, int index, const AudioBuffer& file, const InspectOptions& options = {});

// Plays `model` at the point `z` (z units) once through its envelope, at
// `midiPitch` exactly, mono. Offline.
AudioBuffer renderPoint (const Model& model, const std::vector<float>& z, double midiPitch, double sampleRate);

} // namespace pcs
