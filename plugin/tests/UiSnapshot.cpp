// Renders the editor to a PNG without a window, with a chord sounding so the
// playheads show.
//   pcs-ui-snapshot out.png [width height] [training sound to jump to] [morph u v]
//   pcs-ui-snapshot out.png width height train   (the Train panel after training on generated notes)
#include "../src/PluginEditor.h"
#include "../src/PluginProcessor.h"
#include "TrainingSet.h"
#include "pcs/Wav.h"

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

    if (argc > 4 && juce::String (argv[4]) == "train")
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("pcs-snapshot-train");
        dir.deleteRecursively();
        dir.createDirectory();
        int i = 0;
        for (const auto& family : pcs::testgen::families())
        {
            pcs::testgen::Options o;
            o.midiNote = 48 + (i * 5) % 24;
            o.duration = 2.0;
            const auto clip = pcs::testgen::generate (family, i % 3, o);
            pcs::writeWav (dir.getChildFile ("my " + juce::String (family) + ".wav").getFullPathName().toStdString(), clip.audio);
            ++i;
        }
        auto& trainer = processor.getTrainer();
        trainer.addFiles ({ dir.getFullPathName() });
        auto s = trainer.getSettings();
        s.analysis.autoPitch = true;
        s.analysis.duration = 2.0;
        s.title = "My instruments";
        trainer.setSettings (s);
        juce::String error;
        trainer.trainNow (error);
        ed->refreshModel();
        ed->getTrainPanel().setVisible (true);
        ed->getSoundMap().setVisible (false);
        ed->getMorphPad().setVisible (false);
        ed->getEnvelopeView().setVisible (false);
        dir.deleteRecursively();
    }
    else if (argc > 6)
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
    ed->resized();

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
