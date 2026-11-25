#pragma once
#include "PresetFormat.h"
#include <vector>

namespace pd
{
struct GenSpec
{
    int category = 0;        // index into PresetGenerator::categories()
    juce::String bank, release, track, name;
    int bankIndex = 0;
    uint32_t seed = 1;
};

namespace PresetGenerator
{
    const juce::StringArray& categories();            // display names
    const std::vector<int>& categoryTargets();        // minimum counts
    juce::String categoryType (int category);         // short type tag (Bass, Lead, ...)
    // Full plan of factory presets (2,850). neutralNames replaces track titles with neutral words.
    std::vector<GenSpec> plan (bool neutralNames);
    // Build one preset. `variant` re-rolls the recipe when QA rejects a render.
    PresetData generate (const GenSpec& spec, int variant = 0);
    // QA test phrase: notes to hold for a category.
    std::vector<int> testNotes (int category);
}
} // namespace pd
