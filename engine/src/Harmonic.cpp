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

double noiseBandEdge (int b, int bands) noexcept
{
    return 0.5 * std::pow (192.0, static_cast<double> (b) / std::max (1, bands));
}

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

std::vector<double> trackPartials (const std::vector<double>& mono, double sampleRate, size_t start, double f0, int count)
{
    std::vector<double> f (static_cast<size_t> (std::max (0, count)));
    for (int h = 0; h < count; ++h)
        f[static_cast<size_t> (h)] = (h + 1) * f0;
    const auto spec = longSpectrum (mono, sampleRate, start);
    if (! spec.valid() || count < 1)
        return f;
    const double bin = sampleRate / spec.n;
    double maxMag = 0.0;
    for (double m : spec.mag)
        maxMag = std::max (maxMag, m);

    // The strongest bin within [lo, hi], if it is a clear peak (12 dB above the
    // window's median, and within 80 dB of the loudest), refined parabolically.
    std::vector<double> window;
    auto peakIn = [&] (double lo, double hi, double& freq) {
        const long a = std::max (1L, static_cast<long> (std::ceil (lo / bin)));
        const long b = std::min (static_cast<long> (spec.mag.size()) - 2, static_cast<long> (std::floor (hi / bin)));
        if (b <= a)
            return false;
        long k = a;
        window.clear();
        for (long j = a; j <= b; ++j)
        {
            window.push_back (spec.mag[static_cast<size_t> (j)]);
            if (spec.mag[static_cast<size_t> (j)] > spec.mag[static_cast<size_t> (k)])
                k = j;
        }
        std::nth_element (window.begin(), window.begin() + static_cast<long> (window.size() / 2), window.end());
        const double peak = spec.mag[static_cast<size_t> (k)];
        if (peak < 4.0 * window[window.size() / 2] || peak < 1e-4 * maxMag)
            return false;
        const double la = std::log (spec.mag[static_cast<size_t> (k - 1)] + 1e-30), lb = std::log (peak + 1e-30),
                     lc = std::log (spec.mag[static_cast<size_t> (k + 1)] + 1e-30);
        const double denom = la - 2.0 * lb + lc;
        freq = (static_cast<double> (k) + (denom < 0.0 ? std::clamp (0.5 * (la - lc) / denom, -0.5, 0.5) : 0.0)) * bin;
        return true;
    };

    double found = f0;
    if (peakIn (0.8 * f0, 1.2 * f0, found))
        f[0] = found;
    for (int h = 1; h < count; ++h)
    {
        const double spacing = std::clamp (h >= 2 ? f[static_cast<size_t> (h - 1)] - f[static_cast<size_t> (h - 2)] : f[0], 0.9 * f0, 1.6 * f0);
        const double predicted = f[static_cast<size_t> (h - 1)] + spacing;
        f[static_cast<size_t> (h)] = predicted;
        if (predicted > 0.45 * sampleRate)
            continue;
        if (peakIn (predicted - 0.3 * spacing, predicted + 0.3 * spacing, found))
            f[static_cast<size_t> (h)] = found;
    }
    return f;
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

    // Where each partial is (stretched partials are followed), and how far from an exact harmonic.
    std::vector<double> partial (static_cast<size_t> (s.harmonics));
    if (s.trackPartials)
    {
        const auto skip = static_cast<size_t> (0.02 * sr);
        partial = trackPartials (mono, sr, start + skip < mono.size() / 2 ? start + skip : start, out.f0, s.harmonics);
        // With stretched partials the harmonic-sum estimate is pulled sharp by the
        // upper partials; the fundamental is partial 1 itself.
        if (std::abs (partial[0] / out.f0 - 1.0) < 0.03)
            out.f0 = partial[0];
        out.partialCents.resize (static_cast<size_t> (s.harmonics));
        for (int h = 0; h < s.harmonics; ++h)
            out.partialCents[static_cast<size_t> (h)] = static_cast<float> (
                std::clamp (1200.0 * std::log2 (partial[static_cast<size_t> (h)] / ((h + 1) * out.f0)), -600.0, 1200.0));
    }
    else
        for (int h = 0; h < s.harmonics; ++h)
            partial[static_cast<size_t> (h)] = (h + 1) * out.f0;
    auto spacingAt = [&] (int h) {
        return h == 0 ? partial[0] : partial[static_cast<size_t> (h)] - partial[static_cast<size_t> (h - 1)];
    };

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
    out.numNoiseBands = std::max (0, s.noiseBands);
    out.noiseDb.assign (static_cast<size_t> (out.numFrames * out.numNoiseBands), -300.0f);

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
            const double f = partial[static_cast<size_t> (h)];
            if (f > 0.48 * sr)
                break;
            const double half = 0.35 * spacingAt (h);
            const long lo = std::max (1L, std::lround ((f - half) / binHz));
            const long hi = std::min (static_cast<long> (magDb.size()) - 2, std::lround ((f + half) / binHz));
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

    // Residual noise, from a window twice as long (16 periods): the partials'
    // main lobes end at ±f0/4, so bins further than 0.32 of the local spacing
    // from every partial ("quiet" bins) hold noise and no measurable leakage.
    if (out.numNoiseBands > 0)
    {
        const int nLen = std::max (128, 2 * winLen);
        const int nn = nextPowerOfTwo (nLen) * 2;
        const auto nWindow = blackmanHarris (nLen);
        double windowSq = 0.0;
        for (double w : nWindow)
            windowSq += w * w;
        Fft nfft (nn);
        std::vector<double> nre (static_cast<size_t> (nn)), nim (static_cast<size_t> (nn));
        const double nBin = sr / nn;
        std::vector<char> measured (static_cast<size_t> (out.numNoiseBands), 0);
        std::vector<double> bandPowers;

        std::vector<char> quiet (static_cast<size_t> (nn / 2 + 1), 0);
        const double lastEdge = partial.back() + 0.32 * spacingAt (s.harmonics - 1);
        for (size_t k = 1; k < quiet.size(); ++k)
        {
            const double f = static_cast<double> (k) * nBin;
            if (f > lastEdge)
            {
                quiet[k] = 1;
                continue;
            }
            const auto it = std::lower_bound (partial.begin(), partial.end(), f);
            const auto i = static_cast<size_t> (it - partial.begin());
            // The gap f lies in: below the first partial, or between partials i-1 and i.
            const double below = i == 0 ? 0.0 : partial[i - 1];
            const double above = i < partial.size() ? partial[i] : 1e30;
            const double gap = i == 0 ? partial[0] : (i < partial.size() ? above - below : spacingAt (s.harmonics - 1));
            quiet[k] = std::min (f - below, above - f) >= 0.32 * gap ? 1 : 0;
        }

        for (int t = 0; t < out.numFrames; ++t)
        {
            const auto first = static_cast<long> (start) + std::lround (t * hop) - nLen / 2;
            std::fill (nre.begin(), nre.end(), 0.0);
            std::fill (nim.begin(), nim.end(), 0.0);
            for (int i = 0; i < nLen; ++i)
            {
                const long j = first + i;
                if (j >= 0 && j < static_cast<long> (mono.size()))
                    nre[static_cast<size_t> (i)] = mono[static_cast<size_t> (j)] * nWindow[static_cast<size_t> (i)];
            }
            nfft.forward (nre.data(), nim.data());
            for (int b = 0; b < out.numNoiseBands; ++b)
            {
                const double lo = noiseBandEdge (b, out.numNoiseBands) * out.f0, hi = noiseBandEdge (b + 1, out.numNoiseBands) * out.f0;
                if (lo >= 0.48 * sr)
                    break;
                const long k0 = std::max (1L, static_cast<long> (std::ceil (lo / nBin)));
                const long k1 = std::min (static_cast<long> (quiet.size()) - 1, static_cast<long> (std::floor (std::min (hi, 0.48 * sr) / nBin)));
                bandPowers.clear();
                for (long k = k0; k <= k1; ++k)
                    if (quiet[static_cast<size_t> (k)])
                        bandPowers.push_back (nre[static_cast<size_t> (k)] * nre[static_cast<size_t> (k)] + nim[static_cast<size_t> (k)] * nim[static_cast<size_t> (k)]);
                if (bandPowers.empty() || k1 < k0)
                    continue;
                // Power per bin, robust to bins that partials leak into (vibrato
                // smears them; those are the loud ones): noise bins' powers are
                // exponentially distributed, so the median / ln 2 estimates the
                // mean; bins above 3 × that are dropped and the rest averaged
                // (for pure noise that keeps 0.7906 of the mean, corrected here).
                const auto mid = bandPowers.begin() + static_cast<long> (bandPowers.size() / 2);
                std::nth_element (bandPowers.begin(), mid, bandPowers.end());
                const double limit = 3.0 * (*mid / 0.69314718);
                double kept = 0.0;
                long keptCount = 0;
                for (double pw : bandPowers)
                    if (pw <= limit)
                    {
                        kept += pw;
                        ++keptCount;
                    }
                const double perBin = keptCount > 0 ? kept / static_cast<double> (keptCount) / 0.79061 : *mid / 0.69314718;
                // -> RMS of noise filling the band.
                const double bandBins = static_cast<double> (k1 - k0 + 1);
                const double rms = std::sqrt (perBin * bandBins * 2.0 / (static_cast<double> (nn) * windowSq));
                out.noiseDb[static_cast<size_t> (t * out.numNoiseBands + b)] = static_cast<float> (toDb (rms));
                measured[static_cast<size_t> (b)] = 1;
            }
            // A band with no quiet bins (inside the fundamental's main lobe): the
            // noise density of its nearest measured neighbours, scaled to its width.
            float* row = out.noiseDb.data() + static_cast<size_t> (t * out.numNoiseBands);
            for (int b = 0; b < out.numNoiseBands; ++b)
            {
                if (measured[static_cast<size_t> (b)] || noiseBandEdge (b, out.numNoiseBands) * out.f0 >= 0.48 * sr)
                    continue;
                double density = 0.0;
                int sources = 0;
                for (int dir : { -1, 1 })
                    for (int o = b + dir; o >= 0 && o < out.numNoiseBands; o += dir)
                        if (measured[static_cast<size_t> (o)])
                        {
                            const double width = noiseBandEdge (o + 1, out.numNoiseBands) - noiseBandEdge (o, out.numNoiseBands);
                            density += std::pow (10.0, row[o] / 10.0) / width;
                            ++sources;
                            break;
                        }
                if (sources > 0)
                {
                    const double width = noiseBandEdge (b + 1, out.numNoiseBands) - noiseBandEdge (b, out.numNoiseBands);
                    row[b] = static_cast<float> (10.0 * std::log10 (std::max (1e-30, density / sources * width)));
                }
            }
            std::fill (measured.begin(), measured.end(), 0);
        }
    }

    // Normalise so the loudest frame has unit amplitude-energy, then clamp to the floor.
    // (Energy counts harmonic peak amplitudes squared and noise as 2 × RMS²,
    // the same units.)
    if (s.normalizeLoudness)
    {
        double maxEnergy = 0.0;
        for (int t = 0; t < out.numFrames; ++t)
        {
            double e = 0.0;
            for (int h = 0; h < out.numHarmonics; ++h)
                e += std::pow (10.0, out.at (t, h) / 10.0);
            for (int b = 0; b < out.numNoiseBands; ++b)
                e += 2.0 * std::pow (10.0, out.noiseAt (t, b) / 10.0);
            maxEnergy = std::max (maxEnergy, e);
        }
        out.gainDb = maxEnergy > 0.0 ? -10.0 * std::log10 (maxEnergy) : 0.0;
    }
    for (auto& v : out.db)
        v = static_cast<float> (std::max (s.floorDb, v + out.gainDb));
    for (auto& v : out.noiseDb)
        v = static_cast<float> (std::max (s.floorDb, v + out.gainDb));
    return out;
}

} // namespace pcs
