// Waveform spaces (PCAWave): alignment, exact reconstruction, mix weights,
// level, the file format, and playing them.
#include "Render.h"
#include "TestHelpers.h"
#include "TrainingSet.h"

#include "pcs/Inspect.h"
#include "pcs/WaveModel.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace pcs;
using Catch::Approx;

namespace {
struct Set
{
    std::vector<AudioBuffer> audio;
    std::vector<std::string> names;
};

Set smallSet (int variations = 2)
{
    testgen::Options o;
    o.duration = 1.2;
    o.variations = variations;
    Set s;
    for (auto& c : testgen::generateTrainingSet (o))
    {
        s.audio.push_back (std::move (c.audio));
        s.names.push_back (c.name);
    }
    return s;
}

WaveSettings settings()
{
    WaveSettings w;
    w.duration = 1.0;
    w.sampleRate = 24000.0;
    return w;
}

std::shared_ptr<const WaveModel> smallModel()
{
    static auto m = [] {
        const auto s = smallSet();
        return std::make_shared<const WaveModel> (trainWaveModel (s.audio, s.names, settings()));
    }();
    return m;
}
} // namespace

TEST_CASE ("A waveform space reproduces every training sound exactly with all its components", "[wave]")
{
    const auto m = smallModel();
    REQUIRE (m->numSounds() == 20);
    CHECK (m->numComponents() == 19);
    CHECK (m->numSamples == 24000);
    REQUIRE (m->fitErrorDb.size() == 20);
    for (float e : m->fitErrorDb)
        CHECK (e < -60.0f);
    REQUIRE (m->fitByComponents.size() == 20);
    CHECK (m->fitByComponents.front() > -3.0f); // the mean alone is far from any one sound
    for (size_t k = 1; k < m->fitByComponents.size(); ++k)
        CHECK (m->fitByComponents[k] <= m->fitByComponents[k - 1] + 0.01f);

    // Each training point is the training sound's (aligned) waveform.
    const auto s = smallSet();
    const int i = 5;
    const int shift = m->shifts[static_cast<size_t> (i)];
    const auto prepared = prepareWave (s.audio[static_cast<size_t> (i)], m->settings, m->refHz, {}, s.names[static_cast<size_t> (i)], &shift);
    const auto z = m->soundZ (i);
    const auto decoded = m->decode (z.data(), static_cast<int> (z.size()));
    double diff = 0.0, peak = 0.0;
    for (size_t k = 0; k < decoded.size(); ++k)
    {
        diff = std::max (diff, static_cast<double> (std::abs (decoded[k] - prepared.samples[k])));
        peak = std::max (peak, static_cast<double> (std::abs (prepared.samples[k])));
    }
    CHECK (diff < 1e-3 * peak);
}

TEST_CASE ("Mix weights: a point is a weighted sum of the training sounds", "[wave]")
{
    const auto m = smallModel();
    const auto z = m->soundZ (3);
    const auto w = m->mixWeights (z.data(), static_cast<int> (z.size()));
    double sum = 0.0;
    for (size_t i = 0; i < w.size(); ++i)
    {
        sum += w[i];
        CHECK (w[i] == Approx (i == 3 ? 1.0 : 0.0).margin (1e-4));
    }
    CHECK (sum == Approx (1.0));
    const std::vector<float> centre (static_cast<size_t> (m->numComponents()), 0.0f);
    for (double x : m->mixWeights (centre.data(), static_cast<int> (centre.size())))
        CHECK (x == Approx (1.0 / m->numSounds()));

    // Halfway between two sounds: half of each.
    const auto a = m->soundZ (0), b = m->soundZ (7);
    std::vector<float> mid (a.size());
    for (size_t j = 0; j < a.size(); ++j)
        mid[j] = 0.5f * (a[j] + b[j]);
    const auto h = m->mixWeights (mid.data(), static_cast<int> (mid.size()));
    CHECK (h[0] == Approx (0.5).margin (1e-4));
    CHECK (h[7] == Approx (0.5).margin (1e-4));
}

TEST_CASE ("Mix amounts: dialling sounds in from the centre", "[wave]")
{
    const auto m = smallModel();
    const int n = m->numSounds(), k = m->numComponents();
    auto near = [] (const std::vector<float>& a, const std::vector<float>& b) {
        double d = 0.0;
        for (size_t j = 0; j < a.size(); ++j)
            d = std::max (d, static_cast<double> (std::abs (a[j] - b[j])));
        return d;
    };

    // All 0 is the centre; 1 on one sound is that sound; halves are the midpoint.
    std::vector<double> a (static_cast<size_t> (n), 0.0);
    CHECK (near (m->pointFromAmounts (a.data(), n), std::vector<float> (static_cast<size_t> (k), 0.0f)) < 1e-6);
    a[2] = 1.0;
    CHECK (near (m->pointFromAmounts (a.data(), n), m->soundZ (2)) < 1e-4);
    a[2] = 0.5;
    a[5] = 0.5;
    const auto s2 = m->soundZ (2), s5 = m->soundZ (5);
    std::vector<float> mid (s2.size());
    for (size_t j = 0; j < mid.size(); ++j)
        mid[j] = 0.5f * (s2[j] + s5[j]);
    CHECK (near (m->pointFromAmounts (a.data(), n), mid) < 1e-4);

    // The same constant on every sound doesn't move the point.
    auto shifted = a;
    for (auto& x : shifted)
        x += 0.3;
    CHECK (near (m->pointFromAmounts (shifted.data(), n), m->pointFromAmounts (a.data(), n)) < 1e-4);

    // Reading amounts back: the same point, and sparse at sounds and the centre.
    const std::vector<double> mix { 0.4, 0.0, 0.0, -0.2, 0.0, 0.0, 0.7, 0.0 };
    const auto z = m->pointFromAmounts (mix.data(), static_cast<int> (mix.size()));
    const auto back = m->mixAmounts (z.data(), k);
    CHECK (near (m->pointFromAmounts (back.data(), n), z) < 1e-4);
    for (size_t i = 0; i < mix.size(); ++i)
        CHECK (back[i] == Approx (mix[i]).margin (1e-4));
    const auto at3 = m->mixAmounts (m->soundZ (3).data(), k);
    for (int i = 0; i < n; ++i)
        CHECK (at3[static_cast<size_t> (i)] == (i == 3 ? 1.0 : 0.0));
    const std::vector<float> centre (static_cast<size_t> (k), 0.0f);
    for (double x : m->mixAmounts (centre.data(), k))
        CHECK (x == 0.0);
}

TEST_CASE ("Waveform level: the quadratic form matches the decoded waveform", "[wave]")
{
    const auto m = smallModel();
    std::vector<float> z (static_cast<size_t> (m->numComponents()), 0.0f);
    z[0] = 1.5f;
    z[3] = -2.0f;
    const auto x = m->decode (z.data(), static_cast<int> (z.size()));
    const auto w = static_cast<size_t> (WaveModel::kLevelSeconds * m->sampleRate);
    double e = 0.0;
    for (size_t i = 0; i < w; ++i)
        e += static_cast<double> (x[i]) * x[i];
    CHECK (m->levelDb (z.data(), static_cast<int> (z.size())) == Approx (10.0 * std::log10 (e / static_cast<double> (w))).margin (0.01));
}

TEST_CASE ("Waveform alignment: pitch, onset and phase", "[wave]")
{
    // The same tone at two pitches, one starting later and at another phase.
    auto tone = [] (double f0, double delay, double phase) {
        return test::harmonicTone (f0, 1.3, [] (int h, double t) { return 0.3 / h * std::exp (-t); }, 12, 48000.0, delay + phase);
    };
    std::vector<AudioBuffer> audio { tone (midiToHz (60), 0.0, 0.0), tone (midiToHz (62), 0.137, 0.0) };
    WaveSettings s = settings();
    s.autoPitch = true;
    const auto m = trainWaveModel (audio, { "a", "b" }, s);
    // Both brought to one pitch (the median; of two, the upper), and lined up.
    CHECK (69.0 + 12.0 * std::log2 (m.refHz / 440.0) == Approx (62.0).margin (0.1));
    const auto pa = prepareWave (audio[0], s, m.refHz, {}, "a", &m.shifts[0]);
    const auto pb = prepareWave (audio[1], s, m.refHz, {}, "b", &m.shifts[1]);
    double c = 0.0, ea = 0.0, eb = 0.0;
    for (size_t i = 1200; i < 12000; ++i)
    {
        c += pa.samples[i] * pb.samples[i];
        ea += pa.samples[i] * pa.samples[i];
        eb += pb.samples[i] * pb.samples[i];
    }
    CHECK (c / std::sqrt (ea * eb) > 0.95); // the same waveform, in phase
}

TEST_CASE ("File format round trip", "[wave]")
{
    const auto m = smallModel();
    const auto bytes = serializeSpace (*m);
    const auto back = std::dynamic_pointer_cast<WaveModel> (deserializeSpace (bytes.data(), bytes.size()));
    REQUIRE (back != nullptr);
    CHECK (back->names == m->names);
    CHECK (back->shifts == m->shifts);
    CHECK (back->refHz == Approx (m->refHz));
    CHECK (back->fitByComponents == m->fitByComponents);
    std::vector<float> z (static_cast<size_t> (m->numComponents()), 0.3f);
    CHECK (back->decode (z.data(), static_cast<int> (z.size())) == m->decode (z.data(), static_cast<int> (z.size())));
}

TEST_CASE ("Playing a waveform space: pitch, play modes, modulation", "[wave][synth]")
{
    const auto m = smallModel();
    const int i = m->soundIndex ("reed_1");
    SynthParams p;
    const auto z = m->soundZ (i);
    std::copy (z.begin(), z.end(), p.z.begin());
    p.mode = PlayMode::OneShot;
    // At the common pitch it plays the sound; an octave up, twice the pitch (and half as long).
    AnalysisSettings a;
    a.duration = 0.3;
    a.autoPitch = true;
    const double ref = m->refMidi();
    const auto at = tools::renderNotes (m, p, { { 0.0, 2.0, static_cast<int> (std::lround (ref)), 1.0f } }, 1.2);
    const auto up = tools::renderNotes (m, p, { { 0.0, 2.0, static_cast<int> (std::lround (ref)) + 12, 1.0f } }, 1.2);
    CHECK (analyseHarmonics (at, a, "at").midiPitch() == Approx (std::lround (ref)).margin (0.2));
    CHECK (analyseHarmonics (up, a, "up").midiPitch() == Approx (std::lround (ref) + 12).margin (0.2));
    auto rmsBetween = [] (const AudioBuffer& b, double from, double to) {
        double s = 0.0;
        const auto i0 = static_cast<size_t> (from * 48000.0), i1 = static_cast<size_t> (to * 48000.0);
        for (size_t k = i0; k < i1; ++k)
            s += b.channels[0][k] * b.channels[0][k];
        return std::sqrt (s / static_cast<double> (i1 - i0));
    };
    CHECK (rmsBetween (at, 0.7, 0.9) > 0.01); // one second long at its own pitch
    CHECK (rmsBetween (up, 0.7, 0.9) < 1e-4); // half a second an octave up

    // Loop sustains past the end; ping-pong too.
    for (auto mode : { PlayMode::Loop, PlayMode::PingPong, PlayMode::Scan })
    {
        p.mode = mode;
        const auto held = tools::renderNotes (m, p, { { 0.0, 2.5, 60, 1.0f } }, 2.6);
        CHECK (rmsBetween (held, 2.0, 2.4) > 0.01);
    }

    // Walks, spread and MPE move each voice's weights; the output stays finite and alive.
    p.mode = PlayMode::Loop;
    p.mod.walk.enabled = true;
    p.mod.walk.perVoice = 1.0f;
    p.mod.walk.rate = 3.0f;
    p.mod.voiceSpread = 1.0f;
    p.levelLock = 1.0f;
    const auto walked = tools::renderNotes (m, p, { { 0.0, 1.5, 55, 0.8f }, { 0.0, 1.5, 62, 0.8f } }, 1.6);
    bool finite = true;
    float peak = 0.0f;
    for (float x : walked.channels[0])
    {
        finite = finite && std::isfinite (x);
        peak = std::max (peak, std::abs (x));
    }
    CHECK (finite);
    CHECK (peak > 0.01f);
    CHECK (peak < 4.0f);
}

TEST_CASE ("Inspecting a waveform space", "[wave]")
{
    const auto m = smallModel();
    const auto s = smallSet();
    const int i = 11;
    const auto r = inspectSound (std::static_pointer_cast<const Space> (m), i, s.audio[static_cast<size_t> (i)]);
    CHECK (r.waveform);
    CHECK (r.analysisError < 2.0);
    CHECK (r.pcaError < 0.5);
    CHECK (r.envelopeError < -60.0);
    InspectOptions few;
    few.components = 3;
    const auto t = inspectSound (std::static_pointer_cast<const Space> (m), i, s.audio[static_cast<size_t> (i)], few);
    CHECK (t.envelopeError > -20.0);
    CHECK (t.pcaError > r.pcaError + 1.0);
}
