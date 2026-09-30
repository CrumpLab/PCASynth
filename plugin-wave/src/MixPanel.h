#pragma once

#include "WaveProcessor.h"

#include "SpaceEditor.h"

// A point of a waveform space is a weighted sum of the training waveforms:
// this shows every training sound's weight at the point heard (they sum to
// 100 %; negative weights subtract a sound). Click a sound to go to it.
class MixPanel final : public SpaceView
{
public:
    explicit MixPanel (PCAWaveProcessor& p);

    void setModel (std::shared_ptr<const pcs::Space> space) override;
    void refresh() override;
    void paint (juce::Graphics&) override;
    void resized() override { layout(); }
    void mouseUp (const juce::MouseEvent&) override;

    const std::vector<double>& getWeights() const noexcept { return weights; }

private:
    // The grid: as many rows as fit the height, columns as needed. When the
    // cells would get too narrow, only the most heavily weighted sounds are shown.
    void layout();
    juce::Rectangle<int> cellBounds (int slot) const;

    PCAWaveProcessor& processor;
    std::shared_ptr<const pcs::WaveModel> model;
    std::vector<double> weights;
    std::vector<int> shown; // sound index per grid slot, in sound order
    int rows = 1, cols = 1, cellW = 100, cellH = 16;
};
