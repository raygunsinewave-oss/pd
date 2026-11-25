#include "PresetLibrary.h"
#include <set>

namespace pd
{
PresetLibrary::PresetLibrary() : juce::Thread ("PD Preset Scan") { loadDb(); }
PresetLibrary::~PresetLibrary() { stopThread (5000); }

juce::File PresetLibrary::dataDir()
{
   #if JUCE_LINUX
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (".local/share/Raygun/Pure Devotion");
   #else
    return juce::File::getSpecialLocation (juce::File::commonApplicationDataDirectory).getChildFile ("Raygun/Pure Devotion");
   #endif
}

juce::File PresetLibrary::factoryDir()
{
    const auto env = juce::SystemStats::getEnvironmentVariable ("PD_FACTORY_DIR", {});
    if (env.isNotEmpty()) return juce::File (env);
   #if JUCE_MAC
    return juce::File ("/Library/Application Support/Raygun/Pure Devotion/Factory Presets");
   #else
    return dataDir().getChildFile ("Factory Presets");
   #endif
}

juce::File PresetLibrary::userDir()
{
    const auto env = juce::SystemStats::getEnvironmentVariable ("PD_USER_DIR", {});
    if (env.isNotEmpty()) return juce::File (env);
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Raygun/Pure Devotion/User Presets");
}

static juce::File dbFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Raygun/Pure Devotion/browser_db.json");
}
static juce::File indexFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Raygun/Pure Devotion/preset_index.json");
}

juce::String PresetLibrary::key (const PresetMeta& m)
{
    return (m.factory ? "F:" : "U:") + m.bank + "/" + m.name;
}

void PresetLibrary::loadDb()
{
    juce::var v;
    if (juce::JSON::parse (dbFile().loadFileAsString(), v).failed()) return;
    if (auto* f = v.getProperty ("favourites", {}).getArray()) for (auto& x : *f) favourites.insert (x.toString());
    if (auto* r = v.getProperty ("ratings", {}).getDynamicObject())
        for (auto& p : r->getProperties()) ratings[p.name.toString()] = (int) p.value;
}

void PresetLibrary::saveDb() const
{
    auto* o = new juce::DynamicObject();
    juce::Array<juce::var> f; for (auto& x : favourites) f.add (x);
    o->setProperty ("favourites", f);
    auto* r = new juce::DynamicObject(); for (auto& [k, v] : ratings) r->setProperty (k, v);
    o->setProperty ("ratings", juce::var (r));
    dbFile().getParentDirectory().createDirectory();
    dbFile().replaceWithText (juce::JSON::toString (juce::var (o)));
}

void PresetLibrary::scanInto (std::vector<PresetMeta>& out)
{
    out.clear();
    struct Root { juce::File dir; bool factory; };
    const Root roots[2] = { { factoryDir(), true }, { userDir(), false } };

    // fingerprint for the index cache
    juce::Array<juce::File> files[2];
    juce::int64 fp = 0;
    for (int r = 0; r < 2; ++r)
    {
        if (roots[r].dir.isDirectory())
            files[r] = roots[r].dir.findChildFiles (juce::File::findFiles, true, "*" + PresetFormat::fileExtension());
        for (auto& f : files[r]) fp += f.getSize() * 31 + f.getLastModificationTime().toMilliseconds() % 100000;
        fp += files[r].size() * 1000003LL;
    }
    juce::var cache;
    if (juce::JSON::parse (indexFile().loadFileAsString(), cache).wasOk() && (juce::int64) cache.getProperty ("fp", 0) == fp)
        if (auto* arr = cache.getProperty ("presets", {}).getArray())
        {
            for (auto& e : *arr)
            {
                PresetMeta m; PresetFormat::metaFromJson (e, m);
                m.file = juce::File (e.getProperty ("file", "").toString());
                m.factory = (bool) e.getProperty ("factory", false);
                out.push_back (m);
            }
            return;
        }

    for (int r = 0; r < 2; ++r)
        for (auto& f : files[r])
        {
            if (threadShouldExit()) return;
            juce::var v;
            if (juce::JSON::parse (f.loadFileAsString(), v).failed()) continue;
            PresetData d; juce::String err;
            if (! PresetFormat::fromJson (v, d, err)) continue;
            d.meta.file = f; d.meta.factory = roots[r].factory;
            if (! roots[r].factory && d.meta.bank.isEmpty()) d.meta.bank = "User";
            out.push_back (d.meta);
        }
    std::sort (out.begin(), out.end(), [] (const PresetMeta& a, const PresetMeta& b) {
        if (a.factory != b.factory) return a.factory;
        if (a.bankIndex != b.bankIndex) return a.bankIndex < b.bankIndex;
        return a.name.compareNatural (b.name) < 0; });

    auto* o = new juce::DynamicObject();
    o->setProperty ("fp", fp);
    juce::Array<juce::var> arr;
    for (auto& m : out)
    {
        auto e = PresetFormat::metaToJson (m);
        e.getDynamicObject()->setProperty ("file", m.file.getFullPathName());
        e.getDynamicObject()->setProperty ("factory", m.factory);
        arr.add (e);
    }
    o->setProperty ("presets", arr);
    indexFile().getParentDirectory().createDirectory();
    indexFile().replaceWithText (juce::JSON::toString (juce::var (o), true));
}

void PresetLibrary::rescanBlocking()
{
    std::vector<PresetMeta> fresh;
    scanInto (fresh);
    // z-score features for similarity
    std::vector<std::array<float, kNumFeatures>> nf (fresh.size());
    for (int k = 0; k < kNumFeatures; ++k)
    {
        double mean = 0, var = 0;
        for (auto& m : fresh) mean += m.features[(size_t) k];
        mean /= juce::jmax ((size_t) 1, fresh.size());
        for (auto& m : fresh) var += juce::square (m.features[(size_t) k] - mean);
        const double sd = std::sqrt (var / juce::jmax ((size_t) 1, fresh.size())) + 1e-6;
        for (size_t i = 0; i < fresh.size(); ++i) nf[i][(size_t) k] = (float) ((fresh[i].features[(size_t) k] - mean) / sd);
    }
    const juce::ScopedLock sl (lock);
    presets = std::move (fresh);
    normFeatures = std::move (nf);
}

void PresetLibrary::rescanAsync (std::function<void()> onDone)
{
    if (isThreadRunning()) return;
    pendingDone = std::move (onDone);
    startThread();
}

void PresetLibrary::run()
{
    rescanBlocking();
    auto done = pendingDone;
    if (done) juce::MessageManager::callAsync (done);
}

int PresetLibrary::size() const { const juce::ScopedLock sl (lock); return (int) presets.size(); }
PresetMeta PresetLibrary::get (int i) const
{
    const juce::ScopedLock sl (lock);
    return i >= 0 && i < (int) presets.size() ? presets[(size_t) i] : PresetMeta();
}
int PresetLibrary::indexOfFile (const juce::File& f) const
{
    const juce::ScopedLock sl (lock);
    for (size_t i = 0; i < presets.size(); ++i) if (presets[i].file == f) return (int) i;
    return -1;
}

juce::String PresetLibrary::folderOf (const PresetMeta& m) const
{
    if (m.factory) return {};
    const auto rel = m.file.getParentDirectory().getRelativePathFrom (userDir());
    return rel == "." ? juce::String() : rel;
}

std::vector<int> PresetLibrary::query (const Filter& f) const
{
    const juce::ScopedLock sl (lock);
    std::vector<int> r;
    juce::StringArray words; words.addTokens (f.text.toLowerCase(), " ", "\"");
    words.removeEmptyStrings();
    for (size_t i = 0; i < presets.size(); ++i)
    {
        const auto& m = presets[i];
        if (f.bank.isNotEmpty() && m.bank != f.bank) continue;
        if (f.track.isNotEmpty() && m.track != f.track) continue;
        if (f.category.isNotEmpty() && m.category != f.category) continue;
        if (f.type.isNotEmpty() && m.type != f.type) continue;
        if (f.genre.isNotEmpty() && ! m.genres.contains (f.genre)) continue;
        if (f.mood.isNotEmpty() && ! m.moods.contains (f.mood)) continue;
        if (f.mpeOnly && ! m.mpe) continue;
        if (f.cpuLightOnly && ! m.cpuLight) continue;
        if (f.macrosOnly && ! m.macrosReady) continue;
        if (f.favouritesOnly && favourites.count (key (m)) == 0) continue;
        if (f.minRating > 0) { auto it = ratings.find (key (m)); if (it == ratings.end() || it->second < f.minRating) continue; }
        if (f.folder.isNotEmpty() && folderOf (m) != f.folder) continue;
        bool ok = true;
        if (! words.isEmpty())
        {
            const auto hay = (m.name + " " + m.track + " " + m.category + " " + m.type + " " + m.tags.joinIntoString (" ")
                              + " " + m.genres.joinIntoString (" ") + " " + m.moods.joinIntoString (" ")).toLowerCase();
            for (auto& w : words) if (! hay.contains (w)) { ok = false; break; }
        }
        if (ok) r.push_back ((int) i);
    }
    return r;
}

std::vector<int> PresetLibrary::similar (int index, int k) const
{
    const juce::ScopedLock sl (lock);
    std::vector<std::pair<float, int>> d;
    if (index < 0 || index >= (int) presets.size()) return {};
    const auto& a = normFeatures[(size_t) index];
    for (size_t i = 0; i < presets.size(); ++i)
    {
        if ((int) i == index) continue;
        float s = 0;
        for (int q = 0; q < kNumFeatures; ++q) s += juce::square (a[(size_t) q] - normFeatures[i][(size_t) q]);
        if (presets[i].type == presets[(size_t) index].type) s *= 0.6f;
        d.push_back ({ s, (int) i });
    }
    const int n = juce::jmin (k, (int) d.size());
    std::partial_sort (d.begin(), d.begin() + n, d.end());
    std::vector<int> r; for (int i = 0; i < n; ++i) r.push_back (d[(size_t) i].second);
    return r;
}

bool PresetLibrary::isFavourite (const PresetMeta& m) const { const juce::ScopedLock sl (lock); return favourites.count (key (m)) > 0; }
void PresetLibrary::setFavourite (const PresetMeta& m, bool fav)
{
    { const juce::ScopedLock sl (lock); if (fav) favourites.insert (key (m)); else favourites.erase (key (m)); }
    saveDb();
}
int PresetLibrary::rating (const PresetMeta& m) const
{
    const juce::ScopedLock sl (lock); auto it = ratings.find (key (m)); return it == ratings.end() ? 0 : it->second;
}
void PresetLibrary::setRating (const PresetMeta& m, int stars)
{
    { const juce::ScopedLock sl (lock); if (stars <= 0) ratings.erase (key (m)); else ratings[key (m)] = juce::jlimit (1, 5, stars); }
    saveDb();
}

juce::StringArray PresetLibrary::banks() const
{
    const juce::ScopedLock sl (lock); juce::StringArray a;
    for (auto& m : presets) a.addIfNotAlreadyThere (m.bank);
    return a;
}
juce::StringArray PresetLibrary::tracks (const juce::String& bank) const
{
    const juce::ScopedLock sl (lock); juce::StringArray a;
    for (auto& m : presets) if ((bank.isEmpty() || m.bank == bank) && m.track.isNotEmpty()) a.addIfNotAlreadyThere (m.track);
    return a;
}
juce::StringArray PresetLibrary::categories() const
{
    const juce::ScopedLock sl (lock); juce::StringArray a;
    for (auto& m : presets) if (m.category.isNotEmpty()) a.addIfNotAlreadyThere (m.category);
    return a;
}
juce::StringArray PresetLibrary::userFolders() const
{
    juce::StringArray a;
    for (auto& d : userDir().findChildFiles (juce::File::findDirectories, true))
        a.add (d.getRelativePathFrom (userDir()));
    a.sort (true);
    return a;
}
juce::File PresetLibrary::createUserFolder (const juce::String& name)
{
    auto d = userDir().getChildFile (juce::File::createLegalFileName (name));
    d.createDirectory();
    return d;
}
} // namespace pd
