// Headless checks of the plugin: MIDI in -> sound out, jumping to training
// sounds, and the state round trip with an embedded model.
#include "../src/PluginProcessor.h"
#include "TrainingSet.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <cstdio>

namespace {

int failures = 0;
void check (bool ok, const char* what)
{
    std::printf ("%s %s\n", ok ? "PASS" : "FAIL", what);
    failures += ok ? 0 : 1;
}

bool near (float a, float b) { return std::abs (a - b) < 1e-4f; }

void setParam (juce::AudioProcessor& p, const juce::String& id, float realValue)
{
    for (auto* param : p.getParameters())
        if (auto* withId = dynamic_cast<juce::RangedAudioParameter*> (param); withId && withId->getParameterID() == id)
        {
            withId->setValueNotifyingHost (withId->convertTo0to1 (realValue));
            return;
        }
    std::printf ("no parameter %s\n", id.toRawUTF8());
    std::exit (1);
}

float getParam (juce::AudioProcessor& p, const juce::String& id)
{
    for (auto* param : p.getParameters())
        if (auto* withId = dynamic_cast<juce::RangedAudioParameter*> (param); withId && withId->getParameterID() == id)
            return withId->convertFrom0to1 (withId->getValue());
    return NAN;
}

// Plays a note (held for `holdSeconds`) and returns the left channel.
std::vector<float> playNote (juce::AudioProcessor& p, int note, double holdSeconds, double seconds)
{
    const int block = 512;
    juce::AudioBuffer<float> buf (2, block);
    std::vector<float> out;
    const long total = static_cast<long> (seconds * 48000.0), hold = static_cast<long> (holdSeconds * 48000.0);
    for (long pos = 0; pos < total; pos += block)
    {
        juce::MidiBuffer midi;
        if (pos == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, note, 0.8f), 0);
        if (hold >= pos && hold < pos + block)
            midi.addEvent (juce::MidiMessage::noteOff (1, note), static_cast<int> (hold - pos));
        buf.clear();
        p.processBlock (buf, midi);
        for (int i = 0; i < block; ++i)
            out.push_back (buf.getSample (0, i));
        if (pos == 0 && std::abs (buf.getSample (1, 300) - buf.getSample (0, 300)) > 1e-7f)
            std::printf ("channels differ\n");
    }
    return out;
}

double rms (const std::vector<float>& x, double from, double to)
{
    double s = 0.0;
    long n = 0;
    for (auto i = static_cast<size_t> (from * 48000.0); i < std::min (x.size(), static_cast<size_t> (to * 48000.0)); ++i, ++n)
        s += x[i] * x[i];
    return n > 0 ? std::sqrt (s / n) : 0.0;
}

std::unique_ptr<PCASynthProcessor> fresh()
{
    auto p = std::make_unique<PCASynthProcessor>();
    p->setPlayConfigDetails (0, 2, 48000.0, 512);
    p->prepareToPlay (48000.0, 512);
    return p;
}

std::shared_ptr<const pcs::Model> smallModel()
{
    pcs::testgen::Options o;
    o.duration = 1.6;
    o.variations = 2;
    pcs::AnalysisSettings s;
    s.duration = 1.5;
    s.harmonics = 32;
    std::vector<pcs::HarmonicSound> sounds;
    for (const auto& c : pcs::testgen::generateTrainingSet (o))
        sounds.push_back (pcs::analyseHarmonics (c.audio, s, c.name));
    auto m = std::make_shared<pcs::Model> (pcs::trainModel (sounds, s, 20));
    m->title = "Small test space";
    return m;
}

} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;

    // ---- factory model, MIDI -> sound ----
    auto a = fresh();
    check (a->isFactoryModel() && a->getModel() != nullptr && a->getModel()->numSounds() == 60, "factory model is built in (60 sounds)");
    setParam (*a, "release", 0.1f);
    auto out = playNote (*a, 60, 1.0, 1.5);
    check (rms (out, 0.1, 0.9) > 0.01, "a note sounds");
    check (rms (out, 1.3, 1.5) < 1e-4, "and stops after its release");

    // ---- jumping to a training sound ----
    const auto factory = a->getModel();
    const int reed = factory->soundIndex ("reed_1");
    a->jumpToSound (reed);
    const auto z = factory->soundZ (reed);
    check (std::abs (getParam (*a, "pc1") - z[0]) < 1e-3f && std::abs (getParam (*a, "pc2") - z[1]) < 1e-3f,
           "jump sets PC1/PC2 to the sound's coordinates");
    check (std::abs (a->getDetail()[20] - z[20]) < 1e-6f, "and the detail components beyond PC16");

    // ---- a custom model, embedded in the state ----
    a->setModel (smallModel(), false);
    a->jumpToSound (3);
    setParam (*a, "pc2", 1.25f);
    setParam (*a, "play_mode", 2.0f);
    setParam (*a, "brightness", -3.0f);
    juce::MemoryBlock state;
    a->getStateInformation (state);
    check (state.getSize() > 100000, "custom model is embedded in the state");

    auto b = fresh();
    b->setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    check (! b->isFactoryModel() && b->getModel()->title == "Small test space" && b->getModel()->numSounds() == 20,
           "restored the embedded model");
    check (near (getParam (*b, "pc2"), getParam (*a, "pc2")) && near (getParam (*b, "play_mode"), 2.0f)
               && near (getParam (*b, "brightness"), -3.0f),
           "restored the parameters");
    check (b->getDetail() == a->getDetail(), "restored the detail components");

    // Same state -> same sound.
    a->prepareToPlay (48000.0, 512);
    b->prepareToPlay (48000.0, 512);
    const auto outA = playNote (*a, 64, 0.5, 0.8), outB = playNote (*b, 64, 0.5, 0.8);
    double diff = 0.0;
    for (size_t i = 0; i < outA.size(); ++i)
        diff = std::max (diff, static_cast<double> (std::abs (outA[i] - outB[i])));
    check (rms (outA, 0.1, 0.4) > 0.005 && diff < 1e-6, "restored instance renders identically");

    // ---- factory state stays small; a damaged model falls back to factory ----
    auto c = fresh();
    juce::MemoryBlock small;
    c->getStateInformation (small);
    check (small.getSize() < 20000, "factory state does not embed the model");

    auto broken = state;
    static_cast<char*> (broken.getData())[broken.getSize() - 100000] ^= 0x5a;
    const auto xmlLen = *reinterpret_cast<const int32_t*> (static_cast<const char*> (state.getData()) + 4);
    static_cast<char*> (broken.getData())[8 + xmlLen + 8] = 'X'; // model magic
    auto d = fresh();
    d->setModel (smallModel(), false);
    d->setStateInformation (broken.getData(), static_cast<int> (broken.getSize()));
    check (d->isFactoryModel() && near (getParam (*d, "pc2"), 1.25f), "damaged model: parameters load, factory space returns");

    // ---- swapping models while notes play ----
    auto e = fresh();
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
    e->processBlock (buf, midi);
    midi.clear();
    bool finite = true;
    for (int i = 0; i < 40; ++i)
    {
        if (i % 10 == 5)
            e->setModel (i % 20 == 5 ? smallModel() : PCASynthProcessor::factoryModel(), i % 20 == 5 ? false : true);
        buf.clear();
        e->processBlock (buf, midi);
        for (int s = 0; s < 512; ++s)
            finite = finite && std::isfinite (buf.getSample (0, s));
        e->collectGarbage(); // what the timer does on the message thread
    }
    check (finite, "model swaps during playback stay finite");

    std::printf (failures == 0 ? "all plugin checks passed\n" : "%d plugin checks FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
