#include "Inspector.h"

#include "Trainer.h"

Inspector::Inspector (ModelSource m, FileSource f)
    : juce::Thread ("PCASynth inspector"), modelSource (std::move (m)), fileSource (std::move (f))
{
    startThread (juce::Thread::Priority::low);
}

Inspector::~Inspector()
{
    signalThreadShouldExit();
    wake.signal();
    stopThread (10000);
}

void Inspector::syncModel()
{
    auto current = modelSource();
    if (current == model)
        return;
    model = std::move (current);
    result.reset();
    resultIndex = resultComponents = -1;
    requested = -1;
    evaluating = false;
    scores.assign (model != nullptr ? static_cast<size_t> (model->numSounds()) : 0u, Scores {});
    status.clear();
}

void Inspector::inspect (int soundIndex, int components)
{
    {
        const juce::ScopedLock sl (lock);
        syncModel();
        requested = soundIndex;
        requestedComponents = components;
        status = "Inspecting...";
    }
    wake.signal();
    sendChangeMessage();
}

void Inspector::evaluateAll()
{
    {
        const juce::ScopedLock sl (lock);
        syncModel();
        evaluating = true;
        progress = 0.0;
    }
    wake.signal();
    sendChangeMessage();
}

void Inspector::cancelAll()
{
    const juce::ScopedLock sl (lock);
    requested = -1;
    evaluating = false;
    status = "Stopped.";
}

std::shared_ptr<const pcs::SoundInspection> Inspector::getResult() const
{
    const juce::ScopedLock sl (lock);
    return model == modelSource() ? result : nullptr;
}

int Inspector::getResultIndex() const
{
    const juce::ScopedLock sl (lock);
    return model == modelSource() ? resultIndex : -1;
}

int Inspector::getResultComponents() const
{
    const juce::ScopedLock sl (lock);
    return resultComponents;
}

std::vector<Inspector::Scores> Inspector::getScores() const
{
    const juce::ScopedLock sl (lock);
    return model == modelSource() ? scores : std::vector<Scores>();
}

juce::String Inspector::getStatus() const
{
    const juce::ScopedLock sl (lock);
    return status;
}

bool Inspector::isBusy() const
{
    const juce::ScopedLock sl (lock);
    return requested >= 0 || evaluating || working;
}

void Inspector::store (int index, const pcs::SoundInspection& r)
{
    if (index < 0 || index >= static_cast<int> (scores.size()))
        return;
    auto& s = scores[static_cast<size_t> (index)];
    s.done = true;
    s.error.clear();
    s.pitch = r.pitch;
    s.analysis = r.analysisError;
    s.analysisAttack = r.analysisAttackError;
    s.model = r.modelError;
    s.modelAttack = r.modelAttackError;
    s.pca = r.pcaError;
    s.envelope = r.envelopeError;
}

void Inspector::run()
{
    while (! threadShouldExit())
    {
        // Take the next job: a single sound first, else the next unscored sound.
        std::shared_ptr<const pcs::Model> m;
        int index = -1, components = -1;
        bool full = false;
        {
            const juce::ScopedLock sl (lock);
            if (model != nullptr && requested >= 0)
            {
                index = requested;
                components = requestedComponents;
                requested = -1;
                full = true;
            }
            else if (model != nullptr && evaluating)
            {
                for (size_t i = 0; i < scores.size() && index < 0; ++i)
                    if (! scores[i].done && scores[i].error.isEmpty())
                        index = static_cast<int> (i);
                if (index < 0)
                {
                    evaluating = false;
                    status = "Evaluated every sound.";
                    progress = 1.0;
                }
                else
                    status = "Evaluating " + juce::String (model->names[static_cast<size_t> (index)]) + "...";
            }
            m = model;
            working = index >= 0 && m != nullptr;
        }
        if (index < 0 || m == nullptr)
        {
            sendChangeMessage();
            wake.wait (-1);
            continue;
        }
        if (! full)
            sendChangeMessage();

        const auto name = juce::String (m->names[static_cast<size_t> (index)]);
        juce::String error;
        std::shared_ptr<pcs::SoundInspection> r;
        const auto file = fileSource (name);
        pcs::AudioBuffer audio;
        if (file == juce::File())
            error = name + ": its file isn't in the training list (add the training files under Train...)";
        else if (Trainer::readAudio (file, m->durationSeconds() + 10.0, audio, error))
        {
            try
            {
                pcs::InspectOptions options;
                options.components = components;
                r = std::make_shared<pcs::SoundInspection> (pcs::inspectSound (*m, index, audio, options));
            }
            catch (const std::exception& e)
            {
                error = e.what();
            }
        }
        else
            error = name + ": " + error;

        {
            const juce::ScopedLock sl (lock);
            working = false;
            if (m != model)
                continue; // the model changed meanwhile
            if (r != nullptr && (components < 0 || components >= m->numComponents()))
                store (index, *r);
            else if (r == nullptr && index < static_cast<int> (scores.size()))
                scores[static_cast<size_t> (index)].error = error;
            if (full)
            {
                result = r;
                resultIndex = index;
                resultComponents = components;
                status = r != nullptr ? juce::String() : error;
            }
            int done = 0;
            for (const auto& s : scores)
                done += s.done || s.error.isNotEmpty() ? 1 : 0;
            if (evaluating)
                progress = scores.empty() ? 1.0 : static_cast<double> (done) / static_cast<double> (scores.size());
        }
        sendChangeMessage();
    }
}
