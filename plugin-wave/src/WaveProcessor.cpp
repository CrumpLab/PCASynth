#include "WaveProcessor.h"
#include "WaveEditor.h"

#include "WaveFactory.h"

PCAWaveProcessor::PCAWaveProcessor() : SpaceProcessor (pcsplugin::Kind::Wave) { loadFactoryModel(); }

std::shared_ptr<const pcs::WaveModel> PCAWaveProcessor::factoryModel()
{
    static const std::shared_ptr<const pcs::WaveModel> m = [] {
        auto loaded = std::make_shared<pcs::WaveModel> (pcs::deserializeWaveModel (
            reinterpret_cast<const uint8_t*> (WaveFactory::synthetic_pcsw), static_cast<size_t> (WaveFactory::synthetic_pcswSize)));
        return std::shared_ptr<const pcs::WaveModel> (std::move (loaded));
    }();
    return m;
}

juce::AudioProcessorEditor* PCAWaveProcessor::createEditor() { return new PCAWaveEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PCAWaveProcessor(); }
