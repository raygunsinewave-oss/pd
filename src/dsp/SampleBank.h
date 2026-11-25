#pragma once
#include "../core/Common.h"
#include <memory>
#include <vector>

namespace pd
{
constexpr int kSpecFrames = 64;
constexpr int kSpecPartials = 64;
constexpr int kMaxSlices = 16;

struct Sample
{
    juce::String name, category;
    std::vector<float> L, R;          // R empty => mono
    double sampleRate = 48000.0;
    float rootNote = 60.f;
    float rootHz = 261.63f;
    bool loopable = false;
    int numSlices = 1;
    int sliceStart[kMaxSlices] {};
    float spec[kSpecFrames][kSpecPartials] {};  // harmonic magnitudes for spectral resynthesis

    int length() const noexcept { return (int) L.size(); }
    bool stereo() const noexcept { return ! R.empty(); }
    int sliceEnd (int s) const noexcept { return s + 1 < numSlices ? sliceStart[s + 1] : length(); }

    // 4-point Hermite read (pos in samples), channel 0/1.
    float read (int ch, double pos) const noexcept
    {
        const auto& d = (ch == 1 && stereo()) ? R : L;
        const int n = (int) d.size();
        if (n < 4) return 0.f;
        const int i = (int) pos;
        if (i < 1 || i >= n - 2) return (i >= 0 && i < n) ? d[(size_t) i] : 0.f;
        const float t = (float) (pos - (double) i);
        const float xm1 = d[(size_t) i - 1], x0 = d[(size_t) i], x1 = d[(size_t) i + 1], x2 = d[(size_t) i + 2];
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * t + c2) * t + c1) * t + x0;
    }
};

// Analysis helpers (message/background thread).
float detectPitchHz (const Sample& s);
void autoSlice (Sample& s, int maxSlices);   // transient-based chopper
void analyseSpectrum (Sample& s);

class SampleBank : private juce::Thread
{
public:
    static constexpr int kMaxSamples = 64;
    SampleBank();
    ~SampleBank() override;

    int numSamples() const noexcept { return count.load(); }
    const Sample* get (int i) const noexcept
    {
        return (i >= 0 && i < kMaxSamples) ? samples[i].load (std::memory_order_acquire) : nullptr;
    }
    juce::String name (int i) const { auto* s = get (i); return s ? s->name : juce::String ("—"); }
    bool isReady() const noexcept { return ready.load(); }
    void waitUntilReady() const { while (! ready.load()) juce::Thread::sleep (5); }
    // Loads an audio file as a user sample (message thread). Returns index or -1.
    int loadUserSample (const juce::File& f);
    int addSample (std::unique_ptr<Sample> s);

    static constexpr int kNumFactory = 45;
private:
    void run() override;
    std::atomic<Sample*> samples[kMaxSamples] {};
    std::vector<std::unique_ptr<Sample>> owned;
    juce::CriticalSection lock;
    std::atomic<int> count { 0 };
    std::atomic<bool> ready { false };
};

// Procedural factory content (all original, synthesised in code).
namespace SampleFactory
{
    juce::StringArray names();
    std::unique_ptr<Sample> make (int index, double sr = 48000.0);
}
} // namespace pd
