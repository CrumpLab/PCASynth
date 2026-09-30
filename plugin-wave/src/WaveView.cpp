#include "WaveView.h"

#include "Theme.h"

WaveView::WaveView (PCAWaveProcessor& p) : processor (p) {}

void WaveView::setModel (std::shared_ptr<const pcs::Space> space)
{
    model = std::dynamic_pointer_cast<const pcs::WaveModel> (space);
    dirty = true;
    refresh();
}

PCAWaveProcessor::Point WaveView::currentPoint() const
{
    // The point as the synth hears it: exaggerated and truncated to Components
    // Used, plus any modulation while audio runs (the newest note's own point).
    auto& params = processor.getParameters();
    auto z = processor.getPoint();
    const float ex = params.getRawParameterValue (pcsplugin::id::exaggerate)->load();
    const int used = juce::roundToInt (params.getRawParameterValue (pcsplugin::id::components)->load());
    for (int j = 0; j < pcs::kMaxComponents; ++j)
        z[static_cast<size_t> (j)] = j < used ? z[static_cast<size_t> (j)] * ex : 0.0f;
    if (processor.isAudioRunning())
    {
        z = processor.getHeardPoint();
        PCAWaveProcessor::Point newest;
        if (processor.getNewestVoicePoint (newest))
            z = newest;
    }
    return z;
}

void WaveView::refresh()
{
    const auto z = currentPoint();
    const auto now = juce::Time::getMillisecondCounter();
    if (dirty || (z != shown && now - lastBuild > 150))
    {
        shown = z;
        rebuild();
        lastBuild = now;
        dirty = false;
        repaint();
    }
    std::array<float, pcs::Synth::kMaxVoices> heads {};
    const int n = processor.getVoicePositions (heads.data(), static_cast<int> (heads.size()));
    if (n != numPlayheads || heads != playheads)
    {
        playheads = heads;
        numPlayheads = n;
        repaint();
    }
}

void WaveView::rebuild()
{
    wave.clear();
    spectrum = {};
    if (model == nullptr)
        return;
    wave = model->decode (shown.data(), pcs::kMaxComponents);
    // The spectrogram, 50 frames per second.
    spec = pcs::spectrogram (wave, model->sampleRate, 50.0);
    spectrum = juce::Image (juce::Image::RGB, spec.numFrames, spec.numBands, false);
    float peak = -200.0f;
    for (float v : spec.db)
        peak = std::max (peak, v);
    for (int t = 0; t < spec.numFrames; ++t)
        for (int b = 0; b < spec.numBands; ++b)
            spectrum.setPixelAt (t, spec.numBands - 1 - b, theme::levelColour (juce::jlimit (0.0f, 1.0f, (spec.at (t, b) - peak + 60.0f) / 60.0f)));
}

void WaveView::paint (juce::Graphics& g)
{
    theme::drawPanel (g, getLocalBounds(), "Waveform at the point");
    auto r = getLocalBounds().reduced (10).withTrimmedTop (22).toFloat();
    if (model == nullptr || wave.empty())
        return;
    auto waveArea = r.removeFromTop (r.getHeight() * 0.42f);
    r.removeFromTop (6.0f);
    auto specArea = r;

    // Overview: min/max per pixel column.
    g.setColour (theme::background);
    g.fillRect (waveArea);
    const int width = juce::jmax (1, static_cast<int> (waveArea.getWidth()));
    float peak = 1e-6f;
    for (float v : wave)
        peak = std::max (peak, std::abs (v));
    const float mid = waveArea.getCentreY(), half = waveArea.getHeight() * 0.46f;
    g.setColour (theme::accent);
    for (int x = 0; x < width; ++x)
    {
        const auto a = static_cast<size_t> (static_cast<double> (x) / width * static_cast<double> (wave.size()));
        const auto b = std::max (a + 1, static_cast<size_t> (static_cast<double> (x + 1) / width * static_cast<double> (wave.size())));
        float lo = 0.0f, hi = 0.0f;
        for (size_t i = a; i < b && i < wave.size(); ++i)
        {
            lo = std::min (lo, wave[i]);
            hi = std::max (hi, wave[i]);
        }
        g.drawVerticalLine (static_cast<int> (waveArea.getX()) + x, mid - half * hi / peak, mid - half * lo / peak + 1.0f);
    }

    // Loop or scan markers, and playheads.
    auto& params = processor.getParameters();
    const int mode = juce::roundToInt (params.getRawParameterValue (pcsplugin::id::playMode)->load());
    auto xAt = [&] (double fraction) { return waveArea.getX() + waveArea.getWidth() * static_cast<float> (fraction); };
    g.setColour (theme::text.withAlpha (0.6f));
    const float dash[] = { 3.0f, 3.0f };
    auto marker = [&] (double fraction) {
        g.drawDashedLine (juce::Line<float> (xAt (fraction), waveArea.getY(), xAt (fraction), waveArea.getBottom()), dash, 2);
    };
    if (mode == 1 || mode == 2)
    {
        marker (params.getRawParameterValue (pcsplugin::id::loopStart)->load());
        marker (params.getRawParameterValue (pcsplugin::id::loopEnd)->load());
    }
    else if (mode == 3)
        marker (params.getRawParameterValue (pcsplugin::id::scanPosition)->load());
    g.setColour (theme::cursor);
    for (int i = 0; i < numPlayheads; ++i)
    {
        const float x = xAt (playheads[static_cast<size_t> (i)] / std::max (1.0, static_cast<double> (model->numSamples - 1)));
        g.drawVerticalLine (static_cast<int> (x), waveArea.getY(), waveArea.getBottom());
    }

    // A close look: four cycles (at the space's common pitch) from the loop start, top right.
    const double startFraction = mode == 3 ? params.getRawParameterValue (pcsplugin::id::scanPosition)->load()
                                           : params.getRawParameterValue (pcsplugin::id::loopStart)->load();
    const auto first = static_cast<size_t> (startFraction * static_cast<double> (wave.size() - 1));
    const auto count = static_cast<size_t> (4.0 * model->sampleRate / model->refHz);
    auto inset = waveArea.removeFromRight (juce::jmin (220.0f, waveArea.getWidth() * 0.3f)).reduced (4.0f);
    g.setColour (theme::panel.withAlpha (0.9f));
    g.fillRect (inset);
    g.setColour (theme::axis);
    g.drawRect (inset);
    juce::Path cycles;
    float cpeak = 1e-6f;
    for (size_t i = first; i < first + count && i < wave.size(); ++i)
        cpeak = std::max (cpeak, std::abs (wave[i]));
    for (size_t i = 0; i < count && first + i < wave.size(); ++i)
    {
        const float x = inset.getX() + inset.getWidth() * static_cast<float> (i) / static_cast<float> (count);
        const float y = inset.getCentreY() - inset.getHeight() * 0.45f * wave[first + i] / cpeak;
        i == 0 ? cycles.startNewSubPath (x, y) : cycles.lineTo (x, y);
    }
    g.setColour (theme::accentLight);
    g.strokePath (cycles, juce::PathStrokeType (1.2f));
    g.setColour (theme::faint);
    g.setFont (theme::font (10.0f));
    g.drawText ("4 cycles", inset.reduced (3.0f), juce::Justification::topLeft);

    // Spectrogram.
    g.setColour (theme::background);
    g.fillRect (specArea);
    if (spectrum.isValid())
        g.drawImage (spectrum, specArea, juce::RectanglePlacement::stretchToFit);
    g.setFont (theme::font (10.0f));
    for (double hz : { 100.0, 1000.0, 10000.0 })
    {
        const double b = spec.bandsPerOctave * std::log2 (hz / spec.loHz);
        if (b < 0 || b >= spec.numBands)
            continue;
        const float y = specArea.getBottom() - specArea.getHeight() * static_cast<float> ((b + 0.5) / spec.numBands);
        g.setColour (theme::text.withAlpha (0.55f));
        g.drawText (hz >= 1000.0 ? juce::String (hz / 1000.0, 0) + "k" : juce::String (hz, 0),
                    juce::Rectangle<float> (specArea.getRight() - 28.0f, y - 6.0f, 26.0f, 12.0f), juce::Justification::centredRight);
    }
    g.setColour (theme::faint);
    g.drawText (juce::String (model->durationSeconds(), 1) + " s at its common pitch",
                specArea.reduced (4.0f, 2.0f), juce::Justification::bottomLeft);
}
