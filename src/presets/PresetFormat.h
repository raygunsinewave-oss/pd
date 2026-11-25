#pragma once
#include "../core/ModMatrix.h"
#include <array>
#include <map>

namespace pd
{
constexpr int kPresetFormatVersion = 2;
constexpr int kNumFeatures = 8;

struct PresetMeta
{
    juce::String name, author { "Raygun" }, bank, track, release, category, type, description;
    juce::StringArray genres, moods, tags;     // tags = free-form (includes release + track)
    bool macrosReady = false, mpe = false, cpuLight = false;
    std::array<float, kNumFeatures> features {};
    float peakDb = -100.f, rmsDb = -100.f;
    juce::File file;
    bool factory = false;
    int bankIndex = 0;
    juce::String uid() const { return file.existsAsFile() ? file.getFullPathName() : bank + "/" + name; }
};

struct PresetData
{
    PresetMeta meta;
    std::map<juce::String, float> params;          // plain values by parameter id
    juce::ValueTree engine { "ENGINE" };
    juce::ValueTree patch { "PATCH" };
    std::array<std::map<juce::String, float>, kNumScenes> scenes;
    bool hasScenes = false;
    juce::StringArray macroNames;
};

namespace PresetFormat
{
    juce::var toJson (const PresetData& p);
    bool fromJson (const juce::var& v, PresetData& out, juce::String& error);
    bool save (const juce::File& f, const PresetData& p);
    bool load (const juce::File& f, PresetData& out, juce::String& error);
    juce::var metaToJson (const PresetMeta& m);
    void metaFromJson (const juce::var& v, PresetMeta& m);
    juce::var treeToJson (const juce::ValueTree& t);
    juce::ValueTree treeFromJson (const juce::var& v);
    juce::String fileExtension();
}
} // namespace pd
