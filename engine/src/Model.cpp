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
constexpr uint32_t kFormatVersion = 1;
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

nlohmann::json settingsToJson (const AnalysisSettings& s)
{
    return { { "midiNote", s.midiNote },           { "tuneSearchCents", s.tuneSearchCents },
             { "duration", s.duration },           { "frameRate", s.frameRate },
             { "harmonics", s.harmonics },         { "floorDb", s.floorDb },
             { "periodsPerWindow", s.periodsPerWindow }, { "trimOnset", s.trimOnset },
             { "onsetThresholdDb", s.onsetThresholdDb }, { "normalizeLoudness", s.normalizeLoudness } };
}
AnalysisSettings settingsFromJson (const nlohmann::json& j)
{
    AnalysisSettings s;
    s.midiNote = j.value ("midiNote", s.midiNote);
    s.tuneSearchCents = j.value ("tuneSearchCents", s.tuneSearchCents);
    s.duration = j.value ("duration", s.duration);
    s.frameRate = j.value ("frameRate", s.frameRate);
    s.harmonics = j.value ("harmonics", s.harmonics);
    s.floorDb = j.value ("floorDb", s.floorDb);
    s.periodsPerWindow = j.value ("periodsPerWindow", s.periodsPerWindow);
    s.trimOnset = j.value ("trimOnset", s.trimOnset);
    s.onsetThresholdDb = j.value ("onsetThresholdDb", s.onsetThresholdDb);
    s.normalizeLoudness = j.value ("normalizeLoudness", s.normalizeLoudness);
    return s;
}
} // namespace

int Model::soundIndex (const std::string& name) const
{
    const auto it = std::find (names.begin(), names.end(), name);
    return it == names.end() ? -1 : static_cast<int> (it - names.begin());
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
    if (sound.numFrames != numFrames || sound.numHarmonics != numHarmonics)
        throw std::invalid_argument ("sound shape does not match the model");
    std::vector<float> z (static_cast<size_t> (numComponents()));
    for (int j = 0; j < numComponents(); ++j)
    {
        const float* c = pca.component (j);
        double dot = 0.0;
        for (int i = 0; i < dims(); ++i)
            dot += (sound.db[static_cast<size_t> (i)] - pca.mean[static_cast<size_t> (i)]) * c[i];
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
}

void Model::decodeFrame (int frame, const float* z, int numZ, float* outDb) const noexcept
{
    const size_t base = static_cast<size_t> (frame) * static_cast<size_t> (numHarmonics);
    const float* m = pca.mean.data() + base;
    for (int h = 0; h < numHarmonics; ++h)
        outDb[h] = m[h];
    const int k = std::min (numZ, numComponents());
    for (int j = 0; j < k; ++j)
    {
        const float w = z[j];
        if (w == 0.0f)
            continue;
        const float* l = loadings.data() + static_cast<size_t> (j) * static_cast<size_t> (dims()) + base;
        for (int h = 0; h < numHarmonics; ++h)
            outDb[h] += w * l[h];
    }
    for (int h = 0; h < numHarmonics; ++h)
        outDb[h] = std::clamp (outDb[h], floorDb, kCeilingDb);
}

HarmonicSound Model::decode (const std::vector<float>& z) const
{
    HarmonicSound s;
    s.name = "decoded";
    s.f0 = midiToHz (analysis.midiNote);
    s.frameRate = frameRate;
    s.numFrames = numFrames;
    s.numHarmonics = numHarmonics;
    s.db.resize (static_cast<size_t> (dims()));
    for (int t = 0; t < numFrames; ++t)
        decodeFrame (t, z.data(), static_cast<int> (z.size()), s.db.data() + static_cast<size_t> (t * numHarmonics));
    return s;
}

Model trainModel (const std::vector<HarmonicSound>& sounds, const AnalysisSettings& settings, int maxComponents)
{
    if (sounds.size() < 2)
        throw std::invalid_argument ("training needs at least two sounds");
    if (sounds[0].numHarmonics > kMaxModelHarmonics)
        throw std::invalid_argument ("at most 128 harmonics");
    Model m;
    m.analysis = settings;
    m.numFrames = sounds[0].numFrames;
    m.numHarmonics = sounds[0].numHarmonics;
    m.frameRate = sounds[0].frameRate;
    m.floorDb = static_cast<float> (settings.floorDb);
    std::vector<std::vector<float>> rows;
    for (const auto& s : sounds)
    {
        if (s.numFrames != m.numFrames || s.numHarmonics != m.numHarmonics)
            throw std::invalid_argument ("training sounds were analysed with different settings");
        m.names.push_back (s.name);
        m.f0s.push_back (s.f0);
        m.gainsDb.push_back (s.gainDb);
        rows.push_back (s.db);
    }
    m.pca = computePca (rows, std::min (maxComponents, kMaxComponents));
    m.finalize();
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
    j["frameRate"] = m.frameRate;
    j["floorDb"] = m.floorDb;
    j["numComponents"] = m.numComponents();
    j["totalVariance"] = m.pca.totalVariance;
    j["variance"] = m.pca.variance;
    j["names"] = m.names;
    j["f0s"] = m.f0s;
    j["gainsDb"] = m.gainsDb;
    j["scores"] = m.pca.scores;
    std::string header = j.dump();
    while ((header.size() % 4) != 0)
        header.push_back (' ');

    std::vector<uint8_t> out (kMagic, kMagic + 4);
    putU32 (out, kFormatVersion);
    putU32 (out, static_cast<uint32_t> (header.size()));
    out.insert (out.end(), header.begin(), header.end());
    putFloats (out, m.pca.mean);
    putFloats (out, m.pca.components);
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
        m.frameRate = j.at ("frameRate");
        m.floorDb = j.at ("floorDb");
        m.names = j.at ("names").get<std::vector<std::string>>();
        m.f0s = j.at ("f0s").get<std::vector<double>>();
        m.gainsDb = j.at ("gainsDb").get<std::vector<double>>();
        m.pca.numComponents = j.at ("numComponents");
        m.pca.totalVariance = j.at ("totalVariance");
        m.pca.variance = j.at ("variance").get<std::vector<double>>();
        m.pca.scores = j.at ("scores").get<std::vector<double>>();
    }
    catch (const nlohmann::json::exception& e)
    {
        throw std::runtime_error (std::string ("bad model header: ") + e.what());
    }
    m.pca.numRows = static_cast<int> (m.names.size());
    m.pca.dims = m.dims();
    const auto d = static_cast<size_t> (m.dims());
    const auto k = static_cast<size_t> (m.pca.numComponents);
    if (m.numFrames <= 0 || m.numHarmonics <= 0 || m.numHarmonics > kMaxModelHarmonics || k > static_cast<size_t> (kMaxComponents) || m.pca.variance.size() != k
        || m.pca.scores.size() != k * m.names.size())
        throw std::runtime_error ("model header is inconsistent");
    const size_t offset = 12 + headerSize;
    if (size - offset < 4 * d * (1 + k))
        throw std::runtime_error ("model file is truncated");
    m.pca.mean = getFloats (data + offset, d);
    m.pca.components = getFloats (data + offset + 4 * d, d * k);
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
