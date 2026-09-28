#pragma once

#include "pcs/Model.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace pcs {

// How a note moves through the model's envelope (frames over time).
enum class PlayMode
{
    OneShot,  // play the envelope once, like the training sample
    Loop,     // play to Loop End, then loop Loop Start..Loop End (crossfaded) while held
    PingPong, // play to Loop End, then back and forth between the loop points
    Scan,     // hold a single moment of the envelope (Scan Position), like a wavetable
};

struct SynthParams
{
    std::array<float, kMaxComponents> z {}; // point in PCA space (SD units)
    int activeComponents = kMaxComponents;  // components beyond this are ignored
    float exaggerate = 1.0f;                // multiplies every z
    float gainDb = -6.0f;
    float attack = 0.003f;                  // s, ramp on top of the model's own attack
    float release = 0.3f;                   // s, to -60 dB
    float speed = 1.0f;                     // envelope playback rate (0 freezes)
    PlayMode mode = PlayMode::OneShot;
    float loopStart = 0.3f;                 // fractions of the envelope's duration
    float loopEnd = 0.7f;
    float scanPosition = 0.2f;
    float velocitySensitivity = 1.0f;       // 0: every note at full level
    float pitchBendRange = 2.0f;            // semitones
    float tiltDbPerOctave = 0.0f;           // brightness: harmonic h gets tilt × log2(h) dB
    float morphTime = 0.05f;                // s, smoothing of moves in the space
    int maxHarmonics = 128;
    int polyphony = 16;
};

struct MidiEvent
{
    enum class Type { NoteOn, NoteOff, PitchBend, AllNotesOff };
    int offset = 0;      // sample within the block
    Type type = Type::NoteOn;
    int note = 60;
    float value = 1.0f;  // NoteOn: velocity 0..1; PitchBend: -1..1
};

// Polyphonic additive synth that plays points of a Model. Each voice runs a
// bank of sine oscillators at multiples of the note's frequency, with the
// harmonic amplitudes decoded from the model frame by frame.
class Synth
{
public:
    static constexpr int kMaxVoices = 32;
    static constexpr int kMaxHarmonics = 128;
    static constexpr int kSubBlock = 32;

    // Not real-time safe (allocates).
    void prepare (double sampleRate);
    void setModel (std::shared_ptr<const Model> model);
    const Model* model() const noexcept { return current.get(); }

    // Real-time safe.
    void setParams (const SynthParams& p) noexcept { params = p; }
    const SynthParams& getParams() const noexcept { return params; }
    // Replaces out[0..numChannels)[0..numSamples) with the synth's output.
    void process (float* const* out, int numChannels, int numSamples, const MidiEvent* events, int numEvents) noexcept;
    void reset() noexcept;
    int activeVoiceCount() const noexcept;
    // Smoothed point currently heard.
    const std::array<float, kMaxComponents>& currentZ() const noexcept { return zSmooth; }

private:
    struct Voice
    {
        bool active = false;
        bool releasing = false;
        int note = 60;
        float gain = 1.0f;
        uint64_t age = 0;
        double pos = 0.0;  // envelope frame
        int dir = 1;
        float env = 0.0f;
        float releaseSeconds = 0.3f;
        double freq = 0.0;
        int numHarmonics = 0;
        std::array<float, kMaxHarmonics> re {}, im {}, cr {}, ci {}, amp {};
    };

    void handle (const MidiEvent& e) noexcept;
    void noteOn (int note, float velocity) noexcept;
    void noteOff (int note) noexcept;
    void setVoiceFrequency (Voice& v) noexcept;
    void render (float* out, int n) noexcept;
    void renderVoice (Voice& v, float* out, int n) noexcept;
    const float* frame (int t) noexcept; // linear amplitudes at the smoothed point, cached
    void frameAt (double pos, float* dst) noexcept;

    double sr = 48000.0;
    std::shared_ptr<const Model> current;
    SynthParams params;
    std::array<Voice, kMaxVoices> voices;
    uint64_t noteCounter = 0;
    float bend = 0.0f;

    std::array<float, kMaxComponents> zSmooth {};
    std::vector<float> cache;       // numFrames × numHarmonics, linear amplitude
    std::vector<uint32_t> cacheStamp;
    uint32_t stamp = 1;
    std::vector<float> scratchDb, scratchA, scratchB;
    std::vector<float> mono;
    std::array<float, kMaxHarmonics> phase0 {};
};

} // namespace pcs
