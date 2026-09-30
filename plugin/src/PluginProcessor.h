#pragma once

#include "Params.h"
#include "Trainer.h"

#include "pcs/Model.h"
#include "pcs/Synth.h"
#include "pcs/Wav.h"

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
    bool supportsMPE() const override { return true; }
    double getTailLengthSeconds() const override;

    // One host program. Presets live in the editor's preset bar: exposing them
    // as VST3 programs adds a program-change parameter whose restore fights
    // the saved state (pluginval's state-restoration test fails).
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return getPresetName(); }
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
    juce::String saveModelFile (const juce::File& file) const; // empty on success

    // Training a new space from audio files (Stage 4). A finished training
    // replaces the model and moves the point to the centre of the new space.
    Trainer& getTrainer() noexcept { return trainer; }

    // The point in the space: PC1..16 parameters plus the detail components
    // 17..32. setPoint writes all of them (PCs clamped to ±4); wrap drags in
    // begin/endPointGesture so hosts record one automation gesture.
    using Point = std::array<float, pcs::kMaxComponents>;
    Point getPoint() const;
    void setPoint (const Point& z);
    void beginPointGesture();
    void endPointGesture();
    // Moves the point to a training sound, or back to the centre of the space.
    void jumpToSound (int index);
    void resetToMean();
    Point getDetail() const noexcept;

    // For the editor: voices sounding and where they are in the envelope.
    int getActiveVoices() const noexcept { return activeVoices.load(); }
    int getVoicePositions (float* positions, int max) const noexcept;
    int getLastNote() const noexcept { return lastNote.load(); }

    // Renders one note at the current point and settings to a WAV file on a
    // background thread; `done(error)` is called on the message thread.
    void exportWav (const juce::File& file, int note, double seconds, std::function<void (juce::String)> done);
    // The render behind exportWav: one note held until `seconds` minus the release (stereo, 48 kHz).
    static pcs::AudioBuffer renderNote (std::shared_ptr<const pcs::Model> model, const pcs::SynthParams& params, int note,
                                        double seconds);
    pcs::SynthParams currentSynthParams() const; // parameters + detail + direction sound + tempo

    // Stage 5: the "direction" sound for Toward Sound destinations (LFOs,
    // expression, macro), by name; empty for none. Saved with the state.
    void setDirectionSound (const juce::String& name);
    juce::String getDirectionSound() const;

    // The point actually heard (home plus walk, LFOs, wheel, pressure, macro)
    // and each sounding voice's own point, as of the last audio block.
    Point getHeardPoint() const noexcept;
    int getVoicePoints (Point* points, int max) const noexcept;
    bool isAudioRunning() const noexcept
    {
        return hasProcessed.load() && juce::Time::getMillisecondCounter() - lastBlockMs.load() < 300;
    }
    static constexpr int kMaxShownVoices = 16;

    // Stage 6: per-note expression of the sounding voices (for the MPE tab),
    // and the most recent note's point (for the envelope view).
    int getVoiceInfo (pcs::Synth::VoiceInfo* out, int max) const;
    bool getNewestVoicePoint (Point& out) const noexcept;

    // ---- presets (Stage 8, message thread) ----
    // A factory preset: the factory space, every parameter at its default but
    // the preset's own, and its point.
    void loadFactoryPreset (int index);
    int getFactoryPresetIndex() const noexcept { return currentProgram.load(); } // -1: none
    // User presets hold the whole state (model included) but not the editor's
    // layout or the training list. Both return an error, empty on success.
    juce::String savePresetFile (const juce::File& file);
    juce::String loadPresetFile (const juce::File& file);
    juce::String getPresetName() const { return getUiValue ("presetName", "").toString(); }

    // Editor settings saved with the state (map axes, morph corners, ...).
    juce::var getUiValue (const juce::Identifier& key, const juce::var& fallback) const;
    void setUiValue (const juce::Identifier& key, const juce::var& value);

    // Frees a model the audio thread has let go of (the timer calls this).
    void collectGarbage() { delete retired.exchange (nullptr); }
    // Applies MPE setup the controller sent (zone, bend range) to the parameters (the timer calls this).
    void applyControllerSetup();

    static constexpr int kStateVersion = 1;

private:
    void timerCallback() override;
    void writeState (juce::MemoryBlock& dest, bool asPreset);
    void setParamValue (const juce::String& id, float realValue);
    void setDetail (const Point& d) noexcept;

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
    std::atomic<int> activeVoices { 0 }, lastNote { 60 };
    std::array<std::atomic<float>, pcs::Synth::kMaxVoices> voicePos {};
    std::atomic<int> numVoicePos { 0 };
    std::array<std::atomic<float>, pcs::kMaxComponents> heardPoint {}, direction {};
    std::array<std::array<std::atomic<float>, pcs::kMaxComponents>, kMaxShownVoices> voicePoint {};
    std::atomic<int> numVoicePoints { 0 };
    std::atomic<bool> hasDirection { false };
    std::atomic<double> bpm { 120.0 };
    std::atomic<juce::uint32> lastBlockMs { 0 };
    std::atomic<bool> hasProcessed { false };

    // MPE voice display (audio thread writes with a try-lock; the editor copies).
    mutable juce::SpinLock voiceInfoLock;
    std::array<pcs::Synth::VoiceInfo, kMaxShownVoices> voiceInfos {};
    int numVoiceInfos = 0;
    std::array<std::atomic<float>, pcs::kMaxComponents> newestPoint {};
    std::atomic<bool> hasNewest { false };
    std::atomic<int> currentProgram { -1 }; // factory preset loaded last (-1: none, or a user preset)

    // MPE setup from the controller: RPN 6 (MPE Configuration Message) sets the
    // zone and turns MPE on; RPN 0 on a member channel sets the note bend
    // range. Parsed on the audio thread, applied to parameters by the timer.
    struct Rpn
    {
        int msb = 127, lsb = 127, dataMsb = 0;
    };
    std::array<Rpn, 17> rpn {};
    std::atomic<int> pendingZone { -1 }, pendingBendRange { -1 };
    void parseRpn (const juce::MidiMessage& m) noexcept;
    void resolveDirection();
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);

    // Last: destroyed first, so its thread stops before anything it calls back into.
    Trainer trainer { [this] (std::shared_ptr<const pcs::Model> m) {
        setModel (std::move (m), false);
        resetToMean();
    } };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PCASynthProcessor)
};
