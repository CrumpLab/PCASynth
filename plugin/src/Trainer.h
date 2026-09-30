#pragma once

#include "pcs/Harmonic.h"
#include "pcs/Model.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>

#include <atomic>
#include <functional>
#include <map>
#include <memory>

// Trains a new sound space from audio files inside the plugin (plan Stage 4).
// Analysis and PCA run on a background thread. Analysed sounds are cached by
// file (and modification time and analysis settings), so removing a sound or
// changing only the number of components retrains without re-analysing.
class Trainer final : private juce::Thread
{
public:
    struct Settings
    {
        pcs::AnalysisSettings analysis;
        int components = pcs::kMaxComponents;
        juce::String title;
    };

    struct Entry
    {
        enum class Status { Pending, Analysed, Failed };
        juce::String path, name;
        Status status = Status::Pending;
        double f0 = 0.0;      // detected fundamental once analysed
        juce::String message; // why it failed
    };

    // `onModel` is called on the message thread with each newly trained model.
    explicit Trainer (std::function<void (std::shared_ptr<const pcs::Model>)> onModel);
    ~Trainer() override;

    // ---- message thread ----
    // Adds audio files; folders are searched recursively. Returns how many were added.
    int addFiles (const juce::StringArray& paths);
    void remove (const juce::Array<int>& indices);
    void clear();
    std::vector<Entry> getEntries() const;
    Settings getSettings() const;
    void setSettings (const Settings& s);

    bool start();   // false if already running or fewer than two sounds
    void cancel();
    bool isRunning() const { return isThreadRunning(); }
    double getProgress() const noexcept { return progress.load(); }
    juce::String getStatus() const;
    // Increments whenever entries, settings or status change (for the editor).
    int getVersion() const noexcept { return version.load(); }

    // Saved with the plugin state (file paths and settings, not audio).
    juce::ValueTree toValueTree() const;
    void fromValueTree (const juce::ValueTree& tree);

    // Runs the whole job on the calling thread (tests, offline use).
    std::shared_ptr<const pcs::Model> trainNow (juce::String& error);

    static bool isAudioFile (const juce::File& f);
    static const juce::Identifier treeType;

private:
    void run() override;
    std::shared_ptr<const pcs::Model> train (juce::String& error);
    void setStatus (const juce::String& s);
    void changed() { ++version; }

    struct Cached
    {
        juce::Time modified;
        std::string settingsKey;
        pcs::HarmonicSound sound;
    };

    std::function<void (std::shared_ptr<const pcs::Model>)> onModel;
    mutable juce::CriticalSection lock;
    std::vector<Entry> entries;
    Settings settings;
    juce::String status;
    std::map<juce::String, Cached> cache;
    juce::AudioFormatManager formats;
    std::atomic<double> progress { 0.0 };
    std::atomic<int> version { 0 };
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
};
