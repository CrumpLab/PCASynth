// pcs-train: analyses a set of WAV files (all playing the same note) and
// writes a PCA model (.pcsm) plus a CSV of every sound's coordinates.
#include "Render.h"

#include "pcs/Harmonic.h"
#include "pcs/Model.h"
#include "pcs/Wav.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {
void usage()
{
    std::cerr << "usage: pcs-train -o model.pcsm [options] <wav files or directories>\n"
                 "  --title T         name shown in the plugin (default: the file name)\n"
                 "  --note N|auto     MIDI note every sound plays (default 60 = C4), or detect each\n"
                 "  --duration S      seconds analysed from each onset (default 4)\n"
                 "  --harmonics H     harmonics tracked (default 64, max 128)\n"
                 "  --frame-rate R    envelope frames per second (default 100)\n"
                 "  --components K    components kept (default 32, max 32)\n"
                 "  --floor-db D      level floor in dB (default -80)\n"
                 "  --no-normalize    keep each sound's own loudness\n"
                 "  --no-trim         don't align sounds on their onsets\n";
}
} // namespace

int main (int argc, char** argv)
{
    pcs::AnalysisSettings s;
    int components = pcs::kMaxComponents;
    std::string outPath, title;
    std::vector<std::string> inputs;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto next = [&] { if (i + 1 >= argc) { usage(); std::exit (2); } return std::string (argv[++i]); };
        if (a == "-o") outPath = next();
        else if (a == "--title") title = next();
        else if (a == "--note")
        {
            const auto v = next();
            if (v == "auto")
                s.autoPitch = true;
            else
                s.midiNote = std::stoi (v);
        }
        else if (a == "--duration") s.duration = std::stod (next());
        else if (a == "--harmonics") s.harmonics = std::stoi (next());
        else if (a == "--frame-rate") s.frameRate = std::stod (next());
        else if (a == "--components") components = std::stoi (next());
        else if (a == "--floor-db") s.floorDb = std::stod (next());
        else if (a == "--no-normalize") s.normalizeLoudness = false;
        else if (a == "--no-trim") s.trimOnset = false;
        else if (! a.empty() && a[0] == '-') { usage(); return 2; }
        else inputs.push_back (a);
    }
    if (outPath.empty() || inputs.empty() || s.harmonics < 1 || s.harmonics > 128)
    {
        usage();
        return 2;
    }

    try
    {
        std::vector<pcs::HarmonicSound> sounds;
        for (const auto& path : pcs::tools::collectWavs (inputs))
        {
            sounds.push_back (pcs::analyseHarmonics (pcs::readWav (path), s, pcs::tools::stem (path)));
            const auto& h = sounds.back();
            const double note = 69.0 + 12.0 * std::log2 (h.f0 / 440.0);
            std::printf ("  %-24s f0 %8.2f Hz (MIDI %5.1f)\n", h.name.c_str(), h.f0, note);
        }
        auto model = pcs::trainModel (sounds, s, components);
        model.title = title.empty() ? pcs::tools::stem (outPath) : title;
        pcs::saveModel (model, outPath);

        std::printf ("\n%d sounds, %d frames x %d harmonics, %d components\n", model.numSounds(), model.numFrames,
                     model.numHarmonics, model.numComponents());
        double cumulative = 0.0;
        for (int j = 0; j < model.numComponents(); ++j)
        {
            cumulative += model.varianceExplained (j);
            std::printf ("  PC%-3d %5.1f %%   (cumulative %5.1f %%)\n", j + 1, 100.0 * model.varianceExplained (j), 100.0 * cumulative);
        }

        const auto csvPath = outPath + ".scores.csv";
        std::ofstream csv (csvPath);
        csv << "sound";
        for (int j = 0; j < model.numComponents(); ++j)
            csv << ",PC" << (j + 1);
        csv << "\n";
        for (int i = 0; i < model.numSounds(); ++i)
        {
            csv << model.names[static_cast<size_t> (i)];
            for (float z : model.soundZ (i))
                csv << "," << z;
            csv << "\n";
        }
        std::cout << "\nwrote " << outPath << " and " << csvPath << "\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << "pcs-train: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
