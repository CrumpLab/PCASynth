#pragma once

#include "pcs/Wav.h"

#include <cstdint>
#include <string>
#include <vector>

namespace pcs::testgen {

// Deterministic synthetic instrument notes for training and testing. The same
// options give bit-identical audio on every platform (own RNG).
struct Options
{
    double sampleRate = 48000.0;
    int midiNote = 60;           // every sound plays this note (C4)
    double duration = 4.0;       // seconds per sound
    int variations = 6;          // sounds per family
    uint32_t seed = 1;
    double detuneCents = 8.0;    // each sound is detuned by up to ± this (real samples are never exact)
};

struct Clip
{
    std::string name;    // file stem, e.g. "reed_3"
    std::string family;  // e.g. "reed"
    AudioBuffer audio;
};

// Families: pluck (Karplus-Strong), bowed, reed, brass, flute (with breath
// noise), organ, mallet, epiano (FM), vowel (formants), piano (inharmonic).
std::vector<std::string> families();
std::vector<Clip> generateTrainingSet (const Options& o);
Clip generate (const std::string& family, int variation, const Options& o);

} // namespace pcs::testgen
