#include "Perform.h"

namespace pd
{
// ============================================================ grooves
// 16-step velocity maps (0 = rest). Original patterns.
static const float kGroove[12][16] = {
    { 1, .8f, .9f, .8f, 1, .8f, .9f, .8f, 1, .8f, .9f, .8f, 1, .8f, .9f, .8f },          // Straight
    { 1, 0, .6f, 0, 0, .8f, 0, .5f, 0, 0, .9f, 0, 0, .7f, 0, .4f },                     // 2-Step A
    { 1, 0, 0, .7f, 0, 0, .9f, 0, .5f, 0, 0, .8f, 0, .6f, 0, 0 },                       // 2-Step B
    { 1, 0, .5f, .7f, 0, .6f, 1, 0, .6f, 0, .5f, .8f, 0, .6f, .9f, 0 },                 // Garage Shuffle
    { 1, 0, .5f, 0, .9f, 0, 0, .6f, 0, .7f, .5f, 0, .9f, 0, .4f, 0 },                   // Breakbeat A
    { 1, 0, 0, .6f, .9f, 0, .5f, 0, 0, .8f, 0, .5f, 1, 0, .6f, .4f },                   // Breakbeat B
    { 1, 0, 0, .8f, 0, 0, .7f, 0, 0, .9f, 0, 0, .6f, 0, .8f, 0 },                       // Broken Techno
    { 1, 0, 0, 0, 0, 0, .7f, 0, .9f, 0, 0, 0, 0, 0, .5f, 0 },                           // Halftime
    { 1, .5f, .7f, .5f, .9f, .5f, .7f, .5f, 1, .5f, .7f, .5f, .9f, .5f, .7f, .6f },     // Rolling 16ths
    { 1, 0, .6f, .4f, 0, .7f, 0, .4f, .9f, .4f, 0, .6f, 0, .4f, .8f, .4f },             // Skippy
    { 1, 0, 0, .8f, 0, 0, .9f, 0, 1, 0, 0, .8f, 0, 0, .9f, 0 },                         // Dembow-ish
    { 1, 0, 0, .8f, 0, 0, .9f, 0, 0, 0, .8f, 0, 0, 0, .6f, 0 },                         // Tresillo
};
float ArpChord::grooveVelocity (int g, int step) noexcept { return kGroove[juce::jlimit (0, 11, g)][step & 15]; }

static int chordIntervals (int type, const EngineState& es, int* out)
{
    static const int iv[12][6] = { {}, { 0, 3, 7, 10 }, { 0, 3, 7, 10, 14 }, { 0, 4, 7, 11 }, { 0, 2, 7 }, { 0, 5, 7 },
                                   { 0, 3, 7, 10, 14, 17 }, { 0, 3, 7, 12, 15 }, { 0, 7 }, { 0, 12 }, { 0, 3, 7 }, { 0, 4, 7 } };
    static const int cnt[12] = { 0, 4, 5, 4, 3, 3, 6, 5, 2, 2, 3, 3 };
    if (type <= 0) { for (int i = 0; i < es.numChordNotes; ++i) out[i] = es.chordMemory[i]; return juce::jmax (1, es.numChordNotes); }
    type = juce::jlimit (1, 11, type);
    for (int i = 0; i < cnt[type]; ++i) out[i] = iv[type][i];
    return cnt[type];
}

// ============================================================ ArpChord
void ArpChord::prepare (double sampleRate) { sr = sampleRate; out.ensureSize (16384); reset(); }

void ArpChord::reset()
{
    numPending = numHeld = numPhysical = numLatched = numSounding = 0;
    now = 0; lastStepIndex = -1; arpPos = 0; arpDir = 1; freeBeats = 0;
}

void ArpChord::schedule (int64_t at, int note, int vel, bool on) noexcept
{
    if (numPending >= (int) std::size (pending) || note < 0 || note > 127) return;
    pending[numPending++] = { at, note, vel, on };
}

void ArpChord::addHeld (int note, int vel) noexcept
{
    for (int i = 0; i < numHeld; ++i) if (held[i] == note) return;
    if (numHeld < 64) { held[numHeld] = note; heldVel[numHeld] = vel; ++numHeld; }
}
void ArpChord::removeHeld (int note) noexcept
{
    for (int i = 0; i < numHeld; ++i)
        if (held[i] == note) { for (int j = i; j < numHeld - 1; ++j) { held[j] = held[j + 1]; heldVel[j] = heldVel[j + 1]; } --numHeld; return; }
}

int ArpChord::heldNotes (int* out, int max) const noexcept
{
    int n = juce::jmin (max, numPhysical);
    for (int i = 0; i < n; ++i) out[i] = physical[i];
    return n;
}

void ArpChord::process (juce::MidiBuffer& midi, int n, const float* P, const EngineState& es, const Transport& tr)
{
    const auto& I = IX();
    const bool chordOn = P[I.chordOn] > 0.5f, arpOn = P[I.arpOn] > 0.5f, latch = P[I.arpLatch] > 0.5f;
    out.clear();
    const int strumSamples = (int) (P[I.strum] * 0.001 * sr);
    int iv[8]; const int nIv = chordOn ? chordIntervals ((int) P[I.chordType], es, iv) : 1;
    if (! chordOn) iv[0] = 0;

    // arp switched off -> silence its notes
    if (! arpOn && numSounding > 0)
    {
        for (int i = 0; i < numSounding; ++i) out.addEvent (juce::MidiMessage::noteOff (1, soundingArp[i]), 0);
        numSounding = 0; numHeld = 0; numLatched = 0;
    }

    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        const int pos = meta.samplePosition;
        if (m.isNoteOn())
        {
            const int root = m.getNoteNumber(), vel = m.getVelocity();
            if (numPhysical < 16) physical[numPhysical++] = root;
            if (arpOn)
            {
                if (latch && latchArmed) { numHeld = 0; latchArmed = false; }
                for (int k = 0; k < nIv; ++k) addHeld (root + iv[k], vel);
            }
            else
                for (int k = 0; k < nIv; ++k) schedule (now + pos + k * strumSamples, root + iv[k], vel, true);
        }
        else if (m.isNoteOff())
        {
            const int root = m.getNoteNumber();
            for (int i = 0; i < numPhysical; ++i) if (physical[i] == root) { physical[i] = physical[--numPhysical]; break; }
            if (arpOn)
            {
                if (latch) { if (numPhysical == 0) latchArmed = true; }
                else for (int k = 0; k < nIv; ++k) removeHeld (root + iv[k]);
            }
            else
                for (int k = 0; k < nIv; ++k) schedule (now + pos + k * strumSamples, root + iv[k], 0, false);
        }
        else out.addEvent (m, pos);
    }

    if (arpOn && numHeld > 0)
    {
        const double bps = tr.bpm / 60.0 / sr;
        const double stepBeats = divisionBeats ((int) P[I.arpDiv]);
        const double swing = P[I.arpSwing] / 100.0;
        const int stepLen = (int) (stepBeats / bps);
        for (int i = 0; i < n; ++i)
        {
            const double beat = tr.playing ? tr.ppqAtBlockStart + (double) i * bps : freeBeats + (double) i * bps;
            const double pair = beat / (2.0 * stepBeats);
            const double f = pair - std::floor (pair);
            const double stepIndex = 2.0 * std::floor (pair) + (f >= swing ? 1.0 : 0.0);
            if (stepIndex == lastStepIndex) continue;
            lastStepIndex = stepIndex;
            const int step = (int) std::fmod (stepIndex, 16.0);
            stepCounter = step;
            const float gv = grooveVelocity ((int) P[I.arpGroove], step);
            if (gv <= 0.f || rng.uni() > P[I.arpProb]) continue;

            // build the note list
            int list[256]; int nl = 0;
            const int oct = (int) P[I.arpOct];
            const int mode = (int) P[I.arpMode];
            int base[64]; for (int k = 0; k < numHeld; ++k) base[k] = held[k];
            if (mode != 4) std::sort (base, base + numHeld);
            for (int o = 0; o < oct; ++o) for (int k = 0; k < numHeld; ++k) list[nl++] = base[k] + 12 * o;
            const int ratchet = (int) P[I.arpRatchet];
            const int sub = juce::jmax (1, stepLen / ratchet);
            const int gate = juce::jmax (8, (int) ((float) sub * P[I.arpGate]));
            auto play = [&] (int note, int vel) {
                for (int r = 0; r < ratchet; ++r)
                {
                    schedule (now + i + r * sub, note, vel, true);
                    schedule (now + i + r * sub + gate, note, 0, false);
                }
                if (numSounding < 8) soundingArp[numSounding++] = note; else soundingArp[numSounding - 1] = note;
            };
            numSounding = 0;
            const int baseVel = numHeld > 0 ? heldVel[0] : 100;
            const int vel = juce::jlimit (1, 127, (int) ((float) baseVel * (0.55f + 0.45f * gv)));
            if (mode == 5) { for (int k = 0; k < juce::jmin (nl, 8); ++k) play (list[k], vel); continue; }
            int idx = 0;
            switch (mode)
            {
                case 0: case 4: idx = arpPos % nl; arpPos = (arpPos + 1) % nl; break;
                case 1: idx = nl - 1 - (arpPos % nl); arpPos = (arpPos + 1) % nl; break;
                case 2:
                    if (nl == 1) { idx = 0; break; }
                    idx = juce::jlimit (0, nl - 1, arpPos);
                    arpPos += arpDir;
                    if (arpPos >= nl - 1) { arpPos = nl - 1; arpDir = -1; } else if (arpPos <= 0) { arpPos = 0; arpDir = 1; }
                    break;
                default: idx = rng.range (nl); break;
            }
            play (list[idx], vel);
        }
    }
    else if (arpOn) lastStepIndex = -1;

    // dispatch scheduled events falling into this block
    for (int k = 0; k < numPending;)
    {
        const auto& e = pending[k];
        if (e.at < now + n)
        {
            const int pos = (int) juce::jlimit<int64_t> (0, n - 1, e.at - now);
            out.addEvent (e.on ? juce::MidiMessage::noteOn (1, e.note, (juce::uint8) juce::jlimit (1, 127, e.vel))
                               : juce::MidiMessage::noteOff (1, e.note), pos);
            pending[k] = pending[--numPending];
        }
        else ++k;
    }
    now += n;
    freeBeats += (double) n * tr.bpm / 60.0 / sr;
    midi.swapWith (out);
}

// ============================================================ riser
bool RiserEngine::process (int n, bool armed, int barsChoice, const Transport& tr) noexcept
{
    bool drop = false;
    if (armed && ! wasArmed) { active = true; elapsedBeats = 0; }
    if (active && armed)
    {
        static const double bars[4] = { 4, 8, 16, 32 };
        elapsedBeats += (double) n * tr.bpm / 60.0 / sr;
        p = (float) juce::jmin (1.0, elapsedBeats / (bars[juce::jlimit (0, 3, barsChoice)] * 4.0));
    }
    if (! armed && wasArmed && active) { active = false; p = 0; drop = true; }
    wasArmed = armed;
    return drop;
}

float RiserEngine::noiseSample (float amount) noexcept
{
    if (! active || amount <= 0.f) return 0.f;
    const float w = rng.bi();
    const float c1 = 0.02f + 0.6f * p * p, c2 = 0.01f + 0.3f * p;
    bp1 += c1 * (w - bp1); bp2 += c2 * (bp1 - bp2);
    return (bp1 - bp2) * amount * p * p * 1.5f;
}

// ============================================================ stutter / tape stop
void BeatRepeat::prepare (double sampleRate, int)
{
    sr = sampleRate;
    size = (int) (sampleRate * 4.0);
    hL.assign ((size_t) size, 0.f); hR.assign ((size_t) size, 0.f);
    w = 0;
}

void BeatRepeat::process (float* L, float* R, int n, bool stutter, double stutterBeats, bool tapeStop, float stopTime, double bpm) noexcept
{
    if (size == 0) return;
    if (stutter && ! wasStutter)
    {
        loopLen = juce::jlimit (64, size / 2, (int) (stutterBeats * 60.0 / juce::jmax (20.0, bpm) * sr));
        loopStart = (w - loopLen + size) % size; loopPos = 0;
    }
    if (tapeStop && ! wasStop) { readPos = (double) ((w - 1 + size) % size); rate = 1.f; }
    const bool engaged = stutter || tapeStop;
    if (engaged != (wasStutter || wasStop)) xfade = 0.f;
    wasStutter = stutter; wasStop = tapeStop;

    const int fadeLen = 64;
    for (int i = 0; i < n; ++i)
    {
        const float inL = L[i], inR = R[i];
        float oL = inL, oR = inR;
        if (stutter)
        {
            const int idx = (loopStart + loopPos) % size;
            float env = 1.f;
            if (loopPos < fadeLen) env = (float) loopPos / fadeLen;
            else if (loopPos > loopLen - fadeLen) env = (float) (loopLen - loopPos) / fadeLen;
            oL = hL[(size_t) idx] * env; oR = hR[(size_t) idx] * env;
            if (++loopPos >= loopLen) loopPos = 0;
        }
        else
        {
            hL[(size_t) w] = inL; hR[(size_t) w] = inR; w = (w + 1) % size;
            if (tapeStop)
            {
                rate = juce::jmax (0.f, rate - 1.f / (juce::jmax (0.05f, stopTime) * (float) sr));
                const float shaped = rate * rate;
                readPos += shaped;
                if (readPos >= size) readPos -= size;
                const int i0 = (int) readPos; const float t = (float) (readPos - i0);
                const int i1 = (i0 + 1) % size;
                oL = (hL[(size_t) i0] + (hL[(size_t) i1] - hL[(size_t) i0]) * t) * juce::jmin (1.f, shaped * 4.f);
                oR = (hR[(size_t) i0] + (hR[(size_t) i1] - hR[(size_t) i0]) * t) * juce::jmin (1.f, shaped * 4.f);
            }
        }
        if (xfade < 1.f)
        {
            xfade = juce::jmin (1.f, xfade + 1.f / 256.f);
            // fade from the opposite state
            const float dl = engaged ? inL : oL, dr = engaged ? inR : oR;
            oL = engaged ? lerp (dl, oL, xfade) : lerp (oL, inL, xfade);
            oR = engaged ? lerp (dr, oR, xfade) : lerp (oR, inR, xfade);
            (void) dl; (void) dr;
        }
        L[i] = oL; R[i] = oR;
    }
}

// ============================================================ chop pad
void ChopPlayer::prepare (double sampleRate) { sr = sampleRate; stopAll(); }

void ChopPlayer::trigger (int slot, float velocity, const Sample* s, float pitchSt, float formantSt) noexcept
{
    if (s == nullptr || s->length() < 64) return;
    auto& v = voices[rr]; rr = (rr + 1) % 8;
    const int ns = juce::jmax (1, s->numSlices);
    const int si = slot % ns;
    v.s = s;
    v.srcPos = s->sliceStart[si];
    v.srcEnd = s->sliceEnd (si);
    v.srcRate = s->sampleRate / sr;
    v.pitchRatio = semitonesToRatio (pitchSt);
    v.formantRatio = semitonesToRatio (formantSt);
    v.periodSrc = (float) (s->sampleRate / juce::jmax (60.f, s->rootHz));
    v.nextGrain = 0; v.vel = velocity; v.env = 1.f; v.age = 0;
    for (auto& g : v.g) g.on = false;
    v.on = true;
    lastSlot = slot;
}

void ChopPlayer::process (float* L, float* R, int n, float level) noexcept
{
    for (auto& v : voices)
    {
        if (! v.on) continue;
        const auto* s = v.s;
        const float spacing = (v.periodSrc / (float) v.srcRate) / v.pitchRatio;     // output samples between grains
        const float glen = 2.f * v.periodSrc / (v.formantRatio * (float) v.srcRate); // output samples per grain
        const float norm = juce::jmin (1.5f, 2.f * spacing / juce::jmax (1.f, glen));
        for (int i = 0; i < n; ++i)
        {
            const bool spawning = v.srcPos < v.srcEnd - v.periodSrc;
            if (spawning && v.nextGrain <= 0.f)
                for (auto& g : v.g)
                    if (! g.on) { g.on = true; g.age = 0; g.len = glen; g.pos = juce::jmax (0.0, v.srcPos - v.periodSrc); break; }
            if (spawning) v.nextGrain += (v.nextGrain <= 0.f ? spacing : 0.f);
            v.nextGrain -= 1.f;
            float l = 0, r = 0; bool any = false;
            for (auto& g : v.g)
            {
                if (! g.on) continue;
                any = true;
                const float ph = g.age / g.len;
                const float w = 0.5f - 0.5f * std::cos (kTwoPi * ph);
                l += s->read (0, g.pos) * w;
                r += (s->stereo() ? s->read (1, g.pos) : s->read (0, g.pos)) * w;
                g.pos += v.formantRatio * v.srcRate;
                g.age += 1.f;
                if (g.age >= g.len || g.pos >= s->length() - 2) g.on = false;
            }
            v.srcPos += v.srcRate;
            const float remain = (float) ((v.srcEnd - v.srcPos) / v.srcRate);
            const float fade = clamp01 (remain / (0.005f * (float) sr));
            const float g = norm * v.vel * level * (spawning ? 1.f : 1.f) * juce::jmax (fade, 0.f);
            L[i] += l * g; R[i] += r * g;
            if (! spawning && ! any) { v.on = false; break; }
        }
    }
}
} // namespace pd
