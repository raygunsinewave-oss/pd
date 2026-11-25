#pragma once
#include "../core/ModMatrix.h"
#include "../dsp/SampleBank.h"

namespace pd
{
// ------------------------------------------------------------ chord / strum / arp
class ArpChord
{
public:
    void prepare (double sampleRate);
    void reset();
    // Transforms `midi` in place for one block.
    void process (juce::MidiBuffer& midi, int numSamples, const float* params, const EngineState& es, const Transport& tr);
    // UI: learn the currently held notes into chord memory.
    int heldNotes (int* out, int max) const noexcept;
    int currentStep() const noexcept { return stepCounter.load(); }

    static float grooveVelocity (int groove, int step) noexcept;

private:
    struct Pending { int64_t at; int note; int vel; bool on; };
    void schedule (int64_t at, int note, int vel, bool on) noexcept;
    void addHeld (int note, int vel) noexcept;
    void removeHeld (int note) noexcept;

    double sr = 48000;
    int64_t now = 0;
    Pending pending[512]; int numPending = 0;
    int held[64]; int heldVel[64]; int numHeld = 0;          // after chord expansion, in play order
    int physical[16]; int numPhysical = 0;
    int latched[64]; int numLatched = 0;
    bool latchArmed = false;
    double lastStepIndex = -1;
    int arpPos = 0, arpDir = 1;
    double freeBeats = 0;
    int soundingArp[8]; int numSounding = 0;
    Rng rng { 0xA4F0u };
    juce::MidiBuffer out;
    std::atomic<int> stepCounter { 0 };
};

// ------------------------------------------------------------ riser / drop
class RiserEngine
{
public:
    void prepare (double sampleRate) { sr = sampleRate; }
    // returns true if a "drop" happened this block
    bool process (int numSamples, bool armed, int barsChoice, const Transport& tr) noexcept;
    float progress() const noexcept { return p; }
    bool isActive() const noexcept { return active; }
    // noise swell generator (mono)
    float noiseSample (float amount) noexcept;
private:
    double sr = 48000, elapsedBeats = 0;
    bool active = false, wasArmed = false;
    float p = 0;
    Rng rng { 0x7155u };
    float bp1 = 0, bp2 = 0;
};

// ------------------------------------------------------------ stutter / tape stop
class BeatRepeat
{
public:
    void prepare (double sampleRate, int maxBlock);
    void process (float* L, float* R, int n, bool stutter, double stutterBeats, bool tapeStop, float stopTime, double bpm) noexcept;
private:
    std::vector<float> hL, hR;
    int w = 0, size = 0;
    double sr = 48000;
    bool wasStutter = false, wasStop = false;
    int loopStart = 0, loopLen = 0, loopPos = 0;
    double readPos = 0; float rate = 1.f;
    float xfade = 1.f;
};

// ------------------------------------------------------------ vocal chop pad
class ChopPlayer
{
public:
    void prepare (double sampleRate);
    void trigger (int slot, float velocity, const Sample* s, float pitchSt, float formantSt) noexcept;
    void process (float* L, float* R, int n, float level) noexcept;
    void stopAll() noexcept { for (auto& v : voices) v.on = false; }
    // UI feedback: last triggered slot and its age
    std::atomic<int> lastSlot { -1 };
private:
    struct Grain { double pos; float age, len; bool on; };
    struct V
    {
        bool on = false; const Sample* s = nullptr;
        double srcPos = 0, srcEnd = 0, srcRate = 1;
        float pitchRatio = 1, formantRatio = 1, periodSrc = 100, nextGrain = 0, vel = 1, env = 0;
        int age = 0;
        Grain g[8] {};
    };
    V voices[8];
    double sr = 48000;
    int rr = 0;
};
} // namespace pd
