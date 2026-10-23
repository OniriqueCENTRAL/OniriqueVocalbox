#pragma once

#include <juce_graphics/juce_graphics.h>

//==============================================================================
//  Le potard ONIRIQUE « RONCE », le même dans tous les plugins de la marque (inspiré du logo).
//
//  - Une piste fine ; l'arc de valeur, rouge, pousse des épines comme une ronce (une vers l'extérieur, une vers
//    l'intérieur, en alternance) : celles qui sont allumées vont jusqu'à la valeur, plus vives près d'elle (depuis le
//    milieu pour un réglage bipolaire). Les autres restent en trait pâle.
//  - Un corps noir, éclairé d'en haut à gauche, qui porte un anneau de métal liquide blanc, irrégulier et épineux,
//    avec sa lueur ; l'anneau tourne avec le réglage.
//  - L'aiguille : une épine rouge qui part de l'anneau vers le centre. Le centre reste vierge.
//
//  Ce fichier se copie tel quel dans chaque plugin ONIRIQUE, et ne se modifie qu'ici (plugin-ui/ du dépôt ONIRIQUE
//  CENTRAL). Les plugins des partenaires (ABRASION, RELIC) gardent leur propre style.
//
//      onirique::ui::drawKnob (g, zone, position, angleDebut, angleFin, accent, bipolaire, survolé);
//==============================================================================
namespace onirique::ui
{

/** L'étoile à quatre branches d'ONIRIQUE, centrée en `c`, de rayon `r`. */
inline juce::Path knobStar (juce::Point<float> c, float r)
{
    juce::Path p;
    const float w = r * 0.085f;
    p.startNewSubPath (c.x, c.y - r);
    p.quadraticTo (c.x + w, c.y - w, c.x + r, c.y);
    p.quadraticTo (c.x + w, c.y + w, c.x, c.y + r);
    p.quadraticTo (c.x - w, c.y + w, c.x - r, c.y);
    p.quadraticTo (c.x - w, c.y - w, c.x, c.y - r);
    p.closeSubPath();
    return p;
}

namespace knobdetail
{
/** Une épine : un trapèze effilé le long du rayon d'angle `a`, de r1 (largeur w1) à r2 (largeur w2). */
inline void addThorn (juce::Path& p, juce::Point<float> c, float a, float r1, float r2, float w1, float w2)
{
    const float sx = std::sin (a), cy = std::cos (a);
    const auto at = [&] (float r, float side) { return juce::Point<float> (c.x + r * sx + side * cy, c.y - r * cy + side * sx); };
    p.startNewSubPath (at (r1, w1 * 0.5f));
    p.lineTo (at (r2, w2 * 0.5f));
    p.lineTo (at (r2, -w2 * 0.5f));
    p.lineTo (at (r1, -w1 * 0.5f));
    p.closeSubPath();
}

/** Plusieurs passes de plus en plus larges et transparentes : la lueur d'un trait ou d'un contour. */
inline void glowStroke (juce::Graphics& g, const juce::Path& p, juce::Colour c, float width, float intensity)
{
    if (intensity <= 0.003f || p.isEmpty()) return;
    for (int i = 3; i >= 1; --i)
    {
        g.setColour (c.withAlpha (intensity * 0.16f / (float) i));
        g.strokePath (p, juce::PathStrokeType (width * (1.0f + 1.6f * (float) i), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}
} // namespace knobdetail

/** Dessine le potard dans `area` (un carré y est pris, centré). `position` : 0 à 1. Les angles sont ceux de JUCE
    (radians, 0 en haut, sens des aiguilles d'une montre). Toutes les mesures sont données pour un potard de 240 px. */
inline void drawKnob (juce::Graphics& g, juce::Rectangle<float> area, float position, float startAngle, float endAngle,
                      juce::Colour accent, bool bipolar, bool hover)
{
    using namespace juce;
    using knobdetail::addThorn;

    const auto size = jmin (area.getWidth(), area.getHeight());
    if (size < 8.0f)
        return;

    const auto c = area.getCentre();
    const float s = size / 240.0f;
    const float angle = startAngle + jlimit (0.0f, 1.0f, position) * (endAngle - startAngle);
    const float origin = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
    const float lo = jmin (origin, angle), hi = jmax (origin, angle);
    const bool any = hi - lo > 0.001f;
    const bool down = bipolar && angle < origin;
    const auto hot = accent.interpolatedWith (Colours::white, 0.38f);
    const auto pale = Colour (0xffFFD2D6), silver = Colour (0xffF2E2E4);
    const bool small = size < 62.0f;
    const float glowAmt = hover ? 1.0f : 0.7f;

    // ---- la piste, puis l'arc allumé et sa lueur
    const float rA = 98.0f * s, arcW = jmax (1.2f, 2.6f * s);
    Path track;
    track.addCentredArc (c.x, c.y, rA, rA, 0.0f, startAngle, endAngle, true);
    g.setColour (pale.withAlpha (0.08f));
    g.strokePath (track, PathStrokeType (jmax (1.0f, 2.0f * s), PathStrokeType::curved, PathStrokeType::rounded));

    Path lit;
    if (any)
    {
        lit.addCentredArc (c.x, c.y, rA, rA, 0.0f, lo, hi, true);
        knobdetail::glowStroke (g, lit, accent, arcW, glowAmt);
    }

    // ---- les épines de la ronce : une sur deux vers l'extérieur, les autres vers l'intérieur
    const int count = small ? 10 : 20;
    Path off, on, near;
    for (int i = 0; i <= count; ++i)
    {
        const float a = startAngle + (float) i / (float) count * (endAngle - startAngle);
        const bool outward = i % 2 == 0;
        const bool isLit = any && a >= lo - 1.0e-4f && a <= hi + 1.0e-4f;
        const float closeness = (down ? (hi - a) : (a - lo)) / jmax (1.0e-4f, hi - lo);
        Path& dst = ! isLit ? off : (closeness > 0.8f ? near : on);
        if (outward) addThorn (dst, c, a, 99.0f * s, 112.0f * s, jmax (1.6f, 4.0f * s), 0.25f * s);
        else         addThorn (dst, c, a, 97.0f * s, 88.0f * s, jmax (1.3f, 3.2f * s), 0.25f * s);
    }
    g.setColour (pale.withAlpha (0.14f));
    g.fillPath (off);
    if (any)
    {
        knobdetail::glowStroke (g, on, accent, 1.2f * s, glowAmt * 0.8f);
        knobdetail::glowStroke (g, near, accent, 1.2f * s, glowAmt);
        g.setColour (accent);
        g.strokePath (lit, PathStrokeType (arcW, PathStrokeType::curved, PathStrokeType::rounded));
        g.fillPath (on);
        g.setColour (hot);
        g.fillPath (near);
    }

    // ---- le corps : ombre portée, disque noir éclairé d'en haut à gauche
    const float rB = 72.0f * s;
    {
        const auto sc = c.translated (0.0f, 6.0f * s);
        ColourGradient shadow (Colours::black.withAlpha (0.6f), sc.x, sc.y, Colours::black.withAlpha (0.0f), sc.x + rB * 1.22f, sc.y, true);
        g.setGradientFill (shadow);
        g.fillEllipse (Rectangle<float> (2.44f * rB, 2.44f * rB).withCentre (sc));
    }
    const auto body = Rectangle<float> (2.0f * rB, 2.0f * rB).withCentre (c);
    ColourGradient face (Colour (0xff24110f), c.x - rB * 0.16f, c.y - rB * 0.40f, Colour (0xff070304), c.x + rB * 0.75f, c.y + rB * 0.85f, true);
    face.addColour (0.55, Colour (0xff10070a));
    g.setGradientFill (face);
    g.fillEllipse (body);
    g.setColour (Colours::black.withAlpha (0.8f));
    g.drawEllipse (body, jmax (1.0f, s));

    // ---- l'anneau de métal liquide (il tourne avec le réglage), ses épines et sa lueur blanche
    Path ring;
    {
        const int n = small ? 48 : 96;
        for (int pass = 0; pass < 2; ++pass)
        {
            for (int k = 0; k <= n; ++k)
            {
                const float th = (float) k / (float) n * MathConstants<float>::twoPi;
                const float r = pass == 0 ? 64.0f + 1.6f * std::sin (4.0f * th + 0.5f) + 1.0f * std::sin (9.0f * th)
                                          : 60.0f + 1.2f * std::sin (1.0f - 6.0f * th);
                const float t = angle + (pass == 0 ? th : -th);
                const Point<float> p (c.x + r * s * std::sin (t), c.y - r * s * std::cos (t));
                if (k == 0) ring.startNewSubPath (p); else ring.lineTo (p);
            }
            ring.closeSubPath();
        }
        ring.setUsingNonZeroWinding (false);
    }
    Path vineThorns;
    {
        const float at[] { 45.0f, 100.0f, 160.0f, 215.0f, 280.0f, 330.0f };
        for (int i = 0; i < 6; ++i)
        {
            const float a = angle + degreesToRadians (at[i]);
            if (i % 2 == 1) addThorn (vineThorns, c, a, 64.0f * s, 73.0f * s, 3.2f * s, 0.25f * s);
            else            addThorn (vineThorns, c, a, 61.0f * s, 53.0f * s, 3.0f * s, 0.25f * s);
        }
    }
    knobdetail::glowStroke (g, ring, Colours::white, 3.0f * s, hover ? 0.55f : 0.4f);
    g.setColour (silver);
    g.fillPath (ring);
    g.fillPath (vineThorns);

    // ---- l'aiguille : une épine rouge de l'anneau vers le centre
    Path needle;
    addThorn (needle, c, angle, 60.0f * s, 30.0f * s, jmax (1.8f, 4.2f * s), 0.3f * s);
    knobdetail::glowStroke (g, needle, accent, 2.0f * s, glowAmt);
    g.setColour (hot);
    g.fillPath (needle);
}

} // namespace onirique::ui
