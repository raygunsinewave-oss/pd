#include "PresetGenerator.h"
#include "../patch/PatchGraph.h"
#include <set>

namespace pd
{
namespace
{
juce::String dash() { return juce::String::fromUTF8 ("\xe2\x80\x94"); }

enum Cat { CBassRoll = 0, CReese, CRaveLead, CMelLead, CPad, CVocal, CPluck, CRaveStab, CArp, CDrum, CBreak, CFx, CTexture, CModular, CScene, kNumCats };

// ------------------------------------------------------------ preset builder
struct B
{
    const ParamTable& T = ParamTable::get();
    std::map<juce::String, float> p;
    EngineState es;
    PatchProgram patch;
    Rng r;
    int nextSlot = 0;
    juce::StringArray macroNames { "Tone", "Drive", "Space", "Motion", "Width", "Env", "Grit", "Pitch" };

    explicit B (uint32_t seed) : r (seed)
    {
        for (auto& info : T.all()) p[info.id] = info.def;
        r.next(); r.next();
    }
    void set (const juce::String& id, float v)
    {
        const int i = T.indexOf (id);
        jassert (i >= 0);
        if (i < 0) return;
        const auto& rg = T[i].range;
        p[id] = rg.snapToLegalValue (juce::jlimit (rg.start, rg.end, v));
    }
    void ch (const juce::String& id, const juce::String& choice)
    {
        const int i = T.indexOf (id);
        if (i < 0) return;
        const int c = T[i].choices.indexOf (choice);
        jassert (c >= 0);
        p[id] = (float) juce::jmax (0, c);
    }
    float get (const juce::String& id) { return p[id]; }
    float rr (float lo, float hi) { return lo + (hi - lo) * r.uni(); }
    int ri (int lo, int hi) { return lo + r.range (hi - lo + 1); }
    bool chance (float pr) { return r.uni() < pr; }
    template <typename T2> const T2& pick (std::initializer_list<T2> l) { return *(l.begin() + r.range ((int) l.size())); }

    void mod (int src, const juce::String& dest, float amt, int curve = CurveLinear, int aux = SrcNone)
    {
        if (nextSlot >= kNumModSlots) return;
        const int d = T.indexOf (dest);
        jassert (d >= 0);
        if (d < 0) return;
        auto& s = es.slots[nextSlot];
        s.src = (int16_t) src; s.dest = (int16_t) d; s.curve = (int16_t) curve; s.aux = (int16_t) aux;
        set ("mm" + juce::String (nextSlot + 1) + "_amt", amt);
        ++nextSlot;
    }
    void fx (const juce::String& key, std::initializer_list<std::pair<const char*, float>> params, int bus = 0)
    {
        set ("fx_" + key + "_on", 1.f);
        set ("fx_" + key + "_bus", (float) bus);
        for (auto& [k, v] : params) set ("fx_" + key + "_" + k, v);
    }
    void osc (int o, int engine, float lvl)
    {
        const juce::String px = "o" + juce::String (o + 1) + "_";
        set (px + "on", 1.f); set (px + "eng", (float) engine); set (px + "lvl", lvl);
    }
    juce::String o (int i, const char* k) const { return "o" + juce::String (i + 1) + "_" + k; }
    juce::String f (int i, const char* k) const { return "f" + juce::String (i + 1) + "_" + k; }
    juce::String e (int i, const char* k) const { return "e" + juce::String (i + 1) + "_" + k; }
    juce::String l (int i, const char* k) const { return "l" + juce::String (i + 1) + "_" + k; }
    void env (int i, float a, float d, float s, float rl, float curve = 0.3f, float h = 0.f)
    {
        set (e (i, "a"), a); set (e (i, "d"), d); set (e (i, "s"), s); set (e (i, "r"), rl); set (e (i, "curve"), curve); set (e (i, "h"), h);
    }
    void lfo (int i, float rate, int shape, bool sync = false, int div = 7, int mode = 0, bool poly = true)
    {
        set (l (i, "rate"), rate); set (l (i, "shape"), (float) shape); set (l (i, "sync"), sync ? 1.f : 0.f);
        set (l (i, "div"), (float) div); set (l (i, "mode"), (float) mode); set (l (i, "poly"), poly ? 1.f : 0.f);
    }
    void filter (int i, int type, float cut, float res, float envAmt = 0, float key = 0.3f, float pre = 0, float post = 0)
    {
        set (f (i, "on"), 1.f); set (f (i, "type"), (float) type); set (f (i, "cut"), cut); set (f (i, "res"), res);
        set (f (i, "env"), envAmt); set (f (i, "key"), key); set (f (i, "pre"), pre); set (f (i, "post"), post);
    }
    void unison (int o_, int voices, float det, float width = 0.8f, float blend = 0.7f)
    {
        set (o (o_, "uni"), (float) voices); set (o (o_, "det"), det); set (o (o_, "width"), width); set (o (o_, "blend"), blend);
    }
    void standardMacros (bool withDist = false)
    {
        mod (SrcMacro1, "f1_cut", 0.35f);
        mod (SrcMacro2, "f1_pre", 0.6f);
        if (withDist) mod (SrcMacro2, "fx_dist_drive", 0.4f);
        mod (SrcMacro3, "fx_reverb_mix", 0.45f);
        mod (SrcMacro3, "fx_delay_mix", 0.3f);
        mod (SrcMacro4, "o1_pos", 0.5f);
        mod (SrcMacro5, "o1_det", 0.35f);
        mod (SrcMacro6, "f1_env", 0.4f);
        mod (SrcMacro7, "fx_lofi_mix", 0.6f);
        mod (SrcMacro8, "o1_pmod", 12.f / 96.f);
    }
    void mpe()
    {
        set ("mpe", 1.f);
        mod (SrcMpePressure, "f1_cut", 0.25f);
        mod (SrcMpeTimbre, "o1_pos", 0.4f);
    }
    // patch helpers
    int addModule (int type, float x, float y, std::initializer_list<float> knobs = {}, int option = 0, const juce::String& param = {})
    {
        for (int i = 0; i < kMaxPatchModules; ++i)
            if (patch.m[i].type < 0)
            {
                auto& d = patch.m[i];
                d.type = (int8_t) type; d.option = (int8_t) option; d.x = x; d.y = y;
                const auto& info = patchModuleInfo (type);
                for (int k = 0; k < kPatchIO; ++k) d.knob[k] = info.knobDefaults[k];
                int k = 0; for (float kv : knobs) if (k < kPatchIO) d.knob[k++] = kv;
                d.paramDest = (int16_t) (param.isNotEmpty() ? T.indexOf (param) : -1);
                return i;
            }
        return -1;
    }
    void cable (int fromMod, int fromPort, int toMod, int toPort)
    {
        if (fromMod < 0 || toMod < 0) return;
        patch.m[toMod].inMod[toPort] = (int8_t) fromMod;
        patch.m[toMod].inPort[toPort] = (int8_t) fromPort;
    }
};

// wavetable + sample indices (see Wavetable.cpp / SampleBank.cpp)
enum WT { WtBasic = 0, WtPwm, WtHarm, WtVowel, WtHoover, WtReese, WtOrgan, WtFmBright, WtFmMetal, WtSync, WtFold, WtCrushed,
          WtChoir, WtBell, WtSawTilt, WtSquare, WtTapeKeys, WtGrowl, WtComb, WtPD, WtDrift, WtSub, WtTriFold, WtPluck,
          WtWindow, WtSinc, WtHalf, WtOddEven, WtMetal, WtEeOo, WtSeed, WtWobble };
enum SM { SmChop0 = 0, SmPhrase = 16, SmBreak0 = 17, SmKick = 21, SmKickSub, SmKickCrunch, SmSnare, SmRim, SmSnareGarage,
          SmClap, SmHatC, SmHatO, SmShaker, SmWood, SmMetal, SmKeys, SmOrgan, SmRaveStab, SmStrings, SmBell, SmBassPluck,
          SmChoir, SmGlass, SmRain, SmCrowd, SmVinyl, SmTape };

void commonSpace (B& b, float verb, float delay)
{
    if (verb > 0) b.fx ("reverb", { { "mix", verb }, { "type", b.chance (0.5f) ? 1.f : 0.f }, { "decay", b.rr (1.5f, 5.f) }, { "size", b.rr (0.4f, 0.9f) } });
    if (delay > 0) b.fx ("delay", { { "mix", delay }, { "mode", (float) b.ri (0, 2) }, { "sync", 1.f }, { "div", (float) b.pick ({ 9, 10, 11, 7 }) },
                                    { "fb", b.rr (0.25f, 0.55f) }, { "tone", b.rr (-0.5f, 0.2f) } });
}

// ------------------------------------------------------------ recipes
void bassRolling (B& b)
{
    b.set ("poly", 1); b.set ("legato", b.chance (0.5f) ? 1.f : 0.f); b.set ("glide", b.chance (0.4f) ? b.rr (0.02f, 0.12f) : 0.f);
    const bool analog = b.chance (0.35f);
    b.osc (0, analog ? EngAnalog : EngWavetable, 0.7f);
    b.set ("o1_table", (float) b.pick ({ WtSub, WtSawTilt, WtReese, WtBasic, WtPD, WtWobble, WtSquare }));
    b.set ("o1_pos", b.rr (0.f, 0.7f));
    if (analog) b.set ("o1_va", (float) b.ri (0, 1));
    b.set ("o1_oct", -1);
    b.unison (0, b.pick ({ 1, 1, 2, 3 }), b.rr (0.05f, 0.2f), 0.3f);
    b.set ("sub_on", 1); b.set ("sub_lvl", b.rr (0.5f, 0.85f)); b.set ("sub_oct", -1); b.set ("sub_shape", (float) b.ri (0, 1));
    b.filter (0, b.pick ({ FtLadder, FtLadder, FtDiode, FtSvfLP }), b.rr (220.f, 900.f), b.rr (0.1f, 0.45f), b.rr (0.25f, 0.6f), 0.4f, b.rr (0.f, 0.4f));
    b.env (0, 0.001f, b.rr (0.25f, 0.9f), b.rr (0.25f, 0.8f), b.rr (0.05f, 0.2f));
    b.env (1, 0.001f, b.rr (0.08f, 0.35f), b.rr (0.0f, 0.3f), 0.15f, 0.6f);
    if (b.chance (0.5f)) { b.lfo (0, 1.f, 0, true, b.pick ({ 10, 13, 7 }), 1); b.mod (SrcLfo1, "o1_pos", b.rr (0.1f, 0.3f)); }
    if (b.chance (0.45f)) b.fx ("dist", { { "type", 0 }, { "drive", b.rr (3.f, 12.f) }, { "mix", b.rr (0.4f, 0.9f) } });
    if (b.chance (0.35f)) b.fx ("ott", { { "depth", b.rr (0.2f, 0.5f) } });
    if (b.chance (0.3f)) b.fx ("ducker", { { "depth", b.rr (0.3f, 0.6f) }, { "div", 1 } });
    b.standardMacros (true);
}

void bassReese (B& b)
{
    b.set ("poly", 1); b.set ("legato", 1); b.set ("glide", b.rr (0.0f, 0.1f));
    b.osc (0, b.chance (0.5f) ? EngAnalog : EngWavetable, 0.6f);
    b.osc (1, EngWavetable, 0.55f);
    b.set ("o1_table", (float) b.pick ({ WtReese, WtSawTilt, WtComb, WtGrowl }));
    b.set ("o2_table", (float) b.pick ({ WtReese, WtSawTilt, WtHoover, WtFold }));
    b.set ("o1_oct", -1); b.set ("o2_oct", -1);
    b.set ("o2_fine", b.rr (-18.f, 18.f));
    b.unison (0, b.ri (2, 5), b.rr (0.15f, 0.45f), b.rr (0.4f, 0.9f));
    b.unison (1, b.ri (2, 5), b.rr (0.15f, 0.45f), b.rr (0.4f, 0.9f));
    b.set ("sub_on", b.chance (0.7f) ? 1.f : 0.f); b.set ("sub_lvl", 0.5f);
    const bool comb = b.chance (0.35f);
    b.filter (0, comb ? b.pick ({ FtCombPlus, FtCombMinus }) : b.pick ({ FtLadder, FtDiode, FtRaveLadder }), b.rr (300.f, 1600.f), b.rr (0.2f, 0.6f), b.rr (0.f, 0.3f), 0.3f, b.rr (0.1f, 0.5f));
    if (comb) b.filter (1, FtSvfLP, b.rr (1500.f, 5000.f), 0.2f);
    b.lfo (0, b.rr (0.1f, 1.5f), b.pick ({ 0, 1, 6 }), b.chance (0.5f), b.pick ({ 3, 4, 7 }), 0, false);
    b.mod (SrcLfo1, "f1_cut", b.rr (0.08f, 0.25f));
    b.mod (SrcLfo1, "o1_pos", b.rr (0.1f, 0.4f));
    b.env (0, 0.005f, 0.6f, 0.9f, b.rr (0.1f, 0.3f));
    b.fx ("dist", { { "type", (float) b.pick ({ 0, 1, 2 }) }, { "drive", b.rr (4.f, 18.f) }, { "mix", b.rr (0.4f, 1.f) }, { "tone", b.rr (-0.4f, 0.3f) } });
    if (b.chance (0.5f)) b.fx ("chorus", { { "mode", (float) b.ri (1, 4) }, { "mix", b.rr (0.2f, 0.5f) } });
    if (b.chance (0.5f)) b.fx ("ott", { { "depth", b.rr (0.3f, 0.7f) } });
    b.standardMacros (true);
}

void raveLead (B& b)
{
    b.set ("poly", b.chance (0.5f) ? 1.f : 6.f); b.set ("legato", 1); b.set ("glide", b.rr (0.03f, 0.2f));
    b.osc (0, EngWavetable, 0.65f);
    b.set ("o1_table", (float) b.pick ({ WtHoover, WtHoover, WtPwm, WtSync, WtFmBright }));
    b.set ("o1_pos", b.rr (0.2f, 0.8f));
    b.unison (0, b.ri (6, 12), b.rr (0.35f, 0.75f), 0.9f, 0.8f);
    if (b.chance (0.6f)) { b.osc (1, EngAnalog, 0.4f); b.set ("o2_va", 3); b.set ("o2_oct", -1); b.set ("o2_pw", b.rr (0.3f, 0.8f)); }
    b.set ("o1_warp", (float) b.pick ({ WarpPWM, WarpBendPlus, WarpOff })); b.set ("o1_warp_amt", b.rr (0.1f, 0.5f));
    b.filter (0, b.pick ({ FtLadder, FtRaveLadder, FtSvfLP }), b.rr (1500.f, 6000.f), b.rr (0.1f, 0.5f), b.rr (0.f, 0.3f));
    b.env (0, b.rr (0.001f, 0.02f), 0.5f, 0.85f, b.rr (0.15f, 0.5f));
    // hoover pitch swoop: Env 3 -> pitch
    b.env (2, 0.001f, b.rr (0.08f, 0.3f), 0.f, 0.1f, 0.6f);
    b.mod (SrcEnv3, "o1_pmod", b.pick ({ -1.f, 1.f }) * b.rr (2.f, 7.f) / 96.f);
    b.lfo (1, b.rr (4.f, 6.5f), 0, false, 7, 1); b.set ("l2_delay", 0.4f);
    b.mod (SrcLfo2, "o1_pmod", 0.25f / 96.f);
    b.fx ("chorus", { { "mode", 0 }, { "mix", b.rr (0.3f, 0.6f) }, { "depth", 0.6f } });
    if (b.chance (0.6f)) b.fx ("dist", { { "type", 0 }, { "drive", b.rr (3.f, 9.f) }, { "mix", 0.6f } });
    commonSpace (b, b.rr (0.15f, 0.35f), b.chance (0.5f) ? b.rr (0.1f, 0.3f) : 0.f);
    b.standardMacros (true);
    b.mpe();
}

int WtGlassOrSine (B& b);
void melancholicLead (B& b)
{
    b.set ("poly", 1); b.set ("legato", 1); b.set ("glide", b.rr (0.06f, 0.25f));
    b.osc (0, b.chance (0.5f) ? EngAnalog : EngWavetable, 0.7f);
    b.set ("o1_table", (float) b.pick ({ WtSawTilt, WtPwm, WtWindow, WtPluck }));
    b.unison (0, b.ri (2, 3), b.rr (0.06f, 0.18f), 0.5f);
    if (b.chance (0.4f)) { b.osc (1, EngWavetable, 0.3f); b.set ("o2_table", (float) WtGlassOrSine (b)); b.set ("o2_oct", 1); }
    b.filter (0, b.pick ({ FtSvfLP, FtLadder }), b.rr (1200.f, 4000.f), b.rr (0.05f, 0.3f), b.rr (0.f, 0.2f));
    b.env (0, b.rr (0.01f, 0.08f), 0.8f, 0.8f, b.rr (0.3f, 0.9f));
    b.lfo (0, b.rr (4.5f, 5.8f), 0, false, 7, 1); b.set ("l1_delay", b.rr (0.3f, 0.9f));
    b.mod (SrcLfo1, "o1_pmod", 0.3f / 96.f);
    b.mod (SrcModWheel, "o1_pmod", 0.5f / 96.f, CurveLinear, SrcLfo1);
    b.fx ("tape", { { "mix", 0.6f }, { "wow", b.rr (0.1f, 0.4f) }, { "age", b.rr (0.2f, 0.5f) } });
    commonSpace (b, b.rr (0.2f, 0.4f), b.rr (0.15f, 0.35f));
    b.standardMacros();
    b.mpe();
}
int WtGlassOrSine (B& b) { return b.pick ({ (int) WtBell, (int) WtBasic, (int) WtHarm }); }

void pad (B& b)
{
    b.set ("poly", 12);
    b.osc (0, EngWavetable, 0.6f);
    b.set ("o1_table", (float) b.pick ({ WtVowel, WtChoir, WtDrift, WtHarm, WtSeed, WtEeOo, WtTapeKeys }));
    b.unison (0, b.ri (4, 8), b.rr (0.15f, 0.35f), 1.f, 0.75f);
    b.osc (1, EngWavetable, 0.45f);
    b.set ("o2_table", (float) b.pick ({ WtSawTilt, WtDrift, WtOrgan, WtBasic }));
    b.set ("o2_oct", (float) b.pick ({ -1, 0, 1 }));
    b.unison (1, b.ri (3, 6), b.rr (0.1f, 0.3f), 0.9f);
    if (b.chance (0.3f)) { b.set ("nz_on", 1); b.ch ("nz_type", "Tape Hiss"); b.set ("nz_lvl", 0.08f); }
    b.filter (0, FtSvfLP, b.rr (1500.f, 6000.f), b.rr (0.05f, 0.3f), b.rr (0.05f, 0.25f));
    b.set ("f1_morph", b.rr (0.f, 0.6f));
    b.env (0, b.rr (0.3f, 2.0f), 1.5f, 0.85f, b.rr (1.2f, 4.f));
    b.env (1, b.rr (0.5f, 2.5f), 2.f, 0.4f, 2.f);
    b.lfo (0, b.rr (0.05f, 0.3f), b.pick ({ 0, 6 }), false, 7, 0, false);
    b.mod (SrcLfo1, "o1_pos", b.rr (0.2f, 0.5f));
    b.lfo (1, b.rr (0.08f, 0.4f), 1, false, 7, 0, false);
    b.mod (SrcLfo2, "o2_pos", b.rr (0.15f, 0.4f));
    b.mod (SrcLfo2, "f1_cut", 0.06f);
    b.fx ("tape", { { "mix", b.rr (0.4f, 1.f) }, { "wow", b.rr (0.1f, 0.5f) }, { "age", b.rr (0.2f, 0.6f) } });
    b.fx ("chorus", { { "mode", (float) b.ri (1, 4) }, { "mix", b.rr (0.3f, 0.6f) } });
    if (b.chance (0.3f)) b.fx ("shimmer", { { "mix", b.rr (0.2f, 0.4f) }, { "shimmer", b.rr (0.3f, 0.7f) } });
    else commonSpace (b, b.rr (0.3f, 0.55f), 0.f);
    b.set ("fx_reverb_type", 1);
    b.standardMacros();
    b.mpe();
}

void vocal (B& b)
{
    b.set ("poly", 8);
    const int mode = b.ri (0, 3);
    const int chop = b.chance (0.25f) ? (int) SmPhrase : b.ri (0, 15);
    if (mode == 0)       { b.osc (0, EngSample, 0.85f); b.set ("o1_smode", 0); }
    else if (mode == 1)  { b.osc (0, EngSample, 0.85f); b.set ("o1_smode", 2); }
    else if (mode == 2)  { b.osc (0, EngSpectral, 0.85f); b.set ("o1_formant", b.rr (-5.f, 5.f)); b.set ("o1_tilt", b.rr (-0.3f, 0.2f)); }
    else                 { b.osc (0, EngGranular, 0.85f); b.set ("o1_gsize", b.rr (40.f, 140.f)); b.set ("o1_gdens", b.rr (15.f, 40.f));
                           b.set ("o1_gspray", b.rr (0.f, 0.3f)); b.set ("o1_gfreeze", b.chance (0.4f) ? 1.f : 0.f); b.set ("o1_pos", b.rr (0.1f, 0.5f)); }
    b.set ("o1_smp", (float) chop);
    b.set ("o1_pos", mode == 3 ? b.get ("o1_pos") : 0.f);
    if (b.chance (0.3f)) { b.unison (0, 2, 0.08f, 0.6f); }
    b.filter (0, b.pick ({ FtSvfLP, FtSvfBP, FtFormant }), b.rr (2000.f, 9000.f), b.rr (0.05f, 0.4f));
    if ((int) b.get ("f1_type") == FtFormant) { b.set ("f1_cut", 1000.f); b.set ("f1_morph", b.rr (0.f, 1.f)); b.set ("f1_mix", 0.6f); }
    const bool gated = b.chance (0.6f);
    b.env (0, 0.002f, gated ? b.rr (0.12f, 0.35f) : 0.8f, gated ? 0.f : 0.9f, gated ? 0.08f : 0.3f);
    if (b.chance (0.45f))
    {
        b.set ("arp_on", 1); b.ch ("arp_mode", b.pick ({ juce::String ("As Played"), juce::String ("Up"), juce::String ("Random") }));
        b.set ("arp_groove", (float) b.pick ({ 1, 2, 3, 9 })); b.set ("arp_swing", b.rr (54.f, 66.f)); b.set ("arp_div", 13);
        b.set ("arp_gate", b.rr (0.3f, 0.7f));
    }
    if (b.chance (0.4f)) b.fx ("eq", { { "lowg", -4.f }, { "highg", 3.f } });
    commonSpace (b, b.rr (0.15f, 0.35f), b.rr (0.1f, 0.35f));
    b.standardMacros();
    b.mod (SrcMacro8, "o1_formant", 0.3f);
}

void pluck (B& b)
{
    b.set ("poly", 10);
    const bool keys = b.chance (0.35f);
    if (keys)
    {
        b.osc (0, EngSample, 0.8f); b.set ("o1_smp", (float) b.pick ({ (int) SmKeys, (int) SmKeys, (int) SmBell, (int) SmGlass }));
        b.env (0, 0.001f, 2.5f, 0.0f, b.rr (0.4f, 1.2f));
        b.filter (0, FtSvfLP, b.rr (2500.f, 9000.f), 0.1f, 0.1f, 0.5f);
        b.fx ("tape", { { "mix", 0.7f }, { "age", b.rr (0.3f, 0.7f) }, { "wow", 0.25f } });
    }
    else
    {
        b.osc (0, b.chance (0.5f) ? EngAnalog : EngWavetable, 0.7f);
        b.set ("o1_table", (float) b.pick ({ WtPluck, WtSawTilt, WtSquare, WtFmBright, WtBell, WtTapeKeys }));
        b.unison (0, b.ri (1, 4), b.rr (0.05f, 0.25f), 0.7f);
        b.filter (0, b.pick ({ FtLadder, FtSvfLP }), b.rr (400.f, 1500.f), b.rr (0.1f, 0.5f), b.rr (0.4f, 0.8f), 0.5f);
        b.env (0, 0.001f, b.rr (0.15f, 0.5f), 0.f, b.rr (0.1f, 0.35f));
        b.env (1, 0.001f, b.rr (0.08f, 0.3f), 0.f, 0.2f, 0.6f);
    }
    if (b.chance (0.4f)) { b.set ("chord_on", 1); b.set ("chord_type", (float) b.pick ({ 1, 2, 3, 6, 10 })); b.set ("strum", b.rr (0.f, 25.f)); }
    commonSpace (b, b.rr (0.1f, 0.3f), b.rr (0.15f, 0.4f));
    b.standardMacros();
}

void raveStab (B& b)
{
    b.set ("poly", 16);
    const int src = b.ri (0, 2);
    if (src == 0) { b.osc (0, EngWavetable, 0.7f); b.set ("o1_table", (float) WtOrgan); b.set ("o1_pos", b.rr (0.f, 1.f)); }
    else if (src == 1) { b.osc (0, EngSample, 0.8f); b.set ("o1_smp", (float) b.pick ({ (int) SmOrgan, (int) SmRaveStab, (int) SmStrings })); }
    else { b.osc (0, EngAnalog, 0.6f); b.set ("o1_va", 3); b.set ("o1_pw", b.rr (0.3f, 0.7f)); }
    b.set ("chord_on", 1);
    b.set ("chord_type", (float) b.pick ({ 1, 2, 7, 7, 10, 3 }));
    b.filter (0, b.pick ({ FtLadder, FtSvfLP, FtSvfBP }), b.rr (900.f, 4000.f), b.rr (0.1f, 0.4f), b.rr (0.2f, 0.5f));
    b.env (0, 0.001f, b.rr (0.15f, 0.45f), b.rr (0.f, 0.2f), b.rr (0.1f, 0.3f));
    b.env (1, 0.001f, b.rr (0.1f, 0.3f), 0.f, 0.2f);
    if (b.chance (0.5f)) b.fx ("dist", { { "type", (float) b.pick ({ 0, 3 }) }, { "drive", b.rr (2.f, 8.f) }, { "mix", 0.5f }, { "bits", b.rr (8.f, 12.f) } });
    if (b.chance (0.3f)) b.fx ("lofi", { { "mix", 0.6f }, { "rate", b.rr (12000.f, 26000.f) }, { "bits", b.rr (8.f, 12.f) } });
    commonSpace (b, b.rr (0.2f, 0.4f), b.chance (0.5f) ? b.rr (0.1f, 0.3f) : 0.f);
    b.standardMacros (true);
}

void arp (B& b)
{
    b.set ("poly", 8);
    b.osc (0, b.chance (0.4f) ? EngAnalog : EngWavetable, 0.7f);
    b.set ("o1_table", (float) b.pick ({ WtPluck, WtFmBright, WtSquare, WtSawTilt, WtBell, WtMetal }));
    b.unison (0, b.ri (1, 3), 0.1f, 0.6f);
    b.filter (0, b.pick ({ FtLadder, FtSvfLP, FtDiode }), b.rr (500.f, 2000.f), b.rr (0.2f, 0.55f), b.rr (0.3f, 0.7f));
    b.env (0, 0.001f, b.rr (0.12f, 0.35f), 0.f, 0.12f);
    b.env (1, 0.001f, b.rr (0.06f, 0.2f), 0.f, 0.1f, 0.6f);
    b.set ("arp_on", 1);
    b.set ("arp_mode", (float) b.ri (0, 4));
    b.set ("arp_div", (float) b.pick ({ 13, 13, 10, 14 }));
    b.set ("arp_oct", (float) b.ri (1, 3));
    b.set ("arp_groove", (float) b.ri (0, 11));
    b.set ("arp_swing", b.rr (50.f, 64.f));
    b.set ("arp_ratchet", (float) (b.chance (0.3f) ? 2 : 1));
    b.set ("arp_prob", b.rr (0.8f, 1.f));
    b.set ("arp_gate", b.rr (0.3f, 0.8f));
    b.set ("arp_latch", 0);
    const int style = b.ri (0, 2);
    if (style == 1)   // step-sequenced filter
    {
        for (int k = 1; k <= 16; ++k) b.set ("s1_st" + juce::String (k), b.rr (-1.f, 1.f));
        b.set ("s1_len", (float) b.pick ({ 16, 12, 7, 5 }));
        b.mod (SrcSeq1, "f1_cut", b.rr (0.1f, 0.25f));
    }
    else if (style == 2)   // Turing machine via PATCH tab -> filter + wavetable position
    {
        const int tm = b.addModule (PmTuring, 40, 40, { b.rr (0.1f, 0.4f), b.rr (0.3f, 0.7f), 0.5f });
        const int po = b.addModule (PmParamOut, 260, 40, { 0.75f }, 0, "f1_cut");
        const int po2 = b.addModule (PmParamOut, 260, 160, { 0.7f }, 0, "o1_pos");
        b.cable (tm, 0, po, 0); b.cable (tm, 0, po2, 0);
    }
    commonSpace (b, b.rr (0.1f, 0.3f), b.rr (0.15f, 0.35f));
    b.standardMacros();
}

void drum (B& b)
{
    b.set ("poly", 6);
    const int kind = b.ri (0, 3);
    if (kind == 3)   // synthesised kick: sine + pitch envelope
    {
        b.osc (0, EngWavetable, 0.9f); b.set ("o1_table", (float) WtBasic); b.set ("o1_pos", 0.f); b.set ("o1_oct", -2);
        b.set ("o1_rphase", 0.f);
        b.env (2, 0.f, b.rr (0.03f, 0.08f), 0.f, 0.05f, 0.8f);
        b.mod (SrcEnv3, "o1_pmod", b.rr (24.f, 40.f) / 96.f);
        b.env (0, 0.f, b.rr (0.25f, 0.6f), 0.f, 0.1f, 0.5f);
        b.set ("f1_on", 0);
    }
    else
    {
        static const int kicks[] = { SmKick, SmKickSub, SmKickCrunch }, snares[] = { SmSnare, SmRim, SmSnareGarage, SmClap }, hats[] = { SmHatC, SmHatO, SmShaker, SmWood, SmMetal };
        const int smp = kind == 0 ? kicks[b.ri (0, 2)] : kind == 1 ? snares[b.ri (0, 3)] : hats[b.ri (0, 4)];
        b.osc (0, EngSample, 0.9f); b.set ("o1_smp", (float) smp); b.set ("o1_smode", 0);
        b.set ("o1_semi", (float) b.ri (-3, 3));
        b.env (0, 0.f, 2.f, 1.f, 0.05f);
        b.set ("f1_on", b.chance (0.5f) ? 1.f : 0.f);
        b.set ("f1_type", FtSvfLP); b.set ("f1_cut", b.rr (5000.f, 18000.f));
    }
    b.set ("velo_sens", 0.8f);
    b.fx ("dist", { { "type", (float) b.pick ({ 0, 2, 3 }) }, { "drive", b.rr (2.f, 14.f) }, { "mix", b.rr (0.3f, 0.9f) }, { "bits", b.rr (6.f, 12.f) } });
    b.fx ("transient", { { "attack", b.rr (0.f, 0.6f) }, { "sustain", b.rr (-0.4f, 0.2f) } });
    if (b.chance (0.4f)) b.fx ("lofi", { { "mix", 0.5f }, { "rate", b.rr (14000.f, 30000.f) }, { "bits", b.rr (9.f, 13.f) } });
    if (b.chance (0.3f)) b.fx ("cassette", { { "mix", 0.6f }, { "wear", b.rr (0.2f, 0.5f) } });
    b.standardMacros (true);
}

void breaks (B& b)
{
    b.set ("poly", 4);
    b.osc (0, EngSample, 0.85f);
    b.set ("o1_smp", (float) b.ri (SmBreak0, SmBreak0 + 3));
    b.set ("o1_smode", (float) b.pick ({ 1, 1, 2 }));
    b.env (0, 0.f, 2.f, 1.f, 0.05f);
    b.set ("f1_on", 1); b.set ("f1_type", (float) b.pick ({ FtSvfLP, FtSvfHP })); b.set ("f1_cut", b.get ("f1_type") == FtSvfHP ? b.rr (30.f, 90.f) : b.rr (6000.f, 16000.f));
    b.fx ("dist", { { "type", 0 }, { "drive", b.rr (2.f, 10.f) }, { "mix", 0.5f } });
    if (b.chance (0.6f)) b.fx ("cassette", { { "mix", 0.7f }, { "wear", b.rr (0.2f, 0.6f) }, { "flutter", b.rr (0.1f, 0.4f) } });
    if (b.chance (0.5f)) b.fx ("lofi", { { "mix", 0.4f }, { "rate", b.rr (16000.f, 32000.f) } });
    if (b.chance (0.3f)) b.fx ("ott", { { "depth", 0.35f } });
    b.standardMacros (true);
}

void fxPreset (B& b)
{
    b.set ("poly", 4);
    const int kind = b.ri (0, 4);   // riser, downer, impact, sweep, tape stop
    b.set ("nz_on", 1); b.ch ("nz_type", b.pick ({ juce::String ("White"), juce::String ("Pink"), juce::String ("Tape Hiss") })); b.set ("nz_lvl", b.rr (0.3f, 0.6f));
    b.osc (0, EngWavetable, 0.5f); b.set ("o1_table", (float) b.pick ({ WtSawTilt, WtHoover, WtSync, WtFmMetal }));
    b.unison (0, b.ri (3, 8), 0.4f, 1.f);
    b.filter (0, FtSvfLP, 800.f, 0.4f);
    b.lfo (0, 0.25f, 2, false, 7, 2, true);    // one-shot saw up
    if (kind == 0 || kind == 3)
    {
        b.set ("l1_rate", b.rr (0.12f, 0.35f));
        b.mod (SrcLfo1, "f1_cut", 0.45f);
        b.mod (SrcLfo1, "o1_pmod", (kind == 0 ? 12.f : 3.f) / 96.f);
        b.env (0, b.rr (1.f, 3.f), 1.f, 1.f, 1.f);
    }
    else if (kind == 1)
    {
        b.set ("l1_shape", 3); b.set ("l1_rate", b.rr (0.2f, 0.6f)); b.set ("f1_cut", 6000.f);
        b.mod (SrcLfo1, "o1_pmod", -24.f / 96.f, CurveInverted);
        b.mod (SrcLfo1, "f1_cut", -0.4f, CurveInverted);
        b.env (0, 0.01f, 3.f, 0.f, 1.f);
    }
    else if (kind == 2)
    {
        b.osc (1, EngSample, 0.8f); b.set ("o2_smp", (float) b.pick ({ (int) SmKickSub, (int) SmKickCrunch }));
        b.set ("f1_cut", 3000.f);
        b.env (0, 0.f, 1.5f, 0.f, 1.f);
        b.set ("nz_lvl", 0.25f);
    }
    else
    {
        b.osc (1, EngAnalog, 0.6f); b.set ("o2_va", 0);
        b.env (0, 0.01f, 1.f, 1.f, 0.5f);
        b.set ("tapestop_time", b.rr (0.5f, 1.5f));
        b.lfo (1, 0.6f, 3, false, 7, 2); b.mod (SrcLfo2, "o2_pmod", -12.f / 96.f);
    }
    b.set ("riser_bars", (float) b.ri (0, 2));
    commonSpace (b, b.rr (0.3f, 0.6f), b.rr (0.1f, 0.3f));
    b.set ("fx_reverb_type", 1); b.set ("fx_reverb_decay", b.rr (3.f, 8.f));
    b.standardMacros();
}

void texture (B& b)
{
    b.set ("poly", 6);
    const int kind = b.ri (0, 2);
    if (kind == 0) { b.osc (0, EngSample, 0.7f); b.set ("o1_smp", (float) b.ri (SmRain, SmTape)); b.set ("o1_smode", 1); }
    else if (kind == 1) { b.osc (0, EngGranular, 0.75f); b.set ("o1_smp", (float) b.pick ({ (int) SmRain, (int) SmCrowd, (int) SmChoir, (int) SmStrings, (int) SmTape }));
                          b.set ("o1_gsize", b.rr (60.f, 300.f)); b.set ("o1_gdens", b.rr (8.f, 30.f)); b.set ("o1_gspray", b.rr (0.2f, 0.8f));
                          b.set ("o1_gfreeze", b.chance (0.5f) ? 1.f : 0.f); b.set ("o1_pos", b.rr (0.f, 0.8f)); }
    else { b.osc (0, EngWavetable, 0.4f); b.set ("o1_table", (float) b.pick ({ WtDrift, WtChoir, WtSeed })); b.unison (0, 6, 0.3f, 1.f); }
    b.set ("nz_on", 1);
    b.ch ("nz_type", b.pick ({ juce::String ("Vinyl"), juce::String ("Tape Hiss"), juce::String ("Rain"), juce::String ("Crowd"), juce::String ("Cassette Flutter") }));
    b.set ("nz_lvl", b.rr (0.2f, 0.5f)); b.set ("nz_color", b.rr (-0.6f, 0.2f));
    b.filter (0, FtSvfLP, b.rr (2000.f, 9000.f), 0.1f);
    b.env (0, b.rr (0.5f, 2.f), 1.f, 0.9f, b.rr (1.5f, 4.f));
    b.lfo (0, b.rr (0.03f, 0.2f), 6, false, 7, 0, false); b.mod (SrcLfo1, "f1_cut", 0.12f); b.mod (SrcLfo1, "o1_pos", 0.2f);
    b.fx ("cassette", { { "mix", b.rr (0.3f, 0.8f) }, { "wear", b.rr (0.3f, 0.7f) }, { "hiss", b.rr (0.1f, 0.4f) } });
    commonSpace (b, b.rr (0.35f, 0.6f), b.chance (0.3f) ? 0.2f : 0.f);
    b.set ("fx_reverb_type", 1);
    b.standardMacros();
}

void modular (B& b)
{
    b.set ("poly", 6);
    b.osc (0, EngWavetable, 0.65f);
    b.set ("o1_table", (float) b.pick ({ WtPluck, WtVowel, WtFmBright, WtDrift, WtSquare }));
    b.filter (0, FtLadder, b.rr (600.f, 2500.f), b.rr (0.2f, 0.5f));
    b.env (0, 0.005f, 1.f, 0.8f, 0.6f);
    const int kind = b.ri (0, 3);
    if (kind == 0)   // generative melody on a held note
    {
        const int tm = b.addModule (PmTuring, 30, 30, { b.rr (0.05f, 0.3f), b.rr (0.3f, 0.7f), b.rr (0.3f, 0.6f) });
        const int q = b.addModule (PmQuantizer, 230, 30, { (float) b.ri (0, 11) / 12.f }, b.ri (2, 5));
        const int po = b.addModule (PmParamOut, 430, 30, { 0.5f + 12.f / 96.f }, 0, "o1_pmod");
        const int eu = b.addModule (PmEuclid, 30, 200, { b.rr (0.4f, 1.f), b.rr (0.2f, 0.6f), 0.f });
        const int env = b.addModule (PmADSR, 230, 200, { 0.f, 0.15f, 0.f, 0.2f });
        const int po2 = b.addModule (PmParamOut, 430, 200, { 0.85f }, 0, "f1_cut");
        b.cable (tm, 0, q, 0); b.cable (q, 0, po, 0); b.cable (eu, 0, env, 0); b.cable (env, 0, po2, 0);
    }
    else if (kind == 1)   // self-playing voice, no keys needed
    {
        const int eu = b.addModule (PmEuclid, 30, 30, { b.rr (0.5f, 1.f), b.rr (0.25f, 0.6f), (float) b.ri (0, 7) / 16.f });
        const int tm = b.addModule (PmTuring, 30, 190, { 0.2f, 0.45f, 0.5f });
        const int q = b.addModule (PmQuantizer, 220, 190, { 0.f }, 2);
        const int vco = b.addModule (PmVCO, 220, 30, { b.rr (0.35f, 0.6f), b.rr (0.4f, 0.9f), 0.f });
        const int env = b.addModule (PmADSR, 410, 190, { 0.f, b.rr (0.1f, 0.3f), 0.f, 0.2f });
        const int vca = b.addModule (PmVCA, 410, 30, { 1.f, 0.f });
        const int dl = b.addModule (PmDelay, 600, 30, { b.rr (0.35f, 0.55f), b.rr (0.3f, 0.6f), 0.4f });
        const int out = b.addModule (PmAudioOut, 790, 30, { 0.45f });
        b.cable (tm, 0, q, 0); b.cable (q, 0, vco, 0); b.cable (eu, 0, env, 0); b.cable (vco, 0, vca, 0);
        b.cable (env, 0, vca, 1); b.cable (vca, 0, dl, 0); b.cable (dl, 0, out, 0);
        b.set ("o1_lvl", 0.35f);
    }
    else if (kind == 2)   // slowly evolving drift
    {
        const int l1 = b.addModule (PmLFO, 30, 30, { b.rr (0.05f, 0.25f), 0.f });
        const int l2 = b.addModule (PmLFO, 30, 190, { b.rr (0.1f, 0.3f), 0.25f });
        const int sh = b.addModule (PmSampleHold, 220, 190);
        const int sl = b.addModule (PmSlew, 400, 190, { 0.5f, 0.5f });
        const int p1 = b.addModule (PmParamOut, 400, 30, { 0.8f }, 0, "o1_pos");
        const int p2 = b.addModule (PmParamOut, 590, 190, { 0.7f }, 0, "f1_cut");
        const int cl = b.addModule (PmClock, 220, 30, { 0.5f, 0.f });
        b.cable (l1, 0, p1, 0); b.cable (cl, 0, sh, 1); b.cable (l2, 0, sh, 0); b.cable (sh, 0, sl, 0); b.cable (sl, 0, p2, 0);
        b.env (0, 0.8f, 1.f, 0.9f, 2.f);
    }
    else   // synth -> env follower feedback + folded audio return
    {
        const int si = b.addModule (PmSynthIn, 30, 30);
        const int ef = b.addModule (PmEnvFollower, 220, 30, { 0.1f, 0.4f, 0.6f });
        const int po = b.addModule (PmParamOut, 410, 30, { 0.25f }, 0, "f1_cut");
        const int wf = b.addModule (PmWavefolder, 220, 190, { 0.4f, 0.5f });
        const int out = b.addModule (PmAudioOut, 410, 190, { 0.25f });
        b.cable (si, 0, ef, 0); b.cable (ef, 0, po, 0); b.cable (si, 0, wf, 0); b.cable (wf, 0, out, 0);
    }
    commonSpace (b, b.rr (0.2f, 0.45f), b.rr (0.15f, 0.4f));
    b.standardMacros();
}

void scene (B& b, PresetData& d)
{
    // base sound from a performance-friendly recipe
    switch (b.ri (0, 3)) { case 0: pad (b); break; case 1: raveLead (b); break; case 2: arp (b); break; default: bassRolling (b); break; }
    b.set ("morph_on", 1);
    b.set ("riser_bars", (float) b.ri (0, 3));
    b.set ("riser_filter", b.rr (0.5f, 1.f)); b.set ("riser_noise", b.rr (0.3f, 0.8f)); b.set ("riser_verb", b.rr (0.3f, 0.8f)); b.set ("riser_pitch", b.rr (0.f, 0.5f));
    b.set ("arp_groove", (float) b.ri (1, 11));
    b.set ("stutter_div", (float) b.pick ({ 13, 15, 10 }));
    const char* moves[] = { "f1_cut", "f1_res", "o1_pos", "fx_reverb_mix", "fx_delay_mix", "o1_det", "fx_chorus_mix", "f1_pre" };
    d.hasScenes = true;
    for (int s = 0; s < kNumScenes; ++s)
    {
        auto& sc = d.scenes[(size_t) s];
        for (auto* id : moves)
        {
            const int i = b.T.indexOf (id);
            const auto& rg = b.T[i].range;
            float n = rg.convertTo0to1 (b.p[id]);
            if (s > 0) n = clamp01 (n + b.rr (-0.35f, 0.35f) * (float) s / 3.f);
            sc[id] = rg.convertFrom0to1 (n);
        }
    }
    b.macroNames = { "Tone", "Drive", "Space", "Motion", "Width", "Env", "Grit", "Pitch" };
}
} // namespace

// ============================================================ categories + plan
const juce::StringArray& PresetGenerator::categories()
{
    static const juce::StringArray c {
        "Bass " + dash() + " Rolling / 2-Step", "Bass " + dash() + " Reese & Growl", "Leads " + dash() + " Rave / Hoover",
        "Leads " + dash() + " Melancholic", "Pads " + dash() + " Euphoric / Melancholy", "Vocal Chops & Stabs", "Plucks & Keys",
        "Rave Stabs & Chords", "Arps & Sequences", "Drums " + dash() + " Kicks/Snares/Hats", "Breaks & Percussion Loops",
        "FX " + dash() + " Risers/Downers/Impacts", "Textures & Atmospheres", "Modular Patches", "Performance Scenes" };
    return c;
}

const std::vector<int>& PresetGenerator::categoryTargets()
{
    // Spec minimums sum to 2,650; +200 spread over the core categories to reach the 2,850 total.
    static const std::vector<int> t { 290, 200, 180, 150, 290, 290, 230, 170, 230, 200, 120, 150, 150, 100, 100 };
    return t;
}

juce::String PresetGenerator::categoryType (int c)
{
    static const char* t[] = { "Bass", "Bass", "Lead", "Lead", "Pad", "Vocal", "Pluck", "Stab", "Arp", "Drum", "Loop", "FX", "Texture", "Modular", "Scene" };
    return t[juce::jlimit (0, kNumCats - 1, c)];
}

std::vector<int> PresetGenerator::testNotes (int c)
{
    switch (c)
    {
        case CBassRoll: case CReese: return { 36 };
        case CRaveLead: case CMelLead: return { 67 };
        case CPad: case CTexture: return { 60, 63, 67, 70 };
        case CVocal: case CRaveStab: case CPluck: case CArp: return { 60, 63, 67 };
        case CDrum: case CBreak: case CFx: case CModular: return { 60 };
        default: return { 48, 60, 63, 67 };
    }
}

namespace
{
struct Title { const char* name; int bank; std::vector<int> cats; };

const std::vector<Title>& titles()
{
    static const std::vector<Title> t {
        // Bank 1 — Pure Devotion (2026)
        { "Lockup", 1, { CRaveLead, CRaveStab } },
        { "Knight In Shining Prada", 1, { CReese, CRaveLead, CBassRoll } },
        { "Slowmotion", 1, { CPad, CPluck } },
        { "Even Angels Ghost", 1, { CPad, CVocal, CModular } },
        { "Barum", 1, { CDrum, CBreak, CBassRoll } },
        { "Mialon", 1, { CArp, CPluck, CModular } },
        { "Laxbrook", 1, { CBassRoll, CArp } },
        { "Gem Lingo", 1, { CVocal, CPluck } },
        { "Adre", 1, { CRaveStab, CRaveLead } },
        { "Ballad", 1, { CPad, CPluck, CMelLead } },
        { "After The Rain", 1, { CPad, CPluck, CModular } },
        // Bank 2 — Good Lies (2023)
        { "Feelings Plain", 2, { CPad, CPluck } },
        { "Arla Fearn", 2, { CBassRoll, CReese } },
        { "Good Lies", 2, { CVocal, CMelLead } },
        { "Walk Thru Water", 2, { CVocal, CPad } },
        { "Cold Blooded", 2, { CRaveLead, CRaveStab, CReese } },
        { "Skulled", 2, { CPad, CPluck } },
        { "Sugarushhh", 2, { CArp, CBreak, CDrum } },
        { "Calon", 2, { CMelLead, CPluck } },
        { "Is U", 2, { CBassRoll, CReese, CVocal } },
        { "Vermonly", 2, { CReese, CDrum } },
        { "So U Kno", 2, { CBassRoll, CReese } },
        { "Calling Out", 2, { CVocal, CMelLead } },
        // Bank 3 — EPs & Singles
        { "Arla", 3, { CArp, CBreak, CDrum } },
        { "Raft Living", 3, { CPad, CModular } },
        { "Everything U Need", 3, { CArp, CBreak, CDrum } },
        { "Clipper", 3, { CDrum, CReese } },
        { "Cash Romantic", 3, { CRaveStab, CPluck } },
        { "BMW Track", 3, { CBassRoll, CReese } },
        { "Diamond Cut", 3, { CArp, CBreak, CDrum } },
        { "Bby", 3, { CVocal, CPluck } },
        { "Blind Date", 3, { CVocal } },
        { "Bromley", 3, { CMelLead, CPad } },
        { "Still Moving", 3, { CArp, CModular } },
        { "Freedom 2", 3, { CRaveStab, CRaveLead } },
        { "Blow Out", 3, { CRaveLead, CRaveStab } },
        { "stayinit", 3, { CBassRoll, CDrum } },
        { "Turn The Page", 3, { CPad, CPluck, CModular } },
        { "Lippy", 3, { CVocal, CBassRoll } },
        { "If We Ever", 3, { CMelLead, CPad } },
        { "Paradise Runner", 3, { CRaveLead, CArp } },
    };
    return t;
}

const std::vector<juce::StringArray>& descriptors()
{
    static const std::vector<juce::StringArray> d {
        { "Sub Roll", "Rolling Bass", "2-Step Bass", "Rubber Bass", "Garage Sub", "Bounce Bass", "Low Pluck", "Skip Bass", "Rubber Reese", "Sub Pressure" },
        { "Reese", "Growl", "Dark Reese", "Comb Reese", "Tear Out", "Grit Bass", "Wide Reese", "Low Growl" },
        { "Hoover", "Rave Lead", "Scream Lead", "Stab Lead", "Hoover Rise", "Siren", "Bent Lead" },
        { "Glide Lead", "Thin Lead", "Lonely Lead", "Night Lead", "Soft Lead", "Echo Lead" },
        { "Pad", "Euphoric Pad", "Tape Pad", "Wide Pad", "Dusk Pad", "Chorus Pad", "Slow Pad", "Haze Pad" },
        { "Vocal Chop", "Vox Stab", "Formant Chop", "Gated Vox", "Pitched Chop", "Vox Shuffle", "Vocal Pad" },
        { "Pluck", "Keys", "Piano Outro", "Stab Chord", "Delay Pluck", "Bell Keys", "2-Step Keys" },
        { "Organ Stab", "Rave Stab", "House Chord", "Detuned Chord", "Mono Stab", "Warehouse Stab" },
        { "Arp", "Sequence", "Euclid Arp", "Turing Seq", "Poly Arp", "Ratchet Arp", "Groove Seq" },
        { "Kick", "Snare", "Hat", "Clap", "Perc", "Crunch Kick", "Rim" },
        { "Break", "Break Loop", "Chopped Break", "Perc Loop", "Shuffle Break" },
        { "Riser", "Downer", "Impact", "Sweep", "Tape Stop", "Noise Swell", "Uplifter" },
        { "Texture", "Cassette Hiss", "Rain Bed", "Crowd Haze", "Vinyl Air", "Atmos" },
        { "Patch", "Generative", "Self Patch", "Evolving", "Turing Patch", "Euclid Patch" },
        {} };
    return d;
}

juce::String bankName (int bank)
{
    switch (bank)
    {
        case 1: return "Bank 1 " + dash() + " Pure Devotion (2026)";
        case 2: return "Bank 2 " + dash() + " Good Lies (2023)";
        case 3: return "Bank 3 " + dash() + " EPs & Singles";
        default: return "Bank 4 " + dash() + " Live / Performance Scenes";
    }
}
juce::String releaseName (int bank)
{
    switch (bank) { case 1: return "Pure Devotion"; case 2: return "Good Lies"; case 3: return "EP/Single"; default: return "Live"; }
}

juce::String neutralTitle (int i)
{
    static const char* a[] = { "Sodium", "Shutter", "Ultramarine", "Night Bus", "Concrete", "Halation", "Lavender", "Chrome", "Back Seat", "Low Angle",
                               "Bowl", "Collar", "Tan", "Dusk", "Overpass", "Underpass", "Signal", "Static", "Tunnel", "Ferry", "Canal",
                               "Northern", "Eastbound", "Tarmac", "Strobe", "Afterglow", "Warehouse", "Rooftop", "Arcade", "Pylon", "Estate",
                               "Ring Road", "Lido", "Pier", "Viaduct", "Junction", "Harbour", "Allotment", "Embankment", "Terrace", "Platform" };
    return a[i % (int) std::size (a)];
}
} // namespace

std::vector<GenSpec> PresetGenerator::plan (bool neutralNames)
{
    std::vector<GenSpec> out;
    std::set<juce::String> used;
    const auto& T = titles();
    auto unique = [&used] (juce::String n) {
        if (used.count (n) == 0) { used.insert (n); return n; }
        for (int k = 2;; ++k) { auto c = n + " " + juce::String (k); if (used.count (c) == 0) { used.insert (c); return c; } }
    };
    uint32_t seed = 0x2850u;
    for (int c = 0; c < kNumCats; ++c)
    {
        const int count = categoryTargets()[(size_t) c];
        if (c == CScene)
        {
            static const char* venues[] = { "Ally Pally", "West Holts", "Warehouse Project", "Printworks", "Brixton", "Roundhouse", "Field Day",
                                            "Parklife", "Barrowland", "Motion", "Albert Hall", "Boiler Room", "Sonar Night", "Dekmantel",
                                            "Primavera", "Lost Village", "Kelvingrove", "Victoria Warehouse", "Drumsheds", "Hidden" };
            static const char* moments[] = { "Drop", "Build", "Peak", "Intro", "Encore", "Breakdown", "Closer", "Lights Up", "Sunrise", "Rewind" };
            for (int k = 0; k < count; ++k)
            {
                GenSpec g;
                g.category = c; g.bankIndex = 4; g.bank = bankName (4); g.release = releaseName (4);
                const juce::String venue = neutralNames ? neutralTitle (k) : juce::String (venues[k % 20]);
                g.track = neutralNames ? juce::String ("Live Set") : juce::String ("Pure Devotion Live");
                g.name = unique (g.track + " " + dash() + " " + venue + " " + moments[(k / 20 + k) % 10]);
                g.seed = (seed += 0x9E3779B9u);
                out.push_back (g);
            }
            continue;
        }
        // eligible titles: those whose character matches; FX + textures go to every title
        std::vector<int> elig;
        for (int t = 0; t < (int) T.size(); ++t)
        {
            const auto& cs = T[(size_t) t].cats;
            if (c == CFx || c == CTexture || std::find (cs.begin(), cs.end(), c) != cs.end()) elig.push_back (t);
        }
        const auto& desc = descriptors()[(size_t) c];
        for (int k = 0; k < count; ++k)
        {
            const int ti = elig[(size_t) (k % (int) elig.size())];
            const auto& title = T[(size_t) ti];
            GenSpec g;
            g.category = c; g.bankIndex = title.bank; g.bank = bankName (title.bank); g.release = releaseName (title.bank);
            g.track = neutralNames ? neutralTitle (ti) : juce::String (title.name);
            const auto& d = desc[(k / (int) elig.size() + ti) % desc.size()];
            g.name = unique (g.track + " " + d);
            g.seed = (seed += 0x9E3779B9u);
            out.push_back (g);
        }
    }
    return out;
}

PresetData PresetGenerator::generate (const GenSpec& spec, int variant)
{
    PresetData d;
    B b (spec.seed + (uint32_t) variant * 7919u);
    switch (spec.category)
    {
        case CBassRoll: bassRolling (b); break;
        case CReese: bassReese (b); break;
        case CRaveLead: raveLead (b); break;
        case CMelLead: melancholicLead (b); break;
        case CPad: pad (b); break;
        case CVocal: vocal (b); break;
        case CPluck: pluck (b); break;
        case CRaveStab: raveStab (b); break;
        case CArp: arp (b); break;
        case CDrum: drum (b); break;
        case CBreak: breaks (b); break;
        case CFx: fxPreset (b); break;
        case CTexture: texture (b); break;
        case CModular: modular (b); break;
        default: scene (b, d); break;
    }
    if (variant > 0)   // QA re-roll: safer gain staging
    {
        b.set ("o1_on", 1); b.set ("o1_lvl", 0.8f);
        b.set ("f1_cut", juce::jmax (b.get ("f1_cut"), 1500.f));
        b.set ("e1_s", juce::jmax (b.get ("e1_s"), 0.5f));
    }
    d.params = b.p;
    d.engine = engineStateToTree (b.es);
    d.patch = patchToTree (b.patch);
    d.macroNames = b.macroNames;

    auto& m = d.meta;
    m.name = spec.name; m.bank = spec.bank; m.bankIndex = spec.bankIndex; m.track = spec.track; m.release = spec.release;
    m.category = categories()[spec.category];
    m.type = categoryType (spec.category);
    m.factory = true;
    m.macrosReady = true;
    m.mpe = b.get ("mpe") > 0.5f;
    int uni = 0; bool heavy = false;
    for (int o = 0; o < kNumOscs; ++o)
        if (b.get (b.o (o, "on")) > 0.5f)
        {
            uni += (int) b.get (b.o (o, "uni"));
            const int eng = (int) b.get (b.o (o, "eng"));
            if (eng == EngGranular || eng == EngSpectral) heavy = true;
        }
    m.cpuLight = uni <= 6 && ! heavy && b.get ("oversample") < 0.5f;

    static const std::vector<juce::StringArray> genres {
        { "UK Garage", "Bass" }, { "Bass", "Breakbeat" }, { "Breakbeat", "Techno" }, { "UK Garage", "Ambient" }, { "Ambient", "UK Garage" },
        { "UK Garage", "Breakbeat" }, { "UK Garage", "Techno" }, { "Techno", "Breakbeat" }, { "Techno" }, { "Breakbeat", "Techno" },
        { "Breakbeat" }, { "Techno", "Bass" }, { "Ambient" }, { "Techno", "Ambient" }, { "Techno", "UK Garage", "Breakbeat" } };
    static const std::vector<juce::StringArray> moods {
        { "Driving", "Dark", "Euphoric" }, { "Dark", "Driving" }, { "Euphoric", "Driving" }, { "Melancholy" }, { "Euphoric", "Melancholy" },
        { "Euphoric", "Melancholy" }, { "Euphoric", "Driving" }, { "Euphoric", "Driving" }, { "Driving", "Dark" }, { "Driving", "Dark" },
        { "Driving" }, { "Euphoric", "Dark" }, { "Melancholy", "Dark" }, { "Dark", "Driving" }, { "Euphoric", "Driving" } };
    m.genres = genres[(size_t) spec.category];
    const auto& md = moods[(size_t) spec.category];
    m.moods.add (md[b.r.range (md.size())]);
    m.tags = { spec.release, spec.track, m.type };
    if (m.macrosReady) m.tags.add ("Macros-ready");
    if (m.mpe) m.tags.add ("MPE");
    if (m.cpuLight) m.tags.add ("CPU-light");
    m.description = m.category + " preset from " + spec.bank + ".";
    return d;
}
} // namespace pd
