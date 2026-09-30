// pcs-examples: renders the listening examples for a model trained on a
// folder of sounds, plus a map of the sound space (SVG).
#include "Render.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace pcs;
using namespace pcs::tools;

namespace {

constexpr double kTwoPi = 6.28318530717958647692;

std::shared_ptr<const Model> model;
AnalysisSettings settings;
fs::path outDir;
std::ofstream notes;

void write (const std::string& name, AudioBuffer audio, const std::string& description)
{
    normalisePeak (audio, 0.7f);
    writeWav ((outDir / (name + ".wav")).string(), audio, WavFormat::Pcm24);
    notes << name << ".wav\n    " << description << "\n\n";
    std::cout << "  " << name << ".wav\n";
}

std::vector<float> zOf (const std::string& name) { return model->soundZ (model->soundIndex (name)); }

SynthParams at (const std::vector<float>& z)
{
    SynthParams p;
    for (size_t j = 0; j < z.size() && j < p.z.size(); ++j)
        p.z[j] = z[j];
    return p;
}

std::vector<float> lerp (const std::vector<float>& a, const std::vector<float>& b, float t)
{
    std::vector<float> z (a.size());
    for (size_t j = 0; j < a.size(); ++j)
        z[j] = a[j] + t * (b[j] - a[j]);
    return z;
}

// One full one-shot note of the model's length.
AudioBuffer oneNote (const SynthParams& p, int note = 60)
{
    const double d = model->durationSeconds();
    auto a = renderNotes (model, p, { { 0.0, d, note, 0.8f } }, d + 0.1);
    normalisePeak (a, 0.7f);
    return a;
}

AudioBuffer sequence (const std::vector<AudioBuffer>& parts, double gap = 0.4)
{
    AudioBuffer out;
    out.sampleRate = 48000.0;
    for (const auto& p : parts)
    {
        append (out, p);
        appendSilence (out, gap);
    }
    return out;
}

std::string family (const std::string& name)
{
    const auto u = name.rfind ('_');
    return u == std::string::npos ? name : name.substr (0, u);
}

void writeMap (const fs::path& path)
{
    const int w = 760, h = 560, left = 64, right = 24, top = 48, bottom = 56;
    float lo[2] = { 1e9f, 1e9f }, hi[2] = { -1e9f, -1e9f };
    std::vector<std::vector<float>> zs;
    for (int i = 0; i < model->numSounds(); ++i)
    {
        zs.push_back (model->soundZ (i));
        for (int k = 0; k < 2; ++k)
        {
            lo[k] = std::min (lo[k], zs.back()[static_cast<size_t> (k)]);
            hi[k] = std::max (hi[k], zs.back()[static_cast<size_t> (k)]);
        }
    }
    for (int k = 0; k < 2; ++k)
    {
        lo[k] = std::floor (lo[k] - 0.3f);
        hi[k] = std::ceil (hi[k] + 0.3f);
    }
    auto X = [&] (float v) { return left + (v - lo[0]) / (hi[0] - lo[0]) * (w - left - right); };
    auto Y = [&] (float v) { return h - bottom - (v - lo[1]) / (hi[1] - lo[1]) * (h - top - bottom); };

    std::ofstream s (path);
    s << "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 " << w << " " << h << "' font-family='system-ui, sans-serif'>\n"
      << "<style>\n"
         ".bg{fill:#fcfcfb}.grid{stroke:#e1e0d9;stroke-width:1}.axis{stroke:#c3c2b7;stroke-width:1}\n"
         ".t1{fill:#0b0b0b}.t2{fill:#52514e}.dot{fill:#2a78d6;stroke:#fcfcfb;stroke-width:2}.dot:hover{stroke:#0b0b0b}\n"
         "@media (prefers-color-scheme: dark){.bg{fill:#1a1a19}.grid{stroke:#2c2c2a}.axis{stroke:#52514e}\n"
         ".t1{fill:#ffffff}.t2{fill:#c3c2b7}.dot{fill:#3987e5;stroke:#1a1a19}.dot:hover{stroke:#ffffff}}\n"
         "</style>\n"
      << "<rect class='bg' width='100%' height='100%'/>\n"
      << "<text class='t1' x='" << left << "' y='28' font-size='16' font-weight='600'>Training sounds in PCA space (PC1 × PC2)</text>\n";
    for (int v = static_cast<int> (lo[0]); v <= static_cast<int> (hi[0]); ++v)
        s << "<line class='grid' x1='" << X (v) << "' x2='" << X (v) << "' y1='" << top << "' y2='" << h - bottom << "'/>"
          << "<text class='t2' font-size='11' text-anchor='middle' x='" << X (v) << "' y='" << h - bottom + 16 << "'>" << v << "</text>\n";
    for (int v = static_cast<int> (lo[1]); v <= static_cast<int> (hi[1]); ++v)
        s << "<line class='grid' x1='" << left << "' x2='" << w - right << "' y1='" << Y (v) << "' y2='" << Y (v) << "'/>"
          << "<text class='t2' font-size='11' text-anchor='end' x='" << left - 8 << "' y='" << Y (v) + 4 << "'>" << v << "</text>\n";
    char buf[128];
    std::snprintf (buf, sizeof buf, "PC1 (%.0f %% of variance, SD units)", 100.0 * model->varianceExplained (0));
    s << "<text class='t2' font-size='12' text-anchor='middle' x='" << (left + w - right) / 2 << "' y='" << h - 16 << "'>" << buf << "</text>\n";
    std::snprintf (buf, sizeof buf, "PC2 (%.0f %% of variance, SD units)", 100.0 * model->varianceExplained (1));
    s << "<text class='t2' font-size='12' text-anchor='middle' transform='translate(18 " << (top + h - bottom) / 2 << ") rotate(-90)'>" << buf << "</text>\n";

    std::map<std::string, std::pair<std::vector<float>, int>> centroids;
    for (int i = 0; i < model->numSounds(); ++i)
    {
        const auto& z = zs[static_cast<size_t> (i)];
        const auto& name = model->names[static_cast<size_t> (i)];
        std::snprintf (buf, sizeof buf, "%s  (PC1 %.2f, PC2 %.2f)", name.c_str(), z[0], z[1]);
        s << "<circle class='dot' r='5' cx='" << X (z[0]) << "' cy='" << Y (z[1]) << "'><title>" << buf << "</title></circle>\n";
        auto& c = centroids[family (name)];
        if (c.first.empty())
            c.first.assign (2, 0.0f);
        c.first[0] += z[0];
        c.first[1] += z[1];
        ++c.second;
    }
    for (const auto& [name, c] : centroids)
        s << "<text class='t1' font-size='13' font-weight='600' text-anchor='middle' x='" << X (c.first[0] / c.second)
          << "' y='" << Y (c.first[1] / c.second) - 10 << "'>" << name << "</text>\n";
    s << "</svg>\n";
}

} // namespace

int main (int argc, char** argv)
{
    if (argc != 4)
    {
        std::cerr << "usage: pcs-examples model.pcsm <training-wav-dir> <output-dir>\n";
        return 2;
    }
    try
    {
        auto m = std::make_shared<Model> (loadModel (argv[1]));
        model = m;
        settings = m->analysis;
        const fs::path trainDir = argv[2];
        outDir = argv[3];
        fs::create_directories (outDir);
        notes.open (outDir / "README.txt");
        notes << "PCASynth listening examples\n"
              << "Model: " << model->numSounds() << " sounds, " << model->numComponents() << " components, "
              << model->numHarmonics << " harmonics, " << model->durationSeconds() << " s\n\n";

        // One representative per family (its first sound).
        std::vector<std::string> reps;
        for (const auto& n : model->names)
            if (std::none_of (reps.begin(), reps.end(), [&] (const std::string& r) { return family (r) == family (n); }))
                reps.push_back (n);
        auto has = [&] (const std::string& n) { return model->soundIndex (n) >= 0; };
        auto pick = [&] (const std::string& f, size_t fallback) {
            for (const auto& r : reps)
                if (family (r) == f)
                    return r;
            return reps[fallback % reps.size()];
        };

        // 1. Reconstructions.
        for (const auto& name : reps)
        {
            auto original = readWav ((trainDir / (name + ".wav")).string());
            original.channels.resize (1);
            original.channels[0].resize (static_cast<size_t> (std::min<double> (original.numSamples(), (model->durationSeconds() + 0.1) * original.sampleRate)));
            normalisePeak (original, 0.7f);
            const auto analysed = analyseHarmonics (readWav ((trainDir / (name + ".wav")).string()), settings, name);
            const auto direct = singleSoundModel (analysed, settings);
            SynthParams dp;
            auto harmonicOnly = renderNotes (direct, dp, { { 0.0, model->durationSeconds(), 60, 0.8f } }, model->durationSeconds() + 0.1);
            normalisePeak (harmonicOnly, 0.7f);

            auto p = at (zOf (name));
            std::vector<AudioBuffer> parts { original, harmonicOnly, oneNote (p) };
            for (int k : { 8, 3, 0 })
            {
                p.activeComponents = k;
                parts.push_back (oneNote (p));
            }
            write ("01_reconstruction_" + name, sequence (parts, 0.6),
                   "Six versions of " + name + ": the original WAV | the harmonic model alone (no PCA) | from all "
                       + std::to_string (model->numComponents()) + " components | 8 components | 3 components | 0 (the mean sound).");
        }

        // 2. Stepwise morphs, 3. a glide within one note.
        const std::vector<std::pair<std::string, std::string>> pairs {
            { pick ("pluck", 0), pick ("vowel", 1) }, { pick ("reed", 2), pick ("bowed", 3) },
            { pick ("mallet", 4), pick ("brass", 5) }, { pick ("organ", 6), pick ("piano", 7) } };
        for (const auto& [a, b] : pairs)
        {
            if (! has (a) || ! has (b) || a == b)
                continue;
            std::vector<AudioBuffer> parts;
            for (float t : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
                parts.push_back (oneNote (at (lerp (zOf (a), zOf (b), t))));
            write ("02_morph_" + a + "_to_" + b, sequence (parts),
                   "Five notes from " + a + " to " + b + " in steps of 25 % (straight line in PCA space).");
        }
        for (const auto& [a, b] : { pairs[1], pairs[0] })
        {
            if (! has (a) || ! has (b) || a == b)
                continue;
            const auto za = zOf (a), zb = zOf (b);
            SynthParams p = at (za);
            p.mode = PlayMode::Loop;
            p.loopStart = 0.2f;
            p.loopEnd = 0.6f;
            p.morphTime = 0.1f;
            const double len = 12.0;
            auto automation = [&] (double t, SynthParams& q) {
                const float u = static_cast<float> (0.5 - 0.5 * std::cos (kTwoPi * std::min (1.0, t / len)));
                const auto z = lerp (za, zb, u);
                for (size_t j = 0; j < z.size(); ++j)
                    q.z[j] = z[j];
            };
            auto audio = renderNotes (model, p, { { 0.0, len, 60, 0.8f } }, len + 1.0, 48000.0, automation);
            write ("03_glide_" + a + "_to_" + b, audio,
                   "One held note (Loop mode) gliding from " + a + " to " + b + " and back over 12 s.");
        }

        // 4. Component sweeps.
        for (int j = 0; j < std::min (4, model->numComponents()); ++j)
        {
            std::vector<AudioBuffer> parts;
            for (float v : { -2.0f, -1.0f, 0.0f, 1.0f, 2.0f })
            {
                SynthParams p;
                p.z[static_cast<size_t> (j)] = v;
                parts.push_back (oneNote (p));
            }
            char buf[160];
            std::snprintf (buf, sizeof buf, "PC%d at -2, -1, 0, +1, +2 SD (all other components at 0). PC%d explains %.1f %% of the variance.",
                           j + 1, j + 1, 100.0 * model->varianceExplained (j));
            write ("04_pc" + std::to_string (j + 1) + "_sweep", sequence (parts), buf);
        }

        // 5. Melodies across the keyboard from single points.
        const std::vector<int> tune { 48, 55, 60, 64, 67, 72, 76, 79, 84, 79, 76, 72, 67, 64, 60, 55, 48 };
        for (const auto& name : { pick ("reed", 0), pick ("vowel", 1), pick ("epiano", 2) })
        {
            if (! has (name))
                continue;
            SynthParams p = at (zOf (name));
            p.mode = PlayMode::Loop;
            p.release = 0.15f;
            std::vector<Note> ns;
            for (size_t i = 0; i < tune.size(); ++i)
                ns.push_back ({ 0.3 * static_cast<double> (i), 0.28, tune[i], 0.8f });
            write ("05_melody_" + name, renderNotes (model, p, ns, 0.3 * static_cast<double> (tune.size()) + 1.0),
                   "An arpeggio from C3 to C6 with " + name + "'s timbre. The model was trained only on C4; other pitches come from the harmonic model.");
        }

        // 6. A slow wander through the space under a chord progression.
        {
            SynthParams p;
            p.mode = PlayMode::PingPong;
            p.loopStart = 0.15f;
            p.loopEnd = 0.6f;
            p.release = 1.2f;
            p.gainDb = -12.0f;
            p.morphTime = 0.2f;
            const std::vector<std::vector<int>> chords { { 48, 55, 64, 71 }, { 45, 52, 60, 67, 71 }, { 41, 48, 57, 64 }, { 43, 50, 59, 64 } };
            std::vector<Note> ns;
            for (size_t c = 0; c < 8; ++c)
                for (int n : chords[c % chords.size()])
                    ns.push_back ({ 4.0 * static_cast<double> (c), 3.9, n, 0.7f });
            const int k = std::min (6, model->numComponents());
            auto automation = [k] (double t, SynthParams& q) {
                for (int j = 0; j < k; ++j)
                {
                    const double f = 0.03 + 0.017 * j;
                    q.z[static_cast<size_t> (j)] = static_cast<float> (1.6 * std::sin (kTwoPi * f * t + 1.7 * j) / (1.0 + 0.2 * j));
                }
            };
            write ("06_wander", renderNotes (model, p, ns, 34.0, 48000.0, automation),
                   "A chord progression while the point drifts slowly through PC1-PC6 (PingPong loop mode).");
        }

        // 7. Beyond the training set.
        {
            std::vector<AudioBuffer> parts;
            for (auto [a, b] : { std::pair { -3.0f, -3.0f }, { 3.0f, -3.0f }, { -3.0f, 3.0f }, { 3.0f, 3.0f } })
            {
                SynthParams p;
                p.z[0] = a;
                p.z[1] = b;
                parts.push_back (oneNote (p));
            }
            write ("07_extremes", sequence (parts), "The four corners PC1/PC2 = (-3,-3), (+3,-3), (-3,+3), (+3,+3): off the edge of the training set.");
        }

        // 8. Scanning through a sound's envelope, wavetable-style.
        for (const auto& name : { pick ("piano", 0), pick ("bowed", 1) })
        {
            if (! has (name))
                continue;
            SynthParams p = at (zOf (name));
            p.mode = PlayMode::Scan;
            const double len = 8.0;
            auto automation = [len] (double t, SynthParams& q) { q.scanPosition = static_cast<float> (std::min (1.0, t / len)); };
            write ("08_scan_" + name, renderNotes (model, p, { { 0.0, len, 60, 0.8f } }, len + 0.5, 48000.0, automation),
                   "Scan mode: one held note whose position in " + name + "'s envelope moves from start to end over 8 s.");
        }

        // 9-13. Stage 5: the random walk and LFOs, under a slow chord progression.
        {
            const std::vector<std::vector<int>> chords { { 48, 55, 64, 71 }, { 45, 52, 60, 67 }, { 41, 48, 57, 64 }, { 43, 50, 59, 62 } };
            auto pad = [&chords] (double seconds) {
                std::vector<Note> ns;
                for (int c = 0; c * 4.0 < seconds - 0.5; ++c)
                    for (int n : chords[static_cast<size_t> (c) % chords.size()])
                        ns.push_back ({ 4.0 * c, 3.95, n, 0.7f });
                return ns;
            };
            auto base = [] {
                SynthParams p;
                p.mode = PlayMode::PingPong;
                p.loopStart = 0.15f;
                p.loopEnd = 0.6f;
                p.release = 1.0f;
                p.gainDb = -12.0f;
                p.mod.walk.enabled = true;
                p.mod.walk.seed = 7;
                return p;
            };
            const double len = 24.0;

            auto drift = base();
            drift.mod.walk.mode = WalkMode::Drift;
            drift.mod.walk.rate = 0.3f;
            drift.mod.walk.amount = 1.2f;
            drift.mod.walk.dims = 6;
            write ("09_walk_drift", renderNotes (model, drift, pad (len), len + 1.0),
                   "Random walk, Drift: slow Brownian wandering over PC1-6 (Amount 1.2 SD, Rate 0.3 Hz, Tether 50 %).");

            auto jumps = base();
            jumps.mod.walk.mode = WalkMode::Jumps;
            jumps.mod.walk.rate = 2.0f;
            jumps.mod.walk.glide = 0.15f;
            jumps.mod.walk.amount = 1.5f;
            jumps.mod.walk.dims = 4;
            jumps.mode = PlayMode::Loop;
            std::vector<Note> pulse;
            for (int i = 0; i < 48; ++i)
                pulse.push_back ({ 0.5 * i, 0.45, i % 8 < 4 ? 48 + (i % 4) * 7 : 45 + (i % 4) * 5, 0.8f });
            write ("10_walk_jumps", renderNotes (model, jumps, pulse, len + 1.0),
                   "Random walk, Jumps: a new random point every half second with a short glide, over repeated notes.");

            auto tour = base();
            tour.mod.walk.mode = WalkMode::Tour;
            tour.mod.walk.rate = 0.35f;
            tour.mod.walk.glide = 0.6f;
            write ("11_walk_tour", renderNotes (model, tour, pad (len), len + 1.0),
                   "Random walk, Tour: glides from training sound to training sound (random order), resting on each.");

            auto neighbour = tour;
            neighbour.mod.walk.mode = WalkMode::NeighbourTour;
            write ("12_walk_neighbour_tour", renderNotes (model, neighbour, pad (len), len + 1.0),
                   "Random walk, Neighbour Tour: each step goes to one of the three most similar sounds, so it drifts through families.");

            auto voices = base();
            voices.mod.walk.mode = WalkMode::Drift;
            voices.mod.walk.rate = 0.5f;
            voices.mod.walk.amount = 1.0f;
            voices.mod.walk.perVoice = 1.0f;
            voices.mod.walk.dims = 6;
            voices.mod.voiceSpread = 0.6f;
            write ("13_walk_per_voice", renderNotes (model, voices, pad (len), len + 1.0),
                   "Every note of each chord wanders on its own (Walk Per Voice 100 %, Voice Spread 0.6 SD).");

            auto lfo = base();
            lfo.mod.walk.enabled = false;
            lfo.mod.lfo[0] = { true, LfoShape::Sine, 0.25f, false, 4.0f, 2.0f, 0 };
            lfo.mod.lfo[1] = { true, LfoShape::SmoothRandom, 1.5f, false, 4.0f, 1.0f, 1 };
            write ("14_lfos", renderNotes (model, lfo, pad (16.0), 17.0),
                   "Two LFOs: a slow sine on PC1 (±2 SD, 4 s cycle) and a smooth random LFO on PC2 (±1 SD).");
        }

        writeMap (outDir / "map.svg");
        std::cout << "  map.svg\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << "pcs-examples: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
