#include "Wavetable.h"

namespace pd
{
// ---------------------------------------------------------------- mip build
static int levelSizeFor (int l) { return juce::jmax (64, 1024 >> l); }
static int levelHarmFor (int l) { return 256 >> l; }

std::unique_ptr<MipTable> buildMipTable (const juce::String& name, const float* frames, int numFrames, int frameSize)
{
    numFrames = juce::jlimit (1, kWtFrames, numFrames);
    auto t = std::make_unique<MipTable>();
    t->name = name;
    t->numFrames = numFrames;
    size_t total = 0;
    for (int l = 0; l < kWtLevels; ++l)
    {
        t->levelSize[l] = levelSizeFor (l);
        t->levelHarm[l] = levelHarmFor (l);
        t->levelOffset[l] = total;
        total += (size_t) numFrames * (size_t) (t->levelSize[l] + 1);
    }
    t->data.assign (total, 0.f);

    constexpr int N = kWtSourceSize;
    juce::dsp::FFT fwd (11);   // 2048
    std::unique_ptr<juce::dsp::FFT> inv[kWtLevels];
    for (int l = 0; l < kWtLevels; ++l)
        inv[l] = std::make_unique<juce::dsp::FFT> ((int) std::round (std::log2 ((double) t->levelSize[l])));

    std::vector<float> buf ((size_t) N * 2), spec ((size_t) N * 2), lv (2048 * 2);
    std::vector<float> amp2 (N / 2 + 1);

    for (int f = 0; f < numFrames; ++f)
    {
        // resample the source frame to 2048 samples (cyclic linear)
        const float* src = frames + (size_t) f * (size_t) frameSize;
        for (int i = 0; i < N; ++i)
        {
            const float x = (float) i * (float) frameSize / (float) N;
            const int i0 = (int) x; const float tt = x - (float) i0;
            buf[(size_t) i] = src[i0 % frameSize] * (1 - tt) + src[(i0 + 1) % frameSize] * tt;
        }
        std::fill (buf.begin() + N, buf.end(), 0.f);
        fwd.performRealOnlyForwardTransform (buf.data(), true);
        std::copy (buf.begin(), buf.end(), spec.begin());
        for (int k = 0; k <= N / 2; ++k)
        {
            const float re = spec[(size_t) (2 * k)], im = spec[(size_t) (2 * k + 1)];
            const float a = 2.f * std::sqrt (re * re + im * im) / (float) N;
            amp2[(size_t) k] = a * a;
        }
        float peak0 = 1.f;
        for (int l = 0; l < kWtLevels; ++l)
        {
            const int n = t->levelSize[l];
            const int H = juce::jmin (t->levelHarm[l], n / 2 - 1);
            std::fill (lv.begin(), lv.begin() + 2 * n, 0.f);
            double expectedPow = 0;
            for (int k = 1; k <= H; ++k)
            {
                lv[(size_t) (2 * k)] = spec[(size_t) (2 * k)];
                lv[(size_t) (2 * k + 1)] = spec[(size_t) (2 * k + 1)];
                lv[(size_t) (2 * (n - k))] = spec[(size_t) (2 * k)];
                lv[(size_t) (2 * (n - k) + 1)] = -spec[(size_t) (2 * k + 1)];
                expectedPow += 0.5 * amp2[(size_t) k];
            }
            inv[l]->performRealOnlyInverseTransform (lv.data());
            double pow = 0;
            for (int i = 0; i < n; ++i) pow += (double) lv[(size_t) i] * lv[(size_t) i];
            pow /= n;
            const float scale = pow > 1e-20 ? (float) std::sqrt (expectedPow / pow) : 0.f;
            float* dst = t->data.data() + t->levelOffset[l] + (size_t) f * (size_t) (n + 1);
            for (int i = 0; i < n; ++i) dst[i] = lv[(size_t) i] * scale;
            if (l == 0)
            {
                float pk = 0; for (int i = 0; i < n; ++i) pk = juce::jmax (pk, std::abs (dst[i]));
                peak0 = pk > 1e-6f ? 1.f / pk : 1.f;
            }
            for (int i = 0; i < n; ++i) dst[i] *= peak0;
            dst[n] = dst[0];   // guard sample for interpolation
        }
    }
    return t;
}

// ---------------------------------------------------------------- factory content
namespace
{
constexpr int N = kWtSourceSize;

struct Formant { float f, a, bw; };
const Formant kVowels[5][3] = {
    { { 730, 1.0f, 90 }, { 1090, 0.5f, 110 }, { 2440, 0.25f, 160 } },  // A
    { { 530, 1.0f, 70 }, { 1840, 0.45f, 120 }, { 2480, 0.3f, 160 } },  // E
    { { 270, 1.0f, 60 }, { 2290, 0.35f, 120 }, { 3010, 0.3f, 180 } },  // I
    { { 570, 1.0f, 80 }, { 840, 0.6f, 90 },  { 2410, 0.15f, 160 } },   // O
    { { 300, 1.0f, 60 }, { 870, 0.4f, 90 },  { 2240, 0.1f, 160 } },    // U
};

float vowelEnv (float hz, float morph /*0..4*/)
{
    const int a = juce::jlimit (0, 4, (int) morph);
    const int b = juce::jmin (4, a + 1);
    const float t = morph - (float) a;
    float r = 0;
    for (int i = 0; i < 3; ++i)
    {
        const float f = lerp (kVowels[a][i].f, kVowels[b][i].f, t);
        const float am = lerp (kVowels[a][i].a, kVowels[b][i].a, t);
        const float bw = lerp (kVowels[a][i].bw, kVowels[b][i].bw, t);
        const float d = (hz - f) / bw;
        r += am * std::exp (-0.5f * d * d);
    }
    return r + 0.01f;
}

void additive (float* out, const float* amp, int maxH, const float* phase = nullptr)
{
    std::fill (out, out + N, 0.f);
    for (int k = 1; k <= maxH; ++k)
    {
        const float a = amp[k];
        if (std::abs (a) < 1e-6f) continue;
        const float ph = phase ? phase[k] : 0.f;
        const float w = kTwoPi * (float) k / (float) N;
        for (int i = 0; i < N; ++i) out[i] += a * std::sin (w * (float) i + ph);
    }
}

float naiveSaw (float x) { x -= std::floor (x); return 2.f * x - 1.f; }
float naiveSquare (float x, float pw = 0.5f) { x -= std::floor (x); return x < pw ? 1.f : -1.f; }
float naiveTri (float x) { x -= std::floor (x); return 1.f - 4.f * std::abs (x - 0.5f); }
float fold (float x) { while (x > 1.f || x < -1.f) x = x > 1.f ? 2.f - x : -2.f - x; return x; }

const char* kFactoryNames[] = {
    "Basic Shapes", "Raygun PWM", "Harmonic Climb", "Vowel Morph", "Hoover Core", "Reese Core",
    "Organ Drawbars", "FM Bright", "FM Metal", "Sync Sweep", "Wavefolder", "Crushed",
    "Choir Air", "Glass Bell", "Saw Tilt", "Square Filter", "Tape Keys", "Growl Formant",
    "Comb Saw", "Phase Distort", "Spectral Drift", "Sub Weight", "Tri Fold", "Pluck Close",
    "Window Sync", "Sinc Pulse", "Half Wave", "Odd / Even", "Metallic Spikes", "Ee-Oo Throat",
    "Additive Seed", "Wobble Sine" };
constexpr int kNumFactory = (int) std::size (kFactoryNames);

void generateFrame (int id, int f, float* out)
{
    const float t = (float) f / (float) (kWtFrames - 1);
    static thread_local float amp[1025], ph[1025];
    std::fill (std::begin (amp), std::end (amp), 0.f);
    std::fill (std::begin (ph), std::end (ph), 0.f);
    auto td = [&] (auto fn) { for (int i = 0; i < N; ++i) out[i] = fn ((float) i / (float) N); };

    switch (id)
    {
        case 0: td ([t] (float x) {   // sine -> tri -> saw -> square
            const float s = std::sin (kTwoPi * x), tr = naiveTri (x + 0.25f), sw = naiveSaw (x + 0.5f), sq = naiveSquare (x);
            const float seg = t * 3.f; const int k = juce::jmin (2, (int) seg); const float u = seg - (float) k;
            const float a[4] = { s, tr, sw, sq }; return lerp (a[k], a[k + 1], u); }); break;
        case 1: td ([t] (float x) { return naiveSquare (x, lerp (0.5f, 0.03f, t)); }); break;
        case 2: { const int H = 1 + (int) (t * 127); for (int k = 1; k <= H; ++k) amp[k] = 1.f / (float) k; additive (out, amp, H); } break;
        case 3: for (int k = 1; k <= 200; ++k) amp[k] = vowelEnv (110.f * (float) k, t * 4.f) / std::sqrt ((float) k); additive (out, amp, 200); break;
        case 4: td ([t] (float x) { return 0.55f * naiveSaw (x) + 0.45f * naiveSquare (x + 0.13f, 0.5f - 0.42f * t)
                                          + 0.35f * naiveSaw (2.f * x + t * 0.5f) + 0.2f * std::sin (kTwoPi * 0.5f * x * 2.f); }); break;
        case 5: td ([t] (float x) { return 0.5f * (naiveSaw (x) + naiveSaw (x + 0.5f * t + 0.003f)); }); break;
        case 6: {
            static const int h[] = { 1, 2, 3, 4, 6, 8, 10, 12 };
            const float drawA[] = { 1, 0.8f, 0.6f, 0.5f, 0.2f, 0.4f, 0.1f, 0.1f };
            const float drawB[] = { 0.7f, 1, 0.2f, 0.9f, 0.7f, 0.8f, 0.5f, 0.6f };
            for (int i = 0; i < 8; ++i) amp[h[i]] = lerp (drawA[i], drawB[i], t);
            additive (out, amp, 12); } break;
        case 7: td ([t] (float x) { return std::sin (kTwoPi * x + t * 8.f * std::sin (kTwoPi * 2.f * x)); }); break;
        case 8: td ([t] (float x) { const float m = std::sin (kTwoPi * 3.f * x + 0.5f * t * std::sin (kTwoPi * x));
                                    return std::sin (kTwoPi * x + t * 6.f * m); }); break;
        case 9: td ([t] (float x) { const float r = 1.f + 7.f * t; return naiveSaw (x * r) * (1.f - x * 0.3f); }); break;
        case 10: td ([t] (float x) { return fold (std::sin (kTwoPi * x) * (1.f + 7.f * t)); }); break;
        case 11: td ([t] (float x) { const float steps = 2.f + 30.f * (1.f - t); return std::round (std::sin (kTwoPi * x) * steps) / steps; }); break;
        case 12: { Rng r (77); for (int k = 1; k <= 160; ++k) { amp[k] = vowelEnv (130.f * (float) k, 4.f - t * 4.f) * (0.6f + 0.4f * r.uni()) / std::sqrt ((float) k); ph[k] = r.uni() * kTwoPi; }
                   additive (out, amp, 160, ph); } break;
        case 13: { static const int h[] = { 1, 3, 5, 7, 11, 14, 19, 23 };
                   for (int i = 0; i < 8; ++i) amp[h[i]] = std::exp (-(float) i * (1.2f - t)) * (i % 2 ? 0.7f + 0.3f * t : 1.f);
                   additive (out, amp, 23); } break;
        case 14: { const float p = lerp (2.0f, 0.65f, t); for (int k = 1; k <= 256; ++k) amp[k] = std::pow ((float) k, -p); additive (out, amp, 256); } break;
        case 15: { const float kc = 2.f + t * 100.f; for (int k = 1; k <= 255; k += 2) amp[k] = (1.f / (float) k) / (1.f + std::pow ((float) k / kc, 4.f)); additive (out, amp, 255); } break;
        case 16: for (int k = 1; k <= 64; ++k) amp[k] = (1.f / (float) k) * std::exp (-t * (float) k * 0.35f) * (k == 2 ? 1.4f : 1.f); additive (out, amp, 64); break;
        case 17: { const float fc = lerp (300.f, 2500.f, t);
                   for (int k = 1; k <= 200; ++k) { const float hz = 90.f * (float) k; const float d = (hz - fc) / 250.f;
                       amp[k] = (1.f / (float) k) * (0.25f + 2.5f * std::exp (-0.5f * d * d)); }
                   additive (out, amp, 200); } break;
        case 18: { const float d = t * 0.25f; for (int k = 1; k <= 256; ++k) amp[k] = (1.f / (float) k) * (0.5f + 0.5f * std::cos (kTwoPi * (float) k * d)); additive (out, amp, 256); } break;
        case 19: td ([t] (float x) { const float d = lerp (0.5f, 0.02f, t);
                                     const float xp = x < d ? x * 0.5f / d : 0.5f + (x - d) * 0.5f / (1.f - d); return -std::cos (kTwoPi * xp); }); break;
        case 20: { // interpolate between 8 random spectral keyframes
            const float kf = t * 7.f; const int a = juce::jmin (6, (int) kf); const float u = kf - (float) a;
            Rng ra (1000 + (uint32_t) a), rb (1001 + (uint32_t) a);
            for (int k = 1; k <= 128; ++k) { const float va = ra.uni(), vb = rb.uni(); amp[k] = lerp (va * va, vb * vb, u) / std::sqrt ((float) k); }
            additive (out, amp, 128); } break;
        case 21: amp[1] = 1.f; amp[2] = 0.5f * t; amp[3] = 0.3f * t * t; amp[4] = 0.1f * t; additive (out, amp, 4); break;
        case 22: td ([t] (float x) { return fold (naiveTri (x) * (1.f + 5.f * t)); }); break;
        case 23: { const float kc = 1.f + 64.f * (1.f - t); for (int k = 1; k <= 256; ++k) amp[k] = (1.f / (float) k) / (1.f + std::pow ((float) k / kc, 4.f)); additive (out, amp, 256); } break;
        case 24: td ([t] (float x) { const float r = 1.f + 11.f * t; return std::sin (kTwoPi * x * r) * (0.5f - 0.5f * std::cos (kTwoPi * x)); }); break;
        case 25: td ([t] (float x) { const float w = lerp (0.25f, 0.01f, t); const float u = (x - 0.5f) / w;
                                     return std::abs (u) < 1e-4f ? 1.f : std::sin (kPi * u) / (kPi * u); }); break;
        case 26: td ([t] (float x) { const float s = std::sin (kTwoPi * x); return fastTanh ((s > 0 ? s : 0.f) * (1.f + 6.f * t)) * 2.f - 0.6f; }); break;
        case 27: for (int k = 1; k <= 128; ++k) amp[k] = (1.f / (float) k) * ((k % 2) ? 1.f - 0.8f * t : 0.2f + 0.8f * t); additive (out, amp, 128); break;
        case 28: { static const int pr[] = { 1, 2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31 };
                   for (int i = 0; i < 12; ++i) { const float c = t * 11.f; const float d = ((float) i - c); amp[pr[i]] = std::exp (-0.3f * d * d) + 0.05f; }
                   additive (out, amp, 31); } break;
        case 29: for (int k = 1; k <= 200; ++k) amp[k] = vowelEnv (100.f * (float) k, lerp (2.f, 4.f, t)) / std::sqrt ((float) k); additive (out, amp, 200); break;
        case 30: { Rng r (4242); for (int k = 1; k <= 96; ++k) { const float a = r.uni(), b = r.uni(); amp[k] = lerp (a, b, t) * std::pow ((float) k, -0.8f); ph[k] = r.uni() * kTwoPi; }
                   additive (out, amp, 96, ph); } break;
        case 31: td ([t] (float x) { const float u = std::sin (kPi * t); const float d = lerp (0.5f, 0.05f, u);
                                     const float xp = x < d ? x * 0.5f / d : 0.5f + (x - d) * 0.5f / (1.f - d); return std::sin (kTwoPi * xp); }); break;
        default: td ([] (float x) { return std::sin (kTwoPi * x); }); break;
    }
}
} // namespace

int WavetableBank::numFactoryTables() { return kNumFactory; }
juce::String WavetableBank::factoryName (int i) { return i >= 0 && i < kNumFactory ? kFactoryNames[i] : juce::String(); }

void WavetableBank::generateFactory (int id, std::vector<float>& frames)
{
    frames.resize ((size_t) kWtFrames * N);
    for (int f = 0; f < kWtFrames; ++f)
        generateFrame (id, f, frames.data() + (size_t) f * N);
}

// ---------------------------------------------------------------- bank
WavetableBank::WavetableBank() : juce::Thread ("PD Wavetable Builder")
{
    for (int i = 0; i < kNumFactory; ++i) names[i] = kFactoryNames[i];
    count = kNumFactory;

    // VA shapes (single frame)
    std::vector<float> f (N);
    for (int i = 0; i < N; ++i) f[(size_t) i] = naiveSaw ((float) i / N + 0.5f);
    vaSaw = buildMipTable ("Saw", f.data(), 1, N);
    for (int i = 0; i < N; ++i) f[(size_t) i] = naiveTri ((float) i / N + 0.25f);
    vaTri = buildMipTable ("Tri", f.data(), 1, N);
    for (int i = 0; i < N; ++i) f[(size_t) i] = std::sin (kTwoPi * (float) i / N);
    vaSine = buildMipTable ("Sine", f.data(), 1, N);

    getBlocking (0); // fallback table always available
    startThread (juce::Thread::Priority::low);
}

WavetableBank::~WavetableBank()
{
    signalThreadShouldExit();
    wake.signal();
    stopThread (4000);
}

juce::String WavetableBank::tableName (int i) const
{
    return i >= 0 && i < count.load() ? names[i] : juce::String ("—");
}

const MipTable* WavetableBank::acquire (int index) noexcept
{
    index = juce::jlimit (0, count.load() - 1, index);
    if (auto* t = tables[index].load (std::memory_order_acquire))
        return t;
    if (! requested[index].exchange (true))
        wake.signal();
    return tables[0].load (std::memory_order_acquire);
}

const MipTable* WavetableBank::tryAcquire (int index) noexcept
{
    index = juce::jlimit (0, count.load() - 1, index);
    if (auto* t = tables[index].load (std::memory_order_acquire)) return t;
    if (! requested[index].exchange (true)) wake.signal();
    return nullptr;
}

const MipTable* WavetableBank::getBlocking (int index)
{
    index = juce::jlimit (0, count.load() - 1, index);
    if (auto* t = tables[index].load()) return t;
    const juce::ScopedLock sl (ownedLock);
    if (auto* t = tables[index].load()) return t;
    if (index >= kNumFactory) return tables[0].load();
    std::vector<float> frames;
    generateFactory (index, frames);
    auto built = buildMipTable (names[index], frames.data(), kWtFrames, N);
    auto* raw = built.get();
    owned.push_back (std::move (built));
    tables[index].store (raw, std::memory_order_release);
    return raw;
}

int WavetableBank::addUserTable (const juce::String& name, const std::vector<float>& frames, int numFrames, int frameSize)
{
    const juce::ScopedLock sl (ownedLock);
    // reuse a slot with the same name
    int slot = -1;
    for (int i = kNumFactory; i < count.load(); ++i) if (names[i] == name) slot = i;
    if (slot < 0)
    {
        if (count.load() >= kMaxTables) return -1;
        slot = count.load();
    }
    auto built = buildMipTable (name, frames.data(), numFrames, frameSize);
    names[slot] = name;
    tables[slot].store (built.get(), std::memory_order_release);
    owned.push_back (std::move (built));
    if (slot == count.load()) count = slot + 1;
    return slot;
}

void WavetableBank::replaceTable (int index, std::unique_ptr<MipTable> t)
{
    if (index < 0 || index >= count.load() || t == nullptr) return;
    const juce::ScopedLock sl (ownedLock);
    tables[index].store (t.get(), std::memory_order_release);
    owned.push_back (std::move (t));   // retired tables stay alive: audio thread may still read them
}

void WavetableBank::run()
{
    while (! threadShouldExit())
    {
        wake.wait (500);
        for (int i = 0; i < count.load() && ! threadShouldExit(); ++i)
            if (requested[i].load() && tables[i].load() == nullptr)
            {
                getBlocking (i);
                requested[i] = false;
            }
    }
}
} // namespace pd
