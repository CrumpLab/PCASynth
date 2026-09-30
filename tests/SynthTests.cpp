#include "Render.h"
#include "TestHelpers.h"
#include "TrainingSet.h"

#include "pcs/Synth.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace pcs;
using namespace pcs::tools;
using Catch::Approx;

namespace {
AnalysisSettings settings()
{
    AnalysisSettings s;
    s.duration = 1.5;
    s.harmonics = 32;
    return s;
}

std::shared_ptr<const Model> smallModel()
{
    static std::shared_ptr<const Model> m = [] {
        testgen::Options o;
        o.duration = 1.6;
        o.variations = 2;
        std::vector<HarmonicSound> sounds;
        for (const auto& c : testgen::generateTrainingSet (o))
            sounds.push_back (analyseHarmonics (c.audio, settings(), c.name));
        return std::make_shared<const Model> (trainModel (sounds, settings()));
    }();
    return m;
}

SynthParams at (const std::vector<float>& z)
{
    SynthParams p;
    for (size_t j = 0; j < z.size(); ++j)
        p.z[j] = z[j];
    return p;
}

double rms (const AudioBuffer& a, double from, double to)
{
    const auto b = static_cast<size_t> (from * a.sampleRate), e = std::min (a.channels[0].size(), static_cast<size_t> (to * a.sampleRate));
    double s = 0.0;
    for (size_t i = b; i < e; ++i)
        s += a.channels[0][i] * a.channels[0][i];
    return e > b ? std::sqrt (s / static_cast<double> (e - b)) : 0.0;
}
} // namespace

TEST_CASE ("A rendered training point re-analyses to its own envelope", "[synth]")
{
    const auto m = smallModel();
    for (const auto* name : { "reed_1", "bowed_2", "vowel_1", "mallet_2" })
    {
        INFO (name);
        const int i = m->soundIndex (name);
        REQUIRE (i >= 0);
        SynthParams p = at (m->soundZ (i));
        p.attack = 0.0005f;
        const auto audio = renderNotes (m, p, { { 0.0, 2.0, 60, 1.0f } }, 2.0);
        auto s = settings();
        s.tuneSearchCents = 5.0;
        const auto again = analyseHarmonics (audio, s, name);
        CHECK (again.f0 == Approx (midiToHz (60)).epsilon (0.001));

        // Compare levels of harmonics that matter (within 40 dB of the frame's loudest).
        int compared = 0;
        for (int t = 10; t < 130; t += 10)
        {
            float loudest = -200.0f;
            for (int h = 0; h < s.harmonics; ++h)
                loudest = std::max (loudest, m->decode (m->soundZ (i)).at (t, h));
            for (int h = 0; h < s.harmonics; ++h)
            {
                const float want = m->decode (m->soundZ (i)).at (t, h);
                if (want > loudest - 40.0f && want > -50.0f)
                {
                    CHECK (again.at (t, h) == Approx (want).margin (2.0));
                    ++compared;
                }
            }
        }
        CHECK (compared > 20);
    }
}

TEST_CASE ("Notes play at their own pitch", "[synth]")
{
    const auto m = smallModel();
    for (int note : { 48, 67, 72 })
    {
        const auto audio = renderNotes (m, at (m->soundZ (m->soundIndex ("organ_1"))), { { 0.0, 1.0, note, 1.0f } }, 1.0);
        const auto mono = monoMix (audio);
        CHECK (estimateF0 (mono, 48000.0, 4800, midiToHz (note), 50.0, 8) == Approx (midiToHz (note)).epsilon (0.001));
    }
}

TEST_CASE ("Voices end, sustain in loop modes, and respect polyphony", "[synth]")
{
    const auto m = smallModel();
    SynthParams p = at (m->soundZ (m->soundIndex ("organ_2")));

    SECTION ("silent without notes")
    {
        CHECK (rms (renderNotes (m, p, {}, 0.5), 0.0, 0.5) == 0.0);
    }
    SECTION ("one-shot stops at the end of the envelope, even when held")
    {
        const auto a = renderNotes (m, p, { { 0.0, 4.0, 60, 1.0f } }, 4.0);
        CHECK (rms (a, 0.3, 0.6) > 0.01);
        CHECK (rms (a, 2.0, 4.0) == 0.0);
    }
    SECTION ("release ends a note")
    {
        p.release = 0.1f;
        const auto a = renderNotes (m, p, { { 0.0, 0.5, 60, 1.0f } }, 1.0);
        CHECK (rms (a, 0.3, 0.5) > 0.01);
        CHECK (rms (a, 0.75, 1.0) < 1e-4);
    }
    for (auto mode : { PlayMode::Loop, PlayMode::PingPong, PlayMode::Scan })
    {
        DYNAMIC_SECTION ("mode " << static_cast<int> (mode) << " sustains while held")
        {
            p.mode = mode;
            p.loopStart = 0.2f;
            p.loopEnd = 0.5f;
            p.scanPosition = 0.3f;
            const auto a = renderNotes (m, p, { { 0.0, 5.0, 60, 1.0f } }, 5.5);
            const double early = rms (a, 0.5, 1.0), late = rms (a, 4.0, 4.9);
            CHECK (late > 0.25 * early);
            CHECK (rms (a, 5.4, 5.5) < 0.05 * early);
        }
    }
    SECTION ("polyphony limit")
    {
        p.polyphony = 3;
        Synth s;
        s.prepare (48000.0);
        s.setParams (p);
        s.setModel (m);
        std::vector<MidiEvent> ev;
        for (int n = 0; n < 6; ++n)
            ev.push_back ({ n, MidiEvent::Type::NoteOn, 60 + n, 1.0f });
        std::vector<float> buf (256);
        float* ch[] = { buf.data() };
        s.process (ch, 1, 256, ev.data(), static_cast<int> (ev.size()));
        CHECK (s.activeVoiceCount() == 3);
    }
}

TEST_CASE ("Moves in the space glide, and far-out points stay finite", "[synth]")
{
    const auto m = smallModel();
    SynthParams p;
    p.mode = PlayMode::Loop;
    for (auto& z : p.z)
        z = 12.0f;
    p.exaggerate = 2.0f;
    const auto a = renderNotes (m, p, { { 0.0, 1.0, 60, 1.0f }, { 0.0, 1.0, 64, 1.0f }, { 0.0, 1.0, 67, 1.0f } }, 1.5);
    for (float x : a.channels[0])
        REQUIRE (std::isfinite (x));

    // Zero-length morph jumps straight to the target; the default glides.
    Synth s;
    s.prepare (48000.0);
    SynthParams q;
    s.setParams (q);
    s.setModel (m);
    q.z[0] = 2.0f;
    s.setParams (q);
    std::vector<float> buf (480);
    float* ch[] = { buf.data() };
    s.process (ch, 1, 480, nullptr, 0); // 10 ms
    CHECK (s.currentZ()[0] > 0.2f);
    CHECK (s.currentZ()[0] < 0.5f);
    q.morphTime = 0.0f;
    s.setParams (q);
    s.process (ch, 1, 32, nullptr, 0);
    CHECK (s.currentZ()[0] == 2.0f);
}

TEST_CASE ("Sustain pedal holds released notes until it lifts", "[synth]")
{
    const auto m = smallModel();
    SynthParams p = at (m->soundZ (m->soundIndex ("organ_1")));
    p.mode = PlayMode::Loop;
    p.release = 0.05f;
    Synth s;
    s.prepare (48000.0);
    s.setParams (p);
    s.setModel (m);
    std::vector<float> buf (4800);
    float* ch[] = { buf.data() };
    auto block = [&] (std::vector<MidiEvent> ev) {
        s.process (ch, 1, 4800, ev.data(), static_cast<int> (ev.size()));
    };
    block ({ { 0, MidiEvent::Type::Sustain, 0, 1.0f }, { 0, MidiEvent::Type::NoteOn, 60, 1.0f } });
    block ({ { 0, MidiEvent::Type::NoteOff, 60, 0.0f } });
    for (int i = 0; i < 5; ++i)
        block ({});
    CHECK (s.activeVoiceCount() == 1);
    block ({ { 0, MidiEvent::Type::Sustain, 0, 0.0f } });
    block ({});
    CHECK (s.activeVoiceCount() == 0);
}

TEST_CASE ("Models swap in without the audio thread freeing them", "[synth]")
{
    const auto m = smallModel();
    Synth s;
    s.prepare (48000.0);
    auto slot = Synth::makeSlot (m);
    s.swapModel (slot);
    CHECK (s.model() == m.get());
    CHECK (slot == nullptr); // no previous slot

    auto empty = Synth::makeSlot (nullptr);
    s.swapModel (empty);
    CHECK (s.model() == nullptr);
    REQUIRE (empty != nullptr);
    CHECK (empty->model == m); // handed back, still alive
    std::vector<float> buf (256, 1.0f);
    float* ch[] = { buf.data() };
    MidiEvent on { 0, MidiEvent::Type::NoteOn, 60, 1.0f };
    s.process (ch, 1, 256, &on, 1);
    CHECK (buf[100] == 0.0f);
}

TEST_CASE ("Residual noise renders at the level the model holds", "[synth][stage7]")
{
    // A sound whose harmonics are silent and whose noise is -40 dB RMS in every band.
    AnalysisSettings s;
    s.duration = 1.0;
    s.harmonics = 16;
    s.noiseBands = 16;
    s.normalizeLoudness = false;
    s.trimOnset = false;
    HarmonicSound noise;
    noise.name = "noise";
    noise.f0 = midiToHz (57);
    noise.numFrames = 100;
    noise.numHarmonics = 16;
    noise.numNoiseBands = 16;
    noise.db.assign (100 * 16, -120.0f);
    noise.noiseDb.assign (100 * 16, -40.0f);
    noise.partialCents.assign (16, 0.0f);
    s.floorDb = -120.0;
    auto m = std::make_shared<const Model> (pcs::singleSoundModel (noise, s));

    SynthParams p;
    p.mode = PlayMode::Loop;
    p.gainDb = 0.0f;
    p.attack = 0.001f;
    const auto audio = renderNotes (m, p, { { 0.0, 1.5, 57, 1.0f } }, 1.5);
    // Its own analysis at the same note: every band within 2.5 dB (tone-free, so
    // the analysis needs a nominal pitch).
    auto a = s;
    a.midiNote = 57;
    a.tuneSearchCents = 1.0;
    a.trackPartials = false;
    a.harmonics = 16;
    const auto again = analyseHarmonics (audio, a, "again");
    for (int b = 0; b < 16; ++b)
    {
        if (noiseBandEdge (b + 1, 16) * midiToHz (57) > 0.4 * 48000.0)
            break;
        double mean = 0.0;
        for (int t = 20; t < 80; ++t)
            mean += again.noiseAt (t, b) / 60.0;
        INFO ("band " << b);
        CHECK (mean == Approx (-40.0).margin (2.5));
    }
}

TEST_CASE ("Stretched partials render at their frequencies; notes transpose them", "[synth][stage7]")
{
    // Analyse a piano-like stretched tone, play it back, track its partials again.
    AnalysisSettings s;
    s.duration = 1.2;
    s.harmonics = 32;
    s.midiNote = 48;
    const double f0 = midiToHz (48), B = 0.0004;
    AudioBuffer tone;
    tone.sampleRate = 48000.0;
    tone.resize (1, static_cast<int> (1.6 * 48000.0));
    for (int h = 1; h <= 32; ++h)
        for (size_t i = 0; i < tone.channels[0].size(); ++i)
            tone.channels[0][i] += static_cast<float> (0.3 / h * std::sin (6.28318530717958647692 * h * f0 * std::sqrt (1.0 + B * h * h) * static_cast<double> (i) / 48000.0));
    const auto sound = analyseHarmonics (tone, s, "stretched");
    auto m = std::make_shared<const Model> (pcs::singleSoundModel (sound, s));

    SynthParams p;
    p.mode = PlayMode::Loop;
    p.noiseDb = -60.0f;
    for (int note : { 48, 55 })
    {
        const auto out = renderNotes (m, p, { { 0.0, 1.6, note, 1.0f } }, 1.6);
        const auto mono = monoMix (out);
        const auto f = trackPartials (mono, 48000.0, 9600, midiToHz (note), 24);
        for (int h = 2; h <= 24; h += 2)
        {
            INFO ("note " << note << " partial " << h);
            const double cents = 1200.0 * std::log2 (f[static_cast<size_t> (h - 1)] / (h * f[0]));
            CHECK (cents == Approx (sound.partialCents[static_cast<size_t> (h - 1)] - sound.partialCents[0]).margin (3.0));
        }
    }
}

TEST_CASE ("Keytrack: notes take the timbre the model predicts at their pitch", "[synth][stage7]")
{
    AnalysisSettings s;
    s.duration = 1.0;
    s.harmonics = 24;
    s.autoPitch = true;
    std::vector<HarmonicSound> sounds;
    for (const char* family : { "vowel", "reed", "brass" })
        for (int note : { 48, 60, 72 })
        {
            testgen::Options o;
            o.midiNote = note;
            o.duration = 1.2;
            o.detuneCents = 0.0;
            const auto c = testgen::generate (family, 0, o);
            sounds.push_back (analyseHarmonics (c.audio, s, c.name + std::to_string (note)));
        }
    auto m = std::make_shared<const Model> (trainModel (sounds, s));
    REQUIRE (m->pitchTracking);

    SynthParams p = at (m->soundZ (0)); // vowel at 48, in residual coordinates
    p.mode = PlayMode::OneShot;          // rendered frame t = envelope frame t
    p.noiseDb = -60.0f;
    p.attack = 0.0005f;
    const auto audio = renderNotes (m, p, { { 0.0, 1.2, 72, 1.0f } }, 1.2);
    auto a = s;
    a.autoPitch = false;
    a.midiNote = 72;
    a.tuneSearchCents = 5.0;
    const auto got = analyseHarmonics (audio, a, "k");

    // Compare spectral shapes (the synth's gain is a constant dB offset).
    auto error = [&] (const HarmonicSound& want) {
        std::vector<double> d;
        for (int t = 20; t < 80; t += 10)
            for (int h = 0; h < 12; ++h)
                if (want.at (t, h) > -50.0f)
                    d.push_back (got.at (t, h) - want.at (t, h));
        double mean = 0.0, err = 0.0;
        for (double x : d)
            mean += x / static_cast<double> (d.size());
        for (double x : d)
            err += std::abs (x - mean) / static_cast<double> (d.size());
        return err;
    };
    const double tracked = error (m->decode (m->soundZ (0), m->pitchDelta (72.0)));
    const double flat = error (m->decode (m->soundZ (0), 0.0f));
    INFO ("mean |dB| from the keytracked prediction " << tracked << ", from the un-tracked timbre " << flat);
    CHECK (tracked < 1.0);
    CHECK (tracked < 0.5 * flat);
}

TEST_CASE ("A model swapped in plays exactly as in a fresh synth (no stale caches)", "[synth][stage7]")
{
    // Two different spaces, each with partial tuning and noise.
    AnalysisSettings s;
    s.duration = 1.0;
    s.harmonics = 24;
    auto train = [&] (std::initializer_list<const char*> fams) {
        std::vector<HarmonicSound> sounds;
        testgen::Options o;
        o.duration = 1.2;
        for (const char* f : fams)
            for (int v = 0; v < 2; ++v)
            {
                const auto c = testgen::generate (f, v, o);
                sounds.push_back (analyseHarmonics (c.audio, s, c.name));
            }
        return std::make_shared<const Model> (trainModel (sounds, s));
    };
    const auto first = train ({ "piano", "mallet" }), second = train ({ "reed", "bowed", "vowel" });

    auto play = [] (Synth& synth, int blocks) {
        std::vector<float> out, buf (480);
        float* ch[] = { buf.data() };
        MidiEvent on { 0, MidiEvent::Type::NoteOn, 64, 0.8f };
        for (int b = 0; b < blocks; ++b)
        {
            synth.process (ch, 1, 480, &on, b == 0 ? 1 : 0);
            out.insert (out.end(), buf.begin(), buf.end());
        }
        return out;
    };
    SynthParams p;
    p.mode = PlayMode::Loop;
    Synth used;
    used.prepare (48000.0);
    used.setParams (p);
    used.setModel (first);
    play (used, 50);
    used.setModel (second);
    const auto a = play (used, 30);

    Synth fresh;
    fresh.prepare (48000.0);
    fresh.setParams (p);
    fresh.setModel (second);
    const auto b = play (fresh, 30);
    double diff = 0.0;
    for (size_t i = 0; i < a.size(); ++i)
        diff = std::max (diff, static_cast<double> (std::abs (a[i] - b[i])));
    CHECK (diff < 1e-6);
}

TEST_CASE ("A voice's own point, when it has not moved, sounds like the shared point", "[synth][stage8]")
{
    // The per-voice path decodes into a cache refreshed every few sub-blocks and
    // shifted frame by frame; it must land on the same amplitudes as the shared
    // path, loop crossfades included.
    const auto m = smallModel();
    auto p = at ({ 1.0f, -0.5f, 0.3f });
    p.mode = PlayMode::Loop;
    p.loopStart = 0.2f;
    p.loopEnd = 0.5f;
    const std::vector<Note> notes { { 0.0, 1.2, 48, 0.8f }, { 0.1, 1.0, 55, 0.6f } };
    const auto shared = renderNotes (m, p, notes, 1.6);
    p.mod.velocity = { 0, 0.0f }; // velocity moves each voice along PC1, by nothing: every voice has its own point
    const auto own = renderNotes (m, p, notes, 1.6);
    double diff = 0.0;
    for (size_t i = 0; i < shared.channels[0].size(); ++i)
        diff = std::max (diff, static_cast<double> (std::abs (shared.channels[0][i] - own.channels[0][i])));
    CHECK (rms (shared, 0.2, 1.0) > 0.01);
    CHECK (diff < 1e-5);
}

TEST_CASE ("Level Lock holds far-out points near the training sounds' loudness", "[synth][stage8]")
{
    const auto m = smallModel();
    auto level = [&] (const std::vector<float>& z, float lock) {
        auto p = at (z);
        p.mode = PlayMode::Loop;
        p.levelLock = lock;
        return 20.0 * std::log10 (rms (renderNotes (m, p, { { 0.0, 1.0, 48, 0.8f } }, 1.1), 0.1, 1.0));
    };
    // Training sounds keep (nearly) their level ...
    for (int i = 0; i < m->numSounds(); i += 3)
        CHECK (std::abs (level (m->soundZ (i), 1.0f) - level (m->soundZ (i), 0.0f)) < 3.0);
    // ... while points off the training set, which can be tens of dB out, come back.
    std::vector<double> free, locked;
    for (const auto& z : { std::vector<float> { 3, 3, 3, 3 }, { 4, -4, 4, -4 }, { -3, 3, -3, 3 }, { 0, 0, 0, 0, 3, 3, 3, 3 } })
    {
        free.push_back (level (z, 0.0f));
        locked.push_back (level (z, 1.0f));
    }
    std::vector<double> ref;
    for (int i = 0; i < m->numSounds(); ++i)
        ref.push_back (level (m->soundZ (i), 0.0f));
    const double lo = *std::min_element (ref.begin(), ref.end()) - 6.0, hi = *std::max_element (ref.begin(), ref.end()) + 6.0;
    for (double l : locked)
    {
        CHECK (l > lo);
        CHECK (l < hi);
    }
    const auto spread = [] (const std::vector<double>& v) { return *std::max_element (v.begin(), v.end()) - *std::min_element (v.begin(), v.end()); };
    CHECK (spread (locked) < spread (free));
}
