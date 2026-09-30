#include "PluginEditor.h"

PCASynthEditor::PCASynthEditor (PCASynthProcessor& p) : SpaceEditor (p, "PCASynth", std::make_unique<EnvelopeView> (p), {}) {}

juce::String PCASynthEditor::describeSpace (const pcs::Space& space) const
{
    const auto* m = dynamic_cast<const pcs::Model*> (&space);
    if (m == nullptr)
        return SpaceEditor::describeSpace (space);
    double explained = 0.0;
    for (int j = 0; j < std::min (pcsplugin::kNumPcParams, m->numComponents()); ++j)
        explained += m->varianceExplained (j);
    const auto name = juce::String (m->title.empty() ? "Untitled" : m->title) + (isFactory() ? " (factory)" : "");
    juce::StringArray extras;
    if (m->numNoiseBands > 0)
        extras.add ("noise");
    if (m->hasPartials)
        extras.add ("partial tuning");
    if (m->hasPitchCurve)
        extras.add ("pitch curves");
    if (m->pitchTracking)
        extras.add ("timbre follows pitch");
    if (m->representation == pcs::Representation::ShapeLoudness)
        extras.add ("shape + loudness");
    else if (m->representation == pcs::Representation::Linear)
        extras.add ("linear");
    return name + "  |  " + juce::String (m->numSounds()) + " sounds, " + juce::String (m->numComponents()) + " components, "
         + juce::String (m->numHarmonics) + " harmonics" + (extras.isEmpty() ? juce::String() : " + " + extras.joinIntoString (", "))
         + ", " + juce::String (m->durationSeconds(), 1) + " s  |  PC1-16 explain " + juce::String (juce::roundToInt (100.0 * explained)) + " %";
}
