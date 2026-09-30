#pragma once

#include "SpaceProcessor.h"

#include "pcs/Model.h"

namespace pcsplugin {
const std::vector<FactoryPreset>& harmonicFactoryPresets();
}

// PCASynth: plays harmonic spaces (harmonic amplitude envelopes, noise,
// partial tuning, pitch curves). Everything else is SpaceProcessor's.
class PCASynthProcessor final : public SpaceProcessor
{
public:
    PCASynthProcessor();

    juce::AudioProcessorEditor* createEditor() override;
    std::shared_ptr<const pcs::Space> factorySpace() const override { return factoryModel(); }
    const std::vector<pcsplugin::FactoryPreset>& getFactoryPresets() const override { return pcsplugin::harmonicFactoryPresets(); }

    // The built-in space: 60 synthetic instrument notes (see plan.md §3).
    static std::shared_ptr<const pcs::Model> factoryModel();
    // The current space, as the harmonic model it always is in this plugin.
    std::shared_ptr<const pcs::Model> getModel() const { return std::dynamic_pointer_cast<const pcs::Model> (getSpace()); }
};
