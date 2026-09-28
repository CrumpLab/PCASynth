#include "pcs/Fft.h"

#include <cmath>
#include <utility>

namespace pcs {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

int nextPowerOfTwo (int x) noexcept
{
    int n = 1;
    while (n < x)
        n <<= 1;
    return n;
}

void Fft::prepare (int size)
{
    n = size;
    int bits = 0;
    while ((1 << bits) < n)
        ++bits;
    bitrev.resize (static_cast<size_t> (n));
    for (int i = 0; i < n; ++i)
    {
        int r = 0;
        for (int b = 0; b < bits; ++b)
            r |= ((i >> b) & 1) << (bits - 1 - b);
        bitrev[static_cast<size_t> (i)] = r;
    }
    cosTable.resize (static_cast<size_t> (n / 2));
    sinTable.resize (static_cast<size_t> (n / 2));
    for (int k = 0; k < n / 2; ++k)
    {
        cosTable[static_cast<size_t> (k)] = std::cos (2.0 * kPi * k / n);
        sinTable[static_cast<size_t> (k)] = std::sin (2.0 * kPi * k / n);
    }
}

void Fft::transform (double* re, double* im, bool inverse) const noexcept
{
    for (int i = 0; i < n; ++i)
    {
        const int j = bitrev[static_cast<size_t> (i)];
        if (j > i)
        {
            std::swap (re[i], re[j]);
            std::swap (im[i], im[j]);
        }
    }
    const double sign = inverse ? 1.0 : -1.0;
    for (int len = 2; len <= n; len <<= 1)
    {
        const int half = len / 2;
        const int step = n / len;
        for (int i = 0; i < n; i += len)
            for (int k = 0; k < half; ++k)
            {
                const double wr = cosTable[static_cast<size_t> (k * step)];
                const double wi = sign * sinTable[static_cast<size_t> (k * step)];
                const int a = i + k, b = a + half;
                const double xr = re[b] * wr - im[b] * wi;
                const double xi = re[b] * wi + im[b] * wr;
                re[b] = re[a] - xr;
                im[b] = im[a] - xi;
                re[a] += xr;
                im[a] += xi;
            }
    }
}

} // namespace pcs
