#pragma once

#include "SpaceProcessor.h"

#include "pcs/WaveModel.h"

namespace pcsplugin {
const std::vector<FactoryPreset>& waveFactoryPresets();
}

// PCAWave: plays waveform spaces (PCA on the waveforms themselves; every point
// a weighted sum of the training sounds). Everything else is SpaceProcessor's.
class PCAWaveProcessor final : public SpaceProcessor
{
public:
    PCAWaveProcessor();

    juce::AudioProcessorEditor* createEditor() override;
    std::shared_ptr<const pcs::Space> factorySpace() const override { return factoryModel(); }
    const std::vector<pcsplugin::FactoryPreset>& getFactoryPresets() const override { return pcsplugin::waveFactoryPresets(); }

    // The built-in space: the synthetic instrument notes as waveforms (24 kHz, 2 s).
    static std::shared_ptr<const pcs::WaveModel> factoryModel();
    std::shared_ptr<const pcs::WaveModel> getWaveModel() const { return std::dynamic_pointer_cast<const pcs::WaveModel> (getSpace()); }
};
