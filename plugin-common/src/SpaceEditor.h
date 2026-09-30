#pragma once

#include "Controls.h"
#include "InspectPanel.h"
#include "MorphPad.h"
#include "ParamGrid.h"
#include "SpaceProcessor.h"
#include "PresetBar.h"
#include "SoundMap.h"
#include "Theme.h"
#include "TrainPanel.h"

#include <juce_audio_utils/juce_audio_utils.h>

// A view that follows the space and the point: each plugin's view of the
// current point (PCASynth: harmonics over time; PCAWave: the waveform) and
// any extra tabs it adds.
class SpaceView : public juce::Component
{
public:
    virtual void setModel (std::shared_ptr<const pcs::Space> space) = 0;
    virtual void refresh() = 0; // the editor's timer (20 Hz)
};

// The plugin window, shared by both plugins: a model bar with presets, the
// sound map, the morph pad, the plugin's point view, tabs (components, the
// plugin's extras, random walk, LFOs and expression, MPE) and the controls,
// with the Train and Inspect panels over the middle.
class SpaceEditor : public juce::AudioProcessorEditor,
                    public juce::FileDragAndDropTarget,
                    private juce::ChangeListener,
                    private juce::Timer
{
public:
    struct Tab
    {
        juce::String name;
        std::unique_ptr<SpaceView> view;
    };
    SpaceEditor (SpaceProcessor&, const juce::String& title, std::unique_ptr<SpaceView> pointView, std::vector<Tab> extraTabs);
    ~SpaceEditor() override;

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
    SpaceView& getPointView() noexcept { return *pointView; }
    TrainPanel& getTrainPanel() noexcept { return trainPanel; }
    InspectPanel& getInspectPanel() noexcept { return inspectPanel; }
    void showInspector (bool show);
    void showTraining (bool show);
    PresetBar& getPresetBar() noexcept { return presetBar; }
    void selectTab (int index) { showTab (index); }

protected:
    // The summary of the space in the bar (name, sounds, what it models...).
    virtual juce::String describeSpace (const pcs::Space& space) const;
    bool isFactory() const { return processor.isFactoryModel(); }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refreshModel(); }
    void timerCallback() override { refresh(); }
    void chooseModelFile();
    void chooseExportFile();
    void chooseSaveFile();
    void showMessage (const juce::String& text);

    SpaceProcessor& processor;
    theme::LookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this, 600 };
    juce::TextButton loadButton { "Load Model..." }, factoryButton { "Factory Space" }, exportButton { "Export WAV..." },
        meanButton { "Centre" }, trainToggle { "Train..." }, inspectToggle { "Inspect..." }, saveButton { "Save Model..." };
    juce::Label title, modelInfo, jumpLabel { {}, "Jump to" }, status;
    juce::ComboBox soundBox;
    PresetBar presetBar;
    SoundMap soundMap;
    MorphPad morphPad;
    std::unique_ptr<SpaceView> pointView;
    std::vector<Tab> extraTabs;
    ComponentStrip strip;
    ControlPanel controls;
    TrainPanel trainPanel;
    InspectPanel inspectPanel;
    void updateVisibility(); // the main views, or the Train / Inspect panel over them
    WalkPanel walkPanel;
    ModPanel modPanel;
    MpePanel mpePanel;
    // Components, the extras, Random Walk, LFOs & Expression, MPE.
    std::vector<std::unique_ptr<juce::TextButton>> tabs;
    std::vector<juce::Component*> tabViews;
    void showTab (int index);
    std::unique_ptr<juce::FileChooser> chooser;
    juce::int64 messageUntil = 0;
    bool dragging = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpaceEditor)
};
