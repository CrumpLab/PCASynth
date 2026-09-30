#pragma once

#include "WaveProcessor.h"

#include "SpaceEditor.h"

#include "pcs/Inspect.h"

// The sound at the current point of a waveform space: its waveform over the
// whole note (with loop or scan markers and each voice's playhead), a close
// look at a few of its cycles, and its spectrogram.
class WaveView final : public SpaceView
{
public:
    explicit WaveView (PCAWaveProcessor& p);

    void setModel (std::shared_ptr<const pcs::Space> space) override;
    void refresh() override; // rebuilds when the point changes (at most every 150 ms); moves playheads
    void paint (juce::Graphics&) override;

    const std::vector<float>& waveform() const noexcept { return wave; }

private:
    void rebuild();
    PCAWaveProcessor::Point currentPoint() const;

    PCAWaveProcessor& processor;
    std::shared_ptr<const pcs::WaveModel> model;
    PCAWaveProcessor::Point shown {};
    bool dirty = true;
    juce::uint32 lastBuild = 0;
    std::vector<float> wave;
    std::vector<float> columnMin, columnMax; // the overview, per pixel column
    juce::Image spectrum;
    pcs::Spectrogram spec;
    std::array<float, pcs::Synth::kMaxVoices> playheads {};
    int numPlayheads = 0;
};
