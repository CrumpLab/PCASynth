#pragma once

#include "pcs/Inspect.h"
#include "pcs/Model.h"

#include <juce_events/juce_events.h>

#include <functional>
#include <memory>
#include <vector>

// Inspecting the model against its training sounds, on a background thread:
// one sound in full (renders, spectrograms, pitch curves, scores) on request,
// and the scores of every sound ("Evaluate all"). The audio comes from the
// training list's files; results belong to the model they were made with.
class Inspector final : public juce::ChangeBroadcaster, private juce::Thread
{
public:
    using ModelSource = std::function<std::shared_ptr<const pcs::Model>()>;
    using FileSource = std::function<juce::File (const juce::String& soundName)>;
    Inspector (ModelSource model, FileSource files);
    ~Inspector() override;

    struct Scores
    {
        bool done = false;
        juce::String error;
        double pitch = 0.0, analysis = 0.0, analysisAttack = 0.0, model = 0.0, modelAttack = 0.0, pca = 0.0, envelope = 0.0;
    };

    // ---- message thread ----
    // Inspects one sound (replacing any request not started yet). `components`
    // limits the model's version to its first K components (-1: all).
    void inspect (int soundIndex, int components = -1);
    void evaluateAll();
    void cancelAll();

    // The latest full inspection (null until one finishes), for the current model.
    std::shared_ptr<const pcs::SoundInspection> getResult() const;
    int getResultIndex() const;
    int getResultComponents() const;
    std::vector<Scores> getScores() const; // one per training sound of the current model
    juce::String getStatus() const;
    double getProgress() const noexcept { return progress.load(); }
    bool isBusy() const;

private:
    void run() override;
    void syncModel(); // forgets results made with another model (call with the lock held)
    void store (int index, const pcs::SoundInspection& r);

    ModelSource modelSource;
    FileSource fileSource;
    mutable juce::CriticalSection lock;
    std::shared_ptr<const pcs::Model> model;
    int requested = -1, requestedComponents = -1;
    bool evaluating = false;
    int resultIndex = -1, resultComponents = -1;
    std::shared_ptr<const pcs::SoundInspection> result;
    std::vector<Scores> scores;
    juce::String status;
    std::atomic<double> progress { 0.0 };
    bool working = false; // a job is being computed (under `lock`)
    juce::WaitableEvent wake;
};
