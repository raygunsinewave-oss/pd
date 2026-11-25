#include "Pages.h"
#include "../PluginProcessor.h"

namespace pd
{
class PerformPage::XYPad : public juce::Component
{
public:
    explicit XYPad (PureDevotionProcessor& p) : proc (p) {}
    void paint (juce::Graphics& g) override
    {
        const auto& t = ThemeManager::current();
        auto r = getLocalBounds().toFloat().reduced (2);
        g.setColour (juce::Colours::black.withAlpha (t.lightPanels ? 0.08f : 0.4f));
        g.fillRoundedRectangle (r, 8.f);
        g.setColour (t.textDim.withAlpha (0.25f));
        for (int k = 1; k < 4; ++k)
        {
            g.drawVerticalLine ((int) (r.getX() + r.getWidth() * (float) k / 4.f), r.getY(), r.getBottom());
            g.drawHorizontalLine ((int) (r.getY() + r.getHeight() * (float) k / 4.f), r.getX(), r.getRight());
        }
        const char* names[] = { "A", "B", "C", "D" };
        const juce::Point<float> corners[] = { r.getTopLeft(), r.getTopRight(), r.getBottomLeft(), r.getBottomRight() };
        for (int s = 0; s < 4; ++s)
        {
            auto c = juce::Rectangle<float> (26, 26).withCentre (corners[s] + juce::Point<float> (s % 2 ? -18.f : 18.f, s < 2 ? 18.f : -18.f));
            g.setColour (proc.hasScene (s) ? t.accent : t.panel);
            g.fillEllipse (c);
            g.setColour (t.text); g.setFont (Fonts::header (14.f));
            g.drawText (names[s], c, juce::Justification::centred);
        }
        const float x = proc.getParamPlain (IX().xyX), y = proc.getParamPlain (IX().xyY);
        const auto dot = juce::Point<float> (r.getX() + x * r.getWidth(), r.getY() + y * r.getHeight());
        juce::Path cross; cross.startNewSubPath (dot.x, r.getY()); cross.lineTo (dot.x, r.getBottom()); cross.startNewSubPath (r.getX(), dot.y); cross.lineTo (r.getRight(), dot.y);
        g.setColour (t.accent.withAlpha (0.35f)); g.strokePath (cross, juce::PathStrokeType (1.f));
        g.setColour (t.accent.withAlpha (0.2f)); g.fillEllipse (dot.x - 18, dot.y - 18, 36, 36);
        g.setColour (t.accent); g.fillEllipse (dot.x - 8, dot.y - 8, 16, 16);
        g.setColour (juce::Colours::white); g.drawEllipse (dot.x - 8, dot.y - 8, 16, 16, 1.5f);
    }
    void mouseDown (const juce::MouseEvent& e) override { set (e.position); }
    void mouseDrag (const juce::MouseEvent& e) override { set (e.position); }
    void mouseDoubleClick (const juce::MouseEvent&) override { set (getLocalBounds().toFloat().getCentre()); }
    void set (juce::Point<float> p)
    {
        auto r = getLocalBounds().toFloat().reduced (2);
        proc.setParamPlain (IX().xyX, clamp01 ((p.x - r.getX()) / r.getWidth()));
        proc.setParamPlain (IX().xyY, clamp01 ((p.y - r.getY()) / r.getHeight()));
        repaint();
    }
    PureDevotionProcessor& proc;
};

class PerformPage::ChopPad : public juce::Component, private juce::Timer
{
public:
    ChopPad (PureDevotionProcessor& p, std::function<void (int)> onSelect) : proc (p), select (std::move (onSelect)) { startTimerHz (30); }
    void timerCallback() override
    {
        const int s = proc.chopPlayer().lastSlot.exchange (-1);
        if (s >= 0) { flash[(size_t) (s & 15)] = 1.f; }
        bool any = false;
        for (auto& f : flash) { if (f > 0.01f) any = true; f *= 0.85f; }
        if (any) repaint();
    }
    void paint (juce::Graphics& g) override
    {
        const auto& t = ThemeManager::current();
        auto r = getLocalBounds().toFloat();
        const float w = r.getWidth() / 4.f, h = r.getHeight() / 4.f;
        for (int k = 0; k < kNumChopSlots; ++k)
        {
            const int row = 3 - k / 4, col = k % 4;   // MPC-style: slot 1 bottom-left
            auto pad = juce::Rectangle<float> (r.getX() + (float) col * w, r.getY() + (float) row * h, w, h).reduced (3.f);
            g.setColour (t.panel.withMultipliedAlpha (1.5f).interpolatedWith (t.accent, flash[(size_t) k]));
            g.fillRoundedRectangle (pad, 6.f);
            g.setColour (k == selected ? t.accent : t.panelEdge.withMultipliedAlpha (1.5f));
            g.drawRoundedRectangle (pad, 6.f, k == selected ? 2.f : 1.f);
            g.setColour (t.text); g.setFont (Fonts::header (14.f));
            g.drawText (juce::String (k + 1), pad.reduced (6, 4), juce::Justification::topLeft);
            const auto& es = proc.getEngineState();
            g.setColour (t.textDim); g.setFont (Fonts::mono (9.f));
            g.drawText (juce::String (es.chopPitch[k], 1) + "st / F" + juce::String (es.chopFormant[k], 1), pad.reduced (6, 4), juce::Justification::bottomLeft);
        }
    }
    int slotAt (juce::Point<float> p) const
    {
        const int col = juce::jlimit (0, 3, (int) (p.x / ((float) getWidth() / 4.f)));
        const int row = juce::jlimit (0, 3, (int) (p.y / ((float) getHeight() / 4.f)));
        return (3 - row) * 4 + col;
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int s = slotAt (e.position);
        selected = s;
        if (select) select (s);
        if (! e.mods.isPopupMenu()) proc.triggerChopPad (s, juce::jlimit (0.3f, 1.f, 1.f - (e.position.y - (float) ((3 - s / 4) * getHeight() / 4)) / ((float) getHeight() / 4.f) * 0.5f));
        repaint();
    }
    int selected = 0;
private:
    PureDevotionProcessor& proc;
    std::function<void (int)> select;
    std::array<float, kNumChopSlots> flash {};
};

PerformPage::PerformPage (PureDevotionProcessor& p) : proc (p)
{
    for (auto* pn : { &macroPanel, &scenePanel, &riserPanel, &arpPanel, &chopPanel, &fxPanel }) addAndMakeVisible (pn);
    for (int m = 0; m < kNumMacros; ++m)
    {
        macros.push_back (std::make_unique<ParamKnob> (p, "macro" + juce::String (m + 1), " ", true));
        addAndMakeVisible (*macros.back());
        auto* l = macroNames.add (new juce::Label());
        l->setText (proc.macroNames[m], juce::dontSendNotification);
        l->setEditable (false, true);
        l->setJustificationType (juce::Justification::centred);
        l->setFont (Fonts::header (14.f));
        l->onTextChange = [this, m, l] { proc.macroNames.set (m, l->getText()); };
        addAndMakeVisible (l);
    }
    xy = std::make_unique<XYPad> (p);
    addAndMakeVisible (*xy);
    const char* sn[] = { "Set A", "Set B", "Set C", "Set D" };
    for (int s = 0; s < 4; ++s)
    {
        auto* b = sceneButtons.add (new juce::TextButton (sn[s]));
        b->setTooltip ("Capture the current sound into scene corner " + juce::String::charToString ((juce::juce_wchar) ('A' + s)));
        b->onClick = [this, s] { proc.captureScene (s); xy->repaint(); };
        addAndMakeVisible (b);
    }
    morphToggle = std::make_unique<ParamToggle> (p, "morph_on", "Morph");
    dropKill = std::make_unique<ParamToggle> (p, "drop_kill", "Drop kills tails");
    arpOn = std::make_unique<ParamToggle> (p, "arp_on", "Arp");
    arpLatch = std::make_unique<ParamToggle> (p, "arp_latch", "Latch");
    chordOn = std::make_unique<ParamToggle> (p, "chord_on", "Chord");
    for (auto* c : std::initializer_list<juce::Component*> { morphToggle.get(), dropKill.get(), arpOn.get(), arpLatch.get(), chordOn.get() }) addAndMakeVisible (c);

    riserArm = std::make_unique<juce::TextButton> ("ARM BUILD");
    riserArm->setClickingTogglesState (true);
    riserArm->onClick = [this] { proc.setParamPlain (IX().riserArm, riserArm->getToggleState() ? 1.f : 0.f); };
    addAndMakeVisible (*riserArm);
    stutter = std::make_unique<MomentaryButton> (p, "stutter", "Stutter");
    tapeStop = std::make_unique<MomentaryButton> (p, "tapestop", "Tape Stop");
    addAndMakeVisible (*stutter); addAndMakeVisible (*tapeStop);

    riserBars = std::make_unique<ParamCombo> (p, "riser_bars");
    stutterDiv = std::make_unique<ParamCombo> (p, "stutter_div");
    arpMode = std::make_unique<ParamCombo> (p, "arp_mode");
    arpDiv = std::make_unique<ParamCombo> (p, "arp_div");
    arpGroove = std::make_unique<ParamCombo> (p, "arp_groove");
    chordType = std::make_unique<ParamCombo> (p, "chord_type");
    for (auto* c : std::initializer_list<juce::Component*> { riserBars.get(), stutterDiv.get(), arpMode.get(), arpDiv.get(), arpGroove.get(), chordType.get() }) addAndMakeVisible (c);
    chopSample = std::make_unique<ParamCombo> (p, "chop_smp");
    addAndMakeVisible (*chopSample);

    for (auto id : { "riser_filter", "riser_noise", "riser_verb", "riser_pitch" }) { riserKnobs.push_back (std::make_unique<ParamKnob> (p, id)); addAndMakeVisible (*riserKnobs.back()); }
    riserKnobs[0]->setLabel ("Filter"); riserKnobs[1]->setLabel ("Noise"); riserKnobs[2]->setLabel ("Verb"); riserKnobs[3]->setLabel ("Pitch");
    for (auto id : { "arp_oct", "arp_gate", "arp_ratchet", "arp_prob", "arp_swing", "strum" }) { arpKnobs.push_back (std::make_unique<ParamKnob> (p, id)); addAndMakeVisible (*arpKnobs.back()); }
    const char* al[] = { "Octaves", "Gate", "Ratchet", "Prob", "Swing", "Strum" };
    for (int k = 0; k < 6; ++k) arpKnobs[(size_t) k]->setLabel (al[k]);
    for (auto id : { "chop_lvl", "tapestop_time" }) { chopKnobs.push_back (std::make_unique<ParamKnob> (p, id)); addAndMakeVisible (*chopKnobs.back()); }
    chopKnobs[0]->setLabel ("Chop Level"); chopKnobs[1]->setLabel ("Stop Time");

    chop = std::make_unique<ChopPad> (p, [this] (int s) {
        selectedSlot = s;
        const auto& es = proc.getEngineState();
        slotPitch.setValue (es.chopPitch[s], juce::dontSendNotification);
        slotFormant.setValue (es.chopFormant[s], juce::dontSendNotification);
        slotLabel.setText ("Slot " + juce::String (s + 1), juce::dontSendNotification);
    });
    addAndMakeVisible (*chop);
    for (auto* s : { &slotPitch, &slotFormant })
    {
        s->setSliderStyle (juce::Slider::LinearBar);
        s->setRange (-24.0, 24.0, 0.1);
        addAndMakeVisible (s);
    }
    slotPitch.setTextValueSuffix (" st pitch"); slotFormant.setTextValueSuffix (" st formant");
    slotPitch.onValueChange = [this] { auto es = proc.getEngineState(); es.chopPitch[selectedSlot] = (float) slotPitch.getValue(); proc.setEngineState (es); chop->repaint(); };
    slotFormant.onValueChange = [this] { auto es = proc.getEngineState(); es.chopFormant[selectedSlot] = (float) slotFormant.getValue(); proc.setEngineState (es); chop->repaint(); };
    slotLabel.setText ("Slot 1", juce::dontSendNotification);
    slotLabel.setFont (Fonts::header (13.f));
    addAndMakeVisible (slotLabel);

    learnChord.onClick = [this] {
        int notes[16]; const int n = proc.arpChord().heldNotes (notes, 16);
        if (n == 0) { learnChord.setButtonText ("Hold keys first"); return; }
        std::sort (notes, notes + n);
        auto es = proc.getEngineState();
        es.numChordNotes = juce::jmin (8, n);
        for (int i = 0; i < es.numChordNotes; ++i) es.chordMemory[i] = (int8_t) (notes[i] - notes[0]);
        proc.setEngineState (es);
        proc.setParamPlain (IX().chordType, 0.f);
        proc.setParamPlain (IX().chordOn, 1.f);
        learnChord.setButtonText ("Learned " + juce::String (n) + " notes");
    };
    fullVis.setClickingTogglesState (true);
    fullVis.onClick = [this] { if (onFullScreenVisualizer) onFullScreenVisualizer (fullVis.getToggleState()); };
    lockList.onClick = [this] {
        juce::PopupMenu m;
        m.addSectionHeader ("Performance lock: these ignore preset changes");
        int count = 0;
        for (int i = 0; i < ParamTable::get().size(); ++i)
            if (proc.isLocked (i)) { m.addItem (1 + i, ParamTable::get()[i].name + "  (click to unlock)"); ++count; }
        if (count == 0) m.addItem (-1, "Right-click any knob and choose 'Performance lock'", false);
        m.addSeparator();
        m.addItem (100000, "Lock all macros");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&lockList), [this] (int r) {
            if (r == 100000) { for (int k = 0; k < kNumMacros; ++k) proc.setLocked (IX().macro[k], true); }
            else if (r > 0) proc.setLocked (r - 1, false);
            repaint();
        });
    };
    for (auto* b : { &learnChord, &fullVis, &lockList }) addAndMakeVisible (b);
    timerCallback();
    startTimerHz (15);
}

PerformPage::~PerformPage() { stopTimer(); }

void PerformPage::timerCallback()
{
    const bool armed = proc.getParamPlain (IX().riserArm) > 0.5f;
    if (riserArm->getToggleState() != armed) riserArm->setToggleState (armed, juce::dontSendNotification);
    riserArm->setButtonText (armed ? "DROP" : "ARM BUILD");
    for (int m = 0; m < kNumMacros; ++m)
        if (! macroNames[m]->isBeingEdited() && macroNames[m]->getText() != proc.macroNames[m]) macroNames[m]->setText (proc.macroNames[m], juce::dontSendNotification);
    xy->repaint();
    repaint (riserPanel.getBounds());
    // chop-pad sample names (factory samples are synthesised in the background)
    if (chopNamesFilled < proc.samples().numSamples())
    {
        chopNamesFilled = proc.samples().numSamples();
        for (int i = 0; i < chopSample->getNumItems() && i < chopNamesFilled; ++i) chopSample->changeItemText (i + 1, proc.samples().name (i));
        chopSample->setText (proc.samples().name ((int) proc.getParamPlain (IX().chopSample)), juce::dontSendNotification);
    }
}

void PerformPage::paint (juce::Graphics& g)
{
    const auto& t = ThemeManager::current();
    // riser progress bar
    auto bar = riserPanel.getBounds().toFloat().reduced (12).removeFromBottom (10);
    g.setColour (juce::Colours::black.withAlpha (0.35f)); g.fillRoundedRectangle (bar, 4.f);
    g.setColour (t.accent); g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * proc.feed().riser.load()), 4.f);
}

void PerformPage::resized()
{
    auto r = getLocalBounds();
    auto mp = r.removeFromTop (170);
    macroPanel.setBounds (mp);
    auto mr = mp.reduced (14, 8); mr.removeFromTop (18);
    const int mw = mr.getWidth() / kNumMacros;
    for (int m = 0; m < kNumMacros; ++m)
    {
        auto cell = mr.removeFromLeft (mw).reduced (6, 0);
        macroNames[m]->setBounds (cell.removeFromBottom (22));
        macros[(size_t) m]->setBounds (cell);
    }
    r.removeFromTop (8);
    auto c1 = r.removeFromLeft (320); r.removeFromLeft (8);
    auto c2 = r.removeFromLeft (310); r.removeFromLeft (8);
    auto c3 = r.removeFromLeft (310); r.removeFromLeft (8);
    auto c4 = r;

    scenePanel.setBounds (c1);
    auto s = c1.reduced (12, 8); s.removeFromTop (20);
    auto sb = s.removeFromBottom (56);
    morphToggle->setBounds (sb.removeFromTop (26).removeFromLeft (120));
    layoutRow (sb, { sceneButtons[0], sceneButtons[1], sceneButtons[2], sceneButtons[3] }, 3);
    xy->setBounds (s.withSizeKeepingCentre (juce::jmin (s.getWidth(), s.getHeight() - 6), juce::jmin (s.getWidth(), s.getHeight() - 6)));

    auto rp = c2.removeFromTop (250);
    riserPanel.setBounds (rp);
    auto rr = rp.reduced (12, 8); rr.removeFromTop (22);
    rr.removeFromBottom (16);
    auto armRow = rr.removeFromTop (46);
    riserArm->setBounds (armRow.removeFromLeft (150).reduced (0, 4));
    riserBars->setBounds (armRow.removeFromLeft (120).reduced (8, 10));
    dropKill->setBounds (rr.removeFromTop (24));
    layoutRowVec (rr, riserKnobs, 4);
    c2.removeFromTop (8);
    fxPanel.setBounds (c2);
    auto fr = c2.reduced (12, 8); fr.removeFromTop (22);
    auto b1 = fr.removeFromTop (44);
    stutter->setBounds (b1.removeFromLeft (110).reduced (0, 4));
    tapeStop->setBounds (b1.removeFromLeft (110).reduced (6, 4));
    stutterDiv->setBounds (b1.reduced (4, 10));
    auto b2 = fr.removeFromTop (80);
    chopKnobs[1]->setBounds (b2.removeFromLeft (80));
    auto bb = b2.reduced (4, 6);
    fullVis.setBounds (bb.removeFromTop (30)); bb.removeFromTop (4);
    lockList.setBounds (bb.removeFromTop (30));

    arpPanel.setBounds (c3);
    auto ar = c3.reduced (12, 8); ar.removeFromTop (22);
    auto t1 = ar.removeFromTop (26);
    arpOn->setBounds (t1.removeFromLeft (70)); arpLatch->setBounds (t1.removeFromLeft (80)); chordOn->setBounds (t1.removeFromLeft (80));
    ar.removeFromTop (4);
    arpMode->setBounds (ar.removeFromTop (26).reduced (0, 2));
    arpDiv->setBounds (ar.removeFromTop (26).reduced (0, 2));
    arpGroove->setBounds (ar.removeFromTop (26).reduced (0, 2));
    chordType->setBounds (ar.removeFromTop (26).reduced (0, 2));
    learnChord.setBounds (ar.removeFromTop (30).reduced (0, 2));
    ar.removeFromTop (6);
    auto k1 = ar.removeFromTop (ar.getHeight() / 2);
    layoutRow (k1, { arpKnobs[0].get(), arpKnobs[1].get(), arpKnobs[2].get() }, 2);
    layoutRow (ar, { arpKnobs[3].get(), arpKnobs[4].get(), arpKnobs[5].get() }, 2);

    chopPanel.setBounds (c4);
    auto cr = c4.reduced (12, 8); cr.removeFromTop (22);
    auto ctop = cr.removeFromTop (26);
    chopSample->setBounds (ctop.reduced (0, 2));
    auto cb = cr.removeFromBottom (92);
    auto kn = cb.removeFromRight (70);
    chopKnobs[0]->setBounds (kn);
    slotLabel.setBounds (cb.removeFromTop (22));
    slotPitch.setBounds (cb.removeFromTop (30).reduced (0, 3));
    slotFormant.setBounds (cb.removeFromTop (30).reduced (0, 3));
    chop->setBounds (cr.reduced (0, 4));

}
} // namespace pd
