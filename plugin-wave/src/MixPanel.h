#pragma once

#include "WaveProcessor.h"

#include "SpaceEditor.h"

// A mixer for a waveform space. Every point is the centre plus amounts of the
// training sounds (z = Σ a_i z_i): drag a sound's bar to mix more or less of
// it in (or subtract it), click its name to go to it, Clear to go back to the
// centre. A tick shows each sound's amount in what you actually hear, when a
// walk, LFO, MPE or the Exaggerate knob moves it away from the point you set.
// When the point is moved another way (the map, the pad, a preset), the
// amounts are read back from it.
class MixPanel final : public SpaceView
{
public:
    explicit MixPanel (PCAWaveProcessor& p);

    void setModel (std::shared_ptr<const pcs::Space> space) override;
    void refresh() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

    // The amounts you set, and the heard mix as weights summing to 1.
    const std::vector<double>& getAmounts() const noexcept { return amounts; }
    const std::vector<double>& getWeights() const noexcept { return weights; }
    // Sets one sound's amount and moves the point there (as a drag does).
    void setAmount (int sound, double amount);
    void clearMix();

private:
    // The grid: as many rows as fit the height, columns as needed. When the
    // cells would get too narrow, only the most used sounds are shown.
    void layout();
    juce::Rectangle<int> cellBounds (int slot) const;
    juce::Rectangle<int> barBounds (int slot) const;
    int slotAt (juce::Point<int> p, bool& onBar) const;

    void syncFromPoint();  // read the amounts back if the point moved elsewhere
    void applyAmounts();   // move the point to the amounts
    void storeAmounts();   // keep them with the project
    void loadAmounts();

    PCAWaveProcessor& processor;
    std::shared_ptr<const pcs::WaveModel> model;
    std::vector<double> amounts, heard, weights;
    std::vector<int> shown; // sound index per grid slot, in sound order
    int rows = 1, cols = 1, cellW = 100, cellH = 16;

    int dragging = -1, pressedName = -1;
    double dragStart = 0.0;
    juce::TextButton clearButton { "Clear Mix" };
};
