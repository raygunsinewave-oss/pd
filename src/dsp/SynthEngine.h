#pragma once
#include "Voice.h"
#include <memory>

namespace pd
{
// Visual/meter feed for the UI (written on the audio thread, read by the editor).
struct VisualFeed
{
    std::atomic<float> rms { 0 }, peak { 0 }, env { 0 }, lfoRate { 1 }, pitch { 60 }, tempo { 120 };
    std::atomic<int> transients { 0 };       // increments on each detected transient
    std::atomic<float> oscPhase[kNumOscs] {};
    std::atomic<int> activeVoices { 0 };
    std::atomic<float> modValue[kNumModSources] {};
    std::atomic<float> riser { 0 };
    static constexpr int kScopeSize = 256;
    std::atomic<float> scope[kScopeSize] {};
    std::atomic<int> noteOnCount { 0 };
};

class SynthEngine
{
public:
    SynthEngine (WavetableBank& wt, SampleBank& smp);
    void prepare (double sampleRate, int maxBlock);
    void reset();
    int latencyForOversampling (int osIndex) const;

    struct Inputs
    {
        float* base = nullptr;            // plain values (engine may modify for global mods)
        float* baseNorm = nullptr;
        const EngineState* es = nullptr;
        Transport tr;
        const float* patchOut = nullptr;  // 4 values from the patch engine (previous block)
        float riser = 0, riserPitch = 0, riserCutoffOct = 0;
    };
    // Renders `n` samples (base rate) into out (cleared first).
    void process (juce::AudioBuffer<float>& out, juce::MidiBuffer& midi, int n, Inputs& in);

    VisualFeed& feed() noexcept { return vis; }
    float lastNote() const noexcept { return lastNotePitch; }
    float lastGate() const noexcept { return gateOn ? 1.f : 0.f; }
    float lastVelocity() const noexcept { return lastVel; }
    float modWheel() const noexcept { return modWheelVal; }
    void allNotesOff() noexcept;

private:
    void handleMidi (const juce::MidiMessage& m, const BlockCtx& ctx);
    void noteOn (int note, float vel, int ch, const BlockCtx& ctx);
    void noteOff (int note, int ch, const BlockCtx& ctx);
    void renderVoices (int startOs, int numOs, const BlockCtx& ctx);
    void buildCtx (BlockCtx& ctx, Inputs& in, int n);
    void updateGlobalSources (BlockCtx& ctx, Inputs& in, int n);

    WavetableBank& wtBank;
    SampleBank& smpBank;
    Voice voices[kMaxVoices];
    Lfo globalLfo[kNumLfos];
    std::unique_ptr<juce::dsp::Oversampling<float>> os[3];   // 2x, 4x, 8x
    juce::AudioBuffer<float> renderBuf;
    double sr = 48000; int maxBlockSize = 512;
    int osIndex = 0;
    uint64_t ageCounter = 0;
    uint32_t seedCounter = 1;
    std::vector<bool> isVoiceParam;
    const MipTable* lastWt[kNumOscs] {};   // keeps playing the previous table while a new one builds

    // MIDI state
    float pitchBend = 0, modWheelVal = 0, aftertouch = 0, expression = 1;
    bool sustain = false;
    bool sustained[128] {};
    float chanBend[17] {}, chanPressure[17] {}, chanTimbre[17] {};
    int monoStack[32]; int monoCount = 0;
    float lastNotePitch = 60, lastVel = 0;
    bool gateOn = false;
    int heldCount = 0;

    // global time
    double freeSeconds = 0, freeBeats = 0;
    float seqValue[kNumSeqs] {};
    VisualFeed vis;
    float rmsAcc = 0, envFollow = 0, slowFollow = 0;
    int scopeW = 0;
};
} // namespace pd
