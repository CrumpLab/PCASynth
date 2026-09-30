// Headless checks of the plugin: MIDI in -> sound out, jumping to training
// sounds, and the state round trip with an embedded model.
#include "../src/PluginEditor.h"
#include "../src/PluginProcessor.h"
#include "../src/Presets.h"
#include "TrainingSet.h"
#include "pcs/Wav.h"

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
    size_t firstDiff = 0;
    while (firstDiff < outA.size() && std::abs (outA[firstDiff] - outB[firstDiff]) < 1e-6f)
        ++firstDiff;
    if (diff >= 1e-6)
        std::printf ("     (max difference %g, first at sample %zu)\n", diff, firstDiff);
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

    // ---- Stage 3: the point, the editor's tools, export ----
    auto f = fresh();
    PCASynthProcessor::Point pt {};
    pt[0] = 1.5f;
    pt[3] = -6.0f;  // clamped to -4 (a host parameter)
    pt[20] = -6.0f; // detail: not clamped
    f->setPoint (pt);
    const auto got = f->getPoint();
    check (near (got[0], 1.5f) && near (got[3], -4.0f) && near (got[20], -6.0f) && near (getParam (*f, "pc1"), 1.5f),
           "setPoint writes PCs (clamped) and detail");

    std::unique_ptr<juce::AudioProcessorEditor> editor (f->createEditor());
    auto* ed = dynamic_cast<PCASynthEditor*> (editor.get());
    check (ed != nullptr, "editor opens");
    if (ed != nullptr)
    {
        const auto fm = f->getModel();
        const auto corner = ed->getMorphPad().blend (0.0f, 0.0f);
        const auto zc = fm->soundZ (0); // top-left defaults to the first sound
        bool same = true;
        for (size_t j = 0; j < zc.size(); ++j)
            same = same && near (corner[j], zc[j]);
        const auto mid = ed->getMorphPad().blend (0.5f, 0.5f);
        check (same && ! near (mid[0], corner[0]), "morph pad: a corner is its sound, the centre a blend");

        f->jumpToSound (fm->soundIndex ("bowed_2"));
        ed->refresh();
        const auto want = fm->decode (fm->soundZ (fm->soundIndex ("bowed_2")));
        const auto& shown = ed->getEnvelopeView().levels();
        bool match = shown.size() == want.db.size();
        for (size_t i = 0; match && i < shown.size(); i += 97)
            match = std::abs (shown[i] - want.db[i]) < 1e-3f;
        check (match, "envelope view shows the decoded point");
    }
    editor.reset();

    f->setUiValue ("mapX", 2);
    juce::MemoryBlock uiState;
    f->getStateInformation (uiState);
    auto g2 = fresh();
    g2->setStateInformation (uiState.getData(), static_cast<int> (uiState.getSize()));
    check (static_cast<int> (g2->getUiValue ("mapX", 0)) == 2, "editor settings are saved with the state");

    const auto wav = PCASynthProcessor::renderNote (f->getModel(), f->currentSynthParams(), 67, 2.0);
    double sumSq = 0.0;
    for (float x : wav.channels[1])
        sumSq += x * x;
    check (wav.numSamples() == 96000 && wav.numChannels() == 2 && sumSq > 1.0, "export renders a stereo note");

    // ---- Stage 4: training inside the plugin ----
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("pcs-train-test");
        dir.deleteRecursively();
        dir.createDirectory();
        const int notes[] = { 48, 55, 60, 67, 72, 52 };
        const char* families[] = { "reed", "bowed", "vowel", "brass", "organ", "flute" };
        for (int i = 0; i < 6; ++i)
        {
            pcs::testgen::Options o;
            o.midiNote = notes[i];
            o.duration = 1.6;
            auto clip = pcs::testgen::generate (families[i], i % 3, o);
            pcs::writeWav (dir.getChildFile (juce::String (families[i]) + ".wav").getFullPathName().toStdString(), clip.audio);
        }
        pcs::AudioBuffer silent;
        silent.sampleRate = 48000.0;
        silent.resize (1, 48000);
        pcs::writeWav (dir.getChildFile ("silent.wav").getFullPathName().toStdString(), silent);
        dir.getChildFile ("notes.txt").replaceWithText ("not audio");

        auto t = fresh();
        auto& trainer = t->getTrainer();
        check (trainer.addFiles ({ dir.getFullPathName() }) == 7, "a folder adds its audio files (not the text file)");
        check (trainer.addFiles ({ dir.getChildFile ("reed.wav").getFullPathName() }) == 0, "adding a file twice is ignored");

        auto settings = trainer.getSettings();
        settings.analysis.autoPitch = true;
        settings.analysis.duration = 1.5;
        settings.analysis.harmonics = 32;
        settings.components = 4;
        settings.title = "Mixed pitches";
        trainer.setSettings (settings);
        t->setPoint ({ 2.0f, 1.0f });

        juce::String error;
        const auto trained = trainer.trainNow (error);
        check (trained != nullptr && trained->numSounds() == 6 && trained->numComponents() == 4, "trains on the sounds that analyse");
        check (! t->isFactoryModel() && t->getModel()->title == "Mixed pitches", "the trained space replaces the model");
        check (near (getParam (*t, "pc1"), 0.0f), "and the point moves to its centre");

        bool pitches = true, silentFailed = false;
        for (const auto& entry : trainer.getEntries())
        {
            if (entry.name == "silent")
                silentFailed = entry.status == Trainer::Entry::Status::Failed;
            else
                for (int i = 0; i < 6; ++i)
                    if (entry.name == families[i])
                        pitches = pitches && std::abs (69.0 + 12.0 * std::log2 (entry.f0 / 440.0) - notes[i]) < 0.3;
        }
        check (pitches, "auto pitch finds each sound's note");
        check (silentFailed, "a silent file is reported as failed");

        const auto start = juce::Time::getMillisecondCounterHiRes();
        trainer.remove ({ 0 }); // bowed
        const auto again = trainer.trainNow (error);
        const auto ms = juce::Time::getMillisecondCounterHiRes() - start;
        check (again != nullptr && again->numSounds() == 5 && again->soundIndex ("bowed") < 0, "removing a sound and retraining");
        std::printf ("     (retrain from cache took %.0f ms)\n", ms);

        juce::MemoryBlock trainedState;
        t->getStateInformation (trainedState);
        auto u = fresh();
        u->setStateInformation (trainedState.getData(), static_cast<int> (trainedState.getSize()));
        const auto restoredEntries = u->getTrainer().getEntries();
        check (restoredEntries.size() == 6 && u->getTrainer().getSettings().analysis.autoPitch
                   && u->getTrainer().getSettings().title == "Mixed pitches" && u->getModel()->numSounds() == 5,
               "training list, settings and the trained model are restored");

        // The background thread.
        check (u->getTrainer().start(), "training starts in the background");
        for (int i = 0; i < 600 && u->getTrainer().isRunning(); ++i)
            juce::Thread::sleep (10);
        check (! u->getTrainer().isRunning() && u->getTrainer().getProgress() >= 1.0, "and finishes");
        dir.deleteRecursively();
    }

    // ---- Stage 5: movement and expression ----
    {
        auto w = fresh();
        setParam (*w, "walk_on", 1.0f);
        setParam (*w, "walk_mode", 2.0f);  // Tour
        setParam (*w, "walk_rate", 2.0f);
        setParam (*w, "walk_seed", 17.0f);
        setParam (*w, "walk_sync", 1.0f);
        setParam (*w, "walk_sync_len", 2.0f); // 1/4
        setParam (*w, "lfo2_on", 1.0f);
        setParam (*w, "lfo2_target", 16.0f);  // Toward Sound
        setParam (*w, "vel_dest", 4.0f);      // PC4 (Off is first)
        const auto sp = w->currentSynthParams();
        check (sp.mod.walk.enabled && sp.mod.walk.mode == pcs::WalkMode::Tour && sp.mod.walk.seed == 17
                   && sp.mod.walk.sync && std::abs (sp.mod.walk.syncBeats - 1.0f) < 1e-6f && sp.mod.lfo[1].enabled
                   && sp.mod.lfo[1].target == pcs::kTowardSound && sp.mod.velocity.destination == 3,
               "movement parameters reach the synth");
        setParam (*w, "lfo2_on", 0.0f);
        setParam (*w, "vel_dest", 0.0f);

        // The walk moves what is heard; the home point stays put.
        const auto walked = playNote (*w, 60, 2.0, 2.0);
        const auto heard = w->getHeardPoint(), home = w->getPoint();
        float moved = 0.0f;
        for (size_t j = 0; j < heard.size(); ++j)
            moved = std::max (moved, std::abs (heard[j] - home[j]));
        check (rms (walked, 0.2, 1.8) > 0.005 && moved > 0.1f && near (home[0], 0.0f), "a tour moves the heard point");

        setParam (*w, "walk_on", 0.0f);
        playNote (*w, 60, 1.0, 1.0);
        const auto back = w->getHeardPoint();
        check (near (back[0], 0.0f) && near (back[5], 0.0f), "and it glides home when the walk stops");

        // Mod wheel towards a sound.
        w->setDirectionSound ("brass_2");
        setParam (*w, "mw_dest", 17.0f); // Toward Sound
        setParam (*w, "mw_amount", 1.0f);
        juce::AudioBuffer<float> wheelBuf (2, 512);
        juce::MidiBuffer wheel;
        wheel.addEvent (juce::MidiMessage::controllerEvent (1, 1, 127), 0);
        w->processBlock (wheelBuf, wheel);
        const auto target = w->getModel()->soundZ (w->getModel()->soundIndex ("brass_2"));
        const auto atWheel = w->getHeardPoint();
        bool reached = true;
        for (size_t j = 0; j < target.size(); ++j)
            reached = reached && std::abs (atWheel[j] - target[j]) < 1e-3f;
        check (reached, "mod wheel up reaches the direction sound");

        juce::MemoryBlock ws;
        w->getStateInformation (ws);
        auto w2 = fresh();
        w2->setStateInformation (ws.getData(), static_cast<int> (ws.getSize()));
        check (w2->getDirectionSound() == "brass_2" && near (getParam (*w2, "walk_seed"), 17.0f), "direction sound and walk settings are saved");
    }

    // ---- Stage 6: MPE ----
    {
        auto m = fresh();
        check (m->supportsMPE(), "declares MPE support to the host");

        // The controller's MPE Configuration Message (RPN 6 on channel 1, 15 members)
        // and per-note bend range (RPN 0 on a member channel: 24 semitones).
        juce::MidiBuffer setup;
        for (const auto& msg : { juce::MidiMessage::controllerEvent (1, 101, 0), juce::MidiMessage::controllerEvent (1, 100, 6),
                                 juce::MidiMessage::controllerEvent (1, 6, 15), juce::MidiMessage::controllerEvent (2, 101, 0),
                                 juce::MidiMessage::controllerEvent (2, 100, 0), juce::MidiMessage::controllerEvent (2, 6, 24) })
            setup.addEvent (msg, 0);
        juce::AudioBuffer<float> mb (2, 512);
        m->processBlock (mb, setup);
        m->applyControllerSetup();
        check (near (getParam (*m, "mpe_on"), 1.0f) && near (getParam (*m, "mpe_zone"), 0.0f) && near (getParam (*m, "mpe_bend_range"), 24.0f),
               "the controller's MPE configuration turns MPE on and sets the bend range");

        // Two notes; bend, press and slide one of them.
        juce::MidiBuffer notes;
        notes.addEvent (juce::MidiMessage::noteOn (2, 60, 0.8f), 0);
        notes.addEvent (juce::MidiMessage::noteOn (3, 67, 0.8f), 0);
        notes.addEvent (juce::MidiMessage::pitchWheel (3, 8192 + 4096), 1);   // +12 st at 24
        notes.addEvent (juce::MidiMessage::channelPressureChange (3, 127), 1);
        notes.addEvent (juce::MidiMessage::controllerEvent (3, 74, 127), 1);
        m->processBlock (mb, notes);
        juce::MidiBuffer none;
        for (int i = 0; i < 20; ++i)
            m->processBlock (mb, none);
        pcs::Synth::VoiceInfo info[4];
        const int n = m->getVoiceInfo (info, 4);
        bool ok = n == 2;
        for (int i = 0; i < n; ++i)
        {
            const bool pressed = info[i].note == 67;
            ok = ok && info[i].channel == (pressed ? 3 : 2)
              && std::abs (info[i].bendSemitones - (pressed ? 12.0f : 0.0f)) < 0.01f
              && std::abs (info[i].pressure - (pressed ? 1.0f : 0.0f)) < 0.01f;
        }
        check (ok, "per-note bend and pressure reach only their own note");

        PCASynthProcessor::Point pts[4];
        const int np = m->getVoicePoints (pts, 4);
        check (np == 2 && std::abs (std::abs (pts[0][0] - pts[1][0]) - 1.5f) < 0.02f && std::abs (std::abs (pts[0][1] - pts[1][1]) - 1.0f) < 0.02f,
               "pressure moves its note 1.5 SD along PC1 and slide 1 SD along PC2 (defaults)");

        // MPE off: the same stream is ordinary MIDI (channel bends are global).
        auto o = fresh();
        o->processBlock (mb, notes);
        const int no = o->getVoiceInfo (info, 4);
        check (no == 2 && std::abs (info[0].bendSemitones) < 1e-6f && std::abs (info[1].bendSemitones) < 1e-6f,
               "with MPE off, channels are ignored as before");
    }

    // ---- Stage 7: richer model ----
    {
        auto r = fresh();
        const auto fm = r->getModel();
        check (fm->numNoiseBands == 16 && fm->hasPartials, "the factory space has noise bands and partial tuning");
        setParam (*r, "noise", -12.0f);
        setParam (*r, "keytrack", 0.5f);
        const auto sp = r->currentSynthParams();
        check (std::abs (sp.noiseDb + 12.0f) < 0.01f && std::abs (sp.keytrack - 0.5f) < 0.01f, "Noise and Keytrack reach the synth");

        // Training options: the model is built as asked, and they are saved.
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("pcs-train-7");
        dir.deleteRecursively();
        dir.createDirectory();
        int i = 0;
        for (const char* family : { "vowel", "reed", "brass" })
            for (int note : { 48, 60, 72 })
            {
                pcs::testgen::Options o;
                o.midiNote = note;
                o.duration = 1.4;
                auto clip = pcs::testgen::generate (family, 0, o);
                pcs::writeWav (dir.getChildFile (juce::String (family) + "_" + juce::String (note) + ".wav").getFullPathName().toStdString(), clip.audio);
                ++i;
            }
        auto& tr = r->getTrainer();
        tr.addFiles ({ dir.getFullPathName() });
        auto ts = tr.getSettings();
        ts.analysis.autoPitch = true;
        ts.analysis.duration = 1.2;
        ts.analysis.harmonics = 24;
        ts.analysis.noiseBands = 8;
        ts.analysis.trackPartials = false;
        ts.analysis.representation = pcs::Representation::ShapeLoudness;
        ts.analysis.pitchTracking = pcs::PitchTracking::On;
        tr.setSettings (ts);
        juce::String err;
        const auto tm = tr.trainNow (err);
        check (tm != nullptr && tm->numNoiseBands == 8 && ! tm->hasPartials && tm->representation == pcs::Representation::ShapeLoudness
                   && tm->pitchTracking,
               "training options shape the model (noise bands, partials, representation, pitch tracking)");
        juce::MemoryBlock st;
        r->getStateInformation (st);
        auto r2 = fresh();
        r2->setStateInformation (st.getData(), static_cast<int> (st.getSize()));
        const auto back = r2->getTrainer().getSettings().analysis;
        check (back.noiseBands == 8 && ! back.trackPartials && back.representation == pcs::Representation::ShapeLoudness
                   && back.pitchTracking == pcs::PitchTracking::On && r2->getModel()->pitchTracking,
               "and they are saved with the state, along with the model");
        const auto tracked = playNote (*r2, 67, 0.8, 1.0);
        check (rms (tracked, 0.1, 0.7) > 0.005, "a pitch-tracked space plays");
        dir.deleteRecursively();
    }

    // ---- Stage 8: presets, Level Lock ----
    {
        auto q = fresh();
        const auto& presets = pcsplugin::factoryPresets();
        check (presets.size() >= 16 && presets[0].name == "Init" && q->getNumPrograms() == 1,
               "factory presets exist (in the editor; the host sees one program)");
        check (std::abs (getParam (*q, "level_lock") - 1.0f) < 1e-6f, "Level Lock is on by default");
        setParam (*q, "level_lock", 0.3f);
        check (std::abs (q->currentSynthParams().levelLock - 0.3f) < 0.01f, "and reaches the synth");

        // Every preset loads its sound and point, and plays.
        bool allPlay = true, allAtSound = true;
        for (int i = 0; i < static_cast<int> (presets.size()); ++i)
        {
            q->loadFactoryPreset (i);
            const auto& pr = presets[static_cast<size_t> (i)];
            if (q->getFactoryPresetIndex() != i || q->getPresetName() != pr.name || q->getProgramName (0) != pr.name)
                allAtSound = false;
            if (pr.sound.isNotEmpty())
            {
                const auto m = q->getModel();
                const int s = m->soundIndex (pr.sound.toStdString());
                float want = s >= 0 ? m->soundZ (s)[0] : 0.0f;
                for (const auto& [component, offset] : pr.offsets)
                    want += component == 0 ? offset : 0.0f;
                if (s < 0 || std::abs (getParam (*q, "pc1") - juce::jlimit (-4.0f, 4.0f, want)) > 1e-3f)
                    allAtSound = false;
            }
            const auto played = playNote (*q, 57, 0.8, 1.2);
            float peak = 0.0f;
            bool allFinite = true;
            for (float x : played)
            {
                peak = std::max (peak, std::abs (x));
                allFinite = allFinite && std::isfinite (x);
            }
            const double level = rms (played, 0.05, 0.8);
            std::printf ("  %-22s rms %.4f peak %.3f\n", pr.name.toRawUTF8(), level, peak);
            if (! allFinite || level < 0.002 || peak > 1.5f)
                allPlay = false;
        }
        check (allAtSound, "each factory preset goes to its sound");
        check (allPlay, "each factory preset plays, at a sane level");

        // A preset starts from the defaults.
        setParam (*q, "gain", 3.0f);
        setParam (*q, "pc5", 2.0f);
        q->loadFactoryPreset (0);
        check (std::abs (getParam (*q, "gain") + 12.0f) < 1e-3f && std::abs (getParam (*q, "pc5")) < 1e-4f && q->getDirectionSound().isEmpty(),
               "loading a preset resets what it does not set");

        // The preset survives a session; host program calls change nothing.
        q->loadFactoryPreset (3);
        setParam (*q, "pc3", 1.1f);
        juce::MemoryBlock st;
        q->getStateInformation (st);
        auto q2 = fresh();
        q2->setStateInformation (st.getData(), static_cast<int> (st.getSize()));
        q2->setCurrentProgram (0);
        check (q2->getFactoryPresetIndex() == 3 && q2->getPresetName() == presets[3].name && std::abs (getParam (*q2, "pc3") - 1.1f) < 1e-3f,
               "the preset name is saved; a host program change keeps your edits");

        // User presets: the whole sound, model included, but not the editor layout.
        auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("pcs-presets/Test Sound.pcspreset");
        file.getParentDirectory().deleteRecursively();
        auto u = fresh();
        u->setModel (smallModel(), false);
        u->jumpToSound (3);
        setParam (*u, "walk_on", 1.0f);
        setParam (*u, "release", 1.25f);
        u->setDirectionSound ("reed_1");
        u->setUiValue ("tab", 2);
        check (u->savePresetFile (file).isEmpty() && file.existsAsFile(), "a user preset saves");
        auto v = fresh();
        v->setUiValue ("tab", 1);
        check (v->loadPresetFile (file).isEmpty(), "and loads");
        check (! v->isFactoryModel() && v->getModel()->title == "Small test space" && std::abs (getParam (*v, "release") - 1.25f) < 1e-3f
                   && std::abs (getParam (*v, "walk_on") - 1.0f) < 1e-6f && v->getDetail() == u->getDetail() && v->getDirectionSound() == "reed_1",
               "with its space, point and settings");
        check (static_cast<int> (v->getUiValue ("tab", 0)) == 1 && v->getPresetName() == "Test Sound" && v->getFactoryPresetIndex() == -1,
               "keeping the editor's layout, and naming the preset");
        const auto a1 = playNote (*u, 60, 0.5, 0.8), a2 = playNote (*v, 60, 0.5, 0.8);
        double gap = 0.0;
        for (size_t k = 0; k < a1.size(); ++k)
            gap = std::max (gap, static_cast<double> (std::abs (a1[k] - a2[k])));
        check (rms (a1, 0.05, 0.4) > 0.002 && gap < 1e-5, "a loaded preset sounds like the saved one");
        // A factory-space preset stays small and brings the factory space back.
        auto w = fresh();
        w->loadFactoryPreset (5);
        auto smallPreset = file.getSiblingFile ("Small.pcspreset");
        w->savePresetFile (smallPreset);
        check (smallPreset.getSize() < 20000 && v->loadPresetFile (smallPreset).isEmpty() && v->isFactoryModel(), "a factory-space preset is small and restores the factory space");
        auto junk = file.getSiblingFile ("junk.pcspreset");
        junk.replaceWithText ("not a preset");
        check (v->loadPresetFile (junk).isNotEmpty() && v->isFactoryModel(), "a broken preset file is refused");
        check (pcsplugin::findUserPresets (file.getParentDirectory()).size() == 3, "user presets are found in their folder");
        file.getParentDirectory().deleteRecursively();
    }

    // ---- Fidelity and inspection ----
    {
        auto q = fresh();
        setParam (*q, "pitch_env", 0.5f);
        check (std::abs (q->currentSynthParams().pitchEnvelope - 0.5f) < 0.01f, "Pitch Envelope reaches the synth");

        // Training options round trip.
        auto& tr = q->getTrainer();
        auto ts = tr.getSettings();
        ts.analysis.frameRate = 200.0;
        ts.analysis.trackPitch = false;
        ts.analysis.sharpAttacks = false;
        tr.setSettings (ts);
        juce::MemoryBlock st;
        q->getStateInformation (st);
        auto q2 = fresh();
        q2->setStateInformation (st.getData(), static_cast<int> (st.getSize()));
        const auto back = q2->getTrainer().getSettings().analysis;
        check (std::abs (back.frameRate - 200.0) < 1e-9 && ! back.trackPitch && ! back.sharpAttacks, "frame rate, pitch curve and sharp attacks are saved");

        // Train on files, then inspect through the training list.
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("pcs-inspect-test");
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
        auto r = fresh();
        auto& trainer = r->getTrainer();
        trainer.addFiles ({ dir.getFullPathName() });
        auto rs = trainer.getSettings();
        rs.analysis.duration = 1.0;
        rs.analysis.harmonics = 32;
        trainer.setSettings (rs);
        juce::String err;
        const auto m = trainer.trainNow (err);
        check (m != nullptr && m->hasPitchCurve && m->fitByComponents.size() == static_cast<size_t> (m->numComponents() + 1),
               "a trained space has a pitch curve and a fit report");
        auto& inspector = r->getInspector();
        auto wait = [&inspector] {
            for (int i = 0; i < 1200 && inspector.isBusy(); ++i)
                juce::Thread::sleep (25);
        };
        inspector.inspect (m->soundIndex ("flute_1"));
        wait();
        const auto result = inspector.getResult();
        check (result != nullptr && result->name == "flute_1" && result->original.numSamples() > 40000 && result->modelError < 6.0,
               "the inspector renders a sound from its file and scores it");
        inspector.evaluateAll();
        wait();
        bool all = true;
        for (const auto& sc : inspector.getScores())
            all = all && sc.done && sc.analysis > 0.0;
        check (all && inspector.getScores().size() == 8, "Evaluate All scores every sound");

        // Audition plays over silence, and stops.
        if (result != nullptr)
        {
            r->audition (result->original);
            juce::AudioBuffer<float> clipOut (2, 512);
            juce::MidiBuffer none;
            double level = 0.0;
            for (int k = 0; k < 20; ++k)
            {
                clipOut.clear();
                r->processBlock (clipOut, none);
                level = std::max (level, static_cast<double> (clipOut.getMagnitude (0, 0, 512)));
            }
            r->stopAudition();
            r->collectGarbage();
            double after = 0.0;
            for (int k = 0; k < 4; ++k)
            {
                clipOut.clear();
                r->processBlock (clipOut, none);
                r->collectGarbage();
                after = std::max (after, static_cast<double> (clipOut.getMagnitude (0, 0, 512)));
            }
            check (level > 0.01 && after < 1e-9 && ! r->isAuditioning(), "audition plays a clip and stops");
        }
        dir.deleteRecursively();

        // Components beyond 32 are part of the point and the state.
        auto big = fresh();
        PCASynthProcessor::Point far {};
        far[0] = 1.0f;
        far[50] = -2.5f;
        big->setPoint (far);
        juce::MemoryBlock bs;
        big->getStateInformation (bs);
        auto big2 = fresh();
        big2->setStateInformation (bs.getData(), static_cast<int> (bs.getSize()));
        check (std::abs (big2->getDetail()[50] + 2.5f) < 1e-6f, "components beyond 32 are saved");
    }

    std::printf (failures == 0 ? "all plugin checks passed\n" : "%d plugin checks FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
