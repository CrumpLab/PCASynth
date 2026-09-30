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

// Seconds shown as ms below one second.
void addTime (Layout& l, const juce::String& id, const juce::String& name, juce::NormalisableRange<float> range, float def)
{
    l.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id, 1 }, name, range, def,
        juce::AudioParameterFloatAttributes()
            .withLabel ("s")
            .withStringFromValueFunction ([] (float v, int) {
                return v < 1.0f ? juce::String (v * 1000.0f, v < 0.01f ? 1 : 0) + " ms" : juce::String (v, 2) + " s";
            })
            .withValueFromStringFunction ([] (const juce::String& s) {
                const float x = s.getFloatValue();
                return s.containsIgnoreCase ("ms") ? x / 1000.0f : x;
            })));
}

juce::AudioParameterFloatAttributes percent()
{
    return juce::AudioParameterFloatAttributes()
        .withLabel ("%")
        .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)); })
        .withValueFromStringFunction ([] (const juce::String& s) { return s.getFloatValue() / 100.0f; });
}
} // namespace

juce::StringArray targetNames (bool withOff)
{
    juce::StringArray names;
    if (withOff)
        names.add ("Off");
    for (int j = 0; j < pcs::kNumPcDestinations; ++j)
        names.add ("PC" + juce::String (j + 1));
    names.add ("Toward Sound");
    return names;
}

namespace {
void addBool (Layout& l, const juce::String& id, const juce::String& name, bool def)
{
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, def));
}
void addChoice (Layout& l, const juce::String& id, const juce::String& name, const juce::StringArray& choices, int def)
{
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, choices, def));
}
void addPercent (Layout& l, const juce::String& id, const juce::String& name, float def)
{
    l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (0.0f, 1.0f), def, percent()));
}
void addHz (Layout& l, const juce::String& id, const juce::String& name, float def)
{
    juce::NormalisableRange<float> r (0.01f, 20.0f);
    r.setSkewForCentre (0.5f);
    l.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id, 1 }, name, r, def,
        juce::AudioParameterFloatAttributes().withLabel ("Hz").withStringFromValueFunction ([] (float v, int) {
            return v < 1.0f ? juce::String (v, 2) + " Hz" : juce::String (v, 1) + " Hz";
        })));
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    Layout l;
    for (int j = 0; j < kNumPcParams; ++j)
        addFloat (l, pcId (j), "PC" + juce::String (j + 1), { -4.0f, 4.0f }, 0.0f, "SD");
    addInt (l, id::components, "Components Used", 0, pcs::kMaxComponents, pcs::kMaxComponents);
    addFloat (l, id::exaggerate, "Exaggerate", { 0.0f, 3.0f }, 1.0f, "x");
    addTime (l, id::morphTime, "Morph Time", skewed (0.0f, 2.0f, 0.2f), 0.05f);
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::playMode, 1 }, "Play Mode",
                                                         playModeNames(), 1));
    l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::loopStart, 1 }, "Loop Start",
                                                        juce::NormalisableRange<float> (0.0f, 1.0f), 0.3f, percent()));
    l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::loopEnd, 1 }, "Loop End",
                                                        juce::NormalisableRange<float> (0.0f, 1.0f), 0.7f, percent()));
    l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::scanPosition, 1 }, "Scan Position",
                                                        juce::NormalisableRange<float> (0.0f, 1.0f), 0.2f, percent()));
    addFloat (l, id::speed, "Speed", skewed (0.0f, 4.0f, 1.0f), 1.0f, "x");
    addTime (l, id::attack, "Attack", skewed (0.0005f, 2.0f, 0.1f), 0.003f);
    addTime (l, id::release, "Release", skewed (0.005f, 5.0f, 0.5f), 0.3f);
    addFloat (l, id::brightness, "Brightness", { -12.0f, 12.0f }, 0.0f, "dB/oct");
    addInt (l, id::harmonics, "Harmonics", 1, pcs::Synth::kMaxHarmonics, pcs::Synth::kMaxHarmonics);
    addFloat (l, id::velocity, "Velocity Sensitivity", { 0.0f, 1.0f }, 1.0f);
    addInt (l, id::bendRange, "Pitch Bend Range", 0, 24, 2);
    addInt (l, id::polyphony, "Polyphony", 1, pcs::Synth::kMaxVoices, 16);
    addFloat (l, id::gain, "Gain", { -48.0f, 12.0f }, -6.0f, "dB");

    // Stage 5: random walk.
    addBool (l, id::walkOn, "Walk On", false);
    addChoice (l, id::walkMode, "Walk Mode", walkModeNames(), 0);
    addFloat (l, id::walkAmount, "Walk Amount", { 0.0f, 4.0f }, 1.0f, "SD");
    addHz (l, id::walkRate, "Walk Rate", 0.25f);
    addBool (l, id::walkSync, "Walk Sync", false);
    addChoice (l, id::walkSyncLen, "Walk Step", syncNames(), 4);
    addPercent (l, id::walkGlide, "Walk Glide", 0.7f);
    addPercent (l, id::walkTether, "Walk Tether", 0.5f);
    addInt (l, id::walkDims, "Walk Components", 1, pcs::kMaxComponents, 4);
    addChoice (l, id::walkFocus, "Walk Focus", walkFocusNames(), 0);
    addPercent (l, id::walkPerVoice, "Walk Per Voice", 0.0f);
    addInt (l, id::walkSeed, "Walk Seed", 1, 9999, 1);
    addBool (l, id::walkRestart, "Walk Restart On Note", false);
    addBool (l, id::walkFreeze, "Walk Freeze", false);

    // LFOs.
    for (int n = 0; n < 2; ++n)
    {
        const auto name = "LFO " + juce::String (n + 1) + " ";
        addBool (l, id::lfo (n, "on"), name + "On", false);
        addChoice (l, id::lfo (n, "shape"), name + "Shape", lfoShapeNames(), 0);
        addHz (l, id::lfo (n, "rate"), name + "Rate", n == 0 ? 0.5f : 0.13f);
        addBool (l, id::lfo (n, "sync"), name + "Sync", false);
        addChoice (l, id::lfo (n, "sync_len"), name + "Cycle", syncNames(), 4);
        addFloat (l, id::lfo (n, "depth"), name + "Depth", { -4.0f, 4.0f }, 1.0f, "SD");
        addChoice (l, id::lfo (n, "target"), name + "Target", targetNames (false), n);
    }

    // Expression, macro, spread.
    addChoice (l, id::velDest, "Velocity To", targetNames (true), 0);
    addFloat (l, id::velAmount, "Velocity Amount", { -4.0f, 4.0f }, 1.0f, "SD");
    addChoice (l, id::mwDest, "Mod Wheel To", targetNames (true), pcs::kTowardSound + 1);
    addFloat (l, id::mwAmount, "Mod Wheel Amount", { -4.0f, 4.0f }, 1.0f, "SD");
    addChoice (l, id::atDest, "Aftertouch To", targetNames (true), 0);
    addFloat (l, id::atAmount, "Aftertouch Amount", { -4.0f, 4.0f }, 1.0f, "SD");
    addPercent (l, id::macro, "Macro", 0.0f);
    addFloat (l, id::voiceSpread, "Voice Spread", { 0.0f, 2.0f }, 0.0f, "SD");
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
    walkOn = s.getRawParameterValue (id::walkOn);
    walkMode = s.getRawParameterValue (id::walkMode);
    walkAmount = s.getRawParameterValue (id::walkAmount);
    walkRate = s.getRawParameterValue (id::walkRate);
    walkSync = s.getRawParameterValue (id::walkSync);
    walkSyncLen = s.getRawParameterValue (id::walkSyncLen);
    walkGlide = s.getRawParameterValue (id::walkGlide);
    walkTether = s.getRawParameterValue (id::walkTether);
    walkDims = s.getRawParameterValue (id::walkDims);
    walkFocus = s.getRawParameterValue (id::walkFocus);
    walkPerVoice = s.getRawParameterValue (id::walkPerVoice);
    walkSeed = s.getRawParameterValue (id::walkSeed);
    walkRestart = s.getRawParameterValue (id::walkRestart);
    walkFreeze = s.getRawParameterValue (id::walkFreeze);
    velDest = s.getRawParameterValue (id::velDest);
    velAmount = s.getRawParameterValue (id::velAmount);
    mwDest = s.getRawParameterValue (id::mwDest);
    mwAmount = s.getRawParameterValue (id::mwAmount);
    atDest = s.getRawParameterValue (id::atDest);
    atAmount = s.getRawParameterValue (id::atAmount);
    macro = s.getRawParameterValue (id::macro);
    voiceSpread = s.getRawParameterValue (id::voiceSpread);
    for (int n = 0; n < 2; ++n)
        lfos[static_cast<size_t> (n)] = { s.getRawParameterValue (id::lfo (n, "on")), s.getRawParameterValue (id::lfo (n, "shape")),
                                          s.getRawParameterValue (id::lfo (n, "rate")), s.getRawParameterValue (id::lfo (n, "sync")),
                                          s.getRawParameterValue (id::lfo (n, "sync_len")), s.getRawParameterValue (id::lfo (n, "depth")),
                                          s.getRawParameterValue (id::lfo (n, "target")) };
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

    auto index = [&get] (const std::atomic<float>* a) { return juce::roundToInt (get (a)); };
    auto& w = p.mod.walk;
    w.enabled = get (walkOn) > 0.5f;
    w.mode = static_cast<pcs::WalkMode> (juce::jlimit (0, 3, index (walkMode)));
    w.amount = get (walkAmount);
    w.rate = get (walkRate);
    w.sync = get (walkSync) > 0.5f;
    w.syncBeats = syncBeats (index (walkSyncLen));
    w.glide = get (walkGlide);
    w.tether = get (walkTether);
    w.dims = index (walkDims);
    w.focus = static_cast<pcs::WalkFocus> (juce::jlimit (0, 1, index (walkFocus)));
    w.perVoice = get (walkPerVoice);
    w.seed = static_cast<uint32_t> (index (walkSeed));
    w.restartOnNote = get (walkRestart) > 0.5f;
    w.freeze = get (walkFreeze) > 0.5f;
    for (size_t n = 0; n < lfos.size(); ++n)
    {
        auto& l = p.mod.lfo[n];
        const auto& r = lfos[n];
        l.enabled = get (r.on) > 0.5f;
        l.shape = static_cast<pcs::LfoShape> (juce::jlimit (0, 5, index (r.shape)));
        l.rate = get (r.rate);
        l.sync = get (r.sync) > 0.5f;
        l.syncBeats = syncBeats (index (r.syncLen));
        l.depth = get (r.depth);
        l.target = index (r.target);
    }
    // Expression choices have "Off" first.
    p.mod.velocity = { index (velDest) - 1, get (velAmount) };
    p.mod.modWheel = { index (mwDest) - 1, get (mwAmount) };
    p.mod.pressure = { index (atDest) - 1, get (atAmount) };
    p.mod.macro = get (macro);
    p.mod.voiceSpread = get (voiceSpread);
    return p;
}

} // namespace pcsplugin
