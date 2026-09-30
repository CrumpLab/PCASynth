#pragma once

#include "MixPanel.h"
#include "WaveProcessor.h"
#include "WaveView.h"

#include "SpaceEditor.h"

// PCAWave's window: the shared editor, with the waveform view of the current
// point and a Mix tab (the point as a weighted sum of the training sounds).
class PCAWaveEditor final : public SpaceEditor
{
public:
    explicit PCAWaveEditor (PCAWaveProcessor& p);
    WaveView& getWaveView() noexcept { return static_cast<WaveView&> (getPointView()); }

protected:
    juce::String describeSpace (const pcs::Space& space) const override;
};
