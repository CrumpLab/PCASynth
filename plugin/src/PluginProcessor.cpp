#include "PluginProcessor.h"
#include "PluginEditor.h"

#include "FactoryModel.h"

#include <bit>

namespace {
// Plugin state: "PCS1" | i32 xml length | xml | i64 model length | model (.pcsm bytes; 0 = factory model)
constexpr char kStateMagic[4] = { 'P', 'C', 'S', '1' };
} // namespace

PCASynthProcessor::PCASynthProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "PCASynth", pcsplugin::createLayout()),
      reader (parameters)
{
    events.reserve (1024);
    loadFactoryModel();
    startTimerHz (10);
}

PCASynthProcessor::~PCASynthProcessor()
{
    stopTimer();
    delete pending.exchange (nullptr);
    delete retired.exchange (nullptr);
}

std::shared_ptr<const pcs::Model> PCASynthProcessor::factoryModel()
{
    static const std::shared_ptr<const pcs::Model> m = [] {
        auto loaded = std::make_shared<pcs::Model> (pcs::deserializeModel (
            reinterpret_cast<const uint8_t*> (FactoryModel::synthetic_pcsm), static_cast<size_t> (FactoryModel::synthetic_pcsmSize)));
        return std::shared_ptr<const pcs::Model> (std::move (loaded));
    }();
    return m;
}

void PCASynthProcessor::loadFactoryModel() { setModel (factoryModel(), true); }

juce::String PCASynthProcessor::loadModelFile (const juce::File& file)
{
    try
    {
        auto m = std::make_shared<pcs::Model> (pcs::loadModel (file.getFullPathName().toStdString()));
        if (m->title.empty())
            m->title = file.getFileNameWithoutExtension().toStdString();
        setModel (std::move (m), false);
        return {};
    }
    catch (const std::exception& e)
    {
        return file.getFileName() + ": " + e.what();
    }
}

void PCASynthProcessor::setModel (std::shared_ptr<const pcs::Model> m, bool isFactory)
{
    auto slot = pcs::Synth::makeSlot (m);
    delete pending.exchange (slot.release()); // an older, never-used slot
    {
        const std::lock_guard<std::mutex> lock (modelLock);
        model = std::move (m);
        factory = isFactory;
    }
    sendChangeMessage();
}

std::shared_ptr<const pcs::Model> PCASynthProcessor::getModel() const
{
    const std::lock_guard<std::mutex> lock (modelLock);
    return model;
}

bool PCASynthProcessor::isFactoryModel() const
{
    const std::lock_guard<std::mutex> lock (modelLock);
    return factory;
}

void PCASynthProcessor::setParamValue (const juce::String& paramId, float realValue)
{
    if (auto* param = parameters.getParameter (paramId))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 (realValue));
        param->endChangeGesture();
    }
}

void PCASynthProcessor::setDetail (const std::array<float, pcs::kMaxComponents>& d) noexcept
{
    for (size_t j = 0; j < d.size(); ++j)
        detail[j].store (d[j], std::memory_order_relaxed);
}

std::array<float, pcs::kMaxComponents> PCASynthProcessor::getDetail() const noexcept
{
    std::array<float, pcs::kMaxComponents> d {};
    for (size_t j = 0; j < d.size(); ++j)
        d[j] = detail[j].load (std::memory_order_relaxed);
    return d;
}

void PCASynthProcessor::jumpToSound (int index)
{
    const auto m = getModel();
    if (m == nullptr || index < 0 || index >= m->numSounds())
        return;
    const auto z = m->soundZ (index);
    std::array<float, pcs::kMaxComponents> d {};
    for (int j = 0; j < pcs::kMaxComponents; ++j)
    {
        const float v = j < static_cast<int> (z.size()) ? z[static_cast<size_t> (j)] : 0.0f;
        if (j < pcsplugin::kNumPcParams)
            setParamValue (pcsplugin::pcId (j), juce::jlimit (-4.0f, 4.0f, v));
        else
            d[static_cast<size_t> (j)] = v;
    }
    setDetail (d);
}

void PCASynthProcessor::resetToMean()
{
    for (int j = 0; j < pcsplugin::kNumPcParams; ++j)
        setParamValue (pcsplugin::pcId (j), 0.0f);
    setDetail ({});
}

void PCASynthProcessor::timerCallback() { collectGarbage(); }

bool PCASynthProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo()) && layouts.inputBuses.isEmpty();
}

void PCASynthProcessor::prepareToPlay (double sampleRate, int)
{
    synth.prepare (sampleRate);
}

double PCASynthProcessor::getTailLengthSeconds() const
{
    return parameters.getRawParameterValue (pcsplugin::id::release)->load();
}

void PCASynthProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    // Parameters first, so a model swap (which resets voices) starts from them.
    synth.setParams (reader.read (getDetail()));

    // A new model, once the previous old one has been collected.
    if (retired.load() == nullptr)
        if (auto* p = pending.exchange (nullptr))
        {
            std::unique_ptr<pcs::Synth::ModelSlot> slot (p);
            synth.swapModel (slot);
            retired.store (slot.release());
        }

    events.clear();
    for (const auto meta : midi)
    {
        if (events.size() == events.capacity())
            break;
        const auto m = meta.getMessage();
        pcs::MidiEvent e;
        e.offset = meta.samplePosition;
        if (m.isNoteOn())
            e = { e.offset, pcs::MidiEvent::Type::NoteOn, m.getNoteNumber(), m.getFloatVelocity() };
        else if (m.isNoteOff())
            e = { e.offset, pcs::MidiEvent::Type::NoteOff, m.getNoteNumber(), 0.0f };
        else if (m.isPitchWheel())
            e = { e.offset, pcs::MidiEvent::Type::PitchBend, 0, (m.getPitchWheelValue() - 8192) / 8192.0f };
        else if (m.isSustainPedalOn() || m.isSustainPedalOff())
            e = { e.offset, pcs::MidiEvent::Type::Sustain, 0, m.isSustainPedalOn() ? 1.0f : 0.0f };
        else if (m.isAllNotesOff() || m.isAllSoundOff())
            e = { e.offset, pcs::MidiEvent::Type::AllNotesOff, 0, 0.0f };
        else
            continue;
        events.push_back (e);
    }

    synth.process (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples(), events.data(),
                   static_cast<int> (events.size()));
    activeVoices.store (synth.activeVoiceCount(), std::memory_order_relaxed);
}

juce::AudioProcessorEditor* PCASynthProcessor::createEditor() { return new PCASynthEditor (*this); }

void PCASynthProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.setProperty ("stateVersion", kStateVersion, nullptr);
    juce::StringArray d;
    for (float v : getDetail())
        d.add (juce::String::toHexString (static_cast<juce::int64> (std::bit_cast<uint32_t> (v)))); // bit-exact
    state.setProperty ("detail", d.joinIntoString (","), nullptr);
    auto xml = state.createXml();
    if (xml == nullptr)
        return;
    const auto xmlText = xml->toString().toStdString();

    std::vector<uint8_t> blob;
    {
        const std::lock_guard<std::mutex> lock (modelLock);
        if (! factory && model != nullptr)
            blob = pcs::serializeModel (*model);
    }

    juce::MemoryOutputStream out (destData, false);
    out.write (kStateMagic, 4);
    out.writeInt (static_cast<int> (xmlText.size()));
    out.write (xmlText.data(), xmlText.size());
    out.writeInt64 (static_cast<juce::int64> (blob.size()));
    out.write (blob.data(), blob.size());
}

void PCASynthProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (sizeInBytes < 16 || std::memcmp (data, kStateMagic, 4) != 0)
        return;
    juce::MemoryInputStream in (data, static_cast<size_t> (sizeInBytes), false);
    in.skipNextBytes (4);
    const int xmlLen = in.readInt();
    if (xmlLen <= 0 || xmlLen > in.getNumBytesRemaining())
        return;
    juce::MemoryBlock xmlBytes;
    in.readIntoMemoryBlock (xmlBytes, xmlLen);
    if (auto xml = juce::parseXML (xmlBytes.toString()); xml != nullptr && xml->hasTagName (parameters.state.getType()))
    {
        auto tree = juce::ValueTree::fromXml (*xml);
        std::array<float, pcs::kMaxComponents> d {};
        juce::StringArray items;
        items.addTokens (tree.getProperty ("detail").toString(), ",", {});
        for (int j = 0; j < items.size() && j < pcs::kMaxComponents; ++j)
            d[static_cast<size_t> (j)] = std::bit_cast<float> (static_cast<uint32_t> (items[j].getHexValue64()));
        setDetail (d);
        parameters.replaceState (tree);
    }

    const auto blobLen = in.readInt64();
    if (blobLen > 0 && blobLen <= in.getNumBytesRemaining())
    {
        juce::MemoryBlock blob;
        in.readIntoMemoryBlock (blob, static_cast<ssize_t> (blobLen));
        try
        {
            setModel (std::make_shared<const pcs::Model> (
                          pcs::deserializeModel (static_cast<const uint8_t*> (blob.getData()), blob.getSize())),
                      false);
            return;
        }
        catch (const std::exception&)
        {
            // A damaged model must not stop the parameters loading: fall back to the factory space.
        }
    }
    if (! isFactoryModel())
        loadFactoryModel();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PCASynthProcessor(); }
