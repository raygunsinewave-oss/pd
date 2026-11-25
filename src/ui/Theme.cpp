#include "Theme.h"
#include "PDBinaryData.h"
#include <map>

namespace pd
{
namespace
{
std::vector<Theme> makeThemes()
{
    using namespace Palette;
    std::vector<Theme> v;
    Theme a; a.key = "ultramarine"; a.name = "Ultramarine"; a.background = 0;
    a.bgTop = ultramarine; a.bgBottom = deepNight; a.panel = deepNight.withAlpha (0.72f); a.panelEdge = chrome.withAlpha (0.18f);
    a.accent = sodium; a.accent2 = shutter; a.accent3 = tan; a.chrome = chrome; a.text = text; a.textDim = text.withAlpha (0.55f);
    a.ink = chrome; a.glow = sodium;
    v.push_back (a);

    Theme b = a; b.key = "shutter"; b.name = "Shutter Night"; b.background = 1;
    b.bgTop = juce::Colour (0xff1F3A1B); b.bgBottom = juce::Colour (0xff0B140A); b.panel = juce::Colour (0xd00A1020);
    b.panelEdge = juce::Colour (0x553A7BFF); b.glow = juce::Colour (0xffFF7A33);
    v.push_back (b);

    Theme c = a; c.key = "cassette"; c.name = "Cassette"; c.background = 2; c.lightPanels = true;
    c.bgTop = sodium; c.bgBottom = juce::Colour (0xffD8401F); c.panel = juce::Colour (0x26FFFFFF); c.panelEdge = juce::Colour (0xff111111);
    c.accent = ultramarine; c.accent2 = deepNight; c.chrome = juce::Colour (0xffF6EBDD); c.text = juce::Colour (0xff111111);
    c.textDim = juce::Colour (0xaa111111); c.ink = juce::Colour (0xff111111); c.glow = juce::Colour (0xffFFF2D8);
    v.push_back (c);

    Theme d = a; d.key = "sketch"; d.name = "Sketch"; d.background = 3; d.lightPanels = true;
    d.bgTop = juce::Colour (0xffF5F1E6); d.bgBottom = juce::Colour (0xffECE5D3); d.panel = juce::Colour (0x66FFFFFF);
    d.panelEdge = pink; d.accent = sodium; d.accent2 = pink; d.chrome = juce::Colour (0xffFBF8F1); d.text = juce::Colour (0xff23202A);
    d.textDim = juce::Colour (0x9923202A); d.ink = juce::Colour (0xff2A2630); d.glow = pink;
    v.push_back (d);

    Theme e = a; e.key = "dusk"; e.name = "Dusk Drive"; e.background = 4;
    e.bgTop = lavender; e.bgBottom = juce::Colour (0xffE58C6A); e.panel = juce::Colour (0xb02A1C40); e.panelEdge = pink.withAlpha (0.5f);
    e.accent = sodium; e.accent2 = pink; e.glow = juce::Colour (0xffFFB27A);
    v.push_back (e);
    return v;
}
const std::vector<Theme>& themes() { static const auto t = makeThemes(); return t; }
int& currentThemeIdx() { static int i = 0; return i; }
std::map<void*, std::function<void()>>& listeners() { static std::map<void*, std::function<void()>> l; return l; }

juce::Typeface::Ptr typeface (const void* data, size_t size)
{
    return juce::Typeface::createSystemTypefaceFor (data, size);
}
} // namespace

int ThemeManager::numThemes() { return (int) themes().size(); }
const Theme& ThemeManager::theme (int i) { return themes()[(size_t) juce::jlimit (0, numThemes() - 1, i)]; }
const Theme& ThemeManager::current() { return theme (currentThemeIdx()); }
int ThemeManager::currentIndex() { return currentThemeIdx(); }
void ThemeManager::setCurrent (int i)
{
    currentThemeIdx() = juce::jlimit (0, numThemes() - 1, i);
    auto copy = listeners();
    for (auto& [k, fn] : copy) fn();
}
void ThemeManager::addListener (void* owner, std::function<void()> fn) { listeners()[owner] = std::move (fn); }
void ThemeManager::removeListener (void* owner) { listeners().erase (owner); }

// ---------------------------------------------------------------- fonts
juce::Font Fonts::header (float h)
{
    static auto tf = typeface (PDBinaryData::RobotoCondensedBold_ttf, PDBinaryData::RobotoCondensedBold_ttfSize);
    return juce::Font (juce::FontOptions (tf).withHeight (h));
}
juce::Font Fonts::mono (float h, bool bold)
{
    static auto reg = typeface (PDBinaryData::JetBrainsMonoRegular_ttf, PDBinaryData::JetBrainsMonoRegular_ttfSize);
    static auto bld = typeface (PDBinaryData::JetBrainsMonoBold_ttf, PDBinaryData::JetBrainsMonoBold_ttfSize);
    return juce::Font (juce::FontOptions (bold ? bld : reg).withHeight (h));
}
juce::Font Fonts::label (float h) { return header (h); }

void drawTracked (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, const juce::Font& f,
                  float tracking, juce::Justification just)
{
    juce::GlyphArrangement ga;
    juce::Font ft = f.withExtraKerningFactor (tracking);
    ga.addFittedText (ft, text, r.getX(), r.getY(), r.getWidth(), r.getHeight(), just, 1, 1.f);
    ga.draw (g);
}

// ---------------------------------------------------------------- logo
void drawRaygunLogo (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour body, juce::Colour beam)
{
    const float s = r.getHeight() / 64.f;
    auto tx = juce::AffineTransform::scale (s).translated (r.getX(), r.getY());
    juce::Path gun;
    gun.startNewSubPath (10, 22); gun.lineTo (52, 22); gun.quadraticTo (60, 22, 60, 30); gun.quadraticTo (60, 38, 52, 38);
    gun.lineTo (34, 38); gun.lineTo (28, 54); gun.lineTo (18, 54); gun.lineTo (22, 38); gun.lineTo (10, 38);
    gun.quadraticTo (6, 38, 6, 30); gun.quadraticTo (6, 22, 10, 22); gun.closeSubPath();
    for (float fx : { 18.f, 28.f, 38.f }) { gun.startNewSubPath (fx, 22); gun.lineTo (fx, 14); }
    gun.addEllipse (59, 25, 10, 10);
    g.setColour (body);
    g.strokePath (gun, juce::PathStrokeType (4.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), tx);
    juce::Path wave;
    wave.startNewSubPath (72, 30);
    for (int i = 0; i <= 92; ++i)
    {
        const float x = 72.f + (float) i;
        const float a = 16.f * std::exp (-(float) i / 45.f);
        wave.lineTo (x, 30.f - a * std::sin ((float) i * 0.52f));
    }
    wave.lineTo (234, 30);
    g.setColour (beam);
    g.strokePath (wave, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), tx);
}

void drawHalationArc (juce::Graphics& g, const juce::Path& p, juce::Colour c, float thickness)
{
    // chromatic halation: a soft warm bloom with faint red/blue fringes
    g.setColour (c.withAlpha (0.12f));
    g.strokePath (p, juce::PathStrokeType (thickness * 3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (juce::Colour (0xffff3b30).withAlpha (0.18f));
    g.strokePath (p, juce::PathStrokeType (thickness * 1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), juce::AffineTransform::translation (0.6f, 0.f));
    g.setColour (juce::Colour (0xff3b7bff).withAlpha (0.14f));
    g.strokePath (p, juce::PathStrokeType (thickness * 1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), juce::AffineTransform::translation (-0.6f, 0.f));
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// ---------------------------------------------------------------- backgrounds
static void palm (juce::Graphics& g, float x, float base, float h, juce::Colour c)
{
    juce::Path trunk;
    trunk.startNewSubPath (x, base);
    trunk.quadraticTo (x + h * 0.08f, base - h * 0.5f, x + h * 0.15f, base - h);
    g.setColour (c);
    g.strokePath (trunk, juce::PathStrokeType (h * 0.035f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const float tx = x + h * 0.15f, ty = base - h;
    for (int i = 0; i < 7; ++i)
    {
        const float a = -2.9f + (float) i * 0.48f;
        juce::Path leaf;
        leaf.startNewSubPath (tx, ty);
        const float ex = tx + std::cos (a) * h * 0.42f, ey = ty + std::sin (a) * h * 0.25f + h * 0.12f;
        leaf.quadraticTo (tx + std::cos (a) * h * 0.25f, ty + std::sin (a) * h * 0.3f - h * 0.05f, ex, ey);
        g.strokePath (leaf, juce::PathStrokeType (h * 0.03f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

static void retroCar (juce::Graphics& g, float x, float base, float w, juce::Colour c)
{
    juce::Path car;
    const float h = w * 0.28f;
    car.startNewSubPath (x, base - h * 0.35f);
    car.lineTo (x + w * 0.08f, base - h * 0.62f);
    car.lineTo (x + w * 0.3f, base - h * 0.68f);
    car.lineTo (x + w * 0.42f, base - h);
    car.lineTo (x + w * 0.72f, base - h);
    car.lineTo (x + w * 0.82f, base - h * 0.66f);
    car.lineTo (x + w, base - h * 0.58f);
    car.lineTo (x + w, base - h * 0.2f);
    car.lineTo (x, base - h * 0.2f);
    car.closeSubPath();
    g.setColour (c);
    g.fillPath (car);
    g.fillEllipse (x + w * 0.14f, base - h * 0.42f, h * 0.42f, h * 0.42f);
    g.fillEllipse (x + w * 0.72f, base - h * 0.42f, h * 0.42f, h * 0.42f);
}

void ThemeManager::paintBackground (juce::Graphics& g, juce::Rectangle<float> r, const Theme& t, float time)
{
    g.setGradientFill (juce::ColourGradient (t.bgTop, r.getX(), r.getY(), t.bgBottom, r.getX(), r.getBottom(), false));
    g.fillRect (r);
    switch (t.background)
    {
        case 0:   // ultramarine studio backdrop: soft vignette + floor glow
        {
            juce::ColourGradient vg (juce::Colours::transparentBlack, r.getCentreX(), r.getCentreY() * 0.8f,
                                     juce::Colours::black.withAlpha (0.45f), r.getX(), r.getY(), true);
            g.setGradientFill (vg); g.fillRect (r);
            juce::ColourGradient floor (Palette::ultramarine.brighter (0.4f).withAlpha (0.25f), r.getCentreX(), r.getBottom(),
                                        juce::Colours::transparentBlack, r.getCentreX(), r.getBottom() - r.getHeight() * 0.5f, true);
            g.setGradientFill (floor); g.fillRect (r);
        } break;
        case 1:   // green roller-shutter stripes, sodium glow, blue edge light
        {
            const float stripe = 14.f;
            for (float y = r.getY(); y < r.getBottom(); y += stripe)
            {
                g.setColour (Palette::shutter.withAlpha (0.55f)); g.fillRect (r.getX(), y, r.getWidth(), stripe * 0.62f);
                g.setColour (juce::Colours::black.withAlpha (0.35f)); g.fillRect (r.getX(), y + stripe * 0.62f, r.getWidth(), 1.5f);
                g.setColour (juce::Colours::white.withAlpha (0.05f)); g.fillRect (r.getX(), y + 1.f, r.getWidth(), 1.f);
            }
            juce::ColourGradient glow (t.glow.withAlpha (0.38f + 0.04f * std::sin (time * 1.3f)), r.getWidth() * 0.78f, r.getY() - 40,
                                       juce::Colours::transparentBlack, r.getWidth() * 0.45f, r.getHeight() * 0.7f, true);
            g.setGradientFill (glow); g.fillRect (r);
            juce::ColourGradient edge (juce::Colour (0x553A7BFF), r.getX(), r.getCentreY(), juce::Colours::transparentBlack, r.getX() + 120, r.getCentreY(), false);
            g.setGradientFill (edge); g.fillRect (r);
        } break;
        case 2:   // cassette J-card: flat orange, black line art, printed tracklist rules
        {
            g.setColour (t.ink.withAlpha (0.28f));
            const float m = 18.f;
            g.drawRect (r.reduced (m), 2.f);
            for (int i = 0; i < 6; ++i) g.drawHorizontalLine ((int) (r.getBottom() - m - 30.f - (float) i * 9.f), r.getX() + m + 10, r.getX() + m + 260);
            // tape window line art
            auto win = juce::Rectangle<float> (r.getRight() - 360, r.getBottom() - 150, 320, 110);
            g.drawRoundedRectangle (win, 12.f, 2.f);
            for (float cx : { win.getX() + 80, win.getRight() - 80 })
            {
                g.drawEllipse (cx - 30, win.getCentreY() - 30, 60, 60, 2.f);
                for (int k = 0; k < 6; ++k)
                {
                    const float a = (float) k * juce::MathConstants<float>::pi / 3.f + time * 0.6f;
                    g.drawLine (cx, win.getCentreY(), cx + 12 * std::cos (a), win.getCentreY() + 12 * std::sin (a), 2.f);
                }
            }
        } break;
        case 3:   // sketch paper with hand-drawn pink figures in the margins
        {
            g.setColour (juce::Colour (0x1023202A));
            for (float y = r.getY() + 28; y < r.getBottom(); y += 28) g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
            g.setColour (Palette::pink.withAlpha (0.55f));
            juce::Path p;
            for (int f = 0; f < 3; ++f)
            {
                const float bx = r.getRight() - 120.f - (float) f * 70.f, by = r.getBottom() - 30.f;
                const float sw = std::sin (time * 2.f + (float) f);
                p.addEllipse (bx - 8, by - 92, 16, 16);
                p.startNewSubPath (bx, by - 76); p.lineTo (bx + sw * 4, by - 40);
                p.startNewSubPath (bx + sw * 4, by - 40); p.lineTo (bx - 12, by); p.startNewSubPath (bx + sw * 4, by - 40); p.lineTo (bx + 14, by);
                p.startNewSubPath (bx, by - 68); p.lineTo (bx - 20, by - 50 - sw * 10); p.startNewSubPath (bx, by - 68); p.lineTo (bx + 22, by - 80 + sw * 8);
            }
            g.strokePath (p, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        } break;
        default:  // dusk drive: sunset gradient, sun, palms, retro car silhouette
        {
            juce::ColourGradient sky (Palette::lavender, r.getX(), r.getY(), juce::Colour (0xffF2A7C3), r.getX(), r.getCentreY() + 80, false);
            sky.addColour (0.85, juce::Colour (0xffE58C6A));
            g.setGradientFill (sky); g.fillRect (r);
            const float sunR = r.getHeight() * 0.22f;
            const juce::Point<float> sun (r.getWidth() * 0.7f, r.getBottom() - r.getHeight() * 0.22f);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xffFFD9A0), sun.x, sun.y - sunR, juce::Colour (0xffF06A7A), sun.x, sun.y + sunR, false));
            g.fillEllipse (sun.x - sunR, sun.y - sunR, sunR * 2, sunR * 2);
            g.setColour (juce::Colour (0xffE58C6A));
            for (int i = 0; i < 5; ++i) g.fillRect (sun.x - sunR, sun.y + (float) i * sunR * 0.18f, sunR * 2, sunR * 0.05f + (float) i * 1.2f);
            const juce::Colour sil (0xff2B1D3D);
            const float ground = r.getBottom() - 26.f;
            g.setColour (sil); g.fillRect (r.getX(), ground, r.getWidth(), r.getBottom() - ground);
            palm (g, r.getWidth() * 0.08f, ground, 190.f, sil);
            palm (g, r.getWidth() * 0.15f, ground, 140.f, sil);
            palm (g, r.getWidth() * 0.9f, ground, 170.f, sil);
            retroCar (g, r.getWidth() * 0.48f, ground + 4.f, 210.f, sil);
        } break;
    }
}

void ThemeManager::paintGrain (juce::Graphics& g, juce::Rectangle<int> r, float opacity, int frame)
{
    static juce::Image grain[3];
    if (grain[0].isNull())
        for (int k = 0; k < 3; ++k)
        {
            grain[k] = juce::Image (juce::Image::ARGB, 192, 192, true);
            juce::Random rnd (1234 + k);
            for (int y = 0; y < 192; ++y)
                for (int x = 0; x < 192; ++x)
                {
                    const auto v = (juce::uint8) rnd.nextInt (256);
                    grain[k].setPixelAt (x, y, juce::Colour (v, v, v, (juce::uint8) 255));
                }
        }
    g.setOpacity (opacity);
    g.setTiledImageFill (grain[frame % 3], 0, 0, opacity);
    g.fillRect (r);
}

// ---------------------------------------------------------------- look and feel
PDLookAndFeel::PDLookAndFeel() { refresh(); }

void PDLookAndFeel::refresh()
{
    const auto& t = ThemeManager::current();
    setColour (juce::ResizableWindow::backgroundColourId, t.bgBottom);
    setColour (juce::Label::textColourId, t.text);
    setColour (juce::ComboBox::textColourId, t.text);
    setColour (juce::ComboBox::backgroundColourId, t.panel.withMultipliedAlpha (1.2f));
    setColour (juce::ComboBox::outlineColourId, t.panelEdge);
    setColour (juce::ComboBox::arrowColourId, t.accent);
    setColour (juce::PopupMenu::backgroundColourId, t.lightPanels ? t.bgTop : Palette::deepNight);
    setColour (juce::PopupMenu::textColourId, t.lightPanels ? t.text : Palette::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, t.accent);
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::TextButton::buttonColourId, t.panel);
    setColour (juce::TextButton::buttonOnColourId, t.accent);
    setColour (juce::TextButton::textColourOffId, t.text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    setColour (juce::ToggleButton::textColourId, t.text);
    setColour (juce::ToggleButton::tickColourId, t.accent);
    setColour (juce::TextEditor::backgroundColourId, t.panel);
    setColour (juce::TextEditor::textColourId, t.text);
    setColour (juce::TextEditor::outlineColourId, t.panelEdge);
    setColour (juce::TextEditor::focusedOutlineColourId, t.accent);
    setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::ScrollBar::thumbColourId, t.accent.withAlpha (0.6f));
    setColour (juce::Slider::textBoxTextColourId, t.text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::CaretComponent::caretColourId, t.accent);
}

void PDLookAndFeel::drawChromeKnob (juce::Graphics& g, juce::Rectangle<float> r, float angle, const Theme& t, bool chain)
{
    const auto c = r.getCentre();
    const float rad = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f;
    if (chain && rad > 14.f)
    {
        // chain-link texture on the outer ring
        const int links = juce::jlimit (12, 28, (int) (rad * 0.9f));
        const float lr = rad * 0.93f;
        for (int i = 0; i < links; ++i)
        {
            const float a = (float) i / (float) links * juce::MathConstants<float>::twoPi;
            const float lx = c.x + std::cos (a) * lr, ly = c.y + std::sin (a) * lr;
            const float lw = rad * 0.22f, lh = rad * 0.11f;
            juce::Path link;
            link.addRoundedRectangle (-lw * 0.5f, -lh * 0.5f, lw, lh, lh * 0.5f);
            const float rot = a + juce::MathConstants<float>::halfPi + ((i & 1) ? 0.f : juce::MathConstants<float>::halfPi);
            auto tr = juce::AffineTransform::rotation (rot).translated (lx, ly);
            g.setColour (t.chrome.darker (0.55f));
            g.strokePath (link, juce::PathStrokeType (rad * 0.045f), tr);
            g.setColour (t.chrome.withAlpha (0.65f));
            g.strokePath (link, juce::PathStrokeType (rad * 0.02f), tr.translated (-0.4f, -0.4f));
        }
    }
    const float br = rad * (chain ? 0.74f : 0.9f);
    // brushed chrome body
    juce::ColourGradient body (t.chrome.brighter (0.25f), c.x - br * 0.6f, c.y - br * 0.8f, t.chrome.darker (0.65f), c.x + br * 0.5f, c.y + br, false);
    body.addColour (0.45, t.chrome);
    g.setGradientFill (body);
    g.fillEllipse (c.x - br, c.y - br, br * 2, br * 2);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    for (float k = 0.15f; k < 1.f; k += 0.11f) g.drawEllipse (c.x - br * k, c.y - br * k, br * 2 * k, br * 2 * k, 0.6f);
    // specular sweep
    juce::Path spec;
    spec.addPieSegment (c.x - br, c.y - br, br * 2, br * 2, -2.4f, -0.9f, 0.55f);
    g.setColour (juce::Colours::white.withAlpha (0.22f));
    g.fillPath (spec);
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.drawEllipse (c.x - br, c.y - br, br * 2, br * 2, 1.f);
    // pointer
    const float px = c.x + std::sin (angle) * br * 0.78f, py = c.y - std::cos (angle) * br * 0.78f;
    g.setColour (juce::Colour (0xff15161A));
    g.drawLine (c.x + std::sin (angle) * br * 0.25f, c.y - std::cos (angle) * br * 0.25f, px, py, juce::jmax (1.5f, br * 0.12f));
}

void PDLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    const auto& t = ThemeManager::current();
    auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2.f);
    const float side = juce::jmin (r.getWidth(), r.getHeight());
    r = r.withSizeKeepingCentre (side, side);
    const float rad = side * 0.5f;
    const auto c = r.getCentre();
    // track
    juce::Path track;
    track.addCentredArc (c.x, c.y, rad - 1.5f, rad - 1.5f, 0, start, end, true);
    g.setColour (t.lightPanels ? t.ink.withAlpha (0.18f) : juce::Colours::black.withAlpha (0.45f));
    g.strokePath (track, juce::PathStrokeType (3.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    // value arc (bipolar params draw from centre)
    const bool bipolar = s.getMinimum() < 0 && s.getMaximum() > 0;
    const float zero = bipolar ? (float) s.valueToProportionOfLength (0.0) : 0.f;
    const float a0 = start + zero * (end - start), a1 = start + pos * (end - start);
    juce::Path val;
    val.addCentredArc (c.x, c.y, rad - 1.5f, rad - 1.5f, 0, juce::jmin (a0, a1), juce::jmax (a0, a1), true);
    drawHalationArc (g, val, s.isEnabled() ? t.accent : t.textDim, 3.f);
    drawChromeKnob (g, r.reduced (5.f), a1, t, side > 40.f);
}

void PDLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                      juce::Slider::SliderStyle style, juce::Slider& s)
{
    const auto& t = ThemeManager::current();
    const bool vertical = style == juce::Slider::LinearVertical || style == juce::Slider::LinearBarVertical;
    auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    g.setColour (t.lightPanels ? t.ink.withAlpha (0.15f) : juce::Colours::black.withAlpha (0.4f));
    if (style == juce::Slider::LinearBar || style == juce::Slider::LinearBarVertical)
    {
        g.fillRoundedRectangle (r, 3.f);
        const bool bip = s.getMinimum() < 0;
        const float zx = bip ? r.getX() + r.getWidth() * (float) s.valueToProportionOfLength (0.0) : r.getX();
        auto fill = vertical ? r.withTop (pos) : juce::Rectangle<float> (juce::jmin (zx, pos), r.getY(), std::abs (pos - zx), r.getHeight());
        g.setColour (t.accent.withAlpha (0.85f));
        g.fillRoundedRectangle (fill, 3.f);
        return;
    }
    const auto track = vertical ? r.withSizeKeepingCentre (4.f, r.getHeight()) : r.withSizeKeepingCentre (r.getWidth(), 4.f);
    g.fillRoundedRectangle (track, 2.f);
    g.setColour (t.accent);
    if (vertical) g.fillRoundedRectangle (track.withTop (pos), 2.f);
    else g.fillRoundedRectangle (track.withRight (pos), 2.f);
    const auto thumb = vertical ? juce::Rectangle<float> (12, 12).withCentre ({ r.getCentreX(), pos })
                                : juce::Rectangle<float> (12, 12).withCentre ({ pos, r.getCentreY() });
    drawChromeKnob (g, thumb, 0.f, t, false);
}

void PDLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    const auto& t = ThemeManager::current();
    auto r = b.getLocalBounds().toFloat().reduced (1.f);
    const bool on = b.getToggleState();
    g.setColour (on ? t.accent : t.panel.withMultipliedAlpha (over ? 1.4f : 1.f));
    g.fillRoundedRectangle (r, 4.f);
    g.setColour (on ? t.accent.brighter (0.3f) : t.panelEdge.withMultipliedAlpha (down ? 2.f : 1.f));
    g.drawRoundedRectangle (r, 4.f, 1.f);
}

void PDLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    const auto& t = ThemeManager::current();
    g.setColour (b.getToggleState() ? juce::Colours::white : t.text);
    drawTracked (g, b.getButtonText().toUpperCase(), b.getLocalBounds().toFloat().reduced (4, 0), Fonts::header (juce::jmin (15.f, (float) b.getHeight() * 0.55f)),
                 0.08f, juce::Justification::centred);
}

void PDLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool)
{
    const auto& t = ThemeManager::current();
    auto r = b.getLocalBounds().toFloat();
    auto box = r.removeFromLeft (r.getHeight()).reduced (r.getHeight() * 0.22f);
    g.setColour (b.getToggleState() ? t.accent : t.panel.withMultipliedAlpha (over ? 1.5f : 1.f));
    g.fillRoundedRectangle (box, 3.f);
    g.setColour (t.panelEdge.withMultipliedAlpha (2.f));
    g.drawRoundedRectangle (box, 3.f, 1.f);
    if (b.getToggleState())
    {
        g.setColour (t.accent.withAlpha (0.25f));
        g.fillRoundedRectangle (box.expanded (3.f), 5.f);
    }
    g.setColour (t.text);
    g.setFont (Fonts::header (juce::jmin (14.f, r.getHeight() * 0.62f)));
    g.drawFittedText (b.getButtonText().toUpperCase(), r.reduced (4, 0).toNearestInt(), juce::Justification::centredLeft, 1);
}

void PDLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& cb)
{
    const auto& t = ThemeManager::current();
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (t.panel.withMultipliedAlpha (cb.isMouseOver() ? 1.5f : 1.2f));
    g.fillRoundedRectangle (r, 4.f);
    g.setColour (t.panelEdge.withMultipliedAlpha (1.5f));
    g.drawRoundedRectangle (r, 4.f, 1.f);
    juce::Path arrow;
    const float ax = (float) w - 12.f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 4, ay - 2, ax + 4, ay - 2, ax, ay + 3);
    g.setColour (t.accent);
    g.fillPath (arrow);
}

void PDLookAndFeel::positionComboBoxText (juce::ComboBox& cb, juce::Label& l)
{
    l.setBounds (4, 1, cb.getWidth() - 22, cb.getHeight() - 2);
    l.setFont (getComboBoxFont (cb));
}

juce::Font PDLookAndFeel::getComboBoxFont (juce::ComboBox& cb) { return Fonts::mono (juce::jmin (13.f, (float) cb.getHeight() * 0.55f)); }
juce::Font PDLookAndFeel::getTextButtonFont (juce::TextButton&, int h) { return Fonts::header (juce::jmin (15.f, (float) h * 0.55f)); }
juce::Font PDLookAndFeel::getLabelFont (juce::Label& l) { return Fonts::mono (juce::jmin (13.f, (float) l.getHeight() * 0.8f)); }
juce::Font PDLookAndFeel::getPopupMenuFont() { return Fonts::mono (14.f); }

void PDLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    const auto& t = ThemeManager::current();
    g.fillAll (findColour (juce::PopupMenu::backgroundColourId));
    g.setColour (t.accent.withAlpha (0.6f));
    g.drawRect (0, 0, w, h, 1);
}

void PDLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int ts, int tsz, bool over, bool)
{
    const auto& t = ThemeManager::current();
    auto thumb = vertical ? juce::Rectangle<int> (x + 2, ts, w - 4, tsz) : juce::Rectangle<int> (ts, y + 2, tsz, h - 4);
    g.setColour (t.accent.withAlpha (over ? 0.8f : 0.5f));
    g.fillRoundedRectangle (thumb.toFloat(), 3.f);
}
} // namespace pd
