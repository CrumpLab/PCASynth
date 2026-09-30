// pcs-train: analyses a set of WAV files (all playing the same note) and
// writes a PCA model (.pcsm) plus a CSV of every sound's coordinates.
#include "Render.h"

#include "pcs/Harmonic.h"
#include "pcs/Model.h"
#include "pcs/WaveModel.h"
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
                 "  --components K    components kept (default 64, max 64)\n"
                 "  --floor-db D      level floor in dB (default -80)\n"
                 "  --noise-bands N   residual noise bands (default 16; 0 = harmonics only)\n"
                 "  --no-partials     treat partials as exact harmonics (no inharmonicity)\n"
                 "  --representation R  db | shape | linear (default db)\n"
                 "  --pitch-tracking P  auto | on | off (default auto: on when sounds span 3+ semitones)\n"
                 "  --no-pitch-curve  don't follow the pitch frame by frame (vibrato, glides)\n"
                 "  --no-sharp-attacks  analyse onsets with the same long window as the rest\n"
                 "  --attack S        length of the sharp-attack part (default 0.15 s)\n"
                 "  --no-normalize    keep each sound's own loudness\n"
                 "waveform spaces (PCAWave): PCA on the waveforms themselves, written as .pcsw\n"
                 "  --waveform        train a waveform space (--note, --duration, --components,\n"
                 "                    --no-normalize and --no-trim apply)\n"
                 "  --rate HZ         the waveform space's sample rate (default 48000)\n"
                 "  --no-align-pitch  keep each sound at its own pitch\n"
                 "  --no-align-phase  don't shift sounds to line up their waveforms\n"
                 "  --no-trim         don't align sounds on their onsets\n";
}
} // namespace

int main (int argc, char** argv)
{
    pcs::AnalysisSettings s;
    int components = pcs::kMaxComponents;
    std::string outPath, title;
    bool waveform = false, durationSet = false;
    pcs::WaveSettings ws;
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
        else if (a == "--duration") { s.duration = std::stod (next()); durationSet = true; }
        else if (a == "--waveform") waveform = true;
        else if (a == "--rate") ws.sampleRate = std::stod (next());
        else if (a == "--no-align-pitch") ws.alignPitch = false;
        else if (a == "--no-align-phase") ws.alignPhase = false;
        else if (a == "--harmonics") s.harmonics = std::stoi (next());
        else if (a == "--frame-rate") s.frameRate = std::stod (next());
        else if (a == "--no-pitch-curve") s.trackPitch = false;
        else if (a == "--no-sharp-attacks") s.sharpAttacks = false;
        else if (a == "--attack") s.attackSeconds = std::stod (next());
        else if (a == "--components") components = std::stoi (next());
        else if (a == "--floor-db") s.floorDb = std::stod (next());
        else if (a == "--noise-bands") s.noiseBands = std::stoi (next());
        else if (a == "--no-partials") s.trackPartials = false;
        else if (a == "--representation")
        {
            const auto r = next();
            s.representation = r == "shape" ? pcs::Representation::ShapeLoudness : r == "linear" ? pcs::Representation::Linear : pcs::Representation::Decibels;
        }
        else if (a == "--pitch-tracking")
        {
            const auto t = next();
            s.pitchTracking = t == "on" ? pcs::PitchTracking::On : t == "off" ? pcs::PitchTracking::Off : pcs::PitchTracking::Auto;
        }
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

    if (waveform)
    {
        ws.midiNote = s.midiNote;
        ws.autoPitch = s.autoPitch;
        if (durationSet)
            ws.duration = s.duration;
        ws.normalizeLoudness = s.normalizeLoudness;
        ws.trimOnset = s.trimOnset;
        try
        {
            std::vector<pcs::AudioBuffer> audio;
            std::vector<std::string> names;
            for (const auto& path : pcs::tools::collectWavs (inputs))
            {
                audio.push_back (pcs::readWav (path));
                names.push_back (pcs::tools::stem (path));
            }
            auto model = pcs::trainWaveModel (audio, names, ws, components);
            model.title = title.empty() ? pcs::tools::stem (outPath) : title;
            pcs::saveSpace (model, outPath);
            for (int i = 0; i < model.numSounds(); ++i)
                std::printf ("  %-24s f0 %8.2f Hz  shift %+3d  residual %6.1f dB\n", model.names[static_cast<size_t> (i)].c_str(),
                             model.f0s[static_cast<size_t> (i)], model.shifts[static_cast<size_t> (i)], model.fitErrorDb[static_cast<size_t> (i)]);
            std::printf ("\n%d sounds, %d samples at %.0f Hz (common pitch %.2f Hz), %d components\n", model.numSounds(),
                         model.numSamples, model.sampleRate, model.refHz, model.numComponents());
            double cumulative = 0.0;
            for (int j = 0; j < model.numComponents(); ++j)
            {
                cumulative += model.varianceExplained (j);
                std::printf ("  PC%-3d %5.1f %%   (cumulative %5.1f %%)\n", j + 1, 100.0 * model.varianceExplained (j), 100.0 * cumulative);
            }
            std::cout << "\nwrote " << outPath << "\n";
        }
        catch (const std::exception& e)
        {
            std::cerr << "pcs-train: " << e.what() << "\n";
            return 1;
        }
        return 0;
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

        std::printf ("\n%d sounds, %d frames x %d harmonics + %d noise bands%s, %d components\n", model.numSounds(), model.numFrames,
                     model.numHarmonics, model.numNoiseBands, model.hasPartials ? " + partial tuning" : "", model.numComponents());
        if (model.pitchTracking)
            std::printf ("pitch tracking: timbre follows pitch (training notes %.1f-%.1f, reference %.1f)\n", model.pitchMin,
                         model.pitchMax, model.pitchRef);
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
