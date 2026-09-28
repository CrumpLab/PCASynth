#pragma once

#include "pcs/Harmonic.h"
#include "pcs/Wav.h"

#include <cmath>
#include <functional>
#include <vector>

namespace pcs::test {

// Sum of harmonics of f0 with amplitudes amp(h, t) (h 1-based), sine phases.
inline AudioBuffer harmonicTone (double f0, double seconds, const std::function<double (int, double)>& amp,
                                 int harmonics = 40, double sr = 48000.0, double delay = 0.0)
{
    AudioBuffer a;
    a.sampleRate = sr;
    a.resize (1, static_cast<int> ((seconds + delay) * sr));
    const auto start = static_cast<size_t> (delay * sr);
    for (size_t i = start; i < a.channels[0].size(); ++i)
    {
        const double t = static_cast<double> (i - start) / sr;
        double x = 0.0;
        for (int h = 1; h <= harmonics && h * f0 < 0.45 * sr; ++h)
            x += amp (h, t) * std::sin (6.28318530717958647692 * h * f0 * t + 0.3 * h * h);
        a.channels[0][i] = static_cast<float> (x);
    }
    return a;
}

inline double db (double x) { return 20.0 * std::log10 (std::max (x, 1e-12)); }

} // namespace pcs::test
