#include "PluginProcessor.h"
#include "PluginEditor.h"

#include "FactoryModel.h"

PCASynthProcessor::PCASynthProcessor() : SpaceProcessor (pcsplugin::Kind::Harmonic) { loadFactoryModel(); }

std::shared_ptr<const pcs::Model> PCASynthProcessor::factoryModel()
{
    static const std::shared_ptr<const pcs::Model> m = [] {
        auto loaded = std::make_shared<pcs::Model> (pcs::deserializeModel (
            reinterpret_cast<const uint8_t*> (FactoryModel::synthetic_pcsm), static_cast<size_t> (FactoryModel::synthetic_pcsmSize)));
        return std::shared_ptr<const pcs::Model> (std::move (loaded));
    }();
    return m;
}

juce::AudioProcessorEditor* PCASynthProcessor::createEditor() { return new PCASynthEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PCASynthProcessor(); }
