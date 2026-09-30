// Headless checks of PCAWave: the factory space plays, presets, the state
// round trip with a trained waveform space, training and inspecting in the
// plugin, the Mix weights, and that it keeps to its own kind of space.
#include "../src/WaveEditor.h"
#include "../src/WaveProcessor.h"
#include "TrainingSet.h"
#include "pcs/Model.h"
#include "pcs/Wav.h"

#include <cmath>
#include <cstdio>

namespace {
int failures = 0;
void check (bool ok, const char* what)
{
    std::printf ("%s %s\n", ok ? "PASS" : "FAIL", what);
    failures += ok ? 0 : 1;
}

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

bool hasParam (juce::AudioProcessor& p, const juce::String& id)
{
    for (auto* param : p.getParameters())
        if (auto* withId = dynamic_cast<juce::RangedAudioParameter*> (param); withId && withId->getParameterID() == id)
            return true;
    return false;
}

std::vector<float> playNote (juce::AudioProcessor& p, int note, double hold, double seconds)
{
    juce::AudioBuffer<float> buf (2, 512);
    std::vector<float> out;
    const long total = static_cast<long> (seconds * 48000.0), off = static_cast<long> (hold * 48000.0);
    for (long pos = 0; pos < total; pos += 512)
    {
        juce::MidiBuffer midi;
        if (pos == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, note, 0.8f), 0);
        if (off >= pos && off < pos + 512)
            midi.addEvent (juce::MidiMessage::noteOff (1, note), static_cast<int> (off - pos));
        buf.clear();
        p.processBlock (buf, midi);
        for (int i = 0; i < 512; ++i)
            out.push_back (buf.getSample (0, i));
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

std::unique_ptr<PCAWaveProcessor> fresh()
{
    auto p = std::make_unique<PCAWaveProcessor>();
    p->setPlayConfigDetails (0, 2, 48000.0, 512);
    p->prepareToPlay (48000.0, 512);
    return p;
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;

    auto a = fresh();
    const auto fm = a->getWaveModel();
    check (fm != nullptr && fm->numSounds() == 60 && fm->numComponents() == 24 && a->isFactoryModel(),
           "the factory waveform space is built in (60 sounds, 24 components)");
    check (! hasParam (*a, "harmonics") && ! hasParam (*a, "noise") && ! hasParam (*a, "speed") && hasParam (*a, "level_lock"),
           "harmonic-only parameters are left out");
    a->jumpToSound (fm->soundIndex ("reed_1"));
    const auto out = playNote (*a, 60, 1.0, 1.5);
    check (rms (out, 0.1, 0.9) > 0.01 && rms (out, 1.35, 1.5) < 1e-4, "a note sounds, and stops after its release");

    // Every factory preset plays.
    bool allPlay = true;
    const auto& presets = a->getFactoryPresets();
    for (int i = 0; i < static_cast<int> (presets.size()); ++i)
    {
        a->loadFactoryPreset (i);
        const auto played = playNote (*a, 57, 0.8, 1.2);
        float peak = 0.0f;
        for (float x : played)
            peak = std::max (peak, std::isfinite (x) ? std::abs (x) : 1e9f);
        const double level = rms (played, 0.05, 0.8);
        std::printf ("  %-20s rms %.4f peak %.3f\n", presets[static_cast<size_t> (i)].name.toRawUTF8(), level, peak);
        allPlay = allPlay && level > 0.002 && peak < 1.5f;
    }
    check (presets.size() >= 10 && allPlay, "each factory preset plays, at a sane level");

    // Training in the plugin, then the state round trip with the trained space.
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("pcs-wave-test");
    dir.deleteRecursively();
    dir.createDirectory();
    pcs::testgen::Options o;
    o.duration = 1.2;
    for (const char* family : { "flute", "reed", "pluck", "vowel" })
        for (int v = 0; v < 2; ++v)
        {
            const auto clip = pcs::testgen::generate (family, v, o);
            pcs::writeWav (dir.getChildFile (clip.name + ".wav").getFullPathName().toStdString(), clip.audio);
        }
    auto t = fresh();
    auto& trainer = t->getTrainer();
    trainer.addFiles ({ dir.getFullPathName() });
    auto ts = trainer.getSettings();
    ts.wave.duration = 1.0;
    ts.wave.sampleRate = 32000.0;
    ts.title = "Eight waves";
    trainer.setSettings (ts);
    juce::String err;
    const auto trained = std::dynamic_pointer_cast<const pcs::WaveModel> (trainer.trainNow (err));
    check (trained != nullptr && trained->numSounds() == 8 && trained->numComponents() == 7 && std::abs (trained->sampleRate - 32000.0) < 1e-9
               && ! t->isFactoryModel() && t->getWaveModel()->title == "Eight waves",
           "trains a waveform space from files");
    t->jumpToSound (3);
    setParam (*t, "play_mode", 2.0f);
    juce::MemoryBlock state;
    t->getStateInformation (state);
    auto u = fresh();
    u->setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    check (! u->isFactoryModel() && u->getWaveModel() != nullptr && u->getWaveModel()->title == "Eight waves"
               && std::abs (u->getTrainer().getSettings().wave.sampleRate - 32000.0) < 1e-9,
           "the trained space and training settings are saved with the state");
    const auto x1 = playNote (*t, 60, 0.5, 0.8), x2 = playNote (*u, 60, 0.5, 0.8);
    double gap = 0.0;
    for (size_t k = 0; k < x1.size(); ++k)
        gap = std::max (gap, static_cast<double> (std::abs (x1[k] - x2[k])));
    check (rms (x1, 0.05, 0.4) > 0.005 && gap < 1e-5, "and sounds the same after a restore");

    // Inspecting through the training list.
    auto& inspector = t->getInspector();
    inspector.inspect (1);
    for (int i = 0; i < 1200 && inspector.isBusy(); ++i)
        juce::Thread::sleep (25);
    const auto r = inspector.getResult();
    check (r != nullptr && r->waveform && r->analysisError < 3.0 && r->envelopeError < -60.0,
           "the inspector compares a sound with its aligned waveform and its point");

    // The Mix view: at a training sound, that sound alone.
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (t->createEditor());
        auto* ed = dynamic_cast<PCAWaveEditor*> (editor.get());
        check (ed != nullptr, "editor opens");
        if (ed != nullptr)
        {
            ed->refreshModel();
            ed->refresh();
            ed->selectTab (1); // Mix
            MixPanel mix (*t);
            mix.setSize (1160, 160);
            mix.setModel (t->getSpace());
            mix.refresh();
            const auto& w = mix.getWeights();
            bool alone = w.size() == 8;
            for (size_t k = 0; k < w.size() && alone; ++k)
                alone = std::abs (w[k] - (k == 3 ? 1.0 : 0.0)) < 1e-3;
            check (alone, "the Mix view shows a training sound as itself alone");
            check (! ed->getWaveView().waveform().empty(), "the waveform view shows the point");

            // The mixer: amounts dialled in from the centre make the point.
            mix.clearMix();
            bool centred = true;
            for (float v : t->getPoint())
                centred = centred && std::abs (v) < 1e-5f;
            mix.setAmount (2, 0.5);
            mix.setAmount (5, 0.5);
            const auto wm = t->getWaveModel();
            const auto z2 = wm->soundZ (2), z5 = wm->soundZ (5);
            const auto p = t->getPoint();
            double gapMid = 0.0;
            for (size_t j = 0; j < z2.size(); ++j)
                gapMid = std::max (gapMid, static_cast<double> (std::abs (p[j] - 0.5f * (z2[j] + z5[j]))));
            {
                // While audio runs, the heard point comes from the audio thread,
                // gliding to the new point over Morph Time.
                juce::AudioBuffer<float> block (2, 512);
                juce::MidiBuffer none;
                for (int b = 0; b < 60; ++b)
                    t->processBlock (block, none);
            }
            mix.refresh();
            const auto& mw = mix.getWeights();
            check (centred && gapMid < 1e-4 && std::abs (mw[2] - 0.5) < 1e-3 && std::abs (mw[5] - 0.5) < 1e-3 && std::abs (mw[0]) < 1e-3,
                   "the Mix view is a mixer: 50 % of two sounds from the centre is their midpoint");

            // The amounts are kept with the project, and read back when the point moves elsewhere.
            juce::MemoryBlock mixState;
            t->getStateInformation (mixState);
            auto rp = fresh();
            rp->setStateInformation (mixState.getData(), static_cast<int> (mixState.getSize()));
            MixPanel restored (*rp);
            restored.setSize (1160, 160);
            restored.setModel (rp->getSpace());
            const auto& ra = restored.getAmounts();
            bool kept = ra.size() == 8;
            for (size_t k = 0; k < ra.size() && kept; ++k)
                kept = std::abs (ra[k] - (k == 2 || k == 5 ? 0.5 : 0.0)) < 1e-4;
            check (kept, "the mix is saved with the state");
            t->jumpToSound (6);
            mix.refresh();
            const auto& ja = mix.getAmounts();
            bool followed = ja.size() == 8;
            for (size_t k = 0; k < ja.size() && followed; ++k)
                followed = std::abs (ja[k] - (k == 6 ? 1.0 : 0.0)) < 1e-6;
            check (followed, "moving the point another way updates the mix");
        }
    }

    // The factory space keeps fewer components than sounds: jumping still reads as that sound alone.
    {
        MixPanel factoryMix (*a);
        factoryMix.setSize (1160, 160);
        a->jumpToSound (fm->soundIndex ("reed_1"));
        factoryMix.setModel (a->getSpace());
        const auto& fa = factoryMix.getAmounts();
        const auto reed = static_cast<size_t> (fm->soundIndex ("reed_1"));
        bool alone = fa.size() == 60;
        for (size_t k = 0; k < fa.size() && alone; ++k)
            alone = std::abs (fa[k] - (k == reed ? 1.0 : 0.0)) < 1e-6;
        factoryMix.setAmount (fm->soundIndex ("vowel_1"), 0.3);
        factoryMix.refresh();
        const auto& fw = factoryMix.getWeights();
        check (alone && std::abs (fw[reed] - 1.0) < 0.01 && std::abs (fw[static_cast<size_t> (fm->soundIndex ("vowel_1"))] - 0.3) < 0.01,
               "in the factory space too, amounts read as set");
    }

    // Each plugin keeps to its own kind of space.
    auto harmonicFile = dir.getChildFile ("harmonic.pcsm");
    {
        std::vector<pcs::HarmonicSound> sounds;
        pcs::AnalysisSettings as;
        as.duration = 0.5;
        as.harmonics = 16;
        for (const char* family : { "flute", "reed" })
            sounds.push_back (pcs::analyseHarmonics (pcs::testgen::generate (family, 0, o).audio, as, family));
        pcs::saveModel (pcs::trainModel (sounds, as), harmonicFile.getFullPathName().toStdString());
    }
    check (u->loadModelFile (harmonicFile).isNotEmpty() && u->getWaveModel() != nullptr, "a harmonic space (.pcsm) is refused");
    auto waveFile = dir.getChildFile ("wave.pcsw");
    check (t->saveModelFile (waveFile).isEmpty() && fresh()->loadModelFile (waveFile).isEmpty(), "a .pcsw space saves and loads");
    dir.deleteRecursively();

    std::printf (failures == 0 ? "all PCAWave checks passed\n" : "%d PCAWave checks FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
