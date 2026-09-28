#include "Params.h"

namespace pcsplugin {

namespace {
using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;

juce::NormalisableRange<float> skewed (float lo, float hi, float centre)
{
    juce::NormalisableRange<float> r (lo, hi);
    r.setSkewForCentre (centre);
    return r;
}

void addFloat (Layout& l, const juce::String& id, const juce::String& name, juce::NormalisableRange<float> range,
               float def, const juce::String& unit = {})
{
    const int decimals = range.end - range.start > 20.0f ? 1 : 2;
    l.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id, 1 }, name, range, def,
        juce::AudioParameterFloatAttributes().withLabel (unit).withStringFromValueFunction (
            [decimals] (float v, int) { return juce::String (v, decimals); })));
}

void addInt (Layout& l, const juce::String& id, const juce::String& name, int lo, int hi, int def)
{
    l.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { id, 1 }, name, lo, hi, def));
}

juce::AudioParameterFloatAttributes percent()
{
    return juce::AudioParameterFloatAttributes()
        .withLabel ("%")
        .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)); })
        .withValueFromStringFunction ([] (const juce::String& s) { return s.getFloatValue() / 100.0f; });
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    Layout l;
    for (int j = 0; j < kNumPcParams; ++j)
        addFloat (l, pcId (j), "PC" + juce::String (j + 1), { -4.0f, 4.0f }, 0.0f, "SD");
    addInt (l, id::components, "Components Used", 0, pcs::kMaxComponents, pcs::kMaxComponents);
    addFloat (l, id::exaggerate, "Exaggerate", { 0.0f, 3.0f }, 1.0f, "x");
    addFloat (l, id::morphTime, "Morph Time", skewed (0.0f, 2.0f, 0.2f), 0.05f, "s");
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::playMode, 1 }, "Play Mode",
                                                         playModeNames(), 1));
    l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::loopStart, 1 }, "Loop Start",
                                                        juce::NormalisableRange<float> (0.0f, 1.0f), 0.3f, percent()));
    l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::loopEnd, 1 }, "Loop End",
                                                        juce::NormalisableRange<float> (0.0f, 1.0f), 0.7f, percent()));
    l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::scanPosition, 1 }, "Scan Position",
                                                        juce::NormalisableRange<float> (0.0f, 1.0f), 0.2f, percent()));
    addFloat (l, id::speed, "Speed", skewed (0.0f, 4.0f, 1.0f), 1.0f, "x");
    addFloat (l, id::attack, "Attack", skewed (0.0005f, 2.0f, 0.1f), 0.003f, "s");
    addFloat (l, id::release, "Release", skewed (0.005f, 5.0f, 0.5f), 0.3f, "s");
    addFloat (l, id::brightness, "Brightness", { -12.0f, 12.0f }, 0.0f, "dB/oct");
    addInt (l, id::harmonics, "Harmonics", 1, pcs::Synth::kMaxHarmonics, pcs::Synth::kMaxHarmonics);
    addFloat (l, id::velocity, "Velocity Sensitivity", { 0.0f, 1.0f }, 1.0f);
    addInt (l, id::bendRange, "Pitch Bend Range", 0, 24, 2);
    addInt (l, id::polyphony, "Polyphony", 1, pcs::Synth::kMaxVoices, 16);
    addFloat (l, id::gain, "Gain", { -48.0f, 12.0f }, -6.0f, "dB");
    return l;
}

ParamReader::ParamReader (juce::AudioProcessorValueTreeState& s)
{
    for (int j = 0; j < kNumPcParams; ++j)
        pc[static_cast<size_t> (j)] = s.getRawParameterValue (pcId (j));
    components = s.getRawParameterValue (id::components);
    exaggerate = s.getRawParameterValue (id::exaggerate);
    morphTime = s.getRawParameterValue (id::morphTime);
    playMode = s.getRawParameterValue (id::playMode);
    loopStart = s.getRawParameterValue (id::loopStart);
    loopEnd = s.getRawParameterValue (id::loopEnd);
    scanPosition = s.getRawParameterValue (id::scanPosition);
    speed = s.getRawParameterValue (id::speed);
    attack = s.getRawParameterValue (id::attack);
    release = s.getRawParameterValue (id::release);
    brightness = s.getRawParameterValue (id::brightness);
    harmonics = s.getRawParameterValue (id::harmonics);
    velocity = s.getRawParameterValue (id::velocity);
    bendRange = s.getRawParameterValue (id::bendRange);
    polyphony = s.getRawParameterValue (id::polyphony);
    gain = s.getRawParameterValue (id::gain);
}

pcs::SynthParams ParamReader::read (const std::array<float, pcs::kMaxComponents>& detail) const noexcept
{
    auto get = [] (const std::atomic<float>* a) { return a->load (std::memory_order_relaxed); };
    pcs::SynthParams p;
    for (int j = 0; j < pcs::kMaxComponents; ++j)
        p.z[static_cast<size_t> (j)] = j < kNumPcParams ? get (pc[static_cast<size_t> (j)]) : detail[static_cast<size_t> (j)];
    p.activeComponents = juce::roundToInt (get (components));
    p.exaggerate = get (exaggerate);
    p.morphTime = get (morphTime);
    p.mode = static_cast<pcs::PlayMode> (juce::jlimit (0, 3, juce::roundToInt (get (playMode))));
    p.loopStart = get (loopStart);
    p.loopEnd = get (loopEnd);
    p.scanPosition = get (scanPosition);
    p.speed = get (speed);
    p.attack = get (attack);
    p.release = get (release);
    p.tiltDbPerOctave = get (brightness);
    p.maxHarmonics = juce::roundToInt (get (harmonics));
    p.velocitySensitivity = get (velocity);
    p.pitchBendRange = get (bendRange);
    p.polyphony = juce::roundToInt (get (polyphony));
    p.gainDb = get (gain);
    return p;
}

} // namespace pcsplugin
