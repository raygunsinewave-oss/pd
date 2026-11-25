#include "SynthEngine.h"

namespace pd
{
SynthEngine::SynthEngine (WavetableBank& wt, SampleBank& smp) : wtBank (wt), smpBank (smp)
{
    const auto& t = ParamTable::get();
    isVoiceParam.resize ((size_t) t.size());
    for (int i = 0; i < t.size(); ++i)
    {
        const auto& g = t[i].group;
        const bool global = g.startsWith ("FX") || g == "Perform" || g.startsWith ("Seq") || g == "Macros" || g == "Matrix"
                         || t[i].id == "master_vol";
        isVoiceParam[(size_t) i] = ! global;
    }
}

void SynthEngine::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate; maxBlockSize = maxBlock;
    for (int i = 0; i < 3; ++i)
    {
        os[i] = std::make_unique<juce::dsp::Oversampling<float>> (2, (size_t) (i + 1),
                    juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, false, false);
        os[i]->initProcessing ((size_t) maxBlock);
    }
    for (auto& v : voices) v.prepare (sampleRate * kMaxOversample, maxBlock * kMaxOversample);
    for (auto& v : voices) v.setRenderRate (sampleRate);
    for (auto& l : globalLfo) l.setSampleRate ((float) sampleRate);
    osIndex = 0;
    reset();
}

void SynthEngine::reset()
{
    for (auto& v : voices) v.kill();
    for (auto& o : os) if (o) o->reset();
    monoCount = 0; heldCount = 0; gateOn = false; sustain = false;
    std::fill (std::begin (sustained), std::end (sustained), false);
}

int SynthEngine::latencyForOversampling (int idx) const
{
    if (idx <= 0 || os[idx - 1] == nullptr) return 0;
    return (int) std::ceil (os[idx - 1]->getLatencyInSamples());
}

void SynthEngine::allNotesOff() noexcept
{
    for (auto& v : voices) v.release();
    monoCount = 0; heldCount = 0; gateOn = false;
}

void SynthEngine::updateGlobalSources (BlockCtx& ctx, Inputs& in, int n)
{
    const auto& I = IX();
    const float* base = in.base;
    auto& g = ctx.globalSrc;
    for (int m = 0; m < kNumMacros; ++m) g[SrcMacro1 + m] = base[I.macro[m]];
    g[SrcModWheel] = modWheelVal;
    g[SrcPitchBend] = pitchBend;
    g[SrcAftertouch] = aftertouch;
    g[SrcExpression] = expression;
    for (int p = 0; p < 4; ++p) g[SrcPatch1 + p] = in.patchOut ? in.patchOut[p] : 0.f;
    g[SrcRiser] = in.riser;
    g[SrcXYX] = base[I.xyX]; g[SrcXYY] = base[I.xyY];
    g[SrcVelocity] = lastVel;
    g[SrcNote] = (lastNotePitch - 60.f) / 60.f;

    const double beat = in.tr.playing ? in.tr.ppqAtBlockStart : freeBeats;
    // step sequencers
    for (int s = 0; s < kNumSeqs; ++s)
    {
        const auto& x = I.seq[s];
        const int len = juce::jlimit (1, kSeqSteps, (int) base[x.len]);
        const int idx = (int) std::fmod (std::floor (beat / divisionBeats ((int) base[x.div])), (double) len);
        const float target = base[x.step[juce::jlimit (0, kSeqSteps - 1, idx)]];
        const float glide = base[x.glide];
        const float a = glide > 0.001f ? 1.f - std::exp (-(float) n / (glide * 0.25f * (float) sr)) : 1.f;
        seqValue[s] += (target - seqValue[s]) * a;
        g[SrcSeq1 + s] = seqValue[s];
    }
    // LFOs: free-running phase + global instances
    for (int l = 0; l < kNumLfos; ++l)
    {
        const auto& x = I.lfo[l];
        const bool sync = base[x.sync] > 0.5f;
        const double db = divisionBeats ((int) base[x.div]);
        ctx.lfoPhase[l] = sync ? beat / db : freeSeconds * (double) base[x.rate];
        ctx.lfoGlobal[l] = base[x.poly] < 0.5f;
        auto& L = globalLfo[l];
        L.shape = (int) base[x.shape]; L.mode = (int) base[x.mode]; L.smooth = base[x.smooth]; L.fadeIn = 0;
        L.phaseOffset = base[x.phase]; L.drawn = in.es->lfoDrawn[l];
        L.rateHz = sync ? (float) (in.tr.bpm / 60.0 / db) : base[x.rate];
        L.advance (n, L.mode == 0 ? ctx.lfoPhase[l] : -1.0);
        g[SrcLfo1 + l] = L.current();
    }
    // newest voice for voice-only sources used by global destinations
    const Voice* newest = nullptr;
    for (auto& v : voices) if (v.isActive() && (newest == nullptr || v.getAge() > newest->getAge())) newest = &v;
    for (int e = 0; e < kNumEnvs; ++e) g[SrcEnv1 + e] = newest ? newest->lastEnvValue (e) : 0.f;

    freeBeats += (double) n * in.tr.bpm / 60.0 / sr;
    freeSeconds += (double) n / sr;
}

void SynthEngine::buildCtx (BlockCtx& ctx, Inputs& in, int n)
{
    const auto& I = IX();
    const auto& t = ParamTable::get();
    float* base = in.base; float* norm = in.baseNorm;
    const auto& es = *in.es;
    updateGlobalSources (ctx, in, n);

    // global pass: matrix slots that target non-voice parameters (FX, master, ...)
    int16_t gd[kNumModSlots]; float ga[kNumModSlots]; int ng = 0;
    ctx.numSlots = 0; ctx.numDests = 0;
    for (int s = 0; s < kNumModSlots; ++s)
    {
        const auto& sl = es.slots[s];
        if (sl.src == SrcNone || sl.dest < 0 || sl.dest >= t.size()) continue;
        const float amt = base[I.modAmt[s]];
        if (std::abs (amt) < 1.0e-4f) continue;
        if (! isVoiceParam[(size_t) sl.dest])
        {
            float x = applyCurve (sl.curve, ctx.globalSrc[sl.src]);
            if (sl.aux != SrcNone) x *= ctx.globalSrc[sl.aux];
            int j = 0; while (j < ng && gd[j] != sl.dest) ++j;
            if (j == ng) { gd[ng] = sl.dest; ga[ng] = 0; ++ng; }
            ga[j] += x * amt;
            continue;
        }
        int j = 0; while (j < ctx.numDests && ctx.dests[j] != sl.dest) ++j;
        if (j == ctx.numDests) ctx.dests[ctx.numDests++] = sl.dest;
        const int k = ctx.numSlots++;
        ctx.slotSrc[k] = sl.src; ctx.slotAux[k] = sl.aux; ctx.slotCurve[k] = sl.curve;
        ctx.slotDestJ[k] = (int16_t) j; ctx.slotAmt[k] = amt;
    }
    for (int j = 0; j < ng; ++j)
    {
        const int d = gd[j];
        norm[d] = clamp01 (norm[d] + ga[j]);
        base[d] = t[d].range.convertFrom0to1 (norm[d]);
    }

    ctx.base = base; ctx.baseNorm = norm; ctx.es = in.es; ctx.bank = &wtBank;
    for (int o = 0; o < kNumOscs; ++o)
    {
        const MipTable* t = wtBank.tryAcquire ((int) base[I.osc[o].table]);
        if (t != nullptr) lastWt[o] = t;
        else if (lastWt[o] == nullptr) lastWt[o] = wtBank.acquire (0);
        ctx.wt[o] = lastWt[o];
        ctx.smp[o] = smpBank.get ((int) base[I.osc[o].smp]);
    }
    ctx.bpm = in.tr.bpm;
    ctx.riserPitch = in.riserPitch;
    ctx.riserCutoffOct = in.riserCutoffOct;
    ctx.eco = base[I.eco] > 0.5f;
    ctx.renderRate = (float) (sr * (double) (1 << osIndex));
}

void SynthEngine::process (juce::AudioBuffer<float>& out, juce::MidiBuffer& midi, int n, Inputs& in)
{
    const auto& I = IX();
    int wantOs = juce::jlimit (0, 3, (int) in.base[I.osamp]);
    if (in.base[I.eco] > 0.5f) wantOs = 0;
    if (wantOs != osIndex)
    {
        osIndex = wantOs;
        const double rate = sr * (double) (1 << osIndex);
        for (auto& v : voices) v.setRenderRate (rate);
        if (osIndex > 0) os[osIndex - 1]->reset();
    }
    BlockCtx ctx;
    buildCtx (ctx, in, n);

    const int factor = 1 << osIndex;
    out.clear (0, n);
    juce::dsp::AudioBlock<float> outBlock (out.getArrayOfWritePointers(), 2, (size_t) n);
    float* L; float* R;
    juce::dsp::AudioBlock<float> up;
    if (factor > 1)
    {
        up = os[osIndex - 1]->processSamplesUp (outBlock);
        up.clear();
        L = up.getChannelPointer (0); R = up.getChannelPointer (1);
    }
    else { L = out.getWritePointer (0); R = out.getWritePointer (1); }

    int pos = 0;
    for (const auto meta : midi)
    {
        const int ep = juce::jlimit (0, n, meta.samplePosition);
        if (ep > pos)
        {
            for (auto& v : voices) if (v.isActive()) v.render (L + pos * factor, R + pos * factor, (ep - pos) * factor, ctx);
            pos = ep;
        }
        handleMidi (meta.getMessage(), ctx);
    }
    if (pos < n)
        for (auto& v : voices) if (v.isActive()) v.render (L + pos * factor, R + pos * factor, (n - pos) * factor, ctx);

    if (factor > 1) os[osIndex - 1]->processSamplesDown (outBlock);

    // ---- visual feed
    const float* oL = out.getReadPointer (0); const float* oR = out.getReadPointer (1);
    float pk = 0, sum = 0;
    for (int i = 0; i < n; ++i)
    {
        const float m = 0.5f * (oL[i] + oR[i]);
        pk = juce::jmax (pk, std::abs (m)); sum += m * m;
        envFollow = juce::jmax (std::abs (m), envFollow * 0.9990f);
        slowFollow += (std::abs (m) - slowFollow) * 0.0005f;
        if ((i & 3) == 0) { vis.scope[(size_t) scopeW].store (m, std::memory_order_relaxed); scopeW = (scopeW + 1) % VisualFeed::kScopeSize; }
    }
    const float rms = std::sqrt (sum / (float) juce::jmax (1, n));
    if (envFollow > slowFollow * 2.5f + 0.02f && rms > vis.rms.load() * 1.6f + 0.01f) vis.transients.fetch_add (1);
    vis.rms = rms; vis.peak = pk;
    const Voice* newest = nullptr; int count = 0;
    for (auto& v : voices) if (v.isActive()) { ++count; if (newest == nullptr || v.getAge() > newest->getAge()) newest = &v; }
    vis.activeVoices = count;
    vis.env = newest ? newest->lastEnvValue (0) : 0.f;
    for (int o = 0; o < kNumOscs; ++o) vis.oscPhase[o] = newest ? newest->lastOscPhase (o) : 0.f;
    vis.pitch = lastNotePitch; vis.tempo = (float) in.tr.bpm;
    vis.lfoRate = in.base[I.lfo[0].rate];
    vis.riser = in.riser;
    for (int s = 0; s < kNumModSources; ++s) vis.modValue[s].store (ctx.globalSrc[s], std::memory_order_relaxed);
    if (newest) for (int e = 0; e < kNumEnvs; ++e) vis.modValue[SrcEnv1 + e].store (newest->lastEnvValue (e), std::memory_order_relaxed);
}

void SynthEngine::handleMidi (const juce::MidiMessage& m, const BlockCtx& ctx)
{
    const auto& I = IX();
    const bool mpe = ctx.base[I.mpe] > 0.5f;
    const int ch = m.getChannel();
    if (m.isNoteOn()) noteOn (m.getNoteNumber(), m.getFloatVelocity(), ch, ctx);
    else if (m.isNoteOff()) noteOff (m.getNoteNumber(), ch, ctx);
    else if (m.isPitchWheel())
    {
        const float v = (float) (m.getPitchWheelValue() - 8192) / 8192.f;
        if (mpe && ch > 1)
        {
            chanBend[ch] = v * 48.f;
            for (auto& vc : voices) if (vc.isActive() && vc.getChannel() == ch) vc.setMpeBend (chanBend[ch]);
        }
        else pitchBend = v;
    }
    else if (m.isChannelPressure())
    {
        const float v = (float) m.getChannelPressureValue() / 127.f;
        if (mpe && ch > 1) { chanPressure[ch] = v; for (auto& vc : voices) if (vc.isActive() && vc.getChannel() == ch) vc.setMpePressure (v); }
        else aftertouch = v;
    }
    else if (m.isAftertouch())
    {
        for (auto& vc : voices) if (vc.isActive() && vc.getNote() == m.getNoteNumber()) vc.setPolyAftertouch ((float) m.getAfterTouchValue() / 127.f);
    }
    else if (m.isController())
    {
        const int cc = m.getControllerNumber(); const float v = (float) m.getControllerValue() / 127.f;
        if (cc == 1) modWheelVal = v;
        else if (cc == 11) expression = v;
        else if (cc == 74 && mpe) { chanTimbre[ch] = v; for (auto& vc : voices) if (vc.isActive() && vc.getChannel() == ch) vc.setMpeTimbre (v); }
        else if (cc == 64)
        {
            const bool now = v >= 0.5f;
            if (sustain && ! now)
                for (int k = 0; k < 128; ++k) if (sustained[k]) { sustained[k] = false; sustain = false; noteOff (k, 0, ctx); }
            sustain = now;
        }
        else if (cc == 120 || cc == 123) allNotesOff();
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff()) allNotesOff();
}

void SynthEngine::noteOn (int note, float vel, int ch, const BlockCtx& ctx)
{
    const auto& I = IX();
    const int poly = juce::jlimit (1, kMaxVoices, (int) ctx.base[I.poly]);
    const float glide = ctx.base[I.glide];
    const float glideFrom = (glide > 0.0005f && heldCount > 0) || (glide > 0.0005f && poly == 1) ? lastNotePitch : -1.f;
    const uint32_t seed = (seedCounter++) * 2654435761u;
    sustained[note & 127] = false;
    if (heldCount == 0)
        for (int l = 0; l < kNumLfos; ++l) if (ctx.lfoGlobal[l] && globalLfo[l].mode != 0) globalLfo[l].noteOn (seed + (uint32_t) l);

    if (poly == 1)
    {
        if (monoCount < 32) monoStack[monoCount++] = note;
        auto& v = voices[0];
        const bool legato = ctx.base[I.legato] > 0.5f;
        if (legato && v.isActive() && ! v.isReleasing()) v.setTargetNote (note);
        else { v.start (note, vel, ch, false, glideFrom, ctx, seed); v.setAge (++ageCounter); }
        if (ctx.base[I.mpe] > 0.5f) v.setMpeBend (chanBend[juce::jlimit (0, 16, ch)]);
    }
    else
    {
        Voice* target = nullptr;
        for (int i = 0; i < poly; ++i) if (! voices[i].isActive()) { target = &voices[i]; break; }
        if (target != nullptr)
        {
            target->start (note, vel, ch, false, glideFrom, ctx, seed);
        }
        else
        {
            Voice* victim = nullptr;
            for (int i = 0; i < poly; ++i)
                if (voices[i].isReleasing() && (victim == nullptr || voices[i].getAge() < victim->getAge())) victim = &voices[i];
            if (victim == nullptr)
                for (int i = 0; i < poly; ++i) if (victim == nullptr || voices[i].getAge() < victim->getAge()) victim = &voices[i];
            victim->steal (note, vel, ch, seed);
            target = victim;
        }
        target->setAge (++ageCounter);
    }
    lastNotePitch = (float) note; lastVel = vel; gateOn = true; ++heldCount;
    vis.noteOnCount.fetch_add (1);
}

void SynthEngine::noteOff (int note, int ch, const BlockCtx& ctx)
{
    const auto& I = IX();
    if (sustain) { sustained[note & 127] = true; return; }
    heldCount = juce::jmax (0, heldCount - 1);
    gateOn = heldCount > 0;
    const int poly = juce::jlimit (1, kMaxVoices, (int) ctx.base[I.poly]);
    const bool mpe = ctx.base[I.mpe] > 0.5f;
    if (poly == 1)
    {
        for (int i = 0; i < monoCount; ++i) if (monoStack[i] == note) { for (int j = i; j < monoCount - 1; ++j) monoStack[j] = monoStack[j + 1]; --monoCount; break; }
        auto& v = voices[0];
        if (v.getNote() == note)
        {
            if (monoCount > 0)
            {
                const int prev = monoStack[monoCount - 1];
                if (ctx.base[I.legato] > 0.5f) v.setTargetNote (prev);
                else v.start (prev, lastVel, ch, false, ctx.base[I.glide] > 0.0005f ? (float) note : -1.f, ctx, (seedCounter++) * 2654435761u);
                lastNotePitch = (float) prev;
            }
            else v.release();
        }
        for (int i = 1; i < kMaxVoices; ++i) if (voices[i].isActive() && voices[i].getNote() == note) voices[i].release();
        return;
    }
    for (auto& v : voices)
        if (v.isActive() && v.getNote() == note && ! v.isReleasing() && (! mpe || ch == 0 || v.getChannel() == ch))
            v.release();
}
} // namespace pd
