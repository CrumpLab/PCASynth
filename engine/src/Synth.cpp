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
        for (int i = 0; i < model->numSounds(); ++i)
        {
            Point z {};
            const auto zs = model->soundZ (i);
            std::copy (zs.begin(), zs.end(), z.begin());
            slot->soundZ.push_back (z);
        }
        for (int j = 0; j < model->numComponents(); ++j)
            slot->relSd[static_cast<size_t> (j)] = static_cast<float> (model->sd (j) / model->sd (0));
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
    heard = zSmooth;
    walkSeed = params.mod.walk.seed;
    walk.reset (walkSeed);
    walkMix = params.mod.walk.enabled ? 1.0f : 0.0f;
    for (size_t i = 0; i < lfos.size(); ++i)
        lfos[i].reset (walkSeed + 101u * static_cast<uint32_t> (i + 1));
    modWheel = pressure = 0.0f;
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

void Synth::noteOn (int note, float velocity, int channel) noexcept
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
    v.freq = midiToHz (v.note + bend * params.pitchBendRange + v.noteBend * params.mpe.noteBendRange);
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
        m.decodeFrame (t, heard.data(), kMaxComponents, scratchDb.data());
        const float floorLin = std::pow (10.0f, m.floorDb / 20.0f);
        for (int h = 0; h < m.numHarmonics; ++h)
            dst[h] = std::max (0.0f, std::pow (10.0f, scratchDb[static_cast<size_t> (h)] / 20.0f) - floorLin);
        current->stamps[static_cast<size_t> (t)] = stamp;
    }
    return dst;
}

void Synth::frameAt (const Voice& v, double pos, float* dst) noexcept
{
    const auto& m = *current->model;
    const int h = m.numHarmonics;
    const int t0 = std::clamp (static_cast<int> (std::floor (pos)), 0, m.numFrames - 1);
    const int t1 = std::min (t0 + 1, m.numFrames - 1);
    const auto frac = static_cast<float> (pos - std::floor (pos));
    if (! v.ownPoint)
    {
        const float* a = frame (t0);
        const float* b = frame (t1);
        for (int i = 0; i < h; ++i)
            dst[i] = a[i] + frac * (b[i] - a[i]);
        return;
    }
    // The voice's own point: decode both frames for it (no cache).
    const float floorLin = std::pow (10.0f, m.floorDb / 20.0f);
    m.decodeFrame (t0, v.point.data(), kMaxComponents, scratchDb.data());
    m.decodeFrame (t1, v.point.data(), kMaxComponents, scratchC.data());
    for (int i = 0; i < h; ++i)
    {
        const float a = std::max (0.0f, std::pow (10.0f, scratchDb[static_cast<size_t> (i)] / 20.0f) - floorLin);
        const float b = std::max (0.0f, std::pow (10.0f, scratchC[static_cast<size_t> (i)] / 20.0f) - floorLin);
        dst[i] = a + frac * (b - a);
    }
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
    for (auto& v : voices)
    {
        if (! v.active)
            continue;
        v.ownPoint = perVoice;
        if (! perVoice)
            continue;
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
    modulate (n);

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
    frameAt (v, v.pos, target);
    if (crossfade)
    {
        frameAt (v, v.pos - loopLen, scratchB.data());
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
