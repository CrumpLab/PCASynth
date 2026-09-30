#pragma once

#include "SpaceProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

// PC1..PC16 as vertical sliders, each with a bar showing how much of the
// training set's variance that component explains. Sliders beyond the
// model's components (or Components Used) are dimmed.
class ComponentStrip final : public juce::Component
{
public:
    explicit ComponentStrip (SpaceProcessor& p);
    void setModel (std::shared_ptr<const pcs::Space> model);
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    SpaceProcessor& processor;
    std::shared_ptr<const pcs::Space> model;
    std::array<juce::Slider, pcsplugin::kNumPcParams> sliders;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> attachments;
    int used = -1;
};

// Every other parameter as knobs (and the play mode as a menu), grouped.
class ControlPanel final : public juce::Component
{
public:
    explicit ControlPanel (SpaceProcessor& p);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    struct Group
    {
        juce::String title;
        std::vector<Knob*> knobs;
        bool hasMode = false;
        juce::Rectangle<int> bounds;
    };
    Knob& add (const juce::String& paramId, const juce::String& label);

    SpaceProcessor& processor;
    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<Group> groups;
    juce::ComboBox playMode;
    juce::Label playModeLabel { {}, "Play Mode" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> playModeAttachment;
};
