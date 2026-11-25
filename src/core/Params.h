#pragma once
// Pure Devotion — parameter model.
// Every automatable parameter is described once in a ParamTable. The table
// builds the APVTS layout, supplies the mod-destination list, and fills the
// `Idx` struct of integer indices the DSP uses for fast lookups.
#include "Common.h"
#include <unordered_map>
#include <vector>

namespace pd
{
enum class PKind { Float, Int, Choice, Bool };

struct ParamInfo
{
    juce::String id, name, group;
    PKind kind = PKind::Float;
    juce::NormalisableRange<float> range;
    float def = 0.f;
    juce::StringArray choices;
    bool modulatable = true;
    juce::String unit;
};

namespace choices
{
    const juce::StringArray& oscEngines();
    const juce::StringArray& warpModes();
    const juce::StringArray& vaShapes();
    const juce::StringArray& sampleModes();
    const juce::StringArray& oscRoutes();
    const juce::StringArray& subShapes();
    const juce::StringArray& noiseTypes();
    const juce::StringArray& filterTypes();
    const juce::StringArray& filterRouting();
    const juce::StringArray& envModes();
    const juce::StringArray& lfoShapes();
    const juce::StringArray& lfoModes();
    const juce::StringArray& oversampling();
    const juce::StringArray& arpModes();
    const juce::StringArray& chordTypes();
    const juce::StringArray& grooves();
    const juce::StringArray& riserBars();
    const juce::StringArray& fxBus();
}

enum OscEngine { EngWavetable = 0, EngSample, EngGranular, EngSpectral, EngAnalog };
enum WarpMode { WarpOff = 0, WarpBendPlus, WarpBendMinus, WarpSync, WarpPWM, WarpMirror, WarpQuantize, WarpFM, WarpRM };
enum FilterType { FtLadder = 0, FtDiode, FtSvfLP, FtSvfBP, FtSvfHP, FtSvfNotch, FtSvfPeak, FtCombPlus, FtCombMinus, FtFormant, FtRaveLadder };

// --- FX definitions (generic: each effect has `on`, `bus` and up to 8 params) ---
enum FxType { FxTape = 0, FxCassette, FxChorus, FxPhaser, FxFlanger, FxReverb, FxShimmer, FxDelay,
              FxOTT, FxTransient, FxDistortion, FxFilter, FxEQ, FxDucker, FxLofi, FxWidener, kNumFx };
constexpr int kMaxFxParams = 8;

struct FxParamDef { const char* key; const char* name; float lo, hi, def, skewCentre; int numChoices; const char* choiceList; };
struct FxDef { const char* key; const char* name; int numParams; FxParamDef p[kMaxFxParams]; };
const FxDef& fxDef (int type);

// --- Index struct ----------------------------------------------------------
struct OscIdx  { int on, eng, lvl, pan, oct, semi, fine, table, pos, warp, warpAmt, uni, det, blend, width, drift,
                 phase, rphase, smp, slice, slmode, gdens, gsize, gspray, gfreeze, formant, tilt, va, pw, route, pmod; };
struct FiltIdx { int on, type, cut, res, pre, post, key, env, morph, mix; };
struct EnvIdx  { int a, h, d, s, r, curve, mode; };
struct LfoIdx  { int rate, sync, div, shape, mode, phase, smooth, delay, poly; };
struct SeqIdx  { int div, len, glide, step[kSeqSteps]; };
struct FxIdx   { int on, bus, p[kMaxFxParams]; };

struct Idx
{
    int masterVol, poly, glide, legato, bend, osamp, eco, mpe, velo, voiceSpread;
    OscIdx osc[kNumOscs];
    int subOn, subLvl, subOct, subShape, subRoute;
    int nzOn, nzType, nzLvl, nzColor, nzRoute;
    FiltIdx filt[kNumFilters];
    int filtRouting;
    EnvIdx env[kNumEnvs];
    LfoIdx lfo[kNumLfos];
    SeqIdx seq[kNumSeqs];
    int macro[kNumMacros];
    int modAmt[kNumModSlots];
    FxIdx fx[kNumFx];
    int busALvl, busBLvl;
    // perform
    int xyX, xyY, morphOn;
    int riserArm, riserBars, riserFilter, riserNoise, riserVerb, riserPitch, dropKill;
    int stutter, stutterDiv, tapeStop, tapeStopTime;
    int arpOn, arpMode, arpDiv, arpOct, arpGate, arpRatchet, arpProb, arpSwing, arpGroove, arpLatch;
    int chordOn, chordType, strum;
    int chopLvl, chopSample, chopChannel;
};

class ParamTable
{
public:
    static const ParamTable& get();
    const std::vector<ParamInfo>& all() const noexcept { return params; }
    int size() const noexcept { return (int) params.size(); }
    int indexOf (const juce::String& id) const;
    const ParamInfo& operator[] (int i) const { return params[(size_t) i]; }
    const Idx& idx() const noexcept { return ix; }
    // Mod destinations: indices of modulatable params (ordering is stable).
    const std::vector<int>& modDestinations() const noexcept { return modDests; }
    juce::AudioProcessorValueTreeState::ParameterLayout createLayout() const;

private:
    ParamTable();
    int add (ParamInfo p);
    int addFloat (juce::String id, juce::String name, juce::String group, float lo, float hi, float def,
                  float skewCentre = 0.f, juce::String unit = {}, bool mod = true);
    int addInt (juce::String id, juce::String name, juce::String group, int lo, int hi, int def, bool mod = false);
    int addChoice (juce::String id, juce::String name, juce::String group, const juce::StringArray& c, int def);
    int addBool (juce::String id, juce::String name, juce::String group, bool def);

    std::vector<ParamInfo> params;
    std::unordered_map<std::string, int> lookup;
    std::vector<int> modDests;
    Idx ix {};
};

inline const Idx& IX() noexcept { return ParamTable::get().idx(); }
} // namespace pd
