#include "Controls.h"

#include "Theme.h"

// ---- ComponentStrip -------------------------------------------------------------

ComponentStrip::ComponentStrip (PCASynthProcessor& p) : processor (p)
{
    for (int j = 0; j < pcsplugin::kNumPcParams; ++j)
    {
        auto& s = sliders[static_cast<size_t> (j)];
        s.setSliderStyle (juce::Slider::LinearVertical);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 44, 16);
        s.setDoubleClickReturnValue (true, 0.0);
        s.setTooltip ("PC" + juce::String (j + 1) + " (SD). Double-click for 0.");
        addAndMakeVisible (s);
        attachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            processor.getParameters(), pcsplugin::pcId (j), s));
    }
}

void ComponentStrip::setModel (std::shared_ptr<const pcs::Model> m)
{
    model = std::move (m);
    used = -1;
    refresh();
    repaint();
}

void ComponentStrip::refresh()
{
    const int u = juce::roundToInt (processor.getParameters().getRawParameterValue (pcsplugin::id::components)->load());
    const int k = model != nullptr ? juce::jmin (u, model->numComponents()) : 0;
    if (k == used)
        return;
    used = k;
    for (int j = 0; j < pcsplugin::kNumPcParams; ++j)
        sliders[static_cast<size_t> (j)].setEnabled (j < k);
    repaint();
}

void ComponentStrip::resized()
{
    auto r = getLocalBounds().reduced (10);
    r.removeFromTop (22 + 34); // title, labels and variance bars
    const float w = r.getWidth() / static_cast<float> (pcsplugin::kNumPcParams);
    for (int j = 0; j < pcsplugin::kNumPcParams; ++j)
        sliders[static_cast<size_t> (j)].setBounds (juce::Rectangle<float> (r.getX() + j * w, static_cast<float> (r.getY()), w, static_cast<float> (r.getHeight())).toNearestInt());
}

void ComponentStrip::paint (juce::Graphics& g)
{
    theme::drawPanel (g, getLocalBounds(), "Components (SD from the mean)");
    auto r = getLocalBounds().reduced (10);
    r.removeFromTop (22);
    auto labels = r.removeFromTop (34);
    const float w = labels.getWidth() / static_cast<float> (pcsplugin::kNumPcParams);
    const double top = model != nullptr && model->numComponents() > 0 ? model->varianceExplained (0) : 1.0;
    for (int j = 0; j < pcsplugin::kNumPcParams; ++j)
    {
        const auto cell = juce::Rectangle<float> (labels.getX() + j * w, static_cast<float> (labels.getY()), w, static_cast<float> (labels.getHeight())).reduced (4.0f, 0.0f);
        const bool on = j < used;
        g.setColour (on ? theme::text : theme::faint);
        g.setFont (theme::font (12.0f, true));
        g.drawText ("PC" + juce::String (j + 1), cell.withHeight (14.0f), juce::Justification::centred);
        if (model != nullptr && j < model->numComponents())
        {
            const double ve = model->varianceExplained (j);
            auto track = cell.withTrimmedTop (17.0f).withHeight (4.0f);
            g.setColour (theme::grid.brighter (0.2f));
            g.fillRoundedRectangle (track, 2.0f);
            g.setColour (on ? theme::accentLight : theme::axis);
            g.fillRoundedRectangle (track.withWidth (juce::jmax (2.0f, static_cast<float> (track.getWidth() * ve / top))), 2.0f);
            g.setColour (theme::faint);
            g.setFont (theme::font (10.0f));
            g.drawText ((ve < 0.1 ? juce::String (100.0 * ve, 1) : juce::String (juce::roundToInt (100.0 * ve))) + " %", cell.withTrimmedTop (22.0f), juce::Justification::centred);
        }
    }
}

// ---- ControlPanel ------------------------------------------------------------------

ControlPanel::ControlPanel (PCASynthProcessor& p) : processor (p)
{
    auto group = [this] (const juce::String& title, std::initializer_list<std::pair<const char*, const char*>> items, bool mode = false) {
        Group gr;
        gr.title = title;
        gr.hasMode = mode;
        for (const auto& [paramId, label] : items)
            gr.knobs.push_back (&add (paramId, label));
        groups.push_back (gr);
    };
    group ("Point", { { "exaggerate", "Exaggerate" }, { "components", "Components" }, { "morph_time", "Morph Time" }, { "keytrack", "Keytrack" } });
    group ("Playback", { { "loop_start", "Loop Start" }, { "loop_end", "Loop End" }, { "scan_position", "Scan" }, { "speed", "Speed" } }, true);
    group ("Voice", { { "attack", "Attack" }, { "release", "Release" }, { "brightness", "Brightness" }, { "harmonics", "Harmonics" }, { "noise", "Noise" } });
    group ("Play", { { "velocity", "Velocity" }, { "bend_range", "Bend" }, { "polyphony", "Voices" }, { "gain", "Gain" } });

    playMode.addItemList (pcsplugin::playModeNames(), 1);
    addAndMakeVisible (playMode);
    playModeLabel.setFont (theme::font (11.0f));
    playModeLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (playModeLabel);
    playModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.getParameters(), pcsplugin::id::playMode, playMode);
}

ControlPanel::Knob& ControlPanel::add (const juce::String& paramId, const juce::String& label)
{
    auto k = std::make_unique<Knob>();
    k->slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 14);
    k->slider.setTooltip (processor.getParameters().getParameter (paramId)->getName (64));
    k->label.setText (label, juce::dontSendNotification);
    k->label.setFont (theme::font (11.0f));
    k->label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (k->slider);
    addAndMakeVisible (k->label);
    k->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.getParameters(), paramId, k->slider);
    knobs.push_back (std::move (k));
    return *knobs.back();
}

void ControlPanel::resized()
{
    auto r = getLocalBounds();
    int columns = 0;
    for (const auto& g : groups)
        columns += static_cast<int> (g.knobs.size()) + (g.hasMode ? 1 : 0);
    const int gap = 8;
    const float colW = (r.getWidth() - gap * (static_cast<int> (groups.size()) - 1)) / static_cast<float> (columns);
    float x = static_cast<float> (r.getX());
    for (auto& g : groups)
    {
        const int n = static_cast<int> (g.knobs.size()) + (g.hasMode ? 1 : 0);
        g.bounds = juce::Rectangle<float> (x, static_cast<float> (r.getY()), colW * n, static_cast<float> (r.getHeight())).toNearestInt();
        auto inner = g.bounds.reduced (6, 6);
        inner.removeFromTop (20);
        const int cellW = g.hasMode ? (inner.getWidth() - juce::jmax (inner.getWidth() / n, 96)) / (n - 1) : inner.getWidth() / n;
        if (g.hasMode)
        {
            auto cell = inner.removeFromLeft (juce::jmax (cellW, 96));
            playModeLabel.setBounds (cell.removeFromTop (16));
            playMode.setBounds (cell.withSizeKeepingCentre (cell.getWidth() - 6, 24));
        }
        for (auto* k : g.knobs)
        {
            auto cell = inner.removeFromLeft (cellW);
            k->label.setBounds (cell.removeFromTop (16));
            k->slider.setBounds (cell);
        }
        x += colW * n + gap;
    }
}

void ControlPanel::paint (juce::Graphics& g)
{
    for (const auto& gr : groups)
        theme::drawPanel (g, gr.bounds, gr.title);
}
