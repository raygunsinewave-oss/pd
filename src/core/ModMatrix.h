#pragma once
#include "Params.h"

namespace pd
{
enum ModSource : int16_t
{
    SrcNone = 0,
    SrcLfo1, SrcLfo2, SrcLfo3, SrcLfo4, SrcLfo5, SrcLfo6, SrcLfo7, SrcLfo8,
    SrcEnv1, SrcEnv2, SrcEnv3, SrcEnv4,
    SrcSeq1, SrcSeq2,
    SrcVelocity, SrcNote, SrcModWheel, SrcPitchBend, SrcAftertouch,
    SrcMpePressure, SrcMpeTimbre, SrcRandom, SrcExpression,
    SrcMacro1, SrcMacro2, SrcMacro3, SrcMacro4, SrcMacro5, SrcMacro6, SrcMacro7, SrcMacro8,
    SrcPatch1, SrcPatch2, SrcPatch3, SrcPatch4,
    SrcRiser, SrcXYX, SrcXYY,
    kNumModSources
};

enum ModCurve : int16_t { CurveLinear = 0, CurveExp, CurveLog, CurveSCurve, CurveStepped, CurveInverted, kNumCurves };

const juce::StringArray& modSourceNames();
const juce::StringArray& modCurveNames();
bool isBipolarSource (int src) noexcept;
float applyCurve (int curve, float x) noexcept; // x in [-1,1] or [0,1]

struct ModSlot
{
    int16_t src = SrcNone;
    int16_t aux = SrcNone;   // optional multiplier source
    int16_t dest = -1;       // ParamTable index
    int16_t curve = CurveLinear;
};

constexpr int kMsegPoints = 16;
struct Mseg
{
    int numPoints = 4;
    int sustainPoint = 2;
    float t[kMsegPoints] { 0.f, 0.05f, 0.4f, 1.0f };   // seconds from start (monotonic)
    float y[kMsegPoints] { 0.f, 1.f, 0.6f, 0.f };
    float c[kMsegPoints] {};                           // curve per segment (-1..1)
};

constexpr int kLfoDrawPoints = 32;

// All non-automatable engine state. Plain-old-data: safe to copy through a seqlock.
struct EngineState
{
    ModSlot slots[kNumModSlots];
    float lfoDrawn[kNumLfos][kLfoDrawPoints] {};
    Mseg mseg[kNumEnvs];
    int8_t chordMemory[8] { 0, 3, 7, 10, 0, 0, 0, 0 };
    int numChordNotes = 4;
    float chopPitch[kNumChopSlots] {};      // semitones
    float chopFormant[kNumChopSlots] {};    // semitones
    int8_t fxOrder[kNumFx] { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
    int8_t macroCC[kNumMacros] { 20, 21, 22, 23, 24, 25, 26, 27 };
    bool macroCCOut = false;
    int16_t midiLearn[128];                 // CC -> param index (-1 = none)
    EngineState() noexcept
    {
        for (auto& m : midiLearn) m = -1;
        for (int l = 0; l < kNumLfos; ++l)
            for (int k = 0; k < kLfoDrawPoints; ++k)
                lfoDrawn[l][k] = std::sin (kTwoPi * (float) k / kLfoDrawPoints);
    }
};

// Value-tree <-> EngineState serialisation (message thread only).
juce::ValueTree engineStateToTree (const EngineState& s);
void engineStateFromTree (EngineState& s, const juce::ValueTree& t);
} // namespace pd
