#include "SoundMap.h"

#include "Theme.h"

namespace {
const juce::Identifier kMapX ("mapX"), kMapY ("mapY");
constexpr float kHitRadius = 8.0f;
} // namespace

SoundMap::SoundMap (PCASynthProcessor& p) : processor (p)
{
    for (auto* l : { &xLabel, &yLabel })
    {
        l->setFont (theme::font (12.0f));
        l->setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (l);
    }
    for (auto* c : { &xAxis, &yAxis })
    {
        c->onChange = [this] { axesChanged(); };
        addAndMakeVisible (c);
    }
}

void SoundMap::setModel (std::shared_ptr<const pcs::Model> m)
{
    model = std::move (m);
    soundZ.clear();
    xAxis.clear (juce::dontSendNotification);
    yAxis.clear (juce::dontSendNotification);
    if (model == nullptr)
        return;
    for (int i = 0; i < model->numSounds(); ++i)
        soundZ.push_back (model->soundZ (i));
    for (int j = 0; j < model->numComponents(); ++j)
    {
        const auto name = "PC" + juce::String (j + 1) + "  (" + juce::String (100.0 * model->varianceExplained (j), 1) + " %)";
        xAxis.addItem (name, j + 1);
        yAxis.addItem (name, j + 1);
    }
    const int k = model->numComponents();
    ax = juce::jlimit (0, juce::jmax (0, k - 1), static_cast<int> (processor.getUiValue (kMapX, 0)));
    ay = juce::jlimit (0, juce::jmax (0, k - 1), static_cast<int> (processor.getUiValue (kMapY, juce::jmin (1, k - 1))));
    xAxis.setSelectedId (ax + 1, juce::dontSendNotification);
    yAxis.setSelectedId (ay + 1, juce::dontSendNotification);
    axesChanged();
}

void SoundMap::axesChanged()
{
    ax = juce::jmax (0, xAxis.getSelectedId() - 1);
    ay = juce::jmax (0, yAxis.getSelectedId() - 1);
    processor.setUiValue (kMapX, ax);
    processor.setUiValue (kMapY, ay);
    // Symmetric range that holds every sound, at least ±3 SD.
    range = 3.0f;
    for (const auto& z : soundZ)
        range = juce::jmax (range, std::abs (z[static_cast<size_t> (ax)]) + 0.5f, std::abs (z[static_cast<size_t> (ay)]) + 0.5f);
    range = std::ceil (range);
    repaint();
}

void SoundMap::refresh()
{
    const auto p = processor.getPoint();
    if (p != point)
    {
        point = p;
        repaint();
    }
}

juce::Rectangle<float> SoundMap::plotArea() const
{
    auto r = getLocalBounds().toFloat().reduced (10.0f);
    r.removeFromTop (22.0f);      // title
    r.removeFromBottom (30.0f);   // axis selectors
    r.removeFromLeft (22.0f);     // tick labels
    r.removeFromBottom (14.0f);
    const float side = juce::jmin (r.getWidth(), r.getHeight());
    return r.withSizeKeepingCentre (side, side);
}

juce::Point<float> SoundMap::toScreen (float zx, float zy) const
{
    const auto a = plotArea();
    return { a.getX() + (zx + range) / (2.0f * range) * a.getWidth(), a.getBottom() - (zy + range) / (2.0f * range) * a.getHeight() };
}

juce::Point<float> SoundMap::toSpace (juce::Point<float> s) const
{
    const auto a = plotArea();
    return { (s.x - a.getX()) / a.getWidth() * 2.0f * range - range, (a.getBottom() - s.y) / a.getHeight() * 2.0f * range - range };
}

int SoundMap::soundAt (juce::Point<float> s) const
{
    int best = -1;
    float bestD = kHitRadius;
    for (size_t i = 0; i < soundZ.size(); ++i)
    {
        const float d = toScreen (soundZ[i][static_cast<size_t> (ax)], soundZ[i][static_cast<size_t> (ay)]).getDistanceFrom (s);
        if (d < bestD)
        {
            bestD = d;
            best = static_cast<int> (i);
        }
    }
    return best;
}

void SoundMap::resized()
{
    auto r = getLocalBounds().reduced (10).removeFromBottom (26);
    const int half = r.getWidth() / 2;
    auto left = r.removeFromLeft (half), right = r;
    xLabel.setBounds (left.removeFromLeft (20));
    xAxis.setBounds (left.reduced (4, 1));
    yLabel.setBounds (right.removeFromLeft (20));
    yAxis.setBounds (right.reduced (4, 1));
}

void SoundMap::paint (juce::Graphics& g)
{
    theme::drawPanel (g, getLocalBounds(), "Sound map");
    if (model == nullptr || model->numComponents() < 1)
        return;
    const auto a = plotArea();

    // Grid at every SD, the axes (mean) a little brighter.
    g.setFont (theme::font (10.0f));
    for (int v = static_cast<int> (-range); v <= static_cast<int> (range); ++v)
    {
        const auto p = toScreen (static_cast<float> (v), static_cast<float> (v));
        g.setColour (v == 0 ? theme::axis : theme::grid);
        g.drawVerticalLine (juce::roundToInt (p.x), a.getY(), a.getBottom());
        g.drawHorizontalLine (juce::roundToInt (p.y), a.getX(), a.getRight());
        if (v % 2 == 0 || range <= 4.0f)
        {
            g.setColour (theme::faint);
            g.drawText (juce::String (v), juce::Rectangle<float> (p.x - 12.0f, a.getBottom() + 2.0f, 24.0f, 12.0f), juce::Justification::centred);
            g.drawText (juce::String (v), juce::Rectangle<float> (a.getX() - 26.0f, p.y - 6.0f, 22.0f, 12.0f), juce::Justification::centredRight);
        }
    }

    // Training sounds.
    const bool labelAll = soundZ.size() <= 16;
    for (size_t i = 0; i < soundZ.size(); ++i)
    {
        const auto p = toScreen (soundZ[i][static_cast<size_t> (ax)], soundZ[i][static_cast<size_t> (ay)]);
        const bool hot = static_cast<int> (i) == hover;
        const float r = hot ? 6.0f : 4.5f;
        g.setColour (theme::panel);
        g.fillEllipse (p.x - r - 2.0f, p.y - r - 2.0f, 2.0f * r + 4.0f, 2.0f * r + 4.0f);
        g.setColour (hot ? theme::accentLight : theme::accent);
        g.fillEllipse (p.x - r, p.y - r, 2.0f * r, 2.0f * r);
        if (labelAll && ! hot)
        {
            g.setColour (theme::muted);
            g.setFont (theme::font (10.0f));
            g.drawText (model->names[i], juce::Rectangle<float> (p.x + 7.0f, p.y - 7.0f, 90.0f, 14.0f), juce::Justification::centredLeft);
        }
    }

    // The current point.
    const float px = juce::jlimit (-range, range, point[static_cast<size_t> (ax)]);
    const float py = juce::jlimit (-range, range, point[static_cast<size_t> (ay)]);
    const auto c = toScreen (px, py);
    g.setColour (theme::cursor.withAlpha (0.35f));
    g.drawVerticalLine (juce::roundToInt (c.x), a.getY(), a.getBottom());
    g.drawHorizontalLine (juce::roundToInt (c.y), a.getX(), a.getRight());
    g.setColour (theme::cursor);
    g.drawEllipse (c.x - 7.0f, c.y - 7.0f, 14.0f, 14.0f, 2.0f);
    g.fillEllipse (c.x - 2.5f, c.y - 2.5f, 5.0f, 5.0f);

    // Hover label, drawn last so it sits above everything.
    if (hover >= 0)
    {
        const auto& z = soundZ[static_cast<size_t> (hover)];
        const auto p = toScreen (z[static_cast<size_t> (ax)], z[static_cast<size_t> (ay)]);
        const auto text = juce::String (model->names[static_cast<size_t> (hover)]) + "  (" + juce::String (z[static_cast<size_t> (ax)], 2) + ", "
                        + juce::String (z[static_cast<size_t> (ay)], 2) + ")";
        g.setFont (theme::font (12.0f));
        const float w = static_cast<float> (juce::GlyphArrangement::getStringWidthInt (juce::Font (theme::font (12.0f)), text)) + 12.0f;
        auto box = juce::Rectangle<float> (p.x + 10.0f, p.y - 22.0f, w, 18.0f);
        if (box.getRight() > a.getRight())
            box.setX (p.x - 10.0f - w);
        g.setColour (theme::background.withAlpha (0.92f));
        g.fillRoundedRectangle (box, 3.0f);
        g.setColour (theme::text);
        g.drawText (text, box, juce::Justification::centred);
    }
}

void SoundMap::mouseMove (const juce::MouseEvent& e)
{
    const int h = soundAt (e.position);
    if (h != hover)
    {
        hover = h;
        setMouseCursor (h >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::CrosshairCursor);
        repaint();
    }
}

void SoundMap::mouseExit (const juce::MouseEvent&)
{
    hover = -1;
    repaint();
}

void SoundMap::moveTo (juce::Point<float> screen)
{
    const auto z = toSpace (screen);
    auto p = processor.getPoint();
    p[static_cast<size_t> (ax)] = juce::jlimit (-4.0f, 4.0f, z.x);
    p[static_cast<size_t> (ay)] = juce::jlimit (-4.0f, 4.0f, z.y);
    processor.setPoint (p);
    refresh();
}

void SoundMap::mouseDown (const juce::MouseEvent& e)
{
    if (model == nullptr || ! plotArea().expanded (kHitRadius).contains (e.position))
        return;
    if (const int s = soundAt (e.position); s >= 0)
    {
        processor.jumpToSound (s);
        refresh();
        return;
    }
    dragging = true;
    processor.beginPointGesture();
    moveTo (e.position);
}

void SoundMap::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging)
        moveTo (e.position);
}

void SoundMap::mouseUp (const juce::MouseEvent&)
{
    if (dragging)
        processor.endPointGesture();
    dragging = false;
}
