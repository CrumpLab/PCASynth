#pragma once

#include "PluginProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>

// Stage 2 editor: a model bar (load, factory space, jump to a training sound)
// above JUCE's generic parameter panel. The custom UI is Stage 3.
class PCASynthEditor final : public juce::AudioProcessorEditor,
                             public juce::FileDragAndDropTarget,
                             private juce::ChangeListener,
                             private juce::Timer
{
public:
    explicit PCASynthEditor (PCASynthProcessor&);
    ~PCASynthEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int, int) override;

    void refreshModel();
    juce::String getStatus() const { return status.getText(); }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refreshModel(); }
    void timerCallback() override;
    void chooseModelFile();
    void showMessage (const juce::String& text);

    PCASynthProcessor& processor;
    juce::TextButton loadButton { "Load Model..." }, factoryButton { "Factory Space" }, meanButton { "Centre" };
    juce::Label title, modelInfo, jumpLabel { {}, "Jump to" }, status;
    juce::ComboBox soundBox;
    juce::GenericAudioProcessorEditor generic;
    std::unique_ptr<juce::FileChooser> chooser;
    bool dragging = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PCASynthEditor)
};
