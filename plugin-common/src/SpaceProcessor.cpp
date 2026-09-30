#include "SpaceProcessor.h"

#include "pcs/Model.h"
#include "pcs/Wav.h"
#include "pcs/WaveModel.h"

#include <bit>

namespace {
// Plugin state: magic | i32 xml length | xml | i64 model length | model (the space's file bytes; 0 = factory space).
// The magic tells the plugins apart: "PCS1" PCASynth, "PCW1" PCAWave.
const char* stateMagic (pcsplugin::Kind kind) { return kind == pcsplugin::Kind::Wave ? "PCW1" : "PCS1"; }

bool isKind (const pcs::Space* s, pcsplugin::Kind kind)
{
    return kind == pcsplugin::Kind::Wave ? dynamic_cast<const pcs::WaveModel*> (s) != nullptr : dynamic_cast<const pcs::Model*> (s) != nullptr;
}
} // namespace

SpaceProcessor::SpaceProcessor (pcsplugin::Kind k)
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      kind (k),
      parameters (*this, nullptr, k == pcsplugin::Kind::Wave ? "PCAWave" : "PCASynth", pcsplugin::createLayout (k)),
      reader (parameters)
{
    events.reserve (1024);
    startTimerHz (10);
    // The factory space is loaded by the subclass's constructor (factorySpace() is virtual).
}

SpaceProcessor::~SpaceProcessor()
{
    alive->store (false);
    stopTimer();
    delete pending.exchange (nullptr);
    delete retired.exchange (nullptr);
    delete pendingClip.exchange (nullptr);
    delete retiredClip.exchange (nullptr);
    delete playingClip;
}

void SpaceProcessor::audition (const pcs::AudioBuffer& clip)
{
    if (clip.numSamples() == 0)
        return;
    // Resampled (linearly) to the rate the host plays at.
    const double sr = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;
    const double step = clip.sampleRate / sr;
    const auto& in = clip.channels[0];
    auto c = std::make_unique<Clip>();
    c->samples.resize (static_cast<size_t> (static_cast<double> (in.size()) / step));
    for (size_t i = 0; i < c->samples.size(); ++i)
    {
        const double x = static_cast<double> (i) * step;
        const auto k = static_cast<size_t> (x);
        const double f = x - static_cast<double> (k);
        const float a = in[std::min (k, in.size() - 1)], b = in[std::min (k + 1, in.size() - 1)];
        c->samples[i] = static_cast<float> (a + f * (b - a));
    }
    delete pendingClip.exchange (c.release());
    auditioning = true;
}

void SpaceProcessor::stopAudition()
{
    delete pendingClip.exchange (new Clip()); // an empty clip replaces the playing one
}

void SpaceProcessor::loadFactoryModel() { setModel (factorySpace(), true); }

juce::String SpaceProcessor::loadModelFile (const juce::File& file)
{
    try
    {
        auto m = pcs::loadSpace (file.getFullPathName().toStdString());
        if (! isKind (m.get(), kind))
            return file.getFileName() + (kind == pcsplugin::Kind::Wave ? " is a harmonic space (for PCASynth), not a waveform space"
                                                                         : " is a waveform space (for PCAWave), not a harmonic space");
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

juce::String SpaceProcessor::saveModelFile (const juce::File& file) const
{
    const auto m = getSpace();
    if (m == nullptr)
        return "No model to save.";
    try
    {
        pcs::saveSpace (*m, file.getFullPathName().toStdString());
        return {};
    }
    catch (const std::exception& e)
    {
        return e.what();
    }
}

void SpaceProcessor::setModel (std::shared_ptr<const pcs::Space> m, bool isFactory)
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

std::shared_ptr<const pcs::Space> SpaceProcessor::getSpace() const
{
    const std::lock_guard<std::mutex> lock (modelLock);
    return model;
}

bool SpaceProcessor::isFactoryModel() const
{
    const std::lock_guard<std::mutex> lock (modelLock);
    return factory;
}

void SpaceProcessor::setParamValue (const juce::String& paramId, float realValue)
{
    if (auto* param = parameters.getParameter (paramId))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 (realValue));
        param->endChangeGesture();
    }
}

// ---- presets ----------------------------------------------------------------

void SpaceProcessor::loadFactoryPreset (int index)
{
    const auto& all = getFactoryPresets();
    if (index < 0 || index >= static_cast<int> (all.size()))
        return;
    const auto& preset = all[static_cast<size_t> (index)];
    if (! isFactoryModel())
        loadFactoryModel();
    for (auto* param : AudioProcessor::getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
            ranged->setValueNotifyingHost (ranged->getDefaultValue());
    for (const auto& [paramId, value] : preset.params)
        setParamValue (paramId, value);
    setDirectionSound (preset.direction);

    Point z {};
    const auto m = getSpace();
    if (const int sound = m != nullptr ? m->soundIndex (preset.sound.toStdString()) : -1; sound >= 0)
    {
        const auto zs = m->soundZ (sound);
        std::copy (zs.begin(), zs.begin() + static_cast<long> (std::min (zs.size(), z.size())), z.begin());
    }
    for (const auto& [component, offset] : preset.offsets)
        z[static_cast<size_t> (component)] += offset;
    beginPointGesture();
    setPoint (z);
    endPointGesture();

    currentProgram = index;
    setUiValue ("presetName", preset.name);
    sendChangeMessage();
    updateHostDisplay (ChangeDetails().withProgramChanged (true));
}

juce::String SpaceProcessor::savePresetFile (const juce::File& file)
{
    const auto previous = getPresetName();
    setUiValue ("presetName", file.getFileNameWithoutExtension());
    juce::MemoryBlock data;
    writeState (data, true);
    if (! file.getParentDirectory().createDirectory() || ! file.replaceWithData (data.getData(), data.getSize()))
    {
        setUiValue ("presetName", previous);
        return "Could not write " + file.getFullPathName();
    }
    currentProgram = -1;
    return {};
}

juce::String SpaceProcessor::loadPresetFile (const juce::File& file)
{
    juce::MemoryBlock data;
    if (! file.loadFileAsData (data))
        return "Could not read " + file.getFileName();
    if (data.getSize() < 16 || std::memcmp (data.getData(), stateMagic (kind), 4) != 0)
        return file.getFileName() + " is not a " + juce::String (JucePlugin_Name) + " preset";
    // The editor's layout stays as it is.
    const auto tab = getUiValue ("tab", 0), showTraining = getUiValue ("showTraining", false);
    setStateInformation (data.getData(), static_cast<int> (data.getSize()));
    setUiValue ("tab", tab);
    setUiValue ("showTraining", showTraining);
    setUiValue ("presetName", file.getFileNameWithoutExtension());
    currentProgram = -1;
    sendChangeMessage();
    return {};
}

void SpaceProcessor::setDetail (const Point& d) noexcept
{
    for (size_t j = 0; j < d.size(); ++j)
        detail[j].store (d[j], std::memory_order_relaxed);
}

SpaceProcessor::Point SpaceProcessor::getDetail() const noexcept
{
    Point d {};
    for (size_t j = 0; j < d.size(); ++j)
        d[j] = detail[j].load (std::memory_order_relaxed);
    return d;
}

SpaceProcessor::Point SpaceProcessor::getPoint() const
{
    Point z = getDetail();
    for (int j = 0; j < pcsplugin::kNumPcParams; ++j)
        z[static_cast<size_t> (j)] = parameters.getRawParameterValue (pcsplugin::pcId (j))->load();
    return z;
}

void SpaceProcessor::setPoint (const Point& z)
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

void SpaceProcessor::beginPointGesture()
{
    for (int j = 0; j < pcsplugin::kNumPcParams; ++j)
        if (auto* param = parameters.getParameter (pcsplugin::pcId (j)))
            param->beginChangeGesture();
}

void SpaceProcessor::endPointGesture()
{
    for (int j = 0; j < pcsplugin::kNumPcParams; ++j)
        if (auto* param = parameters.getParameter (pcsplugin::pcId (j)))
            param->endChangeGesture();
}

void SpaceProcessor::jumpToSound (int index)
{
    const auto m = getSpace();
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

void SpaceProcessor::resetToMean()
{
    beginPointGesture();
    setPoint ({});
    endPointGesture();
}

pcs::SynthParams SpaceProcessor::currentSynthParams() const
{
    auto p = reader.read (getDetail());
    p.mod.hasDirection = hasDirection.load();
    for (size_t j = 0; j < p.mod.direction.size(); ++j)
        p.mod.direction[j] = direction[j].load (std::memory_order_relaxed);
    p.bpm = bpm.load();
    return p;
}

void SpaceProcessor::setDirectionSound (const juce::String& name)
{
    setUiValue ("directionSound", name);
    resolveDirection();
}

juce::String SpaceProcessor::getDirectionSound() const { return getUiValue ("directionSound", "").toString(); }

void SpaceProcessor::resolveDirection()
{
    const auto m = getSpace();
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

SpaceProcessor::Point SpaceProcessor::getHeardPoint() const noexcept
{
    Point p {};
    for (size_t j = 0; j < p.size(); ++j)
        p[j] = heardPoint[j].load (std::memory_order_relaxed);
    return p;
}

int SpaceProcessor::getVoicePoints (Point* points, int max) const noexcept
{
    const int n = std::min (max, numVoicePoints.load());
    for (int i = 0; i < n; ++i)
        for (size_t j = 0; j < points[i].size(); ++j)
            points[i][j] = voicePoint[static_cast<size_t> (i)][j].load (std::memory_order_relaxed);
    return n;
}

int SpaceProcessor::getVoicePositions (float* positions, int max) const noexcept
{
    const int n = std::min (max, numVoicePos.load());
    for (int i = 0; i < n; ++i)
        positions[i] = voicePos[static_cast<size_t> (i)].load (std::memory_order_relaxed);
    return n;
}

juce::var SpaceProcessor::getUiValue (const juce::Identifier& key, const juce::var& fallback) const
{
    return parameters.state.getProperty (key, fallback);
}

void SpaceProcessor::setUiValue (const juce::Identifier& key, const juce::var& value)
{
    parameters.state.setProperty (key, value, nullptr);
}

pcs::AudioBuffer SpaceProcessor::renderNote (std::shared_ptr<const pcs::Space> m, const pcs::SynthParams& params, int note,
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

void SpaceProcessor::exportWav (const juce::File& file, int note, double seconds, std::function<void (juce::String)> done)
{
    const auto m = getSpace();
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

void SpaceProcessor::timerCallback()
{
    collectGarbage();
    applyControllerSetup();
}

void SpaceProcessor::applyControllerSetup()
{
    if (const int zone = pendingZone.exchange (-1); zone >= 0)
    {
        setParamValue (pcsplugin::id::mpeOn, 1.0f);
        setParamValue (pcsplugin::id::mpeZone, static_cast<float> (zone));
    }
    if (const int range = pendingBendRange.exchange (-1); range > 0)
        setParamValue (pcsplugin::id::mpeBendRange, static_cast<float> (range));
}

void SpaceProcessor::parseRpn (const juce::MidiMessage& m) noexcept
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

int SpaceProcessor::getVoiceInfo (pcs::Synth::VoiceInfo* out, int max) const
{
    const juce::SpinLock::ScopedLockType sl (voiceInfoLock);
    const int n = std::min (max, numVoiceInfos);
    std::copy (voiceInfos.begin(), voiceInfos.begin() + n, out);
    return n;
}

bool SpaceProcessor::getNewestVoicePoint (Point& out) const noexcept
{
    if (! hasNewest.load())
        return false;
    for (size_t j = 0; j < out.size(); ++j)
        out[j] = newestPoint[j].load (std::memory_order_relaxed);
    return true;
}

bool SpaceProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo()) && layouts.inputBuses.isEmpty();
}

void SpaceProcessor::prepareToPlay (double sampleRate, int)
{
    synth.prepare (sampleRate);
}

double SpaceProcessor::getTailLengthSeconds() const
{
    return parameters.getRawParameterValue (pcsplugin::id::release)->load();
}

void SpaceProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
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

    // An audition clip (Inspect panel), over the synth.
    if (retiredClip.load() == nullptr)
        if (auto* c = pendingClip.exchange (nullptr))
        {
            retiredClip.store (playingClip);
            playingClip = c;
        }
    if (playingClip != nullptr && playingClip->pos < playingClip->samples.size())
    {
        const int n = static_cast<int> (std::min<size_t> (static_cast<size_t> (buffer.getNumSamples()), playingClip->samples.size() - playingClip->pos));
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.addFrom (ch, 0, playingClip->samples.data() + playingClip->pos, n);
        playingClip->pos += static_cast<size_t> (n);
    }
    auditioning.store (playingClip != nullptr && playingClip->pos < playingClip->samples.size(), std::memory_order_relaxed);
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


void SpaceProcessor::getStateInformation (juce::MemoryBlock& destData) { writeState (destData, false); }

void SpaceProcessor::writeState (juce::MemoryBlock& destData, bool asPreset)
{
    auto state = parameters.copyState();
    state.setProperty ("stateVersion", kStateVersion, nullptr);
    juce::StringArray d;
    for (float v : getDetail())
        d.add (juce::String::toHexString (static_cast<juce::int64> (std::bit_cast<uint32_t> (v)))); // bit-exact
    state.setProperty ("detail", d.joinIntoString (","), nullptr);
    state.removeChild (state.getChildWithName (Trainer::treeType), nullptr);
    if (asPreset)
    {
        for (const char* layout : { "tab", "showTraining" })
            state.removeProperty (layout, nullptr);
    }
    else
    {
        state.appendChild (trainer.toValueTree(), nullptr);
        state.setProperty ("program", currentProgram.load(), nullptr);
    }
    auto xml = state.createXml();
    if (xml == nullptr)
        return;
    const auto xmlText = xml->toString().toStdString();

    std::vector<uint8_t> blob;
    {
        const std::lock_guard<std::mutex> lock (modelLock);
        if (! factory && model != nullptr)
            blob = pcs::serializeSpace (*model);
    }

    juce::MemoryOutputStream out (destData, false);
    out.write (stateMagic (kind), 4);
    out.writeInt (static_cast<int> (xmlText.size()));
    out.write (xmlText.data(), xmlText.size());
    out.writeInt64 (static_cast<juce::int64> (blob.size()));
    out.write (blob.data(), blob.size());
}

void SpaceProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (sizeInBytes < 16 || std::memcmp (data, stateMagic (kind), 4) != 0)
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
        currentProgram = static_cast<int> (tree.getProperty ("program", -1));
        tree.removeProperty ("program", nullptr);
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
            auto space = pcs::deserializeSpace (static_cast<const uint8_t*> (blob.getData()), blob.getSize());
            if (isKind (space.get(), kind))
            {
                setModel (std::move (space), false);
                return;
            }
        }
        catch (const std::exception&)
        {
            // A damaged model must not stop the parameters loading: fall back to the factory space.
        }
    }
    if (! isFactoryModel())
        loadFactoryModel();
}

