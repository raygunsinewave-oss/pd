#pragma once
#include "../core/Common.h"
#include <functional>
#include <memory>
#include <vector>

namespace pd
{
constexpr int kWtFrames     = 256;
constexpr int kWtSourceSize = 2048;   // time-domain size of a source frame
constexpr int kWtLevels     = 9;      // mip levels: 256,128,...,1 harmonics

// Band-limited, mip-mapped wavetable. Immutable once built (safe to read from any thread).
struct MipTable
{
    juce::String name;
    int numFrames = kWtFrames;
    int levelSize[kWtLevels] {};
    int levelHarm[kWtLevels] {};
    size_t levelOffset[kWtLevels] {};   // offset of frame 0 of each level
    std::vector<float> data;            // [level][frame][sample + 1 guard]

    const float* frame (int level, int f) const noexcept
    {
        return data.data() + levelOffset[level] + (size_t) f * (size_t) (levelSize[level] + 1);
    }
    // Pick the mip level for a phase increment (cycles per sample).
    int levelFor (float inc) const noexcept
    {
        const float allowed = 0.5f / juce::jmax (1.0e-7f, inc);   // harmonics below Nyquist
        for (int l = 0; l < kWtLevels; ++l)
            if ((float) levelHarm[l] <= allowed) return l;
        return kWtLevels - 1;
    }
    // framePos in [0,1], phase in [0,1)
    float sample (int level, float framePos, float phase) const noexcept
    {
        const float fp = clamp01 (framePos) * (float) (numFrames - 1);
        const int f0 = (int) fp;
        const int f1 = juce::jmin (f0 + 1, numFrames - 1);
        const float ft = fp - (float) f0;
        const int n = levelSize[level];
        const float x = phase * (float) n;
        int i = (int) x; const float t = x - (float) i; i &= (n - 1);
        const float* a = frame (level, f0);
        const float* b = frame (level, f1);
        const float va = a[i] + (a[i + 1] - a[i]) * t;
        const float vb = b[i] + (b[i + 1] - b[i]) * t;
        return va + (vb - va) * ft;
    }
};

// Builds a MipTable from `numFrames` time-domain frames of `frameSize` samples each.
std::unique_ptr<MipTable> buildMipTable (const juce::String& name, const float* frames, int numFrames, int frameSize);

// Shared bank of factory + user wavetables. Factory tables are generated
// procedurally (original content) and built lazily on a background thread.
class WavetableBank : private juce::Thread
{
public:
    static constexpr int kMaxTables = 64;
    WavetableBank();
    ~WavetableBank() override;

    int numTables() const noexcept { return count.load(); }
    juce::String tableName (int i) const;
    // Audio-thread safe. Returns the table if built, otherwise requests a build
    // and returns the always-available fallback (Basic Shapes).
    const MipTable* acquire (int index) noexcept;
    // Audio-thread safe. Returns nullptr (and requests a background build) if not built yet.
    const MipTable* tryAcquire (int index) noexcept;
    // Blocking build (message thread / tools).
    const MipTable* getBlocking (int index);
    // Import frames as a new user table; returns its index or -1 (message thread).
    int addUserTable (const juce::String& name, const std::vector<float>& frames, int numFrames, int frameSize);
    // Replace a user/edited table (wavetable editor). Old table is retired, not freed, until shutdown.
    void replaceTable (int index, std::unique_ptr<MipTable> t);

    // Basic single-frame band-limited shapes for the virtual-analog engine.
    const MipTable& saw() const noexcept { return *vaSaw; }
    const MipTable& tri() const noexcept { return *vaTri; }
    const MipTable& sine() const noexcept { return *vaSine; }

    // Generate time-domain frames of a factory table (also used by PresetForge export).
    static int numFactoryTables();
    static juce::String factoryName (int i);
    static void generateFactory (int i, std::vector<float>& outFrames);   // kWtFrames * kWtSourceSize

private:
    void run() override;
    std::atomic<MipTable*> tables[kMaxTables] {};
    juce::String names[kMaxTables];
    std::atomic<bool> requested[kMaxTables] {};
    std::vector<std::unique_ptr<MipTable>> owned;   // all built tables (never freed while running)
    juce::CriticalSection ownedLock;                // message/build threads only
    std::atomic<int> count { 0 };
    std::unique_ptr<MipTable> vaSaw, vaTri, vaSine;
    juce::WaitableEvent wake;
};
} // namespace pd
