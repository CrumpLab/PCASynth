#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Colours and look-and-feel for the editor (dark surface, one accent hue).
namespace theme {

const juce::Colour background { 0xff1a1a19 };  // chart surface
const juce::Colour panel { 0xff222220 };
const juce::Colour bar { 0xff262624 };
const juce::Colour grid { 0xff2c2c2a };
const juce::Colour axis { 0xff52514e };
const juce::Colour text { 0xffffffff };
const juce::Colour muted { 0xffc3c2b7 };
const juce::Colour faint { 0xff8a897f };
const juce::Colour accent { 0xff3987e5 };      // series / interactive
const juce::Colour accentLight { 0xff9ec5f4 };
const juce::Colour cursor { 0xfff0a030 };      // the current point (distinct from the data hue)

// Sequential ramp for levels: floor (recedes into the surface) -> loudest.
juce::Colour levelColour (float t) noexcept; // t in [0, 1]

juce::FontOptions font (float size, bool bold = false);

class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end,
                           juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
};

// Section title drawn at the top-left of a panel.
void drawPanel (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title);

} // namespace theme
