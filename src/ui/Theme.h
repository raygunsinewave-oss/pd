#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include <functional>

namespace pd
{
namespace Palette
{
    const juce::Colour ultramarine (0xff14245E), deepNight (0xff0C1230), sodium (0xffE8492A), shutter (0xff3F6B34),
                       tan (0xffC47A3A), chrome (0xffD9DDE3), pink (0xffF2A7C3), lavender (0xffB9A3D9), text (0xffF3F1EC);
}

struct Theme
{
    juce::String key, name;
    int background = 0;          // 0 ultramarine, 1 shutter, 2 cassette, 3 sketch, 4 dusk
    juce::Colour bgTop, bgBottom, panel, panelEdge, accent, accent2, accent3, chrome, text, textDim, ink, glow;
    bool lightPanels = false;    // dark text on light surfaces
};

class ThemeManager
{
public:
    static int numThemes();
    static const Theme& theme (int i);
    static const Theme& current();
    static int currentIndex();
    static void setCurrent (int i);
    static void addListener (void* owner, std::function<void()> fn);
    static void removeListener (void* owner);

    // Paint the full-window themed background (time drives subtle animation).
    static void paintBackground (juce::Graphics& g, juce::Rectangle<float> r, const Theme& t, float time);
    // Film grain overlay (2-4 % opacity).
    static void paintGrain (juce::Graphics& g, juce::Rectangle<int> r, float opacity, int frame);
};

namespace Fonts
{
    juce::Font header (float height);     // condensed bold grotesk, caps
    juce::Font mono (float height, bool bold = false);
    juce::Font label (float height);
}

void drawRaygunLogo (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour body, juce::Colour beam);
void drawHalationArc (juce::Graphics& g, const juce::Path& p, juce::Colour c, float thickness);
// Draw spaced (tracked) uppercase text.
void drawTracked (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, const juce::Font& f,
                  float tracking, juce::Justification just);

class PDLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PDLookAndFeel();
    void refresh();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int h) override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbStart, int thumbSize, bool over, bool down) override;

    static void drawChromeKnob (juce::Graphics& g, juce::Rectangle<float> r, float angle, const Theme& t, bool chain);
};
} // namespace pd
