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
                    CHECK (again.at (t, h) == Approx (want).margin (1.5));
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
