#include "SampleBank.h"

namespace pd
{
// ============================================================ analysis
float detectPitchHz (const Sample& s)
{
    const int n = s.length();
    if (n < 4096) return 261.63f;
    // autocorrelation on a 4096-sample window from the loudest region
    int best = 0; float bestE = 0;
    for (int st = 0; st + 4096 < n; st += 2048)
    {
        float e = 0; for (int i = 0; i < 4096; i += 4) e += s.L[(size_t) (st + i)] * s.L[(size_t) (st + i)];
        if (e > bestE) { bestE = e; best = st; }
    }
    const float* x = s.L.data() + best;
    const int minLag = (int) (s.sampleRate / 1200.0), maxLag = (int) (s.sampleRate / 40.0);
    float r0 = 0; for (int i = 0; i < 2048; ++i) r0 += x[i] * x[i];
    if (r0 < 1e-6f) return 261.63f;
    int bestLag = 0; float bestR = 0;
    for (int lag = minLag; lag <= maxLag && lag < 2048; ++lag)
    {
        float r = 0; for (int i = 0; i < 2048; ++i) r += x[i] * x[i + lag];
        r /= r0;
        if (r > bestR * 1.02f) { bestR = r; bestLag = lag; }
    }
    if (bestLag == 0 || bestR < 0.3f) return 261.63f;
    return (float) (s.sampleRate / bestLag);
}

void autoSlice (Sample& s, int maxSlices)
{
    const int n = s.length();
    maxSlices = juce::jlimit (1, kMaxSlices, maxSlices);
    const int hop = 256;
    std::vector<float> env;
    for (int i = 0; i + hop < n; i += hop)
    {
        float e = 0; for (int k = 0; k < hop; ++k) e += std::abs (s.L[(size_t) (i + k)]);
        env.push_back (e / hop);
    }
    struct Onset { int pos; float strength; };
    std::vector<Onset> on;
    for (size_t i = 2; i < env.size(); ++i)
    {
        const float flux = env[i] - 0.5f * (env[i - 1] + env[i - 2]);
        if (flux > 0.02f && env[i] > env[i - 1] * 1.4f) on.push_back ({ (int) i * hop, flux });
    }
    std::sort (on.begin(), on.end(), [] (auto& a, auto& b) { return a.strength > b.strength; });
    std::vector<int> pts { 0 };
    const int minGap = (int) (s.sampleRate * 0.06);
    for (auto& o : on)
    {
        if ((int) pts.size() >= maxSlices) break;
        bool ok = true; for (int p : pts) if (std::abs (p - o.pos) < minGap) ok = false;
        if (ok) pts.push_back (o.pos);
    }
    if ((int) pts.size() < 2)   // fall back to an even grid
    {
        pts.clear(); for (int k = 0; k < maxSlices; ++k) pts.push_back (k * n / maxSlices);
    }
    std::sort (pts.begin(), pts.end());
    s.numSlices = (int) pts.size();
    for (int k = 0; k < s.numSlices; ++k) s.sliceStart[k] = pts[(size_t) k];
}

void analyseSpectrum (Sample& s)
{
    const int n = s.length();
    const float f0 = juce::jmax (30.f, s.rootHz);
    const int period = (int) (s.sampleRate / f0);
    const int W = juce::jlimit (1024, 4096, period * 6);
    float globalMax = 1e-9f;
    std::vector<float> win ((size_t) W);
    for (int i = 0; i < W; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (kTwoPi * (float) i / (float) (W - 1));
    for (int fr = 0; fr < kSpecFrames; ++fr)
    {
        const int centre = (int) ((double) fr / (kSpecFrames - 1) * (n - 1));
        const int st = juce::jlimit (0, juce::jmax (0, n - W), centre - W / 2);
        for (int k = 0; k < kSpecPartials; ++k)
        {
            const float hz = f0 * (float) (k + 1);
            if (hz > s.sampleRate * 0.45) { s.spec[fr][k] = 0; continue; }
            const float w = kTwoPi * hz / (float) s.sampleRate;
            float re = 0, im = 0;
            // recursive oscillator for the correlation
            float c = 1, sn = 0; const float cw = std::cos (w), sw = std::sin (w);
            for (int i = 0; i < W && st + i < n; ++i)
            {
                const float x = s.L[(size_t) (st + i)] * win[(size_t) i];
                re += x * c; im += x * sn;
                const float nc = c * cw - sn * sw; sn = c * sw + sn * cw; c = nc;
            }
            s.spec[fr][k] = std::sqrt (re * re + im * im);
        }
        float e = 0; for (int k = 0; k < kSpecPartials; ++k) e += s.spec[fr][k] * s.spec[fr][k];
        globalMax = juce::jmax (globalMax, std::sqrt (e));
    }
    for (auto& fr : s.spec) for (auto& v : fr) v /= globalMax;
}

// ============================================================ synthesis helpers
namespace
{
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void bandpass (float f, float q, float sr)
    {
        const float w = kTwoPi * juce::jlimit (10.f, sr * 0.45f, f) / sr, al = std::sin (w) / (2 * q), c = std::cos (w), a0 = 1 + al;
        b0 = al / a0; b1 = 0; b2 = -al / a0; a1 = -2 * c / a0; a2 = (1 - al) / a0;
    }
    void lowpass (float f, float q, float sr)
    {
        const float w = kTwoPi * juce::jlimit (10.f, sr * 0.45f, f) / sr, al = std::sin (w) / (2 * q), c = std::cos (w), a0 = 1 + al;
        b0 = (1 - c) / 2 / a0; b1 = (1 - c) / a0; b2 = b0; a1 = -2 * c / a0; a2 = (1 - al) / a0;
    }
    void highpass (float f, float q, float sr)
    {
        const float w = kTwoPi * juce::jlimit (10.f, sr * 0.45f, f) / sr, al = std::sin (w) / (2 * q), c = std::cos (w), a0 = 1 + al;
        b0 = (1 + c) / 2 / a0; b1 = -(1 + c) / a0; b2 = b0; a1 = -2 * c / a0; a2 = (1 - al) / a0;
    }
    float process (float x) { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
};

struct BlepSaw
{
    float ph = 0;
    static float blep (float t, float dt)
    {
        if (t < dt) { t /= dt; return t + t - t * t - 1.f; }
        if (t > 1.f - dt) { t = (t - 1.f) / dt; return t * t + t + t + 1.f; }
        return 0.f;
    }
    float process (float inc) { ph += inc; if (ph >= 1.f) ph -= 1.f; return 2.f * ph - 1.f - blep (ph, inc); }
};

struct Pink
{
    float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    float process (float w)
    {
        b0 = 0.99886f * b0 + w * 0.0555179f; b1 = 0.99332f * b1 + w * 0.0750759f; b2 = 0.96900f * b2 + w * 0.1538520f;
        b3 = 0.86650f * b3 + w * 0.3104856f; b4 = 0.55000f * b4 + w * 0.5329522f; b5 = -0.7616f * b5 - w * 0.0168980f;
        const float o = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362f; b6 = w * 0.115926f; return o * 0.11f;
    }
};

void normalise (Sample& s, float peak = 0.89f)
{
    float m = 1e-9f;
    for (float v : s.L) m = juce::jmax (m, std::abs (v));
    for (float v : s.R) m = juce::jmax (m, std::abs (v));
    const float g = peak / m;
    for (auto& v : s.L) v *= g;
    for (auto& v : s.R) v *= g;
}

void fadeOut (std::vector<float>& d, int len)
{
    const int n = (int) d.size();
    for (int i = 0; i < len && i < n; ++i) d[(size_t) (n - 1 - i)] *= (float) i / (float) len;
}

// ---- vocal formant synthesis ------------------------------------------
struct VowelF { float f[4]; float bw[4]; float g[4]; };
const VowelF kV[] = {
    { { 730, 1090, 2440, 3400 }, { 80, 90, 120, 180 }, { 1.0f, 0.55f, 0.3f, 0.12f } },   // 0 A
    { { 530, 1840, 2480, 3500 }, { 60, 100, 120, 180 }, { 1.0f, 0.45f, 0.35f, 0.12f } },  // 1 E
    { { 270, 2290, 3010, 3700 }, { 50, 100, 140, 200 }, { 1.0f, 0.3f, 0.3f, 0.1f } },     // 2 I
    { { 570, 840, 2410, 3300 },  { 70, 80, 120, 180 }, { 1.0f, 0.6f, 0.15f, 0.08f } },    // 3 O
    { { 300, 870, 2240, 3200 },  { 50, 80, 120, 180 }, { 1.0f, 0.4f, 0.1f, 0.05f } },     // 4 U
    { { 250, 1100, 2400, 3300 }, { 40, 300, 300, 300 }, { 1.0f, 0.05f, 0.03f, 0.02f } },  // 5 M (nasal hum)
};

struct VocalSpec
{
    float dur;                  // seconds
    float pitchA, pitchB;       // midi start/end
    float vib;                  // vibrato depth (semitones)
    int vowels[4]; float vt[4]; int numV;   // vowel path with times (0..1)
    float breath;               // aspiration amount
    float onsetNoise;           // 'h' / consonant burst
    float attack, release;
};

void renderVocal (Sample& s, const VocalSpec& v, uint32_t seed, float* outL, int len, float gain = 1.f)
{
    const float sr = (float) s.sampleRate;
    Rng rng (seed);
    BlepSaw glottal;
    Biquad tilt; tilt.lowpass (1800.f, 0.6f, sr);
    Biquad fr[4];
    float vibPh = 0, jitter = 0;
    for (int i = 0; i < len; ++i)
    {
        const float t = (float) i / (float) len;
        const float tsec = (float) i / sr;
        // vowel interpolation
        int a = 0, b = 0; float u = 0;
        for (int k = 0; k < v.numV; ++k)
            if (t >= v.vt[k]) { a = k; b = juce::jmin (k + 1, v.numV - 1); }
        if (a != b) u = juce::jlimit (0.f, 1.f, (t - v.vt[a]) / juce::jmax (1e-3f, v.vt[b] - v.vt[a]));
        u = u * u * (3 - 2 * u);
        if ((i & 31) == 0)
            for (int k = 0; k < 4; ++k)
            {
                const auto& A = kV[v.vowels[a]]; const auto& B = kV[v.vowels[b]];
                fr[k].bandpass (lerp (A.f[k], B.f[k], u), lerp (A.f[k], B.f[k], u) / lerp (A.bw[k], B.bw[k], u), sr);
            }
        vibPh += kTwoPi * 5.2f / sr;
        jitter = 0.999f * jitter + 0.001f * rng.bi();
        const float vibAmt = v.vib * juce::jmin (1.f, tsec * 2.f);
        const float pitch = lerp (v.pitchA, v.pitchB, std::sqrt (t)) + vibAmt * std::sin (vibPh) + jitter * 3.f;
        const float hz = midiToHz (pitch);
        const float src = tilt.process (glottal.process (hz / sr)) + v.breath * rng.bi() * 0.6f;
        float env = juce::jmin (1.f, tsec / juce::jmax (0.001f, v.attack));
        const float rem = (float) (len - i) / sr;
        env *= juce::jmin (1.f, rem / juce::jmax (0.001f, v.release));
        float y = 0;
        const auto& A = kV[v.vowels[a]]; const auto& B = kV[v.vowels[b]];
        for (int k = 0; k < 4; ++k) y += fr[k].process (src) * lerp (A.g[k], B.g[k], u);
        // consonant onset burst
        if (tsec < 0.06f && v.onsetNoise > 0)
            y += v.onsetNoise * rng.bi() * (1.f - tsec / 0.06f) * 0.3f;
        outL[i] += y * env * gain * 3.f;
    }
}

std::unique_ptr<Sample> blank (const juce::String& name, const juce::String& cat, double sr, float seconds, bool stereo)
{
    auto s = std::make_unique<Sample>();
    s->name = name; s->category = cat; s->sampleRate = sr;
    s->L.assign ((size_t) (seconds * sr), 0.f);
    if (stereo) s->R.assign (s->L.size(), 0.f);
    return s;
}

// ---- drums --------------------------------------------------------------
enum Drum { Kick = 0, KickSub, KickCrunch, Snare, SnareRim, SnareGarage, Clap, HatC, HatO, Shaker, PercWood, PercMetal };

void renderDrum (int type, float* out, int len, float sr, uint32_t seed, float vel = 1.f)
{
    Rng r (seed);
    float ph = 0;
    Biquad hp, bp, bp2; Pink pink;
    switch (type)
    {
        case Kick: case KickSub: case KickCrunch:
        {
            const float base = type == KickSub ? 42.f : 50.f;
            const float sweep = type == KickSub ? 140.f : 230.f;
            const float dec = type == KickSub ? 0.55f : (type == KickCrunch ? 0.28f : 0.36f);
            for (int i = 0; i < len; ++i)
            {
                const float t = (float) i / sr;
                const float f = base + sweep * std::exp (-t / 0.028f);
                ph += f / sr;
                float y = std::sin (kTwoPi * ph) * std::exp (-t / dec);
                if (t < 0.004f) y += r.bi() * 0.5f * (1 - t / 0.004f);
                if (type == KickCrunch) y = fastTanh (y * 3.5f) * 0.8f;
                else y = fastTanh (y * 1.4f);
                out[i] += y * vel;
            }
        } break;
        case Snare: case SnareRim: case SnareGarage:
        {
            hp.highpass (type == SnareRim ? 2500.f : 1200.f, 0.7f, sr);
            const float tone = type == SnareGarage ? 230.f : 185.f;
            const float nd = type == SnareRim ? 0.05f : (type == SnareGarage ? 0.14f : 0.19f);
            for (int i = 0; i < len; ++i)
            {
                const float t = (float) i / sr;
                const float body = (std::sin (kTwoPi * tone * t) + 0.5f * std::sin (kTwoPi * tone * 1.78f * t)) * std::exp (-t / 0.06f);
                const float nz = hp.process (r.bi()) * std::exp (-t / nd);
                float y = body * (type == SnareRim ? 0.3f : 0.6f) + nz * 0.9f;
                out[i] += fastTanh (y * 1.6f) * vel;
            }
        } break;
        case Clap:
        {
            bp.bandpass (1300.f, 1.2f, sr);
            for (int i = 0; i < len; ++i)
            {
                const float t = (float) i / sr;
                float e = 0;
                for (int k = 0; k < 4; ++k) { const float tk = t - (float) k * 0.011f; if (tk >= 0) e = juce::jmax (e, std::exp (-tk / (k == 3 ? 0.16f : 0.008f))); }
                out[i] += bp.process (r.bi()) * e * 1.8f * vel;
            }
        } break;
        case HatC: case HatO:
        {
            static const float fr[] = { 205.3f, 304.4f, 369.6f, 522.7f, 540.f, 800.f };
            float phs[6] {};
            hp.highpass (7000.f, 0.7f, sr); bp.bandpass (10000.f, 1.f, sr);
            const float dec = type == HatO ? 0.35f : 0.045f;
            for (int i = 0; i < len; ++i)
            {
                const float t = (float) i / sr;
                float m = 0; for (int k = 0; k < 6; ++k) { phs[k] += fr[k] * 1.4f / sr; m += (phs[k] - std::floor (phs[k])) < 0.5f ? 1.f : -1.f; }
                const float y = hp.process (bp.process (m * 0.2f + r.bi() * 0.3f));
                out[i] += y * std::exp (-t / dec) * 1.2f * vel;
            }
        } break;
        case Shaker:
        {
            bp.bandpass (6500.f, 1.5f, sr);
            for (int i = 0; i < len; ++i)
            {
                const float t = (float) i / sr;
                const float e = juce::jmin (1.f, t / 0.012f) * std::exp (-t / 0.07f);
                out[i] += bp.process (r.bi()) * e * 1.5f * vel;
            }
        } break;
        case PercWood:
            for (int i = 0; i < len; ++i)
            {
                const float t = (float) i / sr;
                out[i] += (std::sin (kTwoPi * 820.f * t) * std::exp (-t / 0.03f) + std::sin (kTwoPi * 1640.f * t) * 0.3f * std::exp (-t / 0.012f)) * vel;
            }
            break;
        case PercMetal:
            for (int i = 0; i < len; ++i)
            {
                const float t = (float) i / sr;
                const float m = std::sin (kTwoPi * 1210.f * t) * 3.f * std::exp (-t / 0.1f);
                out[i] += std::sin (kTwoPi * 560.f * t + m) * std::exp (-t / 0.18f) * 0.7f * vel;
            }
            break;
        default: break;
    }
}

struct Hit { int step; int drum; float vel; float pan; };

void renderBreak (Sample& s, float bpm, const std::vector<Hit>& hits, float swing, int steps, uint32_t seed)
{
    const float sr = (float) s.sampleRate;
    const float stepLen = 60.f / bpm / 4.f;
    const int total = (int) (stepLen * (float) steps * sr);
    s.L.assign ((size_t) total, 0.f); s.R.assign ((size_t) total, 0.f);
    std::vector<float> tmp;
    for (auto& h : hits)
    {
        float t = (float) h.step * stepLen;
        if (h.step % 2 == 1) t += (swing - 0.5f) * 2.f * stepLen;
        const int start = (int) (t * sr);
        const int len = juce::jmin (total - start, (int) (0.6f * sr));
        if (len <= 0) continue;
        tmp.assign ((size_t) len, 0.f);
        renderDrum (h.drum, tmp.data(), len, sr, seed + (uint32_t) h.step * 31u + (uint32_t) h.drum, h.vel);
        const float gl = std::sqrt (0.5f * (1 - h.pan)), gr = std::sqrt (0.5f * (1 + h.pan));
        for (int i = 0; i < len; ++i) { s.L[(size_t) (start + i)] += tmp[(size_t) i] * gl; s.R[(size_t) (start + i)] += tmp[(size_t) i] * gr; }
    }
    // gritty bus: drive + gentle 12-bit crunch
    for (size_t i = 0; i < s.L.size(); ++i)
    {
        s.L[i] = std::round (fastTanh (s.L[i] * 1.3f) * 2048.f) / 2048.f;
        s.R[i] = std::round (fastTanh (s.R[i] * 1.3f) * 2048.f) / 2048.f;
    }
    s.loopable = true;
    s.numSlices = 16;
    for (int k = 0; k < 16; ++k) s.sliceStart[k] = (int) ((float) k * (float) total / 16.f);
}

const char* kNames[SampleBank::kNumFactory] = {
    // 0-15 vocal chops
    "Chop Ah", "Chop Oh", "Chop Hey", "Chop Yeah", "Chop Ooh", "Chop Ee", "Chop Mm", "Chop Uh-Oh",
    "Chop Ay", "Chop Breath", "Chop Whoa", "Chop Hi Ah", "Chop Lo Oh", "Chop Doo", "Chop Sigh", "Chop Yo",
    // 16 phrase
    "Vocal Phrase (8 slices)",
    // 17-20 breaks
    "Break 134 Shuffle", "Break 172 Roller", "Break 140 Broken", "Break 128 Warehouse",
    // 21-32 drums
    "Kick Punch", "Kick Sub", "Kick Crunch", "Snare Crack", "Snare Rim", "Snare Garage", "Clap", "Hat Closed",
    "Hat Open", "Shaker", "Perc Wood", "Perc Metal",
    // 33-40 tonal
    "Felt Keys C3", "Organ Stab C3", "Rave Stab Cm", "Strings C3", "Bell C4", "Bass Pluck C2", "Choir Pad C3", "Glass C5",
    // 41-44 textures
    "Rain Bed", "Crowd Bed", "Vinyl Bed", "Tape Room" };
} // namespace

juce::StringArray SampleFactory::names()
{
    juce::StringArray a; for (auto* n : kNames) a.add (n); return a;
}

std::unique_ptr<Sample> SampleFactory::make (int idx, double srD)
{
    const float sr = (float) srD;
    std::unique_ptr<Sample> s;
    const juce::String name = kNames[juce::jlimit (0, SampleBank::kNumFactory - 1, idx)];

    if (idx < 16)
    {
        // vowel indices: 0 A, 1 E, 2 I, 3 O, 4 U, 5 M
        static const VocalSpec specs[16] = {
            { 0.55f, 62, 61.5f, 0.15f, { 0, 0 }, { 0, 1 }, 1, 0.05f, 0.0f, 0.01f, 0.12f },          // Ah
            { 0.55f, 60, 59.7f, 0.12f, { 3, 3 }, { 0, 1 }, 1, 0.05f, 0.0f, 0.01f, 0.12f },          // Oh
            { 0.45f, 64, 65,   0.1f,  { 1, 2 }, { 0, 0.7f }, 2, 0.06f, 0.6f, 0.005f, 0.1f },        // Hey
            { 0.6f,  62, 60,   0.15f, { 2, 1, 0 }, { 0, 0.2f, 0.5f }, 3, 0.05f, 0.0f, 0.01f, 0.15f }, // Yeah
            { 0.8f,  64, 64,   0.25f, { 4, 4 }, { 0, 1 }, 1, 0.08f, 0.0f, 0.06f, 0.25f },          // Ooh
            { 0.4f,  67, 67.3f, 0.1f, { 2, 2 }, { 0, 1 }, 1, 0.04f, 0.0f, 0.008f, 0.1f },          // Ee
            { 0.5f,  60, 60,   0.1f,  { 5, 5 }, { 0, 1 }, 1, 0.02f, 0.0f, 0.02f, 0.15f },          // Mm
            { 0.7f,  62, 57,   0.05f, { 0, 3, 3 }, { 0, 0.4f, 0.6f }, 3, 0.05f, 0.0f, 0.01f, 0.12f }, // Uh-Oh
            { 0.5f,  63, 63,   0.12f, { 0, 2 }, { 0, 0.6f }, 2, 0.05f, 0.0f, 0.01f, 0.12f },       // Ay
            { 0.7f,  60, 60,   0.0f,  { 0, 4 }, { 0, 1 }, 2, 0.9f, 0.3f, 0.08f, 0.3f },            // Breath
            { 0.9f,  59, 64,   0.2f,  { 4, 3, 0 }, { 0, 0.15f, 0.5f }, 3, 0.05f, 0.0f, 0.02f, 0.2f },// Whoa
            { 0.45f, 69, 68,   0.1f,  { 0, 0 }, { 0, 1 }, 1, 0.05f, 0.5f, 0.005f, 0.1f },          // Hi Ah
            { 0.6f,  52, 51.5f, 0.1f, { 3, 3 }, { 0, 1 }, 1, 0.05f, 0.0f, 0.01f, 0.15f },          // Lo Oh
            { 0.35f, 62, 62,   0.0f,  { 4, 4 }, { 0, 1 }, 1, 0.03f, 0.8f, 0.002f, 0.08f },         // Doo
            { 1.0f,  60, 55,   0.0f,  { 0, 3 }, { 0, 1 }, 2, 0.5f, 0.2f, 0.05f, 0.4f },            // Sigh
            { 0.4f,  60, 62,   0.05f, { 2, 3 }, { 0, 0.3f }, 2, 0.04f, 0.0f, 0.005f, 0.1f },       // Yo
        };
        const auto& v = specs[idx];
        s = blank (name, "Vocal", srD, v.dur, false);
        renderVocal (*s, v, 100u + (uint32_t) idx, s->L.data(), s->length());
        s->rootNote = std::round (v.pitchA);
        s->rootHz = midiToHz (v.pitchA);
        normalise (*s);
        autoSlice (*s, 4);
    }
    else if (idx == 16)
    {
        s = blank (name, "Vocal", srD, 4.0f, false);
        const int seg = s->length() / 8;
        static const float mel[8] = { 62, 65, 67, 65, 62, 60, 62, 57 };
        static const int vow[8][2] = { { 0, 0 }, { 2, 1 }, { 3, 3 }, { 0, 2 }, { 4, 4 }, { 1, 1 }, { 0, 3 }, { 3, 4 } };
        for (int k = 0; k < 8; ++k)
        {
            VocalSpec v { (float) seg / sr, mel[k], mel[k] - 0.3f, 0.12f, { vow[k][0], vow[k][1] }, { 0, 0.6f }, 2,
                          0.05f, (k % 3 == 0) ? 0.5f : 0.f, 0.01f, 0.08f };
            renderVocal (*s, v, 300u + (uint32_t) k, s->L.data() + k * seg, seg);
        }
        s->rootNote = 62; s->rootHz = midiToHz (62);
        normalise (*s);
        s->numSlices = 8;
        for (int k = 0; k < 8; ++k) s->sliceStart[k] = k * seg;
    }
    else if (idx <= 20)
    {
        s = blank (name, "Break", srD, 0.1f, true);
        std::vector<Hit> h;
        auto add = [&] (std::initializer_list<int> steps, int drum, float vel, float pan = 0) { for (int st : steps) h.push_back ({ st, drum, vel, pan }); };
        float bpm = 134, swing = 0.58f;
        if (idx == 17)
        {
            add ({ 0, 7, 10, 16, 26 }, Kick, 1.f);
            add ({ 4, 12, 20, 28 }, Snare, 0.95f);
            add ({ 14, 30, 23 }, SnareGarage, 0.35f);
            for (int st = 0; st < 32; st += 2) h.push_back ({ st, HatC, (st % 4 == 2) ? 0.8f : 0.45f, 0.3f });
            add ({ 6, 22 }, HatO, 0.5f, 0.3f);
        }
        else if (idx == 18)
        {
            bpm = 172; swing = 0.52f;
            add ({ 0, 10, 16, 26 }, Kick, 1.f);
            add ({ 4, 12, 20, 28 }, Snare, 1.f);
            add ({ 7, 15, 23, 31, 18 }, SnareGarage, 0.3f, -0.1f);
            for (int st = 0; st < 32; ++st) h.push_back ({ st, HatC, (st % 2) ? 0.35f : 0.6f, 0.35f });
            add ({ 3, 19 }, Shaker, 0.4f, -0.4f);
        }
        else if (idx == 19)
        {
            bpm = 140; swing = 0.55f;
            add ({ 0, 3, 11, 16, 19, 27 }, KickCrunch, 1.f);
            add ({ 8, 24 }, Snare, 1.f); add ({ 8, 24 }, Clap, 0.7f);
            for (int st = 2; st < 32; st += 4) h.push_back ({ st, HatO, 0.5f, 0.25f });
            add ({ 5, 13, 21, 29, 30 }, PercWood, 0.4f, -0.5f);
            add ({ 15, 31 }, PercMetal, 0.3f, 0.5f);
        }
        else
        {
            bpm = 128; swing = 0.5f;
            add ({ 0, 4, 8, 12, 16, 20, 24, 28 }, Kick, 1.f);
            add ({ 4, 12, 20, 28 }, Clap, 0.8f);
            for (int st = 2; st < 32; st += 4) h.push_back ({ st, HatO, 0.55f, 0.2f });
            for (int st = 0; st < 32; ++st) h.push_back ({ st, Shaker, (st % 4 == 0) ? 0.2f : 0.4f, -0.3f });
        }
        renderBreak (*s, bpm, h, swing, 32, 900u + (uint32_t) idx);
        normalise (*s, 0.9f);
        s->rootNote = 60; s->rootHz = 120.f;
    }
    else if (idx <= 32)
    {
        const int drum = idx - 21;
        const float dur = (drum == HatO) ? 0.6f : (drum <= KickCrunch ? 0.7f : 0.45f);
        s = blank (name, "Drum", srD, dur, false);
        renderDrum (drum, s->L.data(), s->length(), sr, 1234u + (uint32_t) idx);
        fadeOut (s->L, 256);
        normalise (*s, 0.92f);
        s->rootNote = 60; s->rootHz = drum <= KickCrunch ? 55.f : 200.f;
    }
    else if (idx <= 40)
    {
        Rng r (5000u + (uint32_t) idx);
        const int k = idx - 33;
        const float rootMidi[] = { 48, 48, 48, 48, 60, 36, 48, 72 };
        const float durs[] = { 3.0f, 1.6f, 0.9f, 2.6f, 3.0f, 0.7f, 3.0f, 2.0f };
        s = blank (name, "Tonal", srD, durs[k], k == 3 || k == 6);
        const float f0 = midiToHz (rootMidi[k]);
        const int n = s->length();
        float* L = s->L.data();
        switch (k)
        {
            case 0: // felt keys: slightly inharmonic decaying partials + hammer
                for (int p = 1; p <= 24; ++p)
                {
                    const float fp = f0 * (float) p * std::sqrt (1.f + 0.0004f * (float) (p * p));
                    if (fp > sr * 0.45f) break;
                    const float a = std::pow ((float) p, -1.1f) * (p == 1 ? 1.f : 0.8f), tau = 2.4f / (1.f + 0.35f * (float) p);
                    const float ph0 = r.uni() * kTwoPi;
                    for (int i = 0; i < n; ++i) { const float t = (float) i / sr; L[i] += a * std::sin (kTwoPi * fp * t + ph0) * std::exp (-t / tau); }
                }
                { Biquad lp; lp.lowpass (900.f, 0.7f, sr); for (int i = 0; i < (int) (0.03f * sr); ++i) L[i] += lp.process (r.bi()) * 0.4f * (1.f - (float) i / (0.03f * sr)); }
                break;
            case 1: // organ stab
            {
                static const int h[] = { 1, 2, 3, 4, 6, 8 }; static const float a[] = { 1, 0.9f, 0.7f, 0.5f, 0.35f, 0.4f };
                for (int i = 0; i < n; ++i)
                {
                    const float t = (float) i / sr; float y = 0;
                    for (int q = 0; q < 6; ++q) y += a[q] * std::sin (kTwoPi * f0 * (float) h[q] * t);
                    y += 0.5f * std::sin (kTwoPi * f0 * 3 * t) * std::exp (-t / 0.15f);    // percussion
                    if (t < 0.004f) y += r.bi() * 0.4f;                                      // key click
                    L[i] = y * juce::jmin (1.f, t / 0.003f) * std::exp (-t / 1.2f);
                }
            } break;
            case 2: // rave stab: detuned minor chord saws through closing LP
            {
                BlepSaw saws[8]; Biquad lp;
                const float notes[] = { 0, 3, 7, 12 };
                for (int i = 0; i < n; ++i)
                {
                    const float t = (float) i / sr;
                    if ((i & 15) == 0) lp.lowpass (300.f + 6000.f * std::exp (-t / 0.18f), 2.0f, sr);
                    float y = 0;
                    for (int q = 0; q < 8; ++q) y += saws[q].process (f0 * semitonesToRatio (notes[q % 4] + (q < 4 ? -0.12f : 0.12f)) / sr);
                    L[i] = lp.process (y * 0.2f) * std::exp (-t / 0.35f);
                }
            } break;
            case 3: // strings ensemble (stereo)
            {
                BlepSaw saws[6]; Biquad lpL, lpR; lpL.lowpass (3200.f, 0.7f, sr); lpR.lowpass (3200.f, 0.7f, sr);
                float* R = s->R.data();
                for (int i = 0; i < n; ++i)
                {
                    const float t = (float) i / sr;
                    float l = 0, rr = 0;
                    for (int q = 0; q < 6; ++q)
                    {
                        const float v = saws[q].process (f0 * semitonesToRatio (((float) q - 2.5f) * 0.06f + 0.03f * std::sin (t * (1.1f + (float) q))) / sr);
                        (q % 2 ? l : rr) += v;
                    }
                    const float e = juce::jmin (1.f, t / 0.35f);
                    L[i] = lpL.process (l * 0.25f) * e; R[i] = lpR.process (rr * 0.25f) * e;
                }
                s->loopable = true;
            } break;
            case 4: // FM bell
                for (int i = 0; i < n; ++i)
                {
                    const float t = (float) i / sr;
                    const float m = std::sin (kTwoPi * f0 * 3.5f * t) * 4.f * std::exp (-t / 0.6f);
                    L[i] = std::sin (kTwoPi * f0 * t + m) * std::exp (-t / 1.4f);
                }
                break;
            case 5: // bass pluck
            {
                BlepSaw saw; Biquad lp;
                for (int i = 0; i < n; ++i)
                {
                    const float t = (float) i / sr;
                    if ((i & 15) == 0) lp.lowpass (80.f + 3000.f * std::exp (-t / 0.08f), 1.2f, sr);
                    L[i] = lp.process (saw.process (f0 / sr)) * std::exp (-t / 0.4f);
                }
            } break;
            case 6: // choir pad (stereo, 4 voices)
            {
                VocalSpec v { durs[k], 48, 48, 0.18f, { 0, 3 }, { 0, 1 }, 2, 0.12f, 0.f, 0.4f, 0.5f };
                for (int q = 0; q < 4; ++q)
                {
                    v.pitchA = v.pitchB = 48.f + ((float) q - 1.5f) * 0.08f;
                    renderVocal (*s, v, 700u + (uint32_t) q, (q % 2) ? s->R.data() : s->L.data(), n, 0.5f);
                }
                s->loopable = true;
            } break;
            case 7: // glass
            {
                const float rat[] = { 1.f, 2.32f, 4.25f, 6.63f, 9.38f }; const float am[] = { 1, 0.6f, 0.4f, 0.25f, 0.15f };
                for (int q = 0; q < 5; ++q)
                    for (int i = 0; i < n; ++i) { const float t = (float) i / sr; L[i] += am[q] * std::sin (kTwoPi * f0 * rat[q] * t) * std::exp (-t / (1.2f / (1.f + (float) q))); }
            } break;
            default: break;
        }
        fadeOut (s->L, 512); if (s->stereo()) fadeOut (s->R, 512);
        s->rootNote = rootMidi[k]; s->rootHz = f0;
        normalise (*s);
    }
    else
    {
        s = blank (name, "Texture", srD, 4.0f, true);
        Rng r (8000u + (uint32_t) idx);
        const int n = s->length();
        float* L = s->L.data(); float* R = s->R.data();
        Pink pl, pr; Biquad lpL, lpR, bpL, bpR;
        switch (idx)
        {
            case 41: // rain
            {
                lpL.lowpass (4000.f, 0.7f, sr); lpR.lowpass (4000.f, 0.7f, sr);
                float dropL = 0, dropR = 0; Biquad dl, dr; dl.bandpass (3500.f, 4.f, sr); dr.bandpass (4800.f, 4.f, sr);
                for (int i = 0; i < n; ++i)
                {
                    if (r.uni() < 0.004f) dropL += r.uni();
                    if (r.uni() < 0.004f) dropR += r.uni();
                    dropL *= 0.995f; dropR *= 0.995f;
                    L[i] = lpL.process (pl.process (r.bi())) * 0.5f + dl.process (dropL * r.bi()) * 1.5f;
                    R[i] = lpR.process (pr.process (r.bi())) * 0.5f + dr.process (dropR * r.bi()) * 1.5f;
                }
            } break;
            case 42: // crowd: murmuring formant bands with slow random AM and swells
            {
                Biquad bands[8]; float am[8] {}; float target[8] {};
                const float fc[] = { 350, 520, 700, 950, 1250, 1700, 2300, 3000 };
                for (int b = 0; b < 8; ++b) bands[b].bandpass (fc[b], 3.f, sr);
                for (int i = 0; i < n; ++i)
                {
                    const float t = (float) i / sr;
                    const float swell = 0.6f + 0.4f * std::sin (kTwoPi * 0.15f * t);
                    float l = 0, rr = 0;
                    for (int b = 0; b < 8; ++b)
                    {
                        if ((i & 1023) == 0) target[b] = r.uni();
                        am[b] += (target[b] - am[b]) * 0.0005f;
                        const float v = bands[b].process (r.bi()) * am[b];
                        (b % 2 ? l : rr) += v;
                    }
                    L[i] = l * swell; R[i] = rr * swell;
                }
            } break;
            case 43: // vinyl: crackle, rumble, hiss
            {
                lpL.lowpass (60.f, 0.7f, sr); bpL.highpass (3000.f, 0.7f, sr);
                for (int i = 0; i < n; ++i)
                {
                    float c = 0; if (r.uni() < 0.0015f) c = r.bi() * (r.uni() < 0.1f ? 1.f : 0.3f);
                    const float rumble = lpL.process (r.bi()) * 2.f;
                    const float hiss = bpL.process (r.bi()) * 0.05f;
                    L[i] = c + rumble + hiss; R[i] = c * 0.8f + rumble + bpL.process (r.bi()) * 0.05f;
                }
            } break;
            default: // tape room: hiss, mains hum, wow
            {
                bpL.highpass (2000.f, 0.6f, sr); bpR.highpass (2000.f, 0.6f, sr);
                for (int i = 0; i < n; ++i)
                {
                    const float t = (float) i / sr;
                    const float wow = 1.f + 0.2f * std::sin (kTwoPi * 0.7f * t);
                    const float hum = 0.05f * (std::sin (kTwoPi * 50.f * t) + 0.5f * std::sin (kTwoPi * 100.f * t) + 0.25f * std::sin (kTwoPi * 150.f * t));
                    L[i] = bpL.process (pl.process (r.bi())) * 0.6f * wow + hum;
                    R[i] = bpR.process (pr.process (r.bi())) * 0.6f * wow + hum;
                }
            } break;
        }
        normalise (*s, 0.7f);
        s->loopable = true; s->rootNote = 60; s->rootHz = 200.f;
        autoSlice (*s, 8);
    }
    analyseSpectrum (*s);
    return s;
}

// ============================================================ bank
SampleBank::SampleBank() : juce::Thread ("PD Sample Factory")
{
    startThread (juce::Thread::Priority::normal);
}

SampleBank::~SampleBank()
{
    stopThread (10000);
}

void SampleBank::run()
{
    for (int i = 0; i < kNumFactory && ! threadShouldExit(); ++i)
        addSample (SampleFactory::make (i));
    ready = true;
}

int SampleBank::addSample (std::unique_ptr<Sample> s)
{
    const juce::ScopedLock sl (lock);
    const int slot = count.load();
    if (slot >= kMaxSamples || s == nullptr) return -1;
    samples[slot].store (s.get(), std::memory_order_release);
    owned.push_back (std::move (s));
    count = slot + 1;
    return slot;
}

int SampleBank::loadUserSample (const juce::File& f)
{
    waitUntilReady();   // keep factory indices stable
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (f));
    if (rd == nullptr || rd->lengthInSamples <= 0) return -1;
    const int len = (int) (int) std::min<juce::int64> (rd->lengthInSamples, (juce::int64) (rd->sampleRate * 60));
    juce::AudioBuffer<float> buf ((int) juce::jmin (2u, rd->numChannels), len);
    rd->read (&buf, 0, len, 0, true, true);
    auto s = std::make_unique<Sample>();
    s->name = f.getFileNameWithoutExtension();
    s->category = "User";
    s->sampleRate = rd->sampleRate;
    s->L.assign (buf.getReadPointer (0), buf.getReadPointer (0) + len);
    if (buf.getNumChannels() > 1) s->R.assign (buf.getReadPointer (1), buf.getReadPointer (1) + len);
    s->rootHz = detectPitchHz (*s);
    s->rootNote = 69.f + 12.f * std::log2 (s->rootHz / 440.f);
    autoSlice (*s, 16);
    analyseSpectrum (*s);
    return addSample (std::move (s));
}
} // namespace pd
