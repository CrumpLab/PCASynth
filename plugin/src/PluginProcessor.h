#pragma once

#include "Params.h"

#include "pcs/Model.h"
#include "pcs/Synth.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <memory>
#include <mutex>

// JUCE wrapper around pcs::Synth. All DSP lives in the engine; this class maps
// host parameters and MIDI onto it, owns the current model (swapped into the
// audio thread without allocating there) and saves it in the plugin state.
class PCASynthProcessor final : public juce::AudioProcessor,
                                public juce::ChangeBroadcaster,
                                private juce::Timer
{
public:
    PCASynthProcessor();
    ~PCASynthProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getParameters() noexcept { return parameters; }

    // ---- the sound space (message thread) ----
    // The built-in model: 60 synthetic instrument notes (see plan.md §3).
    static std::shared_ptr<const pcs::Model> factoryModel();
    void loadFactoryModel();
    juce::String loadModelFile (const juce::File& file); // empty on success
    void setModel (std::shared_ptr<const pcs::Model> model, bool isFactory);
    std::shared_ptr<const pcs::Model> getModel() const;
    bool isFactoryModel() const;
    static bool isModelFile (const juce::String& path) { return path.endsWithIgnoreCase (".pcsm"); }

    // Moves the point to a training sound (PC1..16 parameters plus the detail
    // components 17..32), or back to the centre of the space.
    void jumpToSound (int index);
    void resetToMean();
    std::array<float, pcs::kMaxComponents> getDetail() const noexcept;

    // Smoothed point currently heard and voices sounding (for the editor).
    int getActiveVoices() const noexcept { return activeVoices.load(); }

    // Frees a model the audio thread has let go of (the timer calls this).
    void collectGarbage() { delete retired.exchange (nullptr); }

    static constexpr int kStateVersion = 1;

private:
    void timerCallback() override;
    void setParamValue (const juce::String& id, float realValue);
    void setDetail (const std::array<float, pcs::kMaxComponents>& d) noexcept;

    juce::AudioProcessorValueTreeState parameters;
    pcsplugin::ParamReader reader;
    pcs::Synth synth;
    std::vector<pcs::MidiEvent> events;

    // Model handover: the message thread publishes a prepared slot in
    // `pending`; the audio thread swaps it in and parks the old one in
    // `retired`, which the timer frees.
    std::atomic<pcs::Synth::ModelSlot*> pending { nullptr }, retired { nullptr };

    mutable std::mutex modelLock;
    std::shared_ptr<const pcs::Model> model;
    bool factory = true;

    std::array<std::atomic<float>, pcs::kMaxComponents> detail {};
    std::atomic<int> activeVoices { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PCASynthProcessor)
};
