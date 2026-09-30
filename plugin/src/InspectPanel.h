#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

// Inspecting the model against its training sounds: which sounds it
// reproduces well, whether fidelity is lost in the analysis or in the PCA,
// and listening side by side: the original file, its analysis played back
// (no PCA), and the sound's point in the model.
class InspectPanel final : public juce::Component,
                           private juce::ListBoxModel,
                           private juce::ChangeListener
{
public:
    explicit InspectPanel (PCASynthProcessor& p);
    ~InspectPanel() override;

    void refresh();      // follows the model and the inspector
    void refreshModel(); // a new model: the list and the fit chart
    void paint (juce::Graphics&) override;
    void resized() override;
    void selectSound (int index); // and inspect it
    int getSelectedSound() const noexcept { return selected; }

    std::function<void()> onClose;

private:
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void selectedRowsChanged (int lastRowSelected) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }

    void paintFitChart (juce::Graphics&, juce::Rectangle<float>) const;
    void paintDetail (juce::Graphics&) const;
    void rebuildImages();
    int componentsForModel() const;

    PCASynthProcessor& processor;
    Inspector& inspector;
    std::shared_ptr<const pcs::Model> model;
    std::vector<Inspector::Scores> scores;
    std::shared_ptr<const pcs::SoundInspection> result;
    int selected = -1;

    juce::ListBox list { "sounds", this };
    juce::TextButton evaluateButton { "Evaluate All" }, closeButton { "Close" }, goButton { "Go to Sound" },
        playOriginal { "Original" }, playAnalysis { "Analysis" }, playModel { "Model" }, stopButton { "Stop" };
    juce::Slider components;
    juce::Label componentsLabel { {}, "Components" }, status;
    std::array<juce::Image, 3> images;

    juce::Rectangle<int> fitArea, detailArea;
    std::array<juce::Rectangle<int>, 3> specAreas;
    juce::Rectangle<int> pitchArea, headerArea;
};
