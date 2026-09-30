#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

// The sound at the current point: harmonic levels (rows, harmonic 1 at the
// bottom) over time (columns), as a heat map. Loop or scan markers and the
// playheads of sounding voices are drawn on top.
class EnvelopeView final : public juce::Component
{
public:
    explicit EnvelopeView (PCASynthProcessor& p);

    void setModel (std::shared_ptr<const pcs::Model> model);
    void refresh(); // rebuilds the heat map when the point changes; moves playheads

    void paint (juce::Graphics&) override;

    // The decoded levels (dB) that the image shows: numFrames × numHarmonics.
    const std::vector<float>& levels() const noexcept { return db; }

private:
    juce::Rectangle<float> plotArea() const;
    void rebuild();
    // Colour range bottom: the model floor, but no lower than -60 dB so the audible range has contrast.
    float displayFloor() const noexcept { return model != nullptr ? juce::jmax (model->floorDb, -60.0f) : -60.0f; }

    PCASynthProcessor& processor;
    std::shared_ptr<const pcs::Model> model;
    std::array<float, pcs::kMaxComponents> shown {};
    bool dirty = true;
    std::vector<float> db, noise;
    float pitchDelta = 0.0f;
    juce::Image image;
    std::array<float, pcs::Synth::kMaxVoices> playheads {};
    int numPlayheads = 0;
};
