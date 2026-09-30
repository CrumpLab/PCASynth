// Fidelity: pitch curves, sharp attacks, inspection scores, the fit report and
// more than 32 components.
#include "Render.h"
#include "TestHelpers.h"
#include "TrainingSet.h"

#include "pcs/Inspect.h"
#include "pcs/Model.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace pcs;
using Catch::Approx;

namespace {
constexpr double kTwoPi = 6.28318530717958647692;

// A harmonic tone with vibrato: `depth` cents at `rate` Hz, from t = 0.
AudioBuffer vibratoTone (double f0, double seconds, double rate, double depth)
{
    AudioBuffer a;
    a.sampleRate = 48000.0;
    a.resize (1, static_cast<int> (seconds * a.sampleRate));
    double phase = 0.0;
    for (size_t i = 0; i < a.channels[0].size(); ++i)
    {
        const double t = static_cast<double> (i) / a.sampleRate;
        phase += kTwoPi * f0 * std::pow (2.0, depth * std::sin (kTwoPi * rate * t) / 1200.0) / a.sampleRate;
        double x = 0.0;
        for (int h = 1; h <= 12; ++h)
            x += 0.3 / h * std::sin (h * phase);
        a.channels[0][i] = static_cast<float> (x);
    }
    return a;
}

double correlation (const std::vector<float>& a, const std::vector<double>& b)
{
    const size_t n = std::min (a.size(), b.size());
    double ma = 0, mb = 0;
    for (size_t i = 0; i < n; ++i)
    {
        ma += a[i] / n;
        mb += b[i] / n;
    }
    double sab = 0, saa = 0, sbb = 0;
    for (size_t i = 0; i < n; ++i)
    {
        sab += (a[i] - ma) * (b[i] - mb);
        saa += (a[i] - ma) * (a[i] - ma);
        sbb += (b[i] - mb) * (b[i] - mb);
    }
    return sab / std::sqrt (saa * sbb + 1e-30);
}

AnalysisSettings settings()
{
    AnalysisSettings s;
    s.duration = 1.0;
    s.harmonics = 24;
    s.noiseBands = 8;
    return s;
}
} // namespace

TEST_CASE ("Pitch curves: analysis follows vibrato, and the synth plays it back", "[fidelity]")
{
    const auto tone = vibratoTone (220.0, 1.3, 5.0, 30.0);
    auto s = settings();
    s.midiNote = 57;
    const auto a = analyseHarmonics (tone, s, "vibrato");
    REQUIRE (a.pitchCents.size() == static_cast<size_t> (a.numFrames));
    // The known curve at each frame's time (frame 0 sits 1 ms before the onset).
    std::vector<double> want;
    float lo = 1e9f, hi = -1e9f;
    for (int t = 10; t < a.numFrames - 10; ++t)
    {
        want.push_back (30.0 * std::sin (kTwoPi * 5.0 * (t / a.frameRate - 0.001)));
        lo = std::min (lo, a.pitchCents[static_cast<size_t> (t)]);
        hi = std::max (hi, a.pitchCents[static_cast<size_t> (t)]);
    }
    const std::vector<float> got (a.pitchCents.begin() + 10, a.pitchCents.end() - 10);
    CHECK (correlation (got, want) > 0.95);
    CHECK (hi - lo == Approx (60.0).margin (10.0));

    auto flat = s;
    flat.trackPitch = false;
    CHECK (analyseHarmonics (tone, flat, "vibrato").pitchCents.empty());

    // The synth replays it: a single-sound model rendered and re-analysed has the same curve.
    const auto m = singleSoundModel (a, s);
    CHECK (m.hasPitchCurve);
    const auto again = analyseHarmonics (renderPoint (std::make_shared<const Model> (m), {}, 57.0, 48000.0), s, "again");
    const std::vector<float> back (again.pitchCents.begin() + 10, again.pitchCents.end() - 10);
    CHECK (correlation (back, want) > 0.9);
    // Pitch Envelope 0 plays it steady.
    auto quiet = [&] (float amount) {
        SynthParams p;
        p.mode = PlayMode::OneShot;
        p.pitchEnvelope = amount;
        const auto audio = tools::renderNotes (std::make_shared<const Model> (m), p, { { 0.0, 1.0, 57, 1.0f } }, 1.0);
        const auto h = analyseHarmonics (audio, s, "x");
        float mn = 1e9f, mx = -1e9f;
        for (int t = 10; t < h.numFrames - 20; ++t)
        {
            mn = std::min (mn, h.pitchCents[static_cast<size_t> (t)]);
            mx = std::max (mx, h.pitchCents[static_cast<size_t> (t)]);
        }
        return mx - mn;
    };
    CHECK (quiet (0.0f) < 5.0f);
    CHECK (quiet (1.0f) > 40.0f);
}

TEST_CASE ("Pure noise doesn't steer the pitch curve", "[fidelity]")
{
    AudioBuffer noise;
    noise.sampleRate = 48000.0;
    noise.resize (1, 60000);
    uint32_t r = 1;
    for (auto& x : noise.channels[0])
    {
        r ^= r << 13;
        r ^= r >> 17;
        r ^= r << 5;
        x = static_cast<float> (static_cast<int32_t> (r)) / 4.0e9f;
    }
    auto s = settings();
    s.midiNote = 57;
    s.tuneSearchCents = 1.0;
    s.trackPartials = false;
    const auto a = analyseHarmonics (noise, s, "noise");
    for (float c : a.pitchCents)
        REQUIRE (std::abs (c) < 1.0f);
}

TEST_CASE ("Sharp attacks: a plucked onset starts at full strength", "[fidelity]")
{
    // An instant onset after 50 ms of silence, decaying.
    const auto pluck = test::harmonicTone (220.0, 1.2, [] (int h, double t) { return 0.3 / h * std::exp (-3.0 * t * h); }, 20, 48000.0, 0.05);
    auto s = settings();
    s.midiNote = 57;
    auto energy = [] (const HarmonicSound& h, int t) {
        double e = 0.0;
        for (int k = 0; k < h.numHarmonics; ++k)
            e += std::pow (10.0, h.at (t, k) / 10.0);
        return 10.0 * std::log10 (e);
    };
    const auto sharp = analyseHarmonics (pluck, s, "pluck");
    auto soft = s;
    soft.sharpAttacks = false;
    const auto smeared = analyseHarmonics (pluck, soft, "pluck");
    // Frame 1 (the note's first 10 ms) is within 2 dB of the peak with sharp
    // attacks; the long window straddling the onset misses more of it.
    double peakSharp = -300, peakSoft = -300;
    for (int t = 0; t < 10; ++t)
    {
        peakSharp = std::max (peakSharp, energy (sharp, t));
        peakSoft = std::max (peakSoft, energy (smeared, t));
    }
    CHECK (energy (sharp, 1) > peakSharp - 2.0);
    CHECK (energy (sharp, 0) - peakSharp > energy (smeared, 0) - peakSoft);

    // And it sounds closer to the original over the attack.
    auto inspectWith = [&] (const AnalysisSettings& a) {
        std::vector<HarmonicSound> two { analyseHarmonics (pluck, a, "pluck"), analyseHarmonics (pluck, a, "twin") };
        const auto m = trainModel (two, a);
        return inspectSound (m, 0, pluck);
    };
    CHECK (inspectWith (s).analysisAttackError < inspectWith (soft).analysisAttackError);
}

TEST_CASE ("Inspection: renders line up, scores are small for training sounds, and PCA loss shows", "[fidelity]")
{
    testgen::Options o;
    o.duration = 1.2;
    o.variations = 2;
    const auto clips = testgen::generateTrainingSet (o);
    std::vector<HarmonicSound> sounds;
    for (const auto& c : clips)
        sounds.push_back (analyseHarmonics (c.audio, settings(), c.name));
    const auto m = trainModel (sounds, settings());
    for (int i : { 0, 5, 11 })
    {
        const auto& clip = clips[static_cast<size_t> (i)];
        const auto full = inspectSound (m, m.soundIndex (clip.name), clip.audio);
        INFO (clip.name);
        CHECK (full.original.numSamples() == full.analysis.numSamples());
        CHECK (full.model.numSamples() == full.original.numSamples());
        CHECK (full.analysisError < 8.0); // 24 harmonics: nothing above ~6 kHz at C4
        CHECK (full.pcaError < 1.0);      // all components: the model is its analysis
        CHECK (full.envelopeError < 0.1);
        InspectOptions two;
        two.components = 2;
        const auto few = inspectSound (m, m.soundIndex (clip.name), clip.audio, two);
        CHECK (few.envelopeError > full.envelopeError + 1.0);
        CHECK (few.pcaError > full.pcaError);
    }
    // Identical signals are 0 apart; a different one is not.
    CHECK (spectralDistance (spectrogram (clips[0].audio.channels[0], 48000.0), spectrogram (clips[0].audio.channels[0], 48000.0)) == 0.0);
    CHECK (spectralDistance (spectrogram (clips[0].audio.channels[0], 48000.0), spectrogram (clips[7].audio.channels[0], 48000.0)) > 3.0);
}

TEST_CASE ("The fit report falls with components, is saved, and more than 32 components are kept", "[fidelity]")
{
    testgen::Options o;
    o.duration = 1.2;
    o.variations = 4;
    std::vector<HarmonicSound> sounds;
    for (const auto& c : testgen::generateTrainingSet (o))
        sounds.push_back (analyseHarmonics (c.audio, settings(), c.name));
    const auto m = trainModel (sounds, settings());
    CHECK (m.numComponents() == 39); // 40 sounds: up to 64 components now
    REQUIRE (m.fitByComponents.size() == 40);
    REQUIRE (m.fitErrorDb.size() == 40);
    CHECK (m.fitByComponents.front() > 5.0f);
    CHECK (m.fitByComponents.back() < 0.05f);
    for (size_t k = 1; k < m.fitByComponents.size(); ++k)
        CHECK (m.fitByComponents[k] <= m.fitByComponents[k - 1] + 0.05f);
    const auto back = deserializeModel (serializeModel (m).data(), serializeModel (m).size());
    CHECK (back.fitByComponents == m.fitByComponents);
    CHECK (back.hasPitchCurve == m.hasPitchCurve);
    CHECK (back.analysis.trackPitch == m.analysis.trackPitch);
    CHECK (back.analysis.sharpAttacks == m.analysis.sharpAttacks);
    std::vector<float> z (39, 0.4f);
    CHECK (back.decode (z).pitchCents == m.decode (z).pitchCents);
}
