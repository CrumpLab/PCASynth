#pragma once

#include "pcs/Model.h"

#include <array>
#include <cstdint>
#include <vector>

namespace pcs {

using Point = std::array<float, kMaxComponents>;

// ---- Random walk -------------------------------------------------------------------

enum class WalkMode
{
    Drift,         // Brownian motion, pulled back towards home by the tether
    Jumps,         // a new random target every step; Glide sets how it gets there
    Tour,          // travels between training sounds, chosen at random
    NeighbourTour, // travels to one of the nearest training sounds each step
};

enum class WalkFocus
{
    Equal, // every wandering component moves by Amount (SD)
    Main,  // components move in proportion to how much variance they explain
};

struct WalkParams
{
    bool enabled = false;
    WalkMode mode = WalkMode::Drift;
    float amount = 1.0f;        // SD (Drift/Jumps); for tours, 1 = arrive at each sound
    float rate = 0.25f;         // Hz: steps per second, or Drift's speed
    bool sync = false;          // use syncBeats instead of rate
    float syncBeats = 4.0f;     // step length in beats when synced
    float glide = 0.7f;         // Jumps/tours: 0 = jump, 1 = glide for the whole step
    float tether = 0.5f;        // Drift: pull towards home (0 = free, bounded at ±4 SD)
    int dims = 4;               // components that wander (Drift/Jumps; tours move all)
    WalkFocus focus = WalkFocus::Equal;
    float perVoice = 0.0f;      // 0 = one shared walk, 1 = every voice walks alone
    uint32_t seed = 1;
    bool restartOnNote = false; // a note after silence restarts the walk (same seed -> same path)
    bool freeze = false;        // hold where it is
};

// One walker. Produces an offset from home (z units) that changes smoothly.
class RandomWalk
{
public:
    void reset (uint32_t seed) noexcept;
    // Advances by dt seconds. `home` is the point the offset is added to (for
    // tours); `sounds` are the training sounds' coordinates (numSounds × kMaxComponents)
    // and `sd` each component's SD relative to PC1 (for Main focus).
    void advance (double dt, const WalkParams& p, double stepSeconds, const Point& home, const std::vector<Point>& sounds,
                  const Point& relSd) noexcept;
    const Point& offset() const noexcept { return x; }
    int currentSound() const noexcept { return sound; }

private:
    float uniform() noexcept;  // [0, 1)
    float gaussian() noexcept; // N(0, 1)
    void newTarget (const WalkParams& p, const Point& home, const std::vector<Point>& sounds, const Point& relSd) noexcept;

    uint32_t rng = 1;
    Point x {}, from {}, to {};
    double phase = 1.0; // position within the current step, 0..1 (1 = needs a new target)
    int sound = -1, previous = -1;
};

// ---- LFOs --------------------------------------------------------------------------

enum class LfoShape { Sine, Triangle, Saw, Square, SampleHold, SmoothRandom };

// Destinations for LFOs, expression and the macro: PC1..PC16 or towards the
// chosen "direction" sound.
constexpr int kNumPcDestinations = 16;
constexpr int kTowardSound = kNumPcDestinations;

struct LfoParams
{
    bool enabled = false;
    LfoShape shape = LfoShape::Sine;
    float rate = 0.5f;      // Hz
    bool sync = false;
    float syncBeats = 4.0f; // cycle length when synced
    float depth = 1.0f;     // SD (or fraction of the way to the direction sound), bipolar
    int target = 0;         // 0..15 = PC, kTowardSound
};

class Lfo
{
public:
    void reset (uint32_t seed) noexcept;
    float advance (double dt, const LfoParams& p, double cycleSeconds) noexcept; // -1..1

private:
    float uniform() noexcept;
    double phase = 0.0;
    float held = 0.0f, prev = 0.0f, next = 0.0f;
    uint32_t rng = 1;
};

// ---- Everything together -------------------------------------------------------------

struct Expression
{
    int destination = -1; // -1 off, 0..15 PC, kTowardSound
    float amount = 1.0f;  // SD at full value (or fraction of the way to the sound)
};

struct ModParams
{
    WalkParams walk;
    std::array<LfoParams, 2> lfo;
    Expression velocity, modWheel, pressure;
    float macro = 0.0f;       // 0..1 of the way towards the direction sound
    float voiceSpread = 0.0f; // SD: each note starts at its own random offset (PC1..8)
    bool hasDirection = false;
    Point direction {};       // the direction sound's coordinates
};

// Adds `amount` along a destination to `out`: a component, or the line from
// `home` to the direction sound.
void addToDestination (Point& out, int destination, float amount, const ModParams& p, const Point& home) noexcept;

} // namespace pcs
