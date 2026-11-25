#include "PatchGraph.h"

namespace pd
{
static const PatchModuleInfo kInfo[kNumPatchModuleTypes] = {
    { "VCO", "Sound", 3, 2, 3, { "Pitch", "FM", "Sync" }, { "Out", "Square" }, { "Tune", "Shape", "FM Amt" }, { 0.5f, 0.6f, 0.f }, nullptr },
    { "VCA", "Sound", 2, 1, 2, { "In", "CV" }, { "Out" }, { "Gain", "Bias" }, { 1.f, 0.f }, nullptr },
    { "LFO", "Modulation", 2, 3, 2, { "Rate", "Reset" }, { "Bipolar", "Unipolar", "Gate" }, { "Rate", "Shape" }, { 0.4f, 0.f }, "Free|1/16|1/8|1/4|1 Bar" },
    { "ADSR", "Modulation", 1, 2, 4, { "Gate" }, { "Env", "Inv" }, { "Attack", "Decay", "Sustain", "Release" }, { 0.05f, 0.3f, 0.6f, 0.3f }, nullptr },
    { "S&H", "Modulation", 2, 1, 0, { "In", "Trig" }, { "Out" }, {}, {}, nullptr },
    { "Quantizer", "Pitch", 1, 2, 1, { "In" }, { "Out", "Trig" }, { "Root" }, { 0.f }, "Chromatic|Major|Minor|Dorian|Pent Minor|Phrygian" },
    { "Slew", "Modulation", 1, 1, 2, { "In" }, { "Out" }, { "Rise", "Fall" }, { 0.2f, 0.2f }, nullptr },
    { "Clock Div/Mult", "Clock", 2, 2, 2, { "Clock", "Reset" }, { "Div", "Mult" }, { "Divide", "Multiply" }, { 0.2f, 0.2f }, nullptr },
    { "Euclidean", "Clock", 2, 2, 3, { "Clock", "Reset" }, { "Gate", "Accent" }, { "Steps", "Pulses", "Rotate" }, { 1.f, 0.35f, 0.f }, nullptr },
    { "Logic", "Clock", 2, 2, 0, { "A", "B" }, { "Out", "Not" }, {}, {}, "AND|OR|XOR|NAND" },
    { "Turing Machine", "Modulation", 1, 2, 3, { "Clock" }, { "CV", "Gate" }, { "Change", "Length", "Range" }, { 0.2f, 0.45f, 0.5f }, nullptr },
    { "Mixer", "Utility", 4, 1, 4, { "1", "2", "3", "4" }, { "Out" }, { "Lvl 1", "Lvl 2", "Lvl 3", "Lvl 4" }, { 0.8f, 0.8f, 0.8f, 0.8f }, nullptr },
    { "Attenuverter", "Utility", 1, 1, 2, { "In" }, { "Out" }, { "Amount", "Offset" }, { 0.75f, 0.5f }, nullptr },
    { "Delay Line", "Sound", 2, 1, 3, { "In", "Time" }, { "Out" }, { "Time", "Feedback", "Mix" }, { 0.3f, 0.4f, 0.5f }, nullptr },
    { "Wavefolder", "Sound", 2, 1, 2, { "In", "Fold" }, { "Out" }, { "Fold", "Symmetry" }, { 0.3f, 0.5f }, nullptr },
    { "Bitcrusher", "Sound", 1, 1, 2, { "In" }, { "Out" }, { "Bits", "Rate" }, { 0.6f, 0.f }, nullptr },
    { "Env Follower", "Modulation", 1, 2, 3, { "In" }, { "Env", "Gate" }, { "Attack", "Release", "Gain" }, { 0.1f, 0.3f, 0.5f }, nullptr },
    { "MIDI to CV", "Input", 0, 4, 0, {}, { "Pitch", "Gate", "Velocity", "Mod Wheel" }, {}, {}, nullptr },
    { "Macro In", "Input", 0, 1, 1, {}, { "Value" }, { "Macro" }, { 0.f }, nullptr },
    { "Param Out", "Synth", 1, 0, 1, { "CV" }, {}, { "Depth" }, { 0.5f }, nullptr },
    { "Param In", "Synth", 0, 1, 0, {}, { "Value" }, {}, {}, nullptr },
    { "Patch Out", "Synth", 1, 0, 0, { "In" }, {}, {}, {}, "1|2|3|4" },
    { "Audio Out", "Synth", 2, 0, 1, { "L", "R" }, {}, { "Level" }, { 0.7f }, nullptr },
    { "Synth In", "Synth", 0, 3, 0, {}, { "L", "R", "Level" }, {}, {}, nullptr },
    { "Noise", "Sound", 0, 2, 0, {}, { "White", "Brown" }, {}, {}, nullptr },
};

const PatchModuleInfo& patchModuleInfo (int t) { return kInfo[juce::jlimit (0, kNumPatchModuleTypes - 1, t)]; }

// ------------------------------------------------------------ serialisation
juce::ValueTree patchToTree (const PatchProgram& p)
{
    juce::ValueTree t ("PATCH");
    for (int i = 0; i < kMaxPatchModules; ++i)
    {
        const auto& d = p.m[i];
        if (d.type < 0) continue;
        juce::ValueTree m ("MODULE");
        m.setProperty ("i", i, nullptr);
        m.setProperty ("type", patchModuleInfo (d.type).name, nullptr);
        m.setProperty ("opt", (int) d.option, nullptr);
        if (d.paramDest >= 0) m.setProperty ("param", ParamTable::get()[d.paramDest].id, nullptr);
        juce::StringArray k, c;
        for (int j = 0; j < kPatchIO; ++j)
        {
            k.add (juce::String (d.knob[j], 4));
            c.add (juce::String ((int) d.inMod[j]) + ":" + juce::String ((int) d.inPort[j]));
        }
        m.setProperty ("knobs", k.joinIntoString (","), nullptr);
        m.setProperty ("in", c.joinIntoString (","), nullptr);
        m.setProperty ("x", d.x, nullptr);
        m.setProperty ("y", d.y, nullptr);
        t.appendChild (m, nullptr);
    }
    return t;
}

void patchFromTree (PatchProgram& p, const juce::ValueTree& t)
{
    p = PatchProgram();
    for (auto m : t)
    {
        const int i = m.getProperty ("i", -1);
        if (i < 0 || i >= kMaxPatchModules) continue;
        auto& d = p.m[i];
        const auto typeName = m.getProperty ("type").toString();
        for (int k = 0; k < kNumPatchModuleTypes; ++k) if (typeName == kInfo[k].name) d.type = (int8_t) k;
        if (d.type < 0) continue;
        d.option = (int8_t) (int) m.getProperty ("opt", 0);
        d.paramDest = (int16_t) (m.hasProperty ("param") ? ParamTable::get().indexOf (m.getProperty ("param").toString()) : -1);
        juce::StringArray k, c; k.addTokens (m.getProperty ("knobs").toString(), ",", {}); c.addTokens (m.getProperty ("in").toString(), ",", {});
        for (int j = 0; j < kPatchIO; ++j)
        {
            d.knob[j] = j < k.size() ? k[j].getFloatValue() : kInfo[d.type].knobDefaults[j];
            if (j < c.size())
            {
                d.inMod[j] = (int8_t) juce::jlimit (-1, kMaxPatchModules - 1, c[j].upToFirstOccurrenceOf (":", false, false).getIntValue());
                d.inPort[j] = (int8_t) juce::jlimit (0, kPatchIO - 1, c[j].fromFirstOccurrenceOf (":", false, false).getIntValue());
            }
        }
        d.x = m.getProperty ("x", 0.f); d.y = m.getProperty ("y", 0.f);
    }
    // drop dangling cables
    for (auto& d : p.m)
        for (int j = 0; j < kPatchIO; ++j)
            if (d.inMod[j] >= 0 && p.m[d.inMod[j]].type < 0) d.inMod[j] = -1;
}

// ------------------------------------------------------------ engine
void PatchEngine::prepare (double sampleRate)
{
    sr = (float) sampleRate;
    for (auto& d : delayPool) d.assign ((size_t) (sr * 2.0f) + 4, 0.f);
    for (auto& row : meters) for (auto& m : row) m.store (0.f);
    for (auto& s : st) s = State();
}

void PatchEngine::setProgram (const PatchProgram& p) noexcept
{
    for (int i = 0; i < kMaxPatchModules; ++i)
        if (p.m[i].type != prog.m[i].type) st[i] = State();
    prog = p;
    used = 0; int nextDelay = 0;
    for (int i = 0; i < kMaxPatchModules; ++i)
    {
        if (prog.m[i].type >= 0) ++used;
        st[i].delayIdx = (prog.m[i].type == PmDelay && nextDelay < 8) ? nextDelay++ : -1;
    }
}

void PatchEngine::process (int n, const Inputs& I, float* outL, float* outR) noexcept
{
    numParamMods = 0;
    if (used == 0) return;
    static const int scales[6][12] = {
        { 1,1,1,1,1,1,1,1,1,1,1,1 }, { 1,0,1,0,1,1,0,1,0,1,0,1 }, { 1,0,1,1,0,1,0,1,1,0,1,0 },
        { 1,0,1,1,0,1,0,1,0,1,1,0 }, { 1,0,0,1,0,1,0,1,0,0,1,0 }, { 1,1,0,1,0,1,0,1,1,0,1,0 } };
    const double sixteenthsPerSample = I.beatsPerSample * 4.0;
    double freeBeat = lastSixteenth < 0 ? 0 : lastSixteenth;

    for (int i = 0; i < n; ++i)
    {
        const double six = I.playing ? (I.ppq * 4.0 + (double) i * sixteenthsPerSample) : (freeBeat += sixteenthsPerSample);
        const float clk16 = (six - std::floor (six)) < 0.5 ? 1.f : 0.f;
        const float sL = I.synthL ? I.synthL[i] : 0.f, sR = I.synthR ? I.synthR[i] : 0.f;

        for (int m = 0; m < kMaxPatchModules; ++m)
        {
            const auto& d = prog.m[m];
            if (d.type < 0) continue;
            auto& S = st[m];
            const float* k = d.knob;
            auto edge = [&S] (int port, float v) { const bool g = v > 0.5f; const bool e = g && ! S.lastGate[port]; S.lastGate[port] = g; return e; };
            switch (d.type)
            {
                case PmVCO:
                {
                    const float pitch = in (m, 0) + (k[0] - 0.5f) * 8.f;
                    const float f = 130.81f * std::exp2 (pitch);
                    if (connected (m, 2) && edge (2, in (m, 2))) S.s[0] = 0;
                    S.s[0] += f / sr + in (m, 1) * k[2] * 0.05f;
                    S.s[0] -= std::floor (S.s[0]);
                    const float p = S.s[0];
                    const float sh = k[1] * 3.f; const int a = juce::jmin (2, (int) sh); const float u = sh - (float) a;
                    const float w[4] = { std::sin (kTwoPi * p), 1.f - 4.f * std::abs (p - 0.5f), 2.f * p - 1.f, p < 0.5f ? 1.f : -1.f };
                    S.out[0] = lerp (w[a], w[a + 1], u) * 0.8f;
                    S.out[1] = w[3] * 0.8f;
                } break;
                case PmVCA: S.out[0] = in (m, 0) * clampf (k[1] + (connected (m, 1) ? in (m, 1) : 1.f) * k[0], -2.f, 2.f); break;
                case PmLFO:
                {
                    if (edge (1, in (m, 1))) S.s[0] = 0;
                    float rate = 0.01f * std::pow (2000.f, k[0]) * std::exp2 (in (m, 0) * 2.f);
                    if (d.option > 0)
                    {
                        static const double beats[5] = { 0, 0.25, 0.5, 1, 4 };
                        rate = (float) (1.0 / (beats[d.option] / juce::jmax (1e-9, I.beatsPerSample * sr)));
                    }
                    S.s[0] += rate / sr; if (S.s[0] >= 1.f) { S.s[0] -= 1.f; S.s[1] = rng.bi(); }
                    const float p = S.s[0];
                    const float sh = k[1] * 4.f; const int a = juce::jmin (3, (int) sh); const float u = sh - (float) a;
                    const float w[5] = { std::sin (kTwoPi * p), 1.f - 4.f * std::abs (p - 0.5f), 2.f * p - 1.f, p < 0.5f ? 1.f : -1.f, S.s[1] };
                    S.out[0] = lerp (w[a], w[a + 1], u);
                    S.out[1] = 0.5f * S.out[0] + 0.5f;
                    S.out[2] = p < 0.5f ? 1.f : 0.f;
                } break;
                case PmADSR:
                {
                    const float gate = connected (m, 0) ? in (m, 0) : I.gate;
                    auto coef = [this] (float x) { return 1.f - std::exp (-1.f / (0.001f * std::pow (5000.f, x) * sr * 0.3f)); };
                    float& e = S.s[0]; float& stage = S.s[1];
                    const bool g = gate > 0.5f;
                    if (g && ! S.lastGate[0]) stage = 1;
                    if (! g && S.lastGate[0]) stage = 3;
                    S.lastGate[0] = g;
                    if (stage == 1) { e += 1.f / (0.001f * std::pow (5000.f, k[0]) * sr); if (e >= 1.f) { e = 1.f; stage = 2; } }
                    else if (stage == 2) e += (k[2] - e) * coef (k[1]);
                    else if (stage == 3) { e -= e * coef (k[3]); if (e < 1e-4f) { e = 0; stage = 0; } }
                    S.out[0] = e; S.out[1] = 1.f - e;
                } break;
                case PmSampleHold:
                {
                    const float trig = connected (m, 1) ? in (m, 1) : clk16;
                    if (edge (1, trig)) S.out[0] = connected (m, 0) ? in (m, 0) : rng.bi();
                } break;
                case PmQuantizer:
                {
                    const float x = in (m, 0) * 12.f;     // octaves -> semitones
                    const int root = (int) (k[0] * 11.99f);
                    const auto& sc = scales[juce::jlimit (0, 5, (int) d.option)];
                    int note = (int) std::floor (x + 0.5f);
                    for (int off = 0; off < 12; ++off)
                    {
                        const int cand = note - off;
                        int deg = (cand - root) % 12; if (deg < 0) deg += 12;
                        if (sc[deg]) { note = cand; break; }
                    }
                    const float q = (float) note / 12.f;
                    S.out[1] = std::abs (q - S.out[0]) > 1e-4f ? 1.f : 0.f;
                    S.out[0] = q;
                } break;
                case PmSlew:
                {
                    const float x = in (m, 0);
                    const float t = x > S.out[0] ? k[0] : k[1];
                    const float a = std::exp (-1.f / (juce::jmax (1e-4f, t * t * 2.f) * sr));
                    S.out[0] = x + a * (S.out[0] - x);
                } break;
                case PmClock:
                {
                    const float c = connected (m, 0) ? in (m, 0) : clk16;
                    if (edge (1, in (m, 1))) S.counter = 0;
                    const int div = 1 + (int) (k[0] * 15.99f), mul = 1 + (int) (k[1] * 7.99f);
                    S.d += 1.0;
                    if (edge (0, c))
                    {
                        S.s[0] = (float) S.d; S.d = 0;   // measured period (samples)
                        S.out[0] = (S.counter % div) == 0 ? 1.f : 0.f;
                        S.counter++;
                        S.s[2] = 0;
                    }
                    if (S.s[0] > 0)
                    {
                        S.s[2] += 1.f;
                        const float period = S.s[0] / (float) mul;
                        const float ph = std::fmod (S.s[2], period) / period;
                        S.out[1] = ph < 0.5f ? 1.f : 0.f;
                    }
                    if (c < 0.5f) S.out[0] = 0;
                } break;
                case PmEuclid:
                {
                    const float c = connected (m, 0) ? in (m, 0) : clk16;
                    if (edge (1, in (m, 1))) S.step = 0;
                    const int steps = 1 + (int) (k[0] * 15.99f);
                    const int pulses = juce::jmin (steps, (int) (k[1] * 16.99f));
                    const int rot = (int) (k[2] * 15.99f);
                    if (edge (0, c))
                    {
                        const int st_ = (S.step + rot) % steps;
                        const bool hit = pulses > 0 && ((st_ * pulses) % steps) < pulses;
                        S.out[0] = hit ? 1.f : 0.f;
                        S.out[1] = hit && st_ == 0 ? 1.f : 0.f;
                        S.step = (S.step + 1) % steps;
                    }
                    if (c < 0.5f) { S.out[0] = 0; S.out[1] = 0; }
                } break;
                case PmLogic:
                {
                    const bool a = in (m, 0) > 0.5f, b = in (m, 1) > 0.5f;
                    bool r = false;
                    switch (d.option) { case 0: r = a && b; break; case 1: r = a || b; break; case 2: r = a != b; break; default: r = ! (a && b); break; }
                    S.out[0] = r ? 1.f : 0.f; S.out[1] = r ? 0.f : 1.f;
                } break;
                case PmTuring:
                {
                    const float c = connected (m, 0) ? in (m, 0) : clk16;
                    const int len = 2 + (int) (k[1] * 14.99f);
                    if (edge (0, c))
                    {
                        const uint16_t top = (uint16_t) ((S.bits >> (len - 1)) & 1u);
                        const uint16_t bit = rng.uni() < k[0] ? (uint16_t) (1u - top) : top;
                        S.bits = (uint16_t) (((S.bits << 1) | bit) & ((1u << len) - 1u));
                        S.out[0] = (float) (S.bits & 0xFF) / 255.f * k[2] * 2.f;
                        S.out[1] = (S.bits & 1u) ? 1.f : 0.f;
                    }
                    if (c < 0.5f) S.out[1] = 0;
                } break;
                case PmMixer: S.out[0] = in (m, 0) * k[0] + in (m, 1) * k[1] + in (m, 2) * k[2] + in (m, 3) * k[3]; break;
                case PmAttenuverter: S.out[0] = in (m, 0) * (k[0] * 2.f - 1.f) + (k[1] * 2.f - 1.f); break;
                case PmDelay:
                {
                    if (S.delayIdx < 0) { S.out[0] = in (m, 0); break; }
                    auto& buf = delayPool[S.delayIdx]; int& w = delayW[S.delayIdx];
                    const int N = (int) buf.size();
                    const float t = clampf (k[0] + in (m, 1) * 0.25f, 0.f, 1.f);
                    const float ds = juce::jmax (1.f, 0.001f * std::pow (2000.f, t) * sr);
                    float rp = (float) w - ds; while (rp < 0) rp += (float) N;
                    const int i0 = (int) rp; const float fr = rp - (float) i0;
                    const float y = buf[(size_t) i0] + (buf[(size_t) ((i0 + 1) % N)] - buf[(size_t) i0]) * fr;
                    const float x = in (m, 0);
                    buf[(size_t) w] = x + fastTanh (y * k[1] * 1.05f);
                    w = (w + 1) % N;
                    S.out[0] = lerp (x, y, k[2]);
                } break;
                case PmWavefolder:
                {
                    float x = in (m, 0) * (1.f + (k[0] + in (m, 1) * 0.5f) * 8.f) + (k[1] - 0.5f);
                    for (int it = 0; it < 6 && (x > 1.f || x < -1.f); ++it) x = x > 1.f ? 2.f - x : -2.f - x;
                    S.out[0] = clampf (x, -1.f, 1.f);
                } break;
                case PmBitcrusher:
                {
                    const float q = std::exp2 (2.f + k[0] * 12.f);
                    const int hold = 1 + (int) (k[1] * k[1] * 64.f);
                    if (++S.counter >= hold) { S.counter = 0; S.s[0] = std::round (in (m, 0) * q) / q; }
                    S.out[0] = S.s[0];
                } break;
                case PmEnvFollower:
                {
                    const float x = std::abs (in (m, 0)) * (0.5f + k[2] * 4.f);
                    const float a = std::exp (-1.f / ((0.0005f + k[0] * 0.05f) * sr)), r = std::exp (-1.f / ((0.01f + k[1] * 0.8f) * sr));
                    S.out[0] = x > S.out[0] ? x + a * (S.out[0] - x) : x + r * (S.out[0] - x);
                    S.out[1] = S.out[0] > 0.25f ? 1.f : 0.f;
                } break;
                case PmMidiCV:
                    S.out[0] = (I.note - 60.f) / 12.f; S.out[1] = I.gate; S.out[2] = I.velocity; S.out[3] = I.modWheel; break;
                case PmMacroIn:
                    S.out[0] = I.macros ? I.macros[juce::jlimit (0, kNumMacros - 1, (int) (k[0] * 7.99f))] : 0.f; break;
                case PmParamIn:
                    S.out[0] = (I.baseNorm && d.paramDest >= 0) ? I.baseNorm[d.paramDest] : 0.f; break;
                case PmSynthIn:
                    S.out[0] = sL; S.out[1] = sR;
                    S.s[0] = juce::jmax (std::abs (sL), S.s[0] * 0.9995f); S.out[2] = S.s[0];
                    break;
                case PmNoise:
                {
                    const float w = rng.bi();
                    S.s[0] = clampf (S.s[0] + w * 0.02f, -1.f, 1.f) * 0.999f;
                    S.out[0] = w; S.out[1] = S.s[0] * 3.f;
                } break;
                case PmAudioOut:
                    if (outL) outL[i] += clampf (in (m, 0), -2.f, 2.f) * k[0];
                    if (outR) outR[i] += clampf (connected (m, 1) ? in (m, 1) : in (m, 0), -2.f, 2.f) * k[0];
                    break;
                case PmPatchOut: case PmParamOut: S.out[0] = in (m, 0); break;
                default: break;
            }
            for (int p = 0; p < kPatchIO; ++p) S.peak[p] = juce::jmax (S.peak[p], std::abs (S.out[p]));
        }
    }
    if (! I.playing) lastSixteenth = freeBeat;

    // publish block-rate outputs
    for (auto& v : patchOut) v = 0;
    for (int m = 0; m < kMaxPatchModules; ++m)
    {
        const auto& d = prog.m[m];
        if (d.type < 0) continue;
        auto& S = st[m];
        if (d.type == PmPatchOut) patchOut[juce::jlimit (0, 3, (int) d.option)] = clampf (S.out[0], -1.f, 1.f);
        if (d.type == PmParamOut && d.paramDest >= 0 && numParamMods < kMaxPatchModules)
            paramMods[numParamMods++] = { d.paramDest, clampf (S.out[0], -1.f, 1.f) * (d.knob[0] * 2.f - 1.f) };
        for (int p = 0; p < kPatchIO; ++p)
        {
            meters[m][p].store (S.peak[p], std::memory_order_relaxed);
            S.peak[p] *= 0.5f;
        }
    }
}
} // namespace pd
