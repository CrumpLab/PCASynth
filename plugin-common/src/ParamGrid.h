#pragma once

#include "SpaceProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

// A panel of parameter controls in titled groups, laid out in rows: knobs,
// menus (choice parameters), toggles and custom components. Used by the
// movement tabs (Stage 5).
class ParamGrid : public juce::Component
{
public:
    explicit ParamGrid (SpaceProcessor& p);

    void group (const juce::String& title, int row);
    juce::Slider& knob (const juce::String& paramId, const juce::String& label);
    juce::ComboBox& menu (const juce::String& paramId, const juce::String& label, int widthCells = 1);
    juce::ToggleButton& toggle (const juce::String& paramId, const juce::String& label);
    void custom (juce::Component& c, const juce::String& label, int widthCells = 1, bool fillCell = false);
    // Knobs added after this are compact horizontal bars (for two-row panels).
    void setCompact (bool c) { compact = c; }

    // Called by the editor's timer: dims controls that don't apply right now.
    virtual void refresh() {}

    void paint (juce::Graphics&) override;
    void resized() override;

protected:
    float value (const juce::String& paramId) const;
    SpaceProcessor& processor;

private:
    struct Item
    {
        juce::Component* component = nullptr;
        std::unique_ptr<juce::Label> label;
        int cells = 1;
        bool fill = false; // custom components that take the whole cell
    };
    struct Group
    {
        juce::String title;
        int row = 0;
        std::vector<Item> items;
        juce::Rectangle<int> bounds;
    };
    Item& add (juce::Component& c, const juce::String& label, int cells);

    std::vector<Group> groups;
    bool compact = false;
    std::vector<std::unique_ptr<juce::Component>> owned;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> comboAttachments;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAttachments;
};

// The random walk: mode, motion, which components, repeatability.
class WalkPanel final : public ParamGrid
{
public:
    explicit WalkPanel (SpaceProcessor& p);
    void refresh() override;

private:
    juce::Slider *rate = nullptr, *glide = nullptr, *tether = nullptr, *dims = nullptr;
    juce::ComboBox *step = nullptr, *focus = nullptr;
};

// Two LFOs, velocity / mod wheel / aftertouch routing, the direction sound,
// the macro and voice spread.
class ModPanel final : public ParamGrid
{
public:
    explicit ModPanel (SpaceProcessor& p);
    void setModel (std::shared_ptr<const pcs::Space> model);
    void refresh() override;

private:
    juce::ComboBox directionBox;
    std::array<juce::Slider*, 2> lfoRate {};
    std::array<juce::ComboBox*, 2> lfoCycle {};
};

// Each sounding note's per-note expression: channel, bend, pressure, slide.
class NoteMonitor final : public juce::Component
{
public:
    explicit NoteMonitor (SpaceProcessor& p) : processor (p) {}
    void refresh();
    void paint (juce::Graphics&) override;

private:
    SpaceProcessor& processor;
    std::array<pcs::Synth::VoiceInfo, SpaceProcessor::kMaxShownVoices> info {};
    int count = 0;
};

// MPE (Stage 6): zone and ranges, where pressure and slide move each note, and
// a monitor of what the controller sends.
class MpePanel final : public ParamGrid
{
public:
    explicit MpePanel (SpaceProcessor& p);
    void refresh() override;

private:
    NoteMonitor monitor;
};
