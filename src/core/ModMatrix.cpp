#include "ModMatrix.h"

namespace pd
{
const juce::StringArray& modSourceNames()
{
    static const juce::StringArray n {
        "None",
        "LFO 1", "LFO 2", "LFO 3", "LFO 4", "LFO 5", "LFO 6", "LFO 7", "LFO 8",
        "Env 1", "Env 2", "Env 3", "Env 4",
        "Seq 1", "Seq 2",
        "Velocity", "Note", "Mod Wheel", "Pitch Bend", "Aftertouch",
        "MPE Pressure", "MPE Timbre", "Random", "Expression",
        "Macro 1", "Macro 2", "Macro 3", "Macro 4", "Macro 5", "Macro 6", "Macro 7", "Macro 8",
        "Patch Out 1", "Patch Out 2", "Patch Out 3", "Patch Out 4",
        "Riser", "XY X", "XY Y" };
    jassert (n.size() == kNumModSources);
    return n;
}

const juce::StringArray& modCurveNames()
{
    static const juce::StringArray n { "Linear", "Exponential", "Logarithmic", "S-Curve", "Stepped", "Inverted" };
    return n;
}

bool isBipolarSource (int s) noexcept
{
    return (s >= SrcLfo1 && s <= SrcLfo8) || s == SrcSeq1 || s == SrcSeq2 || s == SrcPitchBend
        || s == SrcRandom || (s >= SrcPatch1 && s <= SrcPatch4) || s == SrcNote;
}

float applyCurve (int curve, float x) noexcept
{
    const float s = x < 0 ? -1.f : 1.f;
    const float a = std::abs (x);
    switch (curve)
    {
        case CurveExp:      return s * a * a * a;
        case CurveLog:      return s * (1.f - (1.f - a) * (1.f - a) * (1.f - a));
        case CurveSCurve:   return s * (a * a * (3.f - 2.f * a));
        case CurveStepped:  return s * std::floor (a * 4.f + 0.0001f) * 0.25f;
        case CurveInverted: return -x;
        default:            return x;
    }
}

// ---------------------------------------------------------------- serialisation
juce::ValueTree engineStateToTree (const EngineState& s)
{
    const auto& table = ParamTable::get();
    juce::ValueTree t ("ENGINE");
    juce::ValueTree mm ("MATRIX");
    for (int i = 0; i < kNumModSlots; ++i)
    {
        const auto& sl = s.slots[i];
        if (sl.src == SrcNone || sl.dest < 0) continue;
        juce::ValueTree slot ("SLOT");
        slot.setProperty ("i", i, nullptr);
        slot.setProperty ("src", modSourceNames()[sl.src], nullptr);
        slot.setProperty ("aux", modSourceNames()[sl.aux], nullptr);
        slot.setProperty ("dest", table[sl.dest].id, nullptr);
        slot.setProperty ("curve", (int) sl.curve, nullptr);
        mm.appendChild (slot, nullptr);
    }
    t.appendChild (mm, nullptr);

    auto floatsToString = [] (const float* f, int n) {
        juce::StringArray a; for (int i = 0; i < n; ++i) a.add (juce::String (f[i], 4)); return a.joinIntoString (","); };

    juce::ValueTree lfo ("LFODRAW");
    for (int l = 0; l < kNumLfos; ++l) lfo.setProperty ("l" + juce::String (l), floatsToString (s.lfoDrawn[l], kLfoDrawPoints), nullptr);
    t.appendChild (lfo, nullptr);

    juce::ValueTree ms ("MSEGS");
    for (int e = 0; e < kNumEnvs; ++e)
    {
        juce::ValueTree m ("MSEG");
        m.setProperty ("n", s.mseg[e].numPoints, nullptr);
        m.setProperty ("sus", s.mseg[e].sustainPoint, nullptr);
        m.setProperty ("t", floatsToString (s.mseg[e].t, s.mseg[e].numPoints), nullptr);
        m.setProperty ("y", floatsToString (s.mseg[e].y, s.mseg[e].numPoints), nullptr);
        m.setProperty ("c", floatsToString (s.mseg[e].c, s.mseg[e].numPoints), nullptr);
        ms.appendChild (m, nullptr);
    }
    t.appendChild (ms, nullptr);

    juce::StringArray chord, order, cc, learn;
    for (int i = 0; i < s.numChordNotes; ++i) chord.add (juce::String ((int) s.chordMemory[i]));
    for (int i = 0; i < kNumFx; ++i) order.add (juce::String ((int) s.fxOrder[i]));
    for (int i = 0; i < kNumMacros; ++i) cc.add (juce::String ((int) s.macroCC[i]));
    for (int i = 0; i < 128; ++i) if (s.midiLearn[i] >= 0) learn.add (juce::String (i) + "=" + table[s.midiLearn[i]].id);
    t.setProperty ("chord", chord.joinIntoString (","), nullptr);
    t.setProperty ("fxorder", order.joinIntoString (","), nullptr);
    t.setProperty ("macrocc", cc.joinIntoString (","), nullptr);
    t.setProperty ("macroccout", s.macroCCOut, nullptr);
    t.setProperty ("midilearn", learn.joinIntoString (";"), nullptr);
    t.setProperty ("choppitch", floatsToString (s.chopPitch, kNumChopSlots), nullptr);
    t.setProperty ("chopformant", floatsToString (s.chopFormant, kNumChopSlots), nullptr);
    return t;
}

static int parseFloats (const juce::String& str, float* out, int maxN)
{
    juce::StringArray a; a.addTokens (str, ",", {});
    const int n = juce::jmin (maxN, a.size());
    for (int i = 0; i < n; ++i) out[i] = a[i].getFloatValue();
    return n;
}

void engineStateFromTree (EngineState& s, const juce::ValueTree& t)
{
    s = EngineState();
    if (! t.isValid()) return;
    const auto& table = ParamTable::get();
    auto mm = t.getChildWithName ("MATRIX");
    for (auto slot : mm)
    {
        const int i = slot.getProperty ("i", -1);
        if (i < 0 || i >= kNumModSlots) continue;
        auto& sl = s.slots[i];
        sl.src = (int16_t) juce::jmax (0, modSourceNames().indexOf (slot.getProperty ("src").toString()));
        sl.aux = (int16_t) juce::jmax (0, modSourceNames().indexOf (slot.getProperty ("aux").toString()));
        sl.dest = (int16_t) table.indexOf (slot.getProperty ("dest").toString());
        sl.curve = (int16_t) juce::jlimit (0, kNumCurves - 1, (int) slot.getProperty ("curve", 0));
        if (sl.dest < 0) sl.src = SrcNone;
    }
    auto lfo = t.getChildWithName ("LFODRAW");
    for (int l = 0; l < kNumLfos; ++l)
        if (lfo.hasProperty ("l" + juce::String (l)))
            parseFloats (lfo.getProperty ("l" + juce::String (l)).toString(), s.lfoDrawn[l], kLfoDrawPoints);
    auto ms = t.getChildWithName ("MSEGS");
    for (int e = 0; e < juce::jmin (kNumEnvs, ms.getNumChildren()); ++e)
    {
        auto m = ms.getChild (e);
        auto& g = s.mseg[e];
        g.numPoints = juce::jlimit (2, kMsegPoints, (int) m.getProperty ("n", 4));
        g.sustainPoint = juce::jlimit (-1, g.numPoints - 1, (int) m.getProperty ("sus", 2));
        parseFloats (m.getProperty ("t").toString(), g.t, g.numPoints);
        parseFloats (m.getProperty ("y").toString(), g.y, g.numPoints);
        parseFloats (m.getProperty ("c").toString(), g.c, g.numPoints);
    }
    {
        juce::StringArray a; a.addTokens (t.getProperty ("chord").toString(), ",", {});
        if (a.size() > 0)
        {
            s.numChordNotes = juce::jmin (8, a.size());
            for (int i = 0; i < s.numChordNotes; ++i) s.chordMemory[i] = (int8_t) juce::jlimit (-24, 36, a[i].getIntValue());
        }
    }
    {
        juce::StringArray a; a.addTokens (t.getProperty ("fxorder").toString(), ",", {});
        if (a.size() == kNumFx)
        {
            bool seen[kNumFx] {}; bool ok = true;
            for (int i = 0; i < kNumFx; ++i) { int v = a[i].getIntValue(); if (v < 0 || v >= kNumFx || seen[v]) ok = false; else seen[v] = true; }
            if (ok) for (int i = 0; i < kNumFx; ++i) s.fxOrder[i] = (int8_t) a[i].getIntValue();
        }
    }
    {
        juce::StringArray a; a.addTokens (t.getProperty ("macrocc").toString(), ",", {});
        for (int i = 0; i < juce::jmin (kNumMacros, a.size()); ++i) s.macroCC[i] = (int8_t) juce::jlimit (0, 127, a[i].getIntValue());
    }
    s.macroCCOut = (bool) t.getProperty ("macroccout", false);
    {
        juce::StringArray a; a.addTokens (t.getProperty ("midilearn").toString(), ";", {});
        for (auto& e : a)
        {
            const int cc = e.upToFirstOccurrenceOf ("=", false, false).getIntValue();
            const int pi = table.indexOf (e.fromFirstOccurrenceOf ("=", false, false));
            if (cc >= 0 && cc < 128 && pi >= 0) s.midiLearn[cc] = (int16_t) pi;
        }
    }
    parseFloats (t.getProperty ("choppitch").toString(), s.chopPitch, kNumChopSlots);
    parseFloats (t.getProperty ("chopformant").toString(), s.chopFormant, kNumChopSlots);
}
} // namespace pd
