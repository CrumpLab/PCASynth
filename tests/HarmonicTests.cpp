#include "TestHelpers.h"

#include "pcs/Harmonic.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace pcs;
using Catch::Approx;

namespace {
AnalysisSettings raw()
{
    AnalysisSettings s;
    s.duration = 1.0;
    s.trimOnset = false;
    s.normalizeLoudness = false;
    s.harmonics = 32;
    s.floorDb = -120.0;
    return s;
}
} // namespace

TEST_CASE ("Harmonic amplitudes of a steady tone are measured to within 0.2 dB", "[harmonic]")
{
    const double f0 = midiToHz (60) * std::pow (2.0, 7.0 / 1200.0); // 7 cents sharp
    auto amp = [] (int h, double) { return h > 20 ? 0.0 : (h % 3 == 0 ? 0.0 : 0.5 / h); };
    const auto sound = analyseHarmonics (test::harmonicTone (f0, 1.5, amp), raw(), "tone");

    CHECK (1200.0 * std::log2 (sound.f0 / f0) == Approx (0.0).margin (0.6));
    for (int t : { 20, 50, 80 })
        for (int h = 1; h <= 32; ++h)
        {
            const double expected = amp (h, 0.0);
            if (expected > 0.0)
                CHECK (sound.at (t, h - 1) == Approx (test::db (expected)).margin (0.2));
            else
                CHECK (sound.at (t, h - 1) < -70.0f);
        }
}

TEST_CASE ("Time-varying envelopes are followed", "[harmonic]")
{
    auto amp = [] (int h, double t) { return 0.4 / h * std::exp (-t * h * 0.8); };
    const auto sound = analyseHarmonics (test::harmonicTone (midiToHz (60), 1.2, amp), raw(), "decay");
    for (int t : { 20, 40, 60, 80 })
        for (int h : { 1, 2, 5 })
            CHECK (sound.at (t, h - 1) == Approx (test::db (amp (h, t / 100.0))).margin (0.5));
}

TEST_CASE ("Onsets are aligned and loudness normalised", "[harmonic]")
{
    auto amp = [] (int h, double t) { return std::min (1.0, t / 0.05) * 0.3 / h; };
    AnalysisSettings s = raw();
    s.trimOnset = true;
    s.normalizeLoudness = true;
    const auto early = analyseHarmonics (test::harmonicTone (midiToHz (60), 1.2, amp, 40, 48000.0, 0.1), s);
    const auto late = analyseHarmonics (test::harmonicTone (midiToHz (60), 1.2, amp, 40, 48000.0, 0.37), s);
    for (int t : { 2, 5, 30 })
        for (int h = 0; h < 5; ++h)
            CHECK (late.at (t, h) == Approx (early.at (t, h)).margin (0.3));

    double maxEnergy = 0.0;
    for (int t = 0; t < late.numFrames; ++t)
    {
        double e = 0.0;
        for (int h = 0; h < late.numHarmonics; ++h)
            e += std::pow (10.0, late.at (t, h) / 10.0);
        for (int b = 0; b < late.numNoiseBands; ++b) // noise counts too (2 × RMS², like A²)
            e += 2.0 * std::pow (10.0, late.noiseAt (t, b) / 10.0);
        maxEnergy = std::max (maxEnergy, e);
    }
    CHECK (10.0 * std::log10 (maxEnergy) == Approx (0.0).margin (0.01));
}

TEST_CASE ("Harmonics above Nyquist and silence sit at the floor", "[harmonic]")
{
    AnalysisSettings s = raw();
    s.harmonics = 128;
    s.floorDb = -80.0;
    s.duration = 0.5;
    auto amp = [] (int h, double) { return 0.5 / h; };
    auto audio = test::harmonicTone (midiToHz (60), 0.3, amp, 40, 16000.0); // 0.2 s of silence at the end
    const auto sound = analyseHarmonics (audio, s);
    CHECK (sound.at (10, 127) == -80.0f);  // 33 kHz, far above 8 kHz Nyquist
    CHECK (sound.at (45, 0) == -80.0f);    // after the tone
    CHECK_THROWS (analyseHarmonics (AudioBuffer { 48000.0, { std::vector<float> (4800, 0.0f) } }, s));
}

#include "TrainingSet.h"

TEST_CASE ("Pitch detection finds the note of every synthetic family, at several pitches", "[harmonic][pitch]")
{
    for (int note : { 40, 60, 76 })
    {
        testgen::Options o;
        o.midiNote = note;
        o.duration = 1.5;
        o.variations = 3;
        for (const auto& c : testgen::generateTrainingSet (o))
        {
            INFO (c.name << " at MIDI " << note);
            const auto mono = monoMix (c.audio);
            const double f = detectPitch (mono, c.audio.sampleRate, 2400);
            REQUIRE (f > 0.0);
            CHECK (std::abs (69.0 + 12.0 * std::log2 (f / 440.0) - note) < 0.5);
        }
    }
}

TEST_CASE ("Auto pitch analyses each sound at its own pitch", "[harmonic][pitch]")
{
    AnalysisSettings s;
    s.duration = 1.0;
    s.harmonics = 16;
    s.autoPitch = true;
    auto amp = [] (int h, double) { return 0.4 / h; };
    const auto low = analyseHarmonics (test::harmonicTone (midiToHz (45), 1.2, amp), s, "low");
    const auto high = analyseHarmonics (test::harmonicTone (midiToHz (72) * 1.004, 1.2, amp), s, "high");
    CHECK (low.f0 == Approx (midiToHz (45)).epsilon (0.001));
    CHECK (high.f0 == Approx (midiToHz (72) * 1.004).epsilon (0.001));
    // Same spectrum shape at both pitches.
    for (int h = 0; h < 8; ++h)
        CHECK (low.at (50, h) == Approx (high.at (50, h)).margin (0.5));
}

namespace {
// A tone whose partials may be stretched (f_h = h f0 sqrt(1 + B h^2)), plus white noise.
AudioBuffer stretchedTone (double f0, double seconds, double inharmonicity, int partials, double noiseRms, uint32_t seed = 1)
{
    AudioBuffer a;
    a.sampleRate = 48000.0;
    a.resize (1, static_cast<int> (seconds * a.sampleRate));
    for (int h = 1; h <= partials; ++h)
    {
        const double f = h * f0 * std::sqrt (1.0 + inharmonicity * h * h);
        if (f > 0.45 * a.sampleRate)
            break;
        for (size_t i = 0; i < a.channels[0].size(); ++i)
            a.channels[0][i] += static_cast<float> (0.3 / h * std::sin (6.28318530717958647692 * f * static_cast<double> (i) / a.sampleRate + h));
    }
    for (auto& x : a.channels[0]) // uniform noise with the given RMS
    {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        x += static_cast<float> (noiseRms * std::sqrt (3.0) * (2.0 * (seed / 4294967296.0) - 1.0));
    }
    return a;
}
} // namespace

TEST_CASE ("Residual noise is measured per band at the right level", "[harmonic][noise]")
{
    AnalysisSettings s = raw();
    s.harmonics = 20;
    s.noiseBands = 16;
    const double f0 = midiToHz (57), sigma = 0.01;
    const auto noisy = analyseHarmonics (stretchedTone (f0, 1.2, 0.0, 12, sigma), s);
    for (int b = 0; b < s.noiseBands; ++b)
    {
        const double lo = noiseBandEdge (b, s.noiseBands) * f0, hi = std::min (noiseBandEdge (b + 1, s.noiseBands) * f0, 0.48 * 48000.0);
        if (lo >= 0.48 * 48000.0)
            continue;
        const double expected = test::db (sigma * std::sqrt ((hi - lo) / 24000.0));
        double mean = 0.0;
        for (int t = 20; t < 80; ++t)
            mean += noisy.noiseAt (t, b) / 60.0;
        INFO ("band " << b << " (" << lo << "-" << hi << " Hz)");
        CHECK (mean == Approx (expected).margin (2.0));
    }
    // The harmonics are still measured correctly under the noise.
    CHECK (noisy.at (50, 0) == Approx (test::db (0.3)).margin (0.3));

    // A clean tone: nothing that deserves the name noise.
    const auto clean = analyseHarmonics (stretchedTone (f0, 1.2, 0.0, 12, 0.0), s);
    for (int b = 0; b < s.noiseBands; ++b)
        CHECK (clean.noiseAt (50, b) < -75.0f);
}

TEST_CASE ("Stretched partials are tracked, measured and reported in cents", "[harmonic][partials]")
{
    AnalysisSettings s = raw();
    s.harmonics = 40;
    s.noiseBands = 0;
    s.midiNote = 48;
    const double f0 = midiToHz (48), B = 0.0004;
    const auto sound = analyseHarmonics (stretchedTone (f0, 1.5, B, 40, 0.0), s);
    REQUIRE (sound.partialCents.size() == 40);
    CHECK (sound.f0 == Approx (f0 * std::sqrt (1.0 + B)).epsilon (0.001));
    for (int h = 1; h <= 30; ++h)
    {
        INFO ("partial " << h);
        const double trueCents = 1200.0 * std::log2 (std::sqrt (1.0 + B * h * h) / std::sqrt (1.0 + B));
        CHECK (sound.partialCents[static_cast<size_t> (h - 1)] == Approx (trueCents).margin (3.0));
        CHECK (sound.at (50, h - 1) == Approx (test::db (0.3 / h)).margin (0.5));
    }

    // Exact harmonics report ~0 cents.
    const auto exact = analyseHarmonics (stretchedTone (f0, 1.5, 0.0, 40, 0.0), s);
    for (int h = 0; h < 30; ++h)
        CHECK (std::abs (exact.partialCents[static_cast<size_t> (h)]) < 1.0f);
}
