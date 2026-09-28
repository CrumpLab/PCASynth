#include "MorphPad.h"

#include "Theme.h"

namespace {
const juce::Identifier kCornerKeys[] = { "morphA", "morphB", "morphC", "morphD" };
const juce::Identifier kU ("morphU"), kV ("morphV");
} // namespace

MorphPad::MorphPad (PCASynthProcessor& p) : processor (p)
{
    for (auto& c : corners)
    {
        c.onChange = [this] { cornersChanged(); };
        addAndMakeVisible (c);
    }
    u = static_cast<float> (processor.getUiValue (kU, 0.5));
    v = static_cast<float> (processor.getUiValue (kV, 0.5));
}

void MorphPad::setModel (std::shared_ptr<const pcs::Model> m)
{
    model = std::move (m);
    for (auto& c : corners)
        c.clear (juce::dontSendNotification);
    if (model == nullptr || model->numSounds() == 0)
        return;
    const int n = model->numSounds();
    for (size_t k = 0; k < corners.size(); ++k)
    {
        for (int i = 0; i < n; ++i)
            corners[k].addItem (model->names[static_cast<size_t> (i)], i + 1);
        // Saved choice (by name), else sounds spread through the set.
        int index = model->soundIndex (processor.getUiValue (kCornerKeys[k], "").toString().toStdString());
        if (index < 0)
            index = static_cast<int> (k) * n / 4;
        corners[k].setSelectedId (index + 1, juce::dontSendNotification);
    }
    cornersChanged();
}

void MorphPad::cornersChanged()
{
    if (model == nullptr)
        return;
    for (size_t k = 0; k < corners.size(); ++k)
        processor.setUiValue (kCornerKeys[k], corners[k].getText());
    repaint();
}

PCASynthProcessor::Point MorphPad::blend (float uu, float vv) const
{
    PCASynthProcessor::Point p {};
    if (model == nullptr)
        return p;
    const float w[] = { (1 - uu) * (1 - vv), uu * (1 - vv), (1 - uu) * vv, uu * vv };
    for (size_t k = 0; k < corners.size(); ++k)
    {
        const int index = corners[k].getSelectedId() - 1;
        if (index < 0)
            continue;
        const auto z = model->soundZ (index);
        for (size_t j = 0; j < z.size() && j < p.size(); ++j)
            p[j] += w[k] * z[j];
    }
    return p;
}

juce::Rectangle<float> MorphPad::padArea() const
{
    auto r = getLocalBounds().toFloat().reduced (10.0f);
    r.removeFromTop (22.0f);
    r.removeFromTop (32.0f);
    r.removeFromBottom (32.0f);
    r.reduce (6.0f, 0.0f);
    const float side = juce::jmin (r.getWidth(), r.getHeight());
    return r.withSizeKeepingCentre (side, side);
}

void MorphPad::resized()
{
    const auto pad = padArea().toNearestInt();
    const int w = pad.getWidth() / 2 - 4;
    corners[0].setBounds (pad.getX(), pad.getY() - 30, w, 24);
    corners[1].setBounds (pad.getRight() - w, pad.getY() - 30, w, 24);
    corners[2].setBounds (pad.getX(), pad.getBottom() + 6, w, 24);
    corners[3].setBounds (pad.getRight() - w, pad.getBottom() + 6, w, 24);
}

void MorphPad::paint (juce::Graphics& g)
{
    theme::drawPanel (g, getLocalBounds(), "Morph");
    const auto a = padArea();
    g.setColour (theme::background);
    g.fillRoundedRectangle (a, 4.0f);
    g.setColour (theme::grid);
    for (int i = 1; i < 4; ++i)
    {
        g.drawVerticalLine (juce::roundToInt (a.getX() + a.getWidth() * i / 4.0f), a.getY(), a.getBottom());
        g.drawHorizontalLine (juce::roundToInt (a.getY() + a.getHeight() * i / 4.0f), a.getX(), a.getRight());
    }
    g.setColour (theme::axis);
    g.drawRoundedRectangle (a, 4.0f, 1.0f);
    for (auto corner : { a.getTopLeft(), a.getTopRight(), a.getBottomLeft(), a.getBottomRight() })
    {
        g.setColour (theme::accent);
        g.fillEllipse (corner.x - 5.0f, corner.y - 5.0f, 10.0f, 10.0f);
    }

    const juce::Point<float> puck (a.getX() + u * a.getWidth(), a.getY() + v * a.getHeight());
    g.setColour (theme::cursor);
    g.drawEllipse (puck.x - 8.0f, puck.y - 8.0f, 16.0f, 16.0f, 2.0f);
    g.fillEllipse (puck.x - 3.0f, puck.y - 3.0f, 6.0f, 6.0f);
}

void MorphPad::moveTo (juce::Point<float> s)
{
    const auto a = padArea();
    u = juce::jlimit (0.0f, 1.0f, (s.x - a.getX()) / a.getWidth());
    v = juce::jlimit (0.0f, 1.0f, (s.y - a.getY()) / a.getHeight());
    processor.setUiValue (kU, u);
    processor.setUiValue (kV, v);
    processor.setPoint (blend (u, v));
    repaint();
}

void MorphPad::mouseDown (const juce::MouseEvent& e)
{
    if (model == nullptr || ! padArea().expanded (8.0f).contains (e.position))
        return;
    dragging = true;
    processor.beginPointGesture();
    moveTo (e.position);
}

void MorphPad::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging)
        moveTo (e.position);
}

void MorphPad::mouseUp (const juce::MouseEvent&)
{
    if (dragging)
        processor.endPointGesture();
    dragging = false;
}
