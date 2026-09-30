#include "TrainingSet.h"

#include "pcs/Model.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace pcs;
using Catch::Approx;

namespace {
AnalysisSettings smallSettings()
{
    AnalysisSettings s;
    s.duration = 1.0;
    s.harmonics = 24;
    return s;
}

std::vector<HarmonicSound> smallSet()
{
    testgen::Options o;
    o.duration = 1.2;
    o.variations = 2;
    std::vector<HarmonicSound> out;
    for (const auto& c : testgen::generateTrainingSet (o))
        out.push_back (analyseHarmonics (c.audio, smallSettings(), c.name));
    return out;
}
} // namespace

TEST_CASE ("Training sounds decode back from their coordinates", "[model]")
{
    const auto sounds = smallSet();
    const auto m = trainModel (sounds, smallSettings());
    REQUIRE (m.numSounds() == 20);
    REQUIRE (m.numComponents() == 19);
    for (int i = 0; i < m.numSounds(); i += 3)
    {
        const auto z = m.soundZ (i);
        const auto projected = m.project (sounds[static_cast<size_t> (i)]);
        for (size_t j = 0; j < z.size(); ++j)
            CHECK (projected[j] == Approx (z[j]).margin (1e-3));
        const auto decoded = m.decode (z);
        for (size_t k = 0; k < decoded.db.size(); k += 7)
            CHECK (decoded.db[k] == Approx (sounds[static_cast<size_t> (i)].db[k]).margin (0.01));
    }
    // The z coordinates of the training set have unit SD per component.
    for (int j = 0; j < m.numComponents(); ++j)
    {
        double ss = 0.0;
        for (int i = 0; i < m.numSounds(); ++i)
            ss += std::pow (m.soundZ (i)[static_cast<size_t> (j)], 2.0);
        CHECK (ss / (m.numSounds() - 1) == Approx (1.0).epsilon (1e-4));
    }
}

TEST_CASE ("Model files round-trip", "[model]")
{
    auto m = trainModel (smallSet(), smallSettings(), 6);
    m.title = "Small set";
    const auto bytes = serializeModel (m);
    const auto back = deserializeModel (bytes.data(), bytes.size());
    CHECK (back.names == m.names);
    CHECK (back.title == "Small set");
    CHECK (back.numComponents() == 6);
    CHECK (back.analysis.harmonics == 24);
    CHECK (back.pca.totalVariance == Approx (m.pca.totalVariance));
    std::vector<float> z { 1.5f, -0.5f, 2.0f, 0.0f, -1.0f, 0.3f };
    CHECK (back.decode (z).db == m.decode (z).db);

    CHECK_THROWS (deserializeModel (bytes.data(), bytes.size() - 8));
    auto bad = bytes;
    bad[0] = 'X';
    CHECK_THROWS (deserializeModel (bad.data(), bad.size()));
}

TEST_CASE ("Decoding clamps far-out points to a sane range", "[model]")
{
    const auto m = trainModel (smallSet(), smallSettings(), 6);
    std::vector<float> z (6, 25.0f);
    for (float v : m.decode (z).db)
    {
        CHECK (v >= m.floorDb);
        CHECK (v <= 12.0f);
    }
}

#include <nlohmann/json.hpp>

namespace {
// A few families at several pitches: their spectra are defined in Hz (formants,
// cut-offs), so their shape in harmonic numbers changes with pitch.
std::vector<HarmonicSound> multiPitchSet (const std::vector<int>& notes, const AnalysisSettings& s)
{
    std::vector<HarmonicSound> out;
    for (const char* family : { "vowel", "reed", "brass", "bowed" })
        for (int note : notes)
        {
            testgen::Options o;
            o.midiNote = note;
            o.duration = 1.2;
            o.detuneCents = 0.0;
            auto clip = testgen::generate (family, 0, o);
            out.push_back (analyseHarmonics (clip.audio, s, clip.name + "_" + std::to_string (note)));
        }
    return out;
}

AnalysisSettings stage7Settings()
{
    AnalysisSettings s = smallSettings();
    s.autoPitch = true;
    return s;
}

double levelError (const HarmonicSound& a, const HarmonicSound& b, float floorDb)
{
    double sum = 0.0;
    long n = 0;
    for (size_t i = 0; i < a.db.size(); ++i)
        if (a.db[i] > floorDb + 20.0f || b.db[i] > floorDb + 20.0f)
        {
            sum += std::pow (a.db[i] - b.db[i], 2.0);
            ++n;
        }
    return std::sqrt (sum / std::max (1L, n));
}
} // namespace

TEST_CASE ("Every representation reproduces its training sounds: harmonics, noise and partials", "[model][stage7]")
{
    const auto sounds = smallSet();
    REQUIRE (sounds[0].numNoiseBands == 16);
    REQUIRE (sounds[0].partialCents.size() == 24);
    for (auto rep : { Representation::Decibels, Representation::ShapeLoudness, Representation::Linear })
    {
        INFO ("representation " << static_cast<int> (rep));
        auto s = smallSettings();
        s.representation = rep;
        const auto m = trainModel (sounds, s);
        CHECK (m.hasPitchCurve);
        CHECK (m.dims() == m.numFrames * (24 + 16) + (rep == Representation::ShapeLoudness ? m.numFrames : 0) + 24 + m.numFrames);
        for (int i : { 0, 7, 13 })
        {
            const auto& want = sounds[static_cast<size_t> (i)];
            const auto got = m.decode (m.soundZ (i));
            for (size_t k = 0; k < got.db.size(); k += 5)
                if (want.db[k] > m.floorDb + 1.0f)
                    REQUIRE (got.db[k] == Approx (want.db[k]).margin (0.05));
            for (size_t k = 0; k < got.noiseDb.size(); k += 3)
                if (want.noiseDb[k] > m.floorDb + 1.0f)
                    REQUIRE (got.noiseDb[k] == Approx (want.noiseDb[k]).margin (0.05));
            for (size_t h = 0; h < got.partialCents.size(); ++h)
                REQUIRE (got.partialCents[h] == Approx (want.partialCents[h]).margin (0.05));
        }
        // And the file keeps it all.
        const auto bytes = serializeModel (m);
        const auto back = deserializeModel (bytes.data(), bytes.size());
        CHECK (back.representation == rep);
        CHECK (back.numNoiseBands == 16);
        CHECK (back.hasPartials);
        std::vector<float> z (static_cast<size_t> (m.numComponents()), 0.7f);
        CHECK (back.decode (z).noiseDb == m.decode (z).noiseDb);
    }
}

TEST_CASE ("Version 1 model files still load", "[model][stage7]")
{
    // A v1 model: harmonic dB only, written the way version 0.1-0.6 wrote it.
    auto s = smallSettings();
    s.noiseBands = 0;
    s.trackPartials = false;
    s.trackPitch = false;
    std::vector<HarmonicSound> sounds;
    testgen::Options o;
    o.duration = 1.2;
    o.variations = 1;
    for (const auto& c : testgen::generateTrainingSet (o))
        sounds.push_back (analyseHarmonics (c.audio, s, c.name));
    const auto m = trainModel (sounds, s, 5);

    nlohmann::json j;
    j["format"] = "pcasynth-model";
    j["version"] = 1;
    j["title"] = "old";
    j["analysis"] = { { "midiNote", 60 }, { "duration", s.duration }, { "frameRate", 100.0 }, { "harmonics", 24 }, { "floorDb", -80.0 } };
    j["numFrames"] = m.numFrames;
    j["numHarmonics"] = m.numHarmonics;
    j["frameRate"] = m.frameRate;
    j["floorDb"] = m.floorDb;
    j["numComponents"] = m.numComponents();
    j["totalVariance"] = m.pca.totalVariance;
    j["variance"] = m.pca.variance;
    j["names"] = m.names;
    j["f0s"] = m.f0s;
    j["gainsDb"] = m.gainsDb;
    j["scores"] = m.pca.scores;
    std::string header = j.dump();
    while (header.size() % 4 != 0)
        header.push_back (' ');
    std::vector<uint8_t> bytes { 'P', 'C', 'S', 'M', 1, 0, 0, 0 };
    for (int i = 0; i < 4; ++i)
        bytes.push_back (static_cast<uint8_t> ((header.size() >> (8 * i)) & 0xff));
    bytes.insert (bytes.end(), header.begin(), header.end());
    auto put = [&bytes] (const std::vector<float>& v) {
        const auto* p = reinterpret_cast<const uint8_t*> (v.data());
        bytes.insert (bytes.end(), p, p + 4 * v.size());
    };
    put (m.pca.mean);
    put (m.pca.components);

    const auto old = deserializeModel (bytes.data(), bytes.size());
    CHECK (old.title == "old");
    CHECK (old.numNoiseBands == 0);
    CHECK_FALSE (old.hasPartials);
    CHECK_FALSE (old.pitchTracking);
    CHECK (old.representation == Representation::Decibels);
    const std::vector<float> z { 1.0f, -0.5f, 0.3f };
    CHECK (old.decode (z).db == m.decode (z).db);
}

TEST_CASE ("Pitch tracking: on for multi-pitch sets, reproduces every note, predicts unseen ones", "[model][stage7]")
{
    const auto s = stage7Settings();
    const auto sounds = multiPitchSet ({ 48, 60, 72 }, s);
    const auto tracked = trainModel (sounds, s);
    REQUIRE (tracked.pitchTracking);
    CHECK (tracked.pitchRef == Approx (60.0).margin (0.2));

    // Each training sound comes back at its own pitch.
    for (int i = 0; i < tracked.numSounds(); ++i)
    {
        const auto got = tracked.decode (tracked.soundZ (i), static_cast<float> (tracked.soundPitch (i) - tracked.pitchRef));
        CHECK (levelError (got, sounds[static_cast<size_t> (i)], tracked.floorDb) < 0.05);
    }

    // An unseen pitch (66) of a training instrument: the pitch direction brings the
    // instrument's point (its mean over the three notes) closer than ignoring pitch.
    auto off = s;
    off.pitchTracking = PitchTracking::Off;
    const auto flat = trainModel (sounds, off);
    CHECK_FALSE (flat.pitchTracking);
    const auto unseen = multiPitchSet ({ 66 }, s);
    double withPitch = 0.0, without = 0.0;
    for (size_t f = 0; f < unseen.size(); ++f)
    {
        std::vector<float> zt (static_cast<size_t> (tracked.numComponents()), 0.0f), zf (static_cast<size_t> (flat.numComponents()), 0.0f);
        for (int k = 0; k < 3; ++k) // the instrument's three training notes
        {
            const auto a = tracked.soundZ (static_cast<int> (f) * 3 + k), b = flat.soundZ (static_cast<int> (f) * 3 + k);
            for (size_t j = 0; j < zt.size(); ++j)
                zt[j] += a[j] / 3.0f;
            for (size_t j = 0; j < zf.size(); ++j)
                zf[j] += b[j] / 3.0f;
        }
        withPitch += levelError (tracked.decode (zt, tracked.pitchDelta (unseen[f].midiPitch())), unseen[f], tracked.floorDb);
        without += levelError (flat.decode (zf), unseen[f], flat.floorDb);
    }
    INFO ("mean dB error with pitch tracking " << withPitch / 4 << ", without " << without / 4);
    // Measured: about 2.9 dB against 3.3 dB (13 % lower). One shared pitch
    // direction captures the common trend (formants in Hz sliding across
    // harmonic numbers); each instrument's own pitch behaviour stays in the PCA.
    CHECK (withPitch < 0.95 * without);

    // Sets at one pitch leave it off (Auto).
    CHECK_FALSE (trainModel (smallSet(), smallSettings()).pitchTracking);
}
