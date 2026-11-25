#pragma once
#include "Widgets.h"
#include "DogVisualizer.h"
#include "../patch/PatchGraph.h"
#include "../presets/PresetLibrary.h"
#include <memory>
#include <vector>

class PureDevotionProcessor;

namespace pd
{
using KnobPtr = std::unique_ptr<ParamKnob>;

// ------------------------------------------------------------ OSC
class OscPage : public juce::Component
{
public:
    explicit OscPage (PureDevotionProcessor& p);
    ~OscPage() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    void setVisualizerFullScreen (bool);
    std::vector<DogVisualizer*> visualizers();
private:
    struct OscStrip;
    struct FilterStrip;
    PureDevotionProcessor& proc;
    std::vector<std::unique_ptr<OscStrip>> oscs;
    std::vector<std::unique_ptr<FilterStrip>> filters;
    Panel subNoisePanel { "Sub + Noise" }, ampPanel { "Amp Envelope" };
    std::vector<std::unique_ptr<juce::Component>> owned;
    std::vector<KnobPtr> subKnobs, ampKnobs;
};

// ------------------------------------------------------------ PATCH
class PatchPage : public juce::Component, private juce::Timer
{
public:
    explicit PatchPage (PureDevotionProcessor& p);
    ~PatchPage() override;
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    void timerCallback() override { repaint(); }
    class Canvas;
    PureDevotionProcessor& proc;
    std::unique_ptr<Canvas> canvas;
    juce::ListBox palette;
    struct PaletteModel;
    std::unique_ptr<PaletteModel> paletteModel;
    juce::TextButton clearBtn { "Clear" };
};

// ------------------------------------------------------------ FX
class FxPage : public juce::Component, private juce::Timer
{
public:
    explicit FxPage (PureDevotionProcessor& p);
    ~FxPage() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    void rebuild();
private:
    void timerCallback() override;
    struct Row;
    PureDevotionProcessor& proc;
    juce::Viewport view;
    juce::Component content;
    std::vector<std::unique_ptr<Row>> rows;
    KnobPtr busA, busB;
    int8_t lastOrder[kNumFx] {};
};

// ------------------------------------------------------------ MOD
class ModPage : public juce::Component, private juce::Timer
{
public:
    explicit ModPage (PureDevotionProcessor& p);
    ~ModPage() override;
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    void timerCallback() override;
    void selectSource (int s);
    class ShapeEditor;
    class MatrixRow;
    PureDevotionProcessor& proc;
    juce::OwnedArray<juce::TextButton> sourceTabs;
    int selected = 0;   // 0-7 LFO, 8-11 ENV, 12-13 SEQ
    std::vector<KnobPtr> knobs;
    std::vector<std::unique_ptr<juce::Component>> extras;
    std::unique_ptr<ShapeEditor> shape;
    std::vector<std::unique_ptr<ModSourceChip>> chips;
    juce::Viewport matrixView;
    juce::Component matrixContent;
    std::vector<std::unique_ptr<MatrixRow>> matrixRows;
};

// ------------------------------------------------------------ PERFORM
class PerformPage : public juce::Component, private juce::Timer
{
public:
    explicit PerformPage (PureDevotionProcessor& p);
    ~PerformPage() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    std::function<void (bool)> onFullScreenVisualizer;
private:
    void timerCallback() override;
    class XYPad;
    class ChopPad;
    PureDevotionProcessor& proc;
    std::vector<KnobPtr> macros;
    juce::OwnedArray<juce::Label> macroNames;
    std::unique_ptr<XYPad> xy;
    juce::OwnedArray<juce::TextButton> sceneButtons;
    std::unique_ptr<ParamToggle> morphToggle, dropKill, arpOn, arpLatch, chordOn;
    std::unique_ptr<juce::TextButton> riserArm;
    std::unique_ptr<MomentaryButton> stutter, tapeStop;
    std::unique_ptr<ParamCombo> riserBars, stutterDiv, arpMode, arpDiv, arpGroove, chordType, chopSample;
    std::vector<KnobPtr> riserKnobs, arpKnobs, chopKnobs;
    std::unique_ptr<ChopPad> chop;
    juce::TextButton learnChord { "Learn Chord" }, fullVis { "Visualiser Full Screen" }, lockList { "Locked Params" };
    juce::Slider slotPitch, slotFormant;
    juce::Label slotLabel;
    int selectedSlot = 0, chopNamesFilled = 0;
    Panel macroPanel { "Macros" }, scenePanel { "Scene Morph" }, riserPanel { "Riser / Drop" }, arpPanel { "Chord + Arp + Groove" },
          chopPanel { "Vocal Chop Pad" }, fxPanel { "Stutter / Tape Stop" };
};

// ------------------------------------------------------------ BROWSER
class BrowserPage : public juce::Component, private juce::ListBoxModel, private juce::Timer
{
public:
    explicit BrowserPage (PureDevotionProcessor& p);
    ~BrowserPage() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    void refresh();
    std::function<void()> onPresetLoaded;
    void loadRelative (int delta);
private:
    int getNumRows() override { return (int) results.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void selectedRowsChanged (int row) override;
    void timerCallback() override;
    void loadRow (int row);
    void savePreset();
    void fillCombos();
    PureDevotionProcessor& proc;
    juce::TextEditor search;
    juce::ComboBox bank, track, category, genre, mood, type, folder, rating;
    juce::ToggleButton favOnly { "Favourites" }, mpeOnly { "MPE" }, cpuOnly { "CPU-light" }, macroOnly { "Macros-ready" }, audition { "Audition" };
    juce::TextButton loadBtn { "Load" }, similarBtn { "Similar Sound" }, saveBtn { "Save As" }, folderBtn { "New Folder" }, rescanBtn { "Rescan" },
                     favBtn { "Favourite" }, clearBtn { "Clear Filters" };
    juce::OwnedArray<juce::TextButton> stars;
    juce::ListBox list;
    std::vector<int> results;
    int auditionNote = -1, auditionTicks = 0;
    bool similarMode = false;
    juce::String status;
};
} // namespace pd
