#include "Pages.h"
#include "../PluginProcessor.h"

namespace pd
{
namespace
{
constexpr float kModW = 150.f, kHeader = 20.f, kRow = 20.f, kKnobRow = 46.f, kOptRow = 22.f;

bool hasOptionRow (const PatchModuleDesc& d)
{
    return patchModuleInfo (d.type).options != nullptr || d.type == PmParamOut || d.type == PmParamIn;
}
float moduleHeight (const PatchModuleDesc& d)
{
    const auto& info = patchModuleInfo (d.type);
    return kHeader + (float) juce::jmax (1, juce::jmax (info.numIn, info.numOut)) * kRow + 4.f
         + (info.numKnobs > 0 ? kKnobRow : 0.f) + (hasOptionRow (d) ? kOptRow : 0.f);
}
juce::Rectangle<float> moduleBounds (const PatchModuleDesc& d) { return { d.x, d.y, kModW, moduleHeight (d) }; }
juce::Point<float> inJack (const PatchModuleDesc& d, int k) { return { d.x, d.y + kHeader + 10.f + (float) k * kRow }; }
juce::Point<float> outJack (const PatchModuleDesc& d, int k) { return { d.x + kModW, d.y + kHeader + 10.f + (float) k * kRow }; }
juce::Rectangle<float> knobRect (const PatchModuleDesc& d, int k)
{
    const auto& info = patchModuleInfo (d.type);
    const float top = d.y + kHeader + (float) juce::jmax (1, juce::jmax (info.numIn, info.numOut)) * kRow + 4.f;
    const float w = kModW / (float) juce::jmax (1, info.numKnobs);
    return { d.x + (float) k * w, top, w, kKnobRow };
}
juce::Rectangle<float> optionRect (const PatchModuleDesc& d)
{
    const auto b = moduleBounds (d);
    return { b.getX() + 6, b.getBottom() - kOptRow, b.getWidth() - 12, kOptRow - 4 };
}
juce::String optionText (const PatchModuleDesc& d)
{
    if (d.type == PmParamOut || d.type == PmParamIn)
        return d.paramDest >= 0 ? ParamTable::get()[d.paramDest].name : juce::String ("<choose parameter>");
    juce::StringArray o; o.addTokens (patchModuleInfo (d.type).options, "|", {});
    return o[juce::jlimit (0, o.size() - 1, (int) d.option)];
}
} // namespace

class PatchPage::Canvas : public juce::Component, public juce::DragAndDropTarget
{
public:
    explicit Canvas (PureDevotionProcessor& p) : proc (p) { prog = proc.getPatch(); }

    void sync() { prog = proc.getPatch(); repaint(); }
    void commit() { proc.setPatch (prog); }

    void paint (juce::Graphics& g) override
    {
        const auto& t = ThemeManager::current();
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colours::black.withAlpha (t.lightPanels ? 0.05f : 0.3f));
        g.fillRoundedRectangle (r, 8.f);
        g.setColour (t.textDim.withAlpha (0.12f));
        for (float x = 0; x < r.getWidth(); x += 24) for (float y = 0; y < r.getHeight(); y += 24) g.fillRect (x, y, 1.5f, 1.5f);
        if (prog.numUsed() == 0)
        {
            g.setColour (t.textDim);
            g.setFont (Fonts::header (18.f));
            g.drawFittedText ("DRAG MODULES IN FROM THE LEFT. DRAG FROM AN OUTPUT JACK TO AN INPUT JACK TO PATCH.\n"
                              "PARAM OUT JACKS DRIVE ANY MAIN SYNTH PARAMETER. RIGHT-CLICK A MODULE FOR OPTIONS.",
                              getLocalBounds().reduced (40), juce::Justification::centred, 3);
        }
        // modules
        for (int i = 0; i < kMaxPatchModules; ++i)
        {
            const auto& d = prog.m[i];
            if (d.type < 0) continue;
            const auto& info = patchModuleInfo (d.type);
            const auto b = moduleBounds (d);
            g.setColour (t.panel.withMultipliedAlpha (1.4f));
            g.fillRoundedRectangle (b, 6.f);
            g.setColour (i == selected ? t.accent : t.panelEdge.withMultipliedAlpha (1.6f));
            g.drawRoundedRectangle (b, 6.f, i == selected ? 1.6f : 1.f);
            g.setColour (t.accent.withAlpha (0.85f));
            g.fillRoundedRectangle (b.withHeight (kHeader), 6.f);
            g.setColour (juce::Colours::white);
            drawTracked (g, juce::String (info.name).toUpperCase(), b.withHeight (kHeader).reduced (8, 0), Fonts::header (12.f), 0.1f, juce::Justification::centredLeft);
            g.setFont (Fonts::mono (10.f));
            for (int k = 0; k < info.numIn; ++k)
            {
                const auto j = inJack (d, k);
                drawJack (g, j, false, t);
                g.setColour (t.text); g.drawText (info.inNames[k], (int) j.x + 9, (int) j.y - 7, 60, 14, juce::Justification::centredLeft);
            }
            for (int k = 0; k < info.numOut; ++k)
            {
                const auto j = outJack (d, k);
                drawJack (g, j, true, t);
                g.setColour (t.text); g.drawText (info.outNames[k], (int) j.x - 69, (int) j.y - 7, 60, 14, juce::Justification::centredRight);
            }
            for (int k = 0; k < info.numKnobs; ++k)
            {
                const auto kr = knobRect (d, k);
                auto kk = kr.withTrimmedBottom (12).withSizeKeepingCentre (26, 26);
                const float a = juce::MathConstants<float>::pi * (1.2f + 1.6f * d.knob[k]);
                juce::Path arc; arc.addCentredArc (kk.getCentreX(), kk.getCentreY(), 14, 14, 0, juce::MathConstants<float>::pi * 1.2f, a, true);
                drawHalationArc (g, arc, t.accent, 2.f);
                PDLookAndFeel::drawChromeKnob (g, kk.reduced (2), a, t, false);
                g.setColour (t.textDim);
                g.setFont (Fonts::header (9.f));
                g.drawText (juce::String (info.knobNames[k]).toUpperCase(), kr.withTop (kr.getBottom() - 12).toNearestInt(), juce::Justification::centred);
            }
            if (hasOptionRow (d))
            {
                const auto orr = optionRect (d);
                g.setColour (juce::Colours::black.withAlpha (0.25f)); g.fillRoundedRectangle (orr, 3.f);
                g.setColour (t.text); g.setFont (Fonts::mono (9.5f));
                g.drawFittedText (optionText (d), orr.reduced (4, 0).toNearestInt(), juce::Justification::centredLeft, 1);
            }
        }
        // cables on top: sag + glow by signal level
        for (int i = 0; i < kMaxPatchModules; ++i)
        {
            const auto& d = prog.m[i];
            if (d.type < 0) continue;
            for (int k = 0; k < patchModuleInfo (d.type).numIn; ++k)
            {
                if (d.inMod[k] < 0) continue;
                const auto& s = prog.m[d.inMod[k]];
                const float level = proc.patchEngine().meters[d.inMod[k]][d.inPort[k]].load();
                drawCable (g, outJack (s, d.inPort[k]), inJack (d, k), level, t);
            }
        }
        if (dragFrom >= 0) drawCable (g, outJack (prog.m[dragFrom], dragFromPort), dragPos, 0.5f, t);
    }

    static void drawJack (juce::Graphics& g, juce::Point<float> p, bool out, const Theme& t)
    {
        g.setColour (juce::Colours::black.withAlpha (0.6f)); g.fillEllipse (p.x - 6, p.y - 6, 12, 12);
        g.setColour (out ? t.accent : t.chrome); g.drawEllipse (p.x - 6, p.y - 6, 12, 12, 2.f);
        g.setColour (t.chrome.darker (0.5f)); g.fillEllipse (p.x - 2.5f, p.y - 2.5f, 5, 5);
    }
    static void drawCable (juce::Graphics& g, juce::Point<float> a, juce::Point<float> b, float level, const Theme& t)
    {
        const float dist = a.getDistanceFrom (b);
        const float sag = 30.f + dist * 0.25f;
        juce::Path p;
        p.startNewSubPath (a);
        p.cubicTo (a.x + dist * 0.25f, a.y + sag, b.x - dist * 0.25f, b.y + sag, b.x, b.y);
        const float br = juce::jlimit (0.f, 1.f, level);
        g.setColour (t.accent.withAlpha (0.08f + 0.25f * br));
        g.strokePath (p, juce::PathStrokeType (9.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (t.accent.withMultipliedBrightness (0.55f + 0.6f * br).withAlpha (0.95f));
        g.strokePath (p, juce::PathStrokeType (3.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (juce::Colours::white.withAlpha (0.15f + 0.5f * br));
        g.strokePath (p, juce::PathStrokeType (1.f));
    }

    // hit tests
    int moduleAt (juce::Point<float> p) const
    {
        for (int i = kMaxPatchModules - 1; i >= 0; --i) if (prog.m[i].type >= 0 && moduleBounds (prog.m[i]).expanded (8, 0).contains (p)) return i;
        return -1;
    }
    bool jackAt (juce::Point<float> p, int& mod, int& port, bool& isOut) const
    {
        for (int i = 0; i < kMaxPatchModules; ++i)
        {
            const auto& d = prog.m[i];
            if (d.type < 0) continue;
            const auto& info = patchModuleInfo (d.type);
            for (int k = 0; k < info.numOut; ++k) if (outJack (d, k).getDistanceFrom (p) < 9.f) { mod = i; port = k; isOut = true; return true; }
            for (int k = 0; k < info.numIn; ++k) if (inJack (d, k).getDistanceFrom (p) < 9.f) { mod = i; port = k; isOut = false; return true; }
        }
        return false;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto p = e.position;
        int m, port; bool isOut;
        if (jackAt (p, m, port, isOut))
        {
            if (isOut) { dragFrom = m; dragFromPort = port; dragPos = p; }
            else if (prog.m[m].inMod[port] >= 0)   // pull an existing cable off its input
            {
                dragFrom = prog.m[m].inMod[port]; dragFromPort = prog.m[m].inPort[port]; dragPos = p;
                prog.m[m].inMod[port] = -1; commit();
            }
            return;
        }
        const int mod = moduleAt (p);
        selected = mod;
        if (mod < 0) { repaint(); return; }
        auto& d = prog.m[mod];
        if (e.mods.isPopupMenu()) { showModuleMenu (mod); return; }
        if (hasOptionRow (d) && optionRect (d).contains (p)) { showOptionMenu (mod); return; }
        for (int k = 0; k < patchModuleInfo (d.type).numKnobs; ++k)
            if (knobRect (d, k).contains (p))
            {
                if (e.getNumberOfClicks() > 1) { d.knob[k] = patchModuleInfo (d.type).knobDefaults[k]; commit(); }
                knobDrag = k; knobStart = d.knob[k];
                return;
            }
        moveStart = { d.x, d.y };
        moving = true;
        repaint();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragFrom >= 0) { dragPos = e.position; repaint(); return; }
        if (selected < 0) return;
        auto& d = prog.m[selected];
        if (knobDrag >= 0)
        {
            d.knob[knobDrag] = clamp01 (knobStart - (float) e.getDistanceFromDragStartY() / 150.f);
            commit(); repaint(); return;
        }
        if (moving)
        {
            d.x = juce::jlimit (0.f, (float) getWidth() - kModW, moveStart.x + (float) e.getDistanceFromDragStartX());
            d.y = juce::jlimit (0.f, (float) getHeight() - 40.f, moveStart.y + (float) e.getDistanceFromDragStartY());
            repaint();
        }
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (dragFrom >= 0)
        {
            int m, port; bool isOut;
            if (jackAt (e.position, m, port, isOut) && ! isOut && m != dragFrom)
            {
                prog.m[m].inMod[port] = (int8_t) dragFrom; prog.m[m].inPort[port] = (int8_t) dragFromPort;
            }
            dragFrom = -1;
            commit(); repaint();
        }
        if (moving) commit();
        moving = false; knobDrag = -1;
    }

    void showModuleMenu (int mod)
    {
        juce::PopupMenu m;
        m.addItem (1, "Delete module");
        m.addItem (2, "Disconnect all inputs");
        if (hasOptionRow (prog.m[mod])) m.addItem (3, prog.m[mod].type == PmParamOut || prog.m[mod].type == PmParamIn ? "Choose parameter..." : "Options...");
        m.showMenuAsync (juce::PopupMenu::Options(), [this, mod] (int r) {
            if (r == 1)
            {
                prog.m[mod] = PatchModuleDesc();
                for (auto& d : prog.m) for (auto& in : d.inMod) if (in == mod) in = -1;
                selected = -1;
            }
            else if (r == 2) for (auto& in : prog.m[mod].inMod) in = -1;
            else if (r == 3) { showOptionMenu (mod); return; }
            commit(); repaint();
        });
    }

    void showOptionMenu (int mod)
    {
        auto& d = prog.m[mod];
        juce::PopupMenu m;
        if (d.type == PmParamOut || d.type == PmParamIn)
        {
            // hierarchical parameter list grouped by section
            const auto& T = ParamTable::get();
            std::map<juce::String, juce::PopupMenu> groups;
            juce::StringArray order;
            for (int idx : T.modDestinations())
            {
                const auto& g = T[idx].group;
                if (! order.contains (g)) order.add (g);
                groups[g].addItem (1 + idx, T[idx].name, true, d.paramDest == idx);
            }
            for (auto& g : order) m.addSubMenu (g, groups[g]);
        }
        else
        {
            juce::StringArray o; o.addTokens (patchModuleInfo (d.type).options, "|", {});
            for (int k = 0; k < o.size(); ++k) m.addItem (1 + k, o[k], true, d.option == k);
        }
        m.showMenuAsync (juce::PopupMenu::Options(), [this, mod] (int r) {
            if (r <= 0) return;
            auto& dd = prog.m[mod];
            if (dd.type == PmParamOut || dd.type == PmParamIn) dd.paramDest = (int16_t) (r - 1);
            else dd.option = (int8_t) (r - 1);
            commit(); repaint();
        });
    }

    bool addModule (int type, juce::Point<float> at)
    {
        for (int i = 0; i < kMaxPatchModules; ++i)
            if (prog.m[i].type < 0)
            {
                auto& d = prog.m[i];
                d = PatchModuleDesc();
                d.type = (int8_t) type;
                for (int k = 0; k < kPatchIO; ++k) d.knob[k] = patchModuleInfo (type).knobDefaults[k];
                d.x = juce::jlimit (0.f, (float) getWidth() - kModW, at.x - kModW * 0.5f);
                d.y = juce::jlimit (0.f, (float) getHeight() - 60.f, at.y - 10.f);
                selected = i;
                commit(); repaint();
                return true;
            }
        return false;
    }

    bool isInterestedInDragSource (const SourceDetails& s) override { return s.description.toString().startsWith ("patchmod:"); }
    void itemDropped (const SourceDetails& s) override
    {
        addModule (s.description.toString().fromFirstOccurrenceOf (":", false, false).getIntValue(), s.localPosition.toFloat());
    }
    void clearAll() { prog = PatchProgram(); selected = -1; commit(); repaint(); }

    PureDevotionProcessor& proc;
    PatchProgram prog;
    int selected = -1, dragFrom = -1, dragFromPort = 0, knobDrag = -1;
    float knobStart = 0;
    bool moving = false;
    juce::Point<float> dragPos, moveStart;
};

struct PatchPage::PaletteModel : public juce::ListBoxModel
{
    std::vector<int> order;
    PatchPage::Canvas* canvas = nullptr;
    PaletteModel()
    {
        for (auto* cat : { "Sound", "Modulation", "Clock", "Pitch", "Utility", "Input", "Synth" })
            for (int t = 0; t < kNumPatchModuleTypes; ++t) if (juce::String (patchModuleInfo (t).category) == cat) order.push_back (t);
    }
    int getNumRows() override { return (int) order.size(); }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool sel) override
    {
        const auto& t = ThemeManager::current();
        const auto& info = patchModuleInfo (order[(size_t) row]);
        if (sel) { g.setColour (t.accent.withAlpha (0.3f)); g.fillRect (0, 0, w, h); }
        g.setColour (t.text); g.setFont (Fonts::header (13.f));
        g.drawText (juce::String (info.name).toUpperCase(), 8, 0, w - 60, h, juce::Justification::centredLeft);
        g.setColour (t.textDim); g.setFont (Fonts::mono (9.f));
        g.drawText (info.category, w - 70, 0, 64, h, juce::Justification::centredRight);
    }
    juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override
    {
        return rows.isEmpty() ? juce::var() : juce::var ("patchmod:" + juce::String (order[(size_t) rows[0]]));
    }
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override
    {
        if (canvas) canvas->addModule (order[(size_t) row], { 120.f + (float) (row % 5) * 40.f, 60.f + (float) (row % 7) * 30.f });
    }
};

PatchPage::PatchPage (PureDevotionProcessor& p) : proc (p)
{
    canvas = std::make_unique<Canvas> (p);
    paletteModel = std::make_unique<PaletteModel>();
    paletteModel->canvas = canvas.get();
    palette.setModel (paletteModel.get());
    palette.setRowHeight (24);
    addAndMakeVisible (palette);
    addAndMakeVisible (*canvas);
    addAndMakeVisible (clearBtn);
    clearBtn.onClick = [this] { canvas->clearAll(); };
    startTimerHz (20);
}

PatchPage::~PatchPage() { stopTimer(); palette.setModel (nullptr); }

void PatchPage::paint (juce::Graphics& g)
{
    Panel::paintPanel (g, palette.getBounds().toFloat().expanded (4, 26).withTrimmedBottom (-4), "Modules");
}

void PatchPage::resized()
{
    auto r = getLocalBounds();
    auto left = r.removeFromLeft (190);
    left.removeFromTop (26);
    clearBtn.setBounds (left.removeFromBottom (28).reduced (6, 2));
    palette.setBounds (left.reduced (4, 0));
    r.removeFromLeft (8);
    canvas->setBounds (r);
    if (isShowing()) canvas->sync();
}
} // namespace pd
