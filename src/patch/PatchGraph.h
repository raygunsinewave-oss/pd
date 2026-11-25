#pragma once
#include "../core/ModMatrix.h"
#include <vector>

namespace pd
{
enum PatchModuleType : int8_t
{
    PmVCO = 0, PmVCA, PmLFO, PmADSR, PmSampleHold, PmQuantizer, PmSlew, PmClock, PmEuclid, PmLogic,
    PmTuring, PmMixer, PmAttenuverter, PmDelay, PmWavefolder, PmBitcrusher, PmEnvFollower, PmMidiCV,
    PmMacroIn, PmParamOut, PmParamIn, PmPatchOut, PmAudioOut, PmSynthIn, PmNoise,
    kNumPatchModuleTypes
};

constexpr int kMaxPatchModules = 32;
constexpr int kPatchIO = 4;

struct PatchModuleInfo
{
    const char* name;
    const char* category;
    int numIn, numOut, numKnobs;
    const char* inNames[kPatchIO];
    const char* outNames[kPatchIO];
    const char* knobNames[kPatchIO];
    float knobDefaults[kPatchIO];
    const char* options;   // '|' separated option names or nullptr
};
const PatchModuleInfo& patchModuleInfo (int type);

struct PatchModuleDesc
{
    int8_t type = -1;
    int8_t option = 0;
    int16_t paramDest = -1;                 // ParamOut / ParamIn target
    float knob[kPatchIO] {};
    int8_t inMod[kPatchIO] { -1, -1, -1, -1 };
    int8_t inPort[kPatchIO] {};
    float x = 0, y = 0;                      // UI position (rack coordinates)
};

struct PatchProgram
{
    PatchModuleDesc m[kMaxPatchModules];
    int numUsed() const noexcept { int n = 0; for (auto& d : m) if (d.type >= 0) ++n; return n; }
};

juce::ValueTree patchToTree (const PatchProgram& p);
void patchFromTree (PatchProgram& p, const juce::ValueTree& t);

// Real-time patch interpreter.
class PatchEngine
{
public:
    struct Inputs
    {
        const float* synthL = nullptr; const float* synthR = nullptr;
        float note = 60, gate = 0, velocity = 0, modWheel = 0;
        const float* macros = nullptr;
        const float* baseNorm = nullptr;    // for ParamIn
        double ppq = 0, beatsPerSample = 0;
        bool playing = false;
    };
    struct ParamMod { int16_t param; float amount; };

    void prepare (double sampleRate);
    void setProgram (const PatchProgram& p) noexcept;
    // Process n samples; adds audio-out modules into outL/outR.
    void process (int n, const Inputs& in, float* outL, float* outR) noexcept;

    float patchOut[4] {};
    ParamMod paramMods[kMaxPatchModules] {};
    int numParamMods = 0;
    bool active() const noexcept { return used > 0; }

    std::atomic<float> meters[kMaxPatchModules][kPatchIO];

private:
    struct State
    {
        float out[kPatchIO] {};
        float s[8] {};
        double d = 0;
        int counter = 0, step = 0;
        uint16_t bits = 0xA5A5;
        bool lastGate[kPatchIO] {};
        int delayIdx = -1;
        float peak[kPatchIO] {};
    };
    float in (int m, int k) const noexcept
    {
        const auto& d = prog.m[m];
        const int src = d.inMod[k];
        return src >= 0 ? st[src].out[d.inPort[k]] : 0.f;
    }
    bool connected (int m, int k) const noexcept { return prog.m[m].inMod[k] >= 0; }

    PatchProgram prog;
    State st[kMaxPatchModules];
    std::vector<float> delayPool[8];
    int delayW[8] {};
    float sr = 48000.f;
    int used = 0;
    Rng rng { 0x51A7u };
    double lastSixteenth = -1;
};
} // namespace pd
