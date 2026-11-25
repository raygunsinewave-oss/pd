#pragma once
#include "PluginProcessor.h"
#include "ui/Pages.h"
#include <juce_opengl/juce_opengl.h>
#include <juce_audio_utils/juce_audio_utils.h>

class PureDevotionEditor : public juce::AudioProcessorEditor,
                           public juce::DragAndDropContainer,
                           private juce::Timer,
                           private juce::MidiKeyboardState::Listener
{
public:
    explicit PureDevotionEditor (PureDevotionProcessor&);
    ~PureDevotionEditor() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;

    void showTab (int t);
    void setTheme (int t);
    void tickForScreenshot();

    static constexpr int kBaseW = 1280, kBaseH = 800;

private:
    void timerCallback() override;
    void handleNoteOn (juce::MidiKeyboardState*, int ch, int note, float vel) override;
    void handleNoteOff (juce::MidiKeyboardState*, int ch, int note, float vel) override;
    void showSettingsMenu();
    void setScale (float s);
    void refreshPresetName();
    void setPerformFullScreen (bool on);

    PureDevotionProcessor& proc;
    pd::PDLookAndFeel lnf;
    juce::OpenGLContext gl;
    juce::Component content;   // laid out at base size, scaled by transform

    pd::OscPage oscPage;
    pd::PatchPage patchPage;
    pd::FxPage fxPage;
    pd::ModPage modPage;
    pd::PerformPage performPage;
    pd::BrowserPage browserPage;
    std::array<juce::Component*, 6> pages;
    juce::OwnedArray<juce::TextButton> tabs;
    int currentTab = 0;

    juce::TextButton prevBtn { "<" }, nextBtn { ">" }, presetName { "Init" }, aBtn { "A" }, bBtn { "B" }, copyAB { "A>B" }, settingsBtn { "Settings" };
    juce::ComboBox themeBox, scaleBox;
    juce::MidiKeyboardState keyState;
    juce::MidiKeyboardComponent keyboard { keyState, juce::MidiKeyboardComponent::horizontalKeyboard };
    std::unique_ptr<pd::DogVisualizer> fullVis;
    float animTime = 0.f;
    int grainFrame = 0;
    float scale = 1.f;
    juce::TooltipWindow tooltips { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PureDevotionEditor)
};
