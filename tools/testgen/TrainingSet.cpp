#include "TrainingSet.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>

namespace pcs::testgen {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

// xorshift32: identical on every platform.
struct Rng
{
    uint32_t s;
    explicit Rng (uint32_t seed) : s (seed ? seed : 0x9e3779b9u) {}
    uint32_t next()
    {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    double uniform() { return next() / 4294967296.0; }                    // [0, 1)
    double range (double lo, double hi) { return lo + (hi - lo) * uniform(); }
    double bipolar() { return 2.0 * uniform() - 1.0; }
};

double hz (int note) { return 440.0 * std::pow (2.0, (note - 69) / 12.0); }

// Attack-sustain-release gain: linear attack, exponential release starting at `releaseAt`.
double asr (double t, double attack, double releaseAt, double release)
{
    double g = attack > 0.0 ? std::min (1.0, t / attack) : 1.0;
    if (t > releaseAt)
        g *= std::exp (-6.9 * (t - releaseAt) / release);
    return g;
}

// Level of harmonic h (1-based) at time t, and the pitch offset in cents at t.
struct AdditiveSpec
{
    std::function<double (int h, double t)> amp;
    std::function<double (double t)> cents = [] (double) { return 0.0; };
    double inharmonicity = 0.0; // f_h = h f0 sqrt(1 + B h^2)
    int maxHarmonics = 96;
};

std::vector<float> additive (const AdditiveSpec& spec, double f0, const Options& o, Rng& rng)
{
    const auto n = static_cast<size_t> (o.duration * o.sampleRate);
    std::vector<float> out (n, 0.0f);
    std::vector<double> phase (static_cast<size_t> (spec.maxHarmonics));
    for (auto& p : phase)
        p = rng.uniform() * kTwoPi;
    constexpr size_t block = 32;
    for (size_t b = 0; b < n; b += block)
    {
        const double t = static_cast<double> (b) / o.sampleRate;
        const double f = f0 * std::pow (2.0, spec.cents (t) / 1200.0);
        const size_t len = std::min (block, n - b);
        for (int h = 1; h <= spec.maxHarmonics; ++h)
        {
            const double fh = h * f * std::sqrt (1.0 + spec.inharmonicity * h * h);
            if (fh > 0.45 * o.sampleRate)
                break;
            const double a0 = spec.amp (h, t);
            const double a1 = spec.amp (h, t + static_cast<double> (len) / o.sampleRate);
            if (a0 < 1e-7 && a1 < 1e-7)
            {
                phase[static_cast<size_t> (h - 1)] += kTwoPi * fh * static_cast<double> (len) / o.sampleRate;
                continue;
            }
            const double w = kTwoPi * fh / o.sampleRate;
            double& p = phase[static_cast<size_t> (h - 1)];
            for (size_t i = 0; i < len; ++i)
            {
                const double a = a0 + (a1 - a0) * static_cast<double> (i) / static_cast<double> (len);
                out[b + i] += static_cast<float> (a * std::sin (p));
                p += w;
            }
            p = std::fmod (p, kTwoPi);
        }
    }
    return out;
}

// Smooth bumps in log frequency: a simple "body" or formant response.
struct Resonance
{
    double freq, width /* octaves */, gainDb;
};
double resonances (const std::vector<Resonance>& rs, double f, double baseDb = 0.0)
{
    double db = baseDb;
    for (const auto& r : rs)
    {
        const double d = std::log2 (f / r.freq) / r.width;
        db += r.gainDb * std::exp (-0.5 * d * d);
    }
    return std::pow (10.0, db / 20.0);
}

std::function<double (double)> vibrato (double rate, double depthCents, double delay)
{
    return [=] (double t) {
        if (t < delay)
            return 0.0;
        const double fade = std::min (1.0, (t - delay) / 0.4);
        return depthCents * fade * std::sin (kTwoPi * rate * (t - delay));
    };
}

// Two-pole band-pass filtered noise.
std::vector<float> breath (size_t n, double centre, double q, double sr, Rng& rng)
{
    const double w = kTwoPi * centre / sr, alpha = std::sin (w) / (2.0 * q);
    const double b0 = alpha, a0 = 1.0 + alpha, a1 = -2.0 * std::cos (w), a2 = 1.0 - alpha;
    std::vector<float> out (n);
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    for (size_t i = 0; i < n; ++i)
    {
        const double x = rng.bipolar();
        const double y = (b0 * x - b0 * x2 - a1 * y1 - a2 * y2) / a0;
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;
        out[i] = static_cast<float> (y);
    }
    return out;
}

std::vector<float> karplusStrong (double f0, double p, const Options& o, Rng& rng)
{
    const auto n = static_cast<size_t> (o.duration * o.sampleRate);
    // Loop delay = N + 0.5 (averaging filter) + allpass fractional delay.
    const double period = o.sampleRate / f0;
    const auto len = static_cast<size_t> (std::floor (period - 0.5 - 0.1));
    const double frac = period - 0.5 - static_cast<double> (len);
    const double apc = (1.0 - frac) / (1.0 + frac);
    const double damping = 0.9965 + 0.003 * p + 0.0005 * rng.uniform();
    const double pick = rng.range (0.08, 0.35);          // pick position (comb notch)
    const double bright = rng.range (0.2, 0.9) * (1.0 - 0.4 * p);

    // Excitation: low-passed noise, combed at the pick position.
    std::vector<double> excite (len, 0.0);
    double lp = 0.0;
    for (auto& x : excite)
    {
        lp += bright * (rng.bipolar() - lp);
        x = lp;
    }
    const auto pickDelay = std::max<size_t> (1, static_cast<size_t> (pick * static_cast<double> (len)));
    std::vector<double> buf (len);
    for (size_t i = 0; i < len; ++i)
        buf[i] = excite[i] - (i >= pickDelay ? excite[i - pickDelay] : 0.0);

    std::vector<float> out (n);
    size_t idx = 0;
    double prev = 0.0, apX = 0.0, apY = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const double cur = buf[idx];
        const double avg = damping * 0.5 * (cur + prev);
        prev = cur;
        const double y = apc * avg + apX - apc * apY; // first-order allpass
        apX = avg;
        apY = y;
        buf[idx] = y;
        out[i] = static_cast<float> (cur);
        idx = (idx + 1) % len;
    }
    return out;
}

void normalise (std::vector<float>& x, double peak)
{
    float m = 0.0f;
    for (float v : x)
        m = std::max (m, std::abs (v));
    if (m > 0.0f)
        for (auto& v : x)
            v = static_cast<float> (v * peak / m);
}

} // namespace

std::vector<std::string> families()
{
    return { "pluck", "bowed", "reed", "brass", "flute", "organ", "mallet", "epiano", "vowel", "piano" };
}

Clip generate (const std::string& family, int variation, const Options& o)
{
    uint32_t h = o.seed * 2654435761u + static_cast<uint32_t> (variation) * 40503u;
    for (char c : family)
        h = h * 31u + static_cast<uint8_t> (c);
    Rng rng (h);
    const double p = o.variations > 1 ? static_cast<double> (variation) / (o.variations - 1) : 0.5; // 0..1 across the family
    const double f0 = hz (o.midiNote) * std::pow (2.0, rng.range (-o.detuneCents, o.detuneCents) / 1200.0);
    const double end = o.duration;
    const double rel = std::max (0.6 * end, end - rng.range (0.6, 0.9)); // sustained sounds release here
    const auto n = static_cast<size_t> (o.duration * o.sampleRate);

    std::vector<float> x;
    AdditiveSpec spec;
    if (family == "pluck")
    {
        x = karplusStrong (f0, p, o, rng);
    }
    else if (family == "bowed")
    {
        const double attack = rng.range (0.12, 0.4);
        const std::vector<Resonance> body { { rng.range (250, 450), 0.5, rng.range (4, 9) },
                                            { rng.range (800, 1400), 0.6, rng.range (3, 8) },
                                            { rng.range (2200, 3500), 0.5, rng.range (2, 7) } };
        const double roll = 0.9 + 0.5 * p;
        spec.amp = [=] (int k, double t) {
            // Upper harmonics speak later in the bow stroke.
            const double a = asr (t, attack * (1.0 + 0.03 * k), rel, 0.35);
            return a * std::pow (k, -roll) * resonances (body, k * f0);
        };
        spec.cents = vibrato (rng.range (5.0, 6.2), rng.range (8, 18), rng.range (0.4, 0.8));
    }
    else if (family == "reed")
    {
        const double even = rng.range (0.02, 0.25) * (0.3 + p);
        const double cutoff = rng.range (1200, 2600);
        const double attack = rng.range (0.03, 0.08);
        spec.amp = [=] (int k, double t) {
            const double base = (k % 2 == 1 ? 1.0 : even) / k;
            const double f = k * f0;
            const double roll = f > cutoff ? std::pow (cutoff / f, 2.0) : 1.0;
            return asr (t, attack, rel, 0.15) * base * roll;
        };
        spec.cents = vibrato (5.0, rng.range (0, 6) * p, 0.8);
    }
    else if (family == "brass")
    {
        const double attack = rng.range (0.05, 0.12);
        const double maxBright = rng.range (6, 14) * (0.6 + 0.6 * p);
        spec.amp = [=] (int k, double t) {
            // Louder = brighter: the spectral slope follows the envelope.
            double e = asr (t, attack, rel, 0.2);
            if (t < attack * 2.0)
                e *= 1.0 + 0.25 * std::sin (kPi * std::min (1.0, t / (attack * 2.0))); // overshoot
            const double kc = 1.0 + maxBright * std::pow (e, 1.5);
            return e * std::exp (-(k - 1) / kc) * (k < 3 ? 0.7 + 0.15 * k : 1.0);
        };
    }
    else if (family == "flute")
    {
        const double h2 = rng.range (0.1, 0.5), h3 = rng.range (0.05, 0.25) * (0.5 + p);
        const double attack = rng.range (0.08, 0.2);
        spec.amp = [=] (int k, double t) {
            const double lv = k == 1 ? 1.0 : k == 2 ? h2 : k == 3 ? h3 : h3 * std::pow (0.35, k - 3);
            return asr (t, attack * (k == 1 ? 1.0 : 1.4), rel, 0.2) * lv;
        };
        spec.cents = vibrato (rng.range (4.5, 5.5), rng.range (6, 14), 0.5);
    }
    else if (family == "organ")
    {
        // Drawbars: harmonics 1, 2, 3, 4, 6, 8 (plus a little 5 and 10).
        double bars[11] = {};
        for (int k : { 1, 2, 3, 4, 6, 8, 5, 10 })
            bars[k] = std::pow (rng.uniform(), 1.5) * (k == 1 ? 1.0 : 0.8);
        bars[1] = std::max (bars[1], 0.4);
        bars[4 + variation % 3 * 2] = std::max (bars[4 + variation % 3 * 2], 0.5 * p + 0.2);
        spec.amp = [=] (int k, double t) {
            if (k > 10)
                return 0.0;
            return asr (t, 0.006, rel, 0.03) * bars[k];
        };
    }
    else if (family == "mallet")
    {
        const double tau1 = rng.range (0.4, 1.4) * (0.6 + p), tau4 = rng.range (0.08, 0.25), tau10 = rng.range (0.02, 0.06);
        const double l4 = rng.range (0.2, 0.6), l10 = rng.range (0.05, 0.3), l2 = rng.range (0.0, 0.2);
        spec.amp = [=] (int k, double t) {
            const double a = std::min (1.0, t / 0.001);
            switch (k)
            {
                case 1: return a * std::exp (-t / tau1);
                case 2: return a * l2 * std::exp (-t / (0.5 * tau1));
                case 4: return a * l4 * std::exp (-t / tau4);
                case 10: return a * l10 * std::exp (-t / tau10);
                default: return 0.0;
            }
        };
    }
    else if (family == "epiano")
    {
        // Two-operator FM (1:1), index and level decaying.
        const double i0 = rng.range (1.0, 3.5) * (0.5 + p), tauI = rng.range (0.2, 0.8), tauA = rng.range (1.0, 2.5);
        const double tine = rng.range (0.0, 0.3);
        x.assign (n, 0.0f);
        double pc = rng.uniform() * kTwoPi, pm = 0.0, pt = 0.0;
        for (size_t i = 0; i < n; ++i)
        {
            const double t = static_cast<double> (i) / o.sampleRate;
            const double a = std::min (1.0, t / 0.002) * std::exp (-t / tauA) * std::min (1.0, std::max (0.0, (end - 0.05 - t) / 0.05));
            const double idx = i0 * std::exp (-t / tauI);
            x[i] = static_cast<float> (a * std::sin (pc + idx * std::sin (pm)) + tine * a * std::exp (-t / 0.05) * std::sin (pt));
            pc += kTwoPi * f0 / o.sampleRate;
            pm += kTwoPi * f0 / o.sampleRate;
            pt += kTwoPi * 7.0 * f0 / o.sampleRate;
        }
    }
    else if (family == "vowel")
    {
        static const double vowels[5][3] = { { 800, 1150, 2900 }, { 400, 1600, 2700 }, { 270, 2250, 3000 },
                                             { 450, 800, 2830 },  { 325, 700, 2530 } };
        const auto& v = vowels[variation % 5];
        const double shift = rng.range (0.92, 1.1);
        const std::vector<Resonance> formants { { v[0] * shift, 0.25, 20 }, { v[1] * shift, 0.2, 16 }, { v[2] * shift, 0.15, 12 } };
        const double tilt = rng.range (1.0, 1.6) - 0.3 * p;
        const double attack = rng.range (0.05, 0.12);
        spec.amp = [=] (int k, double t) { return asr (t, attack, rel, 0.2) * std::pow (k, -tilt) * resonances (formants, k * f0, -20); };
        spec.cents = vibrato (rng.range (5.0, 6.0), rng.range (10, 25), rng.range (0.3, 0.6));
    }
    else if (family == "piano")
    {
        const double hardness = rng.range (0.6, 1.6) * (0.6 + 0.8 * p);
        const double strike = rng.range (0.1, 0.15);
        const double tau = rng.range (1.5, 3.5);
        spec.inharmonicity = rng.range (0.0001, 0.0005);
        spec.amp = [=] (int k, double t) {
            const double lv = std::abs (std::sin (kPi * k * strike)) * std::pow (k, -2.2 + 0.8 * hardness);
            const double fast = std::exp (-t * (2.0 + 0.4 * k)), slow = std::exp (-t * (1.0 + 0.12 * k) / tau);
            const double a = std::min (1.0, t / 0.002) * std::min (1.0, std::max (0.0, (end - 0.05 - t) / 0.05));
            return a * lv * (0.6 * fast + 0.4 * slow);
        };
    }
    else
    {
        throw std::invalid_argument ("unknown family " + family);
    }

    if (x.empty())
        x = additive (spec, f0, o, rng);
    if (family == "flute")
    {
        const auto b = breath (n, rng.range (2000, 4000), 1.5, o.sampleRate, rng);
        const double level = rng.range (0.01, 0.04);
        for (size_t i = 0; i < n; ++i)
            x[i] += static_cast<float> (level * asr (static_cast<double> (i) / o.sampleRate, 0.05, rel, 0.2) * b[i]);
    }
    normalise (x, 0.5);

    Clip c;
    c.family = family;
    c.name = family + "_" + std::to_string (variation + 1);
    c.audio.sampleRate = o.sampleRate;
    c.audio.channels.push_back (std::move (x));
    return c;
}

std::vector<Clip> generateTrainingSet (const Options& o)
{
    std::vector<Clip> out;
    for (const auto& f : families())
        for (int v = 0; v < o.variations; ++v)
            out.push_back (generate (f, v, o));
    return out;
}

} // namespace pcs::testgen
