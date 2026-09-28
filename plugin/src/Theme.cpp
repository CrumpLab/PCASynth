#include "Theme.h"

namespace theme {

juce::Colour levelColour (float t) noexcept
{
    static const juce::Colour stops[] = { juce::Colour (0xff1a1a19), juce::Colour (0xff0d366b), juce::Colour (0xff184f95),
                                          juce::Colour (0xff256abf), juce::Colour (0xff3987e5), juce::Colour (0xff6da7ec),
                                          juce::Colour (0xff9ec5f4), juce::Colour (0xffcde2fb) };
    constexpr int n = static_cast<int> (std::size (stops));
    const float x = juce::jlimit (0.0f, 1.0f, t) * (n - 1);
    const int i = juce::jmin (n - 2, static_cast<int> (x));
    return stops[i].interpolatedWith (stops[i + 1], x - static_cast<float> (i));
}

juce::FontOptions font (float size, bool bold)
{
    return juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain);
}

LookAndFeel::LookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, background);
    setColour (juce::Label::textColourId, muted);
    setColour (juce::TextButton::buttonColourId, bar.brighter (0.15f));
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::ComboBox::backgroundColourId, panel);
    setColour (juce::ComboBox::outlineColourId, axis);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::arrowColourId, muted);
    setColour (juce::PopupMenu::backgroundColourId, panel);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.4f));
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::rotarySliderFillColourId, accent);
    setColour (juce::Slider::trackColourId, accent);
    setColour (juce::TooltipWindow::backgroundColourId, panel);
    setColour (juce::TooltipWindow::textColourId, text);
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end,
                                    juce::Slider& s)
{
    const auto r = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (3.0f);
    const float radius = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f;
    const auto c = r.getCentre();
    const float angle = start + pos * (end - start);
    const bool enabled = s.isEnabled();

    juce::Path track;
    track.addCentredArc (c.x, c.y, radius - 2.0f, radius - 2.0f, 0.0f, start, end, true);
    g.setColour (grid.brighter (0.2f));
    g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Bipolar ranges fill from the centre.
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    const float from = bipolar ? start + static_cast<float> (s.valueToProportionOfLength (0.0)) * (end - start) : start;
    juce::Path value;
    value.addCentredArc (c.x, c.y, radius - 2.0f, radius - 2.0f, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
    g.setColour (enabled ? accent : axis);
    g.strokePath (value, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const juce::Point<float> tip (c.x + (radius - 7.0f) * std::sin (angle), c.y - (radius - 7.0f) * std::cos (angle));
    g.setColour (enabled ? text : faint);
    g.drawLine ({ c, tip }, 2.0f);
}

void LookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                    juce::Slider::SliderStyle style, juce::Slider& s)
{
    if (style != juce::Slider::LinearVertical)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0.0f, 0.0f, style, s);
        return;
    }
    const float cx = x + w * 0.5f;
    g.setColour (grid.brighter (0.2f));
    g.fillRoundedRectangle (cx - 2.0f, static_cast<float> (y), 4.0f, static_cast<float> (h), 2.0f);
    // Fill from zero (the mean) to the value.
    const float zero = s.getMinimum() < 0.0 ? static_cast<float> (y + h - s.valueToProportionOfLength (0.0) * h) : static_cast<float> (y + h);
    g.setColour (s.isEnabled() ? accent : axis);
    g.fillRoundedRectangle (cx - 2.0f, juce::jmin (zero, pos), 4.0f, std::abs (zero - pos), 2.0f);
    g.setColour (s.isEnabled() ? text : faint);
    g.fillEllipse (cx - 6.0f, pos - 6.0f, 12.0f, 12.0f);
}

void drawPanel (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title)
{
    g.setColour (panel);
    g.fillRoundedRectangle (r.toFloat(), 6.0f);
    g.setColour (muted);
    g.setFont (font (12.0f, true));
    g.drawText (title.toUpperCase(), r.reduced (10, 6).removeFromTop (16), juce::Justification::centredLeft);
}

} // namespace theme
