#pragma once
// Pure Devotion — voice-level DSP building blocks. All real-time safe:
// memory is allocated in prepare(), never in process().
#include "../core/ModMatrix.h"
#include <vector>

namespace pd
{
// ============================================================ filter
class VoiceFilter
{
public:
    void prepare (double sampleRate, int maxDelaySamples)
    {
        sr = (float) sampleRate;
        comb.assign ((size_t) juce::jmax (16, maxDelaySamples), 0.f);
        reset();
    }
    void setSampleRate (double sampleRate) noexcept { sr = (float) sampleRate; }
    void reset() noexcept
    {
        for (auto& s : st) s = 0;
        ic1 = ic2 = ic1b = ic2b = 0;
        for (auto& f : fm) f.ic1 = f.ic2 = 0;
        std::fill (comb.begin(), comb.end(), 0.f);
        cw = 0; combLp = 0; fbHp = 0;
    }

    // Called at control rate.
    void set (int t, float cutoffHz, float resonance, float morphAmt, float preDrive, float postDrive) noexcept
    {
        type = t;
        const float nyq = sr * 0.49f;
        cutoff = clampf (cutoffHz, 15.f, nyq * 0.98f);
        res = clamp01 (resonance);
        morph = clamp01 (morphAmt);
        pre = preDrive; post = postDrive;
        g = std::tan (kPi * cutoff / sr);
        G = g / (1.f + g);
        if (t >= FtSvfLP && t <= FtSvfPeak)
        {
            k = 2.f - 1.98f * res;
            a1 = 1.f / (1.f + g * (g + k)); a2 = g * a1; a3 = g * a2;
        }
        else if (t == FtFormant)
        {
            // vowel morph A-E-I-O-U, shifted by cutoff (1 kHz neutral)
            static const float F[5][3] = { { 730, 1090, 2440 }, { 530, 1840, 2480 }, { 270, 2290, 3010 }, { 570, 840, 2410 }, { 300, 870, 2240 } };
            const float m = morph * 4.f; const int a = juce::jmin (3, (int) m); const float u = m - (float) a;
            const float shift = cutoff / 1000.f;
            const float q = 2.f + res * 18.f;
            for (int i = 0; i < 3; ++i)
            {
                const float f = clampf (lerp (F[a][i], F[a + 1][i], u) * shift, 40.f, nyq * 0.95f);
                auto& s = fm[i];
                s.g = std::tan (kPi * f / sr); s.k = 1.f / q;
                s.a1 = 1.f / (1.f + s.g * (s.g + s.k)); s.a2 = s.g * s.a1; s.a3 = s.g * s.a2;
            }
        }
        else if (t == FtCombPlus || t == FtCombMinus)
        {
            combLen = clampf (sr / cutoff, 2.f, (float) comb.size() - 3.f);
            combFb = (t == FtCombPlus ? 1.f : -1.f) * res * 0.985f;
            combDampA = 0.05f + 0.95f * (1.f - morph);
        }
    }

    float process (float x) noexcept
    {
        if (pre > 0.001f) x = fastTanh (x * (1.f + pre * 12.f)) / (1.f + pre * 2.f);
        float y;
        switch (type)
        {
            case FtLadder:     y = ladder (x, 4.f * res, 1.f, false); break;
            case FtDiode:      y = diode (x); break;
            case FtRaveLadder: y = ladder (x * 2.5f, 4.65f * res, 1.8f, true) * 0.6f; break;
            case FtSvfLP: case FtSvfBP: case FtSvfHP: case FtSvfNotch: case FtSvfPeak: y = svf (x); break;
            case FtCombPlus: case FtCombMinus: y = combProcess (x); break;
            case FtFormant:
            {
                y = 0;
                const float gains[3] = { 1.f, 0.7f, 0.45f };
                for (int i = 0; i < 3; ++i) y += svfBand (fm[i], x) * gains[i];
                y *= 1.5f;
            } break;
            default: y = x; break;
        }
        if (post > 0.001f) y = fastTanh (y * (1.f + post * 10.f)) / (1.f + post * 1.5f);
        return y;
    }

private:
    struct Band { float g = 0, k = 1, a1 = 0, a2 = 0, a3 = 0, ic1 = 0, ic2 = 0; };
    static float svfBand (Band& b, float v0) noexcept
    {
        const float v3 = v0 - b.ic2;
        const float v1 = b.a1 * b.ic1 + b.a2 * v3;
        const float v2 = b.ic2 + b.a2 * b.ic1 + b.a3 * v3;
        b.ic1 = 2.f * v1 - b.ic1; b.ic2 = 2.f * v2 - b.ic2;
        return v1 * b.k * 4.f;   // normalised band-pass
    }

    // Zero-delay-feedback 4-pole ladder with saturating input (Moog-style).
    float ladder (float x, float kk, float satGain, bool rave) noexcept
    {
        const float S = G * G * G * (st[0] / (1.f + g)) + G * G * (st[1] / (1.f + g)) + G * (st[2] / (1.f + g)) + st[3] / (1.f + g);
        const float G4 = G * G * G * G;
        float y4est = (G4 * x + S) / (1.f + kk * G4);
        float fb = kk * y4est;
        if (rave)
        {
            fbHp += 0.002f * (fb - fbHp);       // thin out the low end of the feedback
            fb = clampf (fb - fbHp * 0.5f, -1.6f, 1.6f);
        }
        float u = fastTanh ((x * (1.f + kk * 0.35f) - fb) * satGain) / satGain;
        for (int i = 0; i < 4; ++i)
        {
            const float v = (u - st[i]) * G;
            const float y = v + st[i];
            st[i] = y + v;
            u = y;
        }
        return u;
    }

    // 3-pole diode-flavoured ladder with asymmetric stage saturation.
    float diode (float x) noexcept
    {
        const float kk = 7.5f * res;
        const float G3 = G * G * G;
        const float S = G * G * (st[0] / (1.f + g)) + G * (st[1] / (1.f + g)) + st[2] / (1.f + g);
        const float y3 = (G3 * x + S) / (1.f + kk * G3);
        float u = x * (1.f + kk * 0.2f) - kk * y3;
        for (int i = 0; i < 3; ++i)
        {
            u = fastTanh (u + 0.15f) - 0.1489f;   // tanh(0.15) offset keeps DC small
            const float v = (u - st[i]) * G;
            const float y = v + st[i];
            st[i] = y + v;
            u = y;
        }
        return u * 1.2f;
    }

    float svf (float v0) noexcept
    {
        auto stage = [this] (float in, float& c1, float& c2, float& lp, float& bp, float& hp) {
            const float v3 = in - c2;
            const float v1 = a1 * c1 + a2 * v3;
            const float v2 = c2 + a2 * c1 + a3 * v3;
            c1 = 2.f * v1 - c1; c2 = 2.f * v2 - c2;
            lp = v2; bp = v1; hp = in - k * v1 - v2;
        };
        float lp, bp, hp;
        stage (v0, ic1, ic2, lp, bp, hp);
        float out1 = pick (lp, bp, hp);
        if (morph < 0.001f) return out1;
        float lp2, bp2, hp2;
        stage (out1, ic1b, ic2b, lp2, bp2, hp2);   // morph: 12 dB -> 24 dB slope
        return lerp (out1, pick (lp2, bp2, hp2), morph);
    }
    float pick (float lp, float bp, float hp) const noexcept
    {
        switch (type)
        {
            case FtSvfLP: return lp;
            case FtSvfBP: return bp * k;
            case FtSvfHP: return hp;
            case FtSvfNotch: return lp + hp;
            default: return lp - hp;   // peak
        }
    }

    float combProcess (float x) noexcept
    {
        const int n = (int) comb.size();
        float rp = (float) cw - combLen; if (rp < 0) rp += (float) n;
        const int i0 = (int) rp; const float t = rp - (float) i0;
        const float d = comb[(size_t) i0] + (comb[(size_t) ((i0 + 1) % n)] - comb[(size_t) i0]) * t;
        combLp += combDampA * (d - combLp);
        const float y = x + combFb * fastTanh (combLp);
        comb[(size_t) cw] = y;
        if (++cw >= n) cw = 0;
        return y * (1.f - std::abs (combFb) * 0.5f);
    }

    float sr = 48000.f;
    int type = FtLadder;
    float cutoff = 1000.f, res = 0, morph = 0, pre = 0, post = 0;
    float g = 0.1f, G = 0.1f, k = 2.f, a1 = 0, a2 = 0, a3 = 0;
    float st[4] {};
    float ic1 = 0, ic2 = 0, ic1b = 0, ic2b = 0;
    Band fm[3];
    std::vector<float> comb;
    int cw = 0;
    float combLen = 100.f, combFb = 0, combLp = 0, combDampA = 1.f, fbHp = 0;
};

// ============================================================ envelope
inline float shapeCurve (float p, float c) noexcept
{
    p = clamp01 (p);
    if (c > 0.001f) return 1.f - std::pow (1.f - p, 1.f + 4.f * c);
    if (c < -0.001f) return std::pow (p, 1.f - 4.f * c);
    return p;
}

class Envelope
{
public:
    enum Stage { Idle, Attack, Hold, Decay, Sustain, Release, MsegRun, MsegHold, MsegRelease };
    float a = 0.01f, h = 0, d = 0.3f, s = 0.7f, r = 0.3f, curve = 0.3f;
    bool msegMode = false;
    const Mseg* mseg = nullptr;

    void setSampleRate (float sampleRate) noexcept { sr = sampleRate; }
    void noteOn (bool legato = false) noexcept
    {
        if (msegMode) { stage = MsegRun; t = 0; seg = 0; startLevel = legato ? value : (mseg ? mseg->y[0] : 0.f); return; }
        stage = Attack; t = 0; startLevel = legato ? value : 0.f;
    }
    void noteOff() noexcept
    {
        if (stage == Idle) return;
        startLevel = value; t = 0;
        stage = msegMode ? MsegRelease : Release;
        if (msegMode && mseg) seg = juce::jmax (0, mseg->sustainPoint);
    }
    void kill() noexcept { stage = Idle; value = 0; }
    bool active() const noexcept { return stage != Idle; }
    bool releasing() const noexcept { return stage == Release || stage == MsegRelease; }

    // Advance by `n` samples and return value at the end.
    float advance (int n) noexcept
    {
        const float dt = (float) n / sr;
        switch (stage)
        {
            case Idle: value = 0; break;
            case Attack:
                t += dt;
                if (a <= 0.0005f || t >= a) { value = 1.f; stage = h > 0 ? Hold : Decay; t = 0; }
                else value = lerp (startLevel, 1.f, shapeCurve (t / a, curve));
                break;
            case Hold:
                t += dt; value = 1.f;
                if (t >= h) { stage = Decay; t = 0; }
                break;
            case Decay:
                t += dt;
                if (t >= d) { value = s; stage = Sustain; }
                else value = lerp (1.f, s, shapeCurve (t / d, curve));
                break;
            case Sustain: value = s; break;
            case Release:
                t += dt;
                if (t >= r) { value = 0; stage = Idle; }
                else value = lerp (startLevel, 0.f, shapeCurve (t / r, curve));
                break;
            case MsegRun: case MsegHold: case MsegRelease: runMseg (dt); break;
        }
        return value;
    }
    float current() const noexcept { return value; }

private:
    void runMseg (float dt) noexcept
    {
        if (mseg == nullptr || mseg->numPoints < 2) { value = 0; stage = Idle; return; }
        const auto& m = *mseg;
        if (stage == MsegHold) { value = m.y[juce::jlimit (0, m.numPoints - 1, m.sustainPoint)]; return; }
        t += dt;
        // advance through segments
        while (seg < m.numPoints - 1)
        {
            const float segLen = juce::jmax (0.0005f, m.t[seg + 1] - m.t[seg]);
            if (t < segLen) break;
            t -= segLen; ++seg; startLevel = m.y[seg];
            if (stage == MsegRun && seg == m.sustainPoint) { stage = MsegHold; value = m.y[seg]; return; }
        }
        if (seg >= m.numPoints - 1) { value = m.y[m.numPoints - 1]; stage = Idle; return; }
        const float segLen = juce::jmax (0.0005f, m.t[seg + 1] - m.t[seg]);
        value = lerp (startLevel, m.y[seg + 1], shapeCurve (t / segLen, m.c[seg]));
    }
    float sr = 48000.f, t = 0, value = 0, startLevel = 0;
    int seg = 0;
    Stage stage = Idle;
};

// ============================================================ LFO
class Lfo
{
public:
    int shape = 0, mode = 0;
    float rateHz = 1.f, phaseOffset = 0.f, smooth = 0.f, fadeIn = 0.f;
    const float* drawn = nullptr;

    void setSampleRate (float s) noexcept { sr = s; }
    void noteOn (uint32_t seed) noexcept
    {
        rng.s = seed | 1u;
        phase = phaseOffset; done = false; age = 0; held = rng.bi(); prevRand = held; nextRand = rng.bi();
    }
    // Free-running mode takes an externally supplied phase (shared clock).
    float advance (int n, double externalPhase = -1.0) noexcept
    {
        const float dt = (float) n / sr;
        age += dt;
        float ph;
        if (mode == 0 && externalPhase >= 0.0)
        {
            const double p = externalPhase + phaseOffset;
            const float newPh = (float) (p - std::floor (p));
            if (newPh < phase) { prevRand = nextRand; nextRand = rng.bi(); held = nextRand; }
            phase = newPh; ph = phase;
        }
        else
        {
            if (! done)
            {
                phase += rateHz * dt;
                if (phase >= 1.f)
                {
                    if (mode == 2 || mode == 3) { phase = 1.f; done = true; }
                    else { phase -= std::floor (phase); prevRand = nextRand; nextRand = rng.bi(); held = nextRand; }
                }
            }
            ph = done ? 0.9999f : phase;
        }
        float v = evaluate (ph);
        if (mode == 3) v = done ? (shape == 1 || shape == 0 ? 0.f : 0.5f * v + 0.5f) : 0.5f * v + 0.5f;
        if (fadeIn > 0.f) v *= clamp01 (age / fadeIn);
        const float a = smooth > 0.001f ? std::exp (-dt / (smooth * 0.25f)) : 0.f;
        out = v + a * (out - v);
        return out;
    }
    float current() const noexcept { return out; }
    float currentPhase() const noexcept { return phase; }

    float evaluate (float ph) const noexcept
    {
        switch (shape)
        {
            case 0: return std::sin (kTwoPi * ph);
            case 1: return 1.f - 4.f * std::abs (ph - 0.5f) - 0.f;
            case 2: return 2.f * ph - 1.f;
            case 3: return 1.f - 2.f * ph;
            case 4: return ph < 0.5f ? 1.f : -1.f;
            case 5: return held;
            case 6: { const float u = 0.5f - 0.5f * std::cos (kPi * ph); return lerp (prevRand, nextRand, u); }
            case 7:
            {
                if (drawn == nullptr) return 0.f;
                const float x = ph * (float) kLfoDrawPoints;
                const int i = (int) x % kLfoDrawPoints; const float t = x - std::floor (x);
                return lerp (drawn[i], drawn[(i + 1) % kLfoDrawPoints], t);
            }
            default: return 0.f;
        }
    }

private:
    float sr = 48000.f, phase = 0, out = 0, age = 0, held = 0, prevRand = 0, nextRand = 0;
    bool done = false;
    Rng rng;
};

// ============================================================ noise / texture oscillator
class NoiseGen
{
public:
    void setSampleRate (float s) noexcept { sr = s; }
    void seed (uint32_t s) noexcept { rng.s = s | 1u; }
    void setColour (float c) noexcept
    {
        colour = c;
        tiltA = std::exp (-kTwoPi * (c < 0 ? lerp (8000.f, 300.f, -c) : 18000.f) / sr);
        hpA = std::exp (-kTwoPi * (c > 0 ? lerp (20.f, 3000.f, c) : 20.f) / sr);
    }
    // returns stereo pair
    void process (int type, float& l, float& r) noexcept
    {
        const float w = rng.bi();
        float y;
        switch (type)
        {
            case 0: y = w; break;
            case 1: y = pink (w); break;
            case 2: // vinyl: crackle + rumble + hiss
            {
                float c = 0; if (rng.uni() < 0.0012f) c = rng.bi() * (rng.uni() < 0.15f ? 1.4f : 0.4f);
                rumble += 0.002f * (w - rumble);
                y = c + rumble * 6.f + w * 0.03f;
            } break;
            case 3: // tape hiss: bright pink, gently modulated
                modPh += 0.37f / sr; y = pink (w) * 0.6f * (1.f + 0.15f * std::sin (kTwoPi * modPh)) + (w - hissHp) * 0.2f; hissHp += 0.3f * (w - hissHp);
                break;
            case 4: // crowd: band-limited murmur with drifting amplitude
            {
                if (++crowdCount >= 2048) { crowdCount = 0; crowdTarget = 0.3f + 0.7f * rng.uni(); }
                crowdAmp += (crowdTarget - crowdAmp) * 0.0008f;
                bp1 += 0.12f * (w - bp1); bp2 += 0.05f * (bp1 - bp2);
                y = (bp1 - bp2) * 3.f * crowdAmp;
            } break;
            case 5: // rain: pink bed + droplets
            {
                if (rng.uni() < 0.003f) drop += rng.uni();
                drop *= 0.994f;
                dropHp += 0.6f * (rng.bi() * drop - dropHp);
                y = pink (w) * 0.35f + (rng.bi() * drop - dropHp) * 1.2f;
            } break;
            default: // cassette flutter: hiss with wow AM and low hum
                modPh += 6.5f / sr; humPh += 50.f / sr; if (humPh > 1.f) humPh -= 1.f; if (modPh > 1.f) modPh -= 1.f;
                y = pink (w) * 0.5f * (1.f + 0.35f * std::sin (kTwoPi * modPh)) + 0.06f * std::sin (kTwoPi * humPh);
                break;
        }
        // colour: tilt low-pass (dark) or high-pass (bright)
        lp = y + tiltA * (lp - y);
        float o = colour < 0 ? lp : y;
        hp = o + hpA * (hp - o);
        o = colour > 0 ? o - hp : o;
        l = o;
        r = type == 0 || type == 1 ? rng.bi() * 0.3f + o * 0.7f : o;   // a little decorrelation
    }

private:
    float pink (float w) noexcept
    {
        b0 = 0.99765f * b0 + w * 0.0990460f; b1 = 0.96300f * b1 + w * 0.2965164f; b2 = 0.57000f * b2 + w * 1.0526913f;
        return (b0 + b1 + b2 + w * 0.1848f) * 0.25f;
    }
    float sr = 48000.f, colour = 0, tiltA = 0, hpA = 0, lp = 0, hp = 0;
    float b0 = 0, b1 = 0, b2 = 0, rumble = 0, modPh = 0, humPh = 0, hissHp = 0;
    float bp1 = 0, bp2 = 0, crowdAmp = 0.5f, crowdTarget = 0.5f, drop = 0, dropHp = 0;
    int crowdCount = 0;
    Rng rng;
};
} // namespace pd
