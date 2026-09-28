#include "pcs/Pca.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace pcs {

void symmetricEigen (std::vector<double>& a, int n, std::vector<double>& values, std::vector<double>& vectors)
{
    auto at = [n] (std::vector<double>& m, int r, int c) -> double& { return m[static_cast<size_t> (r * n + c)]; };
    std::vector<double> v (static_cast<size_t> (n * n), 0.0);
    for (int i = 0; i < n; ++i)
        at (v, i, i) = 1.0;

    for (int sweep = 0; sweep < 100; ++sweep)
    {
        double off = 0.0, diag = 0.0;
        for (int p = 0; p < n; ++p)
        {
            diag += at (a, p, p) * at (a, p, p);
            for (int q = p + 1; q < n; ++q)
                off += at (a, p, q) * at (a, p, q);
        }
        if (off <= 1e-30 * std::max (diag, 1e-300))
            break;

        for (int p = 0; p < n - 1; ++p)
            for (int q = p + 1; q < n; ++q)
            {
                const double apq = at (a, p, q);
                if (std::abs (apq) < 1e-300)
                    continue;
                const double theta = (at (a, q, q) - at (a, p, p)) / (2.0 * apq);
                const double t = (theta >= 0 ? 1.0 : -1.0) / (std::abs (theta) + std::sqrt (theta * theta + 1.0));
                const double c = 1.0 / std::sqrt (t * t + 1.0), s = t * c;
                for (int k = 0; k < n; ++k)
                {
                    const double akp = at (a, k, p), akq = at (a, k, q);
                    at (a, k, p) = c * akp - s * akq;
                    at (a, k, q) = s * akp + c * akq;
                }
                for (int k = 0; k < n; ++k)
                {
                    const double apk = at (a, p, k), aqk = at (a, q, k);
                    at (a, p, k) = c * apk - s * aqk;
                    at (a, q, k) = s * apk + c * aqk;
                }
                for (int k = 0; k < n; ++k)
                {
                    const double vkp = at (v, k, p), vkq = at (v, k, q);
                    at (v, k, p) = c * vkp - s * vkq;
                    at (v, k, q) = s * vkp + c * vkq;
                }
            }
    }

    std::vector<int> order (static_cast<size_t> (n));
    std::iota (order.begin(), order.end(), 0);
    std::sort (order.begin(), order.end(), [&] (int x, int y) { return at (a, x, x) > at (a, y, y); });
    values.resize (static_cast<size_t> (n));
    vectors.assign (static_cast<size_t> (n * n), 0.0);
    for (int j = 0; j < n; ++j)
    {
        values[static_cast<size_t> (j)] = at (a, order[static_cast<size_t> (j)], order[static_cast<size_t> (j)]);
        for (int i = 0; i < n; ++i)
            at (vectors, i, j) = at (v, i, order[static_cast<size_t> (j)]);
    }
}

PcaResult computePca (const std::vector<std::vector<float>>& rows, int maxComponents)
{
    const int n = static_cast<int> (rows.size());
    if (n < 2)
        throw std::invalid_argument ("PCA needs at least two sounds");
    const int d = static_cast<int> (rows[0].size());
    for (const auto& r : rows)
        if (static_cast<int> (r.size()) != d)
            throw std::invalid_argument ("PCA rows differ in length");

    PcaResult out;
    out.numRows = n;
    out.dims = d;

    std::vector<double> mean (static_cast<size_t> (d), 0.0);
    for (const auto& r : rows)
        for (int i = 0; i < d; ++i)
            mean[static_cast<size_t> (i)] += r[static_cast<size_t> (i)];
    for (auto& m : mean)
        m /= n;
    out.mean.assign (mean.begin(), mean.end());

    // Centred data (double) and its Gram matrix.
    std::vector<std::vector<double>> x (static_cast<size_t> (n), std::vector<double> (static_cast<size_t> (d)));
    for (int r = 0; r < n; ++r)
        for (int i = 0; i < d; ++i)
            x[static_cast<size_t> (r)][static_cast<size_t> (i)] = rows[static_cast<size_t> (r)][static_cast<size_t> (i)] - mean[static_cast<size_t> (i)];
    std::vector<double> gram (static_cast<size_t> (n * n));
    for (int a = 0; a < n; ++a)
        for (int b = a; b < n; ++b)
        {
            double dot = 0.0;
            const auto& xa = x[static_cast<size_t> (a)];
            const auto& xb = x[static_cast<size_t> (b)];
            for (int i = 0; i < d; ++i)
                dot += xa[static_cast<size_t> (i)] * xb[static_cast<size_t> (i)];
            gram[static_cast<size_t> (a * n + b)] = gram[static_cast<size_t> (b * n + a)] = dot;
        }

    double trace = 0.0;
    for (int i = 0; i < n; ++i)
        trace += gram[static_cast<size_t> (i * n + i)];
    out.totalVariance = trace / (n - 1);

    std::vector<double> values, vectors;
    symmetricEigen (gram, n, values, vectors);

    int k = std::min ({ maxComponents, n - 1, d });
    while (k > 0 && values[static_cast<size_t> (k - 1)] <= 1e-9 * std::max (trace, 1e-300))
        --k;
    out.numComponents = k;
    out.components.assign (static_cast<size_t> (k) * static_cast<size_t> (d), 0.0f);
    out.variance.resize (static_cast<size_t> (k));
    out.scores.assign (static_cast<size_t> (n * k), 0.0);

    for (int j = 0; j < k; ++j)
    {
        const double lambda = values[static_cast<size_t> (j)];
        out.variance[static_cast<size_t> (j)] = lambda / (n - 1);
        // Component = X^T u / sqrt(lambda); scores = sqrt(lambda) u.
        std::vector<double> comp (static_cast<size_t> (d), 0.0);
        for (int r = 0; r < n; ++r)
        {
            const double u = vectors[static_cast<size_t> (r * n + j)];
            const auto& xr = x[static_cast<size_t> (r)];
            for (int i = 0; i < d; ++i)
                comp[static_cast<size_t> (i)] += u * xr[static_cast<size_t> (i)];
        }
        const double inv = 1.0 / std::sqrt (lambda);
        double sum = 0.0;
        for (auto& c : comp)
        {
            c *= inv;
            sum += c;
        }
        const double sign = sum < 0.0 ? -1.0 : 1.0;
        for (int i = 0; i < d; ++i)
            out.components[static_cast<size_t> (j) * static_cast<size_t> (d) + static_cast<size_t> (i)] = static_cast<float> (sign * comp[static_cast<size_t> (i)]);
        for (int r = 0; r < n; ++r)
            out.scores[static_cast<size_t> (r * k + j)] = sign * std::sqrt (lambda) * vectors[static_cast<size_t> (r * n + j)];
    }
    return out;
}

} // namespace pcs
