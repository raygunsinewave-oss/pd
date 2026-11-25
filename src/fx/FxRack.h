#pragma once
#include "../core/ModMatrix.h"
#include <memory>

namespace pd
{
struct FxCtx
{
    double bpm = 120, ppq = 0, sampleRate = 48000;
    bool playing = false;
    const float* scL = nullptr;   // host sidechain (may be null)
    const float* scR = nullptr;
    float reverbThrow = 0.f;      // riser reverb throw (0..1)
    float delayThrow = 0.f;
};

class FxUnit
{
public:
    virtual ~FxUnit() = default;
    virtual void prepare (double sr, int maxBlock) = 0;
    virtual void reset() = 0;
    // p: plain parameter values for this effect (kMaxFxParams)
    virtual void process (float* L, float* R, int n, const float* p, const FxCtx& ctx) = 0;
    virtual bool isTailEffect() const { return false; }
};

class FxRack
{
public:
    FxRack();
    ~FxRack();
    void prepare (double sr, int maxBlock);
    void reset();
    void killTails();   // drop: clears reverb/delay/shimmer buffers
    void process (juce::AudioBuffer<float>& buffer, int numSamples, const float* params, const EngineState& es, const FxCtx& ctx);

private:
    std::unique_ptr<FxUnit> units[kNumFx];
    juce::AudioBuffer<float> busB;
    double sampleRate = 48000;
};

std::unique_ptr<FxUnit> createFx (int type);
} // namespace pd
