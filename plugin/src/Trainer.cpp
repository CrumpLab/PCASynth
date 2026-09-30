#include "Trainer.h"

const juce::Identifier Trainer::treeType ("Training");

namespace {
std::string settingsKey (const pcs::AnalysisSettings& s)
{
    // Only what changes the analysis (representation and pitch tracking are applied at training).
    return juce::String::formatted ("%d|%d|%.3f|%.3f|%.3f|%d|%.3f|%.3f|%d|%.3f|%d|%d|%d", s.midiNote, s.autoPitch ? 1 : 0,
                                    s.tuneSearchCents, s.duration, s.frameRate, s.harmonics, s.floorDb, s.periodsPerWindow,
                                    s.trimOnset ? 1 : 0, s.onsetThresholdDb, s.normalizeLoudness ? 1 : 0, s.noiseBands,
                                    s.trackPartials ? 1 : 0)
        .toStdString();
}
} // namespace

Trainer::Trainer (std::function<void (std::shared_ptr<const pcs::Model>)> callback)
    : juce::Thread ("PCASynth training"), onModel (std::move (callback))
{
    formats.registerBasicFormats();
    settings.title = "Trained space";
}

Trainer::~Trainer()
{
    alive->store (false);
    stopThread (10000);
}

bool Trainer::isAudioFile (const juce::File& f)
{
    return f.existsAsFile() && f.hasFileExtension ("wav;wave;aif;aiff;flac;ogg;mp3;m4a;caf");
}

int Trainer::addFiles (const juce::StringArray& paths)
{
    juce::Array<juce::File> files;
    for (const auto& p : paths)
    {
        const juce::File f (p);
        if (f.isDirectory())
        {
            auto found = f.findChildFiles (juce::File::findFiles, true);
            found.sort();
            for (const auto& c : found)
                if (isAudioFile (c))
                    files.add (c);
        }
        else if (isAudioFile (f))
            files.add (f);
    }

    int added = 0;
    {
        const juce::ScopedLock sl (lock);
        for (const auto& f : files)
        {
            const auto path = f.getFullPathName();
            if (std::any_of (entries.begin(), entries.end(), [&] (const Entry& e) { return e.path == path; }))
                continue;
            // Unique names: the file name, numbered if it repeats.
            auto name = f.getFileNameWithoutExtension();
            for (int n = 2; std::any_of (entries.begin(), entries.end(), [&] (const Entry& e) { return e.name == name; }); ++n)
                name = f.getFileNameWithoutExtension() + " (" + juce::String (n) + ")";
            Entry e;
            e.path = path;
            e.name = name;
            entries.push_back (e);
            ++added;
        }
    }
    changed();
    return added;
}

void Trainer::remove (const juce::Array<int>& indices)
{
    {
        const juce::ScopedLock sl (lock);
        std::vector<Entry> kept;
        for (size_t i = 0; i < entries.size(); ++i)
            if (! indices.contains (static_cast<int> (i)))
                kept.push_back (entries[i]);
        entries = std::move (kept);
    }
    changed();
}

void Trainer::clear()
{
    {
        const juce::ScopedLock sl (lock);
        entries.clear();
    }
    changed();
}

std::vector<Trainer::Entry> Trainer::getEntries() const
{
    const juce::ScopedLock sl (lock);
    return entries;
}

Trainer::Settings Trainer::getSettings() const
{
    const juce::ScopedLock sl (lock);
    return settings;
}

void Trainer::setSettings (const Settings& s)
{
    {
        const juce::ScopedLock sl (lock);
        settings = s;
        settings.components = juce::jlimit (1, pcs::kMaxComponents, settings.components);
        settings.analysis.harmonics = juce::jlimit (1, pcs::kMaxModelHarmonics, settings.analysis.harmonics);
    }
    changed();
}

juce::String Trainer::getStatus() const
{
    const juce::ScopedLock sl (lock);
    return status;
}

void Trainer::setStatus (const juce::String& s)
{
    {
        const juce::ScopedLock sl (lock);
        status = s;
    }
    changed();
}

bool Trainer::start()
{
    if (isThreadRunning() || getEntries().size() < 2)
        return false;
    progress = 0.0;
    startThread (juce::Thread::Priority::low);
    return true;
}

void Trainer::cancel() { signalThreadShouldExit(); }

void Trainer::run()
{
    juce::String error;
    auto model = train (error);
    if (model == nullptr)
        return;
    auto flag = alive;
    auto callback = onModel;
    juce::MessageManager::callAsync ([flag, callback, model] {
        if (flag->load())
            callback (model);
    });
}

std::shared_ptr<const pcs::Model> Trainer::trainNow (juce::String& error)
{
    auto model = train (error);
    if (model != nullptr)
        onModel (model);
    return model;
}

std::shared_ptr<const pcs::Model> Trainer::train (juce::String& error)
{
    const auto s = getSettings();
    const auto key = settingsKey (s.analysis);
    auto items = getEntries();

    std::vector<pcs::HarmonicSound> sounds;
    std::vector<std::pair<juce::String, Entry>> results; // path -> updated entry
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (threadShouldExit())
        {
            setStatus ("Cancelled.");
            return nullptr;
        }
        auto& e = items[i];
        setStatus ("Analysing " + e.name + " (" + juce::String (i + 1) + " of " + juce::String (items.size()) + ")");
        const juce::File file (e.path);
        e.status = Entry::Status::Failed;
        try
        {
            if (! file.existsAsFile())
                throw std::runtime_error ("file not found");
            const auto modified = file.getLastModificationTime();
            auto it = cache.find (e.path);
            if (it == cache.end() || it->second.modified != modified || it->second.settingsKey != key)
            {
                std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
                if (reader == nullptr)
                    throw std::runtime_error ("not a readable audio file");
                // Enough for leading silence plus the analysed duration.
                const auto maxSamples = static_cast<juce::int64> ((s.analysis.duration + 10.0) * reader->sampleRate);
                const int n = static_cast<int> (std::min<juce::int64> (reader->lengthInSamples, maxSamples));
                const int ch = static_cast<int> (juce::jlimit (1u, 2u, reader->numChannels));
                juce::AudioBuffer<float> buf (ch, n);
                reader->read (&buf, 0, n, 0, true, ch > 1);
                pcs::AudioBuffer audio;
                audio.sampleRate = reader->sampleRate;
                audio.resize (ch, n);
                for (int c = 0; c < ch; ++c)
                    std::copy (buf.getReadPointer (c), buf.getReadPointer (c) + n, audio.channels[static_cast<size_t> (c)].begin());
                Cached c;
                c.modified = modified;
                c.settingsKey = key;
                c.sound = pcs::analyseHarmonics (audio, s.analysis, e.name.toStdString());
                it = cache.insert_or_assign (e.path, std::move (c)).first;
            }
            auto sound = it->second.sound;
            sound.name = e.name.toStdString();
            e.f0 = sound.f0;
            e.status = Entry::Status::Analysed;
            e.message.clear();
            sounds.push_back (std::move (sound));
        }
        catch (const std::exception& ex)
        {
            e.message = ex.what();
        }
        results.emplace_back (e.path, e);
        progress = 0.9 * static_cast<double> (i + 1) / static_cast<double> (items.size());
    }

    // Publish each entry's result (entries may have been edited meanwhile).
    {
        const juce::ScopedLock sl (lock);
        for (auto& live : entries)
            for (const auto& [path, r] : results)
                if (live.path == path)
                {
                    live.status = r.status;
                    live.f0 = r.f0;
                    live.message = r.message;
                }
    }
    changed();

    if (sounds.size() < 2)
    {
        error = "Need at least two sounds that analyse (" + juce::String (static_cast<int> (sounds.size())) + " did).";
        setStatus (error);
        progress = 0.0;
        return nullptr;
    }
    setStatus ("Computing principal components...");
    try
    {
        auto model = std::make_shared<pcs::Model> (pcs::trainModel (sounds, s.analysis, s.components));
        model->title = s.title.isEmpty() ? "Trained space" : s.title.toStdString();
        const int failed = static_cast<int> (items.size() - sounds.size());
        setStatus ("Trained on " + juce::String (static_cast<int> (sounds.size())) + " sounds"
                   + (failed > 0 ? " (" + juce::String (failed) + " failed)" : juce::String()) + ".");
        progress = 1.0;
        return model;
    }
    catch (const std::exception& ex)
    {
        error = ex.what();
        setStatus (error);
        progress = 0.0;
        return nullptr;
    }
}

juce::ValueTree Trainer::toValueTree() const
{
    const auto s = getSettings();
    juce::ValueTree t (treeType);
    t.setProperty ("title", s.title, nullptr);
    t.setProperty ("note", s.analysis.midiNote, nullptr);
    t.setProperty ("autoPitch", s.analysis.autoPitch, nullptr);
    t.setProperty ("duration", s.analysis.duration, nullptr);
    t.setProperty ("harmonics", s.analysis.harmonics, nullptr);
    t.setProperty ("floorDb", s.analysis.floorDb, nullptr);
    t.setProperty ("normalize", s.analysis.normalizeLoudness, nullptr);
    t.setProperty ("trim", s.analysis.trimOnset, nullptr);
    t.setProperty ("components", s.components, nullptr);
    t.setProperty ("noiseBands", s.analysis.noiseBands, nullptr);
    t.setProperty ("partials", s.analysis.trackPartials, nullptr);
    t.setProperty ("representation", static_cast<int> (s.analysis.representation), nullptr);
    t.setProperty ("pitchTracking", static_cast<int> (s.analysis.pitchTracking), nullptr);
    for (const auto& e : getEntries())
    {
        juce::ValueTree f ("File");
        f.setProperty ("path", e.path, nullptr);
        f.setProperty ("name", e.name, nullptr);
        t.appendChild (f, nullptr);
    }
    return t;
}

void Trainer::fromValueTree (const juce::ValueTree& t)
{
    if (! t.hasType (treeType))
        return;
    Settings s;
    s.title = t.getProperty ("title", "Trained space");
    s.analysis.midiNote = t.getProperty ("note", 60);
    s.analysis.autoPitch = t.getProperty ("autoPitch", false);
    s.analysis.duration = t.getProperty ("duration", 4.0);
    s.analysis.harmonics = t.getProperty ("harmonics", 64);
    s.analysis.floorDb = t.getProperty ("floorDb", -80.0);
    s.analysis.normalizeLoudness = t.getProperty ("normalize", true);
    s.analysis.trimOnset = t.getProperty ("trim", true);
    s.components = t.getProperty ("components", pcs::kMaxComponents);
    s.analysis.noiseBands = t.getProperty ("noiseBands", 16);
    s.analysis.trackPartials = t.getProperty ("partials", true);
    s.analysis.representation = static_cast<pcs::Representation> (juce::jlimit (0, 2, static_cast<int> (t.getProperty ("representation", 0))));
    s.analysis.pitchTracking = static_cast<pcs::PitchTracking> (juce::jlimit (0, 2, static_cast<int> (t.getProperty ("pitchTracking", 1))));
    setSettings (s);
    const juce::ScopedLock sl (lock);
    entries.clear();
    for (const auto& f : t)
    {
        Entry e;
        e.path = f.getProperty ("path").toString();
        e.name = f.getProperty ("name").toString();
        if (e.path.isNotEmpty())
            entries.push_back (e);
    }
    changed();
}
