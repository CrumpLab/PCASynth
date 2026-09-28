#include "pcs/Synth.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace pcs {

namespace {
constexpr double kTwoPi = 6.28318530717958647692;
constexpr float kLn1000 = 6.90775527898f; // release reaches -60 dB in `release` seconds
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

std::unique_ptr<Synth::ModelSlot> Synth::makeSlot (std::shared_ptr<const Model> model)
{
    auto slot = std::make_unique<ModelSlot>();
    if (model != nullptr && model->numHarmonics > kMaxHarmonics)
        throw std::invalid_argument ("model has more harmonics than the synth plays");
    if (model != nullptr)
    {
        slot->cache.assign (static_cast<size_t> (model->dims()), 0.0f);
        slot->stamps.assign (static_cast<size_t> (model->numFrames), 0);
    }
    slot->model = std::move (model);
    return slot;
}

void Synth::setModel (std::shared_ptr<const Model> model)
{
    auto slot = makeSlot (std::move (model));
    swapModel (slot);
}

void Synth::swapModel (std::unique_ptr<ModelSlot>& slot) noexcept
{
    std::swap (current, slot); // an empty slot means "no model"
    if (current != nullptr)
        std::fill (current->stamps.begin(), current->stamps.end(), 0u);
    stamp = 1;
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
    ++stamp;
}

int Synth::activeVoiceCount() const noexcept
{
    int n = 0;
    for (const auto& v : voices)
        n += v.active ? 1 : 0;
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

void Synth::handle (const MidiEvent& e) noexcept
{
    switch (e.type)
    {
        case MidiEvent::Type::NoteOn:
            if (e.value > 0.0f)
                noteOn (e.note, e.value);
            else
                noteOff (e.note);
            break;
        case MidiEvent::Type::NoteOff: noteOff (e.note); break;
        case MidiEvent::Type::PitchBend:
            bend = std::clamp (e.value, -1.0f, 1.0f);
            for (auto& v : voices)
                if (v.active)
                    setVoiceFrequency (v);
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

void Synth::noteOn (int note, float velocity) noexcept
{
    if (model() == nullptr)
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

    v->active = true;
    v->releasing = false;
    v->sustained = false;
    v->note = note;
    v->age = ++noteCounter;
    const float sens = std::clamp (params.velocitySensitivity, 0.0f, 1.0f);
    v->gain = 1.0f - sens + sens * std::clamp (velocity, 0.0f, 1.0f) * std::clamp (velocity, 0.0f, 1.0f);
    v->dir = 1;
    v->pos = params.mode == PlayMode::Scan ? params.scanPosition * (model()->numFrames - 1) : 0.0;
    v->env = 0.0f;
    v->releaseSeconds = params.release;
    for (int h = 0; h < kMaxHarmonics; ++h)
    {
        v->re[static_cast<size_t> (h)] = std::cos (phase0[static_cast<size_t> (h)]);
        v->im[static_cast<size_t> (h)] = std::sin (phase0[static_cast<size_t> (h)]);
        v->amp[static_cast<size_t> (h)] = 0.0f;
    }
    setVoiceFrequency (*v);
}

void Synth::noteOff (int note) noexcept
{
    for (auto& v : voices)
        if (v.active && ! v.releasing && ! v.sustained && v.note == note)
        {
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
    v.freq = midiToHz (v.note + bend * params.pitchBendRange);
    const int modelH = model() != nullptr ? model()->numHarmonics : 0;
    const int limit = std::min (modelH, std::max (1, params.maxHarmonics));
    int n = 0;
    while (n < limit && (n + 1) * v.freq < 0.47 * sr)
        ++n;
    for (int h = v.numHarmonics; h < n; ++h)
        v.amp[static_cast<size_t> (h)] = 0.0f; // newly audible harmonics fade in from silence
    v.numHarmonics = n;
    for (int h = 0; h < n; ++h)
    {
        const double w = kTwoPi * (h + 1) * v.freq / sr;
        v.cr[static_cast<size_t> (h)] = static_cast<float> (std::cos (w));
        v.ci[static_cast<size_t> (h)] = static_cast<float> (std::sin (w));
    }
}

const float* Synth::frame (int t) noexcept
{
    const auto& m = *current->model;
    t = std::clamp (t, 0, m.numFrames - 1);
    float* dst = current->cache.data() + static_cast<size_t> (t) * static_cast<size_t> (m.numHarmonics);
    if (current->stamps[static_cast<size_t> (t)] != stamp)
    {
        m.decodeFrame (t, zSmooth.data(), kMaxComponents, scratchDb.data());
        const float floorLin = std::pow (10.0f, m.floorDb / 20.0f);
        for (int h = 0; h < m.numHarmonics; ++h)
            dst[h] = std::max (0.0f, std::pow (10.0f, scratchDb[static_cast<size_t> (h)] / 20.0f) - floorLin);
        current->stamps[static_cast<size_t> (t)] = stamp;
    }
    return dst;
}

void Synth::frameAt (double pos, float* dst) noexcept
{
    const int h = current->model->numHarmonics;
    const int t0 = static_cast<int> (std::floor (pos));
    const auto frac = static_cast<float> (pos - t0);
    const float* a = frame (t0);
    const float* b = frame (t0 + 1);
    for (int i = 0; i < h; ++i)
        dst[i] = a[i] + frac * (b[i] - a[i]);
}

void Synth::render (float* out, int n) noexcept
{
    std::fill (out, out + n, 0.0f);
    if (model() == nullptr)
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

    for (auto& v : voices)
        if (v.active)
            renderVoice (v, out, n);
}

void Synth::renderVoice (Voice& v, float* out, int n) noexcept
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

    // Target amplitudes at the end of this sub-block.
    float* target = scratchA.data();
    frameAt (v.pos, target);
    if (crossfade)
    {
        frameAt (v.pos - loopLen, scratchB.data());
        const auto g = static_cast<float> (xfFrac);
        for (int h = 0; h < v.numHarmonics; ++h)
            target[h] += g * (scratchB[static_cast<size_t> (h)] - target[h]);
    }
    const float level = std::pow (10.0f, params.gainDb / 20.0f) * v.gain * envEnd;
    const float nyquistFadeStart = 0.40f * static_cast<float> (sr), nyquistFadeEnd = 0.47f * static_cast<float> (sr);
    for (int h = 0; h < v.numHarmonics; ++h)
    {
        float g = level;
        if (params.tiltDbPerOctave != 0.0f)
            g *= std::pow (10.0f, params.tiltDbPerOctave * std::log2 (static_cast<float> (h + 1)) / 20.0f);
        const auto f = static_cast<float> ((h + 1) * v.freq);
        if (f > nyquistFadeStart)
            g *= std::clamp ((nyquistFadeEnd - f) / (nyquistFadeEnd - nyquistFadeStart), 0.0f, 1.0f);
        target[h] *= g;
    }

    // Oscillator bank: amplitudes ramp linearly across the sub-block.
    const float invN = 1.0f / static_cast<float> (n);
    for (int h = 0; h < v.numHarmonics; ++h)
    {
        const auto hs = static_cast<size_t> (h);
        float a = v.amp[hs];
        const float t = target[h];
        if (a == 0.0f && t == 0.0f)
            continue;
        const float da = (t - a) * invN;
        float re = v.re[hs], im = v.im[hs];
        const float cr = v.cr[hs], ci = v.ci[hs];
        for (int i = 0; i < n; ++i)
        {
            a += da;
            const float nr = re * cr - im * ci;
            im = re * ci + im * cr;
            re = nr;
            out[i] += a * im;
        }
        // Keep the rotator on the unit circle.
        const float k = 1.5f - 0.5f * (re * re + im * im);
        v.re[hs] = re * k;
        v.im[hs] = im * k;
        v.amp[hs] = t;
    }

    if (finished)
        v.active = false;
}

} // namespace pcs
