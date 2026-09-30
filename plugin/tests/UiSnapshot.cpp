// Renders the editor to a PNG without a window, with a chord sounding so the
// playheads show.
//   pcs-ui-snapshot out.png [width height] [training sound to jump to] [morph u v]
//   pcs-ui-snapshot out.png width height train   (the Train panel after training on generated notes)
//   pcs-ui-snapshot out.png width height walk    (a neighbour tour under a chord, Random Walk tab)
//   pcs-ui-snapshot out.png width height mod     (LFOs & Expression tab)
//   pcs-ui-snapshot out.png width height mpe     (MPE tab, three notes with their own expression)
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

    const juce::String mode = argc > 4 ? juce::String (argv[4]) : juce::String();
    auto set = [&processor] (const juce::String& id, float v) {
        auto* param = processor.getParameters().getParameter (id);
        param->setValueNotifyingHost (param->convertTo0to1 (v));
    };
    if (mode == "walk")
    {
        processor.jumpToSound (processor.getModel()->soundIndex ("reed_1"));
        set ("walk_on", 1.0f);
        set ("walk_mode", 3.0f); // neighbour tour
        set ("walk_rate", 1.2f);
        set ("walk_amount", 1.0f);
        set ("walk_per_voice", 0.3f);
        ed->selectTab (1);
    }
    else if (mode == "mpe")
    {
        set ("mpe_on", 1.0f);
        ed->selectTab (3);
    }
    else if (mode == "mod")
    {
        set ("lfo1_on", 1.0f);
        set ("lfo2_on", 1.0f);
        set ("lfo2_shape", 5.0f);
        set ("vel_dest", 3.0f);
        processor.setDirectionSound ("vowel_1");
        ed->refreshModel();
        ed->selectTab (2);
    }

    // Play a chord (for three seconds with a walk, so it leaves a trail).
    juce::AudioBuffer<float> block (2, 512);
    juce::MidiBuffer midi;
    if (mode == "mpe")
    {
        // Three notes on member channels, each with its own bend, pressure and slide.
        const int notes[] = { 48, 55, 64 };
        for (int i = 0; i < 3; ++i)
        {
            const int ch = 2 + i;
            midi.addEvent (juce::MidiMessage::pitchWheel (ch, 8192 + (i - 1) * 300), 0);
            midi.addEvent (juce::MidiMessage::noteOn (ch, notes[i], 0.8f), 0);
            midi.addEvent (juce::MidiMessage::channelPressureChange (ch, 30 + 40 * i), 0);
            midi.addEvent (juce::MidiMessage::controllerEvent (ch, 74, 40 + 30 * i), 0);
        }
    }
    else
        for (int n : { 48, 55, 64 })
            midi.addEvent (juce::MidiMessage::noteOn (1, n, 0.8f), 0);
    const int blocks = mode == "walk" ? 280 : 90;
    for (int i = 0; i < blocks; ++i)
    {
        processor.processBlock (block, midi);
        midi.clear();
        if (i % 5 == 0)
            ed->refresh();
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
