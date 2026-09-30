#pragma once

#include "SpaceProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

// Four training sounds at the corners of a square. Dragging the puck blends
// their coordinates (bilinearly, over every component) and moves the point
// there. Along one edge it is a straight morph between two sounds.
class MorphPad final : public juce::Component
{
public:
    explicit MorphPad (SpaceProcessor& p);

    void setModel (std::shared_ptr<const pcs::Space> model);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    // Blend of the four corner sounds at (u, v) in [0, 1]² (u: left -> right, v: top -> bottom).
    SpaceProcessor::Point blend (float u, float v) const;

private:
    juce::Rectangle<float> padArea() const;
    void cornersChanged();
    void moveTo (juce::Point<float> screen);

    SpaceProcessor& processor;
    std::shared_ptr<const pcs::Space> model;
    std::array<juce::ComboBox, 4> corners; // top-left, top-right, bottom-left, bottom-right
    float u = 0.5f, v = 0.5f;
    bool dragging = false;
};
