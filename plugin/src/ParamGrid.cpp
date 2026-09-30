#include "ParamGrid.h"

#include "Theme.h"

// ---- ParamGrid -------------------------------------------------------------------------

ParamGrid::ParamGrid (PCASynthProcessor& p) : processor (p) {}

void ParamGrid::group (const juce::String& title, int row)
{
    Group g;
    g.title = title;
    g.row = row;
    groups.push_back (std::move (g));
}

ParamGrid::Item& ParamGrid::add (juce::Component& c, const juce::String& label, int cells)
{
    Item item;
    item.component = &c;
    item.cells = cells;
    item.label = std::make_unique<juce::Label> (juce::String(), label);
    item.label->setFont (theme::font (11.0f));
    item.label->setJustificationType (juce::Justification::centred);
    addAndMakeVisible (c);
    addAndMakeVisible (*item.label);
    groups.back().items.push_back (std::move (item));
    return groups.back().items.back();
}

juce::Slider& ParamGrid::knob (const juce::String& paramId, const juce::String& label)
{
    auto s = std::make_unique<juce::Slider> (juce::Slider::RotaryVerticalDrag, juce::Slider::TextBoxBelow);
    s->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 14);
    if (compact)
    {
        s->setSliderStyle (juce::Slider::LinearBar);
        s->setColour (juce::Slider::trackColourId, theme::accent.withAlpha (0.55f));
    }
    s->setVelocityBasedMode (false);
    s->setTooltip (processor.getParameters().getParameter (paramId)->getName (64));
    auto& ref = *s;
    add (ref, label, 1);
    sliderAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.getParameters(), paramId, ref));
    owned.push_back (std::move (s));
    return ref;
}

juce::ComboBox& ParamGrid::menu (const juce::String& paramId, const juce::String& label, int widthCells)
{
    auto c = std::make_unique<juce::ComboBox>();
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (processor.getParameters().getParameter (paramId)))
        c->addItemList (choice->choices, 1);
    auto& ref = *c;
    add (ref, label, widthCells);
    comboAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.getParameters(), paramId, ref));
    owned.push_back (std::move (c));
    return ref;
}

juce::ToggleButton& ParamGrid::toggle (const juce::String& paramId, const juce::String& label)
{
    auto t = std::make_unique<juce::ToggleButton>();
    t->setTooltip (processor.getParameters().getParameter (paramId)->getName (64));
    auto& ref = *t;
    add (ref, label, 1);
    buttonAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.getParameters(), paramId, ref));
    owned.push_back (std::move (t));
    return ref;
}

void ParamGrid::custom (juce::Component& c, const juce::String& label, int widthCells) { add (c, label, widthCells); }

float ParamGrid::value (const juce::String& paramId) const
{
    return processor.getParameters().getRawParameterValue (paramId)->load();
}

void ParamGrid::resized()
{
    int rows = 0;
    for (const auto& g : groups)
        rows = std::max (rows, g.row + 1);
    auto area = getLocalBounds();
    const int gap = 8;
    const int rowH = rows > 0 ? (area.getHeight() - gap * (rows - 1)) / rows : 0;
    for (int r = 0; r < rows; ++r)
    {
        auto rowArea = area.removeFromTop (rowH);
        area.removeFromTop (gap);
        int cells = 0, count = 0;
        for (const auto& g : groups)
            if (g.row == r)
            {
                for (const auto& it : g.items)
                    cells += it.cells;
                ++count;
            }
        const float cellW = (rowArea.getWidth() - gap * (count - 1) - 12.0f * count) / static_cast<float> (std::max (1, cells));
        int x = rowArea.getX();
        for (auto& g : groups)
        {
            if (g.row != r)
                continue;
            int groupCells = 0;
            for (const auto& it : g.items)
                groupCells += it.cells;
            const int w = juce::roundToInt (cellW * groupCells + 12.0f);
            g.bounds = { x, rowArea.getY(), w, rowArea.getHeight() };
            x += w + gap;
            auto inner = g.bounds.reduced (6, 6);
            inner.removeFromTop (18);
            for (auto& it : g.items)
            {
                auto cell = inner.removeFromLeft (juce::roundToInt (cellW * it.cells));
                it.label->setBounds (cell.removeFromTop (15));
                auto* sl = dynamic_cast<juce::Slider*> (it.component);
                if (sl != nullptr && sl->getSliderStyle() == juce::Slider::LinearBar)
                    it.component->setBounds (cell.withSizeKeepingCentre (cell.getWidth() - 6, 24));
                else if (sl != nullptr && sl->getSliderStyle() == juce::Slider::IncDecButtons)
                    it.component->setBounds (cell.withSizeKeepingCentre (cell.getWidth() - 4, 26));
                else if (sl != nullptr)
                    it.component->setBounds (cell);
                else if (dynamic_cast<juce::ToggleButton*> (it.component) != nullptr)
                    it.component->setBounds (cell.withSizeKeepingCentre (26, 26));
                else
                    it.component->setBounds (cell.withSizeKeepingCentre (cell.getWidth() - 6, 24));
            }
        }
    }
}

void ParamGrid::paint (juce::Graphics& g)
{
    for (const auto& gr : groups)
        theme::drawPanel (g, gr.bounds, gr.title);
}

// ---- WalkPanel ------------------------------------------------------------------------------

WalkPanel::WalkPanel (PCASynthProcessor& p) : ParamGrid (p)
{
    namespace id = pcsplugin::id;
    group ("Random walk", 0);
    toggle (id::walkOn, "On");
    menu (id::walkMode, "Mode", 2).setTooltip ("Drift: Brownian wandering. Jumps: a new random point every step. "
                                                "Tour: from training sound to training sound. Neighbour Tour: to a nearby sound each step.");
    toggle (id::walkFreeze, "Freeze");

    group ("Motion", 0);
    knob (id::walkAmount, "Amount").setTooltip ("How far it wanders (SD). In tours, 1 = arrive at each sound, 0.5 = halfway");
    rate = &knob (id::walkRate, "Rate");
    toggle (id::walkSync, "Sync");
    step = &menu (id::walkSyncLen, "Step", 2);
    glide = &knob (id::walkGlide, "Glide");
    glide->setTooltip ("Jumps and tours: 0 = jump to each new point, 100 % = glide for the whole step");
    tether = &knob (id::walkTether, "Tether");
    tether->setTooltip ("Drift: how strongly it is pulled back home (0 = wanders freely, bounded at 4 SD)");

    group ("Which components", 0);
    dims = &knob (id::walkDims, "Components");
    dims->setTooltip ("Drift and Jumps move PC1..PCn (tours move every component)");
    focus = &menu (id::walkFocus, "Focus", 2);
    focus->setTooltip ("Equal: every component moves the same (SD). Main: in proportion to the variance each explains");
    knob (id::walkPerVoice, "Per Voice").setTooltip ("0 = one walk for everything; 100 % = every note wanders on its own");

    group ("Repeat", 0);
    auto& seed = knob (id::walkSeed, "Seed");
    seed.setSliderStyle (juce::Slider::IncDecButtons);
    seed.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 50, 22);
    seed.setTooltip ("The same seed gives the same path");
    toggle (id::walkRestart, "Restart").setTooltip ("A note after silence restarts the walk from home (with the seed: the same path every phrase)");
}

void WalkPanel::refresh()
{
    const int mode = juce::roundToInt (value (pcsplugin::id::walkMode));
    const bool sync = value (pcsplugin::id::walkSync) > 0.5f;
    const bool tour = mode >= 2;
    rate->setEnabled (! sync);
    step->setEnabled (sync);
    glide->setEnabled (mode != 0);
    tether->setEnabled (mode == 0);
    dims->setEnabled (! tour);
    focus->setEnabled (! tour);
}

// ---- ModPanel -------------------------------------------------------------------------------

ModPanel::ModPanel (PCASynthProcessor& p) : ParamGrid (p)
{
    namespace id = pcsplugin::id;
    setCompact (true);
    for (int n = 0; n < 2; ++n)
    {
        group ("LFO " + juce::String (n + 1), 0);
        toggle (id::lfo (n, "on"), "On");
        menu (id::lfo (n, "shape"), "Shape", 2);
        lfoRate[static_cast<size_t> (n)] = &knob (id::lfo (n, "rate"), "Rate");
        toggle (id::lfo (n, "sync"), "Sync");
        lfoCycle[static_cast<size_t> (n)] = &menu (id::lfo (n, "sync_len"), "Cycle", 2);
        knob (id::lfo (n, "depth"), "Depth");
        menu (id::lfo (n, "target"), "Target", 2);
    }
    group ("Velocity", 1);
    menu (id::velDest, "To", 2);
    knob (id::velAmount, "Amount");
    group ("Mod wheel", 1);
    menu (id::mwDest, "To", 2);
    knob (id::mwAmount, "Amount");
    group ("Aftertouch", 1);
    menu (id::atDest, "To", 2);
    knob (id::atAmount, "Amount");
    group ("Toward sound", 1);
    directionBox.setTextWhenNothingSelected ("(none)");
    directionBox.setTooltip ("The sound that Toward Sound destinations and the Macro move towards");
    directionBox.onChange = [this] {
        processor.setDirectionSound (directionBox.getSelectedId() > 0 ? directionBox.getText() : juce::String());
    };
    custom (directionBox, "Sound", 3);
    knob (id::macro, "Macro").setTooltip ("How far towards the sound (0-100 %)");
    knob (id::voiceSpread, "Spread").setTooltip ("Voice Spread: each note starts at its own random offset (SD, PC1-8)");
}

void ModPanel::setModel (std::shared_ptr<const pcs::Model> m)
{
    directionBox.clear (juce::dontSendNotification);
    if (m == nullptr)
        return;
    for (int i = 0; i < m->numSounds(); ++i)
        directionBox.addItem (m->names[static_cast<size_t> (i)], i + 1);
    const int index = m->soundIndex (processor.getDirectionSound().toStdString());
    directionBox.setSelectedId (index + 1, juce::dontSendNotification);
}

void ModPanel::refresh()
{
    for (int n = 0; n < 2; ++n)
    {
        const bool sync = value (pcsplugin::id::lfo (n, "sync")) > 0.5f;
        lfoRate[static_cast<size_t> (n)]->setEnabled (! sync);
        lfoCycle[static_cast<size_t> (n)]->setEnabled (sync);
    }
}
