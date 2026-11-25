#pragma once
#include "Modules.h"
#include "Wavetable.h"
#include "SampleBank.h"

namespace pd
{
// Fast sine lookup (shared, built once).
struct SineTable
{
    static constexpr int N = 4096;
    float t[N + 1];
    SineTable() { for (int i = 0; i <= N; ++i) t[i] = std::sin (kTwoPi * (float) i / N); }
    static const SineTable& get() { static const SineTable s; return s; }
    float operator() (float phase) const noexcept   // phase in cycles
    {
        float p = phase - std::floor (phase);
        const float x = p * N; const int i = (int) x; const float f = x - (float) i;
        return t[i] + (t[i + 1] - t[i]) * f;
    }
};

// Everything a voice needs from the engine for one block. Built by SynthEngine.
struct BlockCtx
{
    const float* base = nullptr;       // plain parameter values (after global modulation)
    const float* baseNorm = nullptr;   // normalised 0..1
    const EngineState* es = nullptr;
    const MipTable* wt[kNumOscs] {};
    const Sample* smp[kNumOscs] {};
    const WavetableBank* bank = nullptr;
    float globalSrc[kNumModSources] {};
    bool lfoGlobal[kNumLfos] {};
    double lfoPhase[kNumLfos] {};       // shared phase for Free-mode LFOs (cycles)
    float renderRate = 48000.f;
    double bpm = 120.0;
    float riserPitch = 0.f;             // semitones
    float riserCutoffOct = 0.f;         // octaves
    bool eco = false;

    // Active modulation (prepared once per block)
    int numSlots = 0;
    int16_t slotSrc[kNumModSlots] {}, slotAux[kNumModSlots] {}, slotCurve[kNumModSlots] {}, slotDestJ[kNumModSlots] {};
    float slotAmt[kNumModSlots] {};
    int numDests = 0;
    int16_t dests[kNumModSlots] {};
};

class Voice
{
public:
    Voice();
    void prepare (double renderRate, int maxBlock);
    void setRenderRate (double renderRate);
    void start (int note, float velocity, int channel, bool legato, float glideFromPitch, const BlockCtx& ctx, uint32_t seed);
    void release() noexcept;
    void kill() noexcept;
    void steal (int note, float velocity, int channel, uint32_t seed) noexcept;
    bool isActive() const noexcept { return active; }
    bool isReleasing() const noexcept { return env[0].releasing(); }
    int getNote() const noexcept { return note; }
    int getChannel() const noexcept { return channel; }
    uint64_t getAge() const noexcept { return age; }
    void setAge (uint64_t a) noexcept { age = a; }
    float getLevel() const noexcept { return env[0].current(); }

    // MPE / per-note expression
    void setMpeBend (float semis) noexcept { mpeBend = semis; }
    void setMpePressure (float p) noexcept { mpePressure = p; }
    void setMpeTimbre (float t) noexcept { mpeTimbre = t; }
    void setPolyAftertouch (float a) noexcept { polyAT = a; }
    void setTargetNote (int n) noexcept { targetPitch = (float) n; note = n; }

    // Adds into L/R (n samples at render rate).
    void render (float* L, float* R, int n, const BlockCtx& ctx) noexcept;

    // UI feedback
    float lastOscPhase (int o) const noexcept { return osc[o].phase[0]; }
    float lastEnvValue (int e) const noexcept { return env[e].current(); }

private:
    struct Grain { double pos = 0, inc = 1; int age = 0, len = 0; float panL = 1, panR = 1; bool on = false; };
    static constexpr int kMaxGrains = 24;
    struct OscState
    {
        float phase[kMaxUnison] {};
        float drift[kMaxUnison] {};
        float driftTarget[kMaxUnison] {};
        double play[kMaxUnison] {};
        float ssPhase[7] {};
        Grain grains[kMaxGrains];
        double grainHead = 0; float grainAcc = 0;
        float partialPhase[kSpecPartials] {};
        float partialAmp[kSpecPartials] {};
        float partialInc[kSpecPartials] {};
        int numPartials = 0;
        double specHead = 0;
        float last = 0;
        bool finished = false;
        int sliceIdx = 0;
        // per tick
        float inc[kMaxUnison] {}, gainL[kMaxUnison] {}, gainR[kMaxUnison] {};
        int level = 0; float framePos = 0; int uni = 1; float warpAmt = 0; int warp = 0;
        float lvlL = 0, lvlR = 0, prevLvlL = 0, prevLvlR = 0;
        double sampleRatio = 1; int sStart = 0, sEnd = 0;
        float grainInc = 1; int grainLen = 1000; float grainSpray = 0; float grainDensity = 20; bool freeze = false;
    };

    void tick (const BlockCtx& ctx) noexcept;
    void computeMods (const BlockCtx& ctx) noexcept;
    float v (int idx) const noexcept { return val[(size_t) idx]; }
    void startOsc (int o, const BlockCtx& ctx) noexcept;
    float renderOsc (int o, const BlockCtx& ctx, float& outR) noexcept;

    std::vector<float> val;      // modulated plain values for this voice
    float src[kNumModSources] {};
    OscState osc[kNumOscs];
    VoiceFilter filt[kNumFilters][2];
    Envelope env[kNumEnvs];
    Lfo lfo[kNumLfos];
    NoiseGen noise;
    Rng rng;
    float subPhase = 0, subInc = 0;
    float sr = 48000.f;
    bool active = false;
    int note = 60, channel = 1;
    float velocity = 1, randomVal = 0;
    float currentPitch = 60, targetPitch = 60, glideCoef = 0;
    float mpeBend = 0, mpePressure = 0, mpeTimbre = 0, polyAT = 0;
    uint64_t age = 0;
    float ampPrev = 0, ampTarget = 0;
    float voicePan = 0;
    // stealing
    int stealCountdown = 0, pendingNote = -1, pendingChannel = 1; float pendingVel = 0; uint32_t pendingSeed = 0;
    bool needsStart = false;
    float glideTimeStored = 0;
};
} // namespace pd
