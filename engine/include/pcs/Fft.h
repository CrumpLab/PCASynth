#pragma once

#include <vector>

namespace pcs {

// In-place iterative radix-2 complex FFT (power-of-two sizes), double
// precision. Used for offline analysis, not on the audio thread.
class Fft
{
public:
    explicit Fft (int size = 0) { if (size > 0) prepare (size); }
    void prepare (int size);
    int size() const noexcept { return n; }
    void forward (double* re, double* im) const noexcept { transform (re, im, false); }
    void inverse (double* re, double* im) const noexcept { transform (re, im, true); } // unscaled

private:
    void transform (double* re, double* im, bool inverse) const noexcept;
    int n = 0;
    std::vector<int> bitrev;
    std::vector<double> cosTable, sinTable; // n/2 entries
};

int nextPowerOfTwo (int x) noexcept;

} // namespace pcs
