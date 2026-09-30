#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

// Builds a new sound space from audio files: the list of sounds (add files or
// folders, drop them on the window, remove), the analysis settings, and Train.
class TrainPanel final : public juce::Component,
                         private juce::ListBoxModel
{
public:
    explicit TrainPanel (PCASynthProcessor& p);

    void refresh(); // follows the trainer (list, progress, status)
    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void()> onClose;

private:
    int getNumRows() override { return static_cast<int> (entries.size()); }
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void deleteKeyPressed (int) override { removeSelected(); }

    void chooseFiles (bool folders);
    void removeSelected();
    void settingsFromControls();
    void controlsFromSettings();

    PCASynthProcessor& processor;
    Trainer& trainer;
    std::vector<Trainer::Entry> entries;
    int seenVersion = -1;

    juce::ListBox list { "sounds", this };
    juce::TextButton addFiles { "Add Files..." }, addFolder { "Add Folder..." }, removeButton { "Remove" },
        clearButton { "Clear" }, trainButton { "Train" }, closeButton { "Close" };
    juce::Label hint, status;
    juce::TextEditor title;
    juce::ComboBox note;
    juce::Slider duration, harmonics, floorDb, components;
    juce::ToggleButton normalize { "Match loudness" }, trim { "Align onsets" };
    std::vector<std::unique_ptr<juce::Label>> labels;
    double progressValue = 0.0;
    juce::ProgressBar progress { progressValue };
    std::unique_ptr<juce::FileChooser> chooser;
};
