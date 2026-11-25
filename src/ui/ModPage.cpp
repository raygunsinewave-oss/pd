#include "Pages.h"
#include "../PluginProcessor.h"
#include "../dsp/Modules.h"

namespace pd
{
// ------------------------------------------------------------ shape editor (LFO draw / MSEG / step seq)
class ModPage::ShapeEditor : public juce::Component
{
public:
    explicit ShapeEditor (PureDevotionProcessor& p) : proc (p) {}
    int sel = 0;

    void paint (juce::Graphics& g) override
    {
        const auto& t = ThemeManager::current();
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colours::black.withAlpha (t.lightPanels ? 0.06f : 0.35f));
        g.fillRoundedRectangle (r, 6.f);
        g.setColour (t.textDim.withAlpha (0.2f));
        g.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight());
        auto plot = r.reduced (10.f);
        const auto& es = proc.getEngineState();
        const auto& I = IX();
        juce::Path p;
        if (sel < 8)
        {
            Lfo l; l.shape = (int) proc.getParamPlain (I.lfo[sel].shape); l.drawn = es.lfoDrawn[sel];
            for (int i = 0; i <= 200; ++i)
            {
                const float ph = (float) i / 200.f;
                const float v = l.shape == 5 || l.shape == 6 ? std::sin (ph * 37.f) * 0.7f : l.evaluate (ph);
                const float x = plot.getX() + ph * plot.getWidth(), y = plot.getCentreY() - v * plot.getHeight() * 0.48f;
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            if (l.shape == 7)
                for (int k = 0; k < kLfoDrawPoints; ++k)
                {
                    const float x = plot.getX() + (float) k / kLfoDrawPoints * plot.getWidth(), y = plot.getCentreY() - es.lfoDrawn[sel][k] * plot.getHeight() * 0.48f;
                    g.setColour (t.text); g.fillEllipse (x - 2, y - 2, 4, 4);
                }
            g.setColour (t.textDim); g.setFont (Fonts::mono (10.f));
            g.drawText (l.shape == 7 ? "DRAW: click-drag to paint the shape" : "Set Shape to 'Drawn' to paint a custom LFO", getLocalBounds().reduced (8, 4), juce::Justification::bottomLeft);
            // live phase
            const float lv = proc.feed().modValue[SrcLfo1 + sel].load();
            g.setColour (t.accent); g.fillEllipse (plot.getRight() - 8, plot.getCentreY() - lv * plot.getHeight() * 0.48f - 4, 8, 8);
        }
        else if (sel < 12)
        {
            const int e = sel - 8;
            if (proc.getParamPlain (I.env[e].mode) > 0.5f)
            {
                const auto& m = es.mseg[e];
                const float total = juce::jmax (0.1f, m.t[m.numPoints - 1]);
                for (int k = 0; k < m.numPoints; ++k)
                {
                    const auto pt = msegPoint (m, k, plot, total);
                    if (k == 0) p.startNewSubPath (pt);
                    else
                    {
                        const auto prev = msegPoint (m, k - 1, plot, total);
                        for (int s = 1; s <= 16; ++s)
                        {
                            const float u = (float) s / 16.f;
                            p.lineTo (lerp (prev.x, pt.x, u), lerp (prev.y, pt.y, shapeCurve (u, m.c[k - 1])));
                        }
                    }
                }
                for (int k = 0; k < m.numPoints; ++k)
                {
                    const auto pt = msegPoint (m, k, plot, total);
                    g.setColour (k == m.sustainPoint ? t.accent : t.text);
                    g.fillEllipse (pt.x - 4, pt.y - 4, 8, 8);
                }
                g.setColour (t.textDim); g.setFont (Fonts::mono (10.f));
                g.drawText ("MSEG: click to add, drag to move, double-click to delete, right-click = sustain point. " + juce::String (total, 2) + " s",
                            getLocalBounds().reduced (8, 4), juce::Justification::bottomLeft);
            }
            else
            {
                const auto& x = I.env[e];
                const float a = proc.getParamPlain (x.a), h = proc.getParamPlain (x.h), d = proc.getParamPlain (x.d), s = proc.getParamPlain (x.s), rl = proc.getParamPlain (x.r), c = proc.getParamPlain (x.curve);
                const float total = a + h + d + rl + 0.5f;
                auto X = [&] (float tm) { return plot.getX() + tm / total * plot.getWidth(); };
                auto Y = [&] (float v) { return plot.getBottom() - v * plot.getHeight(); };
                p.startNewSubPath (X (0), Y (0));
                for (int i = 1; i <= 24; ++i) p.lineTo (X (a * i / 24.f), Y (shapeCurve ((float) i / 24.f, c)));
                p.lineTo (X (a + h), Y (1));
                for (int i = 1; i <= 24; ++i) p.lineTo (X (a + h + d * i / 24.f), Y (lerp (1.f, s, shapeCurve ((float) i / 24.f, c))));
                p.lineTo (X (a + h + d + 0.5f), Y (s));
                for (int i = 1; i <= 24; ++i) p.lineTo (X (a + h + d + 0.5f + rl * i / 24.f), Y (lerp (s, 0.f, shapeCurve ((float) i / 24.f, c))));
            }
        }
        else
        {
            const int s = sel - 12;
            const int len = (int) proc.getParamPlain (I.seq[s].len);
            const float w = plot.getWidth() / kSeqSteps;
            for (int k = 0; k < kSeqSteps; ++k)
            {
                const float v = proc.getParamPlain (I.seq[s].step[k]);
                auto bar = juce::Rectangle<float> (plot.getX() + (float) k * w + 2, juce::jmin (plot.getCentreY(), plot.getCentreY() - v * plot.getHeight() * 0.5f),
                                                   w - 4, std::abs (v) * plot.getHeight() * 0.5f);
                g.setColour (k < len ? t.accent : t.textDim.withAlpha (0.3f));
                g.fillRoundedRectangle (bar, 2.f);
            }
            g.setColour (t.textDim); g.setFont (Fonts::mono (10.f));
            g.drawText ("STEPS: click-drag to set values", getLocalBounds().reduced (8, 4), juce::Justification::bottomLeft);
        }
        if (! p.isEmpty()) drawHalationArc (g, p, t.accent, 2.f);
    }

    static juce::Point<float> msegPoint (const Mseg& m, int k, juce::Rectangle<float> plot, float total)
    {
        return { plot.getX() + m.t[k] / total * plot.getWidth(), plot.getBottom() - m.y[k] * plot.getHeight() };
    }

    void mouseDown (const juce::MouseEvent& e) override { edit (e, true); }
    void mouseDrag (const juce::MouseEvent& e) override { edit (e, false); }
    void mouseUp (const juce::MouseEvent&) override { dragPoint = -1; }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (sel < 8 || sel >= 12 || proc.getParamPlain (IX().env[sel - 8].mode) < 0.5f) return;
        auto es = proc.getEngineState(); auto& m = es.mseg[sel - 8];
        const int k = hitPoint (m, e.position);
        if (k > 0 && k < m.numPoints - 1 && m.numPoints > 2)
        {
            for (int i = k; i < m.numPoints - 1; ++i) { m.t[i] = m.t[i + 1]; m.y[i] = m.y[i + 1]; m.c[i] = m.c[i + 1]; }
            --m.numPoints; m.sustainPoint = juce::jmin (m.sustainPoint, m.numPoints - 1);
            proc.setEngineState (es); repaint();
        }
    }

private:
    int hitPoint (const Mseg& m, juce::Point<float> p) const
    {
        auto plot = getLocalBounds().toFloat().reduced (10.f);
        const float total = juce::jmax (0.1f, m.t[m.numPoints - 1]);
        for (int k = 0; k < m.numPoints; ++k) if (msegPoint (m, k, plot, total).getDistanceFrom (p) < 8.f) return k;
        return -1;
    }
    void edit (const juce::MouseEvent& e, bool down)
    {
        auto plot = getLocalBounds().toFloat().reduced (10.f);
        const float u = clamp01 ((e.position.x - plot.getX()) / plot.getWidth());
        const float vb = clampf ((plot.getCentreY() - e.position.y) / (plot.getHeight() * 0.48f), -1.f, 1.f);
        const auto& I = IX();
        if (sel < 8)
        {
            if ((int) proc.getParamPlain (I.lfo[sel].shape) != 7) return;
            auto es = proc.getEngineState();
            es.lfoDrawn[sel][juce::jlimit (0, kLfoDrawPoints - 1, (int) (u * kLfoDrawPoints))] = vb;
            proc.setEngineState (es);
        }
        else if (sel < 12)
        {
            if (proc.getParamPlain (I.env[sel - 8].mode) < 0.5f) return;
            auto es = proc.getEngineState(); auto& m = es.mseg[sel - 8];
            const float total = juce::jmax (0.1f, m.t[m.numPoints - 1]);
            const float vy = clamp01 ((plot.getBottom() - e.position.y) / plot.getHeight());
            if (down)
            {
                dragPoint = hitPoint (m, e.position);
                if (e.mods.isPopupMenu()) { if (dragPoint >= 0) m.sustainPoint = m.sustainPoint == dragPoint ? -1 : dragPoint; dragPoint = -1; proc.setEngineState (es); repaint(); return; }
                if (dragPoint < 0 && m.numPoints < kMsegPoints)
                {
                    const float tm = u * total;
                    int ins = 1; while (ins < m.numPoints && m.t[ins] < tm) ++ins;
                    for (int i = m.numPoints; i > ins; --i) { m.t[i] = m.t[i - 1]; m.y[i] = m.y[i - 1]; m.c[i] = m.c[i - 1]; }
                    m.t[ins] = tm; m.y[ins] = vy; m.c[ins] = 0; ++m.numPoints;
                    if (m.sustainPoint >= ins) ++m.sustainPoint;
                    dragPoint = ins;
                }
            }
            if (dragPoint >= 0)
            {
                const float lo = dragPoint > 0 ? m.t[dragPoint - 1] + 0.001f : 0.f;
                const float hi = dragPoint < m.numPoints - 1 ? m.t[dragPoint + 1] - 0.001f : total * 1.5f + 0.5f;
                if (dragPoint > 0) m.t[dragPoint] = juce::jlimit (lo, hi, u * total * (dragPoint == m.numPoints - 1 ? 1.2f : 1.f));
                m.y[dragPoint] = vy;
            }
            proc.setEngineState (es);
        }
        else
        {
            const int k = juce::jlimit (0, kSeqSteps - 1, (int) (u * kSeqSteps));
            proc.setParamPlain (I.seq[sel - 12].step[k], vb);
        }
        repaint();
    }
    PureDevotionProcessor& proc;
    int dragPoint = -1;
};

// ------------------------------------------------------------ matrix row
class ModPage::MatrixRow : public juce::Component
{
public:
    MatrixRow (PureDevotionProcessor& p, int s) : proc (p), slot (s)
    {
        for (auto* c : { &src, &aux }) { c->addItemList (modSourceNames(), 1); addAndMakeVisible (c); }
        curve.addItemList (modCurveNames(), 1); addAndMakeVisible (curve);
        amount.setSliderStyle (juce::Slider::LinearBar);
        amount.setTextValueSuffix ("");
        amount.setColour (juce::Slider::textBoxTextColourId, ThemeManager::current().text);
        addAndMakeVisible (amount);
        att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, "mm" + juce::String (s + 1) + "_amt", amount);
        addAndMakeVisible (dest); addAndMakeVisible (remove);
        src.onChange = [this] { auto es = proc.getEngineState(); es.slots[slot].src = (int16_t) (src.getSelectedId() - 1); proc.setEngineState (es); };
        aux.onChange = [this] { auto es = proc.getEngineState(); es.slots[slot].aux = (int16_t) (aux.getSelectedId() - 1); proc.setEngineState (es); };
        curve.onChange = [this] { auto es = proc.getEngineState(); es.slots[slot].curve = (int16_t) (curve.getSelectedId() - 1); proc.setEngineState (es); };
        dest.onClick = [this] { chooseDest(); };
        remove.onClick = [this] { proc.removeModulation (slot); };
        sync();
    }
    void sync()
    {
        const auto& sl = proc.getEngineState().slots[slot];
        src.setSelectedId (sl.src + 1, juce::dontSendNotification);
        aux.setSelectedId (sl.aux + 1, juce::dontSendNotification);
        curve.setSelectedId (sl.curve + 1, juce::dontSendNotification);
        dest.setButtonText (sl.dest >= 0 ? ParamTable::get()[sl.dest].name : juce::String ("<destination>"));
    }
    void chooseDest()
    {
        const auto& T = ParamTable::get();
        std::map<juce::String, juce::PopupMenu> groups; juce::StringArray order;
        for (int idx : T.modDestinations()) { const auto& g = T[idx].group; if (! order.contains (g)) order.add (g); groups[g].addItem (1 + idx, T[idx].name); }
        juce::PopupMenu m; for (auto& g : order) m.addSubMenu (g, groups[g]);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&dest), [this] (int r) {
            if (r <= 0) return;
            auto es = proc.getEngineState();
            es.slots[slot].dest = (int16_t) (r - 1);
            if (es.slots[slot].src == SrcNone) es.slots[slot].src = SrcLfo1;
            proc.setEngineState (es); sync();
        });
    }
    void paint (juce::Graphics& g) override
    {
        const auto& t = ThemeManager::current();
        g.setColour ((slot & 1) ? t.panel.withMultipliedAlpha (0.6f) : t.panel);
        g.fillRect (getLocalBounds());
        g.setColour (t.textDim); g.setFont (Fonts::mono (10.f));
        g.drawText (juce::String (slot + 1).paddedLeft ('0', 2), 4, 0, 24, getHeight(), juce::Justification::centredLeft);
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (2, 2);
        r.removeFromLeft (26);
        src.setBounds (r.removeFromLeft (130)); r.removeFromLeft (4);
        dest.setBounds (r.removeFromLeft (260)); r.removeFromLeft (4);
        amount.setBounds (r.removeFromLeft (260)); r.removeFromLeft (4);
        curve.setBounds (r.removeFromLeft (120)); r.removeFromLeft (4);
        aux.setBounds (r.removeFromLeft (130)); r.removeFromLeft (4);
        remove.setBounds (r.removeFromLeft (30));
    }
    PureDevotionProcessor& proc;
    int slot;
    juce::ComboBox src, aux, curve;
    juce::TextButton dest, remove { "x" };
    juce::Slider amount;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
};

// ------------------------------------------------------------ page
ModPage::ModPage (PureDevotionProcessor& p) : proc (p)
{
    const char* names[] = { "LFO 1", "LFO 2", "LFO 3", "LFO 4", "LFO 5", "LFO 6", "LFO 7", "LFO 8", "ENV 1", "ENV 2", "ENV 3", "ENV 4", "SEQ 1", "SEQ 2" };
    for (int i = 0; i < 14; ++i)
    {
        auto* b = sourceTabs.add (new juce::TextButton (names[i]));
        b->onClick = [this, i] { selectSource (i); };
        addAndMakeVisible (b);
    }
    shape = std::make_unique<ShapeEditor> (p);
    addAndMakeVisible (*shape);
    for (int s = 1; s < kNumModSources; ++s) { chips.push_back (std::make_unique<ModSourceChip> (p, s)); addAndMakeVisible (*chips.back()); }
    addAndMakeVisible (matrixView);
    matrixView.setViewedComponent (&matrixContent, false);
    for (int s = 0; s < kNumModSlots; ++s) { matrixRows.push_back (std::make_unique<MatrixRow> (p, s)); matrixContent.addAndMakeVisible (*matrixRows.back()); }
    selectSource (0);
    startTimerHz (10);
}

ModPage::~ModPage() { stopTimer(); }

void ModPage::selectSource (int s)
{
    selected = s;
    for (int i = 0; i < sourceTabs.size(); ++i) sourceTabs[i]->setToggleState (i == s, juce::dontSendNotification);
    knobs.clear(); extras.clear();
    shape->sel = s;
    auto addKnob = [this] (const juce::String& id) { knobs.push_back (std::make_unique<ParamKnob> (proc, id)); addAndMakeVisible (*knobs.back()); };
    auto addExtra = [this] (juce::Component* c) { extras.emplace_back (c); addAndMakeVisible (c); };
    if (s < 8)
    {
        const juce::String px = "l" + juce::String (s + 1) + "_";
        for (auto id : { "rate", "phase", "smooth", "delay" }) addKnob (px + id);
        addExtra (new ParamCombo (proc, px + "shape")); addExtra (new ParamCombo (proc, px + "mode")); addExtra (new ParamCombo (proc, px + "div"));
        addExtra (new ParamToggle (proc, px + "sync", "Tempo Sync")); addExtra (new ParamToggle (proc, px + "poly", "Per Voice"));
    }
    else if (s < 12)
    {
        const juce::String px = "e" + juce::String (s - 7) + "_";
        for (auto id : { "a", "h", "d", "s", "r", "curve" }) addKnob (px + id);
        addExtra (new ParamCombo (proc, px + "mode"));
    }
    else
    {
        const juce::String px = "s" + juce::String (s - 11) + "_";
        for (auto id : { "len", "glide" }) addKnob (px + id);
        addExtra (new ParamCombo (proc, px + "div"));
    }
    resized();
    repaint();
}

void ModPage::timerCallback()
{
    shape->repaint();
    for (auto& r : matrixRows) r->sync();
}

void ModPage::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    Panel::paintPanel (g, r.removeFromTop (260), sourceTabs[selected]->getButtonText());
    r.removeFromTop (6);
    Panel::paintPanel (g, r.removeFromTop (88), "Sources - drag onto any knob");
    r.removeFromTop (6);
    Panel::paintPanel (g, r, "Mod Matrix (64 slots)  " + juce::String::fromUTF8 ("\xc2\xb7") + "  source / destination / amount / curve / aux");
}

void ModPage::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (260).reduced (10, 6);
    auto tabs = top.removeFromTop (24);
    tabs.removeFromLeft (110);
    for (auto* b : sourceTabs) { b->setBounds (tabs.removeFromLeft (64).reduced (1, 0)); }
    top.removeFromTop (6);
    auto ctrl = top.removeFromLeft (520);
    auto kr = ctrl.removeFromTop (84);
    for (auto& k : knobs) { k->setBounds (kr.removeFromLeft (80)); }
    ctrl.removeFromTop (4);
    for (size_t i = 0; i < extras.size(); ++i)
    {
        auto cell = juce::Rectangle<int> (ctrl.getX() + (int) (i / 3) * 236, ctrl.getY() + (int) (i % 3) * 28, 226, 28);
        extras[i]->setBounds (cell.reduced (0, 2));
    }
    top.removeFromLeft (10);
    shape->setBounds (top);
    r.removeFromTop (6);
    auto chipArea = r.removeFromTop (88).reduced (10, 6);
    chipArea.removeFromTop (20);
    const int perRow = (int) (chips.size() + 1) / 2;
    const int cw = chipArea.getWidth() / perRow;
    for (size_t i = 0; i < chips.size(); ++i)
        chips[i]->setBounds (chipArea.getX() + (int) (i % (size_t) perRow) * cw, chipArea.getY() + (int) (i / (size_t) perRow) * 30, cw - 3, 27);
    r.removeFromTop (6);
    auto mr = r.reduced (10, 6); mr.removeFromTop (22);
    matrixView.setBounds (mr);
    const int rh = 26;
    matrixContent.setSize (mr.getWidth() - matrixView.getScrollBarThickness(), rh * kNumModSlots);
    for (int s = 0; s < kNumModSlots; ++s) matrixRows[(size_t) s]->setBounds (0, s * rh, matrixContent.getWidth(), rh);
}
} // namespace pd
