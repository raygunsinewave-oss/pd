#include "DogVisualizer.h"
#include "../PluginProcessor.h"

namespace pd
{
namespace
{
const juce::Colour kCoat (0xff17141C), kCoatSheen (0xff3A3544), kTan (Palette::tan), kTanLight (0xffD99550), kCollar (Palette::chrome);
using AT = juce::AffineTransform;

// ------------------------------------------------------------ dog parts (unit space, facing right)
juce::Path headProfile (float jawOpen, juce::Path& jaw, juce::Path& ear, float earAngle)
{
    juce::Path skull;
    skull.startNewSubPath (0.08f, 0.36f);
    skull.cubicTo (0.12f, 0.16f, 0.36f, 0.12f, 0.50f, 0.22f);    // crown -> forehead
    skull.quadraticTo (0.54f, 0.26f, 0.58f, 0.28f);              // stop
    skull.lineTo (0.95f, 0.38f);                                 // muzzle bridge
    skull.quadraticTo (1.00f, 0.40f, 1.00f, 0.46f);              // nose
    skull.lineTo (0.96f, 0.51f);
    skull.lineTo (0.62f, 0.54f);                                 // upper lip -> mouth corner
    skull.quadraticTo (0.50f, 0.62f, 0.36f, 0.74f);              // cheek/throat
    skull.lineTo (0.30f, 1.02f);                                 // neck front
    skull.lineTo (-0.02f, 1.02f);                                // neck back
    skull.quadraticTo (0.0f, 0.6f, 0.08f, 0.36f);
    skull.closeSubPath();

    jaw.clear();
    jaw.startNewSubPath (0.60f, 0.54f);
    jaw.lineTo (0.90f, 0.53f);
    jaw.quadraticTo (0.92f, 0.58f, 0.86f, 0.60f);
    jaw.quadraticTo (0.70f, 0.64f, 0.56f, 0.64f);
    jaw.closeSubPath();
    jaw.applyTransform (AT::rotation (jawOpen * 0.45f, 0.60f, 0.55f));

    ear.clear();
    ear.startNewSubPath (0.20f, 0.26f);
    ear.quadraticTo (0.17f, 0.02f, 0.21f, -0.22f);               // cropped, erect, pointed
    ear.quadraticTo (0.33f, -0.02f, 0.40f, 0.20f);
    ear.closeSubPath();
    ear.applyTransform (AT::rotation (earAngle, 0.30f, 0.22f));
    return skull;
}

void drawHead (juce::Graphics& g, const AT& tx, float jawOpen, float earAngle, bool tongue, float tongueFlap, juce::Colour coat)
{
    juce::Path jaw, ear;
    auto skull = headProfile (jawOpen, jaw, ear, earAngle);
    // mouth interior when open
    if (jawOpen > 0.05f)
    {
        juce::Path mouth;
        mouth.startNewSubPath (0.60f, 0.54f); mouth.lineTo (0.94f, 0.52f); mouth.lineTo (0.88f, 0.56f + jawOpen * 0.18f); mouth.closeSubPath();
        g.setColour (juce::Colour (0xff5A1C24)); g.fillPath (mouth, tx);
    }
    g.setColour (coat); g.fillPath (ear, tx);
    g.setColour (juce::Colour (0xff4A2A30)); g.fillPath (ear, AT::scale (0.6f, 0.75f, 0.30f, 0.20f).followedBy (tx));
    g.setColour (coat); g.fillPath (skull, tx); g.fillPath (jaw, tx);
    // sheen along the skull
    juce::Path sheen; sheen.startNewSubPath (0.16f, 0.30f); sheen.cubicTo (0.24f, 0.2f, 0.4f, 0.18f, 0.5f, 0.24f);
    g.setColour (kCoatSheen); g.strokePath (sheen, juce::PathStrokeType (0.03f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), tx);
    // tan markings: muzzle, cheek, eyebrow, chest
    juce::Path tan;
    tan.startNewSubPath (0.62f, 0.47f); tan.lineTo (0.94f, 0.50f); tan.lineTo (0.62f, 0.54f); tan.closeSubPath();
    tan.addEllipse (0.44f, 0.46f, 0.12f, 0.09f);
    tan.addEllipse (0.49f, 0.245f, 0.05f, 0.035f);
    tan.startNewSubPath (0.28f, 0.9f); tan.quadraticTo (0.30f, 0.80f, 0.34f, 0.78f); tan.lineTo (0.32f, 1.0f); tan.closeSubPath();
    g.setColour (kTan); g.fillPath (tan, tx);
    juce::Path jawTan; jawTan.addEllipse (0.62f, 0.555f, 0.2f, 0.05f);
    jawTan.applyTransform (AT::rotation (jawOpen * 0.45f, 0.60f, 0.55f));
    g.fillPath (jawTan, tx);
    // eye + nose
    g.setColour (juce::Colour (0xff2A1A10)); g.fillEllipse (juce::Rectangle<float> (0.52f, 0.30f, 0.045f, 0.035f).transformedBy (tx));
    g.setColour (juce::Colours::white.withAlpha (0.7f)); g.fillEllipse (juce::Rectangle<float> (0.535f, 0.305f, 0.012f, 0.01f).transformedBy (tx));
    g.setColour (juce::Colour (0xff050505)); g.fillEllipse (juce::Rectangle<float> (0.95f, 0.40f, 0.06f, 0.05f).transformedBy (tx));
    // collar
    juce::Path collar; collar.startNewSubPath (-0.01f, 0.88f); collar.quadraticTo (0.15f, 0.93f, 0.31f, 0.86f);
    g.setColour (kCollar); g.strokePath (collar, juce::PathStrokeType (0.045f), tx);
    if (tongue)
    {
        juce::Path tg;
        const float flap = std::sin (tongueFlap) * 0.06f;
        tg.startNewSubPath (0.80f, 0.56f);
        tg.quadraticTo (0.92f, 0.64f + flap, 0.88f + flap, 0.78f + jawOpen * 0.1f);
        tg.quadraticTo (0.82f, 0.76f, 0.76f, 0.58f);
        tg.closeSubPath();
        g.setColour (juce::Colour (0xffE2788F)); g.fillPath (tg, tx);
    }
}

// side-view standing / trotting dog. (x, ground) is the hind foot line; s = body length in px
void drawDog (juce::Graphics& g, float x, float ground, float s, float gait, float headBob, bool lying, juce::Colour coat, float earAngle = 0.f)
{
    const float h = s * 0.62f;
    AT tx = AT::scale (s).translated (x, ground - h);
    if (lying) tx = AT::scale (s, s * 0.75f).translated (x, ground - h * 0.55f);
    // legs (thigh + shin) for trot: diagonal pairs in phase
    auto leg = [&] (float hipX, float phase, bool front) {
        const float sw = lying ? 0.f : std::sin (phase) * 0.45f;
        const float lift = lying ? 0.f : juce::jmax (0.f, std::cos (phase)) * 0.12f;
        const float hx = hipX, hy = 0.42f;
        const float kx = hx + std::sin (sw) * 0.22f + (front ? 0.02f : -0.04f), ky = hy + 0.26f - lift;
        const float fx = kx + std::sin (sw * 0.6f + (front ? 0.2f : -0.3f)) * 0.22f, fy = juce::jmin (1.0f, ky + 0.34f - lift * 0.5f);
        juce::Path p; p.startNewSubPath (hx, hy); p.lineTo (kx, ky); p.lineTo (fx, lying ? 0.95f : fy);
        g.setColour (coat); g.strokePath (p, juce::PathStrokeType (0.075f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), tx);
        g.setColour (kTan);
        g.fillEllipse (juce::Rectangle<float> (fx - 0.035f, (lying ? 0.95f : fy) - 0.03f, 0.07f, 0.04f).transformedBy (tx));
    };
    leg (0.22f, gait + juce::MathConstants<float>::pi, false);
    leg (0.72f, gait, true);
    // torso
    juce::Path body;
    body.startNewSubPath (0.12f, 0.34f);
    body.cubicTo (0.3f, 0.26f, 0.6f, 0.26f, 0.80f, 0.28f);     // topline
    body.quadraticTo (0.90f, 0.36f, 0.84f, 0.56f);             // deep chest
    body.quadraticTo (0.6f, 0.58f, 0.40f, 0.48f);              // tuck-up
    body.quadraticTo (0.22f, 0.54f, 0.10f, 0.48f);             // thigh
    body.quadraticTo (0.06f, 0.4f, 0.12f, 0.34f);
    body.closeSubPath();
    g.setColour (coat); g.fillPath (body, tx);
    juce::Path tail; tail.startNewSubPath (0.12f, 0.34f); tail.lineTo (0.05f, 0.24f);
    g.strokePath (tail, juce::PathStrokeType (0.05f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), tx);
    juce::Path sheen; sheen.startNewSubPath (0.25f, 0.30f); sheen.cubicTo (0.4f, 0.27f, 0.6f, 0.27f, 0.75f, 0.30f);
    g.setColour (kCoatSheen); g.strokePath (sheen, juce::PathStrokeType (0.02f), tx);
    leg (0.28f, gait, false);
    leg (0.78f, gait + juce::MathConstants<float>::pi, true);
    // chest tan
    g.setColour (kTan); g.fillEllipse (juce::Rectangle<float> (0.78f, 0.40f, 0.07f, 0.1f).transformedBy (tx));
    // head on the neck
    const AT headTx = AT::scale (0.42f).translated (0.70f, -0.12f + headBob * 0.03f).followedBy (tx);
    drawHead (g, headTx, 0.f, earAngle, false, 0.f, coat);
}

// front-ish view head looking down at the camera (Low Angle)
void drawHeadFront (juce::Graphics& g, juce::Rectangle<float> r, float jaw, float glint, float earTwitch)
{
    const float w = r.getWidth(), h = r.getHeight();
    const float cx = r.getCentreX(), top = r.getY();
    auto P = [&] (float x, float y) { return juce::Point<float> (cx + x * w, top + y * h); };
    // ears
    for (float side : { -1.f, 1.f })
    {
        juce::Path ear;
        ear.startNewSubPath (P (side * 0.18f, 0.30f));
        ear.quadraticTo (P (side * (0.30f + earTwitch * 0.03f), 0.08f), P (side * (0.36f + earTwitch * 0.05f), -0.08f));
        ear.quadraticTo (P (side * 0.40f, 0.15f), P (side * 0.34f, 0.36f));
        ear.closeSubPath();
        g.setColour (kCoat); g.fillPath (ear);
    }
    // skull + muzzle (seen from below: big jaw, foreshortened top)
    juce::Path head;
    head.startNewSubPath (P (-0.30f, 0.32f));
    head.quadraticTo (P (0.f, 0.12f), P (0.30f, 0.32f));
    head.quadraticTo (P (0.34f, 0.55f), P (0.16f, 0.72f));
    head.lineTo (P (-0.16f, 0.72f));
    head.quadraticTo (P (-0.34f, 0.55f), P (-0.30f, 0.32f));
    head.closeSubPath();
    g.setColour (kCoat); g.fillPath (head);
    // eyes + tan brows
    for (float side : { -1.f, 1.f })
    {
        g.setColour (kTan); g.fillEllipse (juce::Rectangle<float> (0.06f * w, 0.035f * h).withCentre (P (side * 0.13f, 0.36f)));
        g.setColour (juce::Colour (0xff2A1A10)); g.fillEllipse (juce::Rectangle<float> (0.05f * w, 0.04f * h).withCentre (P (side * 0.13f, 0.42f)));
        g.setColour (kTan); g.fillEllipse (juce::Rectangle<float> (0.12f * w, 0.1f * h).withCentre (P (side * 0.12f, 0.60f)));
    }
    // jaw (opens with the envelope)
    const float jo = jaw * 0.14f;
    juce::Path mouth;
    mouth.addEllipse (juce::Rectangle<float> (0.24f * w, (0.06f + jo) * h).withCentre (P (0.f, 0.74f + jo * 0.5f)));
    g.setColour (juce::Colour (0xff5A1C24)); g.fillPath (mouth);
    g.setColour (juce::Colour (0xffE2788F)); g.fillEllipse (juce::Rectangle<float> (0.12f * w, (0.03f + jo * 0.6f) * h).withCentre (P (0.f, 0.76f + jo * 0.6f)));
    juce::Path lower;
    lower.addRoundedRectangle (juce::Rectangle<float> (0.26f * w, 0.07f * h).withCentre (P (0.f, 0.80f + jo)), 0.03f * w);
    g.setColour (kCoat); g.fillPath (lower);
    g.setColour (kTan); g.fillEllipse (juce::Rectangle<float> (0.18f * w, 0.04f * h).withCentre (P (0.f, 0.81f + jo)));
    // nose
    g.setColour (juce::Colour (0xff050505)); g.fillEllipse (juce::Rectangle<float> (0.12f * w, 0.06f * h).withCentre (P (0.f, 0.68f)));
    // chain collar with glints on peaks
    const int links = 13;
    for (int i = 0; i < links; ++i)
    {
        const float u = (float) i / (float) (links - 1) * 2.f - 1.f;
        const auto c = P (u * 0.34f, 0.93f + (1.f - u * u) * 0.04f);
        juce::Path link; link.addRoundedRectangle (-0.024f * w, -0.014f * h, 0.048f * w, 0.028f * h, 0.012f * w);
        g.setColour (kCollar.darker (0.3f));
        g.strokePath (link, juce::PathStrokeType (0.008f * w), AT::rotation ((i & 1) ? 0.6f : -0.6f).translated (c.x, c.y));
        if (glint > 0.05f && (i % 4 == 1))
        {
            const float gs = 0.035f * w * glint;
            g.setColour (juce::Colours::white.withAlpha (glint));
            g.drawLine (c.x - gs, c.y, c.x + gs, c.y, 1.5f); g.drawLine (c.x, c.y - gs, c.x, c.y + gs, 1.5f);
        }
    }
}

void stickFigure (juce::Graphics& g, juce::Point<float> base, float size, float phase, float bounce, juce::Colour c)
{
    const float s = size;
    const float bob = std::sin (phase * kTwoPi * 2.f) * 0.04f * s * (0.5f + bounce);
    const juce::Point<float> hip (base.x, base.y - s * 0.45f + bob), neck (base.x + std::sin (phase * kTwoPi) * s * 0.05f, base.y - s * 0.82f + bob);
    juce::Path p;
    p.addEllipse (neck.x - s * 0.07f, neck.y - s * 0.18f, s * 0.14f, s * 0.14f);
    p.startNewSubPath (neck); p.lineTo (hip);
    const float a1 = std::sin (phase * kTwoPi) * 1.1f, a2 = std::cos (phase * kTwoPi) * 1.1f;
    p.startNewSubPath (neck); p.lineTo (neck.x + std::sin (a1 + 2.f) * s * 0.22f, neck.y - std::cos (a1 + 2.f) * s * 0.22f);
    p.lineTo (neck.x + std::sin (a1 + 2.6f) * s * 0.38f, neck.y - std::cos (a1 + 2.6f) * s * 0.38f);
    p.startNewSubPath (neck); p.lineTo (neck.x - std::sin (a2 + 2.f) * s * 0.22f, neck.y - std::cos (a2 + 2.f) * s * 0.22f);
    p.lineTo (neck.x - std::sin (a2 + 1.4f) * s * 0.38f, neck.y - std::cos (a2 + 1.4f) * s * 0.38f);
    const float l1 = std::sin (phase * kTwoPi) * 0.5f, l2 = -l1;
    p.startNewSubPath (hip); p.lineTo (hip.x + std::sin (l1) * s * 0.24f, hip.y + s * 0.22f); p.lineTo (hip.x + std::sin (l1 * 1.4f) * s * 0.3f, base.y);
    p.startNewSubPath (hip); p.lineTo (hip.x + std::sin (l2) * s * 0.24f, hip.y + s * 0.22f); p.lineTo (hip.x + std::sin (l2 * 1.4f) * s * 0.3f, base.y);
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (juce::jmax (1.5f, s * 0.022f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}
} // namespace

// ============================================================ scenes
juce::String DogVisualizer::sceneName (int s)
{
    static const char* n[] = { "Studio Portrait", "The Bowl", "Shutter", "Back Seat", "Low Angle", "Sketch", "Dusk Drive" };
    return n[juce::jlimit (0, 6, s)];
}
juce::String DogVisualizer::sceneKey (int s)
{
    static const char* n[] = { "studio_portrait", "the_bowl", "shutter", "back_seat", "low_angle", "sketch", "dusk_drive" };
    return n[juce::jlimit (0, 6, s)];
}

void DogVisualizer::paintScene (juce::Graphics& g, int scene, juce::Rectangle<float> r, const Features& f, const Theme&)
{
    juce::Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (r.toNearestInt());
    const float W = r.getWidth(), H = r.getHeight();
    switch (scene)
    {
        case 0:   // Studio Portrait
        {
            g.setGradientFill (juce::ColourGradient (Palette::ultramarine.brighter (0.25f), r.getCentreX(), r.getY() + H * 0.3f,
                                                     Palette::deepNight, r.getX(), r.getBottom(), true));
            g.fillRect (r);
            const float tilt = juce::jlimit (-0.3f, 0.3f, (f.pitch - 60.f) / 24.f * 0.3f);
            const float s = H * 0.95f;
            const AT tx = AT::scale (s).translated (r.getCentreX() - s * 0.45f, r.getY() + H * 0.18f).rotated (-tilt, r.getCentreX(), r.getBottom());
            drawHead (g, tx, f.env * 0.15f, -0.05f - f.earTwitch * 0.35f, false, 0, kCoat);
        } break;
        case 1:   // The Bowl
        {
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff9FB3C8), r.getX(), r.getY(), juce::Colour (0xff6E7F95), r.getX(), r.getBottom(), false));
            g.fillRect (r);
            // curved tiled concrete with rippling reflections
            const float horizon = r.getY() + H * 0.45f;
            g.setColour (juce::Colour (0xffB8C2CC));
            juce::Path bowl; bowl.startNewSubPath (r.getX(), horizon);
            bowl.quadraticTo (r.getCentreX(), horizon + H * 0.18f, r.getRight(), horizon); bowl.lineTo (r.getRight(), r.getBottom()); bowl.lineTo (r.getX(), r.getBottom()); bowl.closeSubPath();
            g.fillPath (bowl);
            g.setColour (juce::Colour (0x553A4A5E));
            for (int row = 1; row < 9; ++row)
            {
                juce::Path line;
                const float y0 = horizon + (float) (row * row) * H * 0.008f;
                for (int k = 0; k <= 64; ++k)
                {
                    const float x = r.getX() + W * (float) k / 64.f;
                    const float u = (x - r.getCentreX()) / (W * 0.5f);
                    const float y = y0 + (1.f - u * u) * H * 0.12f + f.wave[(size_t) (k & 63)] * 3.f * (float) row * 0.3f;
                    if (k == 0) line.startNewSubPath (x, y); else line.lineTo (x, y);
                }
                g.strokePath (line, juce::PathStrokeType (1.f));
            }
            for (int col = -8; col <= 8; ++col)
            {
                const float x = r.getCentreX() + (float) col * W * 0.07f;
                g.drawLine (x, horizon + H * 0.15f, r.getCentreX() + (float) col * W * 0.16f, r.getBottom(), 1.f);
            }
            const float speed = f.tempo / 120.f;
            const float s = H * 0.42f;
            for (int d = 0; d < 2; ++d)
            {
                float xp = std::fmod (f.time * speed * W * 0.12f + (float) d * W * 0.45f, W + s * 2.f) - s;
                const float gait = f.time * speed * kTwoPi * 1.6f + (float) d;
                drawDog (g, r.getX() + xp, horizon + H * 0.32f + (float) d * H * 0.06f, s, gait, std::sin (gait * 2.f), false, kCoat);
                // reflection
                juce::Graphics::ScopedSaveState s2 (g);
                g.setOpacity (0.18f);
                g.addTransform (AT::verticalFlip (horizon * 2.f + H * 0.66f + (float) d * H * 0.12f));
                drawDog (g, r.getX() + xp, horizon + H * 0.32f + (float) d * H * 0.06f, s, gait, 0, false, kCoat);
            }
        } break;
        case 2:   // Shutter
        {
            g.fillAll (juce::Colour (0xff0B140A));
            const float stripe = H * 0.07f;
            const float off = std::fmod (f.time * 20.f * (0.2f + f.rms * 3.f), stripe);
            const float bright = juce::jlimit (0.f, 1.f, 0.35f + f.rms * 2.5f);
            for (float y = r.getY() - stripe + off; y < r.getBottom(); y += stripe)
            {
                g.setColour (Palette::shutter.withMultipliedBrightness (0.5f + bright * 0.8f));
                g.fillRect (r.getX(), y, W, stripe * 0.7f);
                g.setColour (juce::Colours::black.withAlpha (0.4f)); g.fillRect (r.getX(), y + stripe * 0.7f, W, 1.5f);
            }
            g.setGradientFill (juce::ColourGradient (juce::Colour (0x66FF7A33), r.getRight(), r.getY(), juce::Colours::transparentBlack, r.getCentreX(), r.getBottom(), true));
            g.fillRect (r);
            g.setColour (juce::Colour (0xff1A1E26)); g.fillRect (r.getX(), r.getBottom() - H * 0.14f, W, H * 0.14f);
            drawDog (g, r.getX() + W * 0.12f, r.getBottom() - H * 0.12f, H * 0.62f, 0, 0, true, kCoat, -f.earTwitch * 0.3f);
            drawDog (g, r.getX() + W * 0.55f, r.getBottom() - H * 0.12f, H * 0.6f, 0.3f, std::sin (f.time * 2.f) * f.rms, false, kCoat, -f.earTwitch * 0.3f);
        } break;
        case 3:   // Back Seat
        {
            juce::ColourGradient sky (juce::Colour (0xffFFC27A), r.getX(), r.getY(), juce::Colour (0xffE8492A), r.getX(), r.getBottom(), false);
            g.setGradientFill (sky); g.fillRect (r);
            g.setColour (juce::Colour (0xff6B3A2A));
            for (int k = 0; k < 7; ++k)   // scenery rushing by
            {
                const float x = r.getRight() - std::fmod (f.time * W * 0.6f + (float) k * W * 0.22f, W * 1.4f);
                g.fillEllipse (x, r.getY() + H * 0.35f, W * 0.12f, H * 0.4f);
                g.fillRect (x + W * 0.055f, r.getY() + H * 0.6f, W * 0.01f, H * 0.3f);
            }
            // car door + window frame
            g.setColour (juce::Colour (0xff1D2A4A));
            juce::Path door; door.addRectangle (r.withTop (r.getY() + H * 0.72f));
            door.addRectangle (r.withRight (r.getX() + W * 0.06f));
            door.addRectangle (r.withLeft (r.getRight() - W * 0.06f));
            door.addRectangle (r.withBottom (r.getY() + H * 0.08f));
            g.fillPath (door);
            g.setColour (kCollar.withAlpha (0.8f));
            g.drawLine (r.getX(), r.getY() + H * 0.72f, r.getRight(), r.getY() + H * 0.72f, 3.f);
            const float flap = f.time * kTwoPi * juce::jlimit (0.5f, 12.f, f.lfoRate * 2.f);
            for (int d = 0; d < 2; ++d)
            {
                const float s = H * 0.58f;
                const AT tx = AT::scale (s).translated (r.getX() + W * (0.12f + (float) d * 0.42f), r.getY() + H * 0.24f + (float) d * H * 0.05f);
                drawHead (g, tx, 0.35f, 0.25f + std::sin (flap * 0.5f) * 0.15f, true, flap + (float) d, kCoat);
            }
        } break;
        case 4:   // Low Angle
        {
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4FA3E8), r.getCentreX(), r.getBottom(), juce::Colour (0xff1D4FA8), r.getCentreX(), r.getY(), false));
            g.fillRect (r);
            g.setColour (juce::Colours::white.withAlpha (0.5f));
            for (int k = 0; k < 4; ++k)
            {
                const float x = r.getX() + std::fmod (f.time * 8.f + (float) k * W * 0.3f, W * 1.3f) - W * 0.15f;
                g.fillEllipse (x, r.getY() + H * (0.1f + 0.12f * (float) k), W * 0.22f, H * 0.08f);
            }
            drawHeadFront (g, r.withSizeKeepingCentre (H * 0.95f, H * 0.95f).translated (0, H * 0.06f), f.env, f.glint, f.earTwitch);
        } break;
        case 5:   // Sketch
        {
            g.fillAll (juce::Colour (0xffF5F1E6));
            g.setColour (juce::Colour (0x1423202A));
            for (float y = r.getY() + 14; y < r.getBottom(); y += 14) g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
            stickFigure (g, { r.getX() + W * 0.35f, r.getBottom() - H * 0.06f }, H * 0.8f, f.phase, f.rms * 3.f, Palette::pink.darker (0.1f));
            stickFigure (g, { r.getX() + W * 0.65f, r.getBottom() - H * 0.06f }, H * 0.75f, f.phase + 0.5f, f.rms * 3.f, Palette::pink.darker (0.1f));
        } break;
        default:  // Dusk Drive
        {
            juce::ColourGradient sky (juce::Colour (0xff6A3E9A), r.getX(), r.getY(), juce::Colour (0xffF2A7C3), r.getX(), r.getBottom() - H * 0.2f, false);
            sky.addColour (0.8, juce::Colour (0xffF29A6A));
            g.setGradientFill (sky); g.fillRect (r);
            g.setColour (juce::Colour (0xffFFD9A0).withAlpha (0.9f));
            g.fillEllipse (r.getX() + W * 0.62f, r.getBottom() - H * 0.62f, H * 0.5f, H * 0.5f);
            const juce::Colour sil (0xff2B1D3D);
            const float ground = r.getBottom() - H * 0.1f;
            g.setColour (sil); g.fillRect (r.getX(), ground, W, H * 0.1f);
            // vintage car
            juce::Path car;
            const float cx = r.getX() + W * 0.42f, cw = W * 0.5f, ch = cw * 0.26f;
            car.startNewSubPath (cx, ground - ch * 0.3f); car.lineTo (cx + cw * 0.1f, ground - ch * 0.62f); car.lineTo (cx + cw * 0.32f, ground - ch * 0.68f);
            car.lineTo (cx + cw * 0.44f, ground - ch); car.lineTo (cx + cw * 0.74f, ground - ch); car.lineTo (cx + cw * 0.84f, ground - ch * 0.64f);
            car.lineTo (cx + cw, ground - ch * 0.55f); car.lineTo (cx + cw, ground - ch * 0.15f); car.lineTo (cx, ground - ch * 0.15f); car.closeSubPath();
            g.fillPath (car);
            g.fillEllipse (cx + cw * 0.12f, ground - ch * 0.36f, ch * 0.42f, ch * 0.42f);
            g.fillEllipse (cx + cw * 0.72f, ground - ch * 0.36f, ch * 0.42f, ch * 0.42f);
            const float bob = std::sin (f.time * kTwoPi * f.tempo / 60.f) * f.rms * 2.f;
            drawDog (g, r.getX() + W * 0.04f, ground, H * 0.55f, 0.4f, bob, false, sil);
            drawDog (g, r.getX() + W * 0.2f, ground, H * 0.48f, 0.9f, -bob, false, sil);
        } break;
    }
}

// ============================================================ component
DogVisualizer::DogVisualizer (PureDevotionProcessor& p, int o) : proc (p), osc (o)
{
    setInterceptsMouseClicks (false, false);
    scene = (int) proc.uiState.getProperty ("vis_scene_" + juce::String (osc), osc % numScenes());
}

DogVisualizer::~DogVisualizer() { stopTimer(); }

void DogVisualizer::visibilityChanged()
{
    const bool enabled = (bool) proc.uiState.getProperty ("vis_enabled", true);
    if (isShowing() && enabled) startTimerHz (30); else stopTimer();
}

void DogVisualizer::tick (float dt)
{
    auto& v = proc.feed();
    feat.time += dt;
    feat.rms += (v.rms.load() - feat.rms) * 0.3f;
    feat.env = v.env.load();
    feat.phase = v.oscPhase[osc].load();
    feat.pitch += (v.pitch.load() - feat.pitch) * 0.15f;
    feat.tempo = v.tempo.load();
    feat.lfoRate = v.lfoRate.load();
    const int tr = v.transients.load();
    if (tr != lastTransients) { feat.earTwitch = 1.f; lastTransients = tr; }
    feat.earTwitch *= 0.82f;
    if (v.peak.load() > 0.6f) feat.glint = 1.f;
    feat.glint *= 0.85f;
    // waveform snapshot (decimated scope)
    for (int k = 0; k < 64; ++k) feat.wave[(size_t) k] = v.scope[(size_t) (k * 4)].load();
    opacity = (float) proc.uiState.getProperty ("vis_opacity", 0.35);
    repaint();
}

void DogVisualizer::paint (juce::Graphics& g)
{
    if (! (bool) proc.uiState.getProperty ("vis_enabled", true)) return;
    const float op = fullScreen ? 1.f : juce::jlimit (0.f, 0.6f, opacity);
    if (op <= 0.001f) return;
    g.beginTransparencyLayer (op);
    paintScene (g, scene, getLocalBounds().toFloat(), feat, ThemeManager::current());
    g.endTransparencyLayer();
}
} // namespace pd
