#include "Params.h"

namespace pd
{
namespace choices
{
#define PD_CHOICES(fn, ...) const juce::StringArray& fn() { static const juce::StringArray a { __VA_ARGS__ }; return a; }
PD_CHOICES (oscEngines, "Wavetable", "Multisample", "Granular", "Spectral", "Analog")
PD_CHOICES (warpModes, "Off", "Bend +", "Bend -", "Sync", "PWM", "Mirror", "Quantize", "FM < B", "RM < C")
PD_CHOICES (vaShapes, "Saw", "Pulse", "Triangle", "Supersaw")
PD_CHOICES (sampleModes, "One-Shot", "Loop", "Slice")
PD_CHOICES (oscRoutes, "Filter 1", "Filter 2", "Both", "Direct")
PD_CHOICES (subShapes, "Sine", "Triangle", "Square")
PD_CHOICES (noiseTypes, "White", "Pink", "Vinyl", "Tape Hiss", "Crowd", "Rain", "Cassette Flutter")
PD_CHOICES (filterTypes, "MG Ladder", "Diode", "SVF LP", "SVF BP", "SVF HP", "SVF Notch", "SVF Peak",
            "Comb +", "Comb -", "Formant", "Rave Ladder")
PD_CHOICES (filterRouting, "Serial", "Parallel", "Split")
PD_CHOICES (envModes, "ADSR", "MSEG")
PD_CHOICES (lfoShapes, "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "S&H", "Smooth Random", "Drawn")
PD_CHOICES (lfoModes, "Free", "Retrig", "One-Shot", "Envelope")
PD_CHOICES (oversampling, "1x", "2x", "4x", "8x")
PD_CHOICES (arpModes, "Up", "Down", "Up/Down", "Random", "As Played", "Chord")
PD_CHOICES (chordTypes, "Memory", "Minor 7", "Minor 9", "Major 7", "Sus 2", "Sus 4", "Minor 11", "Rave Stab",
            "Fifths", "Octaves", "Minor Triad", "Major Triad")
PD_CHOICES (grooves, "Straight", "2-Step A", "2-Step B", "Garage Shuffle", "Breakbeat A", "Breakbeat B",
            "Broken Techno", "Halftime", "Rolling 16ths", "Skippy", "Dembow-ish", "Tresillo")
PD_CHOICES (riserBars, "4 Bars", "8 Bars", "16 Bars", "32 Bars")
PD_CHOICES (fxBus, "A", "B")
#undef PD_CHOICES
}

// ---------------------------------------------------------------- FX table
static const FxDef kFxDefs[kNumFx] = {
    { "tape", "Tape", 5, { { "mix", "Mix", 0, 1, 1, 0, 0, nullptr }, { "wow", "Wow", 0, 1, 0.25f, 0, 0, nullptr },
        { "flutter", "Flutter", 0, 1, 0.2f, 0, 0, nullptr }, { "sat", "Saturation", 0, 1, 0.35f, 0, 0, nullptr },
        { "age", "Age", 0, 1, 0.3f, 0, 0, nullptr } } },
    { "cassette", "Cassette", 6, { { "mix", "Mix", 0, 1, 1, 0, 0, nullptr }, { "wear", "Wear", 0, 1, 0.4f, 0, 0, nullptr },
        { "flutter", "Flutter", 0, 1, 0.3f, 0, 0, nullptr }, { "drive", "Drive", 0, 1, 0.3f, 0, 0, nullptr },
        { "comp", "Comp", 0, 1, 0.4f, 0, 0, nullptr }, { "hiss", "Hiss", 0, 1, 0.2f, 0, 0, nullptr } } },
    { "chorus", "Hyper/Dimension", 5, { { "mix", "Mix", 0, 1, 0.5f, 0, 0, nullptr },
        { "mode", "Mode", 0, 4, 0, 0, 5, "Hyper|Dim I|Dim II|Dim III|Dim IV" },
        { "rate", "Rate", 0.02f, 8, 0.6f, 0.8f, 0, nullptr }, { "depth", "Depth", 0, 1, 0.5f, 0, 0, nullptr },
        { "voices", "Voices", 2, 8, 4, 0, 0, nullptr } } },
    { "phaser", "Phaser", 6, { { "mix", "Mix", 0, 1, 0.5f, 0, 0, nullptr }, { "rate", "Rate", 0.02f, 10, 0.3f, 1, 0, nullptr },
        { "depth", "Depth", 0, 1, 0.6f, 0, 0, nullptr }, { "fb", "Feedback", -0.95f, 0.95f, 0.4f, 0, 0, nullptr },
        { "stages", "Stages", 0, 4, 2, 0, 5, "2|4|6|8|12" }, { "freq", "Centre", 100, 8000, 900, 900, 0, nullptr } } },
    { "flanger", "Flanger", 5, { { "mix", "Mix", 0, 1, 0.5f, 0, 0, nullptr }, { "rate", "Rate", 0.02f, 10, 0.2f, 1, 0, nullptr },
        { "depth", "Depth", 0, 1, 0.6f, 0, 0, nullptr }, { "fb", "Feedback", -0.95f, 0.95f, 0.5f, 0, 0, nullptr },
        { "delay", "Delay", 0.2f, 15, 2, 3, 0, nullptr } } },
    { "reverb", "Plate/Hall", 7, { { "mix", "Mix", 0, 1, 0.25f, 0, 0, nullptr }, { "type", "Type", 0, 1, 0, 0, 2, "Plate|Hall" },
        { "size", "Size", 0, 1, 0.6f, 0, 0, nullptr }, { "decay", "Decay", 0.2f, 20, 2.5f, 3, 0, nullptr },
        { "damp", "Damping", 0, 1, 0.4f, 0, 0, nullptr }, { "predelay", "Pre-Delay", 0, 250, 15, 0, 0, nullptr },
        { "width", "Width", 0, 1, 1, 0, 0, nullptr } } },
    { "shimmer", "Shimmer", 6, { { "mix", "Mix", 0, 1, 0.3f, 0, 0, nullptr }, { "decay", "Decay", 0.5f, 30, 6, 6, 0, nullptr },
        { "shimmer", "Shimmer", 0, 1, 0.5f, 0, 0, nullptr }, { "interval", "Interval", 0, 2, 0, 0, 3, "Octave|Fifth|Oct+Fifth" },
        { "damp", "Damping", 0, 1, 0.35f, 0, 0, nullptr }, { "size", "Size", 0, 1, 0.8f, 0, 0, nullptr } } },
    { "delay", "Delay", 8, { { "mix", "Mix", 0, 1, 0.25f, 0, 0, nullptr }, { "mode", "Mode", 0, 2, 0, 0, 3, "Ping-Pong|Tape|Stereo" },
        { "sync", "Sync", 0, 1, 1, 0, 2, "Off|On" }, { "div", "Division", 0, 16, 9, 0, 17, "DIV" },
        { "time", "Time", 10, 2000, 375, 400, 0, nullptr }, { "fb", "Feedback", 0, 0.98f, 0.45f, 0, 0, nullptr },
        { "wow", "Wow", 0, 1, 0.15f, 0, 0, nullptr }, { "tone", "Tone", -1, 1, 0, 0, 0, nullptr } } },
    { "ott", "Multiband OTT", 5, { { "depth", "Depth", 0, 1, 0.5f, 0, 0, nullptr }, { "time", "Time", 0.1f, 4, 1, 1, 0, nullptr },
        { "up", "Upward", 0, 1, 0.6f, 0, 0, nullptr }, { "down", "Downward", 0, 1, 0.7f, 0, 0, nullptr },
        { "gain", "Out Gain", -18, 18, 0, 0, 0, nullptr } } },
    { "transient", "Transient Shaper", 4, { { "attack", "Attack", -1, 1, 0.3f, 0, 0, nullptr },
        { "sustain", "Sustain", -1, 1, 0, 0, 0, nullptr }, { "gain", "Out Gain", -18, 18, 0, 0, 0, nullptr },
        { "speed", "Speed", 0, 1, 0.5f, 0, 0, nullptr } } },
    { "dist", "Distortion", 6, { { "mix", "Mix", 0, 1, 1, 0, 0, nullptr }, { "type", "Type", 0, 3, 0, 0, 4, "Tube|Fold|Fuzz|Bit" },
        { "drive", "Drive", 0, 36, 9, 0, 0, nullptr }, { "tone", "Tone", -1, 1, 0, 0, 0, nullptr },
        { "bits", "Bits", 2, 16, 8, 0, 0, nullptr }, { "bias", "Bias", -1, 1, 0, 0, 0, nullptr } } },
    { "filter", "Filter", 5, { { "mix", "Mix", 0, 1, 1, 0, 0, nullptr }, { "type", "Type", 0, 2, 0, 0, 3, "LP|BP|HP" },
        { "cutoff", "Cutoff", 20, 20000, 8000, 1000, 0, nullptr }, { "res", "Resonance", 0, 1, 0.2f, 0, 0, nullptr },
        { "drive", "Drive", 0, 1, 0, 0, 0, nullptr } } },
    { "eq", "EQ", 7, { { "lowg", "Low Gain", -18, 18, 0, 0, 0, nullptr }, { "lowf", "Low Freq", 30, 500, 110, 120, 0, nullptr },
        { "midg", "Mid Gain", -18, 18, 0, 0, 0, nullptr }, { "midf", "Mid Freq", 200, 8000, 1200, 1200, 0, nullptr },
        { "midq", "Mid Q", 0.2f, 6, 0.9f, 1, 0, nullptr }, { "highg", "High Gain", -18, 18, 0, 0, 0, nullptr },
        { "highf", "High Freq", 1500, 18000, 7000, 6000, 0, nullptr } } },
    { "ducker", "Sidechain Ducker", 6, { { "source", "Source", 0, 1, 0, 0, 2, "Internal 4/4|Host Sidechain" },
        { "depth", "Depth", 0, 1, 0.6f, 0, 0, nullptr }, { "attack", "Attack", 0.1f, 50, 2, 5, 0, nullptr },
        { "release", "Release", 20, 800, 180, 180, 0, nullptr }, { "curve", "Curve", 0, 1, 0.5f, 0, 0, nullptr },
        { "div", "Rate", 0, 3, 1, 0, 4, "1/2|1/4|1/8|1/16" } } },
    { "lofi", "Lo-Fi Degrade", 6, { { "mix", "Mix", 0, 1, 1, 0, 0, nullptr }, { "rate", "Sample Rate", 1000, 48000, 22050, 12000, 0, nullptr },
        { "bits", "Bits", 3, 16, 12, 0, 0, nullptr }, { "jitter", "Jitter", 0, 1, 0.1f, 0, 0, nullptr },
        { "noise", "Noise", 0, 1, 0.1f, 0, 0, nullptr }, { "wobble", "Wobble", 0, 1, 0.2f, 0, 0, nullptr } } },
    { "widener", "Stereo Widener", 3, { { "width", "Width", 0, 2, 1.2f, 0, 0, nullptr }, { "haas", "Haas", 0, 30, 0, 0, 0, nullptr },
        { "bassmono", "Bass Mono", 20, 500, 120, 120, 0, nullptr } } },
};

const FxDef& fxDef (int type) { return kFxDefs[juce::jlimit (0, kNumFx - 1, type)]; }

// ---------------------------------------------------------------- table
const ParamTable& ParamTable::get()
{
    static const ParamTable t;
    return t;
}

int ParamTable::indexOf (const juce::String& id) const
{
    auto it = lookup.find (id.toStdString());
    return it == lookup.end() ? -1 : it->second;
}

int ParamTable::add (ParamInfo p)
{
    jassert (lookup.find (p.id.toStdString()) == lookup.end());
    const int i = (int) params.size();
    lookup[p.id.toStdString()] = i;
    if (p.modulatable && (p.kind == PKind::Float))
        modDests.push_back (i);
    params.push_back (std::move (p));
    return i;
}

int ParamTable::addFloat (juce::String id, juce::String name, juce::String group, float lo, float hi, float def,
                          float skewCentre, juce::String unit, bool mod)
{
    ParamInfo p;
    p.id = id; p.name = name; p.group = group; p.kind = PKind::Float;
    p.range = juce::NormalisableRange<float> (lo, hi);
    if (skewCentre > lo && skewCentre < hi)
        p.range.setSkewForCentre (skewCentre);
    p.def = def; p.unit = unit; p.modulatable = mod;
    return add (p);
}

int ParamTable::addInt (juce::String id, juce::String name, juce::String group, int lo, int hi, int def, bool mod)
{
    ParamInfo p;
    p.id = id; p.name = name; p.group = group; p.kind = PKind::Int;
    p.range = juce::NormalisableRange<float> ((float) lo, (float) hi, 1.f);
    p.def = (float) def; p.modulatable = mod;
    return add (p);
}

int ParamTable::addChoice (juce::String id, juce::String name, juce::String group, const juce::StringArray& c, int def)
{
    ParamInfo p;
    p.id = id; p.name = name; p.group = group; p.kind = PKind::Choice; p.choices = c;
    p.range = juce::NormalisableRange<float> (0.f, (float) juce::jmax (1, c.size() - 1), 1.f);
    p.def = (float) def; p.modulatable = false;
    return add (p);
}

int ParamTable::addBool (juce::String id, juce::String name, juce::String group, bool def)
{
    ParamInfo p;
    p.id = id; p.name = name; p.group = group; p.kind = PKind::Bool;
    p.range = juce::NormalisableRange<float> (0.f, 1.f, 1.f);
    p.def = def ? 1.f : 0.f; p.modulatable = false;
    return add (p);
}

ParamTable::ParamTable()
{
    using S = juce::String;
    auto& I = ix;
    // ---- global
    I.masterVol = addFloat ("master_vol", "Master Volume", "Global", -60, 6, -6, -12, "dB");
    I.poly      = addInt ("poly", "Polyphony", "Global", 1, kMaxVoices, 16);
    I.glide     = addFloat ("glide", "Glide", "Global", 0, 2, 0, 0.25f, "s");
    I.legato    = addBool ("legato", "Mono Legato", "Global", false);
    I.bend      = addInt ("bend_range", "Bend Range", "Global", 0, 24, 2);
    I.osamp     = addChoice ("oversample", "Oversampling", "Global", choices::oversampling(), 0);
    I.eco       = addBool ("eco", "Eco Mode", "Global", false);
    I.mpe       = addBool ("mpe", "MPE", "Global", false);
    I.velo      = addFloat ("velo_sens", "Velocity Sensitivity", "Global", 0, 1, 0.6f);
    I.voiceSpread = addFloat ("voice_spread", "Voice Pan Spread", "Global", 0, 1, 0.0f);

    // ---- oscillators
    for (int o = 0; o < kNumOscs; ++o)
    {
        const S p = "o" + S (o + 1) + "_";
        const S g = "Osc " + S::charToString ((juce::juce_wchar) ('A' + o));
        auto& x = I.osc[o];
        x.on      = addBool (p + "on", g + " On", g, o == 0);
        x.eng     = addChoice (p + "eng", g + " Engine", g, choices::oscEngines(), 0);
        x.lvl     = addFloat (p + "lvl", g + " Level", g, 0, 1, 0.75f);
        x.pan     = addFloat (p + "pan", g + " Pan", g, -1, 1, 0);
        x.oct     = addInt (p + "oct", g + " Octave", g, -4, 4, 0);
        x.semi    = addInt (p + "semi", g + " Semitone", g, -12, 12, 0);
        x.fine    = addFloat (p + "fine", g + " Fine", g, -100, 100, 0, 0, "ct");
        x.table   = addInt (p + "table", g + " Wavetable", g, 0, 63, o);
        x.pos     = addFloat (p + "pos", g + " Position", g, 0, 1, 0);
        x.warp    = addChoice (p + "warp", g + " Warp Mode", g, choices::warpModes(), 0);
        x.warpAmt = addFloat (p + "warp_amt", g + " Warp", g, 0, 1, 0);
        x.uni     = addInt (p + "uni", g + " Unison", g, 1, kMaxUnison, 1);
        x.det     = addFloat (p + "det", g + " Detune", g, 0, 1, 0.2f);
        x.blend   = addFloat (p + "blend", g + " Blend", g, 0, 1, 0.75f);
        x.width   = addFloat (p + "width", g + " Width", g, 0, 1, 0.8f);
        x.drift   = addFloat (p + "drift", g + " Drift", g, 0, 1, 0.1f);
        x.phase   = addFloat (p + "phase", g + " Phase", g, 0, 1, 0);
        x.rphase  = addFloat (p + "rphase", g + " Random Phase", g, 0, 1, 1);
        x.smp     = addInt (p + "smp", g + " Sample", g, 0, 63, 0);
        x.slice   = addInt (p + "slice", g + " Slice", g, 0, 15, 0);
        x.slmode  = addChoice (p + "smode", g + " Sample Mode", g, choices::sampleModes(), 0);
        x.gdens   = addFloat (p + "gdens", g + " Grain Density", g, 1, 120, 24, 20, "Hz");
        x.gsize   = addFloat (p + "gsize", g + " Grain Size", g, 5, 500, 80, 80, "ms");
        x.gspray  = addFloat (p + "gspray", g + " Grain Spray", g, 0, 1, 0.1f);
        x.gfreeze = addBool (p + "gfreeze", g + " Grain Freeze", g, false);
        x.formant = addFloat (p + "formant", g + " Formant", g, -12, 12, 0, 0, "st");
        x.tilt    = addFloat (p + "tilt", g + " Spectral Tilt", g, -1, 1, 0);
        x.va      = addChoice (p + "va", g + " Analog Shape", g, choices::vaShapes(), 0);
        x.pw      = addFloat (p + "pw", g + " Pulse Width", g, 0.05f, 0.95f, 0.5f);
        x.route   = addChoice (p + "route", g + " Route", g, choices::oscRoutes(), 0);
        x.pmod    = addFloat (p + "pmod", g + " Pitch Mod", g, -48, 48, 0, 0, "st");
    }
    I.subOn    = addBool ("sub_on", "Sub On", "Sub", false);
    I.subLvl   = addFloat ("sub_lvl", "Sub Level", "Sub", 0, 1, 0.6f);
    I.subOct   = addInt ("sub_oct", "Sub Octave", "Sub", -2, 0, -1);
    I.subShape = addChoice ("sub_shape", "Sub Shape", "Sub", choices::subShapes(), 0);
    I.subRoute = addChoice ("sub_route", "Sub Route", "Sub", choices::oscRoutes(), 3);
    I.nzOn     = addBool ("nz_on", "Noise On", "Noise", false);
    I.nzType   = addChoice ("nz_type", "Noise Type", "Noise", choices::noiseTypes(), 0);
    I.nzLvl    = addFloat ("nz_lvl", "Noise Level", "Noise", 0, 1, 0.3f);
    I.nzColor  = addFloat ("nz_color", "Noise Colour", "Noise", -1, 1, 0);
    I.nzRoute  = addChoice ("nz_route", "Noise Route", "Noise", choices::oscRoutes(), 0);

    // ---- filters
    for (int f = 0; f < kNumFilters; ++f)
    {
        const S p = "f" + S (f + 1) + "_";
        const S g = "Filter " + S (f + 1);
        auto& x = I.filt[f];
        x.on    = addBool (p + "on", g + " On", g, f == 0);
        x.type  = addChoice (p + "type", g + " Type", g, choices::filterTypes(), f == 0 ? 0 : 2);
        x.cut   = addFloat (p + "cut", g + " Cutoff", g, 20, 20000, f == 0 ? 8000.f : 12000.f, 1000, "Hz");
        x.res   = addFloat (p + "res", g + " Resonance", g, 0, 1, 0.15f);
        x.pre   = addFloat (p + "pre", g + " Pre Drive", g, 0, 1, 0);
        x.post  = addFloat (p + "post", g + " Post Drive", g, 0, 1, 0);
        x.key   = addFloat (p + "key", g + " Key Track", g, 0, 1, 0.3f);
        x.env   = addFloat (p + "env", g + " Env Amount", g, -1, 1, 0);
        x.morph = addFloat (p + "morph", g + " Morph", g, 0, 1, 0);
        x.mix   = addFloat (p + "mix", g + " Mix", g, 0, 1, 1);
    }
    I.filtRouting = addChoice ("filt_routing", "Filter Routing", "Filter", choices::filterRouting(), 0);

    // ---- envelopes (Env 1 = amp, Env 2 = default filter env)
    for (int e = 0; e < kNumEnvs; ++e)
    {
        const S p = "e" + S (e + 1) + "_";
        const S g = "Env " + S (e + 1);
        auto& x = I.env[e];
        x.a     = addFloat (p + "a", g + " Attack", g, 0, 10, e == 0 ? 0.003f : 0.01f, 0.4f, "s");
        x.h     = addFloat (p + "h", g + " Hold", g, 0, 5, 0, 0.3f, "s");
        x.d     = addFloat (p + "d", g + " Decay", g, 0.001f, 10, 0.4f, 0.6f, "s");
        x.s     = addFloat (p + "s", g + " Sustain", g, 0, 1, e == 0 ? 0.8f : 0.3f);
        x.r     = addFloat (p + "r", g + " Release", g, 0.001f, 15, e == 0 ? 0.25f : 0.4f, 0.8f, "s");
        x.curve = addFloat (p + "curve", g + " Curve", g, -1, 1, 0.3f);
        x.mode  = addChoice (p + "mode", g + " Mode", g, choices::envModes(), 0);
    }
    // ---- LFOs
    for (int l = 0; l < kNumLfos; ++l)
    {
        const S p = "l" + S (l + 1) + "_";
        const S g = "LFO " + S (l + 1);
        auto& x = I.lfo[l];
        x.rate   = addFloat (p + "rate", g + " Rate", g, 0.01f, 40, 1, 2, "Hz");
        x.sync   = addBool (p + "sync", g + " Sync", g, false);
        x.div    = addChoice (p + "div", g + " Division", g, divisionNames(), 7);
        x.shape  = addChoice (p + "shape", g + " Shape", g, choices::lfoShapes(), 0);
        x.mode   = addChoice (p + "mode", g + " Mode", g, choices::lfoModes(), 0);
        x.phase  = addFloat (p + "phase", g + " Phase", g, 0, 1, 0);
        x.smooth = addFloat (p + "smooth", g + " Smooth", g, 0, 1, 0);
        x.delay  = addFloat (p + "delay", g + " Fade In", g, 0, 5, 0, 0.5f, "s");
        x.poly   = addBool (p + "poly", g + " Per Voice", g, true);
    }
    // ---- step sequencers
    for (int s = 0; s < kNumSeqs; ++s)
    {
        const S p = "s" + S (s + 1) + "_";
        const S g = "Seq " + S (s + 1);
        auto& x = I.seq[s];
        x.div   = addChoice (p + "div", g + " Division", g, divisionNames(), 13);
        x.len   = addInt (p + "len", g + " Length", g, 1, kSeqSteps, 16);
        x.glide = addFloat (p + "glide", g + " Glide", g, 0, 1, 0);
        for (int k = 0; k < kSeqSteps; ++k)
            x.step[k] = addFloat (p + "st" + S (k + 1), g + " Step " + S (k + 1), g, -1, 1, 0, 0, {}, false);
    }
    // ---- macros & matrix amounts
    for (int m = 0; m < kNumMacros; ++m)
        I.macro[m] = addFloat ("macro" + S (m + 1), "Macro " + S (m + 1), "Macros", 0, 1, 0, 0, {}, false);
    for (int m = 0; m < kNumModSlots; ++m)
        I.modAmt[m] = addFloat ("mm" + S (m + 1) + "_amt", "Mod " + S (m + 1) + " Amount", "Matrix", -1, 1, 0, 0, {}, false);

    // ---- FX
    for (int t = 0; t < kNumFx; ++t)
    {
        const auto& d = fxDef (t);
        const S p = S ("fx_") + d.key + "_";
        const S g = S ("FX ") + d.name;
        auto& x = I.fx[t];
        x.on  = addBool (p + "on", g + " On", g, false);
        x.bus = addChoice (p + "bus", g + " Bus", g, choices::fxBus(), 0);
        for (int k = 0; k < kMaxFxParams; ++k) x.p[k] = -1;
        for (int k = 0; k < d.numParams; ++k)
        {
            const auto& q = d.p[k];
            if (q.numChoices > 0)
            {
                juce::StringArray c;
                if (juce::String (q.choiceList) == "DIV") c = divisionNames();
                else c.addTokens (q.choiceList, "|", {});
                x.p[k] = addChoice (p + q.key, g + " " + q.name, g, c, (int) q.def);
            }
            else
                x.p[k] = addFloat (p + q.key, g + " " + q.name, g, q.lo, q.hi, q.def, q.skewCentre);
        }
    }
    I.busALvl = addFloat ("bus_a_lvl", "FX Bus A Level", "FX", 0, 1, 1);
    I.busBLvl = addFloat ("bus_b_lvl", "FX Bus B Level", "FX", 0, 1, 1);

    // ---- perform
    const S g = "Perform";
    I.xyX         = addFloat ("xy_x", "Scene X", g, 0, 1, 0.5f, 0, {}, false);
    I.xyY         = addFloat ("xy_y", "Scene Y", g, 0, 1, 0.5f, 0, {}, false);
    I.morphOn     = addBool ("morph_on", "Scene Morph", g, false);
    I.riserArm    = addBool ("riser_arm", "Riser Arm", g, false);
    I.riserBars   = addChoice ("riser_bars", "Riser Length", g, choices::riserBars(), 1);
    I.riserFilter = addFloat ("riser_filter", "Riser Filter Sweep", g, 0, 1, 0.8f, 0, {}, false);
    I.riserNoise  = addFloat ("riser_noise", "Riser Noise Swell", g, 0, 1, 0.5f, 0, {}, false);
    I.riserVerb   = addFloat ("riser_verb", "Riser Reverb Throw", g, 0, 1, 0.6f, 0, {}, false);
    I.riserPitch  = addFloat ("riser_pitch", "Riser Pitch Rise", g, 0, 1, 0.3f, 0, {}, false);
    I.dropKill    = addBool ("drop_kill", "Drop Kills Tails", g, true);
    I.stutter     = addBool ("stutter", "Stutter", g, false);
    I.stutterDiv  = addChoice ("stutter_div", "Stutter Division", g, divisionNames(), 13);
    I.tapeStop    = addBool ("tapestop", "Tape Stop", g, false);
    I.tapeStopTime= addFloat ("tapestop_time", "Tape Stop Time", g, 0.1f, 3, 0.8f, 0, "s", false);
    I.arpOn       = addBool ("arp_on", "Arp On", g, false);
    I.arpMode     = addChoice ("arp_mode", "Arp Mode", g, choices::arpModes(), 0);
    I.arpDiv      = addChoice ("arp_div", "Arp Rate", g, divisionNames(), 13);
    I.arpOct      = addInt ("arp_oct", "Arp Octaves", g, 1, 4, 1);
    I.arpGate     = addFloat ("arp_gate", "Arp Gate", g, 0.05f, 1, 0.6f, 0, {}, false);
    I.arpRatchet  = addInt ("arp_ratchet", "Arp Ratchet", g, 1, 4, 1);
    I.arpProb     = addFloat ("arp_prob", "Arp Probability", g, 0, 1, 1, 0, {}, false);
    I.arpSwing    = addFloat ("arp_swing", "Swing", g, 50, 75, 50, 0, "%", false);
    I.arpGroove   = addChoice ("arp_groove", "Groove", g, choices::grooves(), 0);
    I.arpLatch    = addBool ("arp_latch", "Arp Latch", g, false);
    I.chordOn     = addBool ("chord_on", "Chord On", g, false);
    I.chordType   = addChoice ("chord_type", "Chord", g, choices::chordTypes(), 1);
    I.strum       = addFloat ("strum", "Strum", g, 0, 200, 0, 0, "ms", false);
    I.chopLvl     = addFloat ("chop_lvl", "Chop Pad Level", g, 0, 1, 0.8f, 0, {}, false);
    I.chopSample  = addInt ("chop_smp", "Chop Pad Sample", g, 0, 63, 0);
    I.chopChannel = addInt ("chop_ch", "Chop Pad MIDI Channel", g, 0, 16, 16);
}

juce::AudioProcessorValueTreeState::ParameterLayout ParamTable::createLayout() const
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (const auto& p : params)
    {
        const juce::ParameterID pid { p.id, 1 };
        switch (p.kind)
        {
            case PKind::Float:
            {
                auto unit = p.unit;
                auto attrs = juce::AudioParameterFloatAttributes().withLabel (unit)
                    .withStringFromValueFunction ([unit] (float v, int) {
                        if (unit == "Hz") return v >= 1000 ? juce::String (v / 1000.f, 2) + "k" : juce::String (v, v < 10 ? 2 : 0);
                        if (unit == "s")  return v < 1 ? juce::String (v * 1000.f, 0) + "ms" : juce::String (v, 2) + "s";
                        return juce::String (v, std::abs (v) < 10 ? 2 : 1); });
                layout.add (std::make_unique<juce::AudioParameterFloat> (pid, p.name, p.range, p.def, attrs));
                break;
            }
            case PKind::Int:
                layout.add (std::make_unique<juce::AudioParameterInt> (pid, p.name, (int) p.range.start, (int) p.range.end, (int) p.def));
                break;
            case PKind::Choice:
                layout.add (std::make_unique<juce::AudioParameterChoice> (pid, p.name, p.choices, (int) p.def));
                break;
            case PKind::Bool:
                layout.add (std::make_unique<juce::AudioParameterBool> (pid, p.name, p.def > 0.5f));
                break;
        }
    }
    return layout;
}
} // namespace pd
