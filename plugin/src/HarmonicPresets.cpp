#include "PluginProcessor.h"

#include "Params.h"

// PCASynth's factory presets: recipes on its factory space (see Presets.h).
namespace pcsplugin {

namespace {
// Choice indices, spelled out so the table reads.
namespace mode { constexpr float oneShot = 0, loop = 1, pingPong = 2, scan = 3; }
namespace walk { constexpr float drift = 0, jumps = 1, tour = 2, neighbour = 3; }
namespace step { constexpr float eighth = 1, bar = 4; }
namespace lfoShape { constexpr float triangle = 1, smooth = 5; }
// Targets: LFOs take PC1..16 (0..15) or Toward Sound (16); expression adds Off first.
constexpr float lfoPc (int n) { return static_cast<float> (n - 1); }
constexpr float exprPc (int n) { return static_cast<float> (n); }
constexpr float lfoToward = 16, exprToward = 17;
} // namespace

const std::vector<FactoryPreset>& harmonicFactoryPresets()
{
    static const std::vector<FactoryPreset> presets {
        { "Init", "Basic", "", {}, {}, "" },

        // Single instruments: the training sounds as they are, set up to play.
        { "Reed Lead", "Instruments", "reed_2", {},
          { { id::playMode, mode::loop }, { id::attack, 0.01f }, { id::release, 0.25f }, { id::mwDest, exprPc (2) }, { id::mwAmount, 1.5f } }, "" },
        { "Brass Swell", "Instruments", "brass_1", {},
          { { id::playMode, mode::loop }, { id::attack, 0.25f }, { id::release, 0.5f }, { id::velDest, exprPc (1) }, { id::velAmount, 1.0f } },
          "flute_1" },
        { "Breathy Flute", "Instruments", "flute_3", {},
          { { id::playMode, mode::loop }, { id::noise, 4.0f }, { id::attack, 0.04f }, { id::release, 0.4f } }, "" },
        { "Drawbar Organ", "Instruments", "organ_2", {},
          { { id::playMode, mode::loop }, { id::attack, 0.005f }, { id::release, 0.08f }, { id::brightness, 1.5f }, { id::velocity, 0.2f } }, "" },
        { "Mallet Keys", "Instruments", "mallet_1", {},
          { { id::playMode, mode::oneShot }, { id::release, 0.6f }, { id::velDest, exprPc (2) }, { id::velAmount, 1.2f } }, "" },
        { "Soft E-Piano", "Instruments", "epiano_2", {},
          { { id::playMode, mode::oneShot }, { id::release, 0.8f }, { id::brightness, -1.5f }, { id::velDest, exprToward }, { id::velAmount, 0.6f } },
          "mallet_2" },
        { "Plucked String", "Instruments", "pluck_3", {},
          { { id::playMode, mode::oneShot }, { id::release, 0.4f }, { id::velDest, exprPc (1) }, { id::velAmount, 0.8f } }, "" },
        { "Exaggerated Piano", "Instruments", "piano_1", {},
          { { id::playMode, mode::oneShot }, { id::exaggerate, 1.6f }, { id::release, 0.7f } }, "" },

        // Pads and textures: loops, scans and slow movement.
        { "Vowel Choir", "Pads", "vowel_1", {},
          { { id::playMode, mode::pingPong }, { id::loopStart, 0.25f }, { id::loopEnd, 0.8f }, { id::attack, 0.6f }, { id::release, 1.5f },
            { id::lfo (0, "on"), 1 }, { id::lfo (0, "rate"), 0.15f }, { id::lfo (0, "depth"), 0.8f }, { id::lfo (0, "target"), lfoToward } },
          "reed_1" },
        { "Frozen Wavetable", "Pads", "bowed_2", {},
          { { id::playMode, mode::scan }, { id::scanPosition, 0.35f }, { id::attack, 0.3f }, { id::release, 1.2f },
            { id::lfo (0, "on"), 1 }, { id::lfo (0, "shape"), lfoShape::triangle }, { id::lfo (0, "rate"), 0.08f }, { id::lfo (0, "depth"), 1.5f },
            { id::lfo (0, "target"), lfoPc (1) }, { id::lfo (1, "on"), 1 }, { id::lfo (1, "shape"), lfoShape::smooth }, { id::lfo (1, "rate"), 0.2f },
            { id::lfo (1, "depth"), 1.0f }, { id::lfo (1, "target"), lfoPc (3) } },
          "" },
        { "Ensemble", "Pads", "bowed_4", {},
          { { id::playMode, mode::loop }, { id::attack, 0.4f }, { id::release, 1.0f }, { id::voiceSpread, 0.8f }, { id::walkOn, 1 },
            { id::walkMode, walk::drift }, { id::walkAmount, 0.6f }, { id::walkRate, 0.3f }, { id::walkPerVoice, 1.0f } },
          "" },
        { "Between Worlds", "Pads", "", { { 0, 1.2f }, { 1, -0.8f } },
          { { id::playMode, mode::loop }, { id::attack, 0.5f }, { id::release, 2.0f }, { id::macro, 0.4f } }, "organ_3" },

        // The random walk (Stage 5).
        { "Drifting Timbre", "Random Walk", "reed_3", {},
          { { id::playMode, mode::loop }, { id::attack, 0.1f }, { id::release, 0.8f }, { id::walkOn, 1 }, { id::walkMode, walk::drift },
            { id::walkAmount, 1.2f }, { id::walkRate, 0.4f }, { id::walkTether, 0.4f }, { id::walkDims, 6 } },
          "" },
        { "Orchestra Tour", "Random Walk", "", {},
          { { id::playMode, mode::loop }, { id::attack, 0.2f }, { id::release, 1.0f }, { id::walkOn, 1 }, { id::walkMode, walk::tour },
            { id::walkAmount, 1.0f }, { id::walkRate, 0.2f }, { id::walkGlide, 0.9f } },
          "" },
        { "Neighbourhood", "Random Walk", "flute_2", {},
          { { id::playMode, mode::loop }, { id::attack, 0.1f }, { id::release, 0.6f }, { id::walkOn, 1 }, { id::walkMode, walk::neighbour },
            { id::walkAmount, 0.8f }, { id::walkSync, 1 }, { id::walkSyncLen, step::bar }, { id::walkGlide, 0.6f } },
          "" },
        { "Timbre Sequencer", "Random Walk", "brass_3", {},
          { { id::playMode, mode::loop }, { id::attack, 0.005f }, { id::release, 0.15f }, { id::walkOn, 1 }, { id::walkMode, walk::jumps },
            { id::walkAmount, 1.5f }, { id::walkSync, 1 }, { id::walkSyncLen, step::eighth }, { id::walkGlide, 0.0f }, { id::walkDims, 3 },
            { id::walkRestart, 1 } },
          "" },
        { "Every Note Its Own", "Random Walk", "vowel_3", {},
          { { id::playMode, mode::loop }, { id::attack, 0.05f }, { id::release, 0.5f }, { id::walkOn, 1 }, { id::walkMode, walk::jumps },
            { id::walkAmount, 1.0f }, { id::walkRate, 0.7f }, { id::walkGlide, 0.8f }, { id::walkPerVoice, 1.0f }, { id::voiceSpread, 0.5f } },
          "" },

        // Expression: MPE controllers like the Osmose (Stage 6).
        { "MPE Breath", "MPE", "flute_1", {},
          { { id::playMode, mode::loop }, { id::attack, 0.02f }, { id::release, 0.3f }, { id::mpeOn, 1 }, { id::mpePressDest, exprPc (1) },
            { id::mpePressAmount, 1.5f }, { id::mpeSlideDest, exprToward }, { id::mpeSlideAmount, 0.8f }, { id::noise, 2.0f } },
          "reed_2" },
        { "MPE Brass Growl", "MPE", "brass_2", {},
          { { id::playMode, mode::loop }, { id::attack, 0.03f }, { id::release, 0.3f }, { id::mpeOn, 1 }, { id::mpePressDest, exprPc (1) },
            { id::mpePressAmount, 2.0f }, { id::mpePressCurve, 0.3f }, { id::mpeSlideDest, exprPc (2) }, { id::mpeSlideAmount, 1.2f } },
          "" },
        { "MPE Morph Pad", "MPE", "", {},
          { { id::playMode, mode::loop }, { id::attack, 0.3f }, { id::release, 1.5f }, { id::mpeOn, 1 }, { id::mpePressDest, exprToward },
            { id::mpePressAmount, 1.0f }, { id::mpeSlideDest, exprPc (3) }, { id::mpeSlideMode, 1 }, { id::mpeSlideAmount, 1.5f },
            { id::walkOn, 1 }, { id::walkAmount, 0.5f }, { id::walkRate, 0.2f } },
          "vowel_2" },
    };
    return presets;
}

} // namespace pcsplugin
