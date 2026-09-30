#include "TrainPanel.h"

#include "Theme.h"

namespace {
constexpr int kAutoId = 1;
int noteId (int midi) { return midi + 2; }
} // namespace

TrainPanel::TrainPanel (PCASynthProcessor& p) : processor (p), trainer (p.getTrainer())
{
    list.setMultipleSelectionEnabled (true);
    list.setRowHeight (22);
    list.setColour (juce::ListBox::backgroundColourId, theme::background);
    list.setOutlineThickness (0);
    addAndMakeVisible (list);

    hint.setText ("Add audio files or folders (or drop them on the window). Each sound should be one note; "
                  "with Note = Auto every sound is analysed at its own pitch.",
                  juce::dontSendNotification);
    hint.setFont (theme::font (12.0f));
    hint.setColour (juce::Label::textColourId, theme::faint);
    status.setFont (theme::font (13.0f));
    status.setColour (juce::Label::textColourId, theme::text);

    for (auto* c : std::initializer_list<juce::Component*> { &addFiles, &addFolder, &removeButton, &clearButton, &trainButton,
                                                            &closeButton, &hint, &status, &title, &note, &duration, &harmonics,
                                                            &floorDb, &components, &normalize, &trim, &progress, &noiseBands,
                                                            &partials, &representation, &pitchTracking })
        addAndMakeVisible (c);

    addFiles.onClick = [this] { chooseFiles (false); };
    addFolder.onClick = [this] { chooseFiles (true); };
    removeButton.onClick = [this] { removeSelected(); };
    clearButton.onClick = [this] { trainer.clear(); };
    closeButton.onClick = [this] { if (onClose) onClose(); };
    trainButton.onClick = [this] {
        if (trainer.isRunning())
            trainer.cancel();
        else
        {
            settingsFromControls();
            if (! trainer.start())
                status.setText ("Add at least two sounds first.", juce::dontSendNotification);
        }
    };

    note.addItem ("Auto (detect each)", kAutoId);
    for (int m = 24; m <= 96; ++m)
        note.addItem (juce::MidiMessage::getMidiNoteName (m, true, true, 4) + "  (" + juce::String (m) + ")", noteId (m));

    auto setup = [] (juce::Slider& s, double lo, double hi, double step, const juce::String& suffix) {
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 20);
        s.setRange (lo, hi, step);
        s.setTextValueSuffix (suffix);
    };
    setup (duration, 0.5, 10.0, 0.1, " s");
    setup (harmonics, 8, pcs::kMaxModelHarmonics, 1, "");
    setup (floorDb, -120, -40, 1, " dB");
    setup (components, 1, pcs::kMaxComponents, 1, "");
    setup (noiseBands, 0, 32, 1, "");
    representation.addItemList ({ "Decibels", "Shape + loudness", "Linear" }, 1);
    pitchTracking.addItemList ({ "Off", "Auto (3+ semitones)", "On" }, 1);
    title.setColour (juce::TextEditor::backgroundColourId, theme::background);
    title.setColour (juce::TextEditor::outlineColourId, theme::axis);

    for (const auto* text : { "Name", "Note", "Duration", "Harmonics", "Floor", "Components", "Noise bands", "Levels as", "Pitch tracking" })
    {
        auto l = std::make_unique<juce::Label> (juce::String(), text);
        l->setFont (theme::font (12.0f));
        l->setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (*l);
        labels.push_back (std::move (l));
    }

    controlsFromSettings();
    for (auto* s : { &duration, &harmonics, &floorDb, &components, &noiseBands })
        s->onValueChange = [this] { settingsFromControls(); };
    note.onChange = representation.onChange = pitchTracking.onChange = [this] { settingsFromControls(); };
    normalize.onClick = trim.onClick = partials.onClick = [this] { settingsFromControls(); };
    title.onTextChange = [this] { settingsFromControls(); };

    duration.setTooltip ("Seconds analysed from each sound's onset (longer sounds are cut, shorter ones fade to the floor)");
    harmonics.setTooltip ("Harmonics tracked per sound");
    floorDb.setTooltip ("Quietest level kept. Higher floors weigh the audible harmonics' shape more than which harmonics exist");
    components.setTooltip ("Principal components kept (at most one fewer than the number of sounds)");
    normalize.setTooltip ("Scale every sound so its loudest moment is at 0 dB");
    trim.setTooltip ("Line sounds up on their first sound (skip leading silence)");
    noiseBands.setTooltip ("Bands of residual noise (breath, bow, hammer) kept alongside the harmonics. 0 = harmonics only");
    partials.setTooltip ("Learn each partial's tuning (inharmonicity: pianos, bells), instead of exact harmonics");
    representation.setTooltip ("Decibels: morphs blend spectral shapes. Shape + loudness: the loudness envelope is separate "
                               "from the spectrum's shape. Linear: morphs behave more like crossfades");
    pitchTracking.setTooltip ("With sounds at several pitches: learn how timbre changes with pitch, so each note gets the "
                              "timbre of its register (Keytrack sets how much)");
    refresh();
}

void TrainPanel::controlsFromSettings()
{
    const auto s = trainer.getSettings();
    title.setText (s.title, juce::dontSendNotification);
    note.setSelectedId (s.analysis.autoPitch ? kAutoId : noteId (s.analysis.midiNote), juce::dontSendNotification);
    duration.setValue (s.analysis.duration, juce::dontSendNotification);
    harmonics.setValue (s.analysis.harmonics, juce::dontSendNotification);
    floorDb.setValue (s.analysis.floorDb, juce::dontSendNotification);
    components.setValue (s.components, juce::dontSendNotification);
    normalize.setToggleState (s.analysis.normalizeLoudness, juce::dontSendNotification);
    trim.setToggleState (s.analysis.trimOnset, juce::dontSendNotification);
    noiseBands.setValue (s.analysis.noiseBands, juce::dontSendNotification);
    partials.setToggleState (s.analysis.trackPartials, juce::dontSendNotification);
    representation.setSelectedId (static_cast<int> (s.analysis.representation) + 1, juce::dontSendNotification);
    pitchTracking.setSelectedId (static_cast<int> (s.analysis.pitchTracking) + 1, juce::dontSendNotification);
}

void TrainPanel::settingsFromControls()
{
    auto s = trainer.getSettings();
    s.title = title.getText();
    s.analysis.autoPitch = note.getSelectedId() == kAutoId;
    if (! s.analysis.autoPitch && note.getSelectedId() > kAutoId)
        s.analysis.midiNote = note.getSelectedId() - 2;
    s.analysis.duration = duration.getValue();
    s.analysis.harmonics = static_cast<int> (harmonics.getValue());
    s.analysis.floorDb = floorDb.getValue();
    s.components = static_cast<int> (components.getValue());
    s.analysis.normalizeLoudness = normalize.getToggleState();
    s.analysis.trimOnset = trim.getToggleState();
    s.analysis.noiseBands = static_cast<int> (noiseBands.getValue());
    s.analysis.trackPartials = partials.getToggleState();
    s.analysis.representation = static_cast<pcs::Representation> (juce::jmax (0, representation.getSelectedId() - 1));
    s.analysis.pitchTracking = static_cast<pcs::PitchTracking> (juce::jmax (0, pitchTracking.getSelectedId() - 1));
    trainer.setSettings (s);
}

void TrainPanel::refresh()
{
    progressValue = trainer.isRunning() ? trainer.getProgress() : (trainer.getProgress() >= 1.0 ? 1.0 : 0.0);
    if (trainer.getVersion() == seenVersion)
        return;
    seenVersion = trainer.getVersion();
    entries = trainer.getEntries();
    list.updateContent();
    list.repaint();
    const auto text = trainer.getStatus();
    status.setText (text.isNotEmpty() ? text : juce::String (static_cast<int> (entries.size())) + " sounds", juce::dontSendNotification);
    const bool running = trainer.isRunning();
    trainButton.setButtonText (running ? "Cancel" : "Train");
    for (auto* c : std::initializer_list<juce::Component*> { &addFiles, &addFolder, &removeButton, &clearButton, &title, &note,
                                                            &duration, &harmonics, &floorDb, &components, &normalize, &trim,
                                                            &noiseBands, &partials, &representation, &pitchTracking })
        c->setEnabled (! running);
    if (! running)
        controlsFromSettings();
}

void TrainPanel::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (row < 0 || row >= static_cast<int> (entries.size()))
        return;
    const auto& e = entries[static_cast<size_t> (row)];
    if (selected)
        g.fillAll (theme::accent.withAlpha (0.35f));
    auto r = juce::Rectangle<int> (0, 0, width, height).reduced (8, 0);
    g.setFont (theme::font (13.0f));
    g.setColour (theme::text);
    g.drawText (e.name, r.removeFromLeft (width * 45 / 100), juce::Justification::centredLeft, true);
    juce::String info;
    juce::Colour colour = theme::faint;
    switch (e.status)
    {
        case Trainer::Entry::Status::Pending: info = "not analysed yet"; break;
        case Trainer::Entry::Status::Analysed:
        {
            const double midi = 69.0 + 12.0 * std::log2 (e.f0 / 440.0);
            const int nearest = juce::roundToInt (midi);
            info = juce::MidiMessage::getMidiNoteName (nearest, true, true, 4) + " "
                 + (midi >= nearest ? "+" : "") + juce::String (juce::roundToInt (100.0 * (midi - nearest))) + " cents  ("
                 + juce::String (e.f0, 1) + " Hz)";
            colour = theme::muted;
            break;
        }
        case Trainer::Entry::Status::Failed:
            info = "failed: " + e.message;
            colour = theme::cursor;
            break;
    }
    g.setColour (colour);
    g.drawText (info, r, juce::Justification::centredLeft, true);
}

void TrainPanel::chooseFiles (bool folders)
{
    chooser = std::make_unique<juce::FileChooser> (folders ? "Add a folder of sounds" : "Add sounds", juce::File(),
                                                   folders ? juce::String() : "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3;*.m4a;*.caf");
    const int chooserFlags = juce::FileBrowserComponent::openMode
                    | (folders ? juce::FileBrowserComponent::canSelectDirectories
                               : juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems);
    chooser->launchAsync (chooserFlags, [this] (const juce::FileChooser& fc) {
        juce::StringArray paths;
        for (const auto& f : fc.getResults())
            paths.add (f.getFullPathName());
        if (! paths.isEmpty())
            status.setText ("Added " + juce::String (trainer.addFiles (paths)) + " sounds.", juce::dontSendNotification);
    });
}

void TrainPanel::removeSelected()
{
    juce::Array<int> rows;
    for (int i = 0; i < list.getNumSelectedRows(); ++i)
        rows.add (list.getSelectedRow (i));
    if (! rows.isEmpty())
    {
        trainer.remove (rows);
        list.deselectAllRows();
    }
}

void TrainPanel::paint (juce::Graphics& g)
{
    theme::drawPanel (g, getLocalBounds(), "Train a new space");
}

void TrainPanel::resized()
{
    auto r = getLocalBounds().reduced (10);
    auto header = r.removeFromTop (22);
    closeButton.setBounds (header.removeFromRight (80).withHeight (22));
    r.removeFromTop (4);

    auto right = r.removeFromRight (juce::jmin (380, r.getWidth() * 45 / 100));
    r.removeFromRight (10);

    // Left: the sounds.
    hint.setBounds (r.removeFromTop (34));
    auto buttons = r.removeFromBottom (28);
    for (auto* b : { &addFiles, &addFolder, &removeButton, &clearButton })
        b->setBounds (buttons.removeFromLeft (110).reduced (2, 0));
    r.removeFromBottom (6);
    list.setBounds (r);

    // Right: settings, then Train.
    auto row = [&right] { auto x = right.removeFromTop (24); right.removeFromTop (3); return x; };
    juce::Component* controls[] = { &title, &note, &duration, &harmonics, &floorDb, &components, &noiseBands, &representation, &pitchTracking };
    for (size_t i = 0; i < labels.size(); ++i)
    {
        auto x = row();
        labels[i]->setBounds (x.removeFromLeft (84));
        controls[i]->setBounds (x.reduced (4, 2));
    }
    auto toggles = row();
    toggles.removeFromLeft (88);
    const int third = toggles.getWidth() / 3;
    normalize.setBounds (toggles.removeFromLeft (third));
    trim.setBounds (toggles.removeFromLeft (third));
    partials.setBounds (toggles);
    right.removeFromTop (4);
    auto trainRow = right.removeFromTop (30);
    trainButton.setBounds (trainRow.removeFromLeft (120));
    trainRow.removeFromLeft (8);
    progress.setBounds (trainRow.reduced (0, 6));
    right.removeFromTop (6);
    status.setBounds (right.removeFromTop (40));
}
