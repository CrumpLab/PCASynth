#pragma once

#include "pcs/Model.h"
#include "pcs/Modulation.h"
#include "pcs/WaveModel.h"

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

// MPE (Stage 6): per-note pitch bend, pressure and slide from controllers
// such as the Osmose. Notes on member channels follow their channel's
// messages until key-up; the master channel's bend and pressure stay global.
struct MpeParams
{
    bool enabled = false;
    bool upperZone = false;          // lower: master 1, members 2-16; upper: master 16, members 1-15
    float noteBendRange = 48.0f;     // semitones for a member channel's full bend
    Expression pressure { 0, 1.5f }; // destination + amount at full pressure (default PC1)
    float pressureCurve = 0.0f;      // -1..1: >0 more response to a light touch, <0 less
    float smoothing = 0.02f;         // s, for pressure and slide
    Expression slide { 1, 1.0f };    // CC74 (default PC2)
    bool slideBipolar = true;        // true: centre (64) = no move, else 0 = no move
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
    ModParams mod;                          // random walk, LFOs, expression (Stage 5)
    MpeParams mpe;                          // per-note expression (Stage 6)
    float noiseDb = 0.0f;                   // residual noise level vs the model (dB); <= -60 mutes it (Stage 7)
    float keytrack = 1.0f;                  // how much timbre follows pitch (models with pitch tracking)
    float levelLock = 0.0f;                 // 0..1: pulls every point's loudness towards the training sounds' (Stage 8)
    float pitchEnvelope = 1.0f;             // how much of the learned pitch curve (vibrato, glides, pitch drops) to play
    double bpm = 120.0;                     // for synced walk steps and LFOs
};

struct MidiEvent
{
    enum class Type { NoteOn, NoteOff, PitchBend, Sustain, AllNotesOff, ModWheel, Pressure, PolyPressure, Slide };
    int offset = 0;      // sample within the block
    Type type = Type::NoteOn;
    int note = 60;
    float value = 1.0f;  // NoteOn: velocity 0..1; PitchBend: -1..1; Sustain: pedal down when >= 0.5;
                         // ModWheel, Pressure, PolyPressure, Slide (CC74): 0..1
    int channel = 0;     // 1-16 (0: unknown). Only MPE mode looks at it.
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
    static constexpr int kMaxNoiseBands = 64;
    static constexpr int kLanes = 8; // oscillators and noise bands run in groups of this many (SIMD)
    static constexpr int kOwnRefresh = 4; // sub-blocks between decodes of a voice's own point

    // A model plus the per-model buffers the synth needs, built off the
    // audio thread so a model can be swapped in without allocating.
    struct ModelSlot
    {
        std::shared_ptr<const Model> model;      // a harmonic space, or ...
        std::shared_ptr<const WaveModel> wave;   // ... a waveform space
        // Waveform space: per sample, the mean and each component's loading
        // (component × SD), tableWidth floats (a multiple of kLanes, zero-padded).
        std::vector<float> table;
        int tableWidth = 0;
        std::vector<float> cache;       // numFrames × numHarmonics, linear amplitude
        std::vector<float> noiseCache;  // numFrames × numNoiseBands, linear RMS
        std::vector<uint32_t> stamps;   // per frame: point the cache was decoded for
        std::vector<float> bandEdges;   // noise band edges, multiples of f0 (numNoiseBands + 1)
        std::vector<Point> soundZ;      // training sounds' coordinates (for walk tours)
        Point relSd {};                 // each component's SD relative to PC1
        float refLevelDb = 0.0f;        // the training sounds' average Model::levelDb (for Level Lock)
    };
    static std::unique_ptr<ModelSlot> makeSlot (std::shared_ptr<const Space> space); // allocates

    // Not real-time safe (allocates).
    void prepare (double sampleRate);
    void setModel (std::shared_ptr<const Space> space);

    // Real-time safe: installs `slot` (sounding notes stop) and hands back
    // the previous slot in `slot`, to be destroyed off the audio thread.
    void swapModel (std::unique_ptr<ModelSlot>& slot) noexcept;
    const Model* model() const noexcept { return current != nullptr ? current->model.get() : nullptr; }     // harmonic
    const WaveModel* waveModel() const noexcept { return current != nullptr ? current->wave.get() : nullptr; }
    const Space* space() const noexcept
    {
        return model() != nullptr ? static_cast<const Space*> (model()) : static_cast<const Space*> (waveModel());
    }

    // Real-time safe.
    void setParams (const SynthParams& p) noexcept { params = p; }
    const SynthParams& getParams() const noexcept { return params; }
    // Replaces out[0..numChannels)[0..numSamples) with the synth's output.
    void process (float* const* out, int numChannels, int numSamples, const MidiEvent* events, int numEvents) noexcept;
    void reset() noexcept;
    int activeVoiceCount() const noexcept;
    // Where each sounding voice is in the envelope (frames; waveform spaces:
    // samples at the model's rate), for displays.
    // Returns how many were written (at most `max`).
    int voicePositions (float* positions, int max) const noexcept;
    // Smoothed point set by the parameters (home), and the point heard after
    // the shared modulation (walk, LFOs, mod wheel, pressure, macro).
    const std::array<float, kMaxComponents>& currentZ() const noexcept { return zSmooth; }
    const Point& heardPoint() const noexcept { return heard; }
    // Where each sounding voice is in the space (with its own modulation).
    int voicePoints (Point* points, int max) const noexcept;
    int walkSound() const noexcept { return walk.currentSound(); } // tours: the sound it is heading to
    // The point of the most recently played voice still sounding (false if none).
    bool newestVoicePoint (Point& out) const noexcept;

    // Per-note expression of each sounding voice (for displays).
    struct VoiceInfo
    {
        int note = 0, channel = 0;
        float bendSemitones = 0.0f; // per-note bend
        float pressure = 0.0f;      // shaped and smoothed, 0..1
        float slide = 0.0f;         // smoothed, 0..1
        bool releasing = false;
    };
    int voiceInfo (VoiceInfo* out, int max) const noexcept;

private:
    struct Voice
    {
        bool active = false;
        bool releasing = false;
        bool sustained = false; // key up while the pedal is down
        int note = 60;
        float gain = 1.0f;
        uint64_t age = 0;
        double pos = 0.0;  // envelope frame
        int dir = 1;
        float env = 0.0f;
        float releaseSeconds = 0.3f;
        double freq = 0.0;
        int numHarmonics = 0;
        // Oscillators, in lanes of kLanes (see renderVoice): partials past
        // numHarmonics, up to the next multiple of kLanes, stay silent.
        alignas (32) std::array<float, kMaxHarmonics> re {}, im {}, cr {}, ci {}, amp {};
        float velocity = 1.0f;
        RandomWalk walk;     // the voice's own walk (Walk Per Voice)
        Point spread {};     // Voice Spread offset
        Point point {};      // the voice's point this sub-block
        bool ownPoint = false;
        // MPE
        int channel = 0;
        bool follows = false; // still following its member channel (until key-up)
        float noteBend = 0.0f, pressure = 0.0f, slide = 0.5f; // latest raw values
        float pressureSmooth = 0.0f, slideSmooth = 0.5f;
        // Stage 7: partial frequencies, timbre following pitch, noise.
        std::array<float, kMaxHarmonics> cents {}, hf {}; // partial offsets applied; partial frequencies (Hz)
        float pitchDelta = 0.0f;
        float curveCents = 0.0f; // the pitch curve applied (cents)
        bool retunePending = false; // a new note: set curve and partials exactly on its first sub-block
        int numNoiseBands = 0;
        alignas (32) std::array<float, kMaxNoiseBands> nb0 {}, na1 {}, na2 {}, nz1 {}, nz2 {}, nGain {}, nNorm {}, nFc {};
        alignas (32) std::array<uint32_t, kMaxNoiseBands> noiseRng {}; // an independent source per band (their powers add)
        double noiseFreq = -1.0; // fundamental the noise filters were designed for
        // The voice's own point decoded (linear) at frames ownT0 and ownT0 + 1,
        // refreshed every kOwnRefresh sub-blocks or when the frame changes.
        std::array<float, kMaxHarmonics> own0 {}, own1 {};
        std::array<float, kMaxNoiseBands> ownN0 {}, ownN1 {};
        int ownT0 = -1, ownRefresh = 0;
        float levelGain = 1.0f, levelTarget = 1.0f; // Level Lock at the voice's own point
        // Waveform voices: the mixing weights last used (mean, then z), and the level last used.
        alignas (32) std::array<float, kMaxComponents + kLanes> waveW {};
        bool waveStarted = false;
        float waveLevel = 0.0f;
        uint32_t levelTick = 0;
    };

    void handle (const MidiEvent& e) noexcept;
    void noteOn (int note, float velocity, int channel) noexcept;
    void noteOff (int note, int channel) noexcept;
    bool isMemberChannel (int channel) const noexcept;
    void setVoiceFrequency (Voice& v) noexcept;
    void setNoiseFilters (Voice& v) noexcept;
    float pitchCurveAt (const Voice& v, double pos) const noexcept; // cents, Pitch Envelope applied
    void render (float* out, int n) noexcept;
    void renderVoice (Voice& v, int n) noexcept;
    void renderWaveVoice (Voice& v, int n) noexcept;
    double lastPosition() const noexcept; // the envelope's last position (frames, or samples)
    int frame (int t) noexcept; // decodes frame t at the heard point into the caches (once per point); returns t clamped
    // Linear harmonic amplitudes (and noise band RMS, if `noise`) at envelope position `pos`.
    // `cached`: a voice's own point may come from its cache (see Voice::own0).
    void frameAt (Voice& v, double pos, float* harmonics, float* noise, bool cached) noexcept;
    bool keytracking() const noexcept;
    void modulate (int n) noexcept;

    double sr = 48000.0;
    std::unique_ptr<ModelSlot> current;
    SynthParams params;
    std::array<Voice, kMaxVoices> voices;
    uint64_t noteCounter = 0;
    float bend = 0.0f;
    bool sustainDown = false;

    // Modulation state.
    RandomWalk walk;
    std::array<Lfo, 2> lfos;
    float modWheel = 0.0f, pressure = 0.0f, walkMix = 0.0f;
    uint32_t walkSeed = 0;
    WalkMode walkMode = WalkMode::Drift;
    uint32_t spreadRng = 12345;
    Point heard {};
    std::array<float, kMaxHarmonics> scratchC {}, heardCents {};
    uint32_t centsStamp = 0;
    // Level Lock at the shared point, for the stamp and amount it was computed for.
    float sharedLevelGain = 1.0f, levelLockFor = 0.0f;
    uint32_t levelStamp = 0;
    float computeLevelGain (const Point& z, float pitchDelta) noexcept;
    std::array<float, kMaxNoiseBands> scratchN {}, scratchN2 {}, scratchN3 {}, scratchN4 {}, cacheScratchN {};

    // MPE: each channel's latest per-note values (index 1..16).
    std::array<float, 17> chBend {}, chPressure {}, chSlide {};
    std::array<float, kMaxComponents> zSmooth {};
    uint32_t stamp = 1;
    std::array<float, kMaxHarmonics> scratchDb {}, scratchA {}, scratchB {}, scratchLevel {};
    std::array<float, kMaxNoiseBands> scratchLevelN {};
    std::array<float, kSubBlock> mono {};
    // Every voice's partials and noise, summed per lane; reduced to mono once per sub-block.
    alignas (32) std::array<float, kSubBlock * kLanes> lanes {};
    std::array<float, kMaxHarmonics> tiltGain {};
    float tiltFor = 0.0f;
    std::array<float, kMaxHarmonics> phase0 {};
};

} // namespace pcs
