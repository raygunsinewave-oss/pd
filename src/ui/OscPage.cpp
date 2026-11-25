#include "Pages.h"
#include "../PluginProcessor.h"

namespace pd
{
namespace
{
// Waveform/frame display drawn over the dog visualiser. Accepts audio files (drop) to import
// as a wavetable or sample; click-drag draws into the current frame (wavetable editor).
class WaveDisplay : public juce::Component, public juce::FileDragAndDropTarget, public juce::SettableTooltipClient, private juce::Timer
{
public:
    WaveDisplay (PureDevotionProcessor& p, int o) : proc (p), osc (o), vis (p, o)
    {
        addAndMakeVisible (vis);
        startTimerHz (20);
        setTooltip ("Drop a .wav to import it as a wavetable (osc in Wavetable mode) or sample. Shift-drag to draw the current frame. Right-click for scene.");
    }
    DogVisualizer& visualizer() { return vis; }
    void resized() override { vis.setBounds (getLocalBounds().reduced (1)); }
    void timerCallback() override { repaint(); }

    void paint (juce::Graphics& g) override
    {
        const auto& t = ThemeManager::current();
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colours::black.withAlpha (t.lightPanels ? 0.08f : 0.45f));
        g.fillRoundedRectangle (r, 6.f);
    }
    void paintOverChildren (juce::Graphics& g) override
    {
        const auto& t = ThemeManager::current();
        const auto& I = IX().osc[osc];
        auto r = getLocalBounds().toFloat().reduced (6.f);
        const int eng = (int) proc.getParamPlain (I.eng);
        const float pos = proc.getParamPlain (I.pos);
        g.setColour (t.textDim);
        g.setFont (Fonts::mono (10.f));
        juce::String info;
        juce::Path wave;
        const int N = 128;
        if (eng == EngWavetable || eng == EngAnalog)
        {
            const MipTable* tbl = eng == EngAnalog ? &proc.wavetables().saw() : proc.wavetables().acquire ((int) proc.getParamPlain (I.table));
            info = eng == EngAnalog ? choices::vaShapes()[(int) proc.getParamPlain (I.va)] : proc.wavetables().tableName ((int) proc.getParamPlain (I.table));
            if (tbl)
            {
                // 3D frame stack (a few ghost frames) + the current frame
                for (int k = 6; k >= 0; --k)
                {
                    const float fp = k == 0 ? pos : (float) k / 6.f;
                    juce::Path p;
                    const float dx = k == 0 ? 0.f : (float) k * 4.f, dy = k == 0 ? 0.f : -(float) k * 3.f;
                    for (int i = 0; i <= N; ++i)
                    {
                        const float ph = (float) i / N;
                        const float s = eng == EngAnalog ? analog (ph) : tbl->sample (0, fp, ph);
                        const float x = r.getX() + 10 + dx + ph * (r.getWidth() - 40);
                        const float y = r.getCentreY() + dy - s * r.getHeight() * 0.36f;
                        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
                    }
                    if (k == 0) wave = p;
                    else { g.setColour (t.text.withAlpha (0.08f)); g.strokePath (p, juce::PathStrokeType (1.f)); }
                    if (eng == EngAnalog) break;
                }
            }
        }
        else
        {
            const auto* s = proc.samples().get ((int) proc.getParamPlain (I.smp));
            info = s ? s->name : juce::String ("(loading samples)");
            if (s && s->length() > 0)
            {
                const int len = s->length();
                for (int i = 0; i <= N; ++i)
                {
                    const int a = (int) ((double) i / N * (len - 1)), b = juce::jmin (len - 1, a + len / N + 1);
                    float pk = 0; for (int k = a; k < b; k += 4) pk = juce::jmax (pk, std::abs (s->L[(size_t) k]));
                    const float x = r.getX() + (float) i / N * r.getWidth();
                    if (i == 0) wave.startNewSubPath (x, r.getCentreY() - pk * r.getHeight() * 0.42f);
                    else wave.lineTo (x, r.getCentreY() - pk * r.getHeight() * 0.42f);
                }
                for (int i = N; i >= 0; --i)
                {
                    const int a = (int) ((double) i / N * (len - 1)), b = juce::jmin (len - 1, a + len / N + 1);
                    float pk = 0; for (int k = a; k < b; k += 4) pk = juce::jmax (pk, std::abs (s->L[(size_t) k]));
                    wave.lineTo (r.getX() + (float) i / N * r.getWidth(), r.getCentreY() + pk * r.getHeight() * 0.42f);
                }
                wave.closeSubPath();
                g.setColour (t.accent.withAlpha (0.25f)); g.fillPath (wave);
                g.setColour (t.textDim);
                for (int k = 1; k < s->numSlices; ++k)
                {
                    const float x = r.getX() + (float) s->sliceStart[k] / (float) len * r.getWidth();
                    g.drawVerticalLine ((int) x, r.getY() + 8, r.getBottom());
                }
                g.setColour (t.accent);
                g.drawVerticalLine ((int) (r.getX() + pos * r.getWidth()), r.getY(), r.getBottom());
            }
        }
        if (! wave.isEmpty()) drawHalationArc (g, wave, t.accent, 1.8f);
        g.setColour (t.text);
        g.setFont (Fonts::mono (10.f));
        g.drawText (info + (eng == EngWavetable ? "  frame " + juce::String ((int) (pos * 255.f) + 1) + "/256" : juce::String()),
                    getLocalBounds().reduced (8, 4), juce::Justification::topLeft);
        g.setColour (t.textDim);
        g.drawText (DogVisualizer::sceneName (vis.getScene()), getLocalBounds().reduced (8, 4), juce::Justification::bottomRight);
        if (fileHover) { g.setColour (t.accent.withAlpha (0.3f)); g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.f); }
    }

    float analog (float ph) const
    {
        const int shape = (int) proc.getParamPlain (IX().osc[osc].va);
        const float pw = proc.getParamPlain (IX().osc[osc].pw);
        if (shape == 1) return ph < pw ? 0.8f : -0.8f;
        if (shape == 2) return 1.f - 4.f * std::abs (ph - 0.5f);
        return 2.f * ph - 1.f;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            juce::PopupMenu m;
            for (int s = 0; s < DogVisualizer::numScenes(); ++s) m.addItem (s + 1, DogVisualizer::sceneName (s), true, vis.getScene() == s);
            m.addSeparator();
            m.addItem (100, "Normalise + spread drawn frame across table");
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this] (int r) {
                if (r > 0 && r <= DogVisualizer::numScenes())
                {
                    vis.setScene (r - 1);
                    proc.uiState.setProperty ("vis_scene_" + juce::String (osc), r - 1, nullptr);
                }
                else if (r == 100) commitDrawn (true);
            });
            return;
        }
        if (e.mods.isShiftDown()) { startDraw(); drawAt (e.position); }
    }
    void mouseDrag (const juce::MouseEvent& e) override { if (drawing) drawAt (e.position); }
    void mouseUp (const juce::MouseEvent&) override { if (drawing) commitDrawn (false); }

    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        for (auto& f : files) if (f.endsWithIgnoreCase (".wav") || f.endsWithIgnoreCase (".aif") || f.endsWithIgnoreCase (".aiff") || f.endsWithIgnoreCase (".flac")) return true;
        return false;
    }
    void fileDragEnter (const juce::StringArray&, int, int) override { fileHover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { fileHover = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override
    {
        fileHover = false;
        const juce::File f (files[0]);
        const auto& I = IX().osc[osc];
        const int eng = (int) proc.getParamPlain (I.eng);
        if (eng == EngWavetable)
        {
            const int idx = proc.importWavetable (f);
            if (idx >= 0) proc.setParamPlain (I.table, (float) idx);
        }
        else
        {
            const int idx = proc.samples().loadUserSample (f);
            if (idx >= 0) proc.setParamPlain (I.smp, (float) idx);
        }
        repaint();
    }

private:
    // --- simple frame drawing (wavetable editor)
    void startDraw()
    {
        const auto& I = IX().osc[osc];
        const MipTable* tbl = proc.wavetables().acquire ((int) proc.getParamPlain (I.table));
        drawn.assign (2048, 0.f);
        const float pos = proc.getParamPlain (I.pos);
        if (tbl) for (int i = 0; i < 2048; ++i) drawn[(size_t) i] = tbl->sample (0, pos, (float) i / 2048.f);
        drawing = true;
    }
    void drawAt (juce::Point<float> p)
    {
        auto r = getLocalBounds().toFloat().reduced (16.f);
        const float ph = clamp01 ((p.x - r.getX()) / r.getWidth());
        const float v = clampf ((r.getCentreY() - p.y) / (r.getHeight() * 0.36f), -1.f, 1.f);
        const int c = (int) (ph * 2047.f);
        for (int k = -24; k <= 24; ++k)
        {
            const int i = juce::jlimit (0, 2047, c + k);
            const float w = 1.f - std::abs ((float) k) / 25.f;
            drawn[(size_t) i] = lerp (drawn[(size_t) i], v, w);
        }
        repaint();
    }
    void commitDrawn (bool spread)
    {
        drawing = false;
        if (drawn.empty()) return;
        const auto& I = IX().osc[osc];
        const int src = (int) proc.getParamPlain (I.table);
        const MipTable* tbl = proc.wavetables().acquire (src);
        const int target = juce::jlimit (0, 255, (int) (proc.getParamPlain (I.pos) * 255.f));
        std::vector<float> frames ((size_t) kWtFrames * 2048);
        for (int f = 0; f < kWtFrames; ++f)
            for (int i = 0; i < 2048; ++i)
            {
                const float orig = tbl ? tbl->sample (0, (float) f / 255.f, (float) i / 2048.f) : 0.f;
                float w = f == target ? 1.f : 0.f;
                if (spread) w = juce::jmax (0.f, 1.f - std::abs ((float) (f - target)) / 64.f);
                frames[(size_t) (f * 2048 + i)] = lerp (orig, drawn[(size_t) i], w);
            }
        const auto name = proc.wavetables().tableName (src).upToFirstOccurrenceOf (" (edit", false, false) + " (edit " + juce::String (osc + 1) + ")";
        const int idx = proc.addUserWavetable (name, frames, kWtFrames);
        if (idx >= 0) proc.setParamPlain (I.table, (float) idx);
    }

    PureDevotionProcessor& proc;
    int osc;
    DogVisualizer vis;
    bool fileHover = false, drawing = false;
    std::vector<float> drawn;
};
} // namespace

// ============================================================ strips
struct OscPage::OscStrip : public juce::Component, private juce::Timer
{
    OscStrip (PureDevotionProcessor& p, int o) : proc (p), osc (o), display (p, o)
    {
        const juce::String px = "o" + juce::String (o + 1) + "_";
        title = juce::String ("OSC ") + (char) ('A' + o);
        on = std::make_unique<ParamToggle> (p, px + "on", "On");
        engine = std::make_unique<ParamCombo> (p, px + "eng");
        warp = std::make_unique<ParamCombo> (p, px + "warp");
        route = std::make_unique<ParamCombo> (p, px + "route");
        smode = std::make_unique<ParamCombo> (p, px + "smode");
        va = std::make_unique<ParamCombo> (p, px + "va");
        for (auto* c : std::initializer_list<juce::Component*> { on.get(), engine.get(), warp.get(), route.get(), smode.get(), va.get(), &source }) addAndMakeVisible (c);
        addAndMakeVisible (display);
        source.onChange = [this] {
            const auto& I = IX().osc[osc];
            const int eng = (int) proc.getParamPlain (I.eng);
            proc.setParamPlain (eng == EngWavetable ? I.table : I.smp, (float) (source.getSelectedId() - 1));
        };
        for (auto id : { "lvl", "pan", "oct", "semi", "fine", "pos", "warp_amt", "uni", "det", "blend", "width", "drift" })
            main.push_back (std::make_unique<ParamKnob> (p, px + id));
        for (auto id : { "phase", "rphase", "pw", "slice", "gdens", "gsize", "gspray", "formant", "tilt", "pmod" })
            extra.push_back (std::make_unique<ParamKnob> (p, px + id));
        freeze = std::make_unique<ParamToggle> (p, px + "gfreeze", "Freeze");
        addAndMakeVisible (*freeze);
        for (auto& k : main) addAndMakeVisible (*k);
        for (auto& k : extra) addAndMakeVisible (*k);
        lastEngine = -1;
        timerCallback();
        startTimerHz (8);
    }
    void timerCallback() override
    {
        const auto& I = IX().osc[osc];
        const int eng = (int) proc.getParamPlain (I.eng);
        const int count = eng == EngWavetable ? proc.wavetables().numTables() : proc.samples().numSamples();
        if (eng != lastEngine || count != lastCount)
        {
            lastEngine = eng; lastCount = count;
            source.clear (juce::dontSendNotification);
            if (eng == EngWavetable) for (int i = 0; i < count; ++i) source.addItem (proc.wavetables().tableName (i), i + 1);
            else if (eng != EngAnalog) for (int i = 0; i < count; ++i) source.addItem (proc.samples().name (i), i + 1);
            updateVisibility();
        }
        const int sel = (int) proc.getParamPlain (eng == EngWavetable ? I.table : I.smp) + 1;
        if (source.getSelectedId() != sel) source.setSelectedId (sel, juce::dontSendNotification);
    }
    void updateVisibility()
    {
        const int eng = lastEngine;
        // extra: phase rphase pw slice gdens gsize gspray formant tilt pmod
        const bool vis[5][10] = {
            { true, true, false, false, false, false, false, false, false, true },    // wavetable
            { false, false, false, true, false, false, false, false, false, true },   // sample
            { false, false, false, false, true, true, true, false, false, true },     // granular
            { false, false, false, false, false, false, false, true, true, true },    // spectral
            { true, true, true, false, false, false, false, false, false, true } };   // analog
        for (size_t k = 0; k < extra.size(); ++k) extra[k]->setVisible (vis[juce::jlimit (0, 4, eng)][k]);
        freeze->setVisible (eng == EngGranular || eng == EngSpectral);
        smode->setVisible (eng == EngSample);
        va->setVisible (eng == EngAnalog);
        warp->setVisible (eng == EngWavetable);
        source.setVisible (eng != EngAnalog);
        resized();
    }
    void paint (juce::Graphics& g) override { Panel::paintPanel (g, getLocalBounds().toFloat(), title); }
    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 6);
        auto top = r.removeFromTop (22);
        top.removeFromLeft (70);
        on->setBounds (top.removeFromLeft (56));
        engine->setBounds (top.removeFromLeft (110).reduced (2, 1));
        if (source.isVisible()) source.setBounds (top.removeFromLeft (170).reduced (2, 1));
        if (warp->isVisible()) warp->setBounds (top.removeFromLeft (96).reduced (2, 1));
        if (smode->isVisible()) smode->setBounds (top.removeFromLeft (96).reduced (2, 1));
        if (va->isVisible()) va->setBounds (top.removeFromLeft (96).reduced (2, 1));
        route->setBounds (top.removeFromRight (90).reduced (2, 1));
        r.removeFromTop (4);
        display.setBounds (r.removeFromLeft (260));
        r.removeFromLeft (8);
        auto row1 = r.removeFromTop (r.getHeight() / 2);
        layoutRowVec (row1, main, 2);
        std::vector<juce::Component*> ex;
        for (auto& k : extra) if (k->isVisible()) ex.push_back (k.get());
        if (freeze->isVisible()) ex.push_back (freeze.get());
        const int w = 56;
        for (auto* c : ex) { c->setBounds (r.removeFromLeft (w)); r.removeFromLeft (2); }
    }

    PureDevotionProcessor& proc;
    int osc;
    juce::String title;
    WaveDisplay display;
    std::unique_ptr<ParamToggle> on, freeze;
    std::unique_ptr<ParamCombo> engine, warp, route, smode, va;
    juce::ComboBox source;
    std::vector<KnobPtr> main, extra;
    int lastEngine = -1, lastCount = -1;
};

struct OscPage::FilterStrip : public juce::Component
{
    FilterStrip (PureDevotionProcessor& p, int f)
    {
        const juce::String px = "f" + juce::String (f + 1) + "_";
        title = "Filter " + juce::String (f + 1);
        on = std::make_unique<ParamToggle> (p, px + "on", "On");
        type = std::make_unique<ParamCombo> (p, px + "type");
        addAndMakeVisible (*on); addAndMakeVisible (*type);
        const char* ids[] = { "cut", "res", "pre", "post", "key", "env", "morph", "mix" };
        const char* labels[] = { "Cutoff", "Resonance", "Pre", "Post", "Key", "Env", "Morph", "Mix" };
        for (int k = 0; k < 8; ++k) knobs.push_back (std::make_unique<ParamKnob> (p, px + ids[k], labels[k]));
        for (auto& k : knobs) addAndMakeVisible (*k);
    }
    void paint (juce::Graphics& g) override { Panel::paintPanel (g, getLocalBounds().toFloat(), title); }
    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 6);
        auto top = r.removeFromTop (22);
        top.removeFromLeft (90);
        on->setBounds (top.removeFromLeft (56));
        type->setBounds (top.removeFromLeft (130).reduced (2, 1));
        r.removeFromTop (4);
        // big cutoff + resonance, then 6 small
        auto big = r.removeFromLeft (r.getHeight() * 2);
        knobs[0]->setBounds (big.removeFromLeft (big.getWidth() / 2));
        knobs[1]->setBounds (big);
        auto rowA = r.removeFromTop (r.getHeight() / 2), rowB = r;
        layoutRow (rowA, { knobs[2].get(), knobs[3].get(), knobs[4].get() }, 2);
        layoutRow (rowB, { knobs[5].get(), knobs[6].get(), knobs[7].get() }, 2);
    }
    juce::String title;
    std::unique_ptr<ParamToggle> on;
    std::unique_ptr<ParamCombo> type;
    std::vector<KnobPtr> knobs;
};

// ============================================================ page
OscPage::OscPage (PureDevotionProcessor& p) : proc (p)
{
    for (int o = 0; o < kNumOscs; ++o) { oscs.push_back (std::make_unique<OscStrip> (p, o)); addAndMakeVisible (*oscs.back()); }
    for (int f = 0; f < kNumFilters; ++f) { filters.push_back (std::make_unique<FilterStrip> (p, f)); addAndMakeVisible (*filters.back()); }
    addAndMakeVisible (subNoisePanel); addAndMakeVisible (ampPanel);
    auto add = [this] (juce::Component* c) { owned.emplace_back (c); addAndMakeVisible (c); return c; };
    add (new ParamToggle (p, "sub_on", "Sub"));
    add (new ParamCombo (p, "sub_shape"));
    add (new ParamToggle (p, "nz_on", "Noise"));
    add (new ParamCombo (p, "nz_type"));
    add (new ParamCombo (p, "filt_routing"));
    add (new ParamToggle (p, "legato", "Legato"));
    for (auto id : { "sub_lvl", "sub_oct", "nz_lvl", "nz_color" }) { subKnobs.push_back (std::make_unique<ParamKnob> (p, id)); addAndMakeVisible (*subKnobs.back()); }
    for (auto id : { "e1_a", "e1_h", "e1_d", "e1_s", "e1_r", "e1_curve", "glide", "poly", "velo_sens", "master_vol" })
    { ampKnobs.push_back (std::make_unique<ParamKnob> (p, id)); addAndMakeVisible (*ampKnobs.back()); }
}

OscPage::~OscPage() = default;

std::vector<DogVisualizer*> OscPage::visualizers()
{
    std::vector<DogVisualizer*> v;
    for (auto& o : oscs) v.push_back (&o->display.visualizer());
    return v;
}

void OscPage::setVisualizerFullScreen (bool) {}

void OscPage::paint (juce::Graphics&) {}

void OscPage::resized()
{
    auto r = getLocalBounds();
    auto right = r.removeFromRight (420);
    r.removeFromRight (8);
    const int oh = (r.getHeight() - 8) / 3;
    for (auto& o : oscs) { o->setBounds (r.removeFromTop (oh)); r.removeFromTop (4); }

    auto sn = right.removeFromTop (118);
    subNoisePanel.setBounds (sn);
    auto snr = sn.reduced (10, 6); snr.removeFromTop (22);
    auto ctrl = snr.removeFromLeft (150);
    owned[0]->setBounds (ctrl.removeFromTop (22)); owned[1]->setBounds (ctrl.removeFromTop (24).reduced (0, 1));
    owned[2]->setBounds (ctrl.removeFromTop (22)); owned[3]->setBounds (ctrl.removeFromTop (24).reduced (0, 1));
    layoutRowVec (snr, subKnobs, 2);
    right.removeFromTop (6);
    const int fh = 170;
    for (auto& f : filters) { f->setBounds (right.removeFromTop (fh)); right.removeFromTop (6); }
    ampPanel.setBounds (right);
    auto ar = right.reduced (10, 6);
    auto atop = ar.removeFromTop (22);
    atop.removeFromLeft (130);
    owned[4]->setBounds (atop.removeFromLeft (110).reduced (2, 1));
    owned[5]->setBounds (atop.removeFromLeft (90));
    ar.removeFromTop (4);
    auto a1 = ar.removeFromTop (ar.getHeight() / 2);
    layoutRow (a1, { ampKnobs[0].get(), ampKnobs[1].get(), ampKnobs[2].get(), ampKnobs[3].get(), ampKnobs[4].get() }, 2);
    layoutRow (ar, { ampKnobs[5].get(), ampKnobs[6].get(), ampKnobs[7].get(), ampKnobs[8].get(), ampKnobs[9].get() }, 2);
}
} // namespace pd
