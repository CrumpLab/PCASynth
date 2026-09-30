#pragma once

#include "pcs/Synth.h"

#include <juce_audio_processors/juce_audio_processors.h>

// Host parameters. PC1..PC16 are the point in the space (SD units); the
// remaining components (17..64) are "detail" kept in the plugin state and set
// when you jump to a training sound.
namespace pcsplugin {

constexpr int kNumPcParams = 16;

// Which plugin: PCASynth plays harmonic spaces, PCAWave waveform spaces. The
// parameters that only make sense for harmonics (brightness, harmonics, noise,
// keytrack, pitch envelope, speed) exist only in PCASynth.
enum class Kind { Harmonic, Wave };

inline juce::String pcId (int j) { return "pc" + juce::String (j + 1); }

namespace id {
inline const juce::String components = "components", exaggerate = "exaggerate", morphTime = "morph_time",
                          playMode = "play_mode", loopStart = "loop_start", loopEnd = "loop_end",
                          scanPosition = "scan_position", speed = "speed", attack = "attack", release = "release",
                          brightness = "brightness", harmonics = "harmonics", velocity = "velocity",
                          bendRange = "bend_range", polyphony = "polyphony", gain = "gain",
                          // Stage 5: movement and expression
                          walkOn = "walk_on", walkMode = "walk_mode", walkAmount = "walk_amount", walkRate = "walk_rate",
                          walkSync = "walk_sync", walkSyncLen = "walk_sync_len", walkGlide = "walk_glide",
                          walkTether = "walk_tether", walkDims = "walk_dims", walkFocus = "walk_focus",
                          walkPerVoice = "walk_per_voice", walkSeed = "walk_seed", walkRestart = "walk_restart",
                          walkFreeze = "walk_freeze", velDest = "vel_dest", velAmount = "vel_amount", mwDest = "mw_dest",
                          mwAmount = "mw_amount", atDest = "at_dest", atAmount = "at_amount", macro = "macro",
                          voiceSpread = "voice_spread",
                          // Stage 6: MPE
                          mpeOn = "mpe_on", mpeZone = "mpe_zone", mpeBendRange = "mpe_bend_range",
                          mpePressDest = "mpe_press_dest", mpePressAmount = "mpe_press_amount",
                          mpePressCurve = "mpe_press_curve", mpeSmoothing = "mpe_smoothing",
                          mpeSlideDest = "mpe_slide_dest", mpeSlideAmount = "mpe_slide_amount", mpeSlideMode = "mpe_slide_mode",
                          // Stage 7
                          noise = "noise", keytrack = "keytrack",
                          // Stage 8
                          levelLock = "level_lock", pitchEnv = "pitch_env";
inline juce::String lfo (int n, const char* what) { return "lfo" + juce::String (n + 1) + "_" + what; }
}

inline juce::StringArray playModeNames() { return { "One-shot", "Loop", "Ping-pong", "Scan" }; }
inline juce::StringArray walkModeNames() { return { "Drift", "Jumps", "Tour", "Neighbour Tour" }; }
inline juce::StringArray walkFocusNames() { return { "Equal", "Main components" }; }
inline juce::StringArray lfoShapeNames() { return { "Sine", "Triangle", "Saw", "Square", "Sample & Hold", "Smooth Random" }; }
inline juce::StringArray syncNames() { return { "1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars", "4 bars", "8 bars", "16 bars" }; }
inline float syncBeats (int index)
{
    static const float beats[] = { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f, 64.0f };
    return beats[juce::jlimit (0, 8, index)];
}
// LFO targets: PC1..PC16, Toward Sound. Expression adds "Off" first.
juce::StringArray targetNames (bool withOff);

juce::AudioProcessorValueTreeState::ParameterLayout createLayout (Kind kind);

// Reads the current parameter values (any thread; lock-free).
class ParamReader
{
public:
    explicit ParamReader (juce::AudioProcessorValueTreeState& state);
    // `detail` supplies components 17..64.
    pcs::SynthParams read (const std::array<float, pcs::kMaxComponents>& detail) const noexcept;

private:
    std::array<std::atomic<float>*, kNumPcParams> pc {};
    std::atomic<float>*components, *exaggerate, *morphTime, *playMode, *loopStart, *loopEnd, *scanPosition, *speed,
        *attack, *release, *brightness, *harmonics, *velocity, *bendRange, *polyphony, *gain;
    std::atomic<float>*walkOn, *walkMode, *walkAmount, *walkRate, *walkSync, *walkSyncLen, *walkGlide, *walkTether, *walkDims,
        *walkFocus, *walkPerVoice, *walkSeed, *walkRestart, *walkFreeze, *velDest, *velAmount, *mwDest, *mwAmount, *atDest,
        *atAmount, *macro, *voiceSpread;
    std::atomic<float>*mpeOn, *mpeZone, *mpeBendRange, *mpePressDest, *mpePressAmount, *mpePressCurve, *mpeSmoothing,
        *mpeSlideDest, *mpeSlideAmount, *mpeSlideMode, *noise, *keytrack, *levelLock, *pitchEnv;
    struct LfoRefs
    {
        std::atomic<float>*on, *shape, *rate, *sync, *syncLen, *depth, *target;
    };
    std::array<LfoRefs, 2> lfos {};
};

} // namespace pcsplugin
