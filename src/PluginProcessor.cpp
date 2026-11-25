#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace pd;

juce::AudioProcessor::BusesProperties PureDevotionProcessor::makeBuses()
{
    return BusesProperties()
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
        .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false);
}

PureDevotionProcessor::PureDevotionProcessor()
    : juce::AudioProcessor (makeBuses()),
      apvts (*this, nullptr, "PARAMS", ParamTable::get().createLayout()),
      engine (*wtBank, *smpBank)
{
    const auto& t = ParamTable::get();
    for (int i = 0; i < t.size(); ++i)
    {
        params.push_back (apvts.getParameter (t[i].id));
        raw.push_back (apvts.getRawParameterValue (t[i].id));
        jassert (params.back() != nullptr && raw.back() != nullptr);
    }
    plain.assign ((size_t) t.size(), 0.f);
    norm.assign ((size_t) t.size(), 0.f);
    publishEngineState();
    patchLock.write (patchProgram);
    midiOut.ensureSize (4096);
    midiScratch.ensureSize (8192);
    startTimerHz (30);
}

PureDevotionProcessor::~PureDevotionProcessor() { stopTimer(); }

bool PureDevotionProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    if (l.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    if (l.inputBuses.size() > 0)
    {
        const auto in = l.getChannelSet (true, 0);
        if (! in.isDisabled() && in != juce::AudioChannelSet::stereo() && in != juce::AudioChannelSet::mono()) return false;
    }
    return true;
}

void PureDevotionProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    const int maxBlock = juce::jmax (16, samplesPerBlock);
    engine.prepare (sampleRate, maxBlock);
    fxRack.prepare (sampleRate, maxBlock);
    patch.prepare (sampleRate);
    patch.setProgram (patchProgram);
    arp.prepare (sampleRate);
    riser.prepare (sampleRate);
    beatRepeat.prepare (sampleRate, maxBlock);
    chop.prepare (sampleRate);
    work.setSize (2, maxBlock, false, true, true);
    scBuf.setSize (2, maxBlock, false, true, true);
    esSeen = patchSeen = 0xffffffffu;
    const int osIdx = (int) getParamPlain (IX().osamp);
    setLatencySamples (engine.latencyForOversampling (getParamPlain (IX().eco) > 0.5f ? 0 : osIdx));
    latencyOsIndex = osIdx;
}

float PureDevotionProcessor::getParamPlain (int i) const { return raw[(size_t) i]->load(); }

void PureDevotionProcessor::setParamPlain (int i, float v)
{
    auto* p = params[(size_t) i];
    p->setValueNotifyingHost (p->convertTo0to1 (v));
}

// ============================================================ audio
void PureDevotionProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const auto& I = IX();
    const auto& T = ParamTable::get();
    const int n = buffer.getNumSamples();
    if (n > work.getNumSamples())   // host broke its promise: process in chunks
    {
        // fall back to safe chunked processing (no reallocation)
        juce::MidiBuffer empty;
        for (int start = 0; start < n; start += work.getNumSamples())
        {
            const int len = juce::jmin (work.getNumSamples(), n - start);
            juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), start, len);
            juce::MidiBuffer part;
            for (const auto m : midi) if (m.samplePosition >= start && m.samplePosition < start + len) part.addEvent (m.getMessage(), m.samplePosition - start);
            processBlock (view, part);
        }
        return;
    }

    // sidechain (before we overwrite the buffer)
    const float* scL = nullptr; const float* scR = nullptr;
    if (getBusCount (true) > 0 && getBus (true, 0)->isEnabled())
    {
        auto sc = getBusBuffer (buffer, true, 0);
        if (sc.getNumChannels() > 0)
        {
            scBuf.copyFrom (0, 0, sc, 0, 0, n);
            scBuf.copyFrom (1, 0, sc, sc.getNumChannels() > 1 ? 1 : 0, 0, n);
            scL = scBuf.getReadPointer (0); scR = scBuf.getReadPointer (1);
        }
    }

    // 1. parameters
    for (size_t i = 0; i < plain.size(); ++i)
    {
        plain[i] = raw[i]->load (std::memory_order_relaxed);
        norm[i] = T[(int) i].range.convertTo0to1 (plain[i]);
    }
    // 2. published non-parameter state
    engineLock.tryRead (esAudio, esSeen);
    if (patchLock.tryRead (patchAudio, patchSeen)) patch.setProgram (patchAudio);

    // 3. transport
    Transport tr;
    tr.sampleRate = currentSampleRate;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) tr.bpm = *b;
            if (auto q = pos->getPpqPosition()) tr.ppqAtBlockStart = *q;
            tr.playing = pos->getIsPlaying();
        }

    // 4. UI events
    {
        const auto scope = uiFifo.read (uiFifo.getNumReady());
        auto handle = [&] (int start, int size) {
            for (int k = 0; k < size; ++k)
            {
                const auto& e = uiEvents[start + k];
                if (e.type == 0) chop.trigger (e.a, e.v, smpBank->get ((int) plain[(size_t) I.chopSample]), esAudio.chopPitch[e.a & 15], esAudio.chopFormant[e.a & 15]);
                else if (e.type == 1) midi.addEvent (juce::MidiMessage::noteOn (1, e.a, e.v), 0);
                else midi.addEvent (juce::MidiMessage::noteOff (1, e.a), 0);
            }
        };
        handle (scope.startIndex1, scope.blockSize1);
        handle (scope.startIndex2, scope.blockSize2);
    }

    // 5. MIDI learn, learned CCs, chop-pad channel
    {
        const int chopCh = (int) plain[(size_t) I.chopChannel];
        auto& filtered = midiScratch;
        filtered.clear();
        bool changed = false;
        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            if (m.isController())
            {
                const int cc = m.getControllerNumber();
                const int lp = learnParam.load();
                if (lp >= 0 && cc != 64 && cc != 1)
                {
                    learnedCC = cc; learnedFor = lp; learnParam = -1;
                    esAudio.midiLearn[cc] = (int16_t) lp;
                }
                const int target = esAudio.midiLearn[cc];
                if (target >= 0 && target < (int) params.size())
                {
                    params[(size_t) target]->setValueNotifyingHost ((float) m.getControllerValue() / 127.f);
                    changed = true;
                    continue;
                }
            }
            if (chopCh > 0 && m.getChannel() == chopCh && (m.isNoteOnOrOff()))
            {
                const int slot = m.getNoteNumber() - 36;
                if (slot >= 0 && slot < kNumChopSlots)
                {
                    if (m.isNoteOn()) chop.trigger (slot, m.getFloatVelocity(), smpBank->get ((int) plain[(size_t) I.chopSample]),
                                                    esAudio.chopPitch[slot], esAudio.chopFormant[slot]);
                    changed = true;
                    continue;
                }
            }
            filtered.addEvent (m, meta.samplePosition);
        }
        if (changed) midi.swapWith (filtered);
    }

    // 6. chord / arp
    arp.process (midi, n, plain.data(), esAudio, tr);

    // 7. riser / drop
    const bool drop = riser.process (n, plain[(size_t) I.riserArm] > 0.5f, (int) plain[(size_t) I.riserBars], tr);
    if (drop && plain[(size_t) I.dropKill] > 0.5f) fxRack.killTails();
    const float rp = riser.progress();
    SynthEngine::Inputs in;
    in.base = plain.data(); in.baseNorm = norm.data(); in.es = &esAudio; in.tr = tr;
    in.patchOut = patch.patchOut;
    in.riser = rp;
    in.riserCutoffOct = riser.isActive() ? plain[(size_t) I.riserFilter] * 4.f * rp * rp : 0.f;
    in.riserPitch = riser.isActive() ? plain[(size_t) I.riserPitch] * 12.f * rp * rp * rp : 0.f;

    // 8. patch -> parameter jacks (from the previous block)
    for (int k = 0; k < patch.numParamMods; ++k)
    {
        const int d = patch.paramMods[k].param;
        if (d < 0 || d >= (int) plain.size()) continue;
        norm[(size_t) d] = clamp01 (norm[(size_t) d] + patch.paramMods[k].amount);
        plain[(size_t) d] = T[d].range.convertFrom0to1 (norm[(size_t) d]);
    }

    // 9. synth
    juce::AudioBuffer<float> out (buffer.getArrayOfWritePointers(), 2, n);
    engine.process (out, midi, n, in);

    // 10. modular patch (audio + CV)
    float* L = out.getWritePointer (0); float* R = out.getWritePointer (1);
    if (patch.active())
    {
        PatchEngine::Inputs pin;
        work.copyFrom (0, 0, out, 0, 0, n); work.copyFrom (1, 0, out, 1, 0, n);
        pin.synthL = work.getReadPointer (0); pin.synthR = work.getReadPointer (1);
        pin.note = engine.lastNote(); pin.gate = engine.lastGate(); pin.velocity = engine.lastVelocity(); pin.modWheel = engine.modWheel();
        float macros[kNumMacros]; for (int m = 0; m < kNumMacros; ++m) macros[m] = plain[(size_t) I.macro[m]];
        pin.macros = macros; pin.baseNorm = norm.data();
        pin.ppq = tr.ppqAtBlockStart; pin.beatsPerSample = tr.bpm / 60.0 / currentSampleRate; pin.playing = tr.playing;
        patch.process (n, pin, L, R);
    }

    // 11. riser noise swell + chop pad
    const float rn = plain[(size_t) I.riserNoise];
    if (riser.isActive() && rn > 0.f)
        for (int i = 0; i < n; ++i) { const float s = riser.noiseSample (rn); L[i] += s; R[i] += s; }
    chop.process (L, R, n, plain[(size_t) I.chopLvl]);

    // 12. FX
    FxCtx fctx;
    fctx.bpm = tr.bpm; fctx.ppq = tr.ppqAtBlockStart; fctx.playing = tr.playing; fctx.sampleRate = currentSampleRate;
    fctx.scL = scL; fctx.scR = scR;
    fctx.reverbThrow = riser.isActive() ? plain[(size_t) I.riserVerb] * rp : 0.f;
    fctx.delayThrow = fctx.reverbThrow * 0.5f;
    fxRack.process (out, n, plain.data(), esAudio, fctx);

    // 13. stutter / tape stop
    beatRepeat.process (L, R, n, plain[(size_t) I.stutter] > 0.5f, divisionBeats ((int) plain[(size_t) I.stutterDiv]),
                        plain[(size_t) I.tapeStop] > 0.5f, plain[(size_t) I.tapeStopTime], tr.bpm);

    // 14. master + safety soft clip
    const float g = dbToGain (plain[(size_t) I.masterVol]);
    for (int i = 0; i < n; ++i)
    {
        float l = L[i] * g, r = R[i] * g;
        if (std::abs (l) > 0.95f) l = (l > 0 ? 1.f : -1.f) * (0.95f + 0.05f * fastTanh ((std::abs (l) - 0.95f) * 20.f));
        if (std::abs (r) > 0.95f) r = (r > 0 ? 1.f : -1.f) * (0.95f + 0.05f * fastTanh ((std::abs (r) - 0.95f) * 20.f));
        L[i] = std::isfinite (l) ? l : 0.f; R[i] = std::isfinite (r) ? r : 0.f;
    }
    for (int c = 2; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);

    // 15. MIDI out: macro CCs
    midiOut.clear();
    if (esAudio.macroCCOut)
        for (int m = 0; m < kNumMacros; ++m)
        {
            const float v = plain[(size_t) I.macro[m]];
            if (std::abs (v - lastMacroSent[m]) >= 1.f / 127.f)
            {
                lastMacroSent[m] = v;
                midiOut.addEvent (juce::MidiMessage::controllerEvent (1, esAudio.macroCC[m], juce::jlimit (0, 127, (int) std::round (v * 127.f))), 0);
            }
        }
    midi.swapWith (midiOut);
}

// ============================================================ message thread
void PureDevotionProcessor::timerCallback()
{
    // MIDI learn result -> persistent state
    const int cc = learnedCC.exchange (-1);
    if (cc >= 0)
    {
        const int p = learnedFor.load();
        for (auto& x : engineState.midiLearn) if (x == p) x = -1;
        engineState.midiLearn[cc] = (int16_t) p;
        publishEngineState();
    }
    // latency follows oversampling
    const int osIdx = getParamPlain (IX().eco) > 0.5f ? 0 : (int) getParamPlain (IX().osamp);
    if (osIdx != latencyOsIndex)
    {
        latencyOsIndex = osIdx;
        setLatencySamples (engine.latencyForOversampling (osIdx));
    }
    applyMorph();
}

void PureDevotionProcessor::applyMorph()
{
    const auto& I = IX();
    if (getParamPlain (I.morphOn) < 0.5f) return;
    const float x = getParamPlain (I.xyX), y = getParamPlain (I.xyY);
    if (std::abs (x - lastX) < 1e-4f && std::abs (y - lastY) < 1e-4f) return;
    lastX = x; lastY = y;
    const float w[4] = { (1 - x) * (1 - y), x * (1 - y), (1 - x) * y, x * y };
    std::set<int> keys;
    for (auto& s : scenes) for (auto& [k, v] : s) keys.insert (k);
    const auto& T = ParamTable::get();
    for (int k : keys)
    {
        if (isLocked (k) || T[k].kind != PKind::Float) continue;
        float sum = 0, ws = 0;
        for (int s = 0; s < kNumScenes; ++s)
        {
            auto it = scenes[(size_t) s].find (k);
            if (it == scenes[(size_t) s].end()) continue;
            sum += w[s] * it->second; ws += w[s];
        }
        if (ws > 1e-6f) setParamPlain (k, sum / ws);
    }
}

void PureDevotionProcessor::setEngineState (const EngineState& s) { engineState = s; publishEngineState(); }
void PureDevotionProcessor::setPatch (const PatchProgram& p) { patchProgram = p; patchLock.write (patchProgram); }

int PureDevotionProcessor::addModulation (int source, int destParam, float amount)
{
    // reuse an existing slot with the same source/dest
    for (int s = 0; s < kNumModSlots; ++s)
        if (engineState.slots[s].src == source && engineState.slots[s].dest == destParam)
        { setParamPlain (IX().modAmt[s], amount); return s; }
    for (int s = 0; s < kNumModSlots; ++s)
        if (engineState.slots[s].src == SrcNone)
        {
            engineState.slots[s] = ModSlot();
            engineState.slots[s].src = (int16_t) source; engineState.slots[s].dest = (int16_t) destParam;
            publishEngineState();
            setParamPlain (IX().modAmt[s], amount);
            return s;
        }
    return -1;
}

void PureDevotionProcessor::removeModulation (int slot)
{
    if (slot < 0 || slot >= kNumModSlots) return;
    engineState.slots[slot] = ModSlot();
    publishEngineState();
    setParamPlain (IX().modAmt[slot], 0.f);
}

std::vector<int> PureDevotionProcessor::slotsForDest (int d) const
{
    std::vector<int> r;
    for (int s = 0; s < kNumModSlots; ++s) if (engineState.slots[s].src != SrcNone && engineState.slots[s].dest == d) r.push_back (s);
    return r;
}

void PureDevotionProcessor::captureScene (int s)
{
    auto& sc = scenes[(size_t) s];
    sc.clear();
    const auto& T = ParamTable::get();
    const auto& I = IX();
    for (int i = 0; i < T.size(); ++i)
    {
        if (T[i].kind != PKind::Float || T[i].group == "Perform" || T[i].group == "Matrix" || i == I.masterVol) continue;
        sc[i] = getParamPlain (i);
    }
    lastX = lastY = -1;
}

void PureDevotionProcessor::clearScenes() { for (auto& s : scenes) s.clear(); }

void PureDevotionProcessor::setLocked (int p, bool l) { if (l) lockedParams.insert (p); else lockedParams.erase (p); }

void PureDevotionProcessor::clearMidiLearn (int p)
{
    for (auto& x : engineState.midiLearn) if (x == p) x = -1;
    publishEngineState();
}

int PureDevotionProcessor::learnedCCFor (int p) const
{
    for (int c = 0; c < 128; ++c) if (engineState.midiLearn[c] == p) return c;
    return -1;
}

void PureDevotionProcessor::triggerChopPad (int slot, float vel)
{
    const auto scope = uiFifo.write (1);
    if (scope.blockSize1 > 0) uiEvents[scope.startIndex1] = { 0, slot, vel };
}

void PureDevotionProcessor::auditionNote (int note, float vel, bool on)
{
    const auto scope = uiFifo.write (1);
    if (scope.blockSize1 > 0) uiEvents[scope.startIndex1] = { on ? 1 : 2, note, vel };
}

// ============================================================ presets
PresetData PureDevotionProcessor::capturePreset() const
{
    PresetData d;
    const auto& T = ParamTable::get();
    for (int i = 0; i < T.size(); ++i) d.params[T[i].id] = getParamPlain (i);
    d.engine = engineStateToTree (engineState);
    d.patch = patchToTree (patchProgram);
    for (int s = 0; s < kNumScenes; ++s)
        for (auto& [k, v] : scenes[(size_t) s]) { d.scenes[(size_t) s][T[k].id] = v; d.hasScenes = true; }
    d.macroNames = macroNames;
    d.meta.name = currentPresetName;
    return d;
}

void PureDevotionProcessor::applyPreset (const PresetData& d)
{
    const auto& T = ParamTable::get();
    // build the preset's wavetables before switching so the change is seamless
    for (int o = 0; o < kNumOscs; ++o)
    {
        auto it = d.params.find ("o" + juce::String (o + 1) + "_table");
        if (it != d.params.end()) wtBank->getBlocking ((int) it->second);
    }
    for (int i = 0; i < T.size(); ++i)
    {
        if (isLocked (i)) continue;
        auto it = d.params.find (T[i].id);
        setParamPlain (i, it != d.params.end() ? it->second : T[i].def);
    }
    EngineState es;
    engineStateFromTree (es, d.engine);
    // global user settings survive preset changes
    std::copy (std::begin (engineState.midiLearn), std::end (engineState.midiLearn), std::begin (es.midiLearn));
    std::copy (std::begin (engineState.macroCC), std::end (engineState.macroCC), std::begin (es.macroCC));
    es.macroCCOut = engineState.macroCCOut;
    setEngineState (es);
    PatchProgram pp; patchFromTree (pp, d.patch); setPatch (pp);
    for (int s = 0; s < kNumScenes; ++s)
    {
        scenes[(size_t) s].clear();
        if (d.hasScenes) for (auto& [k, v] : d.scenes[(size_t) s]) { const int i = T.indexOf (k); if (i >= 0) scenes[(size_t) s][i] = v; }
    }
    lastX = lastY = -1;
    for (int m = 0; m < kNumMacros; ++m) macroNames.set (m, m < d.macroNames.size() ? d.macroNames[m] : "Macro " + juce::String (m + 1));
    currentPresetName = d.meta.name;
    currentPresetFile = d.meta.file;
}

bool PureDevotionProcessor::loadPresetFile (const juce::File& f)
{
    PresetData d; juce::String err;
    if (! PresetFormat::load (f, d, err)) { DBG ("Preset load failed: " + err); return false; }
    applyPreset (d);
    return true;
}

bool PureDevotionProcessor::savePresetFile (const juce::File& f, const PresetMeta& meta)
{
    auto d = capturePreset();
    d.meta = meta;
    if (PresetFormat::save (f, d)) { currentPresetName = meta.name; currentPresetFile = f; return true; }
    return false;
}

void PureDevotionProcessor::abSelect (int slot)
{
    slot = juce::jlimit (0, 1, slot);
    if (slot == abSlot) return;
    abData[abSlot] = capturePreset(); abValid[abSlot] = true;
    abSlot = slot;
    if (abValid[slot]) applyPreset (abData[slot]);
    else { abData[slot] = abData[1 - slot]; abValid[slot] = true; }
}

void PureDevotionProcessor::abCopyToOther()
{
    abData[1 - abSlot] = capturePreset(); abValid[1 - abSlot] = true;
}

// ============================================================ wavetables
int PureDevotionProcessor::addUserWavetable (const juce::String& name, const std::vector<float>& frames, int numFrames)
{
    const int idx = wtBank->addUserTable (name, frames, numFrames, (int) (frames.size() / (size_t) juce::jmax (1, numFrames)));
    if (idx >= 0) userTableNames.addIfNotAlreadyThere (name);
    return idx;
}

int PureDevotionProcessor::importWavetable (const juce::File& file)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (file));
    if (rd == nullptr) return -1;
    const int len = (int) std::min<juce::int64> (rd->lengthInSamples, (juce::int64) 2048 * 256);
    juce::AudioBuffer<float> buf (1, len);
    rd->read (&buf, 0, len, 0, true, false);
    // Serum-style tables use 2048-sample frames; otherwise slice into up to 256 frames
    int frameSize = 2048;
    if (len % 2048 != 0 || len < 2048) frameSize = juce::jmax (64, len / juce::jmin (256, juce::jmax (1, len / 256)));
    const int numFrames = juce::jlimit (1, kWtFrames, len / frameSize);
    std::vector<float> frames (buf.getReadPointer (0), buf.getReadPointer (0) + numFrames * frameSize);
    const int idx = addUserWavetable (file.getFileNameWithoutExtension(), frames, numFrames);
    // keep a copy in the user folder so the table can be restored with the session
    auto dest = PresetLibrary::userDir().getParentDirectory().getChildFile ("Wavetables").getChildFile (file.getFileName());
    if (! dest.existsAsFile()) { dest.getParentDirectory().createDirectory(); file.copyFileTo (dest); }
    return idx;
}

// ============================================================ state
void PureDevotionProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::ValueTree root ("PureDevotionState");
    root.setProperty ("version", kPresetFormatVersion, nullptr);
    root.setProperty ("presetName", currentPresetName, nullptr);
    root.appendChild (apvts.copyState(), nullptr);
    root.appendChild (engineStateToTree (engineState), nullptr);
    root.appendChild (patchToTree (patchProgram), nullptr);
    const auto& T = ParamTable::get();
    juce::ValueTree sc ("SCENES");
    for (int s = 0; s < kNumScenes; ++s)
    {
        juce::ValueTree one ("SCENE");
        for (auto& [k, v] : scenes[(size_t) s]) one.setProperty (T[k].id, v, nullptr);
        sc.appendChild (one, nullptr);
    }
    root.appendChild (sc, nullptr);
    juce::StringArray locks; for (int p : lockedParams) locks.add (T[p].id);
    root.setProperty ("locks", locks.joinIntoString (","), nullptr);
    root.setProperty ("macroNames", macroNames.joinIntoString ("|"), nullptr);
    root.setProperty ("userTables", userTableNames.joinIntoString ("|"), nullptr);
    root.appendChild (uiState.createCopy(), nullptr);
    if (auto xml = root.createXml()) copyXmlToBinary (*xml, dest);
}

void PureDevotionProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr) return;
    auto root = juce::ValueTree::fromXml (*xml);
    if (! root.isValid()) return;
    // restore user wavetables first so table indices resolve
    juce::StringArray ut; ut.addTokens (root.getProperty ("userTables").toString(), "|", {}); ut.removeEmptyStrings();
    const auto wtDir = PresetLibrary::userDir().getParentDirectory().getChildFile ("Wavetables");
    for (auto& name : ut)
        for (auto& f : wtDir.findChildFiles (juce::File::findFiles, false, name + ".*")) { importWavetable (f); break; }

    auto p = root.getChildWithName ("PARAMS");
    if (p.isValid()) apvts.replaceState (p);
    for (int o = 0; o < kNumOscs; ++o) wtBank->getBlocking ((int) getParamPlain (IX().osc[o].table));
    EngineState es; engineStateFromTree (es, root.getChildWithName ("ENGINE")); setEngineState (es);
    PatchProgram pp; patchFromTree (pp, root.getChildWithName ("PATCH")); setPatch (pp);
    const auto& T = ParamTable::get();
    auto sc = root.getChildWithName ("SCENES");
    for (int s = 0; s < kNumScenes; ++s)
    {
        scenes[(size_t) s].clear();
        auto one = sc.getChild (s);
        for (int k = 0; k < one.getNumProperties(); ++k)
        {
            const int i = T.indexOf (one.getPropertyName (k).toString());
            if (i >= 0) scenes[(size_t) s][i] = one.getProperty (one.getPropertyName (k));
        }
    }
    lockedParams.clear();
    juce::StringArray locks; locks.addTokens (root.getProperty ("locks").toString(), ",", {});
    for (auto& l : locks) { const int i = T.indexOf (l); if (i >= 0) lockedParams.insert (i); }
    juce::StringArray mn; mn.addTokens (root.getProperty ("macroNames").toString(), "|", {});
    for (int m = 0; m < juce::jmin (kNumMacros, mn.size()); ++m) macroNames.set (m, mn[m]);
    currentPresetName = root.getProperty ("presetName", "Init").toString();
    auto ui = root.getChildWithName ("UI");
    if (ui.isValid()) uiState.copyPropertiesAndChildrenFrom (ui, nullptr);
}

juce::AudioProcessorEditor* PureDevotionProcessor::createEditor() { return new PureDevotionEditor (*this); }

#ifndef PD_TOOL_BUILD
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PureDevotionProcessor(); }
#endif
