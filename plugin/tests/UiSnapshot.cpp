// Renders the editor to a PNG without a window, with a chord sounding so the
// playheads show.
//   pcs-ui-snapshot out.png [width height] [training sound to jump to] [morph u v]
#include "../src/PluginEditor.h"
#include "../src/PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdio>

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf ("usage: pcs-ui-snapshot out.png [width height] [sound] [morph-u morph-v]\n");
        return 1;
    }
    juce::ScopedJuceInitialiser_GUI gui;
    const int width = argc > 3 ? std::atoi (argv[2]) : 1180;
    const int height = argc > 3 ? std::atoi (argv[3]) : 840;

    PCASynthProcessor processor;
    processor.setPlayConfigDetails (0, 2, 48000.0, 512);
    processor.prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    editor->setSize (width, height);
    auto* ed = dynamic_cast<PCASynthEditor*> (editor.get());

    if (argc > 6)
    {
        const float u = static_cast<float> (std::atof (argv[5])), v = static_cast<float> (std::atof (argv[6]));
        processor.setPoint (ed->getMorphPad().blend (u, v));
    }
    else if (argc > 4)
        processor.jumpToSound (processor.getModel()->soundIndex (argv[4]));

    // Play a chord for a second.
    juce::AudioBuffer<float> block (2, 512);
    juce::MidiBuffer midi;
    for (int n : { 48, 55, 64 })
        midi.addEvent (juce::MidiMessage::noteOn (1, n, 0.8f), 0);
    for (int i = 0; i < 90; ++i)
    {
        processor.processBlock (block, midi);
        midi.clear();
    }
    ed->refresh();

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
