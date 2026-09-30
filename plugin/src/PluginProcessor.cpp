#include "PluginProcessor.h"
#include "PluginEditor.h"

#include "FactoryModel.h"

#include "pcs/Wav.h"

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
    alive->store (false);
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

juce::String PCASynthProcessor::saveModelFile (const juce::File& file) const
{
    const auto m = getModel();
    if (m == nullptr)
        return "No model to save.";
    try
    {
        pcs::saveModel (*m, file.getFullPathName().toStdString());
        return {};
    }
    catch (const std::exception& e)
    {
        return e.what();
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
    resolveDirection();
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

void PCASynthProcessor::setDetail (const Point& d) noexcept
{
    for (size_t j = 0; j < d.size(); ++j)
        detail[j].store (d[j], std::memory_order_relaxed);
}

PCASynthProcessor::Point PCASynthProcessor::getDetail() const noexcept
{
    Point d {};
    for (size_t j = 0; j < d.size(); ++j)
        d[j] = detail[j].load (std::memory_order_relaxed);
    return d;
}

PCASynthProcessor::Point PCASynthProcessor::getPoint() const
{
    Point z = getDetail();
    for (int j = 0; j < pcsplugin::kNumPcParams; ++j)
        z[static_cast<size_t> (j)] = parameters.getRawParameterValue (pcsplugin::pcId (j))->load();
    return z;
}

void PCASynthProcessor::setPoint (const Point& z)
{
    Point d {};
    for (int j = 0; j < pcs::kMaxComponents; ++j)
    {
        const float v = z[static_cast<size_t> (j)];
        if (j < pcsplugin::kNumPcParams)
        {
            if (auto* param = parameters.getParameter (pcsplugin::pcId (j)))
            {
                const float normalised = param->convertTo0to1 (juce::jlimit (-4.0f, 4.0f, v));
                if (std::abs (param->getValue() - normalised) > 1e-7f)
                    param->setValueNotifyingHost (normalised);
            }
        }
        else
            d[static_cast<size_t> (j)] = v;
    }
    setDetail (d);
}

void PCASynthProcessor::beginPointGesture()
{
    for (int j = 0; j < pcsplugin::kNumPcParams; ++j)
        if (auto* param = parameters.getParameter (pcsplugin::pcId (j)))
            param->beginChangeGesture();
}

void PCASynthProcessor::endPointGesture()
{
    for (int j = 0; j < pcsplugin::kNumPcParams; ++j)
        if (auto* param = parameters.getParameter (pcsplugin::pcId (j)))
            param->endChangeGesture();
}

void PCASynthProcessor::jumpToSound (int index)
{
    const auto m = getModel();
    if (m == nullptr || index < 0 || index >= m->numSounds())
        return;
    const auto z = m->soundZ (index);
    Point p {};
    for (size_t j = 0; j < z.size() && j < p.size(); ++j)
        p[j] = z[j];
    beginPointGesture();
    setPoint (p);
    endPointGesture();
}

void PCASynthProcessor::resetToMean()
{
    beginPointGesture();
    setPoint ({});
    endPointGesture();
}

pcs::SynthParams PCASynthProcessor::currentSynthParams() const
{
    auto p = reader.read (getDetail());
    p.mod.hasDirection = hasDirection.load();
    for (size_t j = 0; j < p.mod.direction.size(); ++j)
        p.mod.direction[j] = direction[j].load (std::memory_order_relaxed);
    p.bpm = bpm.load();
    return p;
}

void PCASynthProcessor::setDirectionSound (const juce::String& name)
{
    setUiValue ("directionSound", name);
    resolveDirection();
}

juce::String PCASynthProcessor::getDirectionSound() const { return getUiValue ("directionSound", "").toString(); }

void PCASynthProcessor::resolveDirection()
{
    const auto m = getModel();
    const int index = m != nullptr ? m->soundIndex (getDirectionSound().toStdString()) : -1;
    if (index < 0)
    {
        hasDirection = false;
        return;
    }
    const auto z = m->soundZ (index);
    for (size_t j = 0; j < direction.size(); ++j)
        direction[j].store (j < z.size() ? z[j] : 0.0f, std::memory_order_relaxed);
    hasDirection = true;
}

PCASynthProcessor::Point PCASynthProcessor::getHeardPoint() const noexcept
{
    Point p {};
    for (size_t j = 0; j < p.size(); ++j)
        p[j] = heardPoint[j].load (std::memory_order_relaxed);
    return p;
}

int PCASynthProcessor::getVoicePoints (Point* points, int max) const noexcept
{
    const int n = std::min (max, numVoicePoints.load());
    for (int i = 0; i < n; ++i)
        for (size_t j = 0; j < points[i].size(); ++j)
            points[i][j] = voicePoint[static_cast<size_t> (i)][j].load (std::memory_order_relaxed);
    return n;
}

int PCASynthProcessor::getVoicePositions (float* positions, int max) const noexcept
{
    const int n = std::min (max, numVoicePos.load());
    for (int i = 0; i < n; ++i)
        positions[i] = voicePos[static_cast<size_t> (i)].load (std::memory_order_relaxed);
    return n;
}

juce::var PCASynthProcessor::getUiValue (const juce::Identifier& key, const juce::var& fallback) const
{
    return parameters.state.getProperty (key, fallback);
}

void PCASynthProcessor::setUiValue (const juce::Identifier& key, const juce::var& value)
{
    parameters.state.setProperty (key, value, nullptr);
}

pcs::AudioBuffer PCASynthProcessor::renderNote (std::shared_ptr<const pcs::Model> m, const pcs::SynthParams& params, int note,
                                                double seconds)
{
    pcs::Synth offline;
    offline.prepare (48000.0);
    offline.setParams (params);
    offline.setModel (std::move (m));
    const double release = std::max (0.0, static_cast<double> (params.release));
    const int total = static_cast<int> (seconds * 48000.0);
    const int noteOff = std::max (1, static_cast<int> ((seconds - std::min (release, 0.5 * seconds)) * 48000.0));
    pcs::AudioBuffer out;
    out.sampleRate = 48000.0;
    out.resize (2, total);
    for (int pos = 0; pos < total; pos += 512)
    {
        const int n = std::min (512, total - pos);
        pcs::MidiEvent noteEvents[2];
        int count = 0;
        if (pos == 0)
            noteEvents[count++] = { 0, pcs::MidiEvent::Type::NoteOn, note, 0.8f };
        if (noteOff >= pos && noteOff < pos + n)
            noteEvents[count++] = { noteOff - pos, pcs::MidiEvent::Type::NoteOff, note, 0.0f };
        float* ch[] = { out.channels[0].data() + pos, out.channels[1].data() + pos };
        offline.process (ch, 2, n, noteEvents, count);
    }
    return out;
}

void PCASynthProcessor::exportWav (const juce::File& file, int note, double seconds, std::function<void (juce::String)> done)
{
    const auto m = getModel();
    const auto params = currentSynthParams();
    const auto path = file.getFullPathName().toStdString();
    auto flag = alive;
    juce::Thread::launch ([m, params, note, seconds, path, flag, done = std::move (done)] {
        juce::String error;
        try
        {
            pcs::writeWav (path, renderNote (m, params, note, seconds), pcs::WavFormat::Pcm24);
        }
        catch (const std::exception& e)
        {
            error = e.what();
        }
        juce::MessageManager::callAsync ([flag, done, error] {
            if (flag->load())
                done (error);
        });
    });
}

void PCASynthProcessor::timerCallback()
{
    collectGarbage();
    applyControllerSetup();
}

void PCASynthProcessor::applyControllerSetup()
{
    if (const int zone = pendingZone.exchange (-1); zone >= 0)
    {
        setParamValue (pcsplugin::id::mpeOn, 1.0f);
        setParamValue (pcsplugin::id::mpeZone, static_cast<float> (zone));
    }
    if (const int range = pendingBendRange.exchange (-1); range > 0)
        setParamValue (pcsplugin::id::mpeBendRange, static_cast<float> (range));
}

void PCASynthProcessor::parseRpn (const juce::MidiMessage& m) noexcept
{
    if (! m.isController())
        return;
    const int ch = m.getChannel();
    auto& r = rpn[static_cast<size_t> (ch)];
    switch (m.getControllerNumber())
    {
        case 101: r.msb = m.getControllerValue(); break;
        case 100: r.lsb = m.getControllerValue(); break;
        case 6:
            r.dataMsb = m.getControllerValue();
            if (r.msb == 0 && r.lsb == 6 && (ch == 1 || ch == 16) && r.dataMsb > 0)
                pendingZone = ch == 1 ? 0 : 1; // MPE Configuration Message
            else if (r.msb == 0 && r.lsb == 0 && ch != 1 && ch != 16 && r.dataMsb > 0)
                pendingBendRange = r.dataMsb;  // per-note pitch bend sensitivity
            break;
        default: break;
    }
}

int PCASynthProcessor::getVoiceInfo (pcs::Synth::VoiceInfo* out, int max) const
{
    const juce::SpinLock::ScopedLockType sl (voiceInfoLock);
    const int n = std::min (max, numVoiceInfos);
    std::copy (voiceInfos.begin(), voiceInfos.begin() + n, out);
    return n;
}

bool PCASynthProcessor::getNewestVoicePoint (Point& out) const noexcept
{
    if (! hasNewest.load())
        return false;
    for (size_t j = 0; j < out.size(); ++j)
        out[j] = newestPoint[j].load (std::memory_order_relaxed);
    return true;
}

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

    // Host tempo for synced walks and LFOs.
    if (auto* ph = getPlayHead())
        if (const auto pos = ph->getPosition())
            if (const auto b = pos->getBpm())
                bpm.store (*b);

    // Parameters first, so a model swap (which resets voices) starts from them.
    synth.setParams (currentSynthParams());

    // A new model, once the previous old one has been collected.
    if (retired.load() == nullptr)
        if (auto* p = pending.exchange (nullptr))
        {
            std::unique_ptr<pcs::Synth::ModelSlot> slot (p);
            synth.swapModel (slot);
            retired.store (slot.release());
        }

    events.clear();
    const bool mpe = parameters.getRawParameterValue (pcsplugin::id::mpeOn)->load() > 0.5f;
    for (const auto meta : midi)
    {
        if (events.size() == events.capacity())
            break;
        const auto m = meta.getMessage();
        parseRpn (m);
        pcs::MidiEvent e;
        e.offset = meta.samplePosition;
        if (m.isNoteOn())
            lastNote.store (m.getNoteNumber(), std::memory_order_relaxed);
        if (m.isNoteOn())
            e = { e.offset, pcs::MidiEvent::Type::NoteOn, m.getNoteNumber(), m.getFloatVelocity() };
        else if (m.isNoteOff())
            e = { e.offset, pcs::MidiEvent::Type::NoteOff, m.getNoteNumber(), 0.0f };
        else if (m.isPitchWheel())
            e = { e.offset, pcs::MidiEvent::Type::PitchBend, 0, (m.getPitchWheelValue() - 8192) / 8192.0f };
        else if (m.isSustainPedalOn() || m.isSustainPedalOff())
            e = { e.offset, pcs::MidiEvent::Type::Sustain, 0, m.isSustainPedalOn() ? 1.0f : 0.0f };
        else if (m.isControllerOfType (1))
            e = { e.offset, pcs::MidiEvent::Type::ModWheel, 0, m.getControllerValue() / 127.0f };
        else if (m.isChannelPressure())
            e = { e.offset, pcs::MidiEvent::Type::Pressure, 0, m.getChannelPressureValue() / 127.0f };
        else if (m.isAftertouch()) // per note with MPE, else the global pressure
            e = { e.offset, mpe ? pcs::MidiEvent::Type::PolyPressure : pcs::MidiEvent::Type::Pressure, m.getNoteNumber(),
                  m.getAfterTouchValue() / 127.0f };
        else if (mpe && m.isControllerOfType (74))
            e = { e.offset, pcs::MidiEvent::Type::Slide, 0, m.getControllerValue() / 127.0f };
        else if (m.isAllNotesOff() || m.isAllSoundOff())
            e = { e.offset, pcs::MidiEvent::Type::AllNotesOff, 0, 0.0f };
        else
            continue;
        e.channel = m.getChannel();
        events.push_back (e);
    }

    synth.process (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples(), events.data(),
                   static_cast<int> (events.size()));
    activeVoices.store (synth.activeVoiceCount(), std::memory_order_relaxed);
    float positions[pcs::Synth::kMaxVoices];
    const int n = synth.voicePositions (positions, pcs::Synth::kMaxVoices);
    for (int i = 0; i < n; ++i)
        voicePos[static_cast<size_t> (i)].store (positions[i], std::memory_order_relaxed);
    numVoicePos.store (n, std::memory_order_relaxed);

    const auto& hp = synth.heardPoint();
    for (size_t j = 0; j < hp.size(); ++j)
        heardPoint[j].store (hp[j], std::memory_order_relaxed);
    pcs::Point pts[kMaxShownVoices];
    const int nv = synth.voicePoints (pts, kMaxShownVoices);
    for (int i = 0; i < nv; ++i)
        for (size_t j = 0; j < pts[i].size(); ++j)
            voicePoint[static_cast<size_t> (i)][j].store (pts[i][j], std::memory_order_relaxed);
    numVoicePoints.store (nv, std::memory_order_relaxed);
    pcs::Point newest;
    const bool have = synth.newestVoicePoint (newest);
    if (have)
        for (size_t j = 0; j < newest.size(); ++j)
            newestPoint[j].store (newest[j], std::memory_order_relaxed);
    hasNewest.store (have, std::memory_order_relaxed);
    {
        const juce::SpinLock::ScopedTryLockType sl (voiceInfoLock);
        if (sl.isLocked())
            numVoiceInfos = synth.voiceInfo (voiceInfos.data(), kMaxShownVoices);
    }
    lastBlockMs.store (juce::Time::getMillisecondCounter(), std::memory_order_relaxed);
    hasProcessed.store (true, std::memory_order_relaxed);
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
    state.removeChild (state.getChildWithName (Trainer::treeType), nullptr);
    state.appendChild (trainer.toValueTree(), nullptr);
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
        if (const auto training = tree.getChildWithName (Trainer::treeType); training.isValid())
        {
            trainer.fromValueTree (training);
            tree.removeChild (training, nullptr);
        }
        parameters.replaceState (tree);
        resolveDirection();
        sendChangeMessage(); // the editor re-reads its settings (map axes, morph corners)
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
