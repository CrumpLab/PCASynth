#include "TrainingSet.h"

#include "pcs/Synth.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace pcs;
using Catch::Approx;
using Type = MidiEvent::Type;

namespace {
std::shared_ptr<const Model> model()
{
    static std::shared_ptr<const Model> m = [] {
        testgen::Options o;
        o.duration = 1.2;
        o.variations = 2;
        AnalysisSettings s;
        s.duration = 1.0;
        s.harmonics = 24;
        std::vector<HarmonicSound> sounds;
        for (const auto& c : testgen::generateTrainingSet (o))
            sounds.push_back (analyseHarmonics (c.audio, s, c.name));
        return std::make_shared<const Model> (trainModel (sounds, s));
    }();
    return m;
}

MidiEvent ev (Type t, int channel, int note, float value) { return { 0, t, note, value, channel }; }

struct Rig
{
    Synth s;
    std::vector<float> buf = std::vector<float> (480);
    explicit Rig (SynthParams p)
    {
        s.prepare (48000.0);
        p.mode = PlayMode::Loop;
        s.setParams (p);
        s.setModel (model());
    }
    void run (std::vector<MidiEvent> events = {}, int blocks = 1)
    {
        float* ch[] = { buf.data() };
        for (int b = 0; b < blocks; ++b)
        {
            s.process (ch, 1, 480, events.data(), static_cast<int> (events.size()));
            events.clear();
        }
    }
    Synth::VoiceInfo voice (int note)
    {
        Synth::VoiceInfo info[8];
        const int n = s.voiceInfo (info, 8);
        for (int i = 0; i < n; ++i)
            if (info[i].note == note && ! info[i].releasing)
                return info[i];
        FAIL ("no voice for note " << note);
        return {};
    }
    Point point (int note)
    {
        Point pts[8];
        Synth::VoiceInfo info[8];
        const int n = s.voicePoints (pts, 8);
        s.voiceInfo (info, 8);
        for (int i = 0; i < n; ++i)
            if (info[i].note == note && ! info[i].releasing)
                return pts[i];
        FAIL ("no voice for note " << note);
        return {};
    }
};

SynthParams mpeParams()
{
    SynthParams p;
    p.mpe.enabled = true;
    p.mpe.pressure = { 0, 2.0f }; // PC1, +2 SD at full pressure
    p.mpe.slide = { 1, 1.0f };    // PC2
    p.mpe.smoothing = 0.001f;     // effectively none, for exact checks
    return p;
}
} // namespace

TEST_CASE ("MPE: notes on member channels bend on their own; the master bends all", "[mpe]")
{
    Rig r (mpeParams());
    r.run ({ ev (Type::NoteOn, 2, 60, 0.8f), ev (Type::NoteOn, 3, 64, 0.8f) });
    r.run ({ ev (Type::PitchBend, 2, 0, 0.5f) }); // +24 semitones on channel 2 only
    CHECK (r.voice (60).bendSemitones == Approx (24.0f));
    CHECK (r.voice (64).bendSemitones == Approx (0.0f));

    // Pitch actually changes: render one note alone and measure it.
    Rig solo (mpeParams());
    solo.run ({ ev (Type::PitchBend, 2, 0, 1.0f / 48.0f) }); // +1 semitone, sent before the note
    std::vector<float> out;
    float* ch[] = { solo.buf.data() };
    MidiEvent on = ev (Type::NoteOn, 2, 60, 0.8f);
    for (int b = 0; b < 60; ++b)
    {
        solo.s.process (ch, 1, 480, &on, b == 0 ? 1 : 0);
        out.insert (out.end(), solo.buf.begin(), solo.buf.end());
    }
    std::vector<double> mono (out.begin(), out.end());
    CHECK (estimateF0 (mono, 48000.0, 4800, midiToHz (61), 30.0, 8) == Approx (midiToHz (61)).epsilon (0.001));

    // Master channel (1, lower zone): bends every note by the ordinary range
    // (2 semitones); the per-note part is unchanged.
    r.run ({ ev (Type::PitchBend, 1, 0, 1.0f) });
    CHECK (r.voice (64).bendSemitones == Approx (0.0f));
    Rig master (mpeParams());
    master.run ({ ev (Type::PitchBend, 1, 0, 1.0f) });
    std::vector<float> out2;
    float* ch2[] = { master.buf.data() };
    MidiEvent on2 = ev (Type::NoteOn, 3, 60, 0.8f);
    for (int b = 0; b < 60; ++b)
    {
        master.s.process (ch2, 1, 480, &on2, b == 0 ? 1 : 0);
        out2.insert (out2.end(), master.buf.begin(), master.buf.end());
    }
    std::vector<double> mono2 (out2.begin(), out2.end());
    CHECK (estimateF0 (mono2, 48000.0, 4800, midiToHz (62), 30.0, 8) == Approx (midiToHz (62)).epsilon (0.001));
}

TEST_CASE ("MPE: pressure and slide move only their own note's point", "[mpe]")
{
    Rig r (mpeParams());
    r.run ({ ev (Type::NoteOn, 2, 60, 0.8f), ev (Type::NoteOn, 3, 67, 0.8f) });
    r.run ({ ev (Type::Pressure, 3, 0, 1.0f) }, 3);
    CHECK (r.point (67)[0] == Approx (2.0f).margin (1e-3));
    CHECK (r.point (60)[0] == Approx (0.0f).margin (1e-3));

    r.run ({ ev (Type::Slide, 2, 0, 1.0f) }, 3); // CC74 = 127: +1 on PC2 (bipolar)
    CHECK (r.point (60)[1] == Approx (1.0f).margin (1e-3));
    CHECK (r.point (67)[1] == Approx (0.0f).margin (0.01)); // untouched: slide sits at centre 64

    r.run ({ { 0, Type::PolyPressure, 60, 0.5f, 2 } }, 3);
    CHECK (r.point (60)[0] == Approx (1.0f).margin (1e-3));
    CHECK (r.point (67)[0] == Approx (2.0f).margin (1e-3));
}

TEST_CASE ("MPE: pressure curve and smoothing", "[mpe]")
{
    auto p = mpeParams();
    p.mpe.pressureCurve = 1.0f; // more response to a light touch: 0.25 -> 0.25^(1/3)
    Rig r (p);
    r.run ({ ev (Type::NoteOn, 2, 60, 0.8f) });
    r.run ({ ev (Type::Pressure, 2, 0, 0.25f) }, 3);
    CHECK (r.voice (60).pressure == Approx (std::pow (0.25f, 1.0f / 3.0f)).margin (1e-3));

    p = mpeParams();
    p.mpe.smoothing = 0.1f;
    Rig slow (p);
    slow.run ({ ev (Type::NoteOn, 2, 60, 0.8f) });
    slow.run ({ ev (Type::Pressure, 2, 0, 1.0f) }); // 10 ms later: partway
    const float early = slow.voice (60).pressure;
    CHECK (early > 0.05f);
    CHECK (early < 0.3f);
    slow.run ({}, 60); // 0.6 s: there
    CHECK (slow.voice (60).pressure == Approx (1.0f).margin (0.01));
}

TEST_CASE ("MPE: a released note keeps its expression when its channel is reused", "[mpe]")
{
    auto p = mpeParams();
    p.release = 1.0f;
    Rig r (p);
    r.run ({ ev (Type::PitchBend, 2, 0, 0.25f), ev (Type::NoteOn, 2, 60, 0.8f), ev (Type::Pressure, 2, 0, 0.8f) }, 3);
    r.run ({ ev (Type::NoteOff, 2, 60, 0.0f) });
    // The controller resets the channel and plays a new note on it.
    r.run ({ ev (Type::PitchBend, 2, 0, 0.0f), ev (Type::Pressure, 2, 0, 0.0f), ev (Type::NoteOn, 2, 62, 0.8f) }, 3);
    Synth::VoiceInfo info[4];
    const int n = r.s.voiceInfo (info, 4);
    REQUIRE (n == 2);
    for (int i = 0; i < n; ++i)
    {
        if (info[i].note == 60)
        {
            CHECK (info[i].releasing);
            CHECK (info[i].bendSemitones == Approx (12.0f));
        }
        else
            CHECK (info[i].bendSemitones == Approx (0.0f));
    }
}

TEST_CASE ("MPE: upper zone, and MPE off leaves channels alone", "[mpe]")
{
    auto p = mpeParams();
    p.mpe.upperZone = true; // master 16, members 1-15
    Rig upper (p);
    upper.run ({ ev (Type::NoteOn, 1, 60, 0.8f), ev (Type::PitchBend, 1, 0, 0.5f) });
    CHECK (upper.voice (60).bendSemitones == Approx (24.0f));
    upper.run ({ ev (Type::PitchBend, 16, 0, 1.0f) }); // master: global
    CHECK (upper.voice (60).bendSemitones == Approx (24.0f));

    SynthParams off; // MPE disabled: channel bends are global, pressure is the Stage 5 global pressure
    Rig plain (off);
    plain.run ({ ev (Type::NoteOn, 2, 60, 0.8f), ev (Type::NoteOn, 3, 64, 0.8f), ev (Type::PitchBend, 2, 0, 0.5f) });
    CHECK (plain.voice (60).bendSemitones == Approx (0.0f));
    CHECK (plain.voice (64).bendSemitones == Approx (0.0f));
    Point pts[4];
    CHECK (plain.s.voicePoints (pts, 4) == 2);
    CHECK (pts[0] == pts[1]); // no per-voice points: everything shares the heard point
}
