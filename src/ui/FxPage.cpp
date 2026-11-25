#include "Pages.h"
#include "../PluginProcessor.h"

namespace pd
{
struct FxPage::Row : public juce::Component, public juce::SettableTooltipClient
{
    Row (PureDevotionProcessor& p, FxPage& page, int fxType) : proc (p), owner (page), type (fxType)
    {
        const auto& d = fxDef (type);
        const juce::String px = juce::String ("fx_") + d.key + "_";
        on = std::make_unique<ParamToggle> (p, px + "on", "");
        bus = std::make_unique<ParamCombo> (p, px + "bus");
        addAndMakeVisible (*on); addAndMakeVisible (*bus);
        for (int k = 0; k < d.numParams; ++k)
        {
            const juce::String id = px + d.p[k].key;
            if (d.p[k].numChoices > 0) { combos.push_back (std::make_unique<ParamCombo> (p, id)); addAndMakeVisible (*combos.back()); comboSlots.push_back (k); }
            else { knobs.push_back (std::make_unique<ParamKnob> (p, id, d.p[k].name)); addAndMakeVisible (*knobs.back()); knobSlots.push_back (k); }
        }
        for (auto* b : { &up, &down }) { addAndMakeVisible (b); }
        up.onClick = [this] { owner_move (-1); };
        down.onClick = [this] { owner_move (1); };
        setTooltip ("Drag the handle to reorder. Bus A and B run in parallel.");
    }
    void owner_move (int dir)
    {
        auto es = proc.getEngineState();
        int pos = 0; for (int i = 0; i < kNumFx; ++i) if (es.fxOrder[i] == type) pos = i;
        const int np = juce::jlimit (0, kNumFx - 1, pos + dir);
        std::swap (es.fxOrder[pos], es.fxOrder[np]);
        proc.setEngineState (es);
        owner.rebuild();
    }
    void paint (juce::Graphics& g) override
    {
        const auto& t = ThemeManager::current();
        auto r = getLocalBounds().toFloat().reduced (1.f);
        const bool active = proc.getParamPlain (IX().fx[type].on) > 0.5f;
        g.setColour (t.panel.withMultipliedAlpha (active ? 1.5f : 0.9f));
        g.fillRoundedRectangle (r, 6.f);
        g.setColour (active ? t.accent : t.panelEdge);
        g.drawRoundedRectangle (r, 6.f, active ? 1.4f : 1.f);
        // drag handle
        g.setColour (t.textDim);
        for (int k = 0; k < 3; ++k) g.fillRect (8.f, r.getCentreY() - 5.f + (float) k * 5.f, 12.f, 1.5f);
        g.setColour (active ? t.text : t.textDim);
        drawTracked (g, juce::String (fxDef (type).name).toUpperCase(), { 54, 0, 150, r.getHeight() }, Fonts::header (14.f), 0.1f, juce::Justification::centredLeft);
        if (dragging) { g.setColour (t.accent.withAlpha (0.2f)); g.fillRoundedRectangle (r, 6.f); }
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (4, 2);
        r.removeFromLeft (22);
        on->setBounds (r.removeFromLeft (26));
        r.removeFromLeft (156);
        bus->setBounds (r.removeFromLeft (50).reduced (2, 9));
        auto ud = r.removeFromRight (24);
        up.setBounds (ud.removeFromTop (ud.getHeight() / 2)); down.setBounds (ud);
        r.removeFromLeft (8);
        for (auto& c : combos) { c->setBounds (r.removeFromLeft (110).reduced (3, 9)); }
        for (auto& k : knobs) { k->setBounds (r.removeFromLeft (64)); r.removeFromLeft (2); }
    }
    void mouseDown (const juce::MouseEvent& e) override { if (e.x < 26) { dragging = true; dragStartY = getY(); repaint(); } }
    void mouseDrag (const juce::MouseEvent& e) override { if (dragging) { setTopLeftPosition (getX(), dragStartY + e.getDistanceFromDragStartY()); toFront (false); } }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (! dragging) return;
        dragging = false;
        const int target = juce::jlimit (0, kNumFx - 1, (getY() + getHeight() / 2) / getHeight());
        auto es = proc.getEngineState();
        std::vector<int8_t> order (es.fxOrder, es.fxOrder + kNumFx);
        order.erase (std::find (order.begin(), order.end(), (int8_t) type));
        order.insert (order.begin() + target, (int8_t) type);
        for (int i = 0; i < kNumFx; ++i) es.fxOrder[i] = order[(size_t) i];
        proc.setEngineState (es);
        owner.rebuild();
    }

    PureDevotionProcessor& proc;
    FxPage& owner;
    int type;
    bool dragging = false; int dragStartY = 0;
    std::unique_ptr<ParamToggle> on;
    std::unique_ptr<ParamCombo> bus;
    std::vector<std::unique_ptr<ParamCombo>> combos;
    std::vector<KnobPtr> knobs;
    std::vector<int> comboSlots, knobSlots;
    juce::TextButton up { "^" }, down { "v" };
};

FxPage::FxPage (PureDevotionProcessor& p) : proc (p)
{
    addAndMakeVisible (view);
    view.setViewedComponent (&content, false);
    view.setScrollBarsShown (true, false);
    for (int t = 0; t < kNumFx; ++t) { rows.push_back (std::make_unique<Row> (p, *this, t)); content.addAndMakeVisible (*rows.back()); }
    busA = std::make_unique<ParamKnob> (p, "bus_a_lvl", "Bus A");
    busB = std::make_unique<ParamKnob> (p, "bus_b_lvl", "Bus B");
    addAndMakeVisible (*busA); addAndMakeVisible (*busB);
    rebuild();
    startTimerHz (4);
}

FxPage::~FxPage() { stopTimer(); }

void FxPage::timerCallback()
{
    const auto& es = proc.getEngineState();
    if (std::memcmp (es.fxOrder, lastOrder, sizeof (lastOrder)) != 0) rebuild();
    for (auto& r : rows) r->repaint();
}

void FxPage::rebuild()
{
    const auto& es = proc.getEngineState();
    std::memcpy (lastOrder, es.fxOrder, sizeof (lastOrder));
    const int rh = 42;
    content.setSize (juce::jmax (100, view.getWidth() - view.getScrollBarThickness()), rh * kNumFx);
    for (int i = 0; i < kNumFx; ++i)
        rows[(size_t) juce::jlimit (0, kNumFx - 1, (int) es.fxOrder[i])]->setBounds (0, i * rh, content.getWidth(), rh - 2);
}

void FxPage::paint (juce::Graphics& g)
{
    const auto& t = ThemeManager::current();
    auto r = getLocalBounds().removeFromTop (34).toFloat();
    g.setColour (t.text);
    drawTracked (g, "FX RACK  " + juce::String::fromUTF8 ("\xc2\xb7") + "  DRAG TO REORDER  " + juce::String::fromUTF8 ("\xc2\xb7") + "  2 PARALLEL BUSES",
                 r.withTrimmedLeft (8), Fonts::header (15.f), 0.15f, juce::Justification::centredLeft);
}

void FxPage::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (40);
    busB->setBounds (top.removeFromRight (60));
    busA->setBounds (top.removeFromRight (60));
    view.setBounds (r);
    rebuild();
}
} // namespace pd
