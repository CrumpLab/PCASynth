#include "WaveProcessor.h"

#include "Params.h"

// PCAWave's factory presets: recipes on its factory space (see Presets.h).
namespace pcsplugin {

namespace {
namespace mode { constexpr float oneShot = 0, loop = 1, pingPong = 2, scan = 3; }
namespace walk { constexpr float drift = 0, jumps = 1, tour = 2, neighbour = 3; }
namespace step { constexpr float quarter = 2, bar = 4; }
constexpr float lfoPc (int n) { return static_cast<float> (n - 1); }
constexpr float exprPc (int n) { return static_cast<float> (n); }
constexpr float exprToward = 17;
} // namespace

const std::vector<FactoryPreset>& waveFactoryPresets()
{
    static const std::vector<FactoryPreset> presets {
        { "Init", "Basic", "", {}, {}, "" },

        // The training sounds themselves: a waveform space plays them exactly.
        { "Reed", "Sounds", "reed_2", {}, { { id::playMode, mode::loop }, { id::attack, 0.01f }, { id::release, 0.3f } }, "" },
        { "Bowed", "Sounds", "bowed_3", {}, { { id::playMode, mode::loop }, { id::attack, 0.08f }, { id::release, 0.5f } }, "" },
        { "Electric Piano", "Sounds", "epiano_1", {}, { { id::playMode, mode::oneShot }, { id::release, 0.5f } }, "" },
        { "Mallet", "Sounds", "mallet_2", {}, { { id::playMode, mode::oneShot }, { id::release, 0.6f } }, "" },

        // Mixes: points between sounds are mixes of their waveforms.
        { "Reed Into Vowel", "Mixes", "reed_1", {},
          { { id::playMode, mode::loop }, { id::attack, 0.05f }, { id::release, 0.6f }, { id::macro, 0.5f } }, "vowel_1" },
        { "Mod Wheel Morph", "Mixes", "flute_2", {},
          { { id::playMode, mode::loop }, { id::attack, 0.05f }, { id::release, 0.5f }, { id::mwDest, exprToward }, { id::mwAmount, 1.0f } },
          "brass_2" },
        { "Frozen Grain", "Mixes", "organ_2", {},
          { { id::playMode, mode::scan }, { id::scanPosition, 0.4f }, { id::attack, 0.2f }, { id::release, 1.0f },
            { id::lfo (0, "on"), 1 }, { id::lfo (0, "rate"), 0.1f }, { id::lfo (0, "depth"), 1.2f }, { id::lfo (0, "target"), lfoPc (1) } },
          "" },

        // Movement.
        { "Drifting Mix", "Random Walk", "vowel_2", {},
          { { id::playMode, mode::loop }, { id::attack, 0.1f }, { id::release, 0.8f }, { id::walkOn, 1 }, { id::walkMode, walk::drift },
            { id::walkAmount, 0.8f }, { id::walkRate, 0.3f }, { id::walkDims, 4 } },
          "" },
        { "Sound Tour", "Random Walk", "", {},
          { { id::playMode, mode::loop }, { id::attack, 0.1f }, { id::release, 0.8f }, { id::walkOn, 1 }, { id::walkMode, walk::tour },
            { id::walkRate, 0.25f }, { id::walkGlide, 0.9f } },
          "" },
        { "Neighbour Steps", "Random Walk", "flute_1", {},
          { { id::playMode, mode::loop }, { id::attack, 0.01f }, { id::release, 0.2f }, { id::walkOn, 1 }, { id::walkMode, walk::neighbour },
            { id::walkSync, 1 }, { id::walkSyncLen, step::quarter }, { id::walkGlide, 0.2f } },
          "" },
        { "Chord Of Mixes", "Random Walk", "bowed_1", {},
          { { id::playMode, mode::pingPong }, { id::attack, 0.2f }, { id::release, 1.0f }, { id::walkOn, 1 }, { id::walkMode, walk::jumps },
            { id::walkAmount, 0.7f }, { id::walkRate, 0.5f }, { id::walkGlide, 0.8f }, { id::walkPerVoice, 1.0f }, { id::voiceSpread, 0.4f },
            { id::walkSync, 0 }, { id::walkSyncLen, step::bar } },
          "" },

        // MPE.
        { "MPE Press To Mix", "MPE", "reed_3", {},
          { { id::playMode, mode::loop }, { id::attack, 0.02f }, { id::release, 0.3f }, { id::mpeOn, 1 }, { id::mpePressDest, exprToward },
            { id::mpePressAmount, 1.0f }, { id::mpeSlideDest, exprPc (1) }, { id::mpeSlideAmount, 1.0f } },
          "brass_1" },
    };
    return presets;
}

} // namespace pcsplugin
