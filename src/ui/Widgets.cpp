#include "Widgets.h"
#include "../PluginProcessor.h"

namespace pd
{
// ============================================================ ParamKnob
ParamKnob::ParamKnob (PureDevotionProcessor& p, const juce::String& id, const juce::String& l, bool b)
    : proc (p), paramIndex (ParamTable::get().indexOf (id)), label (l), big (b)
{
    jassert (paramIndex >= 0);
    if (label.isEmpty() && paramIndex >= 0)
    {
        label = ParamTable::get()[paramIndex].name;
        // drop the group prefix ("Osc A Level" -> "Level")
        const auto g = ParamTable::get()[paramIndex].group;
        if (label.startsWith (g + " ")) label = label.substring (g.length() + 1);
    }
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
    slider.setPopupDisplayEnabled (true, true, nullptr);
    slider.setDoubleClickReturnValue (true, ParamTable::get()[juce::jmax (0, paramIndex)].def);
    addAndMakeVisible (slider);
    slider.addMouseListener (this, false);
    att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, slider);
    startTimerHz (15);
}

ParamKnob::~ParamKnob() { stopTimer(); slider.removeMouseListener (this); }

void ParamKnob::timerCallback() { if (! proc.slotsForDest (paramIndex).empty()) repaint(); }

void ParamKnob::resized()
{
    auto r = getLocalBounds();
    r.removeFromBottom (big ? 18 : 13);
    const int side = juce::jmin (r.getWidth(), r.getHeight());
    slider.setBounds (r.withSizeKeepingCentre (side, side));
}

void ParamKnob::paint (juce::Graphics& g)
{
    const auto& t = ThemeManager::current();
    auto r = getLocalBounds().toFloat();
    auto lr = r.removeFromBottom (big ? 18.f : 13.f);
    const bool locked = proc.isLocked (paramIndex);
    g.setColour (locked ? t.accent : t.textDim);
    drawTracked (g, label.toUpperCase(), lr, Fonts::header (big ? 13.f : 10.f), 0.06f, juce::Justification::centredTop);
    if (dragHover)
    {
        g.setColour (t.accent.withAlpha (0.25f));
        g.fillEllipse (slider.getBounds().toFloat().expanded (3.f));
    }
    if (proc.learnedCCFor (paramIndex) >= 0)
    {
        g.setColour (t.accent2.brighter());
        g.setFont (Fonts::mono (8.f));
        g.drawText ("CC" + juce::String (proc.learnedCCFor (paramIndex)), getLocalBounds().removeFromTop (10), juce::Justification::topRight);
    }
}

void ParamKnob::paintOverChildren (juce::Graphics& g)
{
    const auto slots = proc.slotsForDest (paramIndex);
    if (slots.empty()) return;
    const auto& t = ThemeManager::current();
    const auto& T = ParamTable::get();
    const auto& es = proc.getEngineState();
    auto sb = slider.getBounds().toFloat();
    const float side = juce::jmin (sb.getWidth(), sb.getHeight());
    const auto c = sb.getCentre();
    const float start = juce::MathConstants<float>::pi * 1.2f, end = juce::MathConstants<float>::pi * 2.8f;
    const float base = T[paramIndex].range.convertTo0to1 (proc.getParamPlain (paramIndex));
    float live = base;
    int ring = 0;
    for (int s : slots)
    {
        const float amt = proc.getParamPlain (IX().modAmt[s]);
        const float r = side * 0.5f + 2.f + (float) ring * 3.f;
        const float a0 = start + base * (end - start);
        const float a1 = start + clamp01 (base + amt) * (end - start);
        juce::Path arc;
        arc.addCentredArc (c.x, c.y, r, r, 0, juce::jmin (a0, a1), juce::jmax (a0, a1), true);
        drawHalationArc (g, arc, ring == 0 ? t.accent : t.accent3, 2.f);
        const float sv = proc.feed().modValue[es.slots[s].src].load();
        live += applyCurve (es.slots[s].curve, sv) * amt;
        ++ring;
    }
    live = clamp01 (live);
    const float la = start + live * (end - start);
    const float rr = side * 0.5f + 2.f;
    g.setColour (t.text);
    g.fillEllipse (c.x + std::sin (la) * rr - 2.5f, c.y - std::cos (la) * rr - 2.5f, 5.f, 5.f);
}

void ParamKnob::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) showMenu();
}

void ParamKnob::showMenu()
{
    juce::PopupMenu m;
    const int cc = proc.learnedCCFor (paramIndex);
    m.addSectionHeader (ParamTable::get()[paramIndex].name);
    m.addItem (1, proc.isLearning() ? "Waiting for CC..." : "MIDI Learn");
    if (cc >= 0) m.addItem (2, "Clear MIDI CC " + juce::String (cc));
    m.addItem (3, "Performance lock", true, proc.isLocked (paramIndex));
    m.addItem (4, "Reset to default");
    const auto slots = proc.slotsForDest (paramIndex);
    if (! slots.empty())
    {
        m.addSeparator();
        for (int s : slots)
        {
            juce::PopupMenu sub;
            for (int k = 0; k < 9; ++k)
            {
                static const float amts[] = { 1.f, 0.5f, 0.25f, 0.1f, 0.f, -0.1f, -0.25f, -0.5f, -1.f };
                sub.addItem (1000 + s * 16 + k, (amts[k] > 0 ? "+" : "") + juce::String ((int) (amts[k] * 100)) + "%");
            }
            sub.addSeparator();
            sub.addItem (5000 + s, "Remove");
            m.addSubMenu (modSourceNames()[proc.getEngineState().slots[s].src] + "  " + juce::String (proc.getParamPlain (IX().modAmt[s]) * 100.f, 0) + "%", sub);
        }
    }
    juce::Component::SafePointer<ParamKnob> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe] (int r) {
        if (safe == nullptr || r == 0) return;
        auto& p = safe->proc;
        const int pi = safe->paramIndex;
        if (r == 1) p.startMidiLearn (pi);
        else if (r == 2) p.clearMidiLearn (pi);
        else if (r == 3) p.setLocked (pi, ! p.isLocked (pi));
        else if (r == 4) p.setParamPlain (pi, ParamTable::get()[pi].def);
        else if (r >= 5000) p.removeModulation (r - 5000);
        else if (r >= 1000)
        {
            static const float amts[] = { 1.f, 0.5f, 0.25f, 0.1f, 0.f, -0.1f, -0.25f, -0.5f, -1.f };
            const int s = (r - 1000) / 16, k = (r - 1000) % 16;
            p.setParamPlain (IX().modAmt[s], amts[k]);
        }
        safe->repaint();
    });
}

bool ParamKnob::isInterestedInDragSource (const SourceDetails& d)
{
    return d.description.toString().startsWith ("modsrc:") && ParamTable::get()[paramIndex].modulatable
        && ParamTable::get()[paramIndex].kind == PKind::Float;
}

void ParamKnob::itemDropped (const SourceDetails& d)
{
    dragHover = false;
    const int src = d.description.toString().fromFirstOccurrenceOf ("modsrc:", false, false).getIntValue();
    if (src > 0) proc.addModulation (src, paramIndex, 0.25f);
    repaint();
}

// ============================================================ ModSourceChip
ModSourceChip::ModSourceChip (PureDevotionProcessor& p, int s) : source (s), proc (p)
{
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    setTooltip ("Drag onto any knob to modulate it");
    startTimerHz (20);
}

void ModSourceChip::paint (juce::Graphics& g)
{
    const auto& t = ThemeManager::current();
    auto r = getLocalBounds().toFloat().reduced (1.f);
    g.setColour (t.panel.withMultipliedAlpha (1.4f));
    g.fillRoundedRectangle (r, 4.f);
    const float v = proc.feed().modValue[source].load();
    const bool bip = isBipolarSource (source);
    auto bar = r.reduced (3.f).removeFromBottom (3.f);
    g.setColour (t.textDim.withAlpha (0.3f)); g.fillRect (bar);
    g.setColour (t.accent);
    if (bip) { const float cx = bar.getCentreX(); const float w = bar.getWidth() * 0.5f * clampf (v, -1.f, 1.f); g.fillRect (juce::Rectangle<float> (juce::jmin (cx, cx + w), bar.getY(), std::abs (w), bar.getHeight())); }
    else g.fillRect (bar.withWidth (bar.getWidth() * clamp01 (v)));
    g.setColour (t.text);
    g.setFont (Fonts::header (12.f));
    g.drawFittedText (modSourceNames()[source].toUpperCase(), r.reduced (4, 2).withTrimmedBottom (4).toNearestInt(), juce::Justification::centred, 1);
    g.setColour (t.panelEdge.withMultipliedAlpha (1.5f));
    g.drawRoundedRectangle (r, 4.f, 1.f);
}

void ModSourceChip::mouseDrag (const juce::MouseEvent& e)
{
    if (e.getDistanceFromDragStart() < 4) return;
    if (auto* c = juce::DragAndDropContainer::findParentDragContainerFor (this))
        if (! c->isDragAndDropActive()) c->startDragging ("modsrc:" + juce::String (source), this);
}

// ============================================================ toggles / combos
ParamToggle::ParamToggle (PureDevotionProcessor& p, const juce::String& id, const juce::String& text)
{
    setButtonText (text);
    att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, id, *this);
}

ParamCombo::ParamCombo (PureDevotionProcessor& p, const juce::String& id, const juce::StringArray& items)
{
    const auto& T = ParamTable::get();
    const int i = T.indexOf (id);
    const auto& info = T[i];
    if (! items.isEmpty()) addItemList (items, 1);
    else if (info.kind == PKind::Choice) addItemList (info.choices, 1);
    else for (int v = (int) info.range.start; v <= (int) info.range.end; ++v) addItem (juce::String (v), v - (int) info.range.start + 1);
    att = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, id, *this);
}

MomentaryButton::MomentaryButton (PureDevotionProcessor& p, const juce::String& id, const juce::String& text)
    : proc (p), paramIndex (ParamTable::get().indexOf (id))
{
    setButtonText (text);
    setClickingTogglesState (false);
}
void MomentaryButton::mouseDown (const juce::MouseEvent& e)
{
    juce::TextButton::mouseDown (e);
    proc.setParamPlain (paramIndex, 1.f); setToggleState (true, juce::dontSendNotification);
}
void MomentaryButton::mouseUp (const juce::MouseEvent& e)
{
    juce::TextButton::mouseUp (e);
    if (! e.mods.isShiftDown()) { proc.setParamPlain (paramIndex, 0.f); setToggleState (false, juce::dontSendNotification); }   // shift = latch
}

// ============================================================ Panel
void Panel::paintPanel (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title)
{
    const auto& t = ThemeManager::current();
    g.setColour (juce::Colours::black.withAlpha (t.lightPanels ? 0.06f : 0.25f));
    g.fillRoundedRectangle (r.translated (0, 2), 8.f);
    g.setColour (t.panel);
    g.fillRoundedRectangle (r, 8.f);
    g.setColour (t.panelEdge);
    g.drawRoundedRectangle (r.reduced (0.5f), 8.f, t.background == 2 ? 2.f : 1.f);
    if (title.isNotEmpty())
    {
        auto tr = r.reduced (10, 4).removeFromTop (16);
        g.setColour (t.accent);
        g.fillRect (tr.getX(), tr.getCentreY() - 4, 3.f, 8.f);
        g.setColour (t.text);
        drawTracked (g, title.toUpperCase(), tr.withTrimmedLeft (8), Fonts::header (13.f), 0.18f, juce::Justification::centredLeft);
    }
}

void Panel::paint (juce::Graphics& g) { paintPanel (g, getLocalBounds().toFloat(), title); }

void layoutRow (juce::Rectangle<int> r, std::initializer_list<juce::Component*> comps, int gap)
{
    const int n = (int) comps.size();
    if (n == 0) return;
    const int w = (r.getWidth() - gap * (n - 1)) / n;
    for (auto* c : comps) { if (c) c->setBounds (r.removeFromLeft (w)); r.removeFromLeft (gap); }
}
} // namespace pd
