#include "InspectPanel.h"

#include "Theme.h"

#include "pcs/WaveModel.h"

namespace {
juce::Colour scoreColour (double db)
{
    return db < 2.0 ? theme::muted : db < 4.0 ? theme::accentLight : theme::cursor;
}

juce::String dbText (double db) { return juce::String (db, 1); }

// Waveform spaces' fit is a residual level (dB below the sound): lower is better.
juce::Colour residualColour (double db)
{
    return db < -30.0 ? theme::muted : db < -15.0 ? theme::accentLight : theme::cursor;
}

bool isWave (const pcs::Space* s) { return dynamic_cast<const pcs::WaveModel*> (s) != nullptr; }

juce::Image imageOf (const pcs::Spectrogram& s)
{
    if (s.numFrames <= 0 || s.numBands <= 0)
        return {};
    juce::Image im (juce::Image::RGB, s.numFrames, s.numBands, false);
    float peak = -200.0f;
    for (float v : s.db)
        peak = std::max (peak, v);
    for (int t = 0; t < s.numFrames; ++t)
        for (int b = 0; b < s.numBands; ++b)
            im.setPixelAt (t, s.numBands - 1 - b, theme::levelColour (juce::jlimit (0.0f, 1.0f, (s.at (t, b) - peak + 60.0f) / 60.0f)));
    return im;
}
} // namespace

InspectPanel::InspectPanel (SpaceProcessor& p) : processor (p), inspector (p.getInspector())
{
    list.setRowHeight (20);
    list.setColour (juce::ListBox::backgroundColourId, theme::background);
    list.setOutlineThickness (0);
    for (auto* c : std::initializer_list<juce::Component*> { &list, &evaluateButton, &closeButton, &goButton, &playOriginal,
                                                            &playAnalysis, &playModel, &stopButton, &components, &componentsLabel, &status })
        addAndMakeVisible (c);
    status.setFont (theme::font (12.0f));
    status.setColour (juce::Label::textColourId, theme::muted);
    componentsLabel.setFont (theme::font (12.0f));
    componentsLabel.setJustificationType (juce::Justification::centredRight);
    components.setSliderStyle (juce::Slider::LinearHorizontal);
    components.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 20);
    components.setTooltip ("How many components the Model version uses: hear what the PCA keeps as you add components");
    components.onDragEnd = [this] { selectSound (selected); };
    components.onValueChange = [this] {
        if (! components.isMouseButtonDown())
            selectSound (selected);
        repaint();
    };

    evaluateButton.setTooltip ("Score every training sound (needs their files in the training list)");
    evaluateButton.onClick = [this] {
        if (inspector.isBusy())
            inspector.cancelAll();
        else
            inspector.evaluateAll();
    };
    closeButton.onClick = [this] {
        processor.stopAudition();
        if (onClose)
            onClose();
    };
    goButton.setTooltip ("Move the point to this sound");
    goButton.onClick = [this] {
        if (selected >= 0)
            processor.jumpToSound (selected);
    };
    playOriginal.setTooltip ("The file, from its onset");
    playAnalysis.setTooltip ("Its analysis played back as it is: what the harmonic model keeps");
    playModel.setTooltip ("The sound's point in the model: what the PCA keeps on top");
    stopButton.onClick = [this] { processor.stopAudition(); };
    playOriginal.onClick = [this] { if (result != nullptr) processor.audition (result->original); };
    playAnalysis.onClick = [this] { if (result != nullptr) processor.audition (result->analysis); };
    playModel.onClick = [this] { if (result != nullptr) processor.audition (result->model); };

    inspector.addChangeListener (this);
    refreshModel();
}

InspectPanel::~InspectPanel() { inspector.removeChangeListener (this); }

void InspectPanel::refreshModel()
{
    model = processor.getSpace();
    playAnalysis.setButtonText (isWave (model.get()) ? "Aligned" : "Analysis");
    selected = -1;
    result.reset();
    rebuildImages();
    const int k = model != nullptr ? model->numComponents() : 1;
    components.setRange (1.0, std::max (2, k), 1.0);
    components.setValue (k, juce::dontSendNotification);
    list.updateContent();
    list.deselectAllRows();
    refresh();
}

int InspectPanel::componentsForModel() const
{
    const int k = juce::roundToInt (components.getValue());
    return model != nullptr && k >= model->numComponents() ? -1 : k;
}

void InspectPanel::selectSound (int index)
{
    if (model == nullptr || index < 0 || index >= model->numSounds())
        return;
    selected = index;
    if (list.getSelectedRow() != index)
        list.selectRow (index, false, true);
    inspector.inspect (index, componentsForModel());
    refresh();
}

void InspectPanel::refresh()
{
    if (processor.getSpace() != model)
    {
        refreshModel();
        return;
    }
    scores = inspector.getScores();
    auto r = inspector.getResult();
    if (r != result && inspector.getResultIndex() == selected)
    {
        result = r;
        rebuildImages();
    }
    auto text = inspector.getStatus();
    if (inspector.isBusy() && inspector.getProgress() > 0.0 && inspector.getProgress() < 1.0)
        text += "  " + juce::String (juce::roundToInt (100.0 * inspector.getProgress())) + " %";
    if (text.isEmpty())
        text = selected < 0 ? "Pick a sound to inspect it, or Evaluate All to score every sound." : juce::String();
    status.setText (text, juce::dontSendNotification);
    evaluateButton.setButtonText (inspector.isBusy() ? "Stop" : "Evaluate All");
    const bool have = result != nullptr;
    for (auto* b : { &playOriginal, &playAnalysis, &playModel })
        b->setEnabled (have);
    goButton.setEnabled (selected >= 0);
    list.repaint();
    repaint();
}

void InspectPanel::rebuildImages()
{
    if (result == nullptr)
    {
        images = {};
        return;
    }
    images[0] = imageOf (result->specOriginal);
    images[1] = imageOf (result->specAnalysis);
    images[2] = imageOf (result->specModel);
}

int InspectPanel::getNumRows() { return model != nullptr ? model->numSounds() : 0; }

void InspectPanel::selectedRowsChanged (int row)
{
    if (row >= 0 && row != selected)
        selectSound (row);
}

void InspectPanel::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool isSelected)
{
    if (model == nullptr || row < 0 || row >= model->numSounds())
        return;
    if (isSelected)
        g.fillAll (theme::accent.withAlpha (0.35f));
    auto r = juce::Rectangle<int> (0, 0, width, height).reduced (6, 0);
    g.setFont (theme::font (12.5f));
    g.setColour (theme::text);
    g.drawText (model->names[static_cast<size_t> (row)], r.removeFromLeft (width * 38 / 100), juce::Justification::centredLeft, true);
    const int col = r.getWidth() / 4;
    auto cell = [&] (const juce::String& text, juce::Colour c) {
        g.setColour (c);
        g.drawText (text, r.removeFromLeft (col), juce::Justification::centredRight);
    };
    const bool haveFit = static_cast<int> (model->fitErrorDb.size()) == model->numSounds();
    const auto* s = row < static_cast<int> (scores.size()) ? &scores[static_cast<size_t> (row)] : nullptr;
    cell (juce::MidiMessage::getMidiNoteName (juce::roundToInt (model->soundPitch (row)), true, true, 4), theme::faint);
    if (haveFit)
    {
        const double fit = model->fitErrorDb[static_cast<size_t> (row)];
        cell (dbText (fit), isWave (model.get()) ? residualColour (fit) : scoreColour (fit));
    }
    else
        cell ("-", theme::faint);
    if (s != nullptr && s->done)
    {
        cell (dbText (s->analysis), scoreColour (s->analysis));
        cell (dbText (s->model), scoreColour (s->model));
    }
    else if (s != nullptr && s->error.isNotEmpty())
    {
        g.setColour (theme::cursor);
        g.drawText ("no file", r, juce::Justification::centredRight);
    }
}

void InspectPanel::paint (juce::Graphics& g)
{
    theme::drawPanel (g, getLocalBounds(), "Inspect the model");
    // Column headings over the list.
    auto h = list.getBounds().withHeight (16).translated (0, -17).reduced (6, 0);
    g.setFont (theme::font (11.0f));
    g.setColour (theme::faint);
    g.drawText ("sound", h.removeFromLeft (list.getWidth() * 38 / 100), juce::Justification::centredLeft);
    const int col = h.getWidth() / 4;
    for (const char* t : { "pitch", "fit", "analysis", "model" })
        g.drawText (t, h.removeFromLeft (col), juce::Justification::centredRight);
    paintFitChart (g, fitArea.toFloat());
    paintDetail (g);
}

void InspectPanel::paintFitChart (juce::Graphics& g, juce::Rectangle<float> r) const
{
    g.setColour (theme::background);
    g.fillRect (r);
    g.setFont (theme::font (11.0f));
    g.setColour (theme::faint);
    g.drawText (juce::String ("Fit vs components used: ") + (model != nullptr ? model->fitMeasure() : ""), r.removeFromTop (16.0f).reduced (4.0f, 0.0f),
                juce::Justification::centredLeft);
    if (model == nullptr || model->fitByComponents.size() < 2)
    {
        g.drawText ("(retrain to see how the fit grows with components)", r, juce::Justification::centred);
        return;
    }
    const auto& f = model->fitByComponents;
    // Errors (>= 0) run from 0 up; residuals (dB below the sound, < 0) span their own range.
    const float hi = *std::max_element (f.begin(), f.end()), lo = *std::min_element (f.begin(), f.end());
    const float top = lo < 0.0f ? std::ceil (std::max (hi, lo + 1.0f)) : std::max (0.1f, hi);
    const float bottom = lo < 0.0f ? std::floor (lo) : 0.0f;
    auto plot = r.reduced (32.0f, 6.0f).withTrimmedBottom (10.0f);
    auto x = [&] (size_t k) { return plot.getX() + plot.getWidth() * static_cast<float> (k) / static_cast<float> (f.size() - 1); };
    auto y = [&] (float v) { return plot.getBottom() - plot.getHeight() * (v - bottom) / (top - bottom); };
    g.setColour (theme::grid);
    for (float v : { 0.25f, 0.5f, 0.75f, 1.0f })
        g.drawHorizontalLine (juce::roundToInt (y (bottom + v * (top - bottom))), plot.getX(), plot.getRight());
    g.setColour (theme::faint);
    g.drawText (juce::String (top, lo < 0.0f ? 0 : 1), juce::Rectangle<float> (r.getX(), plot.getY() - 6.0f, 30.0f, 12.0f), juce::Justification::centredRight);
    g.drawText (juce::String (bottom, 0), juce::Rectangle<float> (r.getX(), plot.getBottom() - 6.0f, 30.0f, 12.0f), juce::Justification::centredRight);
    g.drawText (juce::String (static_cast<int> (f.size() - 1)), juce::Rectangle<float> (plot.getRight() - 20.0f, plot.getBottom() + 1.0f, 20.0f, 10.0f),
                juce::Justification::centredRight);
    juce::Path p;
    for (size_t k = 0; k < f.size(); ++k)
        k == 0 ? p.startNewSubPath (x (k), y (f[k])) : p.lineTo (x (k), y (f[k]));
    g.setColour (theme::accent);
    g.strokePath (p, juce::PathStrokeType (1.5f));
    const auto used = static_cast<size_t> (juce::jlimit (0, static_cast<int> (f.size()) - 1, juce::roundToInt (components.getValue())));
    g.setColour (theme::cursor);
    g.drawVerticalLine (juce::roundToInt (x (used)), plot.getY(), plot.getBottom());
    g.fillEllipse (x (used) - 3.0f, y (f[used]) - 3.0f, 6.0f, 6.0f);
    g.drawText (juce::String (static_cast<int> (used)) + ": " + juce::String (f[used], 2) + " dB",
                juce::Rectangle<float> (plot.getRight() - 110.0f, plot.getY(), 110.0f, 12.0f), juce::Justification::centredRight);
}

void InspectPanel::paintDetail (juce::Graphics& g) const
{
    g.setFont (theme::font (13.0f, true));
    g.setColour (theme::text);
    auto header = headerArea;
    if (model == nullptr || selected < 0)
    {
        g.setFont (theme::font (12.0f));
        g.setColour (theme::faint);
        g.drawFittedText (isWave (model.get())
                              ? "Scores are spectral differences in dB: under 2 is close, above 4 clearly different.\n"
                                "analysis = the original vs the aligned waveform the PCA saw (what alignment changes);  "
                                "model = the original vs the sound's point in the space (everything);  "
                                "fit = how far below the sound the PCA's residual is (dB; -60 or lower is exact)."
                              : "Scores are spectral differences in dB: under 2 is close, above 4 clearly different.\n"
                                "analysis = the original vs its analysis (what the harmonic model loses);  "
                                "model = the original vs the sound's point in the model (everything);  "
                                "fit = the harmonic envelopes vs their reconstruction from the components (what the PCA loses).",
                          detailArea.reduced (10), juce::Justification::centred, 6);
        return;
    }
    g.drawText (model->names[static_cast<size_t> (selected)], header.removeFromTop (18), juce::Justification::centredLeft);
    g.setFont (theme::font (12.0f));
    if (result != nullptr)
    {
        const auto k = inspector.getResultComponents();
        g.setColour (theme::muted);
        g.drawText ((result->waveform ? "aligned " : "analysis ") + dbText (result->analysisError) + " dB (attack " + dbText (result->analysisAttackError)
                        + ")   model " + dbText (result->modelError) + " dB (attack " + dbText (result->modelAttackError)
                        + ")   PCA " + dbText (result->pcaError) + " dB, "
                        + (result->waveform ? "waveform residual " : "envelopes ") + dbText (result->envelopeError) + " dB"
                        + (k > 0 ? "   [model: first " + juce::String (k) + " components]" : juce::String()),
                    header, juce::Justification::centredLeft);
    }
    else
    {
        g.setColour (theme::faint);
        g.drawText (inspector.isBusy() ? "Inspecting..." : "", header, juce::Justification::centredLeft);
    }

    const bool waveform = result != nullptr ? result->waveform : isWave (model.get());
    const char* labels[] = { "Original (the file)", waveform ? "Aligned (the waveform the PCA saw, played back)" : "Analysis (no PCA)",
                             "Model (its point in the space)" };
    for (size_t i = 0; i < 3; ++i)
    {
        const auto a = specAreas[i].toFloat();
        g.setColour (theme::background);
        g.fillRect (a);
        if (images[i].isValid())
            g.drawImage (images[i], a, juce::RectanglePlacement::stretchToFit);
        g.setColour (theme::text.withAlpha (0.85f));
        g.setFont (theme::font (11.0f));
        g.drawText (labels[i], a.reduced (4.0f, 2.0f), juce::Justification::topLeft);
        if (result != nullptr && i == 0)
        {
            // Frequency marks (bands are 1/6 octave from 40 Hz).
            const auto& s = result->specOriginal;
            for (double hz : { 100.0, 1000.0, 10000.0 })
            {
                const double b = s.bandsPerOctave * std::log2 (hz / s.loHz);
                if (b < 0 || b >= s.numBands)
                    continue;
                const float yy = a.getBottom() - a.getHeight() * static_cast<float> ((b + 0.5) / s.numBands);
                g.setColour (theme::text.withAlpha (0.5f));
                g.drawText (hz >= 1000.0 ? juce::String (hz / 1000.0, 0) + "k" : juce::String (hz, 0),
                            juce::Rectangle<float> (a.getRight() - 28.0f, yy - 6.0f, 26.0f, 12.0f), juce::Justification::centredRight);
            }
        }
    }

    // Pitch curves.
    const auto a = pitchArea.toFloat();
    g.setColour (theme::background);
    g.fillRect (a);
    g.setFont (theme::font (11.0f));
    g.setColour (theme::faint);
    if (result == nullptr || result->pitchCents.empty())
    {
        g.drawText (result == nullptr ? "Pitch"
                    : result->waveform  ? "Pitch: waveform spaces keep every sound's pitch movement inside its waveform"
                                        : "Pitch: not tracked (retrain with Pitch curve on)",
                    a.reduced (4.0f, 2.0f), juce::Justification::topLeft);
        return;
    }
    float range = 20.0f;
    for (float c : result->pitchCents)
        range = std::max (range, std::abs (c));
    for (float c : result->modelPitchCents)
        range = std::max (range, std::abs (c));
    auto plot = a.reduced (4.0f, 4.0f);
    auto curve = [&] (const std::vector<float>& c, juce::Colour colour) {
        if (c.size() < 2)
            return;
        juce::Path p;
        for (size_t t = 0; t < c.size(); ++t)
        {
            const float xx = plot.getX() + plot.getWidth() * static_cast<float> (t) / static_cast<float> (c.size() - 1);
            const float yy = plot.getCentreY() - plot.getHeight() * 0.5f * c[t] / range;
            t == 0 ? p.startNewSubPath (xx, yy) : p.lineTo (xx, yy);
        }
        g.setColour (colour);
        g.strokePath (p, juce::PathStrokeType (1.3f));
    };
    g.setColour (theme::grid);
    g.drawHorizontalLine (juce::roundToInt (plot.getCentreY()), plot.getX(), plot.getRight());
    curve (result->pitchCents, theme::muted);
    curve (result->modelPitchCents, theme::accent);
    g.setColour (theme::faint);
    g.drawText ("Pitch (cents, +/- " + juce::String (juce::roundToInt (range)) + "): analysed  /  model", a.reduced (4.0f, 2.0f),
                juce::Justification::topLeft);
}

void InspectPanel::resized()
{
    auto r = getLocalBounds().reduced (10);
    auto top = r.removeFromTop (22);
    closeButton.setBounds (top.removeFromRight (80));
    r.removeFromTop (4);

    auto left = r.removeFromLeft (juce::jmin (420, r.getWidth() * 36 / 100));
    r.removeFromLeft (10);
    status.setBounds (left.removeFromBottom (20));
    fitArea = left.removeFromBottom (juce::jmin (130, left.getHeight() / 3));
    left.removeFromBottom (6);
    auto buttons = left.removeFromBottom (26);
    evaluateButton.setBounds (buttons.removeFromLeft (120));
    left.removeFromBottom (4);
    left.removeFromTop (18); // column headings
    list.setBounds (left);

    detailArea = r;
    auto controls = r.removeFromBottom (26);
    goButton.setBounds (controls.removeFromRight (110));
    controls.removeFromRight (8);
    stopButton.setBounds (controls.removeFromRight (60));
    componentsLabel.setBounds (controls.removeFromLeft (80));
    components.setBounds (controls.removeFromLeft (juce::jmin (260, controls.getWidth())));
    r.removeFromBottom (6);
    headerArea = r.removeFromTop (38);
    pitchArea = r.removeFromBottom (juce::jmax (50, r.getHeight() / 6));
    r.removeFromBottom (4);
    const int h = (r.getHeight() - 8) / 3;
    juce::TextButton* plays[] = { &playOriginal, &playAnalysis, &playModel };
    for (size_t i = 0; i < 3; ++i)
    {
        auto row = r.removeFromTop (h);
        plays[i]->setBounds (row.removeFromLeft (76).withSizeKeepingCentre (72, 26));
        row.removeFromLeft (4);
        specAreas[i] = row;
        r.removeFromTop (4);
    }
}
