#pragma once

#include <vector>

namespace pcs {

// Principal components of N row vectors of equal length D. Computed through
// the N×N Gram matrix, so it stays cheap when D (frames × harmonics) is far
// larger than the number of training sounds.
struct PcaResult
{
    int numRows = 0;
    int dims = 0;
    int numComponents = 0;
    std::vector<float> mean;          // D
    std::vector<float> components;    // numComponents × D, orthonormal rows
    std::vector<double> variance;     // variance of the scores on each component (descending)
    std::vector<double> scores;       // numRows × numComponents
    double totalVariance = 0.0;       // sum over all components, including dropped ones

    const float* component (int j) const { return components.data() + static_cast<size_t> (j) * static_cast<size_t> (dims); }
    double score (int row, int j) const { return scores[static_cast<size_t> (row * numComponents + j)]; }
};

// Keeps at most `maxComponents` (and never more than N - 1, nor components
// with negligible variance). Each component's sign is chosen so that its
// elements sum to a positive number: a positive score means "more energy
// overall" along that component. Throws std::invalid_argument for fewer than
// two rows or ragged rows.
PcaResult computePca (const std::vector<std::vector<float>>& rows, int maxComponents);

// Eigen-decomposition of a symmetric n×n matrix (row-major, destroyed) by
// cyclic Jacobi rotations. Eigenvalues come back in descending order;
// eigenvectors are the columns of `vectors` (row-major n×n).
void symmetricEigen (std::vector<double>& matrix, int n, std::vector<double>& values, std::vector<double>& vectors);

} // namespace pcs
