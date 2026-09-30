#pragma once

#include "EnvelopeView.h"
#include "PluginProcessor.h"
#include "SpaceEditor.h"

// PCASynth's window: the shared editor, with the harmonics-over-time view of
// the current point.
class PCASynthEditor final : public SpaceEditor
{
public:
    explicit PCASynthEditor (PCASynthProcessor& p);
    EnvelopeView& getEnvelopeView() noexcept { return static_cast<EnvelopeView&> (getPointView()); }

protected:
    juce::String describeSpace (const pcs::Space& space) const override;
};
