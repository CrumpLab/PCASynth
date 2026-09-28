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
