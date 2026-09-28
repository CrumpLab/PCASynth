#pragma once

#include "pcs/Synth.h"

#include <juce_audio_processors/juce_audio_processors.h>

// Host parameters. PC1..PC16 are the point in the space (SD units); the
// remaining components (17..32) are "detail" kept in the plugin state and set
// when you jump to a training sound.
namespace pcsplugin {

constexpr int kNumPcParams = 16;

inline juce::String pcId (int j) { return "pc" + juce::String (j + 1); }

namespace id {
inline const juce::String components = "components", exaggerate = "exaggerate", morphTime = "morph_time",
                          playMode = "play_mode", loopStart = "loop_start", loopEnd = "loop_end",
                          scanPosition = "scan_position", speed = "speed", attack = "attack", release = "release",
                          brightness = "brightness", harmonics = "harmonics", velocity = "velocity",
                          bendRange = "bend_range", polyphony = "polyphony", gain = "gain";
}

inline juce::StringArray playModeNames() { return { "One-shot", "Loop", "Ping-pong", "Scan" }; }

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

// Reads the current parameter values (any thread; lock-free).
class ParamReader
{
public:
    explicit ParamReader (juce::AudioProcessorValueTreeState& state);
    // `detail` supplies components 17..32.
    pcs::SynthParams read (const std::array<float, pcs::kMaxComponents>& detail) const noexcept;

private:
    std::array<std::atomic<float>*, kNumPcParams> pc {};
    std::atomic<float>*components, *exaggerate, *morphTime, *playMode, *loopStart, *loopEnd, *scanPosition, *speed,
        *attack, *release, *brightness, *harmonics, *velocity, *bendRange, *polyphony, *gain;
};

} // namespace pcsplugin
