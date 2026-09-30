// Renders the PCAWave editor to a PNG (headless), for docs and CI.
//   pcs-wave-snapshot out.png [width height] [preset:NAME | mix | train | inspect [sound]]
#include "../src/WaveEditor.h"
#include "../src/WaveProcessor.h"
#include "TrainingSet.h"
#include "pcs/Wav.h"

#include <cstdio>

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf ("usage: pcs-wave-snapshot out.png [width height] [preset:NAME | mix | train | inspect [sound]]\n");
        return 1;
    }
    juce::ScopedJuceInitialiser_GUI gui;
    const int width = argc > 3 ? std::atoi (argv[2]) : 1180;
    const int height = argc > 3 ? std::atoi (argv[3]) : 840;
    const juce::String mode = argc > 4 ? juce::String (argv[4]) : juce::String ("preset:Reed Into Vowel");

    PCAWaveProcessor processor;
    processor.setPlayConfigDetails (0, 2, 48000.0, 512);
    processor.prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    editor->setSize (width, height);
    auto* ed = dynamic_cast<PCAWaveEditor*> (editor.get());

    auto loadPreset = [&] (const juce::String& name) {
        const auto& presets = processor.getFactoryPresets();
        for (size_t i = 0; i < presets.size(); ++i)
            if (presets[i].name == name)
                processor.loadFactoryPreset (static_cast<int> (i));
    };
    if (mode.startsWith ("preset:"))
        loadPreset (mode.fromFirstOccurrenceOf ("preset:", false, false));
    else if (mode == "mix")
    {
        loadPreset ("Reed Into Vowel");
        ed->selectTab (1);
    }
    else if (mode == "train" || mode == "inspect")
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("pcs-wave-snapshot");
        dir.deleteRecursively();
        dir.createDirectory();
        for (const auto& clip : pcs::testgen::generateTrainingSet ({}))
            pcs::writeWav (dir.getChildFile (clip.name + ".wav").getFullPathName().toStdString(), clip.audio);
        processor.getTrainer().addFiles ({ dir.getFullPathName() });
        if (mode == "train")
            ed->showTraining (true);
        else
        {
            ed->showInspector (true);
            auto& inspector = processor.getInspector();
            inspector.evaluateAll();
            auto wait = [&inspector] {
                for (int i = 0; i < 2400 && inspector.isBusy(); ++i)
                    juce::Thread::sleep (50);
            };
            wait();
            const juce::String sound = argc > 5 ? argv[5] : "vowel_2";
            ed->getInspectPanel().selectSound (processor.getSpace()->soundIndex (sound.toStdString()));
            wait();
            ed->getInspectPanel().refresh();
        }
        ed->refreshModel();
        dir.deleteRecursively();
    }
    ed->refreshModel();

    // A chord, so the playheads show.
    juce::AudioBuffer<float> block (2, 512);
    juce::MidiBuffer midi;
    for (int n : { 48, 55, 64 })
        midi.addEvent (juce::MidiMessage::noteOn (1, n, 0.8f), 0);
    for (int i = 0; i < 60; ++i)
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
