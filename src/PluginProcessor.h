#pragma once
#include "dsp/SynthEngine.h"
#include "fx/FxRack.h"
#include "patch/PatchGraph.h"
#include "perform/Perform.h"
#include "presets/PresetFormat.h"
#include "presets/PresetLibrary.h"
#include <set>

class PureDevotionProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    PureDevotionProcessor();
    ~PureDevotionProcessor() override;

    // AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Pure Devotion"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return currentPresetName; }
    void changeProgramName (int, const juce::String& n) override { currentPresetName = n; }
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- model access (message thread)
    juce::AudioProcessorValueTreeState apvts;
    pd::WavetableBank& wavetables() { return *wtBank; }
    pd::SampleBank& samples() { return *smpBank; }
    pd::VisualFeed& feed() { return engine.feed(); }
    pd::PatchEngine& patchEngine() { return patch; }
    pd::ChopPlayer& chopPlayer() { return chop; }
    pd::ArpChord& arpChord() { return arp; }
    pd::PresetLibrary& library() { return *presetLibrary; }

    const pd::EngineState& getEngineState() const { return engineState; }
    void setEngineState (const pd::EngineState& s);          // publishes to audio thread
    const pd::PatchProgram& getPatch() const { return patchProgram; }
    void setPatch (const pd::PatchProgram& p);

    float getParamPlain (int index) const;
    void setParamPlain (int index, float plainValue);
    juce::RangedAudioParameter* paramAt (int index) const { return params[(size_t) index]; }

    // modulation matrix helpers
    int addModulation (int source, int destParam, float amount);
    void removeModulation (int slot);
    std::vector<int> slotsForDest (int destParam) const;

    // scenes (XY morph)
    void captureScene (int scene);
    bool hasScene (int scene) const { return ! scenes[(size_t) scene].empty(); }
    void clearScenes();

    // performance lock
    void setLocked (int paramIndex, bool locked);
    bool isLocked (int paramIndex) const { return lockedParams.count (paramIndex) > 0; }

    // MIDI learn
    void startMidiLearn (int paramIndex) { learnParam = paramIndex; }
    void clearMidiLearn (int paramIndex);
    int learnedCCFor (int paramIndex) const;
    bool isLearning() const { return learnParam.load() >= 0; }

    // UI-triggered events (lock-free FIFOs)
    void triggerChopPad (int slot, float velocity);
    void auditionNote (int note, float velocity, bool on);

    // presets
    pd::PresetData capturePreset() const;
    void applyPreset (const pd::PresetData& d);
    bool loadPresetFile (const juce::File& f);
    bool savePresetFile (const juce::File& f, const pd::PresetMeta& meta);
    juce::String currentPresetName { "Init" };
    juce::File currentPresetFile;
    // A/B compare
    void abSelect (int slot);
    void abCopyToOther();
    int abCurrent() const { return abSlot; }

    // UI settings persisted in state
    juce::ValueTree uiState { "UI" };
    juce::StringArray macroNames { "Macro 1", "Macro 2", "Macro 3", "Macro 4", "Macro 5", "Macro 6", "Macro 7", "Macro 8" };

    // user wavetables
    int importWavetable (const juce::File& audioFile);
    int addUserWavetable (const juce::String& name, const std::vector<float>& frames, int numFrames);

    int latencyOsIndex = -1;

private:
    void timerCallback() override;
    void applyMorph();
    void publishEngineState() { engineLock.write (engineState); }
    static juce::AudioProcessor::BusesProperties makeBuses();

    juce::SharedResourcePointer<pd::WavetableBank> wtBank;
    juce::SharedResourcePointer<pd::SampleBank> smpBank;
    juce::SharedResourcePointer<pd::PresetLibrary> presetLibrary;
    std::vector<juce::RangedAudioParameter*> params;
    std::vector<std::atomic<float>*> raw;

    pd::SynthEngine engine;
    pd::FxRack fxRack;
    pd::PatchEngine patch;
    pd::ArpChord arp;
    pd::RiserEngine riser;
    pd::BeatRepeat beatRepeat;
    pd::ChopPlayer chop;

    // message-thread copies + seqlocks
    pd::EngineState engineState;
    pd::SeqLocked<pd::EngineState> engineLock;
    pd::PatchProgram patchProgram;
    pd::SeqLocked<pd::PatchProgram> patchLock;

    // audio-thread state
    pd::EngineState esAudio;
    pd::PatchProgram patchAudio;
    uint32_t esSeen = 0xffffffffu, patchSeen = 0xffffffffu;
    std::vector<float> plain, norm;
    juce::AudioBuffer<float> work;
    float lastMacroSent[pd::kNumMacros] {};
    juce::MidiBuffer midiOut, midiScratch;
    juce::AudioBuffer<float> scBuf;

    // fifos
    struct UiEvent { int type, a; float v; };   // type 0 = chop pad, 1 = note on, 2 = note off
    juce::AbstractFifo uiFifo { 256 };
    UiEvent uiEvents[256];

    std::atomic<int> learnParam { -1 };
    std::atomic<int> learnedCC { -1 }, learnedFor { -1 };

    std::array<std::map<int, float>, pd::kNumScenes> scenes;
    float lastX = -1, lastY = -1;
    std::set<int> lockedParams;
    int abSlot = 0;
    pd::PresetData abData[2];
    bool abValid[2] { false, false };
    double currentSampleRate = 48000;
    juce::StringArray userTableNames;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PureDevotionProcessor)
};
