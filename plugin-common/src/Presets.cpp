#include "Presets.h"


namespace pcsplugin {

juce::File userPresetFolder()
{
   #if JUCE_MAC
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Audio/Presets/CrumpLab/" JucePlugin_Name);
   #else
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("CrumpLab/" JucePlugin_Name "/Presets");
   #endif
}

juce::Array<juce::File> findUserPresets (const juce::File& folder)
{
    auto files = folder.findChildFiles (juce::File::findFiles, true, juce::String ("*") + kPresetExtension);
    std::sort (files.begin(), files.end(), [&folder] (const juce::File& a, const juce::File& b) {
        return a.getRelativePathFrom (folder).compareNatural (b.getRelativePathFrom (folder)) < 0;
    });
    return files;
}

} // namespace pcsplugin
