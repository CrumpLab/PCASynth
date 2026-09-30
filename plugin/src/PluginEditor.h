#pragma once

#include "Controls.h"
#include "EnvelopeView.h"
#include "MorphPad.h"
#include "ParamGrid.h"
#include "PluginProcessor.h"
#include "PresetBar.h"
#include "SoundMap.h"
#include "Theme.h"
#include "TrainPanel.h"

#include <juce_audio_utils/juce_audio_utils.h>

// The plugin window: a model bar, the sound map, the morph pad, the envelope
// view of the current point, the component sliders and the other controls.
class PCASynthEditor final : public juce::AudioProcessorEditor,
                             public juce::FileDragAndDropTarget,
                             private juce::ChangeListener,
                             private juce::Timer
{
public:
    explicit PCASynthEditor (PCASynthProcessor&);
    ~PCASynthEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray& files, int, int) override;

    void refreshModel();
    void refresh(); // what the timer does: follow the point, voices, status
    juce::String getStatus() const { return status.getText(); }
    SoundMap& getSoundMap() noexcept { return soundMap; }
    MorphPad& getMorphPad() noexcept { return morphPad; }
    EnvelopeView& getEnvelopeView() noexcept { return envelope; }
    TrainPanel& getTrainPanel() noexcept { return trainPanel; }
    PresetBar& getPresetBar() noexcept { return presetBar; }
    void selectTab (int index) { showTab (index); }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refreshModel(); }
    void timerCallback() override { refresh(); }
    void chooseModelFile();
    void chooseExportFile();
    void chooseSaveFile();
    void showTraining (bool show);
    void showMessage (const juce::String& text);

    PCASynthProcessor& processor;
    theme::LookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this, 600 };
    juce::TextButton loadButton { "Load Model..." }, factoryButton { "Factory Space" }, exportButton { "Export WAV..." },
        meanButton { "Centre" }, trainToggle { "Train..." }, saveButton { "Save Model..." };
    juce::Label title, modelInfo, jumpLabel { {}, "Jump to" }, status;
    juce::ComboBox soundBox;
    PresetBar presetBar;
    SoundMap soundMap;
    MorphPad morphPad;
    EnvelopeView envelope;
    ComponentStrip strip;
    ControlPanel controls;
    TrainPanel trainPanel;
    WalkPanel walkPanel;
    ModPanel modPanel;
    MpePanel mpePanel;
    std::array<juce::TextButton, 4> tabs { juce::TextButton ("Components"), juce::TextButton ("Random Walk"),
                                           juce::TextButton ("LFOs & Expression"), juce::TextButton ("MPE") };
    void showTab (int index);
    std::unique_ptr<juce::FileChooser> chooser;
    juce::int64 messageUntil = 0;
    bool dragging = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PCASynthEditor)
};
