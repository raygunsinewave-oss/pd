#pragma once
// Pure Devotion — shared constants and real-time-safe DSP helpers.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace pd
{
constexpr int kMaxVoices      = 32;
constexpr int kMaxUnison      = 16;
constexpr int kNumOscs        = 3;
constexpr int kNumFilters     = 2;
constexpr int kNumEnvs        = 4;
constexpr int kNumLfos        = 8;
constexpr int kNumSeqs        = 2;
constexpr int kSeqSteps       = 16;
constexpr int kNumMacros      = 8;
constexpr int kNumModSlots    = 64;
constexpr int kNumScenes      = 4;
constexpr int kNumChopSlots   = 16;
constexpr int kControlRate    = 32;   // samples per modulation tick
constexpr int kMaxOversample  = 8;
constexpr float kTwoPi        = juce::MathConstants<float>::twoPi;
constexpr float kPi           = juce::MathConstants<float>::pi;

inline float clamp01 (float x) noexcept { return x < 0.f ? 0.f : (x > 1.f ? 1.f : x); }
inline float clampf (float x, float lo, float hi) noexcept { return x < lo ? lo : (x > hi ? hi : x); }
inline float lerp (float a, float b, float t) noexcept { return a + (b - a) * t; }

// Pade-style tanh approximation, accurate to ~1e-3 in [-3,3] and saturating beyond.
inline float fastTanh (float x) noexcept
{
    if (x > 3.f)  return 1.f;
    if (x < -3.f) return -1.f;
    const float x2 = x * x;
    return x * (27.f + x2) / (27.f + 9.f * x2);
}

inline float semitonesToRatio (float st) noexcept { return std::exp2 (st * (1.0f / 12.0f)); }
inline float midiToHz (float note) noexcept { return 440.0f * std::exp2 ((note - 69.0f) * (1.0f / 12.0f)); }
inline float dbToGain (float db) noexcept { return db <= -100.f ? 0.f : std::pow (10.f, db * 0.05f); }

// xorshift32 — deterministic, allocation-free RNG usable on the audio thread.
struct Rng
{
    uint32_t s = 0x9E3779B9u;
    explicit Rng (uint32_t seed = 0x9E3779B9u) noexcept : s (seed ? seed : 1u) {}
    uint32_t next() noexcept { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() noexcept { return (float) (next() >> 8) * (1.0f / 16777216.0f); }   // [0,1)
    float bi() noexcept { return uni() * 2.f - 1.f; }                                 // [-1,1)
    int range (int n) noexcept { return n <= 0 ? 0 : (int) (next() % (uint32_t) n); }
};

// One-pole smoother.
struct Smoother
{
    float y = 0, a = 0.99f;
    void setTime (float seconds, float sr) noexcept { a = seconds <= 0 ? 0.f : std::exp (-1.0f / (seconds * sr)); }
    float process (float x) noexcept { y = x + a * (y - x); return y; }
    void reset (float v) noexcept { y = v; }
};

// Host tempo / transport snapshot passed down to DSP each block.
struct Transport
{
    double bpm = 120.0;
    double ppqAtBlockStart = 0.0;
    bool playing = false;
    double sampleRate = 48000.0;
    double samplesPerBeat() const noexcept { return sampleRate * 60.0 / juce::jmax (20.0, bpm); }
};

// Tempo division table shared by LFOs, delays, arp, stutter etc. Values in beats (quarter notes).
inline const juce::StringArray& divisionNames()
{
    static const juce::StringArray n { "8/1", "4/1", "2/1", "1/1", "1/2", "1/2T", "1/4D", "1/4", "1/4T",
                                       "1/8D", "1/8", "1/8T", "1/16D", "1/16", "1/16T", "1/32", "1/64" };
    return n;
}
inline double divisionBeats (int idx) noexcept
{
    static constexpr double b[] = { 32, 16, 8, 4, 2, 4.0 / 3.0, 1.5, 1, 2.0 / 3.0,
                                    0.75, 0.5, 1.0 / 3.0, 0.375, 0.25, 1.0 / 6.0, 0.125, 0.0625 };
    return b[juce::jlimit (0, (int) std::size (b) - 1, idx)];
}

// Tiny seqlock for publishing POD state from the message thread to the audio
// thread without locks or allocation. Writer: message thread only.
template <typename T>
class SeqLocked
{
public:
    void write (const T& v) noexcept
    {
        seq.fetch_add (1, std::memory_order_acq_rel);   // odd: writing
        std::atomic_thread_fence (std::memory_order_release);
        data = v;
        std::atomic_thread_fence (std::memory_order_release);
        seq.fetch_add (1, std::memory_order_acq_rel);   // even: stable
    }
    // Returns true if a consistent copy was obtained (tries a few times, never blocks).
    bool tryRead (T& out, uint32_t& lastSeen) const noexcept
    {
        for (int attempt = 0; attempt < 4; ++attempt)
        {
            const auto s1 = seq.load (std::memory_order_acquire);
            if (s1 & 1u) continue;
            if (s1 == lastSeen) return false; // unchanged
            std::atomic_thread_fence (std::memory_order_acquire);
            out = data;
            std::atomic_thread_fence (std::memory_order_acquire);
            if (seq.load (std::memory_order_acquire) == s1) { lastSeen = s1; return true; }
        }
        return false;
    }
    T readCopyBlocking() const noexcept { T t; uint32_t z = 0xffffffffu; while (! tryRead (t, z)) {} return t; }
private:
    T data {};
    std::atomic<uint32_t> seq { 0 };
};
} // namespace pd
