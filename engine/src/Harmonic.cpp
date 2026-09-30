#include "pcs/Harmonic.h"

#include "pcs/Fft.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace pcs {

namespace {
constexpr double kPi = 3.14159265358979323846;

// 4-term Blackman-Harris: -92 dB sidelobes, main lobe ±4 bins.
std::vector<double> blackmanHarris (int n)
{
    std::vector<double> w (static_cast<size_t> (n));
    for (int i = 0; i < n; ++i)
    {
        const double x = 2.0 * kPi * i / (n - 1);
        w[static_cast<size_t> (i)] = 0.35875 - 0.48829 * std::cos (x) + 0.14128 * std::cos (2 * x) - 0.01168 * std::cos (3 * x);
    }
    return w;
}

double toDb (double x) noexcept { return 20.0 * std::log10 (std::max (x, 1e-12)); }
} // namespace

double midiToHz (double note) noexcept { return 440.0 * std::pow (2.0, (note - 69.0) / 12.0); }

std::vector<double> monoMix (const AudioBuffer& audio)
{
    std::vector<double> mono (static_cast<size_t> (audio.numSamples()), 0.0);
    if (audio.numChannels() == 0)
        return mono;
    for (const auto& ch : audio.channels)
        for (size_t i = 0; i < mono.size(); ++i)
            mono[i] += ch[i];
    for (auto& x : mono)
        x /= audio.numChannels();
    return mono;
}

namespace {
// Long-term magnitude spectrum of up to one second of `mono` from `start`.
struct LongSpectrum
{
    std::vector<double> mag;
    int n = 0;
    double sampleRate = 48000.0;

    bool valid() const noexcept { return ! mag.empty(); }
    double at (double hz) const noexcept // max over the nearest 3 bins
    {
        const auto k = static_cast<long> (std::lround (hz * n / sampleRate));
        double m = 0.0;
        for (long j = k - 1; j <= k + 1; ++j)
            if (j > 0 && j < static_cast<long> (mag.size()))
                m = std::max (m, mag[static_cast<size_t> (j)]);
        return m;
    }
};

LongSpectrum longSpectrum (const std::vector<double>& mono, double sampleRate, size_t start)
{
    LongSpectrum s;
    s.sampleRate = sampleRate;
    const size_t available = start < mono.size() ? mono.size() - start : 0;
    const auto len = static_cast<int> (std::min<size_t> (available, static_cast<size_t> (sampleRate)));
    if (len < 64)
        return s;
    s.n = nextPowerOfTwo (len) * 2;
    Fft fft (s.n);
    std::vector<double> re (static_cast<size_t> (s.n), 0.0), im (static_cast<size_t> (s.n), 0.0);
    for (int i = 0; i < len; ++i)
        re[static_cast<size_t> (i)] = mono[start + static_cast<size_t> (i)] * (0.5 - 0.5 * std::cos (2.0 * kPi * i / (len - 1)));
    fft.forward (re.data(), im.data());
    s.mag.resize (static_cast<size_t> (s.n / 2));
    for (size_t k = 0; k < s.mag.size(); ++k)
        s.mag[k] = std::hypot (re[k], im[k]);
    return s;
}
} // namespace

namespace {
// YIN (de Cheveigné & Kawahara 2002) on one window: the period (samples) of
// the first dip of the cumulative-mean-normalised difference below the
// threshold, refined by parabolic interpolation; 0 if the window is unvoiced.
double yinPeriod (const double* x, int window, int minLag, int maxLag)
{
    std::vector<double> d (static_cast<size_t> (maxLag + 2), 0.0);
    for (int tau = 1; tau <= maxLag + 1; ++tau)
    {
        double sum = 0.0;
        for (int i = 0; i < window; ++i)
        {
            const double diff = x[i] - x[i + tau];
            sum += diff * diff;
        }
        d[static_cast<size_t> (tau)] = sum;
    }
    // Cumulative mean normalisation.
    double running = 0.0;
    std::vector<double> dn (d.size(), 1.0);
    for (int tau = 1; tau <= maxLag + 1; ++tau)
    {
        running += d[static_cast<size_t> (tau)];
        dn[static_cast<size_t> (tau)] = running > 0.0 ? d[static_cast<size_t> (tau)] * tau / running : 1.0;
    }
    constexpr double threshold = 0.15;
    int best = -1;
    for (int tau = std::max (2, minLag); tau <= maxLag; ++tau)
        if (dn[static_cast<size_t> (tau)] < threshold)
        {
            while (tau + 1 <= maxLag && dn[static_cast<size_t> (tau + 1)] < dn[static_cast<size_t> (tau)])
                ++tau;
            best = tau;
            break;
        }
    if (best < 0)
    {
        // No clear dip: the global minimum, if it is at least a weak one.
        best = std::max (2, minLag);
        for (int tau = best; tau <= maxLag; ++tau)
            if (dn[static_cast<size_t> (tau)] < dn[static_cast<size_t> (best)])
                best = tau;
        if (dn[static_cast<size_t> (best)] > 0.5)
            return 0.0;
    }
    const double a = dn[static_cast<size_t> (best - 1)], b = dn[static_cast<size_t> (best)], c = dn[static_cast<size_t> (best + 1)];
    const double denom = a - 2.0 * b + c;
    return best + (denom > 0.0 ? std::clamp (0.5 * (a - c) / denom, -0.5, 0.5) : 0.0);
}
} // namespace

double detectPitch (const std::vector<double>& mono, double sampleRate, size_t start, double lowNote, double highNote)
{
    const int minLag = std::max (2, static_cast<int> (std::floor (sampleRate / midiToHz (highNote))));
    const int maxLag = static_cast<int> (std::ceil (sampleRate / midiToHz (lowNote)));
    const int window = std::max (maxLag, static_cast<int> (0.03 * sampleRate));
    const auto need = static_cast<size_t> (window + maxLag + 2);

    // Median over windows spread across the first second after `start`.
    std::vector<double> periods;
    for (int k = 0; k < 8; ++k)
    {
        const auto at = start + static_cast<size_t> (k * 0.12 * sampleRate);
        if (at + need > mono.size())
            break;
        if (const double p = yinPeriod (mono.data() + at, window, minLag, maxLag); p > 0.0)
            periods.push_back (p);
    }
    if (periods.empty())
        return 0.0;
    std::nth_element (periods.begin(), periods.begin() + static_cast<long> (periods.size() / 2), periods.end());
    return sampleRate / periods[periods.size() / 2];
}

double estimateF0 (const std::vector<double>& mono, double sampleRate, size_t start, double nominalHz,
                   double searchCents, int harmonics)
{
    const auto spec = longSpectrum (mono, sampleRate, start);
    if (! spec.valid())
        return nominalHz;
    auto magAt = [&spec] (double hz) { return spec.at (hz); };

    double best = nominalHz, bestScore = -1.0;
    for (double c = -searchCents; c <= searchCents; c += 0.5)
    {
        const double f = nominalHz * std::pow (2.0, c / 1200.0);
        double score = 0.0;
        for (int h = 1; h <= harmonics && h * f < 0.45 * sampleRate; ++h)
            score += magAt (h * f);
        if (score > bestScore)
        {
            bestScore = score;
            best = f;
        }
    }
    return best;
}

HarmonicSound analyseHarmonics (const AudioBuffer& audio, const AnalysisSettings& s, const std::string& name)
{
    const double sr = audio.sampleRate;
    const auto mono = monoMix (audio);

    double peak = 0.0;
    for (double x : mono)
        peak = std::max (peak, std::abs (x));
    if (peak <= 0.0)
        throw std::runtime_error ((name.empty() ? std::string ("sound") : name) + " is silent");

    size_t start = 0;
    if (s.trimOnset)
    {
        const double threshold = peak * std::pow (10.0, s.onsetThresholdDb / 20.0);
        while (start < mono.size() && std::abs (mono[start]) < threshold)
            ++start;
        start -= std::min (start, static_cast<size_t> (0.005 * sr)); // keep 5 ms before the onset
    }

    HarmonicSound out;
    out.name = name;
    out.frameRate = s.frameRate;
    out.numHarmonics = s.harmonics;
    out.numFrames = std::max (1, static_cast<int> (std::lround (s.duration * s.frameRate)));
    double nominal = midiToHz (s.midiNote);
    if (s.autoPitch)
    {
        // Skip the first 50 ms (attack noise) when there is enough sound after it.
        const auto skip = static_cast<size_t> (0.05 * sr);
        const double detected = detectPitch (mono, sr, start + skip < mono.size() / 2 ? start + skip : start);
        if (detected <= 0.0)
            throw std::runtime_error ((name.empty() ? std::string ("sound") : name) + ": no pitch found");
        nominal = detected;
    }
    out.f0 = estimateF0 (mono, sr, start, nominal, s.autoPitch ? 30.0 : s.tuneSearchCents, s.harmonics);

    const int winLen = std::max (64, static_cast<int> (std::lround (s.periodsPerWindow * sr / out.f0)));
    const int n = nextPowerOfTwo (winLen) * 2;
    const auto window = blackmanHarris (winLen);
    double windowSum = 0.0;
    for (double w : window)
        windowSum += w;

    Fft fft (n);
    std::vector<double> re (static_cast<size_t> (n)), im (static_cast<size_t> (n));
    std::vector<double> magDb (static_cast<size_t> (n / 2 + 1));
    out.db.assign (static_cast<size_t> (out.numFrames * out.numHarmonics), static_cast<float> (s.floorDb));

    const double hop = sr / s.frameRate;
    const double binHz = sr / n;
    for (int t = 0; t < out.numFrames; ++t)
    {
        const auto first = static_cast<long> (start) + std::lround (t * hop) - winLen / 2;
        std::fill (re.begin(), re.end(), 0.0);
        std::fill (im.begin(), im.end(), 0.0);
        for (int i = 0; i < winLen; ++i)
        {
            const long j = first + i;
            if (j >= 0 && j < static_cast<long> (mono.size()))
                re[static_cast<size_t> (i)] = mono[static_cast<size_t> (j)] * window[static_cast<size_t> (i)];
        }
        fft.forward (re.data(), im.data());
        for (size_t k = 0; k < magDb.size(); ++k)
            magDb[k] = toDb (std::hypot (re[k], im[k]) * 2.0 / windowSum);

        for (int h = 0; h < out.numHarmonics; ++h)
        {
            const double f = (h + 1) * out.f0;
            if (f > 0.48 * sr)
                break;
            const long lo = std::max (1L, std::lround ((f - 0.35 * out.f0) / binHz));
            const long hi = std::min (static_cast<long> (magDb.size()) - 2, std::lround ((f + 0.35 * out.f0) / binHz));
            long k = lo;
            for (long j = lo; j <= hi; ++j)
                if (magDb[static_cast<size_t> (j)] > magDb[static_cast<size_t> (k)])
                    k = j;
            // Parabolic interpolation of the peak level.
            const double a = magDb[static_cast<size_t> (k - 1)], b = magDb[static_cast<size_t> (k)], c = magDb[static_cast<size_t> (k + 1)];
            const double denom = a - 2.0 * b + c;
            const double p = denom < 0.0 ? std::clamp (0.5 * (a - c) / denom, -0.5, 0.5) : 0.0;
            out.db[static_cast<size_t> (t * out.numHarmonics + h)] = static_cast<float> (b - 0.25 * (a - c) * p);
        }
    }

    // Normalise so the loudest frame has unit amplitude-energy, then clamp to the floor.
    if (s.normalizeLoudness)
    {
        double maxEnergy = 0.0;
        for (int t = 0; t < out.numFrames; ++t)
        {
            double e = 0.0;
            for (int h = 0; h < out.numHarmonics; ++h)
                e += std::pow (10.0, out.at (t, h) / 10.0);
            maxEnergy = std::max (maxEnergy, e);
        }
        out.gainDb = maxEnergy > 0.0 ? -10.0 * std::log10 (maxEnergy) : 0.0;
    }
    for (auto& v : out.db)
        v = static_cast<float> (std::max (s.floorDb, v + out.gainDb));
    return out;
}

} // namespace pcs
