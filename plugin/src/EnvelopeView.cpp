#include "EnvelopeView.h"

#include "Theme.h"

EnvelopeView::EnvelopeView (PCASynthProcessor& p) : processor (p) {}

void EnvelopeView::setModel (std::shared_ptr<const pcs::Model> m)
{
    model = std::move (m);
    dirty = true;
    refresh();
}

void EnvelopeView::refresh()
{
    auto& params = processor.getParameters();
    // The point as the synth hears it: exaggerated, truncated to Components
    // Used, plus any modulation (walk, LFOs, ...) while audio runs.
    auto z = processor.getPoint();
    const float ex = params.getRawParameterValue (pcsplugin::id::exaggerate)->load();
    const int used = juce::roundToInt (params.getRawParameterValue (pcsplugin::id::components)->load());
    for (int j = 0; j < pcs::kMaxComponents; ++j)
        z[static_cast<size_t> (j)] = j < used ? z[static_cast<size_t> (j)] * ex : 0.0f;
    if (processor.isAudioRunning())
        z = processor.getHeardPoint();
    if (dirty || z != shown)
    {
        shown = z;
        rebuild();
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

void EnvelopeView::rebuild()
{
    if (model == nullptr)
    {
        image = {};
        return;
    }
    const int frames = model->numFrames, harmonics = model->numHarmonics;
    db.resize (static_cast<size_t> (frames * harmonics));
    for (int t = 0; t < frames; ++t)
        model->decodeFrame (t, shown.data(), pcs::kMaxComponents, db.data() + static_cast<size_t> (t * harmonics));

    // Colour: display floor .. 0 dB (levels are normalised to the loudest frame of each training sound).
    image = juce::Image (juce::Image::RGB, frames, harmonics, false);
    const float floorDb = displayFloor();
    for (int t = 0; t < frames; ++t)
        for (int h = 0; h < harmonics; ++h)
        {
            const float level = (db[static_cast<size_t> (t * harmonics + h)] - floorDb) / -floorDb;
            image.setPixelAt (t, harmonics - 1 - h, theme::levelColour (level));
        }
}

juce::Rectangle<float> EnvelopeView::plotArea() const
{
    auto r = getLocalBounds().toFloat().reduced (10.0f);
    r.removeFromTop (22.0f);
    r.removeFromLeft (26.0f);
    r.removeFromBottom (16.0f);
    r.removeFromRight (44.0f); // legend
    return r;
}

void EnvelopeView::paint (juce::Graphics& g)
{
    theme::drawPanel (g, getLocalBounds(), "Harmonics over time at the point");
    if (model == nullptr || ! image.isValid())
        return;
    const auto a = plotArea();
    g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
    g.drawImage (image, a, juce::RectanglePlacement::stretchToFit);

    // Axes: harmonic numbers and seconds.
    g.setFont (theme::font (10.0f));
    g.setColour (theme::faint);
    const int harmonics = model->numHarmonics;
    for (int h : { 1, 8, 16, 32, 64, 128 })
        if (h <= harmonics)
        {
            const float y = a.getBottom() - (h - 0.5f) / harmonics * a.getHeight();
            g.drawText (juce::String (h), juce::Rectangle<float> (a.getX() - 26.0f, y - 6.0f, 22.0f, 12.0f), juce::Justification::centredRight);
        }
    const double seconds = model->durationSeconds();
    for (int s = 0; s <= static_cast<int> (seconds); ++s)
    {
        const float x = a.getX() + static_cast<float> (s / seconds) * a.getWidth();
        g.drawText (juce::String (s) + " s", juce::Rectangle<float> (x - 16.0f, a.getBottom() + 2.0f, 32.0f, 12.0f), juce::Justification::centred);
    }

    // Loop region or scan position.
    auto& params = processor.getParameters();
    const int mode = juce::roundToInt (params.getRawParameterValue (pcsplugin::id::playMode)->load());
    auto xAt = [&] (float fraction) { return a.getX() + juce::jlimit (0.0f, 1.0f, fraction) * a.getWidth(); };
    if (mode == 1 || mode == 2)
    {
        const float s = xAt (params.getRawParameterValue (pcsplugin::id::loopStart)->load());
        const float e = xAt (params.getRawParameterValue (pcsplugin::id::loopEnd)->load());
        g.setColour (theme::background.withAlpha (0.35f));
        g.fillRect (juce::Rectangle<float> (a.getX(), a.getY(), juce::jmin (s, e) - a.getX(), a.getHeight()));
        g.fillRect (juce::Rectangle<float> (juce::jmax (s, e), a.getY(), a.getRight() - juce::jmax (s, e), a.getHeight()));
        g.setColour (juce::Colours::transparentBlack);
        g.fillRect (juce::Rectangle<float> (juce::jmin (s, e), a.getY(), std::abs (e - s), a.getHeight()));
        g.setColour (theme::text.withAlpha (0.7f));
        const float dashes[] = { 4.0f, 3.0f };
        for (float x : { s, e })
            g.drawDashedLine ({ x, a.getY(), x, a.getBottom() }, dashes, 2, 1.5f);
    }
    else if (mode == 3)
    {
        g.setColour (theme::muted);
        g.drawVerticalLine (juce::roundToInt (xAt (params.getRawParameterValue (pcsplugin::id::scanPosition)->load())), a.getY(), a.getBottom());
    }

    // Playheads.
    g.setColour (theme::cursor);
    for (int i = 0; i < numPlayheads; ++i)
    {
        const float x = xAt (playheads[static_cast<size_t> (i)] / static_cast<float> (model->numFrames - 1));
        g.fillRect (juce::Rectangle<float> (x - 1.0f, a.getY(), 2.0f, a.getHeight()));
    }

    // Legend: the level ramp.
    const auto legend = juce::Rectangle<float> (a.getRight() + 10.0f, a.getY(), 10.0f, a.getHeight());
    for (int i = 0; i < static_cast<int> (legend.getHeight()); ++i)
    {
        g.setColour (theme::levelColour (1.0f - i / legend.getHeight()));
        g.fillRect (legend.getX(), legend.getY() + static_cast<float> (i), legend.getWidth(), 1.0f);
    }
    g.setColour (theme::faint);
    g.drawText ("0 dB", juce::Rectangle<float> (legend.getRight() + 2.0f, legend.getY() - 2.0f, 34.0f, 12.0f), juce::Justification::centredLeft);
    g.drawText (juce::String (juce::roundToInt (displayFloor())), juce::Rectangle<float> (legend.getRight() + 2.0f, legend.getBottom() - 10.0f, 34.0f, 12.0f), juce::Justification::centredLeft);
}
