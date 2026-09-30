#include "pcs/Inspect.h"

#include "pcs/Fft.h"
#include "pcs/Synth.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace pcs {

namespace {
constexpr double kPi = 3.14159265358979323846;

double rms (const std::vector<float>& x)
{
    double s = 0.0;
    for (float v : x)
        s += static_cast<double> (v) * v;
    return x.empty() ? 0.0 : std::sqrt (s / static_cast<double> (x.size()));
}

AudioBuffer monoBuffer (std::vector<float> samples, double sampleRate)
{
    AudioBuffer b;
    b.sampleRate = sampleRate;
    b.channels.push_back (std::move (samples));
    return b;
}
} // namespace

double Spectrogram::bandHz (int b) const { return loHz * std::pow (2.0, static_cast<double> (b) / bandsPerOctave); }

Spectrogram spectrogram (const std::vector<float>& mono, double sampleRate, double frameRate)
{
    Spectrogram s;
    s.frameRate = frameRate;
    const double top = std::min (20000.0, 0.48 * sampleRate);
    s.numBands = std::max (1, static_cast<int> (std::floor (s.bandsPerOctave * std::log2 (top / s.loHz))));
    const double hop = sampleRate / frameRate;
    s.numFrames = std::max (1, static_cast<int> (std::ceil (static_cast<double> (mono.size()) / hop)));
    s.db.assign (static_cast<size_t> (s.numFrames * s.numBands), -200.0f);

    const int n = nextPowerOfTwo (static_cast<int> (0.04 * sampleRate)); // ~43 ms Hann: fine enough in time and frequency
    std::vector<double> window (static_cast<size_t> (n));
    double windowSq = 0.0;
    for (int i = 0; i < n; ++i)
    {
        window[static_cast<size_t> (i)] = 0.5 - 0.5 * std::cos (2.0 * kPi * i / n);
        windowSq += window[static_cast<size_t> (i)] * window[static_cast<size_t> (i)];
    }
    Fft fft (n);
    std::vector<double> re (static_cast<size_t> (n)), im (static_cast<size_t> (n)), power (static_cast<size_t> (n / 2 + 1));
    const double binHz = sampleRate / n;
    for (int t = 0; t < s.numFrames; ++t)
    {
        const auto first = static_cast<long> (std::lround (t * hop)) - n / 2;
        for (int i = 0; i < n; ++i)
        {
            const long j = first + i;
            re[static_cast<size_t> (i)] = j >= 0 && j < static_cast<long> (mono.size()) ? mono[static_cast<size_t> (j)] * window[static_cast<size_t> (i)] : 0.0;
            im[static_cast<size_t> (i)] = 0.0;
        }
        fft.forward (re.data(), im.data());
        for (size_t k = 0; k < power.size(); ++k)
            power[k] = (re[k] * re[k] + im[k] * im[k]) / windowSq;
        for (int b = 0; b < s.numBands; ++b)
        {
            const double centre = s.bandHz (b), edge = std::pow (2.0, 0.5 / s.bandsPerOctave);
            const auto k0 = static_cast<long> (std::ceil (centre / edge / binHz));
            const auto k1 = static_cast<long> (std::floor (centre * edge / binHz));
            double p = 0.0;
            if (k1 >= k0)
                for (long k = std::max (1L, k0); k <= std::min (k1, static_cast<long> (power.size()) - 1); ++k)
                    p += power[static_cast<size_t> (k)];
            else // a band narrower than a bin: its share of the nearest bin
                p = power[static_cast<size_t> (std::clamp (std::lround (centre / binHz), 1L, static_cast<long> (power.size()) - 1))]
                  * (centre * (edge - 1.0 / edge)) / binHz;
            s.db[static_cast<size_t> (t * s.numBands + b)] = static_cast<float> (10.0 * std::log10 (std::max (p, 1e-20)));
        }
    }
    return s;
}

double spectralDistance (const Spectrogram& a, const Spectrogram& b, double fromSeconds, double toSeconds)
{
    const int bands = std::min (a.numBands, b.numBands);
    const int frames = std::min (a.numFrames, b.numFrames);
    const int t0 = std::clamp (static_cast<int> (std::lround (fromSeconds * a.frameRate)), 0, frames);
    const int t1 = toSeconds < 0.0 ? frames : std::clamp (static_cast<int> (std::lround (toSeconds * a.frameRate)), t0, frames);
    float peakA = -200.0f, peakB = -200.0f;
    for (float v : a.db)
        peakA = std::max (peakA, v);
    for (float v : b.db)
        peakB = std::max (peakB, v);
    double sum = 0.0;
    long cells = 0;
    for (int t = t0; t < t1; ++t)
        for (int k = 0; k < bands; ++k)
        {
            const float x = std::max (a.at (t, k) - peakA, -60.0f), y = std::max (b.at (t, k) - peakB, -60.0f);
            if (x <= -60.0f && y <= -60.0f)
                continue;
            sum += static_cast<double> (x - y) * (x - y);
            ++cells;
        }
    return cells > 0 ? std::sqrt (sum / static_cast<double> (cells)) : 0.0;
}

double envelopeDistance (const HarmonicSound& reference, const HarmonicSound& other)
{
    const int frames = std::min (reference.numFrames, other.numFrames);
    const int harmonics = std::min (reference.numHarmonics, other.numHarmonics);
    float peak = -300.0f;
    for (float v : reference.db)
        peak = std::max (peak, v);
    const float floor = peak - 60.0f;
    double sum = 0.0;
    long cells = 0;
    for (int t = 0; t < frames; ++t)
        for (int h = 0; h < harmonics; ++h)
        {
            const float x = reference.at (t, h);
            if (x <= floor)
                continue;
            const float d = x - std::max (other.at (t, h), floor);
            sum += static_cast<double> (d) * d;
            ++cells;
        }
    return cells > 0 ? std::sqrt (sum / static_cast<double> (cells)) : 0.0;
}

AudioBuffer renderPoint (const Model& model, const std::vector<float>& z, double midiPitch, double sampleRate)
{
    Synth synth;
    synth.prepare (sampleRate);
    SynthParams p;
    for (size_t j = 0; j < z.size() && j < p.z.size(); ++j)
        p.z[j] = z[j];
    p.mode = PlayMode::OneShot;
    p.attack = 0.0005f;
    p.release = 0.02f;
    p.gainDb = 0.0f;
    p.velocitySensitivity = 0.0f;
    p.pitchBendRange = 2.0f;
    p.morphTime = 0.0f;
    p.polyphony = 1;
    p.noiseDb = 0.0f;
    p.levelLock = 0.0f;
    synth.setParams (p);
    synth.setModel (std::make_shared<const Model> (model));

    const int note = static_cast<int> (std::lround (midiPitch));
    MidiEvent events[2];
    events[0].type = MidiEvent::Type::PitchBend;
    events[0].value = static_cast<float> ((midiPitch - note) / p.pitchBendRange);
    events[1].type = MidiEvent::Type::NoteOn;
    events[1].note = note;
    events[1].value = 1.0f;

    const auto total = static_cast<size_t> ((model.durationSeconds() + 0.05) * sampleRate);
    std::vector<float> out (total);
    const int block = 256;
    for (size_t done = 0; done < total; done += block)
    {
        const int n = static_cast<int> (std::min<size_t> (block, total - done));
        float* ch[] = { out.data() + done };
        synth.process (ch, 1, n, done == 0 ? events : nullptr, done == 0 ? 2 : 0);
    }
    return monoBuffer (std::move (out), sampleRate);
}

SoundInspection inspectSound (const Model& model, int index, const AudioBuffer& file, const InspectOptions& options)
{
    if (index < 0 || index >= model.numSounds())
        throw std::runtime_error ("no such training sound");
    SoundInspection r;
    r.name = model.names[static_cast<size_t> (index)];
    r.analysed = analyseHarmonics (file, model.analysis, r.name);
    if (r.analysed.numFrames != model.numFrames || r.analysed.numHarmonics != model.numHarmonics
        || r.analysed.numNoiseBands != model.numNoiseBands)
        throw std::runtime_error (r.name + ": the file no longer analyses to the model's shape");
    r.pitch = r.analysed.midiPitch();
    const double sr = file.sampleRate;

    // The original, from where analysis starts, for the same length as the renders.
    const auto length = static_cast<size_t> ((model.durationSeconds() + 0.05) * sr);
    const auto mono = monoMix (file);
    std::vector<float> original (length, 0.0f);
    const auto start = static_cast<size_t> (std::lround (r.analysed.onsetSeconds * sr));
    const auto fade = static_cast<size_t> (0.02 * sr);
    const size_t playable = static_cast<size_t> (model.durationSeconds() * sr);
    for (size_t i = 0; i < playable && start + i < mono.size(); ++i)
    {
        const double g = i + fade > playable ? static_cast<double> (playable - i) / static_cast<double> (fade) : 1.0;
        original[i] = static_cast<float> (mono[start + i] * g);
    }

    // The analysis on its own, and the sound's point in the model.
    r.analysis = renderPoint (singleSoundModel (r.analysed, model.analysis), {}, r.pitch, sr);
    auto z = model.soundZ (index);
    if (options.components >= 0 && options.components < static_cast<int> (z.size()))
        z.resize (static_cast<size_t> (options.components));
    r.model = renderPoint (model, z, r.pitch, sr);

    // Loudness-matched to the original, for listening side by side.
    const double target = rms (original);
    for (auto* b : { &r.analysis, &r.model })
    {
        auto& x = b->channels[0];
        x.resize (length, 0.0f);
        const double level = rms (x);
        if (level > 0.0 && target > 0.0)
            for (auto& v : x)
                v = static_cast<float> (v * target / level);
    }
    r.original = monoBuffer (std::move (original), sr);

    r.specOriginal = spectrogram (r.original.channels[0], sr);
    r.specAnalysis = spectrogram (r.analysis.channels[0], sr);
    r.specModel = spectrogram (r.model.channels[0], sr);
    const double attack = options.attackSeconds;
    r.analysisError = spectralDistance (r.specOriginal, r.specAnalysis);
    r.analysisAttackError = spectralDistance (r.specOriginal, r.specAnalysis, 0.0, attack);
    r.modelError = spectralDistance (r.specOriginal, r.specModel);
    r.modelAttackError = spectralDistance (r.specOriginal, r.specModel, 0.0, attack);
    r.pcaError = spectralDistance (r.specAnalysis, r.specModel);

    const float delta = model.pitchDelta (r.pitch);
    const auto decoded = model.decode (z, delta);
    r.envelopeError = envelopeDistance (r.analysed, decoded);
    r.pitchCents = r.analysed.pitchCents;
    r.modelPitchCents = decoded.pitchCents;
    return r;
}

} // namespace pcs
