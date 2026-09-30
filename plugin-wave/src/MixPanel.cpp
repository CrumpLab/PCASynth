#include "MixPanel.h"

#include "Theme.h"

namespace {
constexpr int kHeader = 62, kMinCellHeight = 15, kMaxCellHeight = 22, kMinCellWidth = 112;
constexpr double kPixelsPerUnit = 160.0; // drag this far for 100 % (ten times as far with Shift)
constexpr double kMaxAmount = 3.0;
const juce::Identifier kMixKey ("mixAmounts");

juce::String percent (double a)
{
    return (a < 0 ? "-" : "+") + juce::String (juce::roundToInt (std::abs (a) * 100.0)) + " %";
}
} // namespace

MixPanel::MixPanel (PCAWaveProcessor& p) : processor (p)
{
    clearButton.setTooltip ("Back to the centre of the space: every amount to 0");
    clearButton.onClick = [this] { clearMix(); };
    addAndMakeVisible (clearButton);
}

void MixPanel::setModel (std::shared_ptr<const pcs::Space> space)
{
    model = std::dynamic_pointer_cast<const pcs::WaveModel> (space);
    amounts.clear();
    heard.clear();
    weights.clear();
    loadAmounts();
    refresh();
    repaint();
}

void MixPanel::loadAmounts()
{
    if (model == nullptr)
        return;
    amounts.assign (static_cast<size_t> (model->numSounds()), 0.0);
    // "index:amount,..." (only the sounds in use)
    for (const auto& item : juce::StringArray::fromTokens (processor.getUiValue (kMixKey, "").toString(), ",", ""))
    {
        const int i = item.upToFirstOccurrenceOf (":", false, false).getIntValue();
        if (i >= 0 && i < model->numSounds())
            amounts[static_cast<size_t> (i)] = item.fromFirstOccurrenceOf (":", false, false).getDoubleValue();
    }
    // Kept only if they still make the point (same space, point not moved since).
    syncFromPoint();
}

void MixPanel::storeAmounts()
{
    juce::StringArray items;
    for (size_t i = 0; i < amounts.size(); ++i)
        if (std::abs (amounts[i]) > 1e-9)
            items.add (juce::String (static_cast<int> (i)) + ":" + juce::String (amounts[i], 5));
    processor.setUiValue (kMixKey, items.joinIntoString (","));
}

void MixPanel::syncFromPoint()
{
    if (model == nullptr || dragging >= 0)
        return;
    const auto point = processor.getPoint();
    bool same = static_cast<int> (amounts.size()) == model->numSounds();
    if (same)
    {
        const auto z = model->pointFromAmounts (amounts.data(), static_cast<int> (amounts.size()));
        for (size_t j = 0; j < point.size() && same; ++j)
            same = std::abs (point[j] - (j < z.size() ? z[j] : 0.0f)) < 1e-3f;
    }
    if (! same)
    {
        amounts = model->mixAmounts (point.data(), pcs::kMaxComponents);
        storeAmounts();
        layout();
        repaint();
    }
}

void MixPanel::applyAmounts()
{
    if (model == nullptr)
        return;
    const auto z = model->pointFromAmounts (amounts.data(), static_cast<int> (amounts.size()));
    SpaceProcessor::Point p {};
    for (size_t j = 0; j < z.size() && j < p.size(); ++j)
        p[j] = z[j];
    processor.setPoint (p);
}

void MixPanel::setAmount (int sound, double amount)
{
    if (model == nullptr || sound < 0 || sound >= static_cast<int> (amounts.size()))
        return;
    amounts[static_cast<size_t> (sound)] = juce::jlimit (-kMaxAmount, kMaxAmount, amount);
    applyAmounts();
    refresh();
    repaint();
}

void MixPanel::clearMix()
{
    if (model == nullptr)
        return;
    std::fill (amounts.begin(), amounts.end(), 0.0);
    processor.resetToMean();
    storeAmounts();
    refresh();
    repaint();
}

void MixPanel::refresh()
{
    if (model == nullptr)
        return;
    syncFromPoint();

    // What you hear: the point with Exaggerate and Components Used applied,
    // or, while audio runs, the modulated point (walk, LFOs, MPE). It is shown
    // as your amounts plus the amounts of its offset from your point, so the
    // two agree whenever nothing moves it.
    const auto point = processor.getPoint();
    auto z = point;
    auto& params = processor.getParameters();
    const float ex = params.getRawParameterValue (pcsplugin::id::exaggerate)->load();
    const int used = juce::roundToInt (params.getRawParameterValue (pcsplugin::id::components)->load());
    for (int j = 0; j < pcs::kMaxComponents; ++j)
        z[static_cast<size_t> (j)] = j < used ? z[static_cast<size_t> (j)] * ex : 0.0f;
    if (processor.isAudioRunning())
        z = processor.getHeardPoint();
    for (size_t j = 0; j < z.size(); ++j)
        z[j] -= point[j];
    auto h = model->mixAmounts (z.data(), pcs::kMaxComponents);
    for (size_t i = 0; i < h.size() && i < amounts.size(); ++i)
        h[i] += amounts[i];
    bool changed = h.size() != heard.size();
    for (size_t i = 0; i < h.size() && ! changed; ++i)
        changed = std::abs (h[i] - heard[i]) > 1e-4;
    if (changed)
    {
        heard = std::move (h);
        double sum = 0.0;
        for (double x : heard)
            sum += x;
        weights = heard;
        for (auto& w : weights)
            w += (1.0 - sum) / static_cast<double> (weights.size());
        layout();
        repaint();
    }
}

void MixPanel::resized()
{
    clearButton.setBounds (getWidth() - 110, 22, 100, 20);
    layout();
}

void MixPanel::layout()
{
    const int n = static_cast<int> (amounts.size());
    shown.clear();
    if (n == 0)
        return;
    const int width = getWidth() - 20, height = getHeight() - kHeader - 6;
    rows = juce::jmax (1, height / kMinCellHeight);
    cols = juce::jmax (1, width / kMinCellWidth);
    const int capacity = rows * cols;
    if (n <= capacity)
    {
        for (int i = 0; i < n; ++i)
            shown.push_back (i);
        cols = (n + rows - 1) / rows;
        rows = (n + cols - 1) / cols;
    }
    else
    {
        auto use = [this] (int i) {
            const auto k = static_cast<size_t> (i);
            return std::max (std::abs (amounts[k]), k < heard.size() ? std::abs (heard[k]) : 0.0);
        };
        std::vector<int> order (static_cast<size_t> (n));
        for (int i = 0; i < n; ++i)
            order[static_cast<size_t> (i)] = i;
        std::stable_sort (order.begin(), order.end(), [&] (int a, int b) { return use (a) > use (b); });
        shown.assign (order.begin(), order.begin() + capacity);
        std::sort (shown.begin(), shown.end());
    }
    cellW = width / cols;
    cellH = juce::jmin (kMaxCellHeight, height / rows);
}

juce::Rectangle<int> MixPanel::cellBounds (int slot) const
{
    const int col = slot / rows, row = slot % rows;
    return { 10 + col * cellW, kHeader + row * cellH, cellW - 8, cellH - 2 };
}

juce::Rectangle<int> MixPanel::barBounds (int slot) const
{
    auto r = cellBounds (slot);
    return r.removeFromRight (r.getWidth() * 48 / 100);
}

int MixPanel::slotAt (juce::Point<int> p, bool& onBar) const
{
    for (size_t slot = 0; slot < shown.size(); ++slot)
        if (cellBounds (static_cast<int> (slot)).contains (p))
        {
            onBar = barBounds (static_cast<int> (slot)).contains (p);
            return static_cast<int> (slot);
        }
    return -1;
}

void MixPanel::paint (juce::Graphics& g)
{
    theme::drawPanel (g, getLocalBounds(), "Mix: the point as amounts of the training sounds");
    if (model == nullptr || amounts.empty())
        return;

    // Your mix, in words.
    std::vector<size_t> order (amounts.size());
    for (size_t i = 0; i < order.size(); ++i)
        order[i] = i;
    std::sort (order.begin(), order.end(), [this] (size_t a, size_t b) { return std::abs (amounts[a]) > std::abs (amounts[b]); });
    juce::StringArray mine;
    for (size_t k = 0; k < order.size() && mine.size() < 6; ++k)
        if (std::abs (amounts[order[k]]) >= 0.005)
            mine.add (percent (amounts[order[k]]) + " " + juce::String (model->names[order[k]]));
    const bool more = order.size() > 6 && std::abs (amounts[order[6]]) >= 0.005;
    g.setFont (theme::font (12.0f));
    g.setColour (theme::text);
    const int textW = getWidth() - 24 - 120;
    g.drawText (mine.isEmpty() ? juce::String ("The centre of the space. Drag a sound's bar to mix it in; click its name to go to it.")
                               : "Centre " + mine.joinIntoString ("  ") + (more ? "  ..." : ""),
                12, 22, textW, 18, juce::Justification::centredLeft, true);

    // What you hear, as weights of the training sounds (summing to 100 %).
    std::vector<size_t> byWeight (weights.size());
    for (size_t i = 0; i < byWeight.size(); ++i)
        byWeight[i] = i;
    std::sort (byWeight.begin(), byWeight.end(), [this] (size_t a, size_t b) { return std::abs (weights[a]) > std::abs (weights[b]); });
    juce::StringArray top;
    for (size_t k = 0; k < std::min<size_t> (5, byWeight.size()); ++k)
    {
        const double w = weights[byWeight[k]];
        if (std::abs (w) < 0.005)
            break;
        top.add ((w < 0 ? "-" : "") + juce::String (juce::roundToInt (std::abs (w) * 100.0)) + " % " + juce::String (model->names[byWeight[k]]));
    }
    juce::String hearing = "Hearing: " + top.joinIntoString ("  +  ") + (byWeight.size() > 5 ? "  ..." : "");
    if (shown.size() < amounts.size())
        hearing << "      (showing the " << static_cast<int> (shown.size()) << " most used of " << static_cast<int> (amounts.size()) << ")";
    g.setFont (theme::font (11.0f));
    g.setColour (theme::muted);
    g.drawText (hearing, 12, 40, getWidth() - 24, 16, juce::Justification::centredLeft, true);

    // Every sound: its name, then a bar from the centre (right: mixed in; left:
    // subtracted), full width = 100 %, and a tick at its amount in what you hear.
    g.setFont (theme::font (cellH < 18 ? 10.0f : 11.0f));
    for (size_t slot = 0; slot < shown.size(); ++slot)
    {
        const auto i = static_cast<size_t> (shown[slot]);
        const auto cell = cellBounds (static_cast<int> (slot));
        const auto barArea = barBounds (static_cast<int> (slot));
        const double a = amounts[i], h = i < heard.size() ? heard[i] : a;
        const bool used = std::abs (a) >= 0.005 || std::abs (h) >= 0.05;
        g.setColour (static_cast<int> (i) == dragging ? theme::cursor : used ? theme::text : theme::faint);
        g.drawText (model->names[i], cell.withRight (barArea.getX() - 4), juce::Justification::centredLeft, true);

        auto bar = barArea.reduced (0, juce::jmax (3, barArea.getHeight() / 4)).toFloat();
        g.setColour (theme::background);
        g.fillRect (bar);
        const float centre = bar.getCentreX(), half = bar.getWidth() * 0.5f;
        const float len = half * static_cast<float> (juce::jlimit (-1.0, 1.0, a));
        g.setColour (a >= 0 ? theme::accent : theme::cursor);
        g.fillRect (len >= 0 ? juce::Rectangle<float> (centre, bar.getY(), len, bar.getHeight())
                             : juce::Rectangle<float> (centre + len, bar.getY(), -len, bar.getHeight()));
        if (std::abs (a) > 1.0) // beyond 100 %: a bright cap at the end
        {
            g.setColour (theme::text);
            g.fillRect (juce::Rectangle<float> (a > 0 ? bar.getRight() - 2.0f : bar.getX(), bar.getY(), 2.0f, bar.getHeight()));
        }
        g.setColour (theme::axis);
        g.drawVerticalLine (static_cast<int> (centre), bar.getY(), bar.getBottom());
        if (std::abs (h - a) > 0.01)
        {
            const float x = centre + half * static_cast<float> (juce::jlimit (-1.0, 1.0, h));
            g.setColour (theme::text.withAlpha (0.8f));
            g.fillRect (juce::Rectangle<float> (x - 1.0f, bar.getY() - 2.0f, 2.0f, bar.getHeight() + 4.0f));
        }
        if (static_cast<int> (i) == dragging)
        {
            g.setColour (theme::cursor);
            g.drawRect (bar.expanded (1.0f), 1.0f);
        }
    }
}

void MixPanel::mouseMove (const juce::MouseEvent& e)
{
    bool onBar = false;
    const int slot = slotAt (e.getPosition(), onBar);
    setMouseCursor (slot >= 0 && onBar ? juce::MouseCursor::LeftRightResizeCursor
                    : slot >= 0        ? juce::MouseCursor::PointingHandCursor
                                       : juce::MouseCursor::NormalCursor);
}

void MixPanel::mouseDown (const juce::MouseEvent& e)
{
    bool onBar = false;
    const int slot = slotAt (e.getPosition(), onBar);
    pressedName = -1;
    if (slot < 0 || model == nullptr)
        return;
    const int sound = shown[static_cast<size_t> (slot)];
    if (onBar)
    {
        dragging = sound;
        dragStart = amounts[static_cast<size_t> (sound)];
        processor.beginPointGesture();
        repaint();
    }
    else
        pressedName = sound;
}

void MixPanel::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0)
        return;
    const double scale = e.mods.isShiftDown() ? kPixelsPerUnit * 10.0 : kPixelsPerUnit;
    setAmount (dragging, dragStart + e.getDistanceFromDragStartX() / scale);
}

void MixPanel::mouseUp (const juce::MouseEvent& e)
{
    if (dragging >= 0)
    {
        processor.endPointGesture();
        dragging = -1;
        storeAmounts();
        syncFromPoint(); // a PC past ±4 was clamped: read back what the point became
        repaint();
        return;
    }
    if (pressedName >= 0 && ! e.mouseWasDraggedSinceMouseDown())
    {
        // Go to the sound: that sound alone.
        std::fill (amounts.begin(), amounts.end(), 0.0);
        amounts[static_cast<size_t> (pressedName)] = 1.0;
        processor.jumpToSound (pressedName);
        storeAmounts();
        refresh();
        repaint();
    }
    pressedName = -1;
}

void MixPanel::mouseDoubleClick (const juce::MouseEvent& e)
{
    bool onBar = false;
    const int slot = slotAt (e.getPosition(), onBar);
    if (slot < 0 || ! onBar)
        return;
    processor.beginPointGesture();
    setAmount (shown[static_cast<size_t> (slot)], 0.0);
    processor.endPointGesture();
    storeAmounts();
}
