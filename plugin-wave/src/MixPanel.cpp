#include "MixPanel.h"

#include "Theme.h"

namespace {
constexpr int kHeader = 44, kMinCellHeight = 15, kMaxCellHeight = 22, kMinCellWidth = 112;
}

MixPanel::MixPanel (PCAWaveProcessor& p) : processor (p) {}

void MixPanel::setModel (std::shared_ptr<const pcs::Space> space)
{
    model = std::dynamic_pointer_cast<const pcs::WaveModel> (space);
    weights.clear();
    refresh();
    repaint();
}

void MixPanel::refresh()
{
    if (model == nullptr)
        return;
    auto z = processor.getPoint();
    auto& params = processor.getParameters();
    const float ex = params.getRawParameterValue (pcsplugin::id::exaggerate)->load();
    const int used = juce::roundToInt (params.getRawParameterValue (pcsplugin::id::components)->load());
    for (int j = 0; j < pcs::kMaxComponents; ++j)
        z[static_cast<size_t> (j)] = j < used ? z[static_cast<size_t> (j)] * ex : 0.0f;
    if (processor.isAudioRunning())
        z = processor.getHeardPoint();
    auto w = model->mixWeights (z.data(), pcs::kMaxComponents);
    bool changed = w.size() != weights.size();
    for (size_t i = 0; i < w.size() && ! changed; ++i)
        changed = std::abs (w[i] - weights[i]) > 1e-4;
    if (changed)
    {
        weights = std::move (w);
        layout();
        repaint();
    }
}

void MixPanel::layout()
{
    const int n = static_cast<int> (weights.size());
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
        std::vector<int> order (static_cast<size_t> (n));
        for (int i = 0; i < n; ++i)
            order[static_cast<size_t> (i)] = i;
        std::partial_sort (order.begin(), order.begin() + capacity, order.end(), [this] (int a, int b) {
            return std::abs (weights[static_cast<size_t> (a)]) > std::abs (weights[static_cast<size_t> (b)]);
        });
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

void MixPanel::paint (juce::Graphics& g)
{
    theme::drawPanel (g, getLocalBounds(), "The point as a mix of the training sounds");
    if (model == nullptr || weights.empty())
        return;
    double biggest = 1e-9;
    for (double w : weights)
        biggest = std::max (biggest, std::abs (w));

    // The largest contributions, in words.
    std::vector<size_t> order (weights.size());
    for (size_t i = 0; i < order.size(); ++i)
        order[i] = i;
    std::sort (order.begin(), order.end(), [this] (size_t a, size_t b) { return std::abs (weights[a]) > std::abs (weights[b]); });
    juce::StringArray top;
    for (size_t k = 0; k < std::min<size_t> (5, order.size()); ++k)
    {
        const double w = weights[order[k]];
        if (std::abs (w) < 0.005)
            break;
        top.add ((w < 0 ? "-" : "") + juce::String (juce::roundToInt (std::abs (w) * 100.0)) + " % "
                 + juce::String (model->names[order[k]]));
    }
    g.setFont (theme::font (12.0f));
    g.setColour (theme::muted);
    juce::String summary = top.joinIntoString ("  +  ") + (order.size() > 5 ? "  ..." : "");
    if (shown.size() < weights.size())
        summary << "      (showing the " << static_cast<int> (shown.size()) << " largest of " << static_cast<int> (weights.size()) << ")";
    g.drawText (summary, 12, 22, getWidth() - 24, 18, juce::Justification::centredLeft);

    // Every sound: a bar from the centre of its cell (right: adds it; left: subtracts it).
    g.setFont (theme::font (cellH < 18 ? 10.0f : 11.0f));
    for (size_t slot = 0; slot < shown.size(); ++slot)
    {
        const auto i = static_cast<size_t> (shown[slot]);
        auto r = cellBounds (static_cast<int> (slot));
        auto nameArea = r.removeFromLeft (r.getWidth() * 55 / 100);
        const double w = weights[i];
        const bool strong = std::abs (w) > 0.05 * biggest;
        g.setColour (strong ? theme::text : theme::faint);
        g.drawText (model->names[i], nameArea, juce::Justification::centredLeft, true);
        auto bar = r.reduced (0, juce::jmax (3, r.getHeight() / 4)).toFloat();
        g.setColour (theme::background);
        g.fillRect (bar);
        const float centre = bar.getCentreX(), len = bar.getWidth() * 0.5f * static_cast<float> (w / biggest);
        g.setColour (w >= 0 ? theme::accent : theme::cursor);
        g.fillRect (len >= 0 ? juce::Rectangle<float> (centre, bar.getY(), len, bar.getHeight())
                             : juce::Rectangle<float> (centre + len, bar.getY(), -len, bar.getHeight()));
        g.setColour (theme::axis);
        g.drawVerticalLine (static_cast<int> (centre), bar.getY(), bar.getBottom());
    }
}

void MixPanel::mouseUp (const juce::MouseEvent& e)
{
    for (size_t slot = 0; slot < shown.size(); ++slot)
        if (cellBounds (static_cast<int> (slot)).contains (e.getPosition()))
        {
            processor.jumpToSound (shown[slot]);
            return;
        }
}
