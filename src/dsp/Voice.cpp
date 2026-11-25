#include "Voice.h"

namespace pd
{
namespace
{
inline float warpPhase (int mode, float p, float a) noexcept
{
    switch (mode)
    {
        case WarpBendPlus:  { const float b = a * 8.f; return p * (1.f + b) / (1.f + b * p); }
        case WarpBendMinus: { const float b = a * 8.f; return p / (1.f + b * (1.f - p)); }
        case WarpSync:      { const float q = p * (1.f + 7.f * a); return q - std::floor (q); }
        case WarpPWM:       { const float w = 0.5f - 0.49f * a; return p < w ? p * 0.5f / w : 0.5f + (p - w) * 0.5f / (1.f - w); }
        case WarpMirror:    { const float m = p < 0.5f ? p : 1.f - p; return p + (m - p) * a; }
        case WarpQuantize:  { const float s = 4.f + (1.f - a) * (1.f - a) * 252.f; return std::floor (p * s) / s; }
        default: return p;
    }
}
inline void panGains (float pan, float& l, float& r) noexcept
{
    const float x = (clampf (pan, -1.f, 1.f) + 1.f) * (kPi * 0.25f);
    l = std::cos (x) * 1.41421356f; r = std::sin (x) * 1.41421356f;
}
const auto& T() { return ParamTable::get(); }
} // namespace

Voice::Voice()
{
    val.assign ((size_t) T().size(), 0.f);
}

void Voice::prepare (double maxRenderRate, int)
{
    for (auto& f : filt) for (auto& c : f) c.prepare (maxRenderRate, (int) (maxRenderRate / 15.0) + 8);
    setRenderRate (maxRenderRate);
}

void Voice::setRenderRate (double rate)
{
    sr = (float) rate;
    for (auto& f : filt) for (auto& c : f) c.setSampleRate (rate);
    for (auto& e : env) e.setSampleRate (sr);
    for (auto& l : lfo) l.setSampleRate (sr);
    noise.setSampleRate (sr);
}

void Voice::start (int n, float vel, int ch, bool legato, float glideFrom, const BlockCtx& ctx, uint32_t seed)
{
    std::copy (ctx.base, ctx.base + T().size(), val.begin());
    const auto& I = IX();
    const bool wasActive = active;
    rng.s = seed | 1u;
    note = n; channel = ch; velocity = vel;
    targetPitch = (float) n;
    const float glide = v (I.glide);
    if (glide > 0.0005f && glideFrom >= 0.f) currentPitch = glideFrom;
    else if (! (legato && wasActive)) currentPitch = (float) n;
    glideTimeStored = glide;
    randomVal = rng.bi();
    mpeBend = 0; mpePressure = 0; mpeTimbre = 0; polyAT = 0;
    stealCountdown = 0; pendingNote = -1;

    for (int e = 0; e < kNumEnvs; ++e)
    {
        auto& E = env[e]; const auto& x = I.env[e];
        E.a = v (x.a); E.h = v (x.h); E.d = v (x.d); E.s = v (x.s); E.r = v (x.r); E.curve = v (x.curve);
        E.msegMode = v (x.mode) > 0.5f; E.mseg = &ctx.es->mseg[e];
        E.noteOn (legato && wasActive);
    }
    if (! (legato && wasActive))
    {
        for (int l = 0; l < kNumLfos; ++l)
        {
            lfo[l].phaseOffset = v (I.lfo[l].phase);
            lfo[l].noteOn (seed * 2654435761u + (uint32_t) l * 97u);
        }
        for (int o = 0; o < kNumOscs; ++o) startOsc (o, ctx);
        subPhase = 0;
        noise.seed (seed ^ 0xABCDEFu);
        if (! wasActive) for (auto& f : filt) for (auto& c : f) c.reset();
        voicePan = rng.bi() * v (I.voiceSpread);
        ampPrev = ampTarget = 0;
    }
    active = true;
}

void Voice::startOsc (int o, const BlockCtx& ctx) noexcept
{
    const auto& x = IX().osc[o];
    auto& S = osc[o];
    const float ph = v (x.phase), rp = v (x.rphase);
    for (int u = 0; u < kMaxUnison; ++u)
    {
        S.phase[u] = ph + rp * rng.uni();
        S.phase[u] -= std::floor (S.phase[u]);
        S.drift[u] = rng.bi(); S.driftTarget[u] = rng.bi();
    }
    S.finished = false; S.last = 0;
    for (auto& g : S.grains) g.on = false;
    S.grainAcc = 1.f;
    for (int k = 0; k < kSpecPartials; ++k) { S.partialPhase[k] = rng.uni(); S.partialAmp[k] = 0; }
    const Sample* smp = ctx.smp[o];
    const int eng = (int) v (x.eng);
    if (smp != nullptr && smp->length() > 4)
    {
        const int len = smp->length();
        int s0 = 0, s1 = len;
        if ((int) v (x.slmode) == 2 && eng == EngSample)
        {
            const int ns = juce::jmax (1, smp->numSlices);
            int si = ((int) v (x.slice) + note - 60) % ns; if (si < 0) si += ns;
            S.sliceIdx = si; s0 = smp->sliceStart[si]; s1 = smp->sliceEnd (si);
        }
        S.sStart = s0; S.sEnd = s1;
        const double startPos = s0 + (double) v (x.pos) * (double) (s1 - s0 - 1);
        for (int u = 0; u < kMaxUnison; ++u) S.play[u] = startPos;
        S.grainHead = v (x.pos) * (double) (len - 1);
        S.specHead = 0;
    }
}

void Voice::release() noexcept
{
    for (auto& e : env) e.noteOff();
}

void Voice::kill() noexcept
{
    for (auto& e : env) e.kill();
    active = false; stealCountdown = 0; pendingNote = -1;
}

void Voice::steal (int n, float vel, int ch, uint32_t seed) noexcept
{
    pendingNote = n; pendingVel = vel; pendingChannel = ch; pendingSeed = seed;
    note = n; channel = ch;
    stealCountdown = (int) (sr * 0.003f) + 1;
}

void Voice::computeMods (const BlockCtx& ctx) noexcept
{
    for (int i = 0; i < kNumModSources; ++i) src[i] = ctx.globalSrc[i];
    for (int l = 0; l < kNumLfos; ++l) if (! ctx.lfoGlobal[l]) src[SrcLfo1 + l] = lfo[l].current();
    for (int e = 0; e < kNumEnvs; ++e) src[SrcEnv1 + e] = env[e].current();
    src[SrcVelocity] = velocity;
    src[SrcNote] = ((float) note - 60.f) / 60.f;
    src[SrcRandom] = randomVal;
    src[SrcMpePressure] = mpePressure;
    src[SrcMpeTimbre] = mpeTimbre;
    src[SrcAftertouch] = juce::jmax (polyAT, ctx.globalSrc[SrcAftertouch]);
    if (ctx.numSlots == 0) return;
    float acc[kNumModSlots] {};
    for (int s = 0; s < ctx.numSlots; ++s)
    {
        float x = applyCurve (ctx.slotCurve[s], src[ctx.slotSrc[s]]);
        if (ctx.slotAux[s] != SrcNone) x *= src[ctx.slotAux[s]];
        acc[ctx.slotDestJ[s]] += x * ctx.slotAmt[s];
    }
    const auto& t = T();
    for (int j = 0; j < ctx.numDests; ++j)
    {
        const int d = ctx.dests[j];
        val[(size_t) d] = t[d].range.convertFrom0to1 (clamp01 (ctx.baseNorm[d] + acc[j]));
    }
}

void Voice::tick (const BlockCtx& ctx) noexcept
{
    const auto& I = IX();
    computeMods (ctx);
    const float tickDur = (float) kControlRate / sr;

    // glide
    if (glideTimeStored > 0.0005f)
        currentPitch += (targetPitch - currentPitch) * (1.f - std::exp (-tickDur / (glideTimeStored * 0.35f)));
    else currentPitch = targetPitch;

    // envelopes
    for (int e = 0; e < kNumEnvs; ++e)
    {
        auto& E = env[e]; const auto& x = I.env[e];
        E.a = v (x.a); E.h = v (x.h); E.d = v (x.d); E.s = v (x.s); E.r = v (x.r); E.curve = v (x.curve);
        E.advance (kControlRate);
    }
    ampPrev = ampTarget;
    ampTarget = env[0].current();

    // LFOs
    for (int l = 0; l < kNumLfos; ++l)
    {
        if (ctx.lfoGlobal[l]) continue;
        auto& L = lfo[l]; const auto& x = I.lfo[l];
        L.shape = (int) v (x.shape); L.mode = (int) v (x.mode); L.smooth = v (x.smooth); L.fadeIn = v (x.delay);
        L.drawn = ctx.es->lfoDrawn[l];
        L.rateHz = v (x.sync) > 0.5f ? (float) (juce::jmax (20.0, ctx.bpm) / 60.0 / divisionBeats ((int) v (x.div))) : v (x.rate);
        L.advance (kControlRate, L.mode == 0 ? ctx.lfoPhase[l] : -1.0);
    }

    const float bend = ctx.globalSrc[SrcPitchBend] * v (I.bend) + mpeBend + ctx.riserPitch;
    const float basePitch = currentPitch + bend;

    // oscillators
    for (int o = 0; o < kNumOscs; ++o)
    {
        const auto& x = I.osc[o];
        auto& S = osc[o];
        if (v (x.on) < 0.5f) continue;
        const int eng = (int) v (x.eng);
        const float pitch = basePitch + v (x.oct) * 12.f + v (x.semi) + v (x.fine) * 0.01f + v (x.pmod);
        const float freq = midiToHz (pitch);
        int U = (int) v (x.uni);
        int vaShape = (int) v (x.va);
        if (eng == EngAnalog && vaShape == 3) U = juce::jmax (U, 7);
        if (eng == EngGranular || eng == EngSpectral) U = 1;
        if (ctx.eco) U = juce::jmin (U, 4);
        U = juce::jlimit (1, kMaxUnison, U);
        S.uni = U;
        const float det = (eng == EngAnalog && vaShape == 3) ? juce::jmax (v (x.det), v (x.pw) * 0.6f) : v (x.det);
        const float blend = v (x.blend), width = v (x.width), drift = v (x.drift) + (eng == EngAnalog ? 0.15f : 0.f);
        float norm = 0;
        for (int u = 0; u < U; ++u)
        {
            const float spread = U > 1 ? (2.f * (float) u / (float) (U - 1) - 1.f) : 0.f;
            // slow random drift per unison voice
            S.drift[u] += (S.driftTarget[u] - S.drift[u]) * 0.004f;
            if (rng.uni() < 0.002f) S.driftTarget[u] = rng.bi();
            const float semis = spread * det * 0.5f + S.drift[u] * drift * 0.12f;
            S.inc[u] = freq * semitonesToRatio (semis) / sr;
            const bool inner = U == 1 || std::abs (spread) <= 1.f / (float) (U - 1) + 0.001f;
            const float g = inner ? 1.f : blend;
            panGains (spread * width, S.gainL[u], S.gainR[u]);
            S.gainL[u] *= g; S.gainR[u] *= g;
            norm += g * g;
        }
        norm = 1.f / std::sqrt (juce::jmax (1.0e-4f, norm));
        for (int u = 0; u < U; ++u) { S.gainL[u] *= norm; S.gainR[u] *= norm; }

        S.warp = (int) v (x.warp); S.warpAmt = v (x.warpAmt);
        S.framePos = v (x.pos);
        float effInc = S.inc[0];
        if (S.warp == WarpSync) effInc *= 1.f + 7.f * S.warpAmt;
        const MipTable* tbl = eng == EngAnalog ? &ctx.bank->saw() : ctx.wt[o];
        S.level = tbl ? tbl->levelFor (effInc * (1.f + det * 0.05f)) : 0;

        float pl, pr; panGains (v (x.pan), pl, pr);
        const float lvl = v (x.lvl);
        S.prevLvlL = S.lvlL; S.prevLvlR = S.lvlR;
        S.lvlL = lvl * pl; S.lvlR = lvl * pr;

        const Sample* smp = ctx.smp[o];
        if (smp != nullptr && smp->length() > 4)
        {
            const double rateRatio = smp->sampleRate / (double) sr;
            if (eng == EngSample)
            {
                const float ref = (int) v (x.slmode) == 2 ? 60.f : smp->rootNote;
                S.sampleRatio = std::exp2 ((pitch - ref) / 12.0) * rateRatio;
            }
            else if (eng == EngGranular)
            {
                S.grainInc = (float) (std::exp2 ((pitch - smp->rootNote) / 12.0) * rateRatio);
                S.grainLen = juce::jmax (16, (int) (v (x.gsize) * 0.001f * sr));
                S.grainSpray = v (x.gspray);
                S.grainDensity = v (x.gdens);
                S.freeze = v (x.gfreeze) > 0.5f;
                if (S.freeze) S.grainHead = S.framePos * (double) (smp->length() - 1);
            }
            else if (eng == EngSpectral)
            {
                const bool freeze = v (x.gfreeze) > 0.5f;
                if (! freeze) S.specHead += (double) kControlRate * rateRatio / (double) smp->length();
                double posN = (double) S.framePos + (freeze ? 0.0 : S.specHead);
                if (smp->loopable) posN -= std::floor (posN); else posN = juce::jmin (1.0, posN);
                const float fr = (float) posN * (kSpecFrames - 1);
                const int f0i = juce::jmin (kSpecFrames - 2, (int) fr); const float ft = fr - (float) f0i;
                const float formantRatio = semitonesToRatio (v (x.formant));
                const float tilt = v (x.tilt);
                const float f0src = juce::jmax (20.f, smp->rootHz);
                const int maxP = ctx.eco ? 24 : kSpecPartials;
                int np = 0;
                for (int k = 1; k <= maxP; ++k)
                {
                    const float fk = freq * (float) k;
                    if (fk >= sr * 0.45f) break;
                    const float si = fk / (formantRatio * f0src) - 1.f;
                    float a = 0;
                    if (si >= 0 && si < (float) (kSpecPartials - 1))
                    {
                        const int i0 = (int) si; const float t = si - (float) i0;
                        const float a0 = lerp (smp->spec[f0i][i0], smp->spec[f0i][i0 + 1], t);
                        const float a1 = lerp (smp->spec[f0i + 1][i0], smp->spec[f0i + 1][i0 + 1], t);
                        a = lerp (a0, a1, ft);
                    }
                    else if (si < 0) a = smp->spec[f0i][0] * clamp01 (1.f + si);
                    if (tilt != 0.f) a *= std::pow ((float) k, tilt);
                    S.partialAmp[k - 1] += (a - S.partialAmp[k - 1]) * 0.5f;
                    S.partialInc[k - 1] = fk / sr;
                    np = k;
                }
                S.numPartials = np;
            }
        }
    }

    // sub
    subInc = midiToHz (basePitch + v (I.subOct) * 12.f) / sr;
    noise.setColour (v (I.nzColor));

    // filters
    const float keyOffset = (currentPitch - 60.f) / 12.f;
    for (int f = 0; f < kNumFilters; ++f)
    {
        const auto& x = I.filt[f];
        if (v (x.on) < 0.5f) continue;
        const float oct = keyOffset * v (x.key) + env[1].current() * v (x.env) * 7.f + ctx.riserCutoffOct;
        const float cut = v (x.cut) * std::exp2 (oct);
        for (auto& c : filt[f]) c.set ((int) v (x.type), cut, v (x.res), v (x.morph), v (x.pre), v (x.post));
    }
}

float Voice::renderOsc (int o, const BlockCtx& ctx, float& outR) noexcept
{
    auto& S = osc[o];
    const auto& x = IX().osc[o];
    const int eng = (int) v (x.eng);
    float L = 0, R = 0;
    switch (eng)
    {
        case EngWavetable:
        {
            const MipTable* tbl = ctx.wt[o];
            if (tbl == nullptr) break;
            const float fm = S.warp == WarpFM ? S.warpAmt * 1.5f * osc[1].last : 0.f;
            float first = 0;
            for (int u = 0; u < S.uni; ++u)
            {
                float p = warpPhase (S.warp, S.phase[u], S.warpAmt) + fm;
                p -= std::floor (p);
                const float s = tbl->sample (S.level, S.framePos, p);
                if (u == 0) first = s;
                L += s * S.gainL[u]; R += s * S.gainR[u];
                S.phase[u] += S.inc[u]; if (S.phase[u] >= 1.f) S.phase[u] -= 1.f;
            }
            if (S.warp == WarpRM) { const float rm = lerp (1.f, osc[2].last, S.warpAmt); L *= rm; R *= rm; }
            S.last = first;
        } break;
        case EngAnalog:
        {
            const int shape = (int) v (x.va);
            const auto& saw = ctx.bank->saw();
            const auto& tri = ctx.bank->tri();
            const float pw = v (x.pw);
            float first = 0;
            for (int u = 0; u < S.uni; ++u)
            {
                const float p = S.phase[u];
                float s;
                if (shape == 1) { float p2 = p + pw; p2 -= std::floor (p2); s = (saw.sample (S.level, 0, p) - saw.sample (S.level, 0, p2)) * 0.5f; }
                else if (shape == 2) s = tri.sample (juce::jmin (S.level, kWtLevels - 1), 0, p);
                else s = saw.sample (S.level, 0, p);
                if (u == 0) first = s;
                L += s * S.gainL[u]; R += s * S.gainR[u];
                S.phase[u] += S.inc[u]; if (S.phase[u] >= 1.f) S.phase[u] -= 1.f;
            }
            S.last = first;
        } break;
        case EngSample:
        {
            const Sample* smp = ctx.smp[o];
            if (smp == nullptr || S.finished) break;
            const bool loop = (int) v (x.slmode) == 1;
            for (int u = 0; u < S.uni; ++u)
            {
                double& p = S.play[u];
                if (p >= S.sEnd - 2) { if (loop) p = S.sStart + (p - (S.sEnd - 2)); else { if (u == 0) S.finished = true; continue; } }
                const float l = smp->read (0, p), r = smp->stereo() ? smp->read (1, p) : l;
                L += l * S.gainL[u]; R += r * S.gainR[u];
                p += S.sampleRatio * (double) (S.inc[u] / juce::jmax (1.0e-9f, S.inc[0]));
            }
            S.last = (L + R) * 0.5f;
        } break;
        case EngGranular:
        {
            const Sample* smp = ctx.smp[o];
            if (smp == nullptr) break;
            const int len = smp->length();
            S.grainAcc += S.grainDensity / sr;
            if (S.grainAcc >= 1.f)
            {
                S.grainAcc -= 1.f;
                for (auto& g : S.grains)
                    if (! g.on)
                    {
                        g.on = true; g.age = 0; g.len = S.grainLen;
                        g.pos = juce::jlimit (0.0, (double) (len - 2), S.grainHead + (double) (rng.bi() * S.grainSpray * 0.15f * (float) len));
                        g.inc = S.grainInc * (1.0 + rng.bi() * S.grainSpray * 0.01);
                        panGains (rng.bi() * v (x.width), g.panL, g.panR);
                        break;
                    }
            }
            const float overlap = juce::jmax (1.f, S.grainDensity * (float) S.grainLen / sr);
            const float gnorm = 1.f / std::sqrt (overlap);
            const auto& sine = SineTable::get();
            for (auto& g : S.grains)
            {
                if (! g.on) continue;
                const float w = sine (0.5f * (float) g.age / (float) g.len);
                const float win = w * w;
                const float l = smp->read (0, g.pos), r = smp->stereo() ? smp->read (1, g.pos) : l;
                L += l * win * g.panL * gnorm; R += r * win * g.panR * gnorm;
                g.pos += g.inc;
                if (++g.age >= g.len || g.pos >= len - 2) g.on = false;
            }
            if (! S.freeze)
            {
                S.grainHead += smp->sampleRate / (double) sr;
                if (S.grainHead >= len - 1) S.grainHead = 0;
            }
            S.last = (L + R) * 0.5f;
        } break;
        case EngSpectral:
        {
            const auto& sine = SineTable::get();
            float y = 0;
            for (int k = 0; k < S.numPartials; ++k)
            {
                y += S.partialAmp[k] * sine (S.partialPhase[k]);
                S.partialPhase[k] += S.partialInc[k];
                if (S.partialPhase[k] >= 1.f) S.partialPhase[k] -= 1.f;
            }
            y *= 0.9f;
            L = R = y; S.last = y;
        } break;
        default: break;
    }
    outR = R;
    return L;
}

void Voice::render (float* outL, float* outR, int n, const BlockCtx& ctx) noexcept
{
    if (! active) return;
    const auto& I = IX();
    std::copy (ctx.base, ctx.base + T().size(), val.begin());
    const int routing = (int) v (I.filtRouting);
    const bool f1on = v (I.filt[0].on) > 0.5f, f2on = v (I.filt[1].on) > 0.5f;
    const float velGain = lerp (1.f, velocity, v (I.velo));
    float vpL, vpR; panGains (voicePan, vpL, vpR);

    int done = 0;
    while (done < n)
    {
        if (needsStart)
        {
            needsStart = false;
            start (pendingNote, pendingVel, pendingChannel, false, -1.f, ctx, pendingSeed);
            pendingNote = -1;
        }
        tick (ctx);
        const int len = juce::jmin (kControlRate, n - done);
        const float f1mix = v (I.filt[0].mix), f2mix = v (I.filt[1].mix);
        const bool oscOn[3] = { v (I.osc[0].on) > 0.5f, v (I.osc[1].on) > 0.5f, v (I.osc[2].on) > 0.5f };
        const int oscRoute[3] = { (int) v (I.osc[0].route), (int) v (I.osc[1].route), (int) v (I.osc[2].route) };
        const bool subOn = v (I.subOn) > 0.5f, nzOn = v (I.nzOn) > 0.5f;
        const int subShape = (int) v (I.subShape), subRoute = (int) v (I.subRoute);
        const int nzType = (int) v (I.nzType), nzRoute = (int) v (I.nzRoute);
        const float subLvl = v (I.subLvl), nzLvl = v (I.nzLvl);
        const auto& sine = SineTable::get();

        for (int i = 0; i < len; ++i)
        {
            const float ramp = (float) (i + 1) / (float) kControlRate;
            float bus[4][2] {};   // F1, F2, Both, Direct
            for (int o = 0; o < kNumOscs; ++o)
            {
                if (! oscOn[o]) continue;
                auto& S = osc[o];
                float r; const float l = renderOsc (o, ctx, r);
                const float gl = lerp (S.prevLvlL, S.lvlL, ramp), gr = lerp (S.prevLvlR, S.lvlR, ramp);
                bus[oscRoute[o]][0] += l * gl; bus[oscRoute[o]][1] += r * gr;
            }
            if (subOn)
            {
                float s;
                if (subShape == 0) s = sine (subPhase);
                else if (subShape == 1) s = 1.f - 4.f * std::abs (subPhase - 0.5f);
                else s = (ctx.bank->saw().sample (ctx.bank->saw().levelFor (subInc), 0, subPhase)
                          - ctx.bank->saw().sample (ctx.bank->saw().levelFor (subInc), 0, subPhase + 0.5f >= 1.f ? subPhase - 0.5f : subPhase + 0.5f)) * 0.5f;
                subPhase += subInc; if (subPhase >= 1.f) subPhase -= 1.f;
                bus[subRoute][0] += s * subLvl; bus[subRoute][1] += s * subLvl;
            }
            if (nzOn)
            {
                float nl, nr; noise.process (nzType, nl, nr);
                bus[nzRoute][0] += nl * nzLvl; bus[nzRoute][1] += nr * nzLvl;
            }

            float oL, oR;
            if (routing == 2)   // split: F1 left, F2 right
            {
                const float inL = bus[0][0] + bus[1][0] + bus[2][0];
                const float inR = bus[0][1] + bus[1][1] + bus[2][1];
                oL = f1on ? lerp (inL, filt[0][0].process (inL), f1mix) : inL;
                oR = f2on ? lerp (inR, filt[1][1].process (inR), f2mix) : inR;
            }
            else if (routing == 1)   // parallel
            {
                const float k = 0.70710678f;
                const float aL = bus[0][0] + bus[2][0] * k, aR = bus[0][1] + bus[2][1] * k;
                const float bL = bus[1][0] + bus[2][0] * k, bR = bus[1][1] + bus[2][1] * k;
                oL = (f1on ? lerp (aL, filt[0][0].process (aL), f1mix) : aL) + (f2on ? lerp (bL, filt[1][0].process (bL), f2mix) : bL);
                oR = (f1on ? lerp (aR, filt[0][1].process (aR), f1mix) : aR) + (f2on ? lerp (bR, filt[1][1].process (bR), f2mix) : bR);
            }
            else   // serial
            {
                float aL = bus[0][0] + bus[2][0], aR = bus[0][1] + bus[2][1];
                if (f1on) { aL = lerp (aL, filt[0][0].process (aL), f1mix); aR = lerp (aR, filt[0][1].process (aR), f1mix); }
                aL += bus[1][0]; aR += bus[1][1];
                if (f2on) { aL = lerp (aL, filt[1][0].process (aL), f2mix); aR = lerp (aR, filt[1][1].process (aR), f2mix); }
                oL = aL; oR = aR;
            }
            oL += bus[3][0]; oR += bus[3][1];

            float amp = lerp (ampPrev, ampTarget, ramp) * velGain;
            if (stealCountdown > 0)
            {
                amp *= (float) stealCountdown / (sr * 0.003f + 1.f);
                if (--stealCountdown == 0) needsStart = pendingNote >= 0;
            }
            outL[done + i] += oL * amp * vpL;
            outR[done + i] += oR * amp * vpR;
        }
        done += len;
        if (! env[0].active() && ! needsStart && stealCountdown == 0) { active = false; return; }
        if (needsStart && done < n) continue;
    }
}
} // namespace pd
