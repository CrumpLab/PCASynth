// pcs-render: renders notes from a point in a model's PCA space to a WAV.
#include "Render.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void usage()
{
    std::cerr << "usage: pcs-render model.pcsm out.wav [options]\n"
                 "  where in the space (combined in this order):\n"
                 "    --sound NAME        start at a training sound (default: the mean)\n"
                 "    --to NAME           morph towards another training sound ...\n"
                 "    --amount A          ... by this much (0..1, default 0.5)\n"
                 "    --z z1,z2,...       add these (SD units) to PC1, PC2, ...\n"
                 "    --components K      use only the first K components\n"
                 "    --exaggerate X      multiply the point by X\n"
                 "  what to play:\n"
                 "    --notes 60,64,67    MIDI notes, played together (default 60)\n"
                 "    --length S          seconds held (default 3)\n"
                 "    --tail S            seconds after note-off (default 1)\n"
                 "    --mode M            oneshot | loop | pingpong | scan\n"
                 "    --loop A,B          loop points, fractions of the envelope\n"
                 "    --scan P            scan position, fraction of the envelope\n"
                 "    --speed X           envelope playback rate\n"
                 "    --tilt DB           brightness, dB per octave\n"
                 "    --release S         release time\n"
                 "    --gain DB           output gain\n"
                 "  movement (Stage 5):\n"
                 "    --walk MODE         drift | jumps | tour | neighbour\n"
                 "    --walk-amount SD    how far (tours: 1 = arrive at each sound)\n"
                 "    --walk-rate HZ      steps per second (drift: speed)\n"
                 "    --walk-glide G      0..1 (jumps and tours)\n"
                 "    --walk-tether T     0..1 (drift)\n"
                 "    --walk-dims N       components that wander (drift, jumps)\n"
                 "    --walk-main         move in proportion to each component's variance\n"
                 "    --walk-per-voice P  0..1: how independently each note wanders\n"
                 "    --walk-seed N\n"
                 "    --lfo PC,HZ,DEPTH   a sine LFO on component PC (1-based)\n"
                 "    --spread SD         voice spread\n";
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        usage();
        return 2;
    }
    const std::string modelPath = argv[1], outPath = argv[2];
    std::string from, to;
    float amount = 0.5f;
    std::vector<float> offset;
    std::vector<float> notes { 60 };
    double length = 3.0, tail = 1.0;
    pcs::SynthParams p;
    try
    {
        for (int i = 3; i < argc; ++i)
        {
            const std::string a = argv[i];
            auto next = [&] { if (i + 1 >= argc) { usage(); std::exit (2); } return std::string (argv[++i]); };
            if (a == "--sound") from = next();
            else if (a == "--to") to = next();
            else if (a == "--amount") amount = std::stof (next());
            else if (a == "--z") offset = pcs::tools::parseFloats (next());
            else if (a == "--components") p.activeComponents = std::stoi (next());
            else if (a == "--exaggerate") p.exaggerate = std::stof (next());
            else if (a == "--notes") notes = pcs::tools::parseFloats (next());
            else if (a == "--length") length = std::stod (next());
            else if (a == "--tail") tail = std::stod (next());
            else if (a == "--speed") p.speed = std::stof (next());
            else if (a == "--tilt") p.tiltDbPerOctave = std::stof (next());
            else if (a == "--release") p.release = std::stof (next());
            else if (a == "--gain") p.gainDb = std::stof (next());
            else if (a == "--scan") p.scanPosition = std::stof (next());
            else if (a == "--loop")
            {
                const auto l = pcs::tools::parseFloats (next());
                if (l.size() == 2) { p.loopStart = l[0]; p.loopEnd = l[1]; }
            }
            else if (a == "--walk")
            {
                const auto m = next();
                p.mod.walk.enabled = true;
                p.mod.walk.mode = m == "jumps" ? pcs::WalkMode::Jumps : m == "tour" ? pcs::WalkMode::Tour
                                : m == "neighbour" ? pcs::WalkMode::NeighbourTour : pcs::WalkMode::Drift;
            }
            else if (a == "--walk-amount") p.mod.walk.amount = std::stof (next());
            else if (a == "--walk-rate") p.mod.walk.rate = std::stof (next());
            else if (a == "--walk-glide") p.mod.walk.glide = std::stof (next());
            else if (a == "--walk-tether") p.mod.walk.tether = std::stof (next());
            else if (a == "--walk-dims") p.mod.walk.dims = std::stoi (next());
            else if (a == "--walk-main") p.mod.walk.focus = pcs::WalkFocus::Main;
            else if (a == "--walk-per-voice") p.mod.walk.perVoice = std::stof (next());
            else if (a == "--walk-seed") p.mod.walk.seed = static_cast<uint32_t> (std::stoul (next()));
            else if (a == "--spread") p.mod.voiceSpread = std::stof (next());
            else if (a == "--lfo")
            {
                const auto v = pcs::tools::parseFloats (next());
                if (v.size() == 3)
                    p.mod.lfo[0] = { true, pcs::LfoShape::Sine, v[1], false, 4.0f, v[2], static_cast<int> (v[0]) - 1 };
            }
            else if (a == "--mode")
            {
                const auto m = next();
                p.mode = m == "loop" ? pcs::PlayMode::Loop : m == "pingpong" ? pcs::PlayMode::PingPong
                       : m == "scan" ? pcs::PlayMode::Scan : pcs::PlayMode::OneShot;
            }
            else { usage(); return 2; }
        }

        auto model = std::make_shared<pcs::Model> (pcs::loadModel (modelPath));
        auto soundZ = [&] (const std::string& name) {
            const int i = model->soundIndex (name);
            if (i < 0)
                throw std::runtime_error ("no training sound called " + name);
            return model->soundZ (i);
        };
        std::vector<float> z (static_cast<size_t> (model->numComponents()), 0.0f);
        if (! from.empty())
            z = soundZ (from);
        if (! to.empty())
        {
            const auto b = soundZ (to);
            for (size_t j = 0; j < z.size(); ++j)
                z[j] += amount * (b[j] - z[j]);
        }
        for (size_t j = 0; j < z.size() && j < offset.size(); ++j)
            z[j] += offset[j];
        for (size_t j = 0; j < z.size(); ++j)
            p.z[j] = z[j];

        std::vector<pcs::tools::Note> ns;
        for (float n : notes)
            ns.push_back ({ 0.0, length, static_cast<int> (n), 0.8f });
        auto audio = pcs::tools::renderNotes (model, p, ns, length + tail);
        pcs::writeWav (outPath, audio, pcs::WavFormat::Pcm24);
    }
    catch (const std::exception& e)
    {
        std::cerr << "pcs-render: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
