#include "PresetFormat.h"

namespace pd
{
namespace
{
juce::var stringsToVar (const juce::StringArray& a)
{
    juce::Array<juce::var> arr; for (auto& s : a) arr.add (s); return arr;
}
juce::StringArray varToStrings (const juce::var& v)
{
    juce::StringArray a;
    if (auto* arr = v.getArray()) for (auto& x : *arr) a.add (x.toString());
    return a;
}
juce::var paramsToVar (const std::map<juce::String, float>& m)
{
    auto* o = new juce::DynamicObject();
    for (auto& [k, v] : m) o->setProperty (k, (double) v);
    return juce::var (o);
}
void varToParams (const juce::var& v, std::map<juce::String, float>& m)
{
    m.clear();
    if (auto* o = v.getDynamicObject())
        for (auto& p : o->getProperties()) m[p.name.toString()] = (float) (double) p.value;
}

// Parameter renames across format versions (old id -> new id).
const std::map<juce::String, juce::String>& renames()
{
    static const std::map<juce::String, juce::String> r {
        { "volume", "master_vol" }, { "osc1_level", "o1_lvl" }, { "osc2_level", "o2_lvl" }, { "osc3_level", "o3_lvl" },
        { "filter1_cutoff", "f1_cut" }, { "filter2_cutoff", "f2_cut" }, { "filter1_res", "f1_res" }, { "filter2_res", "f2_res" } };
    return r;
}
} // namespace

juce::String PresetFormat::fileExtension() { return ".pdvo"; }

juce::var PresetFormat::treeToJson (const juce::ValueTree& t)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("type", t.getType().toString());
    auto* props = new juce::DynamicObject();
    for (int i = 0; i < t.getNumProperties(); ++i)
    {
        const auto name = t.getPropertyName (i);
        props->setProperty (name, t.getProperty (name));
    }
    o->setProperty ("props", juce::var (props));
    juce::Array<juce::var> kids;
    for (auto c : t) kids.add (treeToJson (c));
    o->setProperty ("children", kids);
    return juce::var (o);
}

juce::ValueTree PresetFormat::treeFromJson (const juce::var& v)
{
    if (! v.isObject()) return {};
    juce::ValueTree t (juce::Identifier (v.getProperty ("type", "NODE").toString()));
    if (auto* p = v.getProperty ("props", {}).getDynamicObject())
        for (auto& prop : p->getProperties()) t.setProperty (prop.name, prop.value, nullptr);
    if (auto* arr = v.getProperty ("children", {}).getArray())
        for (auto& c : *arr) { auto child = treeFromJson (c); if (child.isValid()) t.appendChild (child, nullptr); }
    return t;
}

juce::var PresetFormat::metaToJson (const PresetMeta& m)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("name", m.name);
    o->setProperty ("author", m.author);
    o->setProperty ("bank", m.bank);
    o->setProperty ("bankIndex", m.bankIndex);
    o->setProperty ("track", m.track);
    o->setProperty ("release", m.release);
    o->setProperty ("category", m.category);
    o->setProperty ("type", m.type);
    o->setProperty ("description", m.description);
    o->setProperty ("genres", stringsToVar (m.genres));
    o->setProperty ("moods", stringsToVar (m.moods));
    o->setProperty ("tags", stringsToVar (m.tags));
    o->setProperty ("macrosReady", m.macrosReady);
    o->setProperty ("mpe", m.mpe);
    o->setProperty ("cpuLight", m.cpuLight);
    juce::Array<juce::var> f; for (float x : m.features) f.add ((double) x);
    o->setProperty ("features", f);
    auto* qa = new juce::DynamicObject();
    qa->setProperty ("peakDb", (double) m.peakDb);
    qa->setProperty ("rmsDb", (double) m.rmsDb);
    o->setProperty ("qa", juce::var (qa));
    return juce::var (o);
}

void PresetFormat::metaFromJson (const juce::var& v, PresetMeta& m)
{
    m.name = v.getProperty ("name", "Untitled").toString();
    m.author = v.getProperty ("author", "Raygun").toString();
    m.bank = v.getProperty ("bank", "User").toString();
    m.bankIndex = (int) v.getProperty ("bankIndex", 0);
    m.track = v.getProperty ("track", "").toString();
    m.release = v.getProperty ("release", "").toString();
    m.category = v.getProperty ("category", "").toString();
    m.type = v.getProperty ("type", "").toString();
    m.description = v.getProperty ("description", "").toString();
    m.genres = varToStrings (v.getProperty ("genres", {}));
    m.moods = varToStrings (v.getProperty ("moods", {}));
    m.tags = varToStrings (v.getProperty ("tags", {}));
    m.macrosReady = (bool) v.getProperty ("macrosReady", false);
    m.mpe = (bool) v.getProperty ("mpe", false);
    m.cpuLight = (bool) v.getProperty ("cpuLight", false);
    if (auto* f = v.getProperty ("features", {}).getArray())
        for (int i = 0; i < juce::jmin (kNumFeatures, f->size()); ++i) m.features[(size_t) i] = (float) (double) (*f)[i];
    const auto qa = v.getProperty ("qa", {});
    m.peakDb = (float) (double) qa.getProperty ("peakDb", -100.0);
    m.rmsDb = (float) (double) qa.getProperty ("rmsDb", -100.0);
}

juce::var PresetFormat::toJson (const PresetData& p)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("format", "PureDevotion.Preset");
    o->setProperty ("version", kPresetFormatVersion);
    o->setProperty ("meta", metaToJson (p.meta));
    o->setProperty ("params", paramsToVar (p.params));
    o->setProperty ("engine", treeToJson (p.engine));
    o->setProperty ("patch", treeToJson (p.patch));
    if (p.hasScenes)
    {
        juce::Array<juce::var> sc; for (auto& s : p.scenes) sc.add (paramsToVar (s));
        o->setProperty ("scenes", sc);
    }
    o->setProperty ("macroNames", stringsToVar (p.macroNames));
    return juce::var (o);
}

bool PresetFormat::fromJson (const juce::var& v, PresetData& out, juce::String& error)
{
    if (! v.isObject()) { error = "Not a JSON object"; return false; }
    const auto fmt = v.getProperty ("format", "").toString();
    if (fmt.isNotEmpty() && fmt != "PureDevotion.Preset") { error = "Unknown preset format: " + fmt; return false; }
    const int version = (int) v.getProperty ("version", 0);
    if (version > kPresetFormatVersion) { error = "Preset was saved by a newer version (v" + juce::String (version) + ")"; return false; }

    out = PresetData();
    // v0 stored metadata at the top level and params under "parameters"
    if (version == 0)
    {
        metaFromJson (v, out.meta);
        varToParams (v.getProperty ("parameters", v.getProperty ("params", {})), out.params);
    }
    else
    {
        metaFromJson (v.getProperty ("meta", {}), out.meta);
        varToParams (v.getProperty ("params", {}), out.params);
    }
    // v0/v1 -> v2: parameter renames; v1 had a single "tags" list for genre/mood
    if (version < 2)
    {
        std::map<juce::String, float> renamed;
        for (auto& [k, val] : out.params)
        {
            auto it = renames().find (k);
            renamed[it != renames().end() ? it->second : k] = val;
        }
        out.params = std::move (renamed);
        static const juce::StringArray genres { "UK Garage", "Breakbeat", "Techno", "Bass", "Ambient" };
        static const juce::StringArray moods { "Euphoric", "Melancholy", "Dark", "Driving" };
        for (auto& t : out.meta.tags)
        {
            if (genres.contains (t)) out.meta.genres.addIfNotAlreadyThere (t);
            if (moods.contains (t)) out.meta.moods.addIfNotAlreadyThere (t);
        }
    }
    out.engine = treeFromJson (v.getProperty ("engine", {}));
    if (! out.engine.isValid()) out.engine = juce::ValueTree ("ENGINE");
    out.patch = treeFromJson (v.getProperty ("patch", {}));
    if (! out.patch.isValid()) out.patch = juce::ValueTree ("PATCH");
    if (auto* sc = v.getProperty ("scenes", {}).getArray())
    {
        out.hasScenes = true;
        for (int i = 0; i < juce::jmin (kNumScenes, sc->size()); ++i) varToParams ((*sc)[i], out.scenes[(size_t) i]);
    }
    out.macroNames = varToStrings (v.getProperty ("macroNames", {}));
    return true;
}

bool PresetFormat::save (const juce::File& f, const PresetData& p)
{
    f.getParentDirectory().createDirectory();
    return f.replaceWithText (juce::JSON::toString (toJson (p), true));
}

bool PresetFormat::load (const juce::File& f, PresetData& out, juce::String& error)
{
    const auto text = f.loadFileAsString();
    if (text.isEmpty()) { error = "Empty file"; return false; }
    juce::var v;
    const auto r = juce::JSON::parse (text, v);
    if (r.failed()) { error = r.getErrorMessage(); return false; }
    if (! fromJson (v, out, error)) return false;
    out.meta.file = f;
    return true;
}
} // namespace pd
