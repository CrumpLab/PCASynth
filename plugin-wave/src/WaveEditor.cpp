#include "WaveEditor.h"

namespace {
std::vector<SpaceEditor::Tab> waveTabs (PCAWaveProcessor& p)
{
    std::vector<SpaceEditor::Tab> tabs;
    tabs.push_back ({ "Mix", std::make_unique<MixPanel> (p) });
    return tabs;
}
} // namespace

PCAWaveEditor::PCAWaveEditor (PCAWaveProcessor& p) : SpaceEditor (p, "PCAWave", std::make_unique<WaveView> (p), waveTabs (p)) {}

juce::String PCAWaveEditor::describeSpace (const pcs::Space& space) const
{
    const auto* m = dynamic_cast<const pcs::WaveModel*> (&space);
    if (m == nullptr)
        return SpaceEditor::describeSpace (space);
    double explained = 0.0;
    for (int j = 0; j < std::min (pcsplugin::kNumPcParams, m->numComponents()); ++j)
        explained += m->varianceExplained (j);
    const auto note = juce::MidiMessage::getMidiNoteName (juce::roundToInt (m->refMidi()), true, true, 4);
    return juce::String (m->title.empty() ? "Untitled" : m->title) + (isFactory() ? " (factory)" : "") + "  |  "
         + juce::String (m->numSounds()) + " sounds, " + juce::String (m->numComponents()) + " components, waveforms at "
         + juce::String (m->sampleRate / 1000.0, 0) + " kHz, " + juce::String (m->durationSeconds(), 1) + " s at " + note
         + "  |  PC1-16 explain " + juce::String (juce::roundToInt (100.0 * explained)) + " %";
}
