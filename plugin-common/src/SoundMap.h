#pragma once

#include "SpaceProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

// The training sounds plotted on two chosen components (SD units). Click a
// sound to jump to it; drag anywhere else to move the point along the two
// axes (the other components stay where they are).
class SoundMap final : public juce::Component
{
public:
    explicit SoundMap (SpaceProcessor& p);

    void setModel (std::shared_ptr<const pcs::Space> model);
    void refresh(); // repaints when the point has moved

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> plotArea() const;
    juce::Point<float> toScreen (float zx, float zy) const;
    juce::Point<float> toSpace (juce::Point<float> screen) const;
    int soundAt (juce::Point<float> screen) const;
    void moveTo (juce::Point<float> screen);
    void axesChanged();

    SpaceProcessor& processor;
    std::shared_ptr<const pcs::Space> model;
    std::vector<std::vector<float>> soundZ;
    juce::ComboBox xAxis, yAxis;
    juce::Label xLabel { {}, "X" }, yLabel { {}, "Y" };
    int ax = 0, ay = 1;
    float range = 3.0f;
    SpaceProcessor::Point point {};
    // Stage 5: where the modulation takes the point (while audio runs), its
    // recent path, and each voice's own point.
    SpaceProcessor::Point heard {};
    bool showHeard = false;
    std::vector<juce::Point<float>> trail; // in space units (the current axes)
    std::vector<juce::Point<float>> voices;
    int hover = -1;
    bool dragging = false;
};
