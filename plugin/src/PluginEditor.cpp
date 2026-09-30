#include "PluginEditor.h"

namespace {
constexpr int kBarHeight = 76;
constexpr int kDefaultWidth = 1180, kDefaultHeight = 840;
} // namespace

PCASynthEditor::PCASynthEditor (PCASynthProcessor& p)
    : AudioProcessorEditor (p), processor (p), soundMap (p), morphPad (p), envelope (p), strip (p), controls (p), trainPanel (p)
{
    setLookAndFeel (&lookAndFeel);
    title.setText ("PCASynth", juce::dontSendNotification);
    title.setFont (theme::font (20.0f, true));
    title.setColour (juce::Label::textColourId, theme::text);
    for (auto* l : { &modelInfo, &jumpLabel, &status })
        l->setFont (theme::font (13.0f));
    status.setJustificationType (juce::Justification::centredRight);
    for (auto* c : std::initializer_list<juce::Component*> { &title, &modelInfo, &jumpLabel, &status, &loadButton, &factoryButton,
                                                            &exportButton, &meanButton, &soundBox, &soundMap, &morphPad,
                                                            &envelope, &strip, &controls, &trainToggle, &saveButton })
        addAndMakeVisible (c);
    addChildComponent (trainPanel);
    trainPanel.onClose = [this] { showTraining (false); };
    trainToggle.setClickingTogglesState (true);
    trainToggle.setTooltip ("Build a new space from your own audio files");
    trainToggle.onClick = [this] { showTraining (trainToggle.getToggleState()); };
    saveButton.setTooltip ("Save the current space as a .pcsm file");
    saveButton.onClick = [this] { chooseSaveFile(); };
    showTraining (static_cast<bool> (processor.getUiValue ("showTraining", false)));

    loadButton.setTooltip ("Load a .pcsm model (or drop one on the window)");
    factoryButton.setTooltip ("The built-in space: 60 synthetic instrument notes");
    exportButton.setTooltip ("Render one note at the current point and settings to a WAV file");
    meanButton.setTooltip ("Every component at 0: the average of the training sounds");
    loadButton.onClick = [this] { chooseModelFile(); };
    exportButton.onClick = [this] { chooseExportFile(); };
    factoryButton.onClick = [this] {
        processor.loadFactoryModel();
        showMessage ("Loaded the factory space.");
    };
    meanButton.onClick = [this] {
        processor.resetToMean();
        soundBox.setSelectedId (0, juce::dontSendNotification);
    };
    soundBox.setTextWhenNothingSelected ("Choose a training sound");
    soundBox.onChange = [this] {
        if (const int id = soundBox.getSelectedId(); id > 0)
            processor.jumpToSound (id - 1);
    };

    processor.addChangeListener (this);
    refreshModel();
    setResizable (true, true);
    setResizeLimits (1000, 760, 2000, 1500);
    setSize (kDefaultWidth, kDefaultHeight);
    startTimerHz (20);
}

PCASynthEditor::~PCASynthEditor()
{
    processor.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void PCASynthEditor::refreshModel()
{
    const auto m = processor.getModel();
    soundBox.clear (juce::dontSendNotification);
    soundMap.setModel (m);
    morphPad.setModel (m);
    envelope.setModel (m);
    strip.setModel (m);
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
                           + juce::String (juce::roundToInt (100.0 * explained)) + " % of the variance",
                       juce::dontSendNotification);
}

void PCASynthEditor::showTraining (bool show)
{
    trainToggle.setToggleState (show, juce::dontSendNotification);
    trainPanel.setVisible (show);
    for (auto* c : std::initializer_list<juce::Component*> { &soundMap, &morphPad, &envelope })
        c->setVisible (! show);
    processor.setUiValue ("showTraining", show);
}

void PCASynthEditor::chooseSaveFile()
{
    const auto m = processor.getModel();
    if (m == nullptr)
        return;
    const auto stem = juce::File::createLegalFileName (m->title.empty() ? "PCASynth space" : m->title);
    chooser = std::make_unique<juce::FileChooser> ("Save the sound space",
                                                   juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (stem + ".pcsm"),
                                                   "*.pcsm");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc) {
                              auto file = fc.getResult();
                              if (file == juce::File())
                                  return;
                              file = file.withFileExtension ("pcsm");
                              const auto error = processor.saveModelFile (file);
                              showMessage (error.isEmpty() ? "Saved " + file.getFileName() : error);
                          });
}

void PCASynthEditor::refresh()
{
    trainPanel.refresh();
    soundMap.refresh();
    envelope.refresh();
    strip.refresh();
    if (juce::Time::currentTimeMillis() > messageUntil)
    {
        const int v = processor.getActiveVoices();
        status.setText (juce::String (v) + (v == 1 ? " voice" : " voices"), juce::dontSendNotification);
    }
}

void PCASynthEditor::showMessage (const juce::String& text)
{
    status.setText (text, juce::dontSendNotification);
    messageUntil = juce::Time::currentTimeMillis() + 5000;
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

void PCASynthEditor::chooseExportFile()
{
    const auto m = processor.getModel();
    if (m == nullptr)
        return;
    const int note = processor.getLastNote();
    const auto noteName = juce::MidiMessage::getMidiNoteName (note, true, true, 4);
    const auto stem = juce::File::createLegalFileName (juce::String (m->title.empty() ? "PCASynth" : m->title) + " " + noteName);
    chooser = std::make_unique<juce::FileChooser> ("Export the current point as a WAV",
                                                   juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (stem + ".wav"),
                                                   "*.wav");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, note, noteName, m] (const juce::FileChooser& fc) {
                              auto file = fc.getResult();
                              if (file == juce::File())
                                  return;
                              file = file.withFileExtension ("wav");
                              const double release = processor.getParameters().getRawParameterValue (pcsplugin::id::release)->load();
                              const double seconds = juce::jlimit (0.5, 30.0, m->durationSeconds() + release);
                              showMessage ("Rendering " + noteName + "...");
                              juce::Component::SafePointer<PCASynthEditor> self (this);
                              processor.exportWav (file, note, seconds, [self, file] (juce::String error) {
                                  if (self != nullptr)
                                      self->showMessage (error.isEmpty() ? "Wrote " + file.getFileName() : error);
                              });
                          });
}

bool PCASynthEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (PCASynthProcessor::isModelFile (f) || juce::File (f).isDirectory() || Trainer::isAudioFile (juce::File (f)))
        {
            dragging = true;
            repaint();
            return true;
        }
    return false;
}

void PCASynthEditor::fileDragExit (const juce::StringArray&)
{
    dragging = false;
    repaint();
}

void PCASynthEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dragging = false;
    repaint();
    // A model loads; audio files and folders go to the training set.
    for (const auto& f : files)
        if (PCASynthProcessor::isModelFile (f))
        {
            const auto error = processor.loadModelFile (juce::File (f));
            showMessage (error.isEmpty() ? "Loaded " + juce::File (f).getFileName() : error);
            return;
        }
    if (processor.getTrainer().isRunning())
    {
        showMessage ("Training is running; add sounds when it finishes.");
        return;
    }
    const int added = processor.getTrainer().addFiles (files);
    showTraining (true);
    showMessage ("Added " + juce::String (added) + " sounds to the training set.");
}

void PCASynthEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
    g.setColour (theme::bar);
    g.fillRect (getLocalBounds().removeFromTop (kBarHeight));
}

void PCASynthEditor::paintOverChildren (juce::Graphics& g)
{
    if (dragging)
    {
        g.setColour (theme::accent);
        g.drawRect (getLocalBounds(), 3);
    }
}

void PCASynthEditor::resized()
{
    auto r = getLocalBounds();
    auto bar = r.removeFromTop (kBarHeight).reduced (12, 8);
    auto row1 = bar.removeFromTop (30);
    title.setBounds (row1.removeFromLeft (120));
    loadButton.setBounds (row1.removeFromLeft (112).reduced (2));
    saveButton.setBounds (row1.removeFromLeft (112).reduced (2));
    factoryButton.setBounds (row1.removeFromLeft (112).reduced (2));
    trainToggle.setBounds (row1.removeFromLeft (84).reduced (2));
    exportButton.setBounds (row1.removeFromLeft (112).reduced (2));
    row1.removeFromLeft (16);
    jumpLabel.setBounds (row1.removeFromLeft (56));
    soundBox.setBounds (row1.removeFromLeft (200).reduced (2));
    meanButton.setBounds (row1.removeFromLeft (80).reduced (2));
    status.setBounds (row1);
    bar.removeFromTop (6);
    modelInfo.setBounds (bar.removeFromTop (22));

    r = r.reduced (8);
    const int gap = 8;
    const float scale = r.getHeight() / static_cast<float> (kDefaultHeight - kBarHeight - 16);
    auto top = r.removeFromTop (juce::roundToInt (400 * scale));
    r.removeFromTop (gap);
    auto controlsArea = r.removeFromBottom (juce::roundToInt (132 * scale));
    r.removeFromBottom (gap);
    strip.setBounds (r);
    controls.setBounds (controlsArea);

    trainPanel.setBounds (top);
    const int mapW = juce::jmin (top.getHeight() + 30, top.getWidth() * 36 / 100);
    soundMap.setBounds (top.removeFromLeft (mapW));
    top.removeFromLeft (gap);
    morphPad.setBounds (top.removeFromLeft (juce::jmin (top.getHeight() - 40, top.getWidth() * 42 / 100)));
    top.removeFromLeft (gap);
    envelope.setBounds (top);
}
