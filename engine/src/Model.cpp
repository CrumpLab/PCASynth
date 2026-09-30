#include "pcs/Model.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace pcs {

namespace {
constexpr char kMagic[4] = { 'P', 'C', 'S', 'M' };
constexpr uint32_t kFormatVersion = 3; // 2: Stage 7 sections (noise, partials, representation, pitch); 3: pitch curve, fit report
constexpr float kCeilingDb = 12.0f;

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
    for (float x : f)
    {
        uint32_t u;
        std::memcpy (&u, &x, 4);
        putU32 (v, u);
    }
}
std::vector<float> getFloats (const uint8_t* p, size_t count)
{
    std::vector<float> f (count);
    for (size_t i = 0; i < count; ++i)
    {
        const uint32_t u = getU32 (p + 4 * i);
        std::memcpy (&f[i], &u, 4);
    }
    return f;
}

const char* representationName (Representation r)
{
    return r == Representation::ShapeLoudness ? "shape+loudness" : r == Representation::Linear ? "linear" : "dB";
}
Representation representationFrom (const std::string& s)
{
    return s == "shape+loudness" ? Representation::ShapeLoudness : s == "linear" ? Representation::Linear : Representation::Decibels;
}
const char* pitchTrackingName (PitchTracking p) { return p == PitchTracking::Off ? "off" : p == PitchTracking::On ? "on" : "auto"; }
PitchTracking pitchTrackingFrom (const std::string& s)
{
    return s == "off" ? PitchTracking::Off : s == "on" ? PitchTracking::On : PitchTracking::Auto;
}

nlohmann::json settingsToJson (const AnalysisSettings& s)
{
    return { { "midiNote", s.midiNote },           { "autoPitch", s.autoPitch },
             { "tuneSearchCents", s.tuneSearchCents },
             { "duration", s.duration },           { "frameRate", s.frameRate },
             { "harmonics", s.harmonics },         { "floorDb", s.floorDb },
             { "periodsPerWindow", s.periodsPerWindow }, { "trimOnset", s.trimOnset },
             { "onsetThresholdDb", s.onsetThresholdDb }, { "normalizeLoudness", s.normalizeLoudness },
             { "noiseBands", s.noiseBands }, { "trackPartials", s.trackPartials },
             { "representation", representationName (s.representation) },
             { "pitchTracking", pitchTrackingName (s.pitchTracking) },
             { "trackPitch", s.trackPitch }, { "sharpAttacks", s.sharpAttacks }, { "attackSeconds", s.attackSeconds } };
}
AnalysisSettings settingsFromJson (const nlohmann::json& j)
{
    AnalysisSettings s;
    s.midiNote = j.value ("midiNote", s.midiNote);
    s.autoPitch = j.value ("autoPitch", s.autoPitch);
    s.tuneSearchCents = j.value ("tuneSearchCents", s.tuneSearchCents);
    s.duration = j.value ("duration", s.duration);
    s.frameRate = j.value ("frameRate", s.frameRate);
    s.harmonics = j.value ("harmonics", s.harmonics);
    s.floorDb = j.value ("floorDb", s.floorDb);
    s.periodsPerWindow = j.value ("periodsPerWindow", s.periodsPerWindow);
    s.trimOnset = j.value ("trimOnset", s.trimOnset);
    s.onsetThresholdDb = j.value ("onsetThresholdDb", s.onsetThresholdDb);
    s.normalizeLoudness = j.value ("normalizeLoudness", s.normalizeLoudness);
    s.noiseBands = j.value ("noiseBands", 0);        // absent in version 1: none
    s.trackPartials = j.value ("trackPartials", false);
    s.representation = representationFrom (j.value ("representation", std::string ("dB")));
    s.pitchTracking = pitchTrackingFrom (j.value ("pitchTracking", std::string ("off")));
    s.trackPitch = j.value ("trackPitch", false); // absent before version 3: how those models were analysed
    s.sharpAttacks = j.value ("sharpAttacks", false);
    s.attackSeconds = j.value ("attackSeconds", s.attackSeconds);
    return s;
}
} // namespace

namespace {
constexpr float kLinearScale = 100.0f; // Linear representation: full scale ~ 100, like dB values

void setLayout (Model& m, const AnalysisSettings& s, int frames, int harmonics, int noiseBands, bool partials, bool pitchCurve)
{
    m.numFrames = frames;
    m.numHarmonics = harmonics;
    m.numNoiseBands = noiseBands;
    m.hasPartials = partials;
    m.hasPitchCurve = pitchCurve;
    // 10 cents of pitch in a frame counts like 1 dB on every harmonic of that frame.
    m.pitchCurveWeight = 0.1f * std::sqrt (static_cast<float> (harmonics));
    m.representation = s.representation;
    // Loudness counts like a shift of every band; 10 cents like 1 dB held for the whole note.
    m.loudnessWeight = std::sqrt (static_cast<float> (harmonics + noiseBands));
    m.partialWeight = 0.1f * std::sqrt (static_cast<float> (frames));
    m.floorDb = static_cast<float> (s.floorDb);
}

// The loudest noise band in any frame of any sound, + 6 dB: noise decoded far
// outside the training set (walks, LFOs, extremes) stops there.
float noiseCeiling (const std::vector<const HarmonicSound*>& sounds, float floorDb)
{
    float top = floorDb;
    for (const auto* s : sounds)
        for (float v : s->noiseDb)
            top = std::max (top, v);
    return top + 6.0f;
}
} // namespace

namespace {
// The fit report: each training row rebuilt from the mean plus one component
// at a time, compared on the harmonic levels (dB) where the sound is within 60
// dB of its loudest. Rows are the vectors the PCA saw (pitch direction removed).
void fitReport (Model& m, const std::vector<std::vector<float>>& rows)
{
    const int k = m.numComponents(), frames = m.numFrames, harmonics = m.numHarmonics;
    const float floorLin = std::pow (10.0f, m.floorDb / 20.0f);
    auto toDb = [&] (const std::vector<float>& v, int t, int h) {
        const float x = v[static_cast<size_t> (t * harmonics + h)];
        switch (m.representation)
        {
            case Representation::Decibels: return std::max (x, m.floorDb);
            case Representation::ShapeLoudness:
                return std::max (x + v[static_cast<size_t> (m.loudnessOffset() + t)] / m.loudnessWeight, m.floorDb);
            case Representation::Linear: return std::max (20.0f * std::log10 (std::max (x / kLinearScale + floorLin, 1e-12f)), m.floorDb);
        }
        return x;
    };
    m.fitByComponents.assign (static_cast<size_t> (k + 1), 0.0f);
    m.fitErrorDb.assign (rows.size(), 0.0f);
    std::vector<float> recon;
    for (size_t r = 0; r < rows.size(); ++r)
    {
        const auto& row = rows[r];
        float peak = m.floorDb;
        for (int t = 0; t < frames; ++t)
            for (int h = 0; h < harmonics; ++h)
                peak = std::max (peak, toDb (row, t, h));
        const float floor = peak - 60.0f;
        std::vector<float> original (static_cast<size_t> (frames * harmonics));
        for (int t = 0; t < frames; ++t)
            for (int h = 0; h < harmonics; ++h)
                original[static_cast<size_t> (t * harmonics + h)] = toDb (row, t, h);
        recon = m.pca.mean;
        for (int j = 0; j <= k; ++j)
        {
            if (j > 0)
            {
                const auto score = static_cast<float> (m.pca.score (static_cast<int> (r), j - 1));
                const float* c = m.pca.component (j - 1);
                for (size_t i = 0; i < recon.size(); ++i)
                    recon[i] += score * c[i];
            }
            double sum = 0.0;
            long cells = 0;
            for (int t = 0; t < frames; ++t)
                for (int h = 0; h < harmonics; ++h)
                {
                    const float x = original[static_cast<size_t> (t * harmonics + h)];
                    if (x <= floor)
                        continue;
                    const float d = x - std::max (toDb (recon, t, h), floor);
                    sum += static_cast<double> (d) * d;
                    ++cells;
                }
            const auto err = static_cast<float> (cells > 0 ? std::sqrt (sum / static_cast<double> (cells)) : 0.0);
            m.fitByComponents[static_cast<size_t> (j)] += err / static_cast<float> (rows.size());
            if (j == k)
                m.fitErrorDb[r] = err;
        }
    }
}
} // namespace

int Model::soundIndex (const std::string& name) const
{
    const auto it = std::find (names.begin(), names.end(), name);
    return it == names.end() ? -1 : static_cast<int> (it - names.begin());
}

float Model::pitchDelta (double midiNote, float keytrack) const noexcept
{
    if (! pitchTracking)
        return 0.0f;
    const double d = std::clamp (midiNote, pitchMin - 12.0, pitchMax + 12.0) - pitchRef;
    return static_cast<float> (d) * keytrack;
}

std::vector<float> Model::encode (const HarmonicSound& s) const
{
    if (s.numFrames != numFrames || s.numHarmonics != numHarmonics || s.numNoiseBands != numNoiseBands)
        throw std::invalid_argument ("sound shape does not match the model");
    std::vector<float> v (static_cast<size_t> (dims()), 0.0f);
    const float floorLin = std::pow (10.0f, floorDb / 20.0f);
    auto level = [&] (float db, float loudness) {
        switch (representation)
        {
            case Representation::Decibels: return std::max (db, floorDb);
            case Representation::ShapeLoudness: return std::max (db, floorDb) - loudness;
            case Representation::Linear: return std::max (0.0f, std::pow (10.0f, db / 20.0f) - floorLin) * kLinearScale;
        }
        return db;
    };
    for (int t = 0; t < numFrames; ++t)
    {
        float loudness = 0.0f;
        if (representation == Representation::ShapeLoudness)
        {
            double e = 0.0;
            for (int h = 0; h < numHarmonics; ++h)
                e += std::pow (10.0, s.at (t, h) / 10.0);
            for (int b = 0; b < numNoiseBands; ++b)
                e += 2.0 * std::pow (10.0, s.noiseAt (t, b) / 10.0);
            loudness = std::max (floorDb, static_cast<float> (10.0 * std::log10 (std::max (e, 1e-30))));
            v[static_cast<size_t> (loudnessOffset() + t)] = loudness * loudnessWeight;
        }
        for (int h = 0; h < numHarmonics; ++h)
            v[static_cast<size_t> (t * numHarmonics + h)] = level (s.at (t, h), loudness);
        for (int b = 0; b < numNoiseBands; ++b)
            v[static_cast<size_t> (noiseOffset() + t * numNoiseBands + b)] = level (s.noiseAt (t, b), loudness);
    }
    if (hasPartials)
        for (int h = 0; h < numHarmonics; ++h)
            v[static_cast<size_t> (partialOffset() + h)] =
                (h < static_cast<int> (s.partialCents.size()) ? s.partialCents[static_cast<size_t> (h)] : 0.0f) * partialWeight;
    if (hasPitchCurve)
        for (int t = 0; t < numFrames; ++t)
            v[static_cast<size_t> (pitchCurveOffset() + t)] =
                (t < static_cast<int> (s.pitchCents.size()) ? s.pitchCents[static_cast<size_t> (t)] : 0.0f) * pitchCurveWeight;
    return v;
}

std::vector<float> Model::soundZ (int i) const
{
    std::vector<float> z (static_cast<size_t> (numComponents()));
    for (int j = 0; j < numComponents(); ++j)
        z[static_cast<size_t> (j)] = static_cast<float> (pca.score (i, j) / sd (j));
    return z;
}

std::vector<float> Model::project (const HarmonicSound& sound) const
{
    auto v = encode (sound);
    if (pitchTracking)
    {
        const auto d = static_cast<float> (sound.midiPitch() - pitchRef);
        for (size_t i = 0; i < v.size(); ++i)
            v[i] -= d * pitchSlope[i];
    }
    std::vector<float> z (static_cast<size_t> (numComponents()));
    for (int j = 0; j < numComponents(); ++j)
    {
        const float* c = pca.component (j);
        double dot = 0.0;
        for (int i = 0; i < dims(); ++i)
            dot += (v[static_cast<size_t> (i)] - pca.mean[static_cast<size_t> (i)]) * c[i];
        z[static_cast<size_t> (j)] = static_cast<float> (dot / sd (j));
    }
    return z;
}

void Model::finalize()
{
    loadings.resize (pca.components.size());
    for (int j = 0; j < numComponents(); ++j)
    {
        const auto s = static_cast<float> (sd (j));
        const float* c = pca.component (j);
        float* l = loadings.data() + static_cast<size_t> (j) * static_cast<size_t> (dims());
        for (int i = 0; i < dims(); ++i)
            l[i] = c[i] * s;
    }
    if (pitchSlope.size() != static_cast<size_t> (dims()))
        pitchSlope.assign (static_cast<size_t> (dims()), 0.0f);
}

void Model::accumulate (int offset, int count, const float* z, int numZ, float pitchDelta, float* out) const noexcept
{
    const float* m = pca.mean.data() + offset;
    for (int i = 0; i < count; ++i)
        out[i] = m[i];
    const int k = std::min (numZ, numComponents());
    for (int j = 0; j < k; ++j)
    {
        const float w = z[j];
        if (w == 0.0f)
            continue;
        const float* l = loadings.data() + static_cast<size_t> (j) * static_cast<size_t> (dims()) + offset;
        for (int i = 0; i < count; ++i)
            out[i] += w * l[i];
    }
    if (pitchTracking && pitchDelta != 0.0f)
    {
        const float* sl = pitchSlope.data() + offset;
        for (int i = 0; i < count; ++i)
            out[i] += pitchDelta * sl[i];
    }
}

float Model::toDb (float value) const noexcept
{
    if (representation == Representation::Linear)
    {
        const float floorLin = std::pow (10.0f, floorDb / 20.0f);
        return 20.0f * std::log10 (std::max (value / kLinearScale + floorLin, 1e-9f));
    }
    return value;
}

void Model::decodeFrame (int frame, const float* z, int numZ, float* outDb, float pitchDelta) const noexcept
{
    accumulate (frame * numHarmonics, numHarmonics, z, numZ, pitchDelta, outDb);
    float loudness = 0.0f;
    if (representation == Representation::ShapeLoudness)
        accumulate (loudnessOffset() + frame, 1, z, numZ, pitchDelta, &loudness), loudness /= loudnessWeight;
    for (int h = 0; h < numHarmonics; ++h)
        outDb[h] = std::clamp (toDb (outDb[h]) + loudness, floorDb, kCeilingDb);
}

void Model::decodeNoiseFrame (int frame, const float* z, int numZ, float* outDb, float pitchDelta) const noexcept
{
    if (numNoiseBands == 0)
        return;
    accumulate (noiseOffset() + frame * numNoiseBands, numNoiseBands, z, numZ, pitchDelta, outDb);
    float loudness = 0.0f;
    if (representation == Representation::ShapeLoudness)
        accumulate (loudnessOffset() + frame, 1, z, numZ, pitchDelta, &loudness), loudness /= loudnessWeight;
    for (int b = 0; b < numNoiseBands; ++b)
        outDb[b] = std::clamp (toDb (outDb[b]) + loudness, floorDb, std::max (floorDb, noiseCeilingDb));
}

void Model::decodePartials (const float* z, int numZ, float* outCents, float pitchDelta) const noexcept
{
    if (! hasPartials)
    {
        std::fill (outCents, outCents + numHarmonics, 0.0f);
        return;
    }
    accumulate (partialOffset(), numHarmonics, z, numZ, pitchDelta, outCents);
    for (int h = 0; h < numHarmonics; ++h)
        outCents[h] = std::clamp (outCents[h] / partialWeight, -600.0f, 1200.0f);
}

float Model::levelDb (const float* z, int numZ, float pitchDelta, float* scratchH, float* scratchN) const noexcept
{
    const float floorLin = std::pow (10.0f, floorDb / 20.0f);
    auto energy = [floorLin] (float db) {
        const float a = std::max (0.0f, std::pow (10.0f, db / 20.0f) - floorLin); // as the synth plays it
        return a * a;
    };
    // Probes spaced logarithmically (frame 0, a few early ones for attacks, then
    // wider apart), taking the loudest: close to the loudest-frame measure the
    // analysis normalises every training sound by.
    double loudest = 0.0;
    for (int k = 0; k < kLevelProbes; ++k)
    {
        const int t = std::clamp (static_cast<int> (std::lround (std::pow (static_cast<double> (numFrames), k / (kLevelProbes - 1.0)))) - 1, 0, numFrames - 1);
        decodeFrame (t, z, numZ, scratchH, pitchDelta);
        double e = 0.0;
        for (int h = 0; h < numHarmonics; ++h)
            e += energy (scratchH[h]);
        if (numNoiseBands > 0)
        {
            decodeNoiseFrame (t, z, numZ, scratchN, pitchDelta);
            for (int b = 0; b < numNoiseBands; ++b)
                e += 2.0 * energy (scratchN[b]);
        }
        loudest = std::max (loudest, e);
    }
    return static_cast<float> (10.0 * std::log10 (std::max (1e-12, loudest)));
}

float Model::decodePitch (int frame, const float* z, int numZ, float pitchDelta) const noexcept
{
    if (! hasPitchCurve)
        return 0.0f;
    float v = 0.0f;
    accumulate (pitchCurveOffset() + std::clamp (frame, 0, numFrames - 1), 1, z, numZ, pitchDelta, &v);
    return std::clamp (v / pitchCurveWeight, -1200.0f, 1200.0f);
}

HarmonicSound Model::decode (const std::vector<float>& z, float pitchDelta) const
{
    HarmonicSound s;
    s.name = "decoded";
    s.f0 = midiToHz (pitchTracking ? pitchRef + pitchDelta : analysis.midiNote);
    s.frameRate = frameRate;
    s.numFrames = numFrames;
    s.numHarmonics = numHarmonics;
    s.numNoiseBands = numNoiseBands;
    s.db.resize (static_cast<size_t> (numFrames * numHarmonics));
    s.noiseDb.resize (static_cast<size_t> (numFrames * numNoiseBands));
    const int nz = static_cast<int> (z.size());
    for (int t = 0; t < numFrames; ++t)
    {
        decodeFrame (t, z.data(), nz, s.db.data() + static_cast<size_t> (t * numHarmonics), pitchDelta);
        decodeNoiseFrame (t, z.data(), nz, s.noiseDb.data() + static_cast<size_t> (t * numNoiseBands), pitchDelta);
    }
    if (hasPartials)
    {
        s.partialCents.resize (static_cast<size_t> (numHarmonics));
        decodePartials (z.data(), nz, s.partialCents.data(), pitchDelta);
    }
    if (hasPitchCurve)
    {
        s.pitchCents.resize (static_cast<size_t> (numFrames));
        for (int t = 0; t < numFrames; ++t)
            s.pitchCents[static_cast<size_t> (t)] = decodePitch (t, z.data(), nz, pitchDelta);
    }
    return s;
}

Model singleSoundModel (const HarmonicSound& sound, const AnalysisSettings& settings)
{
    Model m;
    m.analysis = settings;
    setLayout (m, settings, sound.numFrames, sound.numHarmonics, sound.numNoiseBands, ! sound.partialCents.empty(),
               ! sound.pitchCents.empty());
    m.frameRate = sound.frameRate;
    m.names = { sound.name };
    m.f0s = { sound.f0 };
    m.gainsDb = { sound.gainDb };
    m.noiseCeilingDb = noiseCeiling ({ &sound }, m.floorDb);
    m.pca.numRows = 1;
    m.pca.dims = m.dims();
    m.pca.mean = m.encode (sound);
    m.pca.totalVariance = 1.0;
    m.finalize();
    return m;
}

Model trainModel (const std::vector<HarmonicSound>& sounds, const AnalysisSettings& settings, int maxComponents)
{
    if (sounds.size() < 2)
        throw std::invalid_argument ("training needs at least two sounds");
    if (sounds[0].numHarmonics > kMaxModelHarmonics)
        throw std::invalid_argument ("at most 128 harmonics");
    Model m;
    m.analysis = settings;
    bool partials = true, pitchCurve = true;
    for (const auto& s : sounds)
    {
        partials = partials && ! s.partialCents.empty();
        pitchCurve = pitchCurve && ! s.pitchCents.empty();
    }
    setLayout (m, settings, sounds[0].numFrames, sounds[0].numHarmonics, sounds[0].numNoiseBands, partials, pitchCurve);
    m.frameRate = sounds[0].frameRate;
    std::vector<std::vector<float>> rows;
    std::vector<double> pitches;
    std::vector<const HarmonicSound*> all;
    for (const auto& s : sounds)
        all.push_back (&s);
    m.noiseCeilingDb = noiseCeiling (all, m.floorDb);
    for (const auto& s : sounds)
    {
        if (s.numFrames != m.numFrames || s.numHarmonics != m.numHarmonics || s.numNoiseBands != m.numNoiseBands)
            throw std::invalid_argument ("training sounds were analysed with different settings");
        m.names.push_back (s.name);
        m.f0s.push_back (s.f0);
        m.gainsDb.push_back (s.gainDb);
        rows.push_back (m.encode (s));
        pitches.push_back (s.midiPitch());
    }
    if (settings.autoPitch)
    {
        // Nominal note of the set: the median detected pitch.
        auto notes = pitches;
        std::nth_element (notes.begin(), notes.begin() + static_cast<long> (notes.size() / 2), notes.end());
        m.analysis.midiNote = static_cast<int> (std::lround (notes[notes.size() / 2]));
    }

    // Pitch tracking: regress every dimension on pitch (least squares) and take
    // that direction out, so the PCA describes timbre apart from register.
    const auto [lo, hi] = std::minmax_element (pitches.begin(), pitches.end());
    m.pitchTracking = settings.pitchTracking == PitchTracking::On
                   || (settings.pitchTracking == PitchTracking::Auto && *hi - *lo >= 3.0);
    if (m.pitchTracking && *hi - *lo > 0.1)
    {
        double ref = 0.0;
        for (double p : pitches)
            ref += p / static_cast<double> (pitches.size());
        m.pitchRef = ref;
        m.pitchMin = *lo;
        m.pitchMax = *hi;
        const size_t d = static_cast<size_t> (m.dims());
        std::vector<double> mean (d, 0.0), slope (d, 0.0);
        for (const auto& r : rows)
            for (size_t i = 0; i < d; ++i)
                mean[i] += r[i] / static_cast<double> (rows.size());
        double sxx = 0.0;
        for (size_t k = 0; k < rows.size(); ++k)
        {
            const double dp = pitches[k] - ref;
            sxx += dp * dp;
            for (size_t i = 0; i < d; ++i)
                slope[i] += dp * (rows[k][i] - mean[i]);
        }
        m.pitchSlope.resize (d);
        for (size_t i = 0; i < d; ++i)
            m.pitchSlope[i] = static_cast<float> (slope[i] / sxx);
        for (size_t k = 0; k < rows.size(); ++k)
        {
            const auto dp = static_cast<float> (pitches[k] - ref);
            for (size_t i = 0; i < d; ++i)
                rows[k][i] -= dp * m.pitchSlope[i];
        }
    }
    else
        m.pitchTracking = false;

    m.pca = computePca (rows, std::min (maxComponents, kMaxComponents));
    m.finalize();
    fitReport (m, rows);
    return m;
}

std::vector<uint8_t> serializeModel (const Model& m)
{
    nlohmann::json j;
    j["format"] = "pcasynth-model";
    j["version"] = kFormatVersion;
    j["title"] = m.title;
    j["analysis"] = settingsToJson (m.analysis);
    j["numFrames"] = m.numFrames;
    j["numHarmonics"] = m.numHarmonics;
    j["numNoiseBands"] = m.numNoiseBands;
    j["hasPartials"] = m.hasPartials;
    j["representation"] = representationName (m.representation);
    j["loudnessWeight"] = m.loudnessWeight;
    j["partialWeight"] = m.partialWeight;
    j["hasPitchCurve"] = m.hasPitchCurve;
    j["pitchCurveWeight"] = m.pitchCurveWeight;
    j["fit"] = { { "errorDb", m.fitErrorDb }, { "byComponents", m.fitByComponents } };
    j["noiseCeilingDb"] = m.noiseCeilingDb;
    j["frameRate"] = m.frameRate;
    j["floorDb"] = m.floorDb;
    j["numComponents"] = m.numComponents();
    j["totalVariance"] = m.pca.totalVariance;
    j["variance"] = m.pca.variance;
    j["names"] = m.names;
    j["f0s"] = m.f0s;
    j["gainsDb"] = m.gainsDb;
    j["scores"] = m.pca.scores;
    j["pitch"] = { { "tracking", m.pitchTracking }, { "ref", m.pitchRef }, { "min", m.pitchMin }, { "max", m.pitchMax } };
    std::string header = j.dump();
    while ((header.size() % 4) != 0)
        header.push_back (' ');

    std::vector<uint8_t> out (kMagic, kMagic + 4);
    putU32 (out, kFormatVersion);
    putU32 (out, static_cast<uint32_t> (header.size()));
    out.insert (out.end(), header.begin(), header.end());
    putFloats (out, m.pca.mean);
    putFloats (out, m.pca.components);
    if (m.pitchTracking)
        putFloats (out, m.pitchSlope);
    return out;
}

Model deserializeModel (const uint8_t* data, size_t size)
{
    if (size < 12 || std::memcmp (data, kMagic, 4) != 0)
        throw std::runtime_error ("not a PCASynth model");
    if (getU32 (data + 4) > kFormatVersion)
        throw std::runtime_error ("model was written by a newer PCASynth");
    const size_t headerSize = getU32 (data + 8);
    if (12 + headerSize > size)
        throw std::runtime_error ("model file is truncated");

    Model m;
    try
    {
        const auto j = nlohmann::json::parse (data + 12, data + 12 + headerSize);
        m.title = j.value ("title", std::string());
        m.analysis = settingsFromJson (j.at ("analysis"));
        m.numFrames = j.at ("numFrames");
        m.numHarmonics = j.at ("numHarmonics");
        // Version 1 files have none of the Stage 7 sections: harmonic dB only.
        m.numNoiseBands = j.value ("numNoiseBands", 0);
        m.hasPartials = j.value ("hasPartials", false);
        m.representation = representationFrom (j.value ("representation", std::string ("dB")));
        m.loudnessWeight = j.value ("loudnessWeight", 1.0f);
        m.partialWeight = j.value ("partialWeight", 1.0f);
        m.hasPitchCurve = j.value ("hasPitchCurve", false);
        m.pitchCurveWeight = j.value ("pitchCurveWeight", 1.0f);
        if (j.contains ("fit"))
        {
            m.fitErrorDb = j.at ("fit").value ("errorDb", std::vector<float>());
            m.fitByComponents = j.at ("fit").value ("byComponents", std::vector<float>());
        }
        m.noiseCeilingDb = j.value ("noiseCeilingDb", 0.0f);
        m.frameRate = j.at ("frameRate");
        m.floorDb = j.at ("floorDb");
        m.names = j.at ("names").get<std::vector<std::string>>();
        m.f0s = j.at ("f0s").get<std::vector<double>>();
        m.gainsDb = j.at ("gainsDb").get<std::vector<double>>();
        m.pca.numComponents = j.at ("numComponents");
        m.pca.totalVariance = j.at ("totalVariance");
        m.pca.variance = j.at ("variance").get<std::vector<double>>();
        m.pca.scores = j.at ("scores").get<std::vector<double>>();
        if (j.contains ("pitch"))
        {
            const auto& pj = j.at ("pitch");
            m.pitchTracking = pj.value ("tracking", false);
            m.pitchRef = pj.value ("ref", 60.0);
            m.pitchMin = pj.value ("min", 60.0);
            m.pitchMax = pj.value ("max", 60.0);
        }
    }
    catch (const nlohmann::json::exception& e)
    {
        throw std::runtime_error (std::string ("bad model header: ") + e.what());
    }
    if (m.numFrames <= 0 || m.numHarmonics <= 0 || m.numHarmonics > kMaxModelHarmonics || m.numNoiseBands < 0 || m.numNoiseBands > 64
        || m.loudnessWeight <= 0.0f || m.partialWeight <= 0.0f || m.pitchCurveWeight <= 0.0f)
        throw std::runtime_error ("model header is inconsistent");
    m.pca.numRows = static_cast<int> (m.names.size());
    m.pca.dims = m.dims();
    const auto d = static_cast<size_t> (m.dims());
    const auto k = static_cast<size_t> (m.pca.numComponents);
    if (k > static_cast<size_t> (kMaxComponents) || m.pca.variance.size() != k || m.pca.scores.size() != k * m.names.size()
        || m.f0s.size() != m.names.size())
        throw std::runtime_error ("model header is inconsistent");
    const size_t offset = 12 + headerSize;
    const size_t floats = d * (1 + k) + (m.pitchTracking ? d : 0);
    if (size - offset < 4 * floats)
        throw std::runtime_error ("model file is truncated");
    m.pca.mean = getFloats (data + offset, d);
    m.pca.components = getFloats (data + offset + 4 * d, d * k);
    if (m.pitchTracking)
        m.pitchSlope = getFloats (data + offset + 4 * d * (1 + k), d);
    m.finalize();
    return m;
}

void saveModel (const Model& model, const std::string& path)
{
    const auto bytes = serializeModel (model);
    std::ofstream out (path, std::ios::binary);
    if (! out.write (reinterpret_cast<const char*> (bytes.data()), static_cast<std::streamsize> (bytes.size())))
        throw std::runtime_error ("cannot write " + path);
}

Model loadModel (const std::string& path)
{
    std::ifstream in (path, std::ios::binary);
    if (! in)
        throw std::runtime_error ("cannot open " + path);
    std::vector<uint8_t> bytes ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    return deserializeModel (bytes.data(), bytes.size());
}

} // namespace pcs
