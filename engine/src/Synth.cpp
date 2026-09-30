#include "pcs/Synth.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace pcs {

namespace {
constexpr double kTwoPi = 6.28318530717958647692;
constexpr float kLn1000 = 6.90775527898f; // release reaches -60 dB in `release` seconds

#if defined(__GNUC__) && ! defined(__clang__)
#pragma GCC diagnostic ignored "-Wpsabi" // the vector helpers are internal and inlined: no ABI to keep
#endif

// Eight floats (or uint32s) at once, with GCC/Clang vector extensions: SSE
// or AVX on x86, NEON on arm64, from the same source.
using F8 = float __attribute__ ((vector_size (32)));
using U8 = uint32_t __attribute__ ((vector_size (32)));
using I8 = int32_t __attribute__ ((vector_size (32)));
static_assert (Synth::kLanes == 8, "lanes are 8-wide vectors");
template <typename V, typename T> inline V load (const T* p) noexcept { V v; std::memcpy (&v, p, sizeof v); return v; }
template <typename V, typename T> inline void store (T* p, V v) noexcept { std::memcpy (p, &v, sizeof v); }

inline float dbToLin (float db) noexcept { return std::exp2 (db * 0.166096404744f); } // 10^(db/20)
constexpr int roundUpToLanes (int n) noexcept { return (n + Synth::kLanes - 1) / Synth::kLanes * Synth::kLanes; }
}

void Synth::prepare (double sampleRate)
{
    sr = sampleRate;
    // Fixed, spread-out starting phases: every note starts the same way,
    // without the peaky waveform of all-zero phases.
    for (int h = 0; h < kMaxHarmonics; ++h)
    {
        const double golden = 0.61803398874989484820;
        phase0[static_cast<size_t> (h)] = static_cast<float> (kTwoPi * std::fmod ((h + 1) * (h + 1) * golden, 1.0));
    }
    reset();
}

std::unique_ptr<Synth::ModelSlot> Synth::makeSlot (std::shared_ptr<const Space> space)
{
    auto slot = std::make_unique<ModelSlot>();
    if (auto wave = std::dynamic_pointer_cast<const WaveModel> (space))
    {
        for (int i = 0; i < wave->numSounds(); ++i)
        {
            Point z {};
            const auto zs = wave->soundZ (i);
            std::copy (zs.begin(), zs.end(), z.begin());
            slot->soundZ.push_back (z);
        }
        for (int j = 0; j < wave->numComponents(); ++j)
            slot->relSd[static_cast<size_t> (j)] = static_cast<float> (wave->sd (j) / wave->sd (0));
        slot->refLevelDb = wave->refLevelDb;
        const int k = wave->numComponents();
        slot->tableWidth = roundUpToLanes (k + 1);
        const auto w = static_cast<size_t> (slot->tableWidth);
        slot->table.assign (static_cast<size_t> (wave->numSamples) * w, 0.0f);
        for (int i = 0; i < wave->numSamples; ++i)
        {
            float* row = slot->table.data() + static_cast<size_t> (i) * w;
            row[0] = wave->pca.mean[static_cast<size_t> (i)];
            for (int j = 0; j < k; ++j)
                row[1 + j] = static_cast<float> (wave->pca.component (j)[i] * wave->sd (j));
        }
        slot->wave = std::move (wave);
        return slot;
    }
    auto model = std::dynamic_pointer_cast<const Model> (space);
    if (space != nullptr && model == nullptr)
        throw std::invalid_argument ("unknown kind of space");
    if (model != nullptr && model->numHarmonics > kMaxHarmonics)
        throw std::invalid_argument ("model has more harmonics than the synth plays");
    if (model != nullptr)
    {
        if (model->numNoiseBands > kMaxNoiseBands)
            throw std::invalid_argument ("model has more noise bands than the synth plays");
        slot->cache.assign (static_cast<size_t> (model->numFrames * model->numHarmonics), 0.0f);
        slot->noiseCache.assign (static_cast<size_t> (model->numFrames * model->numNoiseBands), 0.0f);
        slot->stamps.assign (static_cast<size_t> (model->numFrames), 0);
        for (int i = 0; i < model->numSounds(); ++i)
        {
            Point z {};
            const auto zs = model->soundZ (i);
            std::copy (zs.begin(), zs.end(), z.begin());
            slot->soundZ.push_back (z);
        }
        for (int b = 0; b <= model->numNoiseBands; ++b)
            slot->bandEdges.push_back (static_cast<float> (noiseBandEdge (b, model->numNoiseBands)));
        if (model->numSounds() > 0)
        {
            std::vector<float> h (static_cast<size_t> (model->numHarmonics)), nz (static_cast<size_t> (std::max (1, model->numNoiseBands)));
            double sum = 0.0;
            for (const auto& z : slot->soundZ)
                sum += model->levelDb (z.data(), kMaxComponents, 0.0f, h.data(), nz.data());
            slot->refLevelDb = static_cast<float> (sum / model->numSounds());
        }
        for (int j = 0; j < model->numComponents(); ++j)
            slot->relSd[static_cast<size_t> (j)] = static_cast<float> (model->sd (j) / model->sd (0));
    }
    slot->model = std::move (model);
    return slot;
}

void Synth::setModel (std::shared_ptr<const Space> model)
{
    auto slot = makeSlot (std::move (model));
    swapModel (slot);
}

void Synth::swapModel (std::unique_ptr<ModelSlot>& slot) noexcept
{
    std::swap (current, slot); // an empty slot means "no model"
    if (current != nullptr)
        std::fill (current->stamps.begin(), current->stamps.end(), 0u);
    // `stamp` keeps counting (never back to 1): caches outside the slot, like the
    // partial tuning, must never mistake an old model's values for the new one's.
    reset();
}

void Synth::reset() noexcept
{
    for (auto& v : voices)
        v.active = false;
    bend = 0.0f;
    sustainDown = false;
    for (int j = 0; j < kMaxComponents; ++j)
        zSmooth[static_cast<size_t> (j)] = j < params.activeComponents ? params.z[static_cast<size_t> (j)] * params.exaggerate : 0.0f;
    heard = zSmooth;
    walkSeed = params.mod.walk.seed;
    walk.reset (walkSeed);
    walkMix = params.mod.walk.enabled ? 1.0f : 0.0f;
    for (size_t i = 0; i < lfos.size(); ++i)
        lfos[i].reset (walkSeed + 101u * static_cast<uint32_t> (i + 1));
    modWheel = pressure = 0.0f;
    noteCounter = 0; // noise seeds come from it: renders after a reset are reproducible
    centsStamp = 0;
    levelStamp = 0;
    sharedLevelGain = 1.0f;
    chBend.fill (0.0f);
    chPressure.fill (0.0f);
    chSlide.fill (64.0f / 127.0f);
    ++stamp;
}

int Synth::activeVoiceCount() const noexcept
{
    int n = 0;
    for (const auto& v : voices)
        n += v.active ? 1 : 0;
    return n;
}

int Synth::voicePositions (float* positions, int max) const noexcept
{
    int n = 0;
    for (const auto& v : voices)
        if (v.active && n < max)
            positions[n++] = static_cast<float> (v.pos);
    return n;
}

void Synth::process (float* const* out, int numChannels, int numSamples, const MidiEvent* events, int numEvents) noexcept
{
    int e = 0;
    int pos = 0;
    while (pos < numSamples)
    {
        while (e < numEvents && events[e].offset <= pos)
            handle (events[e++]);
        int end = std::min (numSamples, pos + kSubBlock);
        if (e < numEvents)
            end = std::min (end, std::max (pos + 1, events[e].offset));
        const int n = end - pos;
        render (mono.data(), n);
        for (int c = 0; c < numChannels; ++c)
            std::copy (mono.begin(), mono.begin() + n, out[c] + pos);
        pos = end;
    }
    while (e < numEvents)
        handle (events[e++]);
}

bool Synth::isMemberChannel (int channel) const noexcept
{
    if (! params.mpe.enabled || channel < 1 || channel > 16)
        return false;
    return channel != (params.mpe.upperZone ? 16 : 1);
}

void Synth::handle (const MidiEvent& e) noexcept
{
    const bool member = isMemberChannel (e.channel);
    const auto ch = static_cast<size_t> (std::clamp (e.channel, 0, 16));
    switch (e.type)
    {
        case MidiEvent::Type::NoteOn:
            if (e.value > 0.0f)
                noteOn (e.note, e.value, e.channel);
            else
                noteOff (e.note, e.channel);
            break;
        case MidiEvent::Type::NoteOff: noteOff (e.note, e.channel); break;
        case MidiEvent::Type::PitchBend:
            if (member)
            {
                chBend[ch] = std::clamp (e.value, -1.0f, 1.0f);
                for (auto& v : voices)
                    if (v.active && v.follows && v.channel == e.channel)
                    {
                        v.noteBend = chBend[ch];
                        setVoiceFrequency (v);
                    }
                break;
            }
            bend = std::clamp (e.value, -1.0f, 1.0f);
            for (auto& v : voices)
                if (v.active)
                    setVoiceFrequency (v);
            break;
        case MidiEvent::Type::ModWheel: modWheel = std::clamp (e.value, 0.0f, 1.0f); break;
        case MidiEvent::Type::Pressure:
            if (member)
            {
                chPressure[ch] = std::clamp (e.value, 0.0f, 1.0f);
                for (auto& v : voices)
                    if (v.active && v.follows && v.channel == e.channel)
                        v.pressure = chPressure[ch];
                break;
            }
            pressure = std::clamp (e.value, 0.0f, 1.0f);
            break;
        case MidiEvent::Type::PolyPressure: // per-note pressure (MPE mode; the plugin sends it only then)
            for (auto& v : voices)
                if (v.active && ! v.releasing && v.note == e.note && (e.channel == 0 || v.channel == e.channel))
                    v.pressure = std::clamp (e.value, 0.0f, 1.0f);
            break;
        case MidiEvent::Type::Slide:
            if (member)
            {
                chSlide[ch] = std::clamp (e.value, 0.0f, 1.0f);
                for (auto& v : voices)
                    if (v.active && v.follows && v.channel == e.channel)
                        v.slide = chSlide[ch];
            }
            break;
        case MidiEvent::Type::Sustain:
            sustainDown = e.value >= 0.5f;
            if (! sustainDown)
                for (auto& v : voices)
                    if (v.active && v.sustained)
                    {
                        v.sustained = false;
                        v.releasing = true;
                        v.releaseSeconds = params.release;
                    }
            break;
        case MidiEvent::Type::AllNotesOff:
            sustainDown = false;
            for (auto& v : voices)
            {
                v.releasing = v.active;
                v.sustained = false;
                v.releaseSeconds = params.release;
            }
            break;
    }
}

double Synth::lastPosition() const noexcept
{
    if (const auto* w = waveModel())
        return w->numSamples - 1;
    return model() != nullptr ? model()->numFrames - 1 : 0;
}

void Synth::noteOn (int note, float velocity, int channel) noexcept
{
    if (space() == nullptr)
        return;
    const int poly = std::clamp (params.polyphony, 1, kMaxVoices);
    Voice* v = nullptr;
    for (int i = 0; i < poly && v == nullptr; ++i)
        if (! voices[static_cast<size_t> (i)].active)
            v = &voices[static_cast<size_t> (i)];
    if (v == nullptr)
    {
        // Steal: the quietest releasing voice, else the oldest.
        for (int i = 0; i < poly; ++i)
        {
            auto& c = voices[static_cast<size_t> (i)];
            if (v == nullptr || (c.releasing && (! v->releasing || c.env < v->env)) || (! v->releasing && ! c.releasing && c.age < v->age))
                v = &c;
        }
    }

    // A note after silence restarts the shared walk (same seed -> same path).
    if (params.mod.walk.restartOnNote)
    {
        bool held = false;
        for (const auto& c : voices)
            held = held || (c.active && ! c.releasing);
        if (! held)
            walk.reset (params.mod.walk.seed);
    }

    v->active = true;
    v->releasing = false;
    v->sustained = false;
    v->velocity = std::clamp (velocity, 0.0f, 1.0f);
    // MPE: a note on a member channel starts from (and follows) that channel's state.
    v->channel = channel;
    v->follows = isMemberChannel (channel);
    const auto ch = static_cast<size_t> (std::clamp (channel, 0, 16));
    v->noteBend = v->follows ? chBend[ch] : 0.0f;
    v->pressure = v->follows ? chPressure[ch] : 0.0f;
    v->pressureSmooth = std::pow (v->pressure, std::pow (3.0f, -std::clamp (params.mpe.pressureCurve, -1.0f, 1.0f)));
    v->slide = v->slideSmooth = v->follows ? chSlide[ch] : 64.0f / 127.0f;
    v->walk.reset (params.mod.walk.seed * 7919u + static_cast<uint32_t> (noteCounter + 1));
    v->nz1 = {};
    v->nz2 = {};
    v->nGain = {};
    v->numNoiseBands = 0;
    v->noiseFreq = -1.0;
    v->ownT0 = -1;
    v->ownRefresh = 0;
    v->levelGain = v->levelTarget = 1.0f;
    v->levelTick = 0;
    for (size_t b = 0; b < v->noiseRng.size(); ++b)
    {
        v->noiseRng[b] = (0x9e3779b9u + static_cast<uint32_t> (b) * 0x85ebca6bu) ^ static_cast<uint32_t> (noteCounter * 2654435761u);
        if (v->noiseRng[b] == 0)
            v->noiseRng[b] = 1;
    }
    v->cents = {};
    v->ownPoint = false; // until modulate() says otherwise: start from the shared point
    v->pitchDelta = keytracking() ? model()->pitchDelta (note, params.keytrack) : 0.0f;
    if (const Model* m = model(); m != nullptr && m->hasPartials)
    {
        // Start at the right partial frequencies (they are refined every sub-block).
        m->decodePartials (heard.data(), kMaxComponents, v->cents.data(), v->pitchDelta);
    }
    v->spread = {};
    if (params.mod.voiceSpread > 0.0f)
        for (int j = 0; j < 8; ++j)
        {
            spreadRng ^= spreadRng << 13;
            spreadRng ^= spreadRng >> 17;
            spreadRng ^= spreadRng << 5;
            const float u = static_cast<float> (spreadRng >> 8) / 16777216.0f;
            v->spread[static_cast<size_t> (j)] = (2.0f * u - 1.0f) * 1.7320508f * params.mod.voiceSpread; // SD = voiceSpread
        }
    v->note = note;
    v->age = ++noteCounter;
    const float sens = std::clamp (params.velocitySensitivity, 0.0f, 1.0f);
    v->gain = 1.0f - sens + sens * std::clamp (velocity, 0.0f, 1.0f) * std::clamp (velocity, 0.0f, 1.0f);
    v->dir = 1;
    v->pos = params.mode == PlayMode::Scan ? params.scanPosition * lastPosition() : 0.0;
    v->waveStarted = false;
    v->env = 0.0f;
    v->releaseSeconds = params.release;
    for (int h = 0; h < kMaxHarmonics; ++h)
    {
        v->re[static_cast<size_t> (h)] = std::cos (phase0[static_cast<size_t> (h)]);
        v->im[static_cast<size_t> (h)] = std::sin (phase0[static_cast<size_t> (h)]);
        v->amp[static_cast<size_t> (h)] = 0.0f;
    }
    // The pitch curve and partials are set exactly on the first sub-block, from
    // the point as it is then (a restarted walk has moved it by then).
    v->curveCents = 0.0f;
    v->retunePending = true;
    setVoiceFrequency (*v);
}

void Synth::noteOff (int note, int channel) noexcept
{
    for (auto& v : voices)
        if (v.active && ! v.releasing && ! v.sustained && v.note == note
            && (! params.mpe.enabled || channel == 0 || v.channel == channel))
        {
            v.follows = false; // keeps its last bend, pressure and slide from here on
            if (sustainDown)
            {
                v.sustained = true;
                continue;
            }
            v.releasing = true;
            v.releaseSeconds = params.release;
        }
}

void Synth::setVoiceFrequency (Voice& v) noexcept
{
    v.freq = midiToHz (v.note + bend * params.pitchBendRange + v.noteBend * params.mpe.noteBendRange + v.curveCents / 100.0);
    const Model* m = model();
    const int modelH = m != nullptr ? m->numHarmonics : 0;
    const int limit = std::min (modelH, std::max (1, params.maxHarmonics));
    // Partial frequencies: harmonics, shifted by the model's partial offsets (inharmonicity).
    int n = 0;
    while (n < limit)
    {
        const float c = v.cents[static_cast<size_t> (n)];
        const double f = (n + 1) * v.freq * (c != 0.0f ? std::exp2 (c / 1200.0) : 1.0);
        if (f >= 0.47 * sr)
            break;
        v.hf[static_cast<size_t> (n)] = static_cast<float> (f);
        ++n;
    }
    for (int h = v.numHarmonics; h < n; ++h)
        v.amp[static_cast<size_t> (h)] = 0.0f; // newly audible harmonics fade in from silence
    v.numHarmonics = n;
    for (int h = 0; h < n; ++h)
    {
        const auto w = static_cast<float> (kTwoPi * v.hf[static_cast<size_t> (h)] / sr);
        v.cr[static_cast<size_t> (h)] = std::cos (w);
        v.ci[static_cast<size_t> (h)] = std::sin (w);
    }
    // The noise bands are wide: follow the pitch once it has moved 3 cents.
    if (v.noiseFreq <= 0.0 || std::abs (std::log2 (v.freq / v.noiseFreq)) > 3.0 / 1200.0)
        setNoiseFilters (v);
}

void Synth::setNoiseFilters (Voice& v) noexcept
{
    // Noise bands: band-pass filters (RBJ, 0 dB peak) spanning each band, which
    // sits at fixed multiples of the played fundamental. nNorm scales unit-variance
    // white noise to unit RMS out of the filter: for H = b0 (1 - z^-2) / (1 + a1 z^-1
    // + a2 z^-2) the output variance is 2 b0² / (1 - a2) = alpha / (1 + alpha),
    // exact up to Nyquist (the analog π/2 × bandwidth rule is not).
    v.noiseFreq = v.freq;
    const Model* m = model();
    const int bands = m != nullptr ? std::min (m->numNoiseBands, kMaxNoiseBands) : 0;
    const float* edges = current != nullptr ? current->bandEdges.data() : nullptr;
    int nb = 0;
    for (; nb < bands; ++nb)
    {
        const double lo = edges[nb] * v.freq, hi = edges[nb + 1] * v.freq;
        if (lo >= 0.47 * sr)
            break;
        const double top = std::min (hi, 0.49 * sr);
        const double fc = std::sqrt (lo * top), bw = std::max (1.0, top - lo);
        const double w = kTwoPi * fc / sr;
        const double alpha = std::sin (w) * bw / (2.0 * fc);
        const double a0 = 1.0 + alpha;
        const auto b = static_cast<size_t> (nb);
        v.nb0[b] = static_cast<float> (alpha / a0);
        v.na1[b] = static_cast<float> (-2.0 * std::cos (w) / a0);
        v.na2[b] = static_cast<float> ((1.0 - alpha) / a0);
        v.nNorm[b] = static_cast<float> (std::sqrt ((1.0 + alpha) / alpha));
        v.nFc[b] = static_cast<float> (fc);
    }
    for (int b = v.numNoiseBands; b < nb; ++b)
        v.nGain[static_cast<size_t> (b)] = 0.0f;
    // Bands past the top (up to a whole lane group) stay silent: no filter, no gain.
    for (int b = nb; b < roundUpToLanes (nb); ++b)
    {
        const auto bs = static_cast<size_t> (b);
        v.nb0[bs] = v.na1[bs] = v.na2[bs] = v.nz1[bs] = v.nz2[bs] = v.nGain[bs] = 0.0f;
        v.nFc[bs] = 0.0f;
    }
    v.numNoiseBands = nb;
}

float Synth::computeLevelGain (const Point& z, float pitchDelta) noexcept
{
    const float lock = std::clamp (params.levelLock, 0.0f, 1.0f);
    if (lock <= 0.0f || space() == nullptr || space()->numSounds() == 0)
        return 1.0f;
    const float level = waveModel() != nullptr ? waveModel()->levelDb (z.data(), kMaxComponents)
                                               : model()->levelDb (z.data(), kMaxComponents, pitchDelta, scratchLevel.data(), scratchLevelN.data());
    // Cut as much as needed; boost only a little (near-silent points stay quiet rather than turning into hiss).
    return dbToLin (lock * std::clamp (current->refLevelDb - level, -48.0f, 12.0f));
}

float Synth::pitchCurveAt (const Voice& v, double pos) const noexcept
{
    if (model() == nullptr)
        return 0.0f;
    const auto& m = *current->model;
    if (! m.hasPitchCurve || params.pitchEnvelope == 0.0f)
        return 0.0f;
    const float* z = v.ownPoint ? v.point.data() : heard.data();
    const int t0 = std::clamp (static_cast<int> (std::floor (pos)), 0, m.numFrames - 1);
    const auto frac = static_cast<float> (std::clamp (pos - t0, 0.0, 1.0));
    const float a = m.decodePitch (t0, z, kMaxComponents, v.pitchDelta), b = m.decodePitch (t0 + 1, z, kMaxComponents, v.pitchDelta);
    return params.pitchEnvelope * (a + frac * (b - a));
}

bool Synth::keytracking() const noexcept
{
    const Model* m = model();
    return m != nullptr && m->pitchTracking && params.keytrack != 0.0f;
}

int Synth::frame (int t) noexcept
{
    const auto& m = *current->model;
    t = std::clamp (t, 0, m.numFrames - 1);
    if (current->stamps[static_cast<size_t> (t)] != stamp)
    {
        const float floorLin = std::pow (10.0f, m.floorDb / 20.0f);
        float* dst = current->cache.data() + static_cast<size_t> (t) * static_cast<size_t> (m.numHarmonics);
        m.decodeFrame (t, heard.data(), kMaxComponents, scratchDb.data());
        for (int h = 0; h < m.numHarmonics; ++h)
            dst[h] = std::max (0.0f, dbToLin (scratchDb[static_cast<size_t> (h)]) - floorLin);
        if (m.numNoiseBands > 0)
        {
            float* nd = current->noiseCache.data() + static_cast<size_t> (t) * static_cast<size_t> (m.numNoiseBands);
            m.decodeNoiseFrame (t, heard.data(), kMaxComponents, cacheScratchN.data()); // not scratchN: callers hold targets there
            for (int b = 0; b < m.numNoiseBands; ++b)
                nd[b] = std::max (0.0f, dbToLin (cacheScratchN[static_cast<size_t> (b)]) - floorLin);
        }
        current->stamps[static_cast<size_t> (t)] = stamp;
    }
    return t;
}

void Synth::frameAt (Voice& v, double pos, float* dst, float* noise, bool cached) noexcept
{
    const auto& m = *current->model;
    const int h = m.numHarmonics, nb = m.numNoiseBands;
    const int t0 = std::clamp (static_cast<int> (std::floor (pos)), 0, m.numFrames - 1);
    const int t1 = std::min (t0 + 1, m.numFrames - 1);
    const auto frac = static_cast<float> (pos - std::floor (pos));
    if (! v.ownPoint)
    {
        frame (t0);
        frame (t1);
        const float* a = current->cache.data() + static_cast<size_t> (t0 * h);
        const float* b = current->cache.data() + static_cast<size_t> (t1 * h);
        for (int i = 0; i < h; ++i)
            dst[i] = a[i] + frac * (b[i] - a[i]);
        if (noise != nullptr && nb > 0)
        {
            const float* na = current->noiseCache.data() + static_cast<size_t> (t0 * nb);
            const float* nbb = current->noiseCache.data() + static_cast<size_t> (t1 * nb);
            for (int i = 0; i < nb; ++i)
                noise[i] = na[i] + frac * (nbb[i] - na[i]);
        }
        return;
    }
    // The voice's own point (and pitch, with keytracking): decode both frames for it.
    const float floorLin = dbToLin (m.floorDb);
    auto decode = [&] (int t, float* harm, float* nz)
    {
        m.decodeFrame (t, v.point.data(), kMaxComponents, harm, v.pitchDelta);
        for (int i = 0; i < h; ++i)
            harm[i] = std::max (0.0f, dbToLin (harm[i]) - floorLin);
        if (nb > 0)
        {
            m.decodeNoiseFrame (t, v.point.data(), kMaxComponents, nz, v.pitchDelta);
            for (int i = 0; i < nb; ++i)
                nz[i] = std::max (0.0f, dbToLin (nz[i]) - floorLin);
        }
    };
    const float *a = v.own0.data(), *b = v.own1.data(), *na = v.ownN0.data(), *nbb = v.ownN1.data();
    if (! cached)
    {
        decode (t0, scratchDb.data(), scratchN2.data());
        decode (t1, scratchC.data(), scratchN4.data());
        a = scratchDb.data();
        b = scratchC.data();
        na = scratchN2.data();
        nbb = scratchN4.data();
    }
    else if (t0 != v.ownT0 || v.ownRefresh == kOwnRefresh)
    {
        if (t0 == v.ownT0 + 1 && v.ownRefresh != kOwnRefresh)
        {
            // Moved on by one frame at the same point: the old t1 is the new t0.
            v.own0 = v.own1;
            v.ownN0 = v.ownN1;
        }
        else
            decode (t0, v.own0.data(), v.ownN0.data());
        decode (t1, v.own1.data(), v.ownN1.data());
        v.ownT0 = t0;
    }
    for (int i = 0; i < h; ++i)
        dst[i] = a[i] + frac * (b[i] - a[i]);
    if (noise != nullptr && nb > 0)
        for (int i = 0; i < nb; ++i)
            noise[i] = na[i] + frac * (nbb[i] - na[i]);
}

bool Synth::newestVoicePoint (Point& out) const noexcept
{
    const Voice* best = nullptr;
    for (const auto& v : voices)
        if (v.active && (best == nullptr || (v.releasing == best->releasing ? v.age > best->age : ! v.releasing)))
            best = &v;
    if (best == nullptr)
        return false;
    out = best->ownPoint ? best->point : heard;
    return true;
}

int Synth::voiceInfo (VoiceInfo* out, int max) const noexcept
{
    int n = 0;
    for (const auto& v : voices)
        if (v.active && n < max)
            out[n++] = { v.note, v.channel, v.noteBend * params.mpe.noteBendRange, v.pressureSmooth, v.slideSmooth, v.releasing };
    return n;
}

int Synth::voicePoints (Point* points, int max) const noexcept
{
    int n = 0;
    for (const auto& v : voices)
        if (v.active && n < max)
            points[n++] = v.ownPoint ? v.point : heard;
    return n;
}

void Synth::modulate (int n) noexcept
{
    const auto& mp = params.mod;
    const auto& w = mp.walk;
    const double dt = n / sr;
    const double beat = 60.0 / std::max (1.0, params.bpm);

    // The walk: restart on a new seed, retarget on a new mode, fade in and out.
    if (w.seed != walkSeed)
    {
        walkSeed = w.seed;
        walk.reset (walkSeed);
    }
    if (w.mode != walkMode)
    {
        walkMode = w.mode;
        walk.reset (walkSeed);
    }
    const float fade = 1.0f - std::exp (-static_cast<float> (dt) / 0.1f);
    walkMix += ((w.enabled ? 1.0f : 0.0f) - walkMix) * fade;
    if (walkMix < 1e-4f && ! w.enabled)
        walkMix = 0.0f;
    const double step = w.sync ? w.syncBeats * beat : 1.0 / std::max (0.001f, w.rate);
    const float shared = walkMix * (1.0f - std::clamp (w.perVoice, 0.0f, 1.0f));
    const float own = walkMix * std::clamp (w.perVoice, 0.0f, 1.0f);
    if (w.enabled)
        walk.advance (dt, w, step, zSmooth, current->soundZ, current->relSd);

    Point g = zSmooth;
    for (size_t j = 0; j < g.size(); ++j)
        g[j] += shared * walk.offset()[j];
    for (size_t i = 0; i < lfos.size(); ++i)
    {
        const auto& lp = mp.lfo[i];
        const float value = lfos[i].advance (dt, lp, lp.sync ? lp.syncBeats * beat : 1.0 / std::max (0.001f, lp.rate));
        if (lp.enabled)
            addToDestination (g, lp.target, lp.depth * value, mp, zSmooth);
    }
    addToDestination (g, mp.modWheel.destination, mp.modWheel.amount * modWheel, mp, zSmooth);
    addToDestination (g, mp.pressure.destination, mp.pressure.amount * pressure, mp, zSmooth);
    if (mp.macro != 0.0f)
        addToDestination (g, kTowardSound, mp.macro, mp, zSmooth);
    for (auto& v : g)
        v = std::clamp (v, -8.0f, 8.0f);
    if (g != heard)
    {
        heard = g;
        ++stamp;
    }

    // Per-voice points: own walks, velocity, spread and MPE pressure and slide.
    const auto& mpe = params.mpe;
    const bool mpeMoves = mpe.enabled && (mpe.pressure.destination >= 0 || mpe.slide.destination >= 0);
    const bool perVoice = own > 0.0f || mp.velocity.destination >= 0 || mp.voiceSpread > 0.0f || mpeMoves;
    const float smooth = 1.0f - std::exp (-static_cast<float> (dt) / std::max (0.001f, mpe.smoothing));
    const float gamma = std::pow (3.0f, -std::clamp (mpe.pressureCurve, -1.0f, 1.0f));
    const bool keytrack = keytracking();
    for (auto& v : voices)
    {
        if (! v.active)
            continue;
        if (! v.ownPoint && (perVoice || keytrack))
        {
            v.ownRefresh = 0; // decode its own point on the next sub-block
            v.levelTick = 0;
        }
        v.ownPoint = perVoice || keytrack;
        if (! perVoice)
        {
            v.point = heard;
            continue;
        }
        if (own > 0.0f)
            v.walk.advance (dt, w, step, heard, current->soundZ, current->relSd);
        auto& pt = v.point;
        pt = heard;
        for (size_t j = 0; j < pt.size(); ++j)
            pt[j] += own * v.walk.offset()[j] + v.spread[j];
        addToDestination (pt, mp.velocity.destination, mp.velocity.amount * v.velocity, mp, zSmooth);
        v.pressureSmooth += (std::pow (v.pressure, gamma) - v.pressureSmooth) * smooth;
        v.slideSmooth += (v.slide - v.slideSmooth) * smooth;
        if (mpe.enabled)
        {
            addToDestination (pt, mpe.pressure.destination, mpe.pressure.amount * v.pressureSmooth, mp, zSmooth);
            const float slide = mpe.slideBipolar ? (v.slideSmooth - 64.0f / 127.0f) / (63.0f / 127.0f) : v.slideSmooth;
            addToDestination (pt, mpe.slide.destination, mpe.slide.amount * std::clamp (slide, -1.0f, 1.0f), mp, zSmooth);
        }
        for (auto& x : pt)
            x = std::clamp (x, -8.0f, 8.0f);
    }
}

void Synth::render (float* out, int n) noexcept
{
    std::fill (out, out + n, 0.0f);
    if (space() == nullptr)
        return;

    // Glide towards the target point.
    const float coef = params.morphTime > 0.0f ? 1.0f - std::exp (-n / (params.morphTime * static_cast<float> (sr))) : 1.0f;
    bool moved = false;
    for (int j = 0; j < kMaxComponents; ++j)
    {
        const float target = j < params.activeComponents ? params.z[static_cast<size_t> (j)] * params.exaggerate : 0.0f;
        auto& z = zSmooth[static_cast<size_t> (j)];
        const float next = std::abs (target - z) < 1e-5f ? target : z + coef * (target - z);
        moved = moved || next != z;
        z = next;
    }
    if (moved)
        ++stamp;
    modulate (n);
    if (levelStamp != stamp || levelLockFor != params.levelLock)
    {
        sharedLevelGain = computeLevelGain (heard, 0.0f);
        levelStamp = stamp;
        levelLockFor = params.levelLock;
    }

    if (params.tiltDbPerOctave != tiltFor)
    {
        tiltFor = params.tiltDbPerOctave;
        for (int h = 0; h < kMaxHarmonics; ++h)
            tiltGain[static_cast<size_t> (h)] = dbToLin (tiltFor * std::log2 (static_cast<float> (h + 1)));
    }
    std::fill (lanes.begin(), lanes.end(), 0.0f);
    for (auto& v : voices)
        if (v.active)
        {
            if (waveModel() != nullptr)
                renderWaveVoice (v, n);
            else
                renderVoice (v, n);
        }
    for (int i = 0; i < n; ++i)
    {
        const float* l = lanes.data() + i * kLanes;
        float sum = 0.0f;
        for (int k = 0; k < kLanes; ++k)
            sum += l[k];
        out[i] = sum;
    }
}

void Synth::renderVoice (Voice& v, int n) noexcept
{
    const auto& m = *current->model;
    const double last = m.numFrames - 1;
    const double step = params.speed * m.frameRate * n / sr;

    // Move through the envelope.
    double loopA = std::clamp<double> (params.loopStart, 0.0, 1.0) * last;
    double loopB = std::clamp<double> (params.loopEnd, 0.0, 1.0) * last;
    if (loopB < loopA)
        std::swap (loopA, loopB);
    if (loopB - loopA < 4.0)
    {
        loopB = std::min (last, loopA + 4.0);
        loopA = std::max (0.0, loopB - 4.0);
    }
    const double loopLen = loopB - loopA;
    bool crossfade = false;
    double xfFrac = 0.0;
    switch (params.mode)
    {
        case PlayMode::OneShot:
            v.pos += step;
            if (v.pos >= last)
            {
                v.pos = last;
                if (! v.releasing)
                {
                    v.releasing = true;
                    v.releaseSeconds = std::min (v.releaseSeconds, 0.05f);
                }
            }
            break;
        case PlayMode::Loop:
        {
            v.pos += step;
            if (v.pos >= loopB)
                v.pos = loopA + std::fmod (v.pos - loopA, loopLen);
            const double xf = std::min (loopLen * 0.5, 0.1 * m.frameRate);
            if (v.pos > loopB - xf && v.pos <= loopB)
            {
                crossfade = true;
                xfFrac = (v.pos - (loopB - xf)) / xf;
            }
            v.pos = std::min (v.pos, last);
            break;
        }
        case PlayMode::PingPong:
            v.pos += step * v.dir;
            if (v.pos >= loopB)
            {
                v.pos = loopB - std::min (loopLen, v.pos - loopB);
                v.dir = -1;
            }
            else if (v.dir < 0 && v.pos <= loopA)
            {
                v.pos = loopA + std::min (loopLen, loopA - v.pos);
                v.dir = 1;
            }
            break;
        case PlayMode::Scan:
        {
            const double target = std::clamp<double> (params.scanPosition, 0.0, 1.0) * last;
            const double c = 1.0 - std::exp (-n / (0.05 * sr));
            v.pos += (target - v.pos) * c;
            break;
        }
    }

    // Envelope.
    if (v.releasing)
        v.env *= std::exp (-kLn1000 * n / (std::max (0.001f, v.releaseSeconds) * static_cast<float> (sr)));
    else
        v.env = std::min (1.0f, v.env + n / (std::max (0.0005f, params.attack) * static_cast<float> (sr)));
    const bool finished = v.releasing && v.env < 1e-4f;
    const float envEnd = finished ? 0.0f : v.env;

    // Timbre following pitch (keytrack), and the partials' tuning at this point.
    v.pitchDelta = keytracking() ? m.pitchDelta (v.note + bend * params.pitchBendRange + v.noteBend * params.mpe.noteBendRange, params.keytrack) : 0.0f;
    // A voice's own point is decoded afresh only every kOwnRefresh sub-blocks
    // (~3 ms), and the partials' tuning follows any point at that rate: points
    // move smoothly, and the amplitudes ramp in between. Counted from the
    // note's start, so a phrase played again sounds the same.
    if (--v.ownRefresh <= 0)
        v.ownRefresh = kOwnRefresh;
    const bool refresh = v.ownRefresh == kOwnRefresh;
    // Level Lock at the voice's own point: loudness changes slowly, so it is
    // measured every fourth refresh (~11 ms) and glided to.
    if (v.ownPoint && refresh && v.levelTick++ % 4 == 0)
        v.levelTarget = computeLevelGain (v.point, v.pitchDelta);
    v.levelGain = v.levelTick <= 1 ? v.levelTarget : v.levelGain + 0.1f * (v.levelTarget - v.levelGain);
    // The learned pitch curve (vibrato, glides), at the same rate.
    const bool exact = v.retunePending;
    v.retunePending = false;
    bool retune = exact;
    if (refresh)
    {
        const float curve = pitchCurveAt (v, v.pos);
        if (exact || std::abs (curve - v.curveCents) > 0.05f)
        {
            v.curveCents = curve;
            retune = true;
        }
    }
    if (m.hasPartials && refresh)
    {
        const float* want = heardCents.data();
        if (v.ownPoint)
        {
            m.decodePartials (v.point.data(), kMaxComponents, scratchC.data(), v.pitchDelta);
            want = scratchC.data();
        }
        else if (centsStamp != stamp)
        {
            m.decodePartials (heard.data(), kMaxComponents, heardCents.data());
            centsStamp = stamp;
        }
        bool changed = exact;
        for (int h = 0; h < m.numHarmonics && ! changed; ++h)
            changed = std::abs (want[h] - v.cents[static_cast<size_t> (h)]) > 0.01f;
        if (changed)
        {
            std::copy (want, want + m.numHarmonics, v.cents.begin());
            retune = true;
        }
    }
    if (retune)
        setVoiceFrequency (v);

    // Target amplitudes at the end of this sub-block.
    const bool noiseOn = m.numNoiseBands > 0 && params.noiseDb > -59.9f;
    float* target = scratchA.data();
    float* noiseTarget = noiseOn ? scratchN.data() : nullptr;
    frameAt (v, v.pos, target, noiseTarget, true);
    if (crossfade)
    {
        frameAt (v, v.pos - loopLen, scratchB.data(), noiseOn ? scratchN3.data() : nullptr, false);
        const auto g = static_cast<float> (xfFrac);
        for (int h = 0; h < v.numHarmonics; ++h)
            target[h] += g * (scratchB[static_cast<size_t> (h)] - target[h]);
        if (noiseOn)
            for (int b = 0; b < m.numNoiseBands; ++b)
                noiseTarget[b] += g * (scratchN3[static_cast<size_t> (b)] - noiseTarget[b]);
    }
    const float level = dbToLin (params.gainDb) * v.gain * envEnd * (v.ownPoint ? v.levelGain : sharedLevelGain);
    const float nyquistFadeStart = 0.40f * static_cast<float> (sr), nyquistFadeEnd = 0.47f * static_cast<float> (sr);
    for (int h = 0; h < v.numHarmonics; ++h)
    {
        float g = level;
        if (tiltFor != 0.0f)
            g *= tiltGain[static_cast<size_t> (h)];
        const float f = v.hf[static_cast<size_t> (h)];
        if (f > nyquistFadeStart)
            g *= std::clamp ((nyquistFadeEnd - f) / (nyquistFadeEnd - nyquistFadeStart), 0.0f, 1.0f);
        target[h] *= g;
    }

    // Oscillator bank: complex rotators, amplitudes ramping linearly across the
    // sub-block. Partials run kLanes at a time, as one vector per group;
    // lanes past numHarmonics are silent.
    const float invN = 1.0f / static_cast<float> (n);
    const int oscEnd = roundUpToLanes (v.numHarmonics);
    for (int h = v.numHarmonics; h < oscEnd; ++h)
    {
        target[h] = 0.0f;
        v.amp[static_cast<size_t> (h)] = 0.0f;
    }
    for (int g0 = 0; g0 < oscEnd; g0 += kLanes)
    {
        bool silent = true;
        for (int k = 0; k < kLanes; ++k)
            silent = silent && v.amp[static_cast<size_t> (g0 + k)] == 0.0f && target[g0 + k] == 0.0f;
        if (silent)
            continue; // skipping keeps the phases where they were, like a partial that stays silent
        const auto hs = static_cast<size_t> (g0);
        F8 re = load<F8> (v.re.data() + hs), im = load<F8> (v.im.data() + hs);
        const F8 cr = load<F8> (v.cr.data() + hs), ci = load<F8> (v.ci.data() + hs);
        F8 a = load<F8> (v.amp.data() + hs);
        const F8 t = load<F8> (target + g0);
        const F8 da = (t - a) * invN;
        for (int i = 0; i < n; ++i)
        {
            a += da;
            const F8 nr = re * cr - im * ci;
            im = re * ci + im * cr;
            re = nr;
            float* acc = lanes.data() + i * kLanes;
            store (acc, load<F8> (acc) + a * im);
        }
        // Keep the rotators on the unit circle.
        const F8 c = 1.5f - 0.5f * (re * re + im * im);
        store (v.re.data() + hs, re * c);
        store (v.im.data() + hs, im * c);
        store (v.amp.data() + hs, t);
    }

    // Residual noise: white noise through each band's filter, gains ramping
    // across the sub-block like the harmonics; bands also run kLanes at a time.
    if (noiseOn || v.numNoiseBands > 0)
    {
        const float noiseLevel = noiseOn ? level * dbToLin (params.noiseDb) : 0.0f;
        alignas (32) std::array<float, kMaxNoiseBands> dg {};
        bool any = false;
        for (int b = 0; b < v.numNoiseBands; ++b)
        {
            const auto bs = static_cast<size_t> (b);
            float g = noiseOn ? noiseTarget[b] * v.nNorm[bs] * noiseLevel : 0.0f;
            if (g > 0.0f && tiltFor != 0.0f)
                g *= dbToLin (tiltFor * std::log2 (v.nFc[bs] / static_cast<float> (v.freq)));
            if (v.nFc[bs] > nyquistFadeStart)
                g *= std::clamp ((nyquistFadeEnd - v.nFc[bs]) / (nyquistFadeEnd - nyquistFadeStart), 0.0f, 1.0f);
            dg[bs] = (g - v.nGain[bs]) * invN;
            any = any || g > 0.0f || v.nGain[bs] > 0.0f;
        }
        if (any)
        {
            constexpr float kUnitVariance = 1.7320508f / 2147483648.0f; // uniform int32 -> variance 1
            for (int g0 = 0; g0 < roundUpToLanes (v.numNoiseBands); g0 += kLanes)
            {
                const auto bs = static_cast<size_t> (g0);
                const F8 b0 = load<F8> (v.nb0.data() + bs), a1 = load<F8> (v.na1.data() + bs), a2 = load<F8> (v.na2.data() + bs);
                const F8 dgain = load<F8> (dg.data() + bs);
                F8 z1 = load<F8> (v.nz1.data() + bs), z2 = load<F8> (v.nz2.data() + bs), gain = load<F8> (v.nGain.data() + bs);
                U8 r = load<U8> (v.noiseRng.data() + bs);
                for (int i = 0; i < n; ++i)
                {
                    r ^= r << 13;
                    r ^= r >> 17;
                    r ^= r << 5;
                    const F8 x = __builtin_convertvector ((I8) r, F8) * kUnitVariance;
                    // Transposed direct form II band-pass (b1 = 0, b2 = -b0).
                    const F8 y = b0 * x + z1;
                    z1 = z2 - a1 * y;
                    z2 = -(b0 * x) - a2 * y;
                    gain += dgain;
                    float* acc = lanes.data() + i * kLanes;
                    store (acc, load<F8> (acc) + gain * y);
                }
                store (v.nz1.data() + bs, z1);
                store (v.nz2.data() + bs, z2);
                store (v.nGain.data() + bs, gain);
                store (v.noiseRng.data() + bs, r);
            }
        }
    }

    if (finished)
        v.active = false;
}

// ---- waveform voices (WaveModel) ------------------------------------------------------

void Synth::renderWaveVoice (Voice& v, int n) noexcept
{
    const auto& m = *current->wave;
    const auto& table = current->table;
    const int width = current->tableWidth;
    const double last = m.numSamples - 1;
    // Resampling: the model's sounds sit at m.refHz, so the note's pitch sets the speed.
    const double step = v.freq / m.refHz * m.sampleRate / sr;

    // The mixing weights at the end of this sub-block: the mean, then the point.
    const auto& z = v.ownPoint ? v.point : heard;
    alignas (32) std::array<float, kMaxComponents + kLanes> w1 {};
    w1[0] = 1.0f;
    for (int j = 0; j < std::min (m.numComponents(), kMaxComponents); ++j)
        w1[static_cast<size_t> (1 + j)] = z[static_cast<size_t> (j)];
    if (! v.waveStarted)
        v.waveW = w1;
    alignas (32) std::array<float, kMaxComponents + kLanes> dw {};
    for (int k = 0; k < width; ++k)
        dw[static_cast<size_t> (k)] = w1[static_cast<size_t> (k)] - v.waveW[static_cast<size_t> (k)];

    // Level Lock, envelope and gain, ramped across the sub-block.
    if (v.ownPoint && (v.levelTick++ % 16 == 0))
        v.levelTarget = computeLevelGain (v.point, 0.0f);
    v.levelGain = v.levelTick <= 1 ? v.levelTarget : v.levelGain + 0.1f * (v.levelTarget - v.levelGain);
    if (v.releasing)
        v.env *= std::exp (-kLn1000 * n / (std::max (0.001f, v.releaseSeconds) * static_cast<float> (sr)));
    else
        v.env = std::min (1.0f, v.env + n / (std::max (0.0005f, params.attack) * static_cast<float> (sr)));
    const bool finished = v.releasing && v.env < 1e-4f;
    const float level = finished ? 0.0f : dbToLin (params.gainDb) * v.gain * v.env * (v.ownPoint ? v.levelGain : sharedLevelGain);
    const float levelStart = v.waveStarted ? v.waveLevel : 0.0f;

    // The mix at integer sample i: the start weights' and the change's (a small cache: neighbours repeat).
    struct Cached
    {
        long index = -1;
        float base = 0.0f, change = 0.0f;
    };
    std::array<Cached, 8> cache {};
    auto mixAt = [&] (long i) -> const Cached& {
        i = std::clamp (i, 0L, static_cast<long> (last));
        auto& c = cache[static_cast<size_t> (i & 7)];
        if (c.index != i)
        {
            const float* row = table.data() + static_cast<size_t> (i) * static_cast<size_t> (width);
            F8 a {}, d {};
            for (int k = 0; k < width; k += kLanes)
            {
                const F8 t = load<F8> (row + k);
                a += load<F8> (v.waveW.data() + k) * t;
                d += load<F8> (dw.data() + k) * t;
            }
            float sa = 0.0f, sd = 0.0f;
            for (int k = 0; k < kLanes; ++k)
            {
                sa += a[k];
                sd += d[k];
            }
            c = { i, sa, sd };
        }
        return c;
    };
    // 4-point Hermite interpolation at position p, weights `g` of the way to the end ones.
    auto read = [&] (double p, float g) {
        const auto i = static_cast<long> (std::floor (p));
        const auto f = static_cast<float> (p - static_cast<double> (i));
        float y[4];
        for (int k = 0; k < 4; ++k)
        {
            const auto& c = mixAt (i - 1 + k);
            y[k] = c.base + g * c.change;
        }
        const float c1 = 0.5f * (y[2] - y[0]);
        const float c2 = y[0] - 2.5f * y[1] + 2.0f * y[2] - 0.5f * y[3];
        const float c3 = 0.5f * (y[3] - y[0]) + 1.5f * (y[1] - y[2]);
        return ((c3 * f + c2) * f + c1) * f + y[1];
    };

    // Where the loop is (Loop, Ping-pong), or the grain held (Scan).
    double loopA = std::clamp<double> (params.loopStart, 0.0, 1.0) * last;
    double loopB = std::clamp<double> (params.loopEnd, 0.0, 1.0) * last;
    if (loopB < loopA)
        std::swap (loopA, loopB);
    if (params.mode == PlayMode::Scan)
    {
        const double centre = std::clamp<double> (params.scanPosition, 0.0, 1.0) * last, half = 0.04 * m.sampleRate;
        loopA = std::max (0.0, centre - half);
        loopB = std::min (last, centre + half);
    }
    const double minLen = 0.02 * m.sampleRate;
    if (loopB - loopA < minLen)
    {
        loopB = std::min (last, loopA + minLen);
        loopA = std::max (0.0, loopB - minLen);
    }
    const double loopLen = loopB - loopA;
    const double xf = std::min (loopLen * 0.5, 0.05 * m.sampleRate); // crossfade at the loop's end
    const bool looping = params.mode == PlayMode::Loop || params.mode == PlayMode::Scan;

    for (int i = 0; i < n; ++i)
    {
        const float g = static_cast<float> (i + 1) / static_cast<float> (n);
        float x = 0.0f;
        if (looping && v.pos > loopB - xf && v.pos < loopB)
        {
            // Crossfade into the loop's start before jumping there.
            const auto t = static_cast<float> ((v.pos - (loopB - xf)) / xf);
            x = (1.0f - t) * read (v.pos, g) + t * read (v.pos - loopLen, g);
        }
        else
            x = read (v.pos, g);
        lanes[static_cast<size_t> (i * kLanes)] += x * (levelStart + g * (level - levelStart));

        switch (params.mode)
        {
            case PlayMode::OneShot:
                v.pos += step;
                if (v.pos >= last)
                {
                    v.pos = last;
                    if (! v.releasing)
                    {
                        v.releasing = true;
                        v.releaseSeconds = std::min (v.releaseSeconds, 0.02f);
                    }
                }
                break;
            case PlayMode::Loop:
            case PlayMode::Scan:
                v.pos += step;
                if (v.pos >= loopB)
                    v.pos = loopA + std::fmod (v.pos - loopA, loopLen);
                if (params.mode == PlayMode::Scan && (v.pos < loopA - 1.0 || v.pos > loopB))
                    v.pos = loopA; // the grain moved
                break;
            case PlayMode::PingPong:
                v.pos += step * v.dir;
                if (v.pos >= loopB)
                {
                    v.pos = loopB - std::min (loopLen, v.pos - loopB);
                    v.dir = -1;
                }
                else if (v.dir < 0 && v.pos <= loopA)
                {
                    v.pos = loopA + std::min (loopLen, loopA - v.pos);
                    v.dir = 1;
                }
                break;
        }
    }
    v.waveW = w1;
    v.waveLevel = level;
    v.waveStarted = true;
    if (finished)
        v.active = false;
}

} // namespace pcs
