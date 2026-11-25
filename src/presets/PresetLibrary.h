#pragma once
#include "PresetFormat.h"
#include <functional>
#include <set>
#include <vector>

namespace pd
{
class PresetLibrary : private juce::Thread
{
public:
    PresetLibrary();
    ~PresetLibrary() override;

    static juce::File factoryDir();
    static juce::File userDir();
    static juce::File dataDir();

    // Rescan on a background thread; onDone is called on the message thread.
    void rescanAsync (std::function<void()> onDone = {});
    void rescanBlocking();
    bool isScanning() const noexcept { return isThreadRunning(); }

    struct Filter
    {
        juce::String text, bank, track, category, genre, mood, type, folder;
        bool favouritesOnly = false, mpeOnly = false, cpuLightOnly = false, macrosOnly = false;
        int minRating = 0;
    };
    std::vector<int> query (const Filter& f) const;
    std::vector<int> similar (int index, int k) const;

    int size() const;
    PresetMeta get (int i) const;
    int indexOfFile (const juce::File& f) const;

    bool isFavourite (const PresetMeta& m) const;
    void setFavourite (const PresetMeta& m, bool fav);
    int rating (const PresetMeta& m) const;
    void setRating (const PresetMeta& m, int stars);

    juce::StringArray banks() const;
    juce::StringArray tracks (const juce::String& bank) const;
    juce::StringArray categories() const;
    juce::StringArray userFolders() const;
    juce::File createUserFolder (const juce::String& name);
    juce::String folderOf (const PresetMeta& m) const;

private:
    void run() override;
    void scanInto (std::vector<PresetMeta>& out);
    void loadDb();
    void saveDb() const;
    static juce::String key (const PresetMeta& m);

    mutable juce::CriticalSection lock;
    std::vector<PresetMeta> presets;
    std::vector<std::array<float, kNumFeatures>> normFeatures;
    std::map<juce::String, int> ratings;
    std::set<juce::String> favourites;
    std::function<void()> pendingDone;
};
} // namespace pd
