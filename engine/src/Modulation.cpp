#include "pcs/Modulation.h"

#include <algorithm>
#include <cmath>

namespace pcs {

namespace {
constexpr double kTwoPi = 6.28318530717958647692;
constexpr float kBound = 4.0f; // Drift reflects off ±4 SD

uint32_t xorshift (uint32_t& s) noexcept
{
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

float smoothstep (float t) noexcept { return t * t * (3.0f - 2.0f * t); }
} // namespace

// ---- RandomWalk ---------------------------------------------------------------------

float soundDistance (const Point& a, const Point& b, const Point& relSd) noexcept
{
    double d = 0.0;
    for (size_t j = 0; j < a.size(); ++j)
    {
        const double diff = static_cast<double> (a[j] - b[j]) * relSd[j];
        d += diff * diff;
    }
    return static_cast<float> (d);
}

void RandomWalk::reset (uint32_t seed) noexcept
{
    rng = seed * 2654435761u + 0x9e3779b9u;
    if (rng == 0)
        rng = 1;
    x = from = to = {};
    phase = 1.0;
    sound = previous = -1;
}

float RandomWalk::uniform() noexcept { return static_cast<float> (xorshift (rng) >> 8) / 16777216.0f; }

float RandomWalk::gaussian() noexcept
{
    const float u1 = std::max (1e-7f, uniform()), u2 = uniform();
    return std::sqrt (-2.0f * std::log (u1)) * std::cos (static_cast<float> (kTwoPi) * u2);
}

void RandomWalk::newTarget (const WalkParams& p, const Point& home, const std::vector<Point>& sounds, const Point& relSd) noexcept
{
    from = x;
    const bool tour = p.mode == WalkMode::Tour || p.mode == WalkMode::NeighbourTour;
    if (tour && sounds.size() >= 2)
    {
        const int n = static_cast<int> (sounds.size());
        int next = 0;
        if (p.mode == WalkMode::Tour || sound < 0)
        {
            do
                next = static_cast<int> (uniform() * static_cast<float> (n)) % n;
            while (next == sound);
        }
        else
        {
            // One of the three nearest sounds (not the one we just left), by
            // distance in the model's own units, so the main components dominate.
            std::array<std::pair<float, int>, 3> best;
            best.fill ({ 1e30f, -1 });
            for (int i = 0; i < n; ++i)
            {
                if (i == sound || (i == previous && n > 2))
                    continue;
                const float d = soundDistance (sounds[static_cast<size_t> (i)], sounds[static_cast<size_t> (sound)], relSd);
                if (d < best.back().first)
                {
                    best.back() = { d, i };
                    std::sort (best.begin(), best.end());
                }
            }
            int count = 0;
            for (const auto& b : best)
                count += b.second >= 0 ? 1 : 0;
            next = best[static_cast<size_t> (static_cast<int> (uniform() * static_cast<float> (count)) % std::max (1, count))].second;
        }
        previous = sound;
        sound = next;
        for (int j = 0; j < kMaxComponents; ++j)
            to[static_cast<size_t> (j)] = (sounds[static_cast<size_t> (sound)][static_cast<size_t> (j)] - home[static_cast<size_t> (j)]) * p.amount;
        return;
    }
    // Jumps: a random point, normally distributed around home.
    for (int j = 0; j < kMaxComponents; ++j)
        to[static_cast<size_t> (j)] = j < p.dims ? gaussian() * p.amount * relSd[static_cast<size_t> (j)] : 0.0f;
}

void RandomWalk::advance (double dt, const WalkParams& p, double stepSeconds, const Point& home, const std::vector<Point>& sounds,
                          const Point& relSd) noexcept
{
    if (p.freeze || dt <= 0.0)
        return;
    stepSeconds = std::max (0.01, stepSeconds);

    if (p.mode == WalkMode::Drift)
    {
        // Ornstein-Uhlenbeck: dx = -theta x dt + sigma dW. With tether 1 the
        // walk's spread (SD) is Amount; lower tethers let it wander further.
        const double rate = 1.0 / stepSeconds;
        const double theta = rate * std::max (0.0f, p.tether);
        const double sigma = p.amount * std::sqrt (2.0 * rate);
        const auto sq = static_cast<float> (std::sqrt (dt));
        for (int j = 0; j < kMaxComponents; ++j)
        {
            auto& v = x[static_cast<size_t> (j)];
            if (j >= p.dims)
            {
                v *= static_cast<float> (std::exp (-4.0 * rate * dt)); // components leaving the walk fade home
                continue;
            }
            const float s = relSd[static_cast<size_t> (j)];
            v += static_cast<float> (-theta * v * dt) + static_cast<float> (sigma) * s * sq * gaussian();
            const float bound = kBound * std::max (s, 0.05f);
            if (v > bound)
                v = 2.0f * bound - v;
            if (v < -bound)
                v = -2.0f * bound - v;
        }
        phase = 1.0;
        return;
    }

    // Stepped modes: glide from `from` to `to` over the first `glide` of each step.
    if (phase >= 1.0)
    {
        newTarget (p, home, sounds, relSd);
        phase = 0.0;
    }
    phase = std::min (1.0, phase + dt / stepSeconds);
    if (phase > 1.0 - 1e-9) // rounding must not delay the next step (platform-independent timing)
        phase = 1.0;
    const float g = std::clamp (p.glide, 0.0f, 1.0f);
    const float t = g <= 0.0f ? 1.0f : smoothstep (std::min (1.0f, static_cast<float> (phase) / g));
    for (int j = 0; j < kMaxComponents; ++j)
        x[static_cast<size_t> (j)] = from[static_cast<size_t> (j)] + t * (to[static_cast<size_t> (j)] - from[static_cast<size_t> (j)]);
}

// ---- Lfo -------------------------------------------------------------------------------

void Lfo::reset (uint32_t seed) noexcept
{
    rng = seed * 747796405u + 2891336453u;
    if (rng == 0)
        rng = 1;
    phase = 0.0;
    held = prev = 0.0f;
    next = uniform() * 2.0f - 1.0f;
}

float Lfo::uniform() noexcept { return static_cast<float> (xorshift (rng) >> 8) / 16777216.0f; }

float Lfo::advance (double dt, const LfoParams& p, double cycleSeconds) noexcept
{
    phase += dt / std::max (0.01, cycleSeconds);
    if (phase >= 1.0)
    {
        phase -= std::floor (phase);
        held = uniform() * 2.0f - 1.0f;
        prev = next;
        next = uniform() * 2.0f - 1.0f;
    }
    const auto ph = static_cast<float> (phase);
    switch (p.shape)
    {
        case LfoShape::Sine: return std::sin (static_cast<float> (kTwoPi) * ph);
        case LfoShape::Triangle: return ph < 0.25f ? 4.0f * ph : ph < 0.75f ? 2.0f - 4.0f * ph : 4.0f * ph - 4.0f;
        case LfoShape::Saw: return 2.0f * ph - 1.0f;
        case LfoShape::Square: return ph < 0.5f ? 1.0f : -1.0f;
        case LfoShape::SampleHold: return held;
        case LfoShape::SmoothRandom: return prev + smoothstep (ph) * (next - prev);
    }
    return 0.0f;
}

// ---- Destinations ------------------------------------------------------------------------

void addToDestination (Point& out, int destination, float amount, const ModParams& p, const Point& home) noexcept
{
    if (destination >= 0 && destination < kNumPcDestinations)
        out[static_cast<size_t> (destination)] += amount;
    else if (destination == kTowardSound && p.hasDirection)
        for (size_t j = 0; j < out.size(); ++j)
            out[j] += amount * (p.direction[j] - home[j]);
}

} // namespace pcs
