#pragma once

#include <juce_core/juce_core.h>

#include <utility>
#include <vector>

// Presets (Stage 8). Each plugin has its own factory presets
// (SpaceProcessor::getFactoryPresets): recipes on its factory space. Start
// from every parameter's default, change a few, and go to a point (a training
// sound, plus offsets). User presets are .pcspreset files holding the whole
// plugin state, model included (each plugin reads only its own).
namespace pcsplugin {

struct FactoryPreset
{
    juce::String name, category;
    juce::String sound;                                    // training sound to start at (empty: the centre)
    std::vector<std::pair<int, float>> offsets;            // (component index, SD) added to the sound's point
    std::vector<std::pair<juce::String, float>> params;    // parameter id, real value (choices: the index)
    juce::String direction;                                // "Toward Sound" target (empty: none)
};

constexpr const char* kPresetExtension = ".pcspreset";

// Where user presets live: ~/Library/Audio/Presets/CrumpLab/<plugin> on macOS,
// the user's application data folder elsewhere.
juce::File userPresetFolder();
// The .pcspreset files in `folder` (and its sub-folders), sorted by name.
juce::Array<juce::File> findUserPresets (const juce::File& folder);

} // namespace pcsplugin
