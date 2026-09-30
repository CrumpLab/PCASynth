#include "pcs/WaveModel.h"

#include "pcs/Harmonic.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace pcs {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr char kMagic[4] = { 'P', 'C', 'S', 'W' };
constexpr uint32_t kFormatVersion = 1;
constexpr double kTargetRms = 0.25; // the loudest 50 ms of every sound, normalised

// Band-limited resampling: `out[j]` = `x` at position `start + j * step`
// (input samples), windowed sinc (a polyphase table), low-passed below the
// output's Nyquist when reading faster than 1 input sample per output sample.
std::vector<float> resample (const std::vector<double>& x, double start, double step, size_t count)
{
    constexpr int kPhases = 512;
    const double cutoff = 0.97 * std::min (1.0, 1.0 / step);
    const int half = static_cast<int> (std::ceil (16.0 / cutoff));
    const int taps = 2 * half;
    // table[p][t]: the kernel at distance (p / kPhases) + half - 1 - t.
    std::vector<float> table (static_cast<size_t> ((kPhases + 1) * taps));
    for (int p = 0; p <= kPhases; ++p)
        for (int t = 0; t < taps; ++t)
        {
            const double d = static_cast<double> (p) / kPhases + half - 1 - t;
            const double arg = kPi * d * cutoff;
            const double sinc = std::abs (d) < 1e-9 ? 1.0 : std::sin (arg) / arg;
            const double w = std::abs (d) >= half ? 0.0 : 0.42 + 0.5 * std::cos (kPi * d / half) + 0.08 * std::cos (2.0 * kPi * d / half);
            table[static_cast<size_t> (p * taps + t)] = static_cast<float> (sinc * cutoff * w);
        }
    std::vector<float> out (count, 0.0f);
    for (size_t j = 0; j < count; ++j)
    {
        const double pos = start + static_cast<double> (j) * step;
        const auto base = static_cast<long> (std::floor (pos));
        const auto phase = static_cast<int> (std::lround ((pos - static_cast<double> (base)) * kPhases));
        const float* k = table.data() + static_cast<size_t> (phase * taps);
        const long first = base - half + 1;
        double sum = 0.0;
        if (first >= 0 && first + taps <= static_cast<long> (x.size()))
            for (int t = 0; t < taps; ++t)
                sum += x[static_cast<size_t> (first + t)] * k[t];
        else
            for (int t = 0; t < taps; ++t)
                if (first + t >= 0 && first + t < static_cast<long> (x.size()))
                    sum += x[static_cast<size_t> (first + t)] * k[t];
        out[j] = static_cast<float> (sum);
    }
    return out;
}

size_t onsetOf (const std::vector<double>& mono, const WaveSettings& s, double sampleRate)
{
    if (! s.trimOnset)
        return 0;
    double peak = 0.0;
    for (double v : mono)
        peak = std::max (peak, std::abs (v));
    const double threshold = peak * std::pow (10.0, s.onsetThresholdDb / 20.0);
    size_t start = 0;
    while (start < mono.size() && std::abs (mono[start]) < threshold)
        ++start;
    return start - std::min (start, static_cast<size_t> (0.001 * sampleRate)); // 1 ms before the onset
}

double dot (const float* a, const float* b, size_t n)
{
    double s = 0.0;
    for (size_t i = 0; i < n; ++i)
        s += static_cast<double> (a[i]) * b[i];
    return s;
}
} // namespace

double wavePitch (const AudioBuffer& audio, const WaveSettings& s)
{
    const auto mono = monoMix (audio);
    double peak = 0.0;
    for (double v : mono)
        peak = std::max (peak, std::abs (v));
    if (peak <= 0.0)
        throw std::runtime_error ("the sound is silent");
    const auto start = onsetOf (mono, s, audio.sampleRate);
    double nominal = midiToHz (s.midiNote);
    if (s.autoPitch)
    {
        const auto skip = static_cast<size_t> (0.05 * audio.sampleRate);
        nominal = detectPitch (mono, audio.sampleRate, start + skip < mono.size() / 2 ? start + skip : start);
        if (nominal <= 0.0)
            throw std::runtime_error ("no pitch found");
    }
    return estimateF0 (mono, audio.sampleRate, start, nominal, s.autoPitch ? 30.0 : s.tuneSearchCents, 32);
}

PreparedWave prepareWave (const AudioBuffer& audio, const WaveSettings& s, double targetHz,
                          const std::vector<float>& phaseReference, const std::string& name, const int* forcedShift)
{
    PreparedWave out;
    out.name = name;
    const auto mono = monoMix (audio);
    out.f0 = wavePitch (audio, s);
    const auto start = onsetOf (mono, s, audio.sampleRate);
    out.onsetSeconds = static_cast<double> (start) / audio.sampleRate;

    // Reading `speed` times faster brings the pitch to the target.
    const double speed = s.alignPitch ? targetHz / out.f0 : 1.0;
    const double step = speed * audio.sampleRate / s.sampleRate; // input samples per output sample
    const auto count = static_cast<size_t> (std::lround (s.duration * s.sampleRate));
    // With phase alignment the sound may start up to half a period earlier or later.
    const int period = static_cast<int> (std::lround (s.sampleRate / targetHz));
    const int margin = forcedShift != nullptr ? std::abs (*forcedShift) : s.alignPhase && ! phaseReference.empty() ? period / 2 + 1 : 0;
    const auto wide = resample (mono, static_cast<double> (start) - margin * step, step, count + 2 * static_cast<size_t> (margin));

    int shift = forcedShift != nullptr ? *forcedShift : 0;
    if (margin > 0 && forcedShift == nullptr)
    {
        // The lag that best matches the reference just after the attack.
        const auto a = std::min (count, static_cast<size_t> (0.03 * s.sampleRate));
        const auto b = std::min (count, static_cast<size_t> (0.28 * s.sampleRate));
        double best = -1e300;
        for (int lag = -margin; lag <= margin; ++lag)
        {
            double c = 0.0, e = 0.0;
            for (size_t i = a; i < b; ++i)
            {
                const float v = wide[static_cast<size_t> (static_cast<long> (i) + margin + lag)];
                c += v * phaseReference[i];
                e += v * v;
            }
            const double score = e > 0.0 ? c / std::sqrt (e) : -1e300;
            if (score > best)
            {
                best = score;
                shift = lag;
            }
        }
    }
    out.shift = shift;
    out.samples.assign (wide.begin() + margin + shift, wide.begin() + margin + shift + static_cast<long> (count));

    // Loudness: the loudest 50 ms at kTargetRms.
    if (s.normalizeLoudness)
    {
        const auto win = std::max<size_t> (1, static_cast<size_t> (0.05 * s.sampleRate));
        double loudest = 0.0, e = 0.0;
        for (size_t i = 0; i < out.samples.size(); ++i)
        {
            e += static_cast<double> (out.samples[i]) * out.samples[i];
            if (i >= win)
                e -= static_cast<double> (out.samples[i - win]) * out.samples[i - win];
            loudest = std::max (loudest, e / static_cast<double> (win));
        }
        if (loudest <= 0.0)
            throw std::runtime_error (name + " is silent");
        const double g = kTargetRms / std::sqrt (loudest);
        out.gainDb = 20.0 * std::log10 (g);
        for (auto& v : out.samples)
            v = static_cast<float> (v * g);
    }
    // Fade out at the end of the kept duration.
    const auto fade = std::min (count, static_cast<size_t> (s.fadeSeconds * s.sampleRate));
    for (size_t i = 0; i < fade; ++i)
        out.samples[count - 1 - i] *= static_cast<float> (static_cast<double> (i) / static_cast<double> (fade));
    return out;
}

WaveModel trainWaveModel (const std::vector<AudioBuffer>& sounds, const std::vector<std::string>& names,
                          const WaveSettings& settings, int maxComponents)
{
    if (sounds.size() < 2 || names.size() != sounds.size())
        throw std::invalid_argument ("training needs at least two sounds");
    WaveModel m;
    m.settings = settings;
    m.sampleRate = settings.sampleRate;

    std::vector<double> pitches;
    for (const auto& a : sounds)
        pitches.push_back (wavePitch (a, settings));
    if (settings.autoPitch)
    {
        auto logs = pitches;
        for (auto& p : logs)
            p = std::log (p);
        std::nth_element (logs.begin(), logs.begin() + static_cast<long> (logs.size() / 2), logs.end());
        m.refHz = std::exp (logs[logs.size() / 2]);
    }
    else
        m.refHz = midiToHz (settings.midiNote);

    // Each sound lined up with the mean of the ones before it.
    std::vector<std::vector<float>> rows;
    std::vector<double> running;
    std::vector<float> reference;
    for (size_t i = 0; i < sounds.size(); ++i)
    {
        auto p = prepareWave (sounds[i], settings, m.refHz, reference, names[i]);
        if (running.empty())
            running.assign (p.samples.size(), 0.0);
        for (size_t k = 0; k < p.samples.size(); ++k)
            running[k] += p.samples[k];
        reference.assign (running.begin(), running.end());
        m.names.push_back (names[i]);
        m.f0s.push_back (p.f0);
        m.gainsDb.push_back (p.gainDb);
        m.shifts.push_back (p.shift);
        rows.push_back (std::move (p.samples));
    }
    m.numSamples = static_cast<int> (rows[0].size());
    m.pca = computePca (rows, std::min (maxComponents, kMaxComponents));

    // Fit report: each sound rebuilt from the mean plus one component at a
    // time; the residual's level below the sound (dB).
    const int k = m.numComponents();
    const auto d = static_cast<size_t> (m.numSamples);
    m.fitByComponents.assign (static_cast<size_t> (k + 1), 0.0f);
    m.fitErrorDb.assign (rows.size(), 0.0f);
    std::vector<double> residual (d);
    for (size_t r = 0; r < rows.size(); ++r)
    {
        const double energy = std::max (1e-20, dot (rows[r].data(), rows[r].data(), d));
        for (size_t i = 0; i < d; ++i)
            residual[i] = rows[r][i] - m.pca.mean[i];
        for (int j = 0; j <= k; ++j)
        {
            if (j > 0)
            {
                const auto score = m.pca.score (static_cast<int> (r), j - 1);
                const float* c = m.pca.component (j - 1);
                for (size_t i = 0; i < d; ++i)
                    residual[i] -= score * c[i];
            }
            double e = 0.0;
            for (double v : residual)
                e += v * v;
            const auto err = static_cast<float> (std::max (-100.0, 10.0 * std::log10 (std::max (e, 1e-30) / energy)));
            m.fitByComponents[static_cast<size_t> (j)] += err / static_cast<float> (rows.size());
            if (j == k)
                m.fitErrorDb[r] = err;
        }
    }
    m.finalize();
    return m;
}

WaveModel singleWaveModel (const PreparedWave& sound, const WaveModel& like)
{
    WaveModel m;
    m.settings = like.settings;
    m.sampleRate = like.sampleRate;
    m.refHz = like.refHz;
    m.numSamples = static_cast<int> (sound.samples.size());
    m.names = { sound.name };
    m.f0s = { sound.f0 };
    m.gainsDb = { sound.gainDb };
    m.shifts = { sound.shift };
    m.pca.numRows = 1;
    m.pca.dims = m.numSamples;
    m.pca.mean = sound.samples;
    m.pca.totalVariance = 1.0;
    m.finalize();
    return m;
}

void WaveModel::finalize()
{
    const int k = numComponents();
    const auto w = static_cast<size_t> (std::min<double> (numSamples, kLevelSeconds * sampleRate));
    levelGram.assign (static_cast<size_t> (k * k), 0.0);
    levelLinear.assign (static_cast<size_t> (k), 0.0);
    levelConst = dot (pca.mean.data(), pca.mean.data(), w);
    for (int a = 0; a < k; ++a)
    {
        levelLinear[static_cast<size_t> (a)] = sd (a) * dot (pca.component (a), pca.mean.data(), w);
        for (int b = a; b < k; ++b)
            levelGram[static_cast<size_t> (a * k + b)] = levelGram[static_cast<size_t> (b * k + a)]
                = sd (a) * sd (b) * dot (pca.component (a), pca.component (b), w);
    }
    double sum = 0.0;
    for (int i = 0; i < numSounds(); ++i)
    {
        const auto z = soundZ (i);
        sum += levelDb (z.data(), static_cast<int> (z.size()));
    }
    refLevelDb = numSounds() > 0 ? static_cast<float> (sum / numSounds()) : 0.0f;
}

float WaveModel::levelDb (const float* z, int numZ) const noexcept
{
    const int k = std::min (numZ, numComponents());
    double e = levelConst;
    for (int a = 0; a < k; ++a)
    {
        if (z[a] == 0.0f)
            continue;
        e += 2.0 * z[a] * levelLinear[static_cast<size_t> (a)];
        for (int b = 0; b < k; ++b)
            e += static_cast<double> (z[a]) * z[b] * levelGram[static_cast<size_t> (a * numComponents() + b)];
    }
    const double w = std::max (1.0, std::min<double> (numSamples, kLevelSeconds * sampleRate));
    return static_cast<float> (10.0 * std::log10 (std::max (e / w, 1e-20)));
}

std::vector<float> WaveModel::decode (const float* z, int numZ) const
{
    std::vector<float> out (pca.mean);
    for (int j = 0; j < std::min (numZ, numComponents()); ++j)
    {
        if (z[j] == 0.0f)
            continue;
        const auto g = static_cast<float> (z[j] * sd (j));
        const float* c = pca.component (j);
        for (size_t i = 0; i < out.size(); ++i)
            out[i] += g * c[i];
    }
    return out;
}

std::vector<double> WaveModel::mixWeights (const float* z, int numZ) const
{
    // The point is mean + Σ z_j sd_j c_j, and each component is a combination
    // of the centred training rows: c_j = Σ_i s_ij (x_i - mean) / Σ_i s_ij².
    const int n = numSounds();
    std::vector<double> v (static_cast<size_t> (n), 0.0);
    for (int j = 0; j < std::min (numZ, numComponents()); ++j)
    {
        double ss = 0.0;
        for (int i = 0; i < n; ++i)
            ss += pca.score (i, j) * pca.score (i, j);
        if (ss <= 0.0)
            continue;
        for (int i = 0; i < n; ++i)
            v[static_cast<size_t> (i)] += z[j] * sd (j) * pca.score (i, j) / ss;
    }
    double sum = 0.0;
    for (double x : v)
        sum += x;
    for (auto& x : v)
        x += (1.0 - sum) / n;
    return v;
}

// ---- file -------------------------------------------------------------------------

namespace {
void putU32 (std::vector<uint8_t>& v, uint32_t x)
{
    for (int i = 0; i < 4; ++i)
        v.push_back (static_cast<uint8_t> ((x >> (8 * i)) & 0xff));
}
uint32_t getU32 (const uint8_t* p)
{
    return static_cast<uint32_t> (p[0]) | (static_cast<uint32_t> (p[1]) << 8) | (static_cast<uint32_t> (p[2]) << 16)
         | (static_cast<uint32_t> (p[3]) << 24);
}
void putFloats (std::vector<uint8_t>& v, const std::vector<float>& f)
{
    const auto* p = reinterpret_cast<const uint8_t*> (f.data());
    v.insert (v.end(), p, p + 4 * f.size()); // little-endian hosts only (x86, arm64)
}
std::vector<float> getFloats (const uint8_t* p, size_t count)
{
    std::vector<float> f (count);
    std::memcpy (f.data(), p, 4 * count);
    return f;
}
} // namespace

std::vector<uint8_t> serializeWaveModel (const WaveModel& m)
{
    nlohmann::json j;
    j["format"] = "pcasynth-wave-model";
    j["version"] = kFormatVersion;
    j["title"] = m.title;
    const auto& s = m.settings;
    j["settings"] = { { "midiNote", s.midiNote }, { "autoPitch", s.autoPitch }, { "tuneSearchCents", s.tuneSearchCents },
                      { "duration", s.duration }, { "sampleRate", s.sampleRate }, { "alignPitch", s.alignPitch },
                      { "trimOnset", s.trimOnset }, { "onsetThresholdDb", s.onsetThresholdDb }, { "alignPhase", s.alignPhase },
                      { "normalizeLoudness", s.normalizeLoudness }, { "fadeSeconds", s.fadeSeconds } };
    j["sampleRate"] = m.sampleRate;
    j["numSamples"] = m.numSamples;
    j["refHz"] = m.refHz;
    j["names"] = m.names;
    j["f0s"] = m.f0s;
    j["gainsDb"] = m.gainsDb;
    j["shifts"] = m.shifts;
    j["numComponents"] = m.numComponents();
    j["totalVariance"] = m.pca.totalVariance;
    j["variance"] = m.pca.variance;
    j["scores"] = m.pca.scores;
    j["fit"] = { { "errorDb", m.fitErrorDb }, { "byComponents", m.fitByComponents } };
    std::string header = j.dump();
    while (header.size() % 4 != 0)
        header.push_back (' ');
    std::vector<uint8_t> out (kMagic, kMagic + 4);
    putU32 (out, kFormatVersion);
    putU32 (out, static_cast<uint32_t> (header.size()));
    out.insert (out.end(), header.begin(), header.end());
    putFloats (out, m.pca.mean);
    putFloats (out, m.pca.components);
    return out;
}

WaveModel deserializeWaveModel (const uint8_t* data, size_t size)
{
    if (size < 12 || std::memcmp (data, kMagic, 4) != 0)
        throw std::runtime_error ("not a PCAWave model");
    if (getU32 (data + 4) > kFormatVersion)
        throw std::runtime_error ("model was written by a newer PCAWave");
    const size_t headerSize = getU32 (data + 8);
    if (12 + headerSize > size)
        throw std::runtime_error ("model file is truncated");
    WaveModel m;
    try
    {
        const auto j = nlohmann::json::parse (data + 12, data + 12 + headerSize);
        m.title = j.value ("title", std::string());
        const auto& js = j.at ("settings");
        auto& s = m.settings;
        s.midiNote = js.value ("midiNote", s.midiNote);
        s.autoPitch = js.value ("autoPitch", s.autoPitch);
        s.tuneSearchCents = js.value ("tuneSearchCents", s.tuneSearchCents);
        s.duration = js.value ("duration", s.duration);
        s.sampleRate = js.value ("sampleRate", s.sampleRate);
        s.alignPitch = js.value ("alignPitch", s.alignPitch);
        s.trimOnset = js.value ("trimOnset", s.trimOnset);
        s.onsetThresholdDb = js.value ("onsetThresholdDb", s.onsetThresholdDb);
        s.alignPhase = js.value ("alignPhase", s.alignPhase);
        s.normalizeLoudness = js.value ("normalizeLoudness", s.normalizeLoudness);
        s.fadeSeconds = js.value ("fadeSeconds", s.fadeSeconds);
        m.sampleRate = j.at ("sampleRate");
        m.numSamples = j.at ("numSamples");
        m.refHz = j.at ("refHz");
        m.names = j.at ("names").get<std::vector<std::string>>();
        m.f0s = j.at ("f0s").get<std::vector<double>>();
        m.gainsDb = j.at ("gainsDb").get<std::vector<double>>();
        m.shifts = j.value ("shifts", std::vector<int> (m.names.size(), 0));
        m.pca.numComponents = j.at ("numComponents");
        m.pca.totalVariance = j.at ("totalVariance");
        m.pca.variance = j.at ("variance").get<std::vector<double>>();
        m.pca.scores = j.at ("scores").get<std::vector<double>>();
        if (j.contains ("fit"))
        {
            m.fitErrorDb = j.at ("fit").value ("errorDb", std::vector<float>());
            m.fitByComponents = j.at ("fit").value ("byComponents", std::vector<float>());
        }
    }
    catch (const nlohmann::json::exception& e)
    {
        throw std::runtime_error (std::string ("bad model header: ") + e.what());
    }
    const auto d = static_cast<size_t> (m.numSamples);
    const auto k = static_cast<size_t> (m.pca.numComponents);
    if (m.numSamples <= 0 || m.sampleRate <= 0.0 || m.refHz <= 0.0 || k > static_cast<size_t> (kMaxComponents)
        || m.pca.variance.size() != k || m.pca.scores.size() != k * m.names.size() || m.f0s.size() != m.names.size())
        throw std::runtime_error ("model header is inconsistent");
    m.pca.numRows = static_cast<int> (m.names.size());
    m.pca.dims = m.numSamples;
    const size_t offset = 12 + headerSize;
    if (size - offset < 4 * d * (1 + k))
        throw std::runtime_error ("model file is truncated");
    m.pca.mean = getFloats (data + offset, d);
    m.pca.components = getFloats (data + offset + 4 * d, d * k);
    m.finalize();
    return m;
}

} // namespace pcs
