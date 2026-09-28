#include "PluginEditor.h"

namespace {
const juce::Colour kBackground { 0xff1a1a19 }, kBar { 0xff262624 }, kText { 0xffffffff }, kMuted { 0xffc3c2b7 },
    kAccent { 0xff3987e5 };
} // namespace

PCASynthEditor::PCASynthEditor (PCASynthProcessor& p)
    : AudioProcessorEditor (p), processor (p), generic (p)
{
    title.setText ("PCASynth", juce::dontSendNotification);
    title.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, kText);
    for (auto* l : { &modelInfo, &jumpLabel, &status })
    {
        l->setColour (juce::Label::textColourId, kMuted);
        l->setFont (juce::FontOptions (13.0f));
    }
    for (auto* c : std::initializer_list<juce::Component*> { &title, &modelInfo, &jumpLabel, &status, &loadButton,
                                                            &factoryButton, &meanButton, &soundBox, &generic })
        addAndMakeVisible (c);

    loadButton.onClick = [this] { chooseModelFile(); };
    factoryButton.onClick = [this] {
        processor.loadFactoryModel();
        showMessage ("Loaded the factory space.");
    };
    meanButton.onClick = [this] {
        processor.resetToMean();
        soundBox.setSelectedId (0, juce::dontSendNotification);
    };
    meanButton.setTooltip ("Every component at 0: the average of the training sounds");
    soundBox.setTextWhenNothingSelected ("Choose a training sound");
    soundBox.onChange = [this] {
        if (const int id = soundBox.getSelectedId(); id > 0)
            processor.jumpToSound (id - 1);
    };

    processor.addChangeListener (this);
    refreshModel();
    setResizable (true, true);
    setResizeLimits (620, 480, 1600, 1400);
    setSize (760, 700);
    startTimerHz (10);
}

PCASynthEditor::~PCASynthEditor() { processor.removeChangeListener (this); }

void PCASynthEditor::refreshModel()
{
    const auto m = processor.getModel();
    soundBox.clear (juce::dontSendNotification);
    if (m == nullptr)
    {
        modelInfo.setText ("No model", juce::dontSendNotification);
        return;
    }
    for (int i = 0; i < m->numSounds(); ++i)
        soundBox.addItem (m->names[static_cast<size_t> (i)], i + 1);

    double explained = 0.0;
    for (int j = 0; j < std::min (pcsplugin::kNumPcParams, m->numComponents()); ++j)
        explained += m->varianceExplained (j);
    const auto name = juce::String (m->title.empty() ? "Untitled" : m->title) + (processor.isFactoryModel() ? " (factory)" : "");
    modelInfo.setText (name + "  |  " + juce::String (m->numSounds()) + " sounds, " + juce::String (m->numComponents())
                           + " components, " + juce::String (m->numHarmonics) + " harmonics, "
                           + juce::String (m->durationSeconds(), 1) + " s  |  PC1-16 explain "
                           + juce::String (juce::roundToInt (100.0 * explained)) + " %",
                       juce::dontSendNotification);
}

void PCASynthEditor::timerCallback()
{
    // Status messages fade back to the voice count after a few seconds.
    if (status.getProperties()["until"].toString().getLargeIntValue() < juce::Time::currentTimeMillis())
        status.setText (juce::String (processor.getActiveVoices()) + " voices", juce::dontSendNotification);
}

void PCASynthEditor::showMessage (const juce::String& text)
{
    status.setText (text, juce::dontSendNotification);
    status.getProperties().set ("until", juce::String (juce::Time::currentTimeMillis() + 4000));
}

void PCASynthEditor::chooseModelFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Load a PCASynth model", juce::File(), "*.pcsm");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc) {
                              const auto file = fc.getResult();
                              if (file == juce::File())
                                  return;
                              const auto error = processor.loadModelFile (file);
                              showMessage (error.isEmpty() ? "Loaded " + file.getFileName() : error);
                          });
}

bool PCASynthEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (PCASynthProcessor::isModelFile (f))
            return (dragging = true);
    return false;
}

void PCASynthEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dragging = false;
    for (const auto& f : files)
        if (PCASynthProcessor::isModelFile (f))
        {
            const auto error = processor.loadModelFile (juce::File (f));
            showMessage (error.isEmpty() ? "Loaded " + juce::File (f).getFileName() : error);
            break;
        }
    repaint();
}

void PCASynthEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBackground);
    g.setColour (kBar);
    g.fillRect (getLocalBounds().removeFromTop (92));
    if (dragging)
    {
        g.setColour (kAccent);
        g.drawRect (getLocalBounds(), 3);
    }
}

void PCASynthEditor::resized()
{
    auto r = getLocalBounds();
    auto bar = r.removeFromTop (92).reduced (12, 8);
    auto row1 = bar.removeFromTop (28);
    title.setBounds (row1.removeFromLeft (120));
    status.setBounds (row1.removeFromRight (220));
    status.setJustificationType (juce::Justification::centredRight);
    loadButton.setBounds (row1.removeFromLeft (120).reduced (2));
    factoryButton.setBounds (row1.removeFromLeft (120).reduced (2));
    modelInfo.setBounds (bar.removeFromTop (22));
    auto row3 = bar.removeFromTop (28);
    jumpLabel.setBounds (row3.removeFromLeft (60));
    meanButton.setBounds (row3.removeFromRight (90).reduced (2));
    soundBox.setBounds (row3.reduced (2));
    generic.setBounds (r);
}
