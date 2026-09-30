#include "Trainer.h"

const juce::Identifier Trainer::treeType ("Training");

namespace {
std::string settingsKey (const pcs::AnalysisSettings& s)
{
    // Only what changes the analysis (representation and pitch tracking are applied at training).
    return juce::String::formatted ("%d|%d|%.3f|%.3f|%.3f|%d|%.3f|%.3f|%d|%.3f|%d|%d|%d|%d|%d|%.3f", s.midiNote, s.autoPitch ? 1 : 0,
                                    s.tuneSearchCents, s.duration, s.frameRate, s.harmonics, s.floorDb, s.periodsPerWindow,
                                    s.trimOnset ? 1 : 0, s.onsetThresholdDb, s.normalizeLoudness ? 1 : 0, s.noiseBands,
                                    s.trackPartials ? 1 : 0, s.trackPitch ? 1 : 0, s.sharpAttacks ? 1 : 0, s.attackSeconds)
        .toStdString();
}
} // namespace

Trainer::Trainer (pcsplugin::Kind k, std::function<void (std::shared_ptr<const pcs::Space>)> callback)
    : juce::Thread ("PCASynth training"), kind (k), onModel (std::move (callback))
{
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

std::shared_ptr<const pcs::Space> Trainer::trainNow (juce::String& error)
{
    auto model = train (error);
    if (model != nullptr)
        onModel (model);
    return model;
}

std::shared_ptr<const pcs::Space> Trainer::trainWave (juce::String& error)
{
    const auto s = getSettings();
    auto items = getEntries();
    std::vector<pcs::AudioBuffer> audio;
    std::vector<std::string> names;
    std::vector<std::pair<juce::String, Entry>> results;
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (threadShouldExit())
        {
            setStatus ("Cancelled.");
            return nullptr;
        }
        auto& e = items[i];
        setStatus ("Reading " + e.name + " (" + juce::String (i + 1) + " of " + juce::String (items.size()) + ")");
        e.status = Entry::Status::Failed;
        pcs::AudioBuffer a;
        juce::String readError;
        // Enough for leading silence plus the kept duration, even when a low sound is sped up to the common pitch.
        if (! readAudio (juce::File (e.path), 4.0 * s.wave.duration + 10.0, a, readError))
            e.message = readError;
        else
            try
            {
                e.f0 = pcs::wavePitch (a, s.wave);
                e.status = Entry::Status::Analysed;
                e.message.clear();
                audio.push_back (std::move (a));
                names.push_back (e.name.toStdString());
            }
            catch (const std::exception& ex)
            {
                e.message = ex.what();
            }
        results.emplace_back (e.path, e);
        progress = 0.5 * static_cast<double> (i + 1) / static_cast<double> (items.size());
    }
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
    if (audio.size() < 2)
    {
        error = "Need at least two sounds that read and have a pitch (" + juce::String (static_cast<int> (audio.size())) + " did).";
        setStatus (error);
        progress = 0.0;
        return nullptr;
    }
    setStatus ("Lining up the waveforms and computing principal components...");
    try
    {
        auto model = std::make_shared<pcs::WaveModel> (pcs::trainWaveModel (audio, names, s.wave, s.components));
        model->title = s.title.isEmpty() ? "Trained space" : s.title.toStdString();
        const int failed = static_cast<int> (items.size() - audio.size());
        setStatus ("Trained on " + juce::String (static_cast<int> (audio.size())) + " sounds"
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

std::shared_ptr<const pcs::Space> Trainer::train (juce::String& error)
{
    if (kind == pcsplugin::Kind::Wave)
        return trainWave (error);
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
                pcs::AudioBuffer audio;
                juce::String readError;
                // Enough for leading silence plus the analysed duration.
                if (! readAudio (file, s.analysis.duration + 10.0, audio, readError))
                    throw std::runtime_error (readError.toStdString());
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

bool Trainer::readAudio (const juce::File& file, double maxSeconds, pcs::AudioBuffer& audio, juce::String& error)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
    {
        error = file.existsAsFile() ? "not a readable audio file" : "file not found";
        return false;
    }
    const auto maxSamples = static_cast<juce::int64> (maxSeconds * reader->sampleRate);
    const int n = static_cast<int> (std::min<juce::int64> (reader->lengthInSamples, maxSamples));
    const int ch = static_cast<int> (juce::jlimit (1u, 2u, reader->numChannels));
    juce::AudioBuffer<float> buf (ch, n);
    reader->read (&buf, 0, n, 0, true, ch > 1);
    audio.sampleRate = reader->sampleRate;
    audio.resize (ch, n);
    for (int c = 0; c < ch; ++c)
        std::copy (buf.getReadPointer (c), buf.getReadPointer (c) + n, audio.channels[static_cast<size_t> (c)].begin());
    return true;
}

juce::File Trainer::fileFor (const juce::String& soundName) const
{
    const juce::ScopedLock sl (lock);
    for (const auto& e : entries)
        if (e.name == soundName)
            return juce::File (e.path);
    return {};
}

juce::ValueTree Trainer::toValueTree() const
{
    auto s = getSettings();
    if (kind == pcsplugin::Kind::Wave)
    {
        // The settings both kinds have are saved under the same names.
        s.analysis.midiNote = s.wave.midiNote;
        s.analysis.autoPitch = s.wave.autoPitch;
        s.analysis.normalizeLoudness = s.wave.normalizeLoudness;
        s.analysis.trimOnset = s.wave.trimOnset;
    }
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
    t.setProperty ("frameRate", s.analysis.frameRate, nullptr);
    t.setProperty ("pitchCurve", s.analysis.trackPitch, nullptr);
    t.setProperty ("sharpAttacks", s.analysis.sharpAttacks, nullptr);
    t.setProperty ("waveRate", s.wave.sampleRate, nullptr);
    t.setProperty ("waveDuration", s.wave.duration, nullptr);
    t.setProperty ("alignPitch", s.wave.alignPitch, nullptr);
    t.setProperty ("alignPhase", s.wave.alignPhase, nullptr);
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
    s.analysis.frameRate = juce::jlimit (50.0, 400.0, static_cast<double> (t.getProperty ("frameRate", 100.0)));
    s.analysis.trackPitch = t.getProperty ("pitchCurve", true);
    s.analysis.sharpAttacks = t.getProperty ("sharpAttacks", true);
    s.wave.sampleRate = juce::jlimit (8000.0, 96000.0, static_cast<double> (t.getProperty ("waveRate", 48000.0)));
    s.wave.duration = juce::jlimit (0.2, 10.0, static_cast<double> (t.getProperty ("waveDuration", 3.0)));
    s.wave.alignPitch = t.getProperty ("alignPitch", true);
    s.wave.alignPhase = t.getProperty ("alignPhase", true);
    s.wave.midiNote = s.analysis.midiNote;
    s.wave.autoPitch = s.analysis.autoPitch;
    s.wave.normalizeLoudness = s.analysis.normalizeLoudness;
    s.wave.trimOnset = s.analysis.trimOnset;
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
