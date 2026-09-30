// pcs-inspect: how faithfully a model reproduces its training sounds. For each
// training sound found among the given files it compares the original, its
// harmonic analysis played back without PCA, and its point in the model.
#include "Render.h"

#include "pcs/Inspect.h"
#include "pcs/Model.h"
#include "pcs/Wav.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {
void usage()
{
    std::cerr << "usage: pcs-inspect model.pcsm [options] <training wav files or directories>\n"
                 "  --components K   use only the first K components for the model's version\n"
                 "  --out DIR        also write <name>_original/_analysis/_model.wav there\n"
                 "Scores are spectral distances in dB (0 = identical, above ~8 = clearly different):\n"
                 "  analysis  original vs its analysis played back (what the harmonic model loses)\n"
                 "  model     original vs the model at the sound's point (everything)\n"
                 "  pca       analysis vs model (what the PCA loses); env = the same on the envelopes\n"
                 "            (waveform spaces: analysis = the aligned sound the PCA saw; env = its residual, dB)\n"
                 "  ...@att   the same over the first 150 ms\n";
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        usage();
        return 2;
    }
    std::string outDir;
    pcs::InspectOptions options;
    std::vector<std::string> inputs;
    for (int i = 2; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--components" && i + 1 < argc)
            options.components = std::atoi (argv[++i]);
        else if (a == "--out" && i + 1 < argc)
            outDir = argv[++i];
        else
            inputs.push_back (a);
    }
    try
    {
        const std::shared_ptr<const pcs::Space> space = pcs::loadSpace (argv[1]);
        const auto& model = *space;
        std::map<std::string, std::string> files;
        for (const auto& f : pcs::tools::collectWavs (inputs))
            files[pcs::tools::stem (f)] = f;
        if (! outDir.empty())
            std::filesystem::create_directories (outDir);

        std::printf ("%-18s %6s %9s %9s %9s %9s %7s %7s\n", "sound", "pitch", "analysis", "@att", "model", "@att", "pca", "env");
        double sums[6] = {};
        int count = 0;
        for (int i = 0; i < model.numSounds(); ++i)
        {
            const auto& name = model.names[static_cast<size_t> (i)];
            const auto it = files.find (name);
            if (it == files.end())
            {
                std::printf ("%-18s (no file)\n", name.c_str());
                continue;
            }
            const auto r = pcs::inspectSound (space, i, pcs::readWav (it->second), options);
            std::printf ("%-18s %6.1f %9.2f %9.2f %9.2f %9.2f %7.2f %7.2f\n", name.c_str(), r.pitch, r.analysisError,
                         r.analysisAttackError, r.modelError, r.modelAttackError, r.pcaError, r.envelopeError);
            const double v[6] = { r.analysisError, r.analysisAttackError, r.modelError, r.modelAttackError, r.pcaError, r.envelopeError };
            for (int k = 0; k < 6; ++k)
                sums[k] += v[k];
            ++count;
            if (! outDir.empty())
            {
                const auto base = (std::filesystem::path (outDir) / name).string();
                pcs::writeWav (base + "_original.wav", r.original, pcs::WavFormat::Pcm24);
                pcs::writeWav (base + "_analysis.wav", r.analysis, pcs::WavFormat::Pcm24);
                pcs::writeWav (base + "_model.wav", r.model, pcs::WavFormat::Pcm24);
            }
        }
        if (count > 0)
            std::printf ("%-18s %6s %9.2f %9.2f %9.2f %9.2f %7.2f %7.2f\n", "MEAN", "", sums[0] / count, sums[1] / count,
                         sums[2] / count, sums[3] / count, sums[4] / count, sums[5] / count);
    }
    catch (const std::exception& e)
    {
        std::cerr << "pcs-inspect: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
