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
