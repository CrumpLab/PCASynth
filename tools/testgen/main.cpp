// pcs-testgen: writes the synthetic training set (one WAV per sound).
#include "TrainingSet.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

int main (int argc, char** argv)
{
    pcs::testgen::Options o;
    std::string dir;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto next = [&] { if (i + 1 >= argc) { std::cerr << a << " needs a value\n"; std::exit (2); } return std::string (argv[++i]); };
        if (a == "--note") o.midiNote = std::stoi (next());
        else if (a == "--duration") o.duration = std::stod (next());
        else if (a == "--variations") o.variations = std::stoi (next());
        else if (a == "--seed") o.seed = static_cast<uint32_t> (std::stoul (next()));
        else if (a == "--sample-rate") o.sampleRate = std::stod (next());
        else if (a == "--detune") o.detuneCents = std::stod (next());
        else if (a[0] != '-' && dir.empty()) dir = a;
        else
        {
            std::cerr << "usage: pcs-testgen [--note 60] [--duration 4] [--variations 6] [--seed 1] [--sample-rate 48000]\n"
                         "                   [--detune 8] <output-dir>\n";
            return 2;
        }
    }
    if (dir.empty())
    {
        std::cerr << "pcs-testgen: no output directory\n";
        return 2;
    }
    std::filesystem::create_directories (dir);
    for (const auto& clip : pcs::testgen::generateTrainingSet (o))
    {
        const auto path = (std::filesystem::path (dir) / (clip.name + ".wav")).string();
        pcs::writeWav (path, clip.audio, pcs::WavFormat::Pcm24);
        std::cout << path << "\n";
    }
    return 0;
}
