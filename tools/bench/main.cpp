// pcs-bench: how much of one CPU core the synth needs, in a few stress cases.
// Optionally writes each case's output, to check that an optimisation leaves
// the sound unchanged.
#include "pcs/Model.h"
#include "pcs/Space.h"
#include "pcs/Synth.h"
#include "pcs/Wav.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace {

struct Case
{
    const char* name;
    int voices;
    std::function<void (pcs::SynthParams&)> setup;
};

double run (const std::shared_ptr<const pcs::Space>& model, const Case& c, double seconds, const std::string& outDir)
{
    constexpr double sr = 48000.0;
    constexpr int block = 256;
    pcs::Synth synth;
    synth.prepare (sr);
    synth.setModel (model);
    pcs::SynthParams p;
    p.mode = pcs::PlayMode::Loop;
    p.polyphony = 32;
    p.gainDb = -24.0f;
    p.levelLock = 1.0f; // as the plugin plays by default
    c.setup (p);
    synth.setParams (p);

    std::vector<pcs::MidiEvent> notes;
    for (int i = 0; i < c.voices; ++i)
    {
        pcs::MidiEvent e;
        e.type = pcs::MidiEvent::Type::NoteOn;
        e.note = 36 + (i * 7) % 48; // spread over four octaves, low notes have the most partials
        e.value = 0.8f;
        e.channel = 2 + i % 15;
        notes.push_back (e);
    }

    const auto total = static_cast<int> (seconds * sr);
    std::vector<float> buf (block), all;
    if (! outDir.empty())
        all.reserve (static_cast<size_t> (total));
    float* chans[] = { buf.data() };
    double busy = 0.0;
    for (int done = 0; done < total; done += block)
    {
        const int n = std::min (block, total - done);
        const auto t0 = std::chrono::steady_clock::now();
        synth.process (chans, 1, n, done == 0 ? notes.data() : nullptr, done == 0 ? static_cast<int> (notes.size()) : 0);
        busy += std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        if (! outDir.empty())
            all.insert (all.end(), buf.begin(), buf.begin() + n);
    }
    if (! outDir.empty())
    {
        pcs::AudioBuffer a;
        a.sampleRate = sr;
        a.channels.push_back (std::move (all));
        pcs::writeWav (outDir + "/" + c.name + ".wav", a);
    }
    return 100.0 * busy / seconds;
}

} // namespace

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf (stderr, "usage: pcs-bench model.pcsm [seconds] [--out DIR]\n");
        return 2;
    }
    const std::shared_ptr<const pcs::Space> model = pcs::loadSpace (argv[1]);
    double seconds = 5.0;
    std::string outDir;
    for (int i = 2; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--out" && i + 1 < argc)
            outDir = argv[++i];
        else
            seconds = std::atof (a.c_str());
    }

    const std::vector<Case> cases {
        { "shared_8", 8, [] (pcs::SynthParams& p) { p.noiseDb = -60.0f; } },
        { "shared_16_noise", 16, [] (pcs::SynthParams&) {} },
        { "walk_16_noise", 16, [] (pcs::SynthParams& p) {
              p.mod.walk.enabled = true;
              p.mod.walk.rate = 2.0f;
          } },
        { "per_voice_16_noise", 16, [] (pcs::SynthParams& p) {
              p.mod.walk.enabled = true;
              p.mod.walk.rate = 2.0f;
              p.mod.walk.perVoice = 1.0f;
              p.mod.voiceSpread = 1.0f;
          } },
        { "mpe_32_noise", 32, [] (pcs::SynthParams& p) {
              p.mpe.enabled = true;
              p.mod.voiceSpread = 0.5f;
          } },
    };
    if (const auto* h = dynamic_cast<const pcs::Model*> (model.get()))
        std::printf ("model: %s, %d harmonics, %d noise bands, %d components\n", h->title.c_str(), h->numHarmonics,
                     h->numNoiseBands, h->numComponents());
    else
        std::printf ("waveform space: %s, %d components\n", model->title.c_str(), model->numComponents());
    std::printf ("%-22s %8s\n", "case", "% core");
    for (const auto& c : cases)
        std::printf ("%-22s %8.2f\n", c.name, run (model, c, seconds, outDir));
    return 0;
}
