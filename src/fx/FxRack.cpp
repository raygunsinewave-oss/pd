#include "FxRack.h"
#include "../dsp/Modules.h"

namespace pd
{
namespace
{
// ------------------------------------------------------------ helpers
struct DelayLine
{
    std::vector<float> buf; int w = 0, mask = 0;
    void prepare (int maxSamples)
    {
        int n = 1; while (n < maxSamples + 4) n <<= 1;
        buf.assign ((size_t) n, 0.f); mask = n - 1; w = 0;
    }
    void clear() { std::fill (buf.begin(), buf.end(), 0.f); }
    void push (float x) noexcept { buf[(size_t) w] = x; w = (w + 1) & mask; }
    float read (float delay) const noexcept   // samples, fractional (linear)
    {
        float rp = (float) w - 1.f - delay;
        while (rp < 0) rp += (float) (mask + 1);
        const int i = (int) rp; const float t = rp - (float) i;
        return buf[(size_t) (i & mask)] + (buf[(size_t) ((i + 1) & mask)] - buf[(size_t) (i & mask)]) * t;
    }
    float readInt (int delay) const noexcept { return buf[(size_t) ((w - 1 - delay) & mask)]; }
};

struct OnePole
{
    float a = 0, y = 0;
    void setLP (float hz, float sr) { a = std::exp (-kTwoPi * clampf (hz, 5.f, sr * 0.49f) / sr); }
    float lp (float x) noexcept { y = x + a * (y - x); return y; }
    float hp (float x) noexcept { return x - lp (x); }
};

struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    float process (float x) noexcept { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    void reset() { z1 = z2 = 0; }
    void set (float B0, float B1, float B2, float A0, float A1, float A2) { b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0; }
    void lowShelf (float f, float db, float sr)
    {
        const float A = std::pow (10.f, db / 40.f), w = kTwoPi * f / sr, c = std::cos (w), s = std::sin (w), al = s / 2.f * std::sqrt (2.f);
        const float sq = 2.f * std::sqrt (A) * al;
        set (A * ((A + 1) - (A - 1) * c + sq), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sq),
             (A + 1) + (A - 1) * c + sq, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sq);
    }
    void highShelf (float f, float db, float sr)
    {
        const float A = std::pow (10.f, db / 40.f), w = kTwoPi * f / sr, c = std::cos (w), s = std::sin (w), al = s / 2.f * std::sqrt (2.f);
        const float sq = 2.f * std::sqrt (A) * al;
        set (A * ((A + 1) + (A - 1) * c + sq), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sq),
             (A + 1) - (A - 1) * c + sq, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sq);
    }
    void peak (float f, float q, float db, float sr)
    {
        const float A = std::pow (10.f, db / 40.f), w = kTwoPi * f / sr, al = std::sin (w) / (2.f * q), c = std::cos (w);
        set (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    void lowpass (float f, float q, float sr)
    {
        const float w = kTwoPi * clampf (f, 10.f, sr * 0.45f) / sr, al = std::sin (w) / (2 * q), c = std::cos (w);
        set ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void highpass (float f, float q, float sr)
    {
        const float w = kTwoPi * clampf (f, 10.f, sr * 0.45f) / sr, al = std::sin (w) / (2 * q), c = std::cos (w);
        set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
};

struct EnvFollower
{
    float att = 0.9f, rel = 0.999f, y = 0;
    void set (float attackMs, float releaseMs, float sr)
    {
        att = std::exp (-1.f / (attackMs * 0.001f * sr)); rel = std::exp (-1.f / (releaseMs * 0.001f * sr));
    }
    float process (float x) noexcept { x = std::abs (x); y = x > y ? x + att * (y - x) : x + rel * (y - x); return y; }
};

inline float mixf (float dry, float wet, float m) noexcept { return dry + (wet - dry) * m; }

// ------------------------------------------------------------ FDN reverb core
class Fdn
{
public:
    static constexpr int N = 8;
    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        for (auto& d : lines) d.prepare ((int) (0.25f * sr));
        for (auto& d : diff) d.prepare ((int) (0.03f * sr));
        pre.prepare ((int) (0.3f * sr));
        reset();
    }
    void reset() { for (auto& d : lines) d.clear(); for (auto& d : diff) d.clear(); pre.clear(); for (auto& l : damp) l.y = 0; }
    void set (bool hall, float size, float decay, float dampAmt, float predelayMs)
    {
        static const float hallMs[N] = { 37.1f, 41.3f, 47.7f, 53.9f, 61.3f, 67.9f, 73.1f, 79.7f };
        static const float plateMs[N] = { 11.3f, 13.7f, 17.9f, 19.3f, 23.1f, 27.7f, 29.9f, 31.3f };
        const float scale = hall ? lerp (0.4f, 1.6f, size) : lerp (0.5f, 1.5f, size);
        for (int i = 0; i < N; ++i)
        {
            len[i] = (hall ? hallMs[i] : plateMs[i]) * 0.001f * sr * scale;
            g[i] = std::pow (10.f, -3.f * len[i] / (juce::jmax (0.1f, decay) * sr));
            damp[i].setLP (lerp (18000.f, 1500.f, dampAmt), sr);
        }
        static const float dMs[4] = { 4.7f, 3.6f, 12.7f, 9.3f };
        for (int i = 0; i < 4; ++i) dlen[i] = dMs[i] * 0.001f * sr * (hall ? 1.f : 0.6f);
        diffG = hall ? 0.6f : 0.72f;
        preLen = predelayMs * 0.001f * sr;
        modDepth = (hall ? 0.0009f : 0.0004f) * sr;
    }
    void process (float inL, float inR, float& outL, float& outR) noexcept
    {
        pre.push ((inL + inR) * 0.5f);
        float x = preLen > 1 ? pre.read (preLen) : (inL + inR) * 0.5f;
        for (int i = 0; i < 4; ++i)   // series allpass diffusion
        {
            const float d = diff[i].read (dlen[i]);
            const float v = x + diffG * d;
            diff[i].push (v);
            x = d - diffG * v;
        }
        modPh += 0.37f / sr; if (modPh > 1.f) modPh -= 1.f;
        float y[N]; float sum = 0;
        for (int i = 0; i < N; ++i)
        {
            float l = len[i];
            if (i == 0 || i == 3 || i == 6) l += modDepth * std::sin (kTwoPi * (modPh + (float) i * 0.13f));
            y[i] = damp[i].lp (lines[i].read (l)) * g[i];
            sum += y[i];
        }
        const float h = sum * (2.f / N);
        for (int i = 0; i < N; ++i)
            lines[i].push (y[i] - h + x * ((i & 1) ? -0.5f : 0.5f));
        outL = (y[0] + y[2] + y[4] + y[6]) * 0.6f;
        outR = (y[1] + y[3] + y[5] + y[7]) * 0.6f;
    }
private:
    float sr = 48000.f;
    DelayLine lines[N], diff[4], pre;
    float len[N] {}, g[N] {}, dlen[4] {}, diffG = 0.6f, preLen = 0, modPh = 0, modDepth = 0;
    OnePole damp[N];
};

// Delay-line pitch shifter (two crossfaded heads)
class PitchShifter
{
public:
    void prepare (double sampleRate) { sr = (float) sampleRate; window = 0.05f * sr; d.prepare ((int) (window * 2.5f)); }
    void reset() { d.clear(); ph = 0; }
    float process (float x, float ratio) noexcept
    {
        d.push (x);
        ph += (1.f - ratio) / window;
        ph -= std::floor (ph);
        const float p2 = ph + 0.5f - std::floor (ph + 0.5f);
        const float a = d.read (ph * window + 1.f), b = d.read (p2 * window + 1.f);
        const float wa = std::sin (kPi * ph), wb = std::sin (kPi * p2);
        return a * wa * wa + b * wb * wb;
    }
private:
    DelayLine d; float sr = 48000.f, window = 2400.f, ph = 0;
};

// ============================================================ effects
class Tape : public FxUnit
{
public:
    void prepare (double s, int) override { sr = (float) s; for (auto& d : dl) d.prepare ((int) (0.03f * sr)); reset(); }
    void reset() override { for (auto& d : dl) d.clear(); lp[0].y = lp[1].y = 0; wowPh = fPh = 0; }
    void process (float* L, float* R, int n, const float* p, const FxCtx&) override
    {
        const float mix = p[0], wow = p[1], flutter = p[2], sat = p[3], age = p[4];
        for (auto& f : lp) f.setLP (lerp (19000.f, 4500.f, age), sr);
        bump.lowShelf (90.f, 2.5f * age + 1.f, sr); bump2 = bump;
        float* ch[2] = { L, R };
        for (int i = 0; i < n; ++i)
        {
            wowPh += (0.6f + 0.3f * rng.uni()) / sr; if (wowPh > 1.f) wowPh -= 1.f;
            fPh += 9.5f / sr; if (fPh > 1.f) fPh -= 1.f;
            const float mod = 0.006f * sr + wow * 0.0028f * sr * (0.5f + 0.5f * std::sin (kTwoPi * wowPh))
                            + flutter * 0.00025f * sr * std::sin (kTwoPi * fPh);
            for (int c = 0; c < 2; ++c)
            {
                const float dry = ch[c][i];
                dl[c].push (dry);
                float y = dl[c].read (mod + (float) c * 0.3f);
                const float drive = 1.f + sat * 6.f;
                y = (fastTanh (y * drive + 0.1f * sat) - fastTanh (0.1f * sat)) / std::sqrt (drive);
                y = lp[c].lp (c ? bump2.process (y) : bump.process (y));
                y += age * 0.004f * rng.bi();
                ch[c][i] = mixf (dry, y, mix);
            }
        }
    }
private:
    float sr = 48000, wowPh = 0, fPh = 0; DelayLine dl[2]; OnePole lp[2]; Biquad bump, bump2; Rng rng;
};

class Cassette : public FxUnit
{
public:
    void prepare (double s, int) override { sr = (float) s; for (auto& d : dl) d.prepare ((int) (0.03f * sr)); reset(); }
    void reset() override { for (auto& d : dl) d.clear(); for (auto& b : lp) b.reset(); for (auto& b : hp) b.reset(); env.y = 0; }
    void process (float* L, float* R, int n, const float* p, const FxCtx&) override
    {
        const float mix = p[0], wear = p[1], flutter = p[2], drive = p[3], comp = p[4], hiss = p[5];
        for (int c = 0; c < 2; ++c) { lp[c].lowpass (lerp (14000.f, 3200.f, wear), 0.8f, sr); hp[c].highpass (lerp (35.f, 140.f, wear), 0.7f, sr); }
        env.set (5.f, 120.f, sr);
        float* ch[2] = { L, R };
        for (int i = 0; i < n; ++i)
        {
            ph1 += 1.3f / sr; ph2 += 7.9f / sr; ph1 -= std::floor (ph1); ph2 -= std::floor (ph2);
            wob += (rng.bi() - wob) * 0.0005f;
            const float m = 0.005f * sr + flutter * sr * (0.0018f * std::sin (kTwoPi * ph1) + 0.0004f * std::sin (kTwoPi * ph2) + 0.002f * wob);
            const float lev = env.process ((L[i] + R[i]) * 0.5f);
            const float cg = 1.f / (1.f + comp * 4.f * juce::jmax (0.f, lev - 0.1f));
            for (int c = 0; c < 2; ++c)
            {
                const float dry = ch[c][i];
                dl[c].push (dry);
                float y = dl[c].read (m + (float) c * 0.7f * wear);   // azimuth skew
                y = fastTanh (y * (1.f + drive * 5.f)) / (1.f + drive * 1.5f);
                y = hp[c].process (lp[c].process (y)) * cg * (1.f + comp);
                y += hiss * 0.02f * rng.bi();
                ch[c][i] = mixf (dry, y, mix);
            }
        }
    }
private:
    float sr = 48000, ph1 = 0, ph2 = 0, wob = 0; DelayLine dl[2]; Biquad lp[2], hp[2]; EnvFollower env; Rng rng;
};

class Chorus : public FxUnit
{
public:
    void prepare (double s, int) override { sr = (float) s; for (auto& d : dl) d.prepare ((int) (0.05f * sr)); reset(); }
    void reset() override { for (auto& d : dl) d.clear(); ph = 0; }
    void process (float* L, float* R, int n, const float* p, const FxCtx&) override
    {
        const float mix = p[0]; const int mode = (int) p[1]; const float rate = p[2], depth = p[3];
        const int voices = juce::jlimit (2, 8, (int) p[4]);
        // Dimension modes: fixed rate/depth pairs on anti-phase triangle LFOs
        static const float dimRate[4] = { 0.25f, 0.4f, 0.55f, 0.75f }, dimDepth[4] = { 0.25f, 0.4f, 0.6f, 0.85f };
        for (int i = 0; i < n; ++i)
        {
            const float dl_ = L[i], dr = R[i];
            dl[0].push (dl_); dl[1].push (dr);
            float wl = 0, wr = 0;
            if (mode == 0)
            {
                ph += rate / sr; ph -= std::floor (ph);
                for (int v = 0; v < voices; ++v)
                {
                    const float o = (float) v / (float) voices;
                    const float d = (0.007f + 0.006f * depth * (0.5f + 0.5f * std::sin (kTwoPi * (ph + o)))) * sr;
                    const float s = dl[v & 1].read (d + (float) v * 0.0007f * sr);
                    if (v & 1) wr += s; else wl += s;
                }
                wl *= 2.f / (float) voices; wr *= 2.f / (float) voices;
            }
            else
            {
                const int m = mode - 1;
                ph += dimRate[m] / sr; ph -= std::floor (ph);
                const float tri = 1.f - 4.f * std::abs (ph - 0.5f);
                const float d1 = (0.004f + 0.0025f * dimDepth[m] * tri) * sr, d2 = (0.004f - 0.0025f * dimDepth[m] * tri) * sr;
                const float m1 = dl[0].read (d1) , m2 = dl[1].read (d2);
                wl = 0.5f * (dl_ + m1 - 0.4f * m2); wr = 0.5f * (dr + m2 - 0.4f * m1);
            }
            L[i] = mixf (dl_, wl, mix); R[i] = mixf (dr, wr, mix);
        }
    }
private:
    float sr = 48000, ph = 0; DelayLine dl[2];
};

class Phaser : public FxUnit
{
public:
    void prepare (double s, int) override { sr = (float) s; reset(); }
    void reset() override { for (auto& c : z) for (auto& v : c) v = 0; fb[0] = fb[1] = 0; ph = 0; }
    void process (float* L, float* R, int n, const float* p, const FxCtx&) override
    {
        const float mix = p[0], rate = p[1], depth = p[2], feedback = p[3];
        static const int st[5] = { 2, 4, 6, 8, 12 };
        const int stages = st[juce::jlimit (0, 4, (int) p[4])];
        const float centre = p[5];
        float* ch[2] = { L, R };
        for (int i = 0; i < n; ++i)
        {
            ph += rate / sr; ph -= std::floor (ph);
            for (int c = 0; c < 2; ++c)
            {
                const float lfo = std::sin (kTwoPi * (ph + (c ? 0.25f : 0.f)));
                const float f = clampf (centre * std::exp2 (lfo * depth * 2.5f), 30.f, sr * 0.45f);
                const float t = std::tan (kPi * f / sr); const float a = (t - 1.f) / (t + 1.f);
                const float dry = ch[c][i];
                float x = dry + fb[c] * feedback;
                for (int s = 0; s < stages; ++s) { const float y = a * x + z[c][s]; z[c][s] = x - a * y; x = y; }
                fb[c] = fastTanh (x);
                ch[c][i] = dry * (1.f - 0.5f * mix) + x * 0.5f * mix;
            }
        }
    }
private:
    float sr = 48000, ph = 0, fb[2] {}; float z[2][12] {};
};

class Flanger : public FxUnit
{
public:
    void prepare (double s, int) override { sr = (float) s; for (auto& d : dl) d.prepare ((int) (0.04f * sr)); reset(); }
    void reset() override { for (auto& d : dl) d.clear(); fb[0] = fb[1] = 0; ph = 0; }
    void process (float* L, float* R, int n, const float* p, const FxCtx&) override
    {
        const float mix = p[0], rate = p[1], depth = p[2], feedback = p[3], delayMs = p[4];
        float* ch[2] = { L, R };
        for (int i = 0; i < n; ++i)
        {
            ph += rate / sr; ph -= std::floor (ph);
            for (int c = 0; c < 2; ++c)
            {
                const float lfo = 0.5f + 0.5f * std::sin (kTwoPi * (ph + (c ? 0.25f : 0.f)));
                const float d = (0.1f + delayMs * (1.f - depth + depth * lfo)) * 0.001f * sr;
                const float dry = ch[c][i];
                const float w = dl[c].read (d);
                dl[c].push (dry + fastTanh (w * feedback));
                ch[c][i] = mixf (dry, (dry + w) * 0.7f, mix);
            }
        }
    }
private:
    float sr = 48000, ph = 0, fb[2] {}; DelayLine dl[2];
};

class Reverb : public FxUnit
{
public:
    void prepare (double s, int) override { fdn.prepare (s); }
    void reset() override { fdn.reset(); }
    bool isTailEffect() const override { return true; }
    void process (float* L, float* R, int n, const float* p, const FxCtx& ctx) override
    {
        const float mix = juce::jmax (p[0], ctx.reverbThrow);
        fdn.set ((int) p[1] == 1, p[2], p[3] * (1.f + ctx.reverbThrow * 2.f), p[4], p[5]);
        const float width = p[6];
        for (int i = 0; i < n; ++i)
        {
            float wl, wr; fdn.process (L[i], R[i], wl, wr);
            const float m = 0.5f * (wl + wr), sd = 0.5f * (wl - wr) * width;
            L[i] = L[i] * (1.f - mix * 0.5f) + (m + sd) * mix;
            R[i] = R[i] * (1.f - mix * 0.5f) + (m - sd) * mix;
        }
    }
private:
    Fdn fdn;
};

class Shimmer : public FxUnit
{
public:
    void prepare (double s, int) override { sr = (float) s; fdn.prepare (s); ps1.prepare (s); ps2.prepare (s); reset(); }
    void reset() override { fdn.reset(); ps1.reset(); ps2.reset(); fbL = fbR = 0; }
    bool isTailEffect() const override { return true; }
    void process (float* L, float* R, int n, const float* p, const FxCtx& ctx) override
    {
        const float mix = juce::jmax (p[0], ctx.reverbThrow * 0.7f), decay = p[1], amt = p[2], damp = p[4], size = p[5];
        const int interval = (int) p[3];
        const float r1 = interval == 1 ? 1.4983f : 2.f, r2 = interval == 2 ? 2.9966f : r1;
        fdn.set (true, size, decay, damp, 20.f);
        hp.setLP (300.f, sr);
        for (int i = 0; i < n; ++i)
        {
            const float sh = 0.5f * (ps1.process (fbL, r1) + ps2.process (fbR, r2));
            const float shc = sh - hp.lp (sh);
            float wl, wr; fdn.process (L[i] + shc * amt * 0.6f, R[i] + shc * amt * 0.6f, wl, wr);
            fbL = wl; fbR = wr;
            L[i] = L[i] * (1.f - mix * 0.5f) + wl * mix;
            R[i] = R[i] * (1.f - mix * 0.5f) + wr * mix;
        }
    }
private:
    float sr = 48000, fbL = 0, fbR = 0; Fdn fdn; PitchShifter ps1, ps2; OnePole hp;
};

class Delay : public FxUnit
{
public:
    void prepare (double s, int) override { sr = (float) s; for (auto& d : dl) d.prepare ((int) (2.6f * sr)); reset(); }
    void reset() override { for (auto& d : dl) d.clear(); for (auto& f : lp) f.y = 0; for (auto& f : hpf) f.y = 0; timeSm = -1; }
    bool isTailEffect() const override { return true; }
    void process (float* L, float* R, int n, const float* p, const FxCtx& ctx) override
    {
        const float mix = juce::jmax (p[0], ctx.delayThrow); const int mode = (int) p[1]; const bool sync = p[2] > 0.5f;
        float timeS = sync ? (float) (divisionBeats ((int) p[3]) * 60.0 / juce::jmax (20.0, ctx.bpm)) : p[4] * 0.001f;
        timeS = clampf (timeS, 0.005f, 2.5f);
        const float fb = juce::jmin (0.98f, p[5] + ctx.delayThrow * 0.3f), wow = p[6], tone = p[7];
        for (int c = 0; c < 2; ++c) { lp[c].setLP (tone < 0 ? lerp (16000.f, 1200.f, -tone) : 16000.f, sr); hpf[c].setLP (tone > 0 ? lerp (30.f, 1500.f, tone) : 30.f, sr); }
        const float target = timeS * sr;
        if (timeSm < 0) timeSm = target;
        for (int i = 0; i < n; ++i)
        {
            timeSm += (target - timeSm) * 0.0005f;
            wph += 0.8f / sr; wph -= std::floor (wph);
            const float d = timeSm * (1.f + (mode == 1 ? wow * 0.004f * std::sin (kTwoPi * wph) : 0.f));
            const float inL = L[i], inR = R[i];
            float yl = dl[0].read (d), yr = dl[1].read (d);
            auto shape = [&] (float y, int c) { y = hpf[c].hp (lp[c].lp (y)); return mode == 1 ? fastTanh (y * 1.3f) * 0.85f : y; };
            yl = shape (yl, 0); yr = shape (yr, 1);
            if (mode == 0)   // ping-pong: mono input into left, cross feedback
            {
                dl[0].push ((inL + inR) * 0.5f + yr * fb);
                dl[1].push (yl * fb);
            }
            else
            {
                dl[0].push (inL + yl * fb);
                dl[1].push (inR + yr * fb);
            }
            L[i] = inL + yl * mix; R[i] = inR + yr * mix;
        }
    }
private:
    float sr = 48000, wph = 0, timeSm = -1; DelayLine dl[2]; OnePole lp[2], hpf[2];
};

class OTT : public FxUnit
{
public:
    void prepare (double s, int) override { sr = (float) s; reset(); }
    void reset() override { for (auto& a : lr) for (auto& b : a) b.reset(); for (auto& e : env) e.y = 0; }
    void process (float* L, float* R, int n, const float* p, const FxCtx&) override
    {
        const float depth = p[0], time = p[1], up = p[2], down = p[3], gain = dbToGain (p[4]);
        // LR4 crossovers at 120 Hz and 2.5 kHz: [ch][0..3] lp1, [4..7] hp1, [8..11] lp2, [12..15] hp2
        for (int c = 0; c < 2; ++c)
        {
            for (int k = 0; k < 2; ++k) { lr[c][k].lowpass (120.f, 0.7071f, sr); lr[c][2 + k].highpass (120.f, 0.7071f, sr);
                                          lr[c][4 + k].lowpass (2500.f, 0.7071f, sr); lr[c][6 + k].highpass (2500.f, 0.7071f, sr); }
        }
        static const float att[3] = { 30, 12, 4 }, rel[3] = { 300, 160, 90 };
        for (int b = 0; b < 3; ++b) env[b].set (att[b] * time, rel[b] * time, sr);
        const float downThr = dbToGain (-30.f), upThr = dbToGain (-42.f);
        const float ratio = 1.f + down * 9.f;
        float* ch[2] = { L, R };
        for (int i = 0; i < n; ++i)
        {
            float band[3][2];
            for (int c = 0; c < 2; ++c)
            {
                const float x = ch[c][i];
                const float lo = lr[c][1].process (lr[c][0].process (x));
                const float rest = lr[c][3].process (lr[c][2].process (x));
                const float mid = lr[c][5].process (lr[c][4].process (rest));
                const float hi = lr[c][7].process (lr[c][6].process (rest));
                band[0][c] = lo; band[1][c] = mid; band[2][c] = hi;
            }
            float outL = 0, outR = 0;
            for (int b = 0; b < 3; ++b)
            {
                const float lev = env[b].process (juce::jmax (std::abs (band[b][0]), std::abs (band[b][1]))) + 1e-6f;
                float g = 1.f;
                if (lev > downThr) g = std::pow (lev / downThr, 1.f / ratio - 1.f);
                else if (lev < upThr) g = juce::jmin (dbToGain (24.f * up), std::pow (upThr / lev, up * 0.75f));
                g = lerp (1.f, g, depth) * (b == 1 ? 1.f : 1.1f);
                outL += band[b][0] * g; outR += band[b][1] * g;
            }
            L[i] = outL * gain; R[i] = outR * gain;
        }
    }
private:
    float sr = 48000; Biquad lr[2][8]; EnvFollower env[3];
};

class Transient : public FxUnit
{
public:
    void prepare (double s, int) override { sr = (float) s; reset(); }
    void reset() override { fast.y = slow.y = 0; }
    void process (float* L, float* R, int n, const float* p, const FxCtx&) override
    {
        const float a = p[0], s = p[1], gain = dbToGain (p[2]), speed = p[3];
        fast.set (lerp (3.f, 0.3f, speed), lerp (60.f, 20.f, speed), sr);
        slow.set (lerp (40.f, 10.f, speed), lerp (300.f, 120.f, speed), sr);
        for (int i = 0; i < n; ++i)
        {
            const float x = 0.5f * (std::abs (L[i]) + std::abs (R[i]));
            const float f = fast.process (x) + 1e-6f, sl = slow.process (x) + 1e-6f;
            const float diff = clampf ((f - sl) / sl, -1.f, 4.f);
            const float db = (diff > 0 ? a * diff * 9.f : -s * diff * 9.f);
            const float g = dbToGain (clampf (db, -24.f, 18.f)) * gain;
            L[i] *= g; R[i] *= g;
        }
    }
private:
    float sr = 48000; EnvFollower fast, slow;
};

class Distortion : public FxUnit
{
public:
    void prepare (double s, int maxBlock) override
    {
        sr = (float) s;
        os = std::make_unique<juce::dsp::Oversampling<float>> (2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, false);
        os->initProcessing ((size_t) maxBlock);
        reset();
    }
    void reset() override { if (os) os->reset(); tl[0].y = tl[1].y = 0; }
    void process (float* L, float* R, int n, const float* p, const FxCtx&) override
    {
        const float mix = p[0]; const int type = (int) p[1]; const float drive = dbToGain (p[2]), tone = p[3], bits = p[4], bias = p[5];
        float* chans[2] = { L, R };
        juce::dsp::AudioBlock<float> block (chans, 2, (size_t) n);
        // keep dry copy in the oversampled domain by mixing before downsampling
        auto up = os->processSamplesUp (block);
        const float q = std::exp2 (bits - 1.f);
        const float comp = 1.f / std::sqrt (drive);
        for (size_t c = 0; c < up.getNumChannels(); ++c)
        {
            auto* d = up.getChannelPointer (c);
            for (size_t i = 0; i < up.getNumSamples(); ++i)
            {
                const float dry = d[i];
                float x = dry * drive + bias * 0.5f;
                float y;
                switch (type)
                {
                    case 0: y = (fastTanh (x) - fastTanh (bias * 0.5f)) * (x > 0 ? 1.f : 0.85f); break;
                    case 1: { y = std::sin (x * 1.3f); } break;
                    case 2: { const float h = clampf (x, -1.f, 1.f); y = h * 0.7f + std::abs (h) * 0.5f - 0.25f; } break;
                    default: y = std::round (fastTanh (x) * q) / q; break;
                }
                if (type == 0 || type == 3) y *= juce::jmin (1.f, comp * 2.5f);
                d[i] = mixf (dry, y * (type == 1 ? 0.8f : 1.f), mix);
            }
        }
        os->processSamplesDown (block);
        for (int c = 0; c < 2; ++c)
        {
            tl[c].setLP (tone < 0 ? lerp (18000.f, 900.f, -tone) : lerp (200.f, 3000.f, tone), sr);
            for (int i = 0; i < n; ++i)
            {
                const float x = chans[c][i];
                chans[c][i] = tone < 0 ? tl[c].lp (x) : x + tone * 0.8f * tl[c].hp (x);
            }
        }
    }
private:
    float sr = 48000; std::unique_ptr<juce::dsp::Oversampling<float>> os; OnePole tl[2];
};

class FilterFx : public FxUnit
{
public:
    void prepare (double s, int) override { for (auto& f : flt) f.prepare (s, 16); }
    void reset() override { for (auto& f : flt) f.reset(); }
    void process (float* L, float* R, int n, const float* p, const FxCtx&) override
    {
        static const int types[3] = { FtSvfLP, FtSvfBP, FtSvfHP };
        for (auto& f : flt) f.set (types[juce::jlimit (0, 2, (int) p[1])], p[2], p[3], 0.f, p[4], 0.f);
        const float mix = p[0];
        for (int i = 0; i < n; ++i) { L[i] = mixf (L[i], flt[0].process (L[i]), mix); R[i] = mixf (R[i], flt[1].process (R[i]), mix); }
    }
private:
    VoiceFilter flt[2];
};

class EQ : public FxUnit
{
public:
    void prepare (double s, int) override { sr = (float) s; reset(); }
    void reset() override { for (auto& b : bq) for (auto& x : b) x.reset(); }
    void process (float* L, float* R, int n, const float* p, const FxCtx&) override
    {
        for (int c = 0; c < 2; ++c) { bq[c][0].lowShelf (p[1], p[0], sr); bq[c][1].peak (p[3], p[4], p[2], sr); bq[c][2].highShelf (p[6], p[5], sr); }
        for (int i = 0; i < n; ++i)
        {
            L[i] = bq[0][2].process (bq[0][1].process (bq[0][0].process (L[i])));
            R[i] = bq[1][2].process (bq[1][1].process (bq[1][0].process (R[i])));
        }
    }
private:
    float sr = 48000; Biquad bq[2][3];
};

class Ducker : public FxUnit
{
public:
    void prepare (double s, int) override { sr = (float) s; reset(); }
    void reset() override { sc.y = 0; freePh = 0; g = 1; }
    void process (float* L, float* R, int n, const float* p, const FxCtx& ctx) override
    {
        const bool host = p[0] > 0.5f; const float depth = p[1], attackMs = p[2], releaseMs = p[3], curve = p[4];
        static const double divs[4] = { 2.0, 1.0, 0.5, 0.25 };
        const double div = divs[juce::jlimit (0, 3, (int) p[5])];
        sc.set (attackMs, releaseMs, sr);
        const double beatsPerSample = ctx.bpm / 60.0 / ctx.sampleRate;
        for (int i = 0; i < n; ++i)
        {
            float target;
            if (host && ctx.scL != nullptr)
            {
                const float e = sc.process (0.5f * (ctx.scL[i] + (ctx.scR ? ctx.scR[i] : ctx.scL[i])));
                target = 1.f - depth * clamp01 (e * 3.f);
            }
            else
            {
                const double beat = ctx.playing ? ctx.ppq + (double) i * beatsPerSample : freePh;
                const double ph = std::fmod (beat, div) / div;
                const float tSec = (float) (ph * div * 60.0 / juce::jmax (20.0, ctx.bpm));
                const float a = attackMs * 0.001f, r = releaseMs * 0.001f;
                const float duck = tSec < a ? tSec / a : 1.f - shapeCurve (clamp01 ((tSec - a) / r), curve * 2.f - 1.f);
                target = 1.f - depth * duck;
            }
            g += (target - g) * 0.05f;
            L[i] *= g; R[i] *= g;
            if (! ctx.playing) freePh += beatsPerSample;
        }
    }
private:
    float sr = 48000, g = 1; double freePh = 0; EnvFollower sc;
};

class Lofi : public FxUnit
{
public:
    void prepare (double s, int) override { sr = (float) s; for (auto& d : dl) d.prepare ((int) (0.02f * sr)); reset(); }
    void reset() override { acc = 1; holdL = holdR = 0; for (auto& d : dl) d.clear(); }
    void process (float* L, float* R, int n, const float* p, const FxCtx&) override
    {
        const float mix = p[0], rate = p[1], bits = p[2], jitter = p[3], nz = p[4], wobble = p[5];
        const float q = std::exp2 (bits - 1.f);
        for (int i = 0; i < n; ++i)
        {
            wph += 0.9f / sr; wph -= std::floor (wph);
            const float d = (0.003f + wobble * 0.002f * (0.5f + 0.5f * std::sin (kTwoPi * wph))) * sr;
            dl[0].push (L[i]); dl[1].push (R[i]);
            const float inL = dl[0].read (d), inR = dl[1].read (d);
            acc += rate * (1.f + jitter * 0.5f * rng.bi()) / sr;
            if (acc >= 1.f) { acc -= std::floor (acc); holdL = std::round (inL * q) / q; holdR = std::round (inR * q) / q; }
            const float noise = nz * 0.015f * rng.bi();
            L[i] = mixf (L[i], holdL + noise, mix); R[i] = mixf (R[i], holdR + noise, mix);
        }
    }
private:
    float sr = 48000, acc = 1, holdL = 0, holdR = 0, wph = 0; DelayLine dl[2]; Rng rng;
};

class Widener : public FxUnit
{
public:
    void prepare (double s, int) override { sr = (float) s; dl.prepare ((int) (0.035f * sr)); reset(); }
    void reset() override { dl.clear(); sideLp.y = 0; }
    void process (float* L, float* R, int n, const float* p, const FxCtx&) override
    {
        const float width = p[0], haas = p[1] * 0.001f * sr; sideLp.setLP (p[2], sr);
        for (int i = 0; i < n; ++i)
        {
            dl.push (R[i]);
            const float r = haas > 0.5f ? dl.read (haas) : R[i];
            const float m = 0.5f * (L[i] + r);
            float s = 0.5f * (L[i] - r) * width;
            s -= sideLp.lp (s);   // bass mono
            L[i] = m + s; R[i] = m - s;
        }
    }
private:
    float sr = 48000; DelayLine dl; OnePole sideLp;
};
} // namespace

std::unique_ptr<FxUnit> createFx (int type)
{
    switch (type)
    {
        case FxTape: return std::make_unique<Tape>();
        case FxCassette: return std::make_unique<Cassette>();
        case FxChorus: return std::make_unique<Chorus>();
        case FxPhaser: return std::make_unique<Phaser>();
        case FxFlanger: return std::make_unique<Flanger>();
        case FxReverb: return std::make_unique<Reverb>();
        case FxShimmer: return std::make_unique<Shimmer>();
        case FxDelay: return std::make_unique<Delay>();
        case FxOTT: return std::make_unique<OTT>();
        case FxTransient: return std::make_unique<Transient>();
        case FxDistortion: return std::make_unique<Distortion>();
        case FxFilter: return std::make_unique<FilterFx>();
        case FxEQ: return std::make_unique<EQ>();
        case FxDucker: return std::make_unique<Ducker>();
        case FxLofi: return std::make_unique<Lofi>();
        default: return std::make_unique<Widener>();
    }
}

FxRack::FxRack() { for (int t = 0; t < kNumFx; ++t) units[t] = createFx (t); }
FxRack::~FxRack() = default;

void FxRack::prepare (double sr, int maxBlock)
{
    sampleRate = sr;
    for (auto& u : units) u->prepare (sr, maxBlock);
    busB.setSize (2, maxBlock, false, true, true);
}

void FxRack::reset() { for (auto& u : units) u->reset(); }
void FxRack::killTails() { for (auto& u : units) if (u->isTailEffect()) u->reset(); }

void FxRack::process (juce::AudioBuffer<float>& buffer, int n, const float* params, const EngineState& es, const FxCtx& ctx)
{
    const auto& I = IX();
    bool anyB = false;
    for (int t = 0; t < kNumFx; ++t)
        if (params[I.fx[t].on] > 0.5f && params[I.fx[t].bus] > 0.5f) anyB = true;
    if (anyB)
        for (int c = 0; c < 2; ++c) busB.copyFrom (c, 0, buffer, c, 0, n);

    float p[kMaxFxParams];
    for (int k = 0; k < kNumFx; ++k)
    {
        const int t = juce::jlimit (0, kNumFx - 1, (int) es.fxOrder[k]);
        const auto& x = I.fx[t];
        const bool throwActive = (t == FxReverb && ctx.reverbThrow > 0.001f) || (t == FxDelay && ctx.delayThrow > 0.001f);
        if (params[x.on] < 0.5f && ! throwActive) continue;
        for (int q = 0; q < kMaxFxParams; ++q) p[q] = x.p[q] >= 0 ? params[x.p[q]] : 0.f;
        if (params[x.on] < 0.5f) p[0] = 0.f;   // throw-only: dry passthrough plus throw mix
        const bool onB = params[x.bus] > 0.5f && params[x.on] > 0.5f;
        auto& b = onB ? busB : buffer;
        units[t]->process (b.getWritePointer (0), b.getWritePointer (1), n, p, ctx);
    }
    const float la = params[I.busALvl], lb = params[I.busBLvl];
    if (anyB)
    {
        const float k = 0.70710678f;
        for (int c = 0; c < 2; ++c)
        {
            auto* d = buffer.getWritePointer (c); const auto* s = busB.getReadPointer (c);
            for (int i = 0; i < n; ++i) d[i] = (d[i] * la + s[i] * lb) * k;
        }
    }
    else if (la < 0.999f) buffer.applyGain (0, n, la);
}
} // namespace pd
