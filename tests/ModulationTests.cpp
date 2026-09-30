#include "Render.h"
#include "TrainingSet.h"

#include "pcs/Modulation.h"
#include "pcs/Synth.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace pcs;
using Catch::Approx;

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

std::vector<Point> soundPoints (const Model& m)
{
    std::vector<Point> out;
    for (int i = 0; i < m.numSounds(); ++i)
    {
        Point p {};
        const auto z = m.soundZ (i);
        std::copy (z.begin(), z.end(), p.begin());
        out.push_back (p);
    }
    return out;
}

Point ones()
{
    Point p;
    p.fill (1.0f);
    return p;
}
} // namespace

TEST_CASE ("Drift wanders with the expected spread and stays in bounds", "[walk]")
{
    WalkParams p;
    p.enabled = true;
    p.amount = 1.0f;
    p.tether = 1.0f;
    p.dims = 2;
    RandomWalk w;
    w.reset (3);
    double ss = 0.0, maxAbs = 0.0;
    long n = 0;
    for (int i = 0; i < 200000; ++i) // 2000 s at 10 ms, rate 1 Hz
    {
        w.advance (0.01, p, 1.0, {}, {}, ones());
        if (i > 1000)
        {
            ss += w.offset()[0] * w.offset()[0];
            ++n;
        }
        maxAbs = std::max (maxAbs, static_cast<double> (std::abs (w.offset()[1])));
        REQUIRE (w.offset()[2] == 0.0f); // only `dims` components move
    }
    CHECK (std::sqrt (ss / n) == Approx (1.0).margin (0.2));
    CHECK (maxAbs <= 4.0);

    // Low tether wanders further (still bounded).
    p.tether = 0.1f;
    w.reset (3);
    ss = 0.0;
    n = 0;
    for (int i = 0; i < 200000; ++i)
    {
        w.advance (0.01, p, 1.0, {}, {}, ones());
        ss += w.offset()[0] * w.offset()[0];
        ++n;
        REQUIRE (std::abs (w.offset()[0]) <= 4.0f);
    }
    CHECK (std::sqrt (ss / n) > 1.8);
}

TEST_CASE ("Jumps hold between steps without glide and move smoothly with it", "[walk]")
{
    WalkParams p;
    p.enabled = true;
    p.mode = WalkMode::Jumps;
    p.dims = 3;
    p.glide = 0.0f;
    RandomWalk w;
    w.reset (5);
    int changes = 0;
    Point last = w.offset();
    for (int i = 0; i < 1000; ++i) // 10 s, 0.5 s steps
    {
        w.advance (0.01, p, 0.5, {}, {}, ones());
        if (w.offset() != last)
            ++changes;
        last = w.offset();
    }
    CHECK (changes >= 19);
    CHECK (changes <= 21);

    p.glide = 1.0f;
    w.reset (5);
    float maxDelta = 0.0f;
    last = w.offset();
    for (int i = 0; i < 1000; ++i)
    {
        w.advance (0.01, p, 0.5, {}, {}, ones());
        maxDelta = std::max (maxDelta, std::abs (w.offset()[0] - last[0]));
        last = w.offset();
    }
    CHECK (maxDelta < 0.2f); // no jumps: at most a small move per 10 ms
}

TEST_CASE ("Tours arrive at training sounds; neighbour tours step to near ones", "[walk]")
{
    const auto m = model();
    const auto sounds = soundPoints (*m);
    for (auto mode : { WalkMode::Tour, WalkMode::NeighbourTour })
    {
        WalkParams p;
        p.enabled = true;
        p.mode = mode;
        p.amount = 1.0f;
        p.glide = 0.5f;
        RandomWalk w;
        w.reset (9);
        Point home {};
        home[0] = 0.5f;
        auto dist = [&] (int x, int y) {
            float d = 0.0f;
            for (int j = 0; j < kMaxComponents; ++j)
                d += std::pow (sounds[static_cast<size_t> (x)][static_cast<size_t> (j)] - sounds[static_cast<size_t> (y)][static_cast<size_t> (j)], 2.0f);
            return d;
        };
        std::vector<int> path; // every sound the tour heads to, in order
        for (int step = 0; step < 12; ++step)
        {
            for (int i = 0; i < 100; ++i) // one 1 s step
            {
                w.advance (0.01, p, 1.0, home, sounds, ones());
                if (path.empty() || w.currentSound() != path.back())
                    path.push_back (w.currentSound());
            }
            // At the end of each step the walk has arrived at its sound.
            const int s = w.currentSound();
            REQUIRE (s >= 0);
            for (int j = 0; j < kMaxComponents; ++j)
                REQUIRE (home[static_cast<size_t> (j)] + w.offset()[static_cast<size_t> (j)] == Approx (sounds[static_cast<size_t> (s)][static_cast<size_t> (j)]).margin (1e-4));
        }
        // One new sound per step, never the same twice in a row.
        CHECK (path.size() == 12);
        for (size_t k = 1; k < path.size(); ++k)
        {
            CHECK (path[k] != path[k - 1]);
            if (mode == WalkMode::NeighbourTour)
            {
                // Among the 3 nearest to the previous sound (not counting the one
                // before it, which is excluded to avoid backtracking).
                const int from = path[k - 1], to = path[k], before = k >= 2 ? path[k - 2] : -1;
                int closer = 0;
                for (int i = 0; i < m->numSounds(); ++i)
                    if (i != from && i != before && dist (i, from) < dist (to, from))
                        ++closer;
                CHECK (closer <= 2);
            }
        }

    }
}

TEST_CASE ("LFO shapes stay in range and keep their period", "[lfo]")
{
    for (auto shape : { LfoShape::Sine, LfoShape::Triangle, LfoShape::Saw, LfoShape::Square, LfoShape::SampleHold, LfoShape::SmoothRandom })
    {
        LfoParams p;
        p.shape = shape;
        Lfo l;
        l.reset (1);
        int upCrossings = 0;
        float prev = l.advance (0.0, p, 1.0);
        for (int i = 0; i < 4000; ++i) // 4 s of a 1 s cycle at 1 ms
        {
            const float v = l.advance (0.001, p, 1.0);
            REQUIRE (v >= -1.0001f);
            REQUIRE (v <= 1.0001f);
            if (prev < 0.0f && v >= 0.0f)
                ++upCrossings;
            prev = v;
        }
        if (shape == LfoShape::Sine || shape == LfoShape::Triangle || shape == LfoShape::Saw)
            CHECK (upCrossings == 4);
    }
}

TEST_CASE ("The synth's point follows the walk, LFOs, expression and the macro", "[walk][synth]")
{
    const auto m = model();
    Synth s;
    s.prepare (48000.0);
    SynthParams p;
    p.mode = PlayMode::Loop;
    s.setParams (p);
    s.setModel (m);
    std::vector<float> buf (4800);
    float* ch[] = { buf.data() };
    auto run = [&] (int blocks, std::vector<MidiEvent> ev = {}) {
        for (int b = 0; b < blocks; ++b)
        {
            s.process (ch, 1, 4800, ev.data(), static_cast<int> (ev.size()));
            ev.clear();
        }
    };

    SECTION ("walk off: the heard point is home")
    {
        run (5);
        CHECK (s.heardPoint() == s.currentZ());
    }
    SECTION ("walk on: it moves, and fades back home when switched off")
    {
        p.mod.walk.enabled = true;
        p.mod.walk.rate = 2.0f;
        s.setParams (p);
        run (20);
        float d = 0.0f;
        for (int j = 0; j < 4; ++j)
            d += std::abs (s.heardPoint()[static_cast<size_t> (j)]);
        CHECK (d > 0.05f);
        p.mod.walk.enabled = false;
        s.setParams (p);
        run (20);
        CHECK (s.heardPoint()[0] == Approx (0.0f).margin (1e-3));
    }
    SECTION ("per-voice walks give each note its own point; velocity moves it")
    {
        p.mod.walk.enabled = true;
        p.mod.walk.perVoice = 1.0f;
        p.mod.walk.rate = 4.0f;
        s.setParams (p);
        run (1, { { 0, MidiEvent::Type::NoteOn, 60, 1.0f }, { 0, MidiEvent::Type::NoteOn, 67, 1.0f } });
        run (10);
        Point pts[4];
        REQUIRE (s.voicePoints (pts, 4) == 2);
        CHECK (pts[0] != pts[1]);

        p.mod.walk.enabled = false;
        p.mod.velocity = { 2, 2.0f }; // PC3, +2 SD at full velocity
        s.setParams (p);
        run (1, { { 0, MidiEvent::Type::AllNotesOff, 0, 0.0f } });
        run (10);
        run (1, { { 0, MidiEvent::Type::NoteOn, 62, 0.5f } });
        run (1);
        REQUIRE (s.voicePoints (pts, 4) >= 1);
        bool found = false;
        for (int i = 0; i < s.voicePoints (pts, 4); ++i)
            found = found || std::abs (pts[i][2] - 1.0f) < 1e-3f;
        CHECK (found);
    }
    SECTION ("an LFO on PC2 swings the point by its depth")
    {
        p.mod.lfo[0] = { true, LfoShape::Sine, 1.0f, false, 4.0f, 1.5f, 1 };
        s.setParams (p);
        float lo = 0.0f, hi = 0.0f;
        for (int b = 0; b < 40; ++b) // 4 s
        {
            run (1);
            lo = std::min (lo, s.heardPoint()[1]);
            hi = std::max (hi, s.heardPoint()[1]);
        }
        CHECK (hi == Approx (1.5f).margin (0.1));
        CHECK (lo == Approx (-1.5f).margin (0.1));
    }
    SECTION ("mod wheel and macro move towards the direction sound")
    {
        const auto target = m->soundZ (m->soundIndex ("vowel_1"));
        p.mod.hasDirection = true;
        std::copy (target.begin(), target.end(), p.mod.direction.begin());
        p.mod.modWheel = { kTowardSound, 1.0f };
        s.setParams (p);
        run (1, { { 0, MidiEvent::Type::ModWheel, 0, 1.0f } });
        for (size_t j = 0; j < target.size(); ++j)
            CHECK (s.heardPoint()[j] == Approx (target[j]).margin (1e-4));
        run (1, { { 0, MidiEvent::Type::ModWheel, 0, 0.0f } });
        p.mod.macro = 0.5f;
        s.setParams (p);
        run (1);
        CHECK (s.heardPoint()[0] == Approx (0.5f * target[0]).margin (1e-4));
    }
}

TEST_CASE ("Same seed, same path; restart on note repeats it", "[walk][synth]")
{
    const auto m = model();
    SynthParams p;
    p.mode = PlayMode::Loop;
    p.mod.walk.enabled = true;
    p.mod.walk.mode = WalkMode::Jumps;
    p.mod.walk.rate = 4.0f;
    p.mod.walk.seed = 42;
    p.mod.walk.restartOnNote = true;
    p.release = 0.05f;
    // Two identical phrases separated by silence.
    const std::vector<tools::Note> notes { { 0.0, 1.0, 60, 0.8f }, { 2.0, 1.0, 60, 0.8f } };
    const auto a = tools::renderNotes (m, p, notes, 3.0);
    double diff = 0.0, level = 0.0;
    for (size_t i = 0; i < 48000; ++i)
    {
        diff = std::max (diff, static_cast<double> (std::abs (a.channels[0][i] - a.channels[0][i + 96000])));
        level = std::max (level, static_cast<double> (std::abs (a.channels[0][i])));
    }
    CHECK (level > 0.01);
    CHECK (diff < 1e-4);

    // Another seed: a different path.
    p.mod.walk.seed = 43;
    const auto b = tools::renderNotes (m, p, notes, 3.0);
    double other = 0.0;
    for (size_t i = 0; i < 48000; ++i)
        other = std::max (other, static_cast<double> (std::abs (a.channels[0][i] - b.channels[0][i])));
    CHECK (other > 1e-3);
}

TEST_CASE ("Synced steps follow the tempo", "[walk][synth]")
{
    const auto m = model();
    Synth s;
    s.prepare (48000.0);
    SynthParams p;
    p.mod.walk.enabled = true;
    p.mod.walk.mode = WalkMode::Jumps;
    p.mod.walk.glide = 0.0f;
    p.mod.walk.sync = true;
    p.mod.walk.syncBeats = 1.0f; // a beat: 0.5 s at 120 BPM, 0.25 s at 240
    p.bpm = 240.0;
    s.setParams (p);
    s.setModel (m);
    std::vector<float> buf (480);
    float* ch[] = { buf.data() };
    int changes = 0;
    Point last = s.heardPoint();
    for (int b = 0; b < 400; ++b) // 4 s
    {
        s.process (ch, 1, 480, nullptr, 0);
        if (s.heardPoint() != last)
            ++changes;
        last = s.heardPoint();
    }
    CHECK (changes >= 15);
    CHECK (changes <= 17);
}
