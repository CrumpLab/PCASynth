#include "PresetBar.h"

#include "Presets.h"
#include "Theme.h"

PresetBar::PresetBar (SpaceProcessor& p) : processor (p)
{
    label.setFont (theme::font (13.0f));
    label.setJustificationType (juce::Justification::centredRight);
    for (auto* c : std::initializer_list<juce::Component*> { &label, &prev, &box, &next, &saveButton })
        addAndMakeVisible (c);
    prev.setTooltip ("Previous preset");
    next.setTooltip ("Next preset");
    saveButton.setTooltip ("Save the current sound (space, point and settings) as a preset");
    box.setTooltip ("Factory presets play the built-in space; your presets carry their own space with them");
    box.setTextWhenNothingSelected ("No preset");
    prev.onClick = [this] { step (-1); };
    next.onClick = [this] { step (1); };
    saveButton.onClick = [this] { save(); };
    box.onChange = [this] { choose (box.getSelectedId()); };
    refresh();
}

void PresetBar::refresh()
{
    box.clear (juce::dontSendNotification);
    const auto& factory = processor.getFactoryPresets();
    juce::String category;
    for (size_t i = 0; i < factory.size(); ++i)
    {
        if (factory[i].category != category)
        {
            category = factory[i].category;
            box.addSectionHeading (category);
        }
        box.addItem (factory[i].name, static_cast<int> (i) + 1);
    }
    const auto folder = pcsplugin::userPresetFolder();
    userFiles = pcsplugin::findUserPresets (folder);
    if (! userFiles.isEmpty())
    {
        box.addSectionHeading ("Your presets");
        for (int i = 0; i < userFiles.size(); ++i)
            box.addItem (userFiles[i].getRelativePathFrom (folder).upToLastOccurrenceOf (pcsplugin::kPresetExtension, false, true),
                         kUserBase + i);
    }
    box.addSeparator();
    box.addItem ("Load Preset File...", kLoadFile);
    box.addItem ("Show Presets Folder", kOpenFolder);

    const int program = processor.getFactoryPresetIndex();
    const auto name = processor.getPresetName();
    if (name.isNotEmpty() && program >= 0 && program < static_cast<int> (factory.size()) && factory[static_cast<size_t> (program)].name == name)
        box.setSelectedId (program + 1, juce::dontSendNotification);
    else
    {
        int found = 0;
        for (int i = 0; i < userFiles.size() && found == 0; ++i)
            if (userFiles[i].getFileNameWithoutExtension() == name)
                found = kUserBase + i;
        if (found != 0)
            box.setSelectedId (found, juce::dontSendNotification);
        else
            box.setText (name, juce::dontSendNotification);
    }
}

void PresetBar::choose (int id)
{
    if (id <= 0)
        return;
    const int numFactory = static_cast<int> (processor.getFactoryPresets().size());
    if (id <= numFactory)
    {
        processor.loadFactoryPreset (id - 1);
        return;
    }
    if (id >= kUserBase && id < kUserBase + userFiles.size())
    {
        loadFile (userFiles[id - kUserBase]);
        return;
    }
    refresh(); // back to showing the current preset
    if (id == kOpenFolder)
    {
        const auto folder = pcsplugin::userPresetFolder();
        folder.createDirectory();
        folder.revealToUser();
    }
    else if (id == kLoadFile)
    {
        chooser = std::make_unique<juce::FileChooser> ("Load a PCASynth preset", pcsplugin::userPresetFolder(),
                                                       juce::String ("*") + pcsplugin::kPresetExtension);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc) {
                                  if (const auto file = fc.getResult(); file != juce::File())
                                      loadFile (file);
                              });
    }
}

void PresetBar::loadFile (const juce::File& file)
{
    const auto error = processor.loadPresetFile (file);
    if (onMessage)
        onMessage (error.isEmpty() ? "Loaded preset " + file.getFileNameWithoutExtension() : error);
}

void PresetBar::step (int delta)
{
    // Factory presets, then the user's, wrapping around.
    const int numFactory = static_cast<int> (processor.getFactoryPresets().size());
    std::vector<int> ids;
    for (int i = 1; i <= numFactory; ++i)
        ids.push_back (i);
    for (int i = 0; i < userFiles.size(); ++i)
        ids.push_back (kUserBase + i);
    const auto at = std::find (ids.begin(), ids.end(), box.getSelectedId());
    const int index = at == ids.end() ? (delta > 0 ? -1 : 0) : static_cast<int> (at - ids.begin());
    const int n = static_cast<int> (ids.size());
    choose (ids[static_cast<size_t> (((index + delta) % n + n) % n)]);
}

void PresetBar::save()
{
    const auto folder = pcsplugin::userPresetFolder();
    folder.createDirectory();
    auto name = processor.getPresetName();
    if (name.isEmpty())
        name = "My Preset";
    chooser = std::make_unique<juce::FileChooser> ("Save a PCASynth preset",
                                                   folder.getChildFile (juce::File::createLegalFileName (name) + pcsplugin::kPresetExtension),
                                                   juce::String ("*") + pcsplugin::kPresetExtension);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc) {
                              auto file = fc.getResult();
                              if (file == juce::File())
                                  return;
                              file = file.withFileExtension (pcsplugin::kPresetExtension);
                              const auto error = processor.savePresetFile (file);
                              refresh();
                              if (onMessage)
                                  onMessage (error.isEmpty() ? "Saved preset " + file.getFileNameWithoutExtension() : error);
                          });
}

void PresetBar::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromLeft (52));
    saveButton.setBounds (r.removeFromRight (112).reduced (2));
    next.setBounds (r.removeFromRight (30).reduced (2));
    prev.setBounds (r.removeFromLeft (30).reduced (2));
    box.setBounds (r.reduced (2));
}
