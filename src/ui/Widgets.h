#pragma once
#include "Theme.h"
#include "../core/ModMatrix.h"
#include <juce_audio_processors/juce_audio_processors.h>

class PureDevotionProcessor;

namespace pd
{
// Rotary parameter knob with mod rings, drag-and-drop mod assignment and a context menu
// (MIDI learn, performance lock, modulation amounts).
class ParamKnob : public juce::Component, public juce::DragAndDropTarget, private juce::Timer
{
public:
    ParamKnob (PureDevotionProcessor& p, const juce::String& paramId, const juce::String& label = {}, bool big = false);
    ~ParamKnob() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override { dragHover = true; repaint(); }
    void itemDragExit (const SourceDetails&) override { dragHover = false; repaint(); }
    void itemDropped (const SourceDetails&) override;
    void setLabel (const juce::String& l) { label = l; repaint(); }
    int getParamIndex() const { return paramIndex; }
    juce::Slider slider;

private:
    void timerCallback() override;
    void showMenu();
    PureDevotionProcessor& proc;
    int paramIndex;
    juce::String label;
    bool big, dragHover = false;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
};

class ModSourceChip : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    ModSourceChip (PureDevotionProcessor& p, int source);
    void paint (juce::Graphics&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    int source;
private:
    void timerCallback() override { repaint(); }
    PureDevotionProcessor& proc;
};

class ParamToggle : public juce::ToggleButton
{
public:
    ParamToggle (PureDevotionProcessor& p, const juce::String& paramId, const juce::String& text);
private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> att;
};

class ParamCombo : public juce::ComboBox
{
public:
    ParamCombo (PureDevotionProcessor& p, const juce::String& paramId, const juce::StringArray& items = {});
private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> att;
};

// Momentary text button bound to a bool parameter (on while held).
class MomentaryButton : public juce::TextButton
{
public:
    MomentaryButton (PureDevotionProcessor& p, const juce::String& paramId, const juce::String& text);
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
private:
    PureDevotionProcessor& proc; int paramIndex;
};

class Panel : public juce::Component
{
public:
    explicit Panel (const juce::String& title = {}) : title (title) { setInterceptsMouseClicks (false, true); }
    void paint (juce::Graphics& g) override;
    juce::String title;
    static void paintPanel (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title);
};

// Lays out components left-to-right in equal cells.
void layoutRow (juce::Rectangle<int> r, std::initializer_list<juce::Component*> comps, int gap = 4);
template <typename T> void layoutRowVec (juce::Rectangle<int> r, const std::vector<std::unique_ptr<T>>& comps, int gap = 4)
{
    if (comps.empty()) return;
    const int w = (r.getWidth() - gap * ((int) comps.size() - 1)) / (int) comps.size();
    for (auto& c : comps) { c->setBounds (r.removeFromLeft (w)); r.removeFromLeft (gap); }
}
} // namespace pd
