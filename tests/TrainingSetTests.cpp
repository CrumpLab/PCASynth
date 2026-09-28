#include "TrainingSet.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace pcs;

TEST_CASE ("Training set is deterministic and complete", "[testgen]")
{
    testgen::Options o;
    o.duration = 0.5;
    const auto a = testgen::generateTrainingSet (o);
    const auto b = testgen::generateTrainingSet (o);
    REQUIRE (a.size() == testgen::families().size() * 6);
    for (size_t i = 0; i < a.size(); ++i)
    {
        CHECK (a[i].name == b[i].name);
        CHECK (a[i].audio.channels == b[i].audio.channels);
        float peak = 0.0f;
        for (float x : a[i].audio.channels[0])
        {
            REQUIRE (std::isfinite (x));
            peak = std::max (peak, std::abs (x));
        }
        INFO (a[i].name);
        CHECK (std::abs (peak - 0.5f) < 1e-4f);
    }
    o.seed = 2;
    CHECK (testgen::generateTrainingSet (o)[0].audio.channels != a[0].audio.channels);
}
