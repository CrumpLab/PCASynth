#include "Render.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <sstream>
#include <stdexcept>

namespace pcs::tools {

AudioBuffer renderNotes (std::shared_ptr<const Model> model, const SynthParams& params, const std::vector<Note>& notes,
                         double seconds, double sampleRate, const Automation& automation)
{
    Synth synth;
    synth.prepare (sampleRate);
    SynthParams p = params;
    if (automation)
        automation (0.0, p);
    synth.setParams (p);
    synth.setModel (std::move (model));

    struct Timed
    {
        long sample;
        MidiEvent event;
    };
    std::vector<Timed> events;
    for (const auto& n : notes)
    {
        events.push_back ({ std::lround (n.start * sampleRate), { 0, MidiEvent::Type::NoteOn, n.note, n.velocity } });
        events.push_back ({ std::lround ((n.start + n.length) * sampleRate), { 0, MidiEvent::Type::NoteOff, n.note, 0.0f } });
    }
    std::stable_sort (events.begin(), events.end(), [] (const Timed& a, const Timed& b) { return a.sample < b.sample; });

    AudioBuffer out;
    out.sampleRate = sampleRate;
    const auto total = static_cast<int> (std::lround (seconds * sampleRate));
    out.resize (1, total);
    constexpr int block = 256;
    std::vector<MidiEvent> blockEvents;
    size_t next = 0;
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = std::min (block, total - pos);
        if (automation)
        {
            automation (pos / sampleRate, p);
            synth.setParams (p);
        }
        blockEvents.clear();
        while (next < events.size() && events[next].sample < pos + n)
        {
            auto e = events[next++].event;
            e.offset = std::max (0, static_cast<int> (events[next - 1].sample - pos));
            blockEvents.push_back (e);
        }
        float* ch[] = { out.channels[0].data() + pos };
        synth.process (ch, 1, n, blockEvents.data(), static_cast<int> (blockEvents.size()));
    }
    return out;
}

std::shared_ptr<Model> singleSoundModel (const HarmonicSound& sound, const AnalysisSettings& settings)
{
    return std::make_shared<Model> (pcs::singleSoundModel (sound, settings));
}

std::vector<std::string> collectWavs (const std::vector<std::string>& paths)
{
    namespace fs = std::filesystem;
    std::vector<std::string> out;
    for (const auto& p : paths)
    {
        if (fs::is_directory (p))
        {
            std::vector<std::string> dir;
            for (const auto& e : fs::directory_iterator (p))
            {
                auto ext = e.path().extension().string();
                std::transform (ext.begin(), ext.end(), ext.begin(), [] (unsigned char c) { return static_cast<char> (std::tolower (c)); });
                if (e.is_regular_file() && ext == ".wav")
                    dir.push_back (e.path().string());
            }
            std::sort (dir.begin(), dir.end());
            out.insert (out.end(), dir.begin(), dir.end());
        }
        else
        {
            out.push_back (p);
        }
    }
    return out;
}

std::string stem (const std::string& path) { return std::filesystem::path (path).stem().string(); }

void appendSilence (AudioBuffer& dst, double seconds)
{
    const auto n = static_cast<size_t> (seconds * dst.sampleRate);
    if (dst.channels.empty())
        dst.channels.resize (1);
    for (auto& c : dst.channels)
        c.insert (c.end(), n, 0.0f);
}

void append (AudioBuffer& dst, const AudioBuffer& src)
{
    if (dst.channels.empty())
    {
        dst.sampleRate = src.sampleRate;
        dst.channels.resize (1);
    }
    for (size_t c = 0; c < dst.channels.size(); ++c)
    {
        const auto& s = src.channels[std::min (c, src.channels.size() - 1)];
        dst.channels[c].insert (dst.channels[c].end(), s.begin(), s.end());
    }
}

void normalisePeak (AudioBuffer& a, float peak)
{
    float m = 0.0f;
    for (const auto& c : a.channels)
        for (float x : c)
            m = std::max (m, std::abs (x));
    if (m > 0.0f)
        for (auto& c : a.channels)
            for (auto& x : c)
                x *= peak / m;
}

std::vector<float> parseFloats (const std::string& csv)
{
    std::vector<float> out;
    std::stringstream ss (csv);
    std::string item;
    while (std::getline (ss, item, ','))
        if (! item.empty())
            out.push_back (std::stof (item));
    return out;
}

} // namespace pcs::tools
