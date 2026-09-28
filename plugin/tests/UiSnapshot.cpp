// Renders the editor to a PNG without a window.
//   pcs-ui-snapshot out.png [width height] [training sound to jump to]
#include "../src/PluginEditor.h"
#include "../src/PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdio>

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf ("usage: pcs-ui-snapshot out.png [width height] [sound]\n");
        return 1;
    }
    juce::ScopedJuceInitialiser_GUI gui;
    const int width = argc > 3 ? std::atoi (argv[2]) : 760;
    const int height = argc > 3 ? std::atoi (argv[3]) : 700;

    PCASynthProcessor processor;
    if (argc > 4)
        processor.jumpToSound (processor.getModel()->soundIndex (argv[4]));

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    editor->setSize (width, height);
    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
    juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]));
    out.deleteFile();
    juce::FileOutputStream stream (out);
    juce::PNGImageFormat png;
    if (! stream.openedOk() || ! png.writeImageToStream (image, stream))
    {
        std::printf ("could not write %s\n", argv[1]);
        return 1;
    }
    std::printf ("wrote %s\n", out.getFullPathName().toRawUTF8());
    return 0;
}
