#pragma once

#include "SpaceProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

// The preset row: previous/next, the list (factory presets by category, then
// the user's .pcspreset files), and saving the current sound as a preset.
class PresetBar final : public juce::Component
{
public:
    explicit PresetBar (SpaceProcessor&);

    void refresh(); // rescans the user folder and shows the current preset
    void resized() override;
    std::function<void (const juce::String&)> onMessage;

private:
    static constexpr int kUserBase = 1000, kLoadFile = 9000, kOpenFolder = 9001;
    void choose (int id);
    void step (int delta);
    void save();
    void loadFile (const juce::File& file);

    SpaceProcessor& processor;
    juce::Label label { {}, "Preset" };
    juce::TextButton prev { "<" }, next { ">" }, saveButton { "Save Preset..." };
    juce::ComboBox box;
    juce::Array<juce::File> userFiles;
    std::unique_ptr<juce::FileChooser> chooser;
};
