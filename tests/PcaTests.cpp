#include "pcs/Pca.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>

using namespace pcs;
using Catch::Approx;

namespace {
std::vector<std::vector<float>> randomRows (int n, int d, uint32_t seed)
{
    std::vector<std::vector<float>> rows (static_cast<size_t> (n), std::vector<float> (static_cast<size_t> (d)));
    for (auto& r : rows)
        for (int i = 0; i < d; ++i)
        {
            seed = seed * 1664525u + 1013904223u;
            // Decreasing spread per dimension so the spectrum is well separated.
            r[static_cast<size_t> (i)] = static_cast<float> ((seed / 4294967296.0 - 0.5) * 10.0 / (1 + i % 7));
        }
    return rows;
}
} // namespace

TEST_CASE ("Jacobi eigen-decomposition of a known matrix", "[pca]")
{
    std::vector<double> m { 2, -1, 0, -1, 2, -1, 0, -1, 2 };
    std::vector<double> values, vectors;
    symmetricEigen (m, 3, values, vectors);
    CHECK (values[0] == Approx (2.0 + std::sqrt (2.0)));
    CHECK (values[1] == Approx (2.0));
    CHECK (values[2] == Approx (2.0 - std::sqrt (2.0)));
    // First eigenvector ∝ (1, -√2, 1).
    CHECK (std::abs (vectors[0 * 3 + 0]) == Approx (0.5));
    CHECK (std::abs (vectors[1 * 3 + 0]) == Approx (std::sqrt (0.5)));
}

TEST_CASE ("PCA: orthonormal components, ordered variance, exact reconstruction", "[pca]")
{
    const int n = 12, d = 300;
    const auto rows = randomRows (n, d, 7);
    const auto p = computePca (rows, 32);
    REQUIRE (p.numComponents == n - 1);

    for (int a = 0; a < p.numComponents; ++a)
        for (int b = 0; b < p.numComponents; ++b)
        {
            double dot = 0.0;
            for (int i = 0; i < d; ++i)
                dot += static_cast<double> (p.component (a)[i]) * p.component (b)[i];
            CHECK (dot == Approx (a == b ? 1.0 : 0.0).margin (1e-5));
        }
    double sum = 0.0;
    for (int j = 0; j < p.numComponents; ++j)
    {
        if (j > 0)
            CHECK (p.variance[static_cast<size_t> (j)] <= p.variance[static_cast<size_t> (j - 1)]);
        sum += p.variance[static_cast<size_t> (j)];
        double s = 0.0;
        for (int i = 0; i < d; ++i)
            s += p.component (j)[i];
        CHECK (s >= 0.0); // sign convention
    }
    CHECK (sum == Approx (p.totalVariance).epsilon (1e-6));

    for (int r = 0; r < n; ++r)
        for (int i = 0; i < d; i += 17)
        {
            double x = p.mean[static_cast<size_t> (i)];
            for (int j = 0; j < p.numComponents; ++j)
                x += p.score (r, j) * p.component (j)[i];
            CHECK (x == Approx (rows[static_cast<size_t> (r)][static_cast<size_t> (i)]).margin (1e-3));
        }
}

TEST_CASE ("PCA keeps at most the requested components and drops empty ones", "[pca]")
{
    auto rows = randomRows (10, 50, 3);
    CHECK (computePca (rows, 4).numComponents == 4);
    // Rank-1 data: every row is a multiple of the same vector.
    for (size_t r = 0; r < rows.size(); ++r)
        for (size_t i = 0; i < rows[r].size(); ++i)
            rows[r][i] = static_cast<float> (r) * rows[0][i] + 1.0f;
    CHECK (computePca (rows, 32).numComponents == 1);
    CHECK_THROWS (computePca ({ { 1.0f, 2.0f } }, 4));
    CHECK_THROWS (computePca ({ { 1.0f, 2.0f }, { 1.0f } }, 4));
}
