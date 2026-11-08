#include "PluginEditor.h"
#include "Learning.h"
#include "Habits.h"
#include "OniriqueWindowSize.h"
#include <BinaryData.h>

using namespace onirique;

/*  Toutes les mesures sont celles de la démo validée (en points d'une fenêtre de 1600 de large) : carrés de 132 x 96,
    panneau de verre au rayon de 28, polices aux tailles CSS (ui::css), espacement des lettres compris. */

namespace
{
constexpr float kPi = juce::MathConstants<float>::pi;

// la fenêtre : la barre du haut (en points, 1900 à la taille par défaut), l'interface dessous (1600 x 936, comme la démo
// sous son en-tête)
constexpr float kBaseW = 1600.0f, kBaseH = 936.0f;
constexpr float kBarUnits = 1900.0f, kBarH = 50.0f;   // nuit::Bar::heightFor (1900)
constexpr float kHeightPerWidth = kBarH / kBarUnits + kBaseH / kBaseW;
constexpr int kMinW = 1180, kMaxW = 3800;
int heightFor (int width) { return juce::roundToInt ((float) width * kHeightPerWidth); }

// la chaîne : carré 132 x 96, 12 de marge, flèche de 14, 12 de marge
constexpr float kTileW = 132.0f, kTileH = 96.0f, kPitch = 132.0f + 12.0f + 14.0f + 12.0f;

const juce::Colour cBone  { 0xffF4ECEC };   // textes
const juce::Colour cHaze  { 0xffa8939a };   // textes secondaires
const juce::Colour cLabel { 0xffd9c6c9 };
const juce::Colour cBtn   { 0xffd8c6c8 };

// majuscules, accents compris (toUpperCase laisse les lettres accentuées en minuscule)
juce::String upper (juce::String s)
{
    s = s.toUpperCase();
    for (auto [a, b] : { std::pair { "é", "É" }, std::pair { "è", "È" }, std::pair { "ê", "Ê" }, std::pair { "à", "À" }, std::pair { "ç", "Ç" },
                         std::pair { "î", "Î" }, std::pair { "ô", "Ô" }, std::pair { "û", "Û" }, std::pair { "â", "Â" } })
        s = s.replace (juce::String::fromUTF8 (a), juce::String::fromUTF8 (b));
    return s;
}

const char* iconPath (int type)
{
    static const char* p[modules::numTypes] {
        "M2 16h5V7h10v9h5",                                                                                   // gate
        "M2 12h3l2-5 3 10 3-8 2 5 2-2h5M4 20L20 4",                                                           // noise
        "M12 3a4 4 0 0 1 4 4v4a4 4 0 0 1-8 0V7a4 4 0 0 1 4-4zM6 11a6 6 0 0 0 12 0M12 17v4",                  // plosives
        "M2 18h5l2-13 2 13h3l2-7 2 7h4",                                                                      // reso
        "M16 6.5c-1-2.2-7.5-2.4-8 .8-.6 3.7 8.6 3 8.2 7.5-.4 3.6-7.6 3.4-9 .6",                              // de-ess
        "M5 20v-7M5 9V4M12 20v-4M12 12V4M19 20v-9M19 7V4M3 9h4M10 12h4M17 7h4",                               // eq
        "M2 15c3-7 6-7 10 0s7 7 10 0M6 4v3M18 4v3",                                                           // analog
        "M12 3c3.5 4.5 6 7.5 6 11a6 6 0 0 1-12 0c0-2.6 1.6-4.6 2.8-6.3.8 1.8 1.8 2.8 3 2.8 0-2.6-.8-4.6.2-7.5z", // sat
        "M3 17a9 9 0 0 1 18 0M12 17l4.5-6.5M6 13l1 1M18 13l-1 1",                                             // 1176
        "M9 18h6M10 21h4M12 3a6 6 0 0 0-3.8 10.6c.8.7.8 1.6.8 2.4h6c0-.8 0-1.7.8-2.4A6 6 0 0 0 12 3z",         // la-2a
        "M4 20v-6M9.3 20V6M14.7 20v-9M20 20V4",                                                               // multi
        "M3 8h18M3 16h18M17 4l4 4-4 4",                                                                       // parallel
        "M3 12a5.5 5.5 0 1 0 11 0a5.5 5.5 0 1 0-11 0M10 12a5.5 5.5 0 1 0 11 0a5.5 5.5 0 1 0-11 0",           // doubler
        "M2 12c2.5-4 5-4 7.5 0s5 4 7.5 0 3-2 5-2M2 16c2.5-4 5-4 7.5 0s5 4 7.5 0",                            // chorus
        "M2 12h20M5 8c2 2.5 2 5.5 0 8M10 6c2.5 3.5 2.5 8.5 0 12M15 8c2 2.5 2 5.5 0 8",                       // flanger
        "M6 3h5l1 5-3 2a11 11 0 0 0 5 5l2-3 5 1v5a2 2 0 0 1-2 2A17 17 0 0 1 4 5a2 2 0 0 1 2-2z",              // phone
        "M4 6v12M9.5 8v8M14.5 10v4M19.5 11.2v1.6",                                                            // delay
        "M3 19a9 9 0 0 1 18 0M6.5 19a5.5 5.5 0 0 1 11 0M10 19a2 2 0 0 1 4 0",                                 // reverb
        "M12 3l1.8 5.2L19 10l-5.2 1.8L12 17l-1.8-5.2L5 10l5.2-1.8zM19 15l.8 2.2L22 18l-2.2.8L19 21l-.8-2.2L16 18l2.2-.8z",       // creative
        "M4 5h16v14H4zM8 12a4 4 0 1 0 8 0a4 4 0 1 0-8 0M12 12l2.5-2.5M7 16.5h1M16 16.5h1",                    // preamp (un bouton de gain)
        "M3 12h3M18 12h3M8 9.5h8v5H8zM10.5 9.5V7M13.5 9.5V7M12 17v2M9 19h6",                                  // cl1b (la lampe et sa cellule)
        "M3 6h18M3 12h18M3 18h18M5 15c3-6 5-6 7-3s4 3 7-3",                                                  // tuner (la portée et la voix accordée)
        "M2 12h6l1.5-6 1.5 12 1.5-6h9.5M17 5l3 3M20 5l-3 3",                                                  // de-click (le clic barré)
        "M3 9c3 0 4-2 7-2s4 2 7 2h4M3 15c3 0 4-2 7-2s3 1 5 1",                                                // de-breath (le souffle)
        "M3 19a9 9 0 0 1 18 0M8 19a4 4 0 0 1 8 0M4 4l16 16",                                                  // de-reverb (la réverbe barrée)
    };
    return p[juce::jlimit (0, (int) modules::numTypes - 1, type)];
}

// les pictogrammes de power et de retrait (trait de 2.4 sur une grille de 24, comme la démo)
void drawPower (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, float u)
{
    auto p = juce::Drawable::parseSVGPath ("M12 3v8M6.3 7.3a8 8 0 1 0 11.4 0");
    p.applyTransform (juce::AffineTransform::scale (r.getWidth() / 24.0f, r.getHeight() / 24.0f).translated (r.getX(), r.getY()));
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (2.4f * r.getWidth() / 24.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    juce::ignoreUnused (u);
}
void drawCross (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
{
    auto p = juce::Drawable::parseSVGPath ("M6 6l12 12M18 6L6 18");
    p.applyTransform (juce::AffineTransform::scale (r.getWidth() / 24.0f, r.getHeight() / 24.0f).translated (r.getX(), r.getY()));
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (2.4f * r.getWidth() / 24.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// bouton .btn.lit de la démo (dégradé braise, liseré, lueur dessous)
void drawLitButton (juce::Graphics& g, juce::Rectangle<float> r, float lit, float hover, float u)
{
    const float rad = 12.0f * u;
    if (lit > 0.01f)
    {
        juce::Path shape; shape.addRoundedRectangle (r, rad);
        juce::DropShadow (ui::ember.withAlpha (0.45f * lit), juce::roundToInt (16.0f * u), { 0, juce::roundToInt (3.0f * u) }).drawForPath (g, shape);
        g.setGradientFill (juce::ColourGradient (onirique::look::ember.withAlpha (0.55f * lit), 0.0f, r.getY(),
                                                 onirique::look::emberDeep.withAlpha (0.55f * lit), 0.0f, r.getBottom(), false));
        g.fillRoundedRectangle (r, rad);
        g.setColour (onirique::look::emberLit.withAlpha (0.55f * lit));
        g.drawRoundedRectangle (r.reduced (0.5f), rad, 1.0f);
    }
    if (hover > 0.01f && lit < 0.99f)
    {
        g.setColour (juce::Colours::white.withAlpha (0.06f * hover * (1.0f - lit)));
        g.fillRoundedRectangle (r, rad);
    }
}
} // namespace

void ui::drawModuleIcon (juce::Graphics& g, int type, juce::Rectangle<float> r, juce::Colour c, float thickness)
{
    auto path = juce::Drawable::parseSVGPath (iconPath (type));
    path.applyTransform (juce::AffineTransform::scale (r.getWidth() / 24.0f, r.getHeight() / 24.0f).translated (r.getX(), r.getY()));
    g.setColour (c);
    g.strokePath (path, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

//==============================================================================
ChainStrip::ChainStrip (OniriqueVocalBoxAudioProcessor& p) : processor (p)
{
    sync();
}

bool ChainStrip::isOn (int code) const
{
    auto* prm = processor.apvts.getRawParameterValue (ids::inst (modules::info (modules::typeOf (code)).onId, modules::instanceOf (code)));
    return prm != nullptr && prm->load() > 0.5f;
}

// la rangée occupe toute la largeur ; les carrés commencent à 24 du bord (comme la démo), les lueurs ont la place de s'étaler
float ChainStrip::slotX (int index) const { return (24.0f + (float) index * kPitch) * ui::unitOf (*this); }

float ChainStrip::contentWidth() const
{
    const float u = ui::unitOf (*this);
    return tiles.empty() ? 144.0f * u : slotX ((int) tiles.size() - 1) + (kTileW + 12.0f + 96.0f + 24.0f) * u;
}

juce::Rectangle<float> ChainStrip::tileRect (float x) const
{
    const float u = ui::unitOf (*this);
    return { x - scroll, ((float) getHeight() - kTileH * u) * 0.5f, kTileW * u, kTileH * u };
}

juce::Rectangle<float> ChainStrip::plusRect() const
{
    const float u = ui::unitOf (*this);
    const float x = tiles.empty() ? 24.0f * u : slotX ((int) tiles.size() - 1) + (kTileW + 12.0f) * u;
    return { x - scroll, ((float) getHeight() - 96.0f * u) * 0.5f, 96.0f * u, 96.0f * u };
}

// les boutons du haut à droite : 22 x 22, 2 d'écart, 12 du bord
juce::Rectangle<float> ChainStrip::powerRect (const juce::Rectangle<float>& t) const
{
    const float u = ui::unitOf (*this);
    return { t.getRight() - (12.0f + 22.0f + 2.0f + 22.0f) * u, t.getY() + 12.0f * u, 22.0f * u, 22.0f * u };
}

juce::Rectangle<float> ChainStrip::closeRect (const juce::Rectangle<float>& t) const
{
    const float u = ui::unitOf (*this);
    return { t.getRight() - (12.0f + 22.0f) * u, t.getY() + 12.0f * u, 22.0f * u, 22.0f * u };
}

int ChainStrip::tileAt (juce::Point<float> pt) const
{
    for (int i = (int) tiles.size() - 1; i >= 0; --i)
        if (tileRect (tiles[(size_t) i].x).contains (pt)) return i;
    return -1;
}

void ChainStrip::setSelected (int code)
{
    if (code == selected) return;
    selected = code;
    for (int i = 0; i < (int) tiles.size(); ++i) if (tiles[(size_t) i].code == code) reveal (i);
    repaint();
}

// fait défiler la rangée pour que le carré i soit visible
void ChainStrip::reveal (int i)
{
    if (getWidth() <= 0) return;
    const float u = ui::unitOf (*this);
    const float x0 = slotX (i), x1 = x0 + kTileW * u, w = (float) getWidth();
    if (x0 < scrollTarget) scrollTarget = x0 - 8.0f * u;
    else if (x1 > scrollTarget + w) scrollTarget = x1 - w + 8.0f * u;
    scrollTarget = juce::jlimit (0.0f, std::max (0.0f, contentWidth() - w), scrollTarget);
}

void ChainStrip::sync()
{
    const auto chain = processor.getChain();
    std::vector<Tile> next;
    for (int c : chain)
    {
        if (c == 0) continue;
        auto it = std::find_if (tiles.begin(), tiles.end(), [c] (const Tile& t) { return t.code == c; });
        if (it != tiles.end()) next.push_back (*it);
        else { Tile t; t.code = c; t.x = slotX ((int) next.size()); t.on = isOn (c) ? 1.0f : 0.0f; next.push_back (t); }
    }
    const bool changed = next.size() != tiles.size()
                      || ! std::equal (next.begin(), next.end(), tiles.begin(), [] (const Tile& a, const Tile& b) { return a.code == b.code; });
    if (! changed) return;
    const int dragCode = dragTile >= 0 && dragTile < (int) tiles.size() ? tiles[(size_t) dragTile].code : 0;
    tiles = std::move (next);
    dragTile = -1;
    for (int i = 0; i < (int) tiles.size(); ++i) if (tiles[(size_t) i].code == dragCode) dragTile = i;
    hoverTile = -1;
    scrollTarget = juce::jlimit (0.0f, std::max (0.0f, contentWidth() - (float) getWidth()), scrollTarget);
}

void ChainStrip::tick()
{
    sync();
    const float u = ui::unitOf (*this);
    bool moved = false;
    // pendant un glisser près d'un bord : la rangée défile
    if (dragTile >= 0 && dragMoved)
    {
        const float edge = 60.0f * u, x = dragX - scroll + dragOffset;
        if (x < edge) scrollTarget -= 8.0f * u;
        else if (x > (float) getWidth() - edge) scrollTarget += 8.0f * u;
        scrollTarget = juce::jlimit (0.0f, std::max (0.0f, contentWidth() - (float) getWidth()), scrollTarget);
    }
    moved |= ui::approach (scroll, scrollTarget, 0.25f);
    int k = 0;
    for (int i = 0; i < (int) tiles.size(); ++i)
    {
        auto& t = tiles[(size_t) i];
        moved |= ui::approach (t.on, isOn (t.code) ? 1.0f : 0.0f, 0.2f);
        moved |= ui::approach (t.sel, t.code == selected ? 1.0f : 0.0f, 0.2f);
        if (i == dragTile && dragMoved) { moved |= t.x != dragX; t.x = dragX; continue; }
        if (dragTile >= 0 && dragMoved && k == dropIndex) ++k;
        moved |= ui::spring (t.x, t.vel, slotX (k), 0.2f, 0.66f);
        ++k;
        // survol : 0.35 s pour se soulever, 0.9 s pour le reflet (comme la démo)
        moved |= ui::approach (t.hover, i == hoverTile ? 1.0f : 0.0f, 0.16f);
        moved |= ui::approach (t.sheen, i == hoverTile ? 1.0f : 0.0f, 0.07f);
    }
    // les mini-mètres : ce que chaque module retire en ce moment (montée rapide, retombée douce) ; seul le carré qui bouge est
    // redessiné (pendant la lecture, toute la rangée de verre à chaque image coûterait cher)
    juce::Rectangle<int> meterDirty;
    for (auto& t : tiles)
    {
        float red = 0.0f;
        if (auto* m = processor.module (t.code)) red = isOn (t.code) ? m->activityDb() : 0.0f;
        const float before = t.meter;
        t.meter = red > t.meter ? t.meter + 0.6f * (red - t.meter) : t.meter + 0.12f * (red - t.meter);
        if (t.meter < 0.05f) t.meter = 0.0f;
        if (std::abs (t.meter - before) > 0.03f || (before > 0.0f) != (t.meter > 0.0f))
            meterDirty = meterDirty.getUnion (tileRect (t.x).expanded (6.0f * u).getSmallestIntegerContainer());
    }
    if (! moved && ! meterDirty.isEmpty()) repaint (meterDirty);
    moved |= ui::approach (plusGlow, plusHover ? 1.0f : 0.0f, 0.16f);
    if (moved) repaint();
}

void ChainStrip::paint (juce::Graphics& g)
{
    const float u = ui::unitOf (*this);

    // les flèches entre les carrés (chevron de 14, trait #6e5a5e)
    for (int i = 0; i + 1 < (int) tiles.size(); ++i)
    {
        const auto a = tileRect (slotX (i));
        const auto box = juce::Rectangle<float> (14.0f * u, 14.0f * u).withCentre ({ a.getRight() + 19.0f * u, a.getCentreY() });
        auto p = juce::Drawable::parseSVGPath ("M9 6l6 6-6 6");
        p.applyTransform (juce::AffineTransform::scale (box.getWidth() / 24.0f).translated (box.getX(), box.getY()));
        g.setColour (juce::Colour (0xff6e5a5e));
        g.strokePath (p, juce::PathStrokeType (2.4f * box.getWidth() / 24.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    auto paintTile = [&] (int index, const Tile& t, bool lifted)
    {
        auto r = tileRect (t.x);
        const float lift = lifted ? 1.0f : t.hover;
        r = r.translated (0.0f, -4.0f * u * lift);
        r = r.withSizeKeepingCentre (r.getWidth() * (1.0f + 0.03f * lift), r.getHeight() * (1.0f + 0.03f * lift));
        const float rad = 20.0f * u;
        const int type = modules::typeOf (t.code);
        const auto& inf = modules::info (type);
        const float alpha = (lifted ? 0.9f : 1.0f) * (0.42f + 0.58f * t.on);

        // sélection : lueur braise de 36 autour, ombre portée
        if (t.sel > 0.01f)
            ui::softGlowRect (g, r, rad, onirique::look::ember.withAlpha (0.55f * t.sel), 36.0f * u);
        juce::Graphics::ScopedSaveState state (g);
        g.beginTransparencyLayer (alpha);
        g.setColour (juce::Colour (0xff100b0d));
        g.fillRoundedRectangle (r, rad);
        ui::drawDemoGlass (g, r, rad, u, true);
        // liseré : rgba(255,140,150,.35) au survol, rgba(255,110,125,.75) sélectionné (+ anneau intérieur)
        if (t.hover > 0.01f && t.sel < 0.99f)
        {
            g.setColour (onirique::look::emberPink.withAlpha (0.35f * t.hover * (1.0f - t.sel)));
            g.drawRoundedRectangle (r.reduced (0.5f), rad, 1.0f);
        }
        if (t.sel > 0.01f)
        {
            g.setColour (onirique::look::emberLit.withAlpha (0.75f * t.sel));
            g.drawRoundedRectangle (r.reduced (0.5f), rad, 1.0f);
            g.setColour (onirique::look::emberLit.withAlpha (0.5f * t.sel));
            g.drawRoundedRectangle (r.reduced (1.5f), rad - 1.0f, 1.0f);
        }
        // le reflet qui traverse au survol (115°, blanc .16 au milieu)
        if (t.sheen > 0.01f && t.sheen < 0.995f)
        {
            juce::Graphics::ScopedSaveState s (g);
            juce::Path clip; clip.addRoundedRectangle (r, rad);
            g.reduceClipRegion (clip);
            const float cx = r.getX() - r.getWidth() * 0.6f + r.getWidth() * 2.2f * t.sheen;
            juce::ColourGradient sh (juce::Colours::white.withAlpha (0.0f), cx - 0.2f * r.getWidth(), r.getY() + 0.1f * r.getHeight(),
                                     juce::Colours::white.withAlpha (0.0f), cx + 0.2f * r.getWidth(), r.getBottom() - 0.1f * r.getHeight(), false);
            sh.addColour (0.5, juce::Colours::white.withAlpha (0.16f));
            g.setGradientFill (sh);
            g.fillRect (r);
        }

        const float pad = 12.0f * u;
        // témoin 9 x 9 (braise et sa lueur de 10 quand le module tourne)
        const auto led = juce::Rectangle<float> (9.0f * u, 9.0f * u).withPosition (r.getX() + pad, r.getY() + pad + 6.5f * u);
        if (t.on > 0.01f)
            ui::softGlowEllipse (g, led, onirique::look::ember.withAlpha (0.8f * t.on), 10.0f * u);
        g.setColour (ui::ember.interpolatedWith (juce::Colour (0xff4a3a3e), 1.0f - t.on));
        g.fillEllipse (led);

        // le mini-mètre : à droite du témoin, ce que le module retire (0 .. 18 dB), avec sa valeur ; il s'efface au survol
        // (les boutons power et retirer prennent la place)
        if (t.meter > 0.05f && t.hover < 0.99f)
        {
            const float fade = 1.0f - t.hover;
            const auto vf = ui::css (ui::Face::mono, 8.5f * u, 0.06f);
            const juce::String val = juce::String::fromUTF8 ("−") + juce::String (t.meter, t.meter < 10.0f ? 1 : 0);
            const float vw = ui::textWidth (vf, val) + 2.0f;
            const auto track = juce::Rectangle<float> (led.getRight() + 8.0f * u, led.getCentreY() - 1.5f * u,
                                                       r.getRight() - pad - vw - 6.0f * u - (led.getRight() + 8.0f * u), 3.0f * u);
            if (track.getWidth() > 4.0f * u)
            {
                g.setColour (juce::Colours::white.withAlpha (0.07f * fade));
                g.fillRoundedRectangle (track, 1.5f * u);
                const auto fill = track.withWidth (track.getWidth() * juce::jlimit (0.0f, 1.0f, t.meter / 18.0f));
                ui::softGlowRect (g, fill, 1.5f * u, onirique::look::ember.withAlpha (0.45f * fade), 6.0f * u);
                g.setColour (onirique::look::ember.withAlpha (fade));
                g.fillRoundedRectangle (fill, 1.5f * u);
                g.setColour (juce::Colour (0xffcbb6b9).withAlpha (fade));
                g.setFont (vf);
                g.drawText (val, juce::Rectangle<float> (track.getRight() + 6.0f * u, led.getY() - 3.0f * u, vw, led.getHeight() + 6.0f * u),
                            juce::Justification::centredLeft, false);
            }
        }

        // power et retirer, au survol (#cbb6b9, fond blanc .12 sous la souris)
        if (t.hover > 0.01f && ! lifted)
            for (int b = 0; b < 2; ++b)
            {
                const auto br = b == 0 ? powerRect (r) : closeRect (r);
                if (index == hoverTile && hoverButton == b)
                {
                    g.setColour (juce::Colours::white.withAlpha (0.12f * t.hover));
                    g.fillRoundedRectangle (br, 8.0f * u);
                }
                const auto c = (index == hoverTile && hoverButton == b ? juce::Colours::white : juce::Colour (0xffcbb6b9)).withAlpha (t.hover);
                const auto ic = br.withSizeKeepingCentre (12.0f * u, 12.0f * u);
                if (b == 0) drawPower (g, ic, c, u); else drawCross (g, ic, c);
            }

        // en bas : pictogramme 16 + nom (Unbounded 13, 0.04 em), puis le groupe (JetBrains Mono 9, 0.12 em)
        int count = 0, nth = 0;
        for (const auto& o : tiles) if (modules::typeOf (o.code) == type) { ++count; if (o.code == t.code) nth = count; }
        const auto name = juce::String (inf.shortName) + (count > 1 ? " " + juce::String (nth) : juce::String());
        const auto subF = ui::css (ui::Face::mono, 9.0f * u, 0.12f);
        const float subY = r.getBottom() - pad - subF.getHeight();
        const float nameH = 18.0f * u, nameY = subY - 3.0f * u - nameH;
        ui::drawModuleIcon (g, type, juce::Rectangle<float> (16.0f * u, 16.0f * u).withPosition (r.getX() + pad, nameY + (nameH - 16.0f * u) * 0.5f),
                            onirique::look::emberPink, 1.8f * 16.0f * u / 24.0f);
        g.setColour (cBone);
        g.setFont (ui::css (ui::Face::display, 12.0f * u, 0.02f));
        g.drawText (name, juce::Rectangle<float> (r.getX() + pad + 23.0f * u, nameY, r.getWidth() - 2.0f * pad - 23.0f * u, nameH),
                    juce::Justification::centredLeft, false);
        g.setColour (cHaze);
        g.setFont (ui::css (ui::Face::mono, 9.0f * u, 0.12f));
        g.drawText (modules::groupName (inf.group), juce::Rectangle<float> (r.getX() + pad, subY, r.getWidth() - 2.0f * pad, subF.getHeight() + 2.0f),
                    juce::Justification::centredLeft, false);
        g.endTransparencyLayer();
    };

    for (int i = 0; i < (int) tiles.size(); ++i)
        if (i != dragTile || ! dragMoved) paintTile (i, tiles[(size_t) i], false);
    if (dragTile >= 0 && dragMoved)
    {
        // l'endroit où il tombera : le trait braise de la démo (.over), à gauche du carré d'arrivée
        const auto at = tileRect (slotX (dropIndex));
        g.setColour (ui::ember);
        g.fillRoundedRectangle (juce::Rectangle<float> (4.0f * u, at.getHeight() - 8.0f * u).withCentre ({ at.getX() - 6.0f * u, at.getCentreY() }), 2.0f * u);
        paintTile (dragTile, tiles[(size_t) dragTile], true);
    }

    // le + : 96 x 96, croix #ffb3bb ; au survol elle tourne d'un quart de tour, grossit et s'allume
    const auto pr0 = plusRect();
    const auto pr = pr0.withSizeKeepingCentre (pr0.getWidth() * (1.0f + 0.06f * plusGlow), pr0.getHeight() * (1.0f + 0.06f * plusGlow));
    if (plusGlow > 0.01f)
        ui::softGlowRect (g, pr, 20.0f * u, onirique::look::ember.withAlpha (0.55f * plusGlow), 36.0f * u);
    g.setColour (juce::Colour (0xff100b0d));
    g.fillRoundedRectangle (pr, 20.0f * u);
    ui::drawDemoGlass (g, pr, 20.0f * u, u, true);
    if (plusGlow > 0.01f)
    {
        g.setColour (onirique::look::emberLit.withAlpha (0.6f * plusGlow));
        g.drawRoundedRectangle (pr.reduced (0.5f), 20.0f * u, 1.0f);
    }
    {
        const auto c = pr.getCentre();
        const float s = 34.0f * u / 24.0f * 7.0f * (1.0f + 0.06f * plusGlow);
        juce::Path p;
        p.startNewSubPath (c.x, c.y - s); p.lineTo (c.x, c.y + s);
        p.startNewSubPath (c.x - s, c.y); p.lineTo (c.x + s, c.y);
        p.applyTransform (juce::AffineTransform::rotation (plusGlow * kPi * 0.5f, c.x, c.y));
        g.setColour (onirique::look::emberPink.interpolatedWith (juce::Colours::white, 0.25f).interpolatedWith (juce::Colours::white, plusGlow));
        g.strokePath (p, juce::PathStrokeType (2.0f * 34.0f * u / 24.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    if (tiles.empty())
    {
        g.setColour (cHaze);
        g.setFont (ui::css (ui::Face::mono, 11.0f * u, 0.12f));
        g.drawText (juce::String::fromUTF8 ("CLIQUE SUR + POUR AJOUTER UN MODULE À LA CHAÎNE"), pr.withX (pr.getRight() + 24.0f * u).withWidth (800.0f * u),
                    juce::Justification::centredLeft, false);
    }

    // bords qui s'estompent quand la rangée déborde
    const float w = (float) getWidth(), fade = 40.0f * u;
    const auto band = juce::Rectangle<float> (0.0f, (float) getHeight() * 0.5f - 66.0f * u, w, 132.0f * u);
    if (scroll > 1.0f)
    {
        g.setGradientFill (juce::ColourGradient (ui::night, 0.0f, 0.0f, ui::night.withAlpha (0.0f), fade, 0.0f, false));
        g.fillRect (0.0f, band.getY(), fade, band.getHeight());
    }
    if (contentWidth() - scroll > w + 1.0f)
    {
        g.setGradientFill (juce::ColourGradient (ui::night.withAlpha (0.0f), w - fade, 0.0f, ui::night, w, 0.0f, false));
        g.fillRect (w - fade, band.getY(), fade, band.getHeight());
    }
}

void ChainStrip::mouseMove (const juce::MouseEvent& e)
{
    const int h = tileAt (e.position);
    const bool ph = plusRect().contains (e.position);
    int hb = -1;
    if (h >= 0)
    {
        const auto r = tileRect (tiles[(size_t) h].x);
        if (powerRect (r).contains (e.position)) hb = 0; else if (closeRect (r).contains (e.position)) hb = 1;
    }
    if (h != hoverTile || ph != plusHover || hb != hoverButton) { hoverTile = h; plusHover = ph; hoverButton = hb; repaint(); }
    setMouseCursor (hb >= 0 || ph ? juce::MouseCursor::PointingHandCursor : (h >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor));
}

void ChainStrip::mouseExit (const juce::MouseEvent&)
{
    hoverTile = -1; plusHover = false; hoverButton = -1;
}

void ChainStrip::mouseDown (const juce::MouseEvent& e)
{
    downPos = e.position;
    dragMoved = false;
    if (plusRect().contains (e.position)) { if (onAdd) onAdd(); return; }
    const int i = tileAt (e.position);
    if (i < 0) return;
    const auto r = tileRect (tiles[(size_t) i].x);
    const int code = tiles[(size_t) i].code;
    if (powerRect (r).contains (e.position))
    {
        if (auto* p = processor.param (modules::info (modules::typeOf (code)).onId, modules::instanceOf (code)))
            p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f);
        return;
    }
    if (closeRect (r).contains (e.position)) { processor.removeModule (code); return; }
    dragTile = i;
    dragOffset = e.position.x + scroll - tiles[(size_t) i].x;
    dragX = tiles[(size_t) i].x;
    dropIndex = i;
    if (onSelect) onSelect (code);
}

void ChainStrip::mouseDrag (const juce::MouseEvent& e)
{
    if (dragTile < 0) return;
    if (! dragMoved && e.position.getDistanceFrom (downPos) < 6.0f) return;
    dragMoved = true;
    const float step = kPitch * ui::unitOf (*this);
    dragX = juce::jlimit (slotX (0), slotX ((int) tiles.size() - 1), e.position.x + scroll - dragOffset);
    dropIndex = juce::jlimit (0, (int) tiles.size() - 1, juce::roundToInt ((dragX - slotX (0)) / step));
    repaint();
}

void ChainStrip::mouseUp (const juce::MouseEvent&)
{
    if (dragTile >= 0 && dragMoved && dropIndex != dragTile)
    {
        tiles[(size_t) dragTile].x = dragX;
        processor.moveModule (dragTile, dropIndex);
        auto t = tiles[(size_t) dragTile];
        tiles.erase (tiles.begin() + dragTile);
        tiles.insert (tiles.begin() + dropIndex, t);
    }
    dragTile = -1;
    dragMoved = false;
    repaint();
}

void ChainStrip::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    const float d = (std::abs (w.deltaX) > std::abs (w.deltaY) ? -w.deltaX : -w.deltaY) * 400.0f * ui::unitOf (*this);
    scrollTarget = juce::jlimit (0.0f, std::max (0.0f, contentWidth() - (float) getWidth()), scrollTarget + d);
}

juce::String ChainStrip::getTooltip()
{
    if (plusHover) return "Add a module to the chain (up to three of each).";
    if (hoverButton == 0) return "Switch this module on / off.";
    if (hoverButton == 1) return "Remove this module from the chain.";
    if (hoverTile >= 0 && hoverTile < (int) tiles.size())
    {
        // le pourquoi d'AUTO MIX, pour un module qu'il a monté (sa ligne du compte rendu)
        if (const auto why = processor.whyOf (tiles[(size_t) hoverTile].code); why.isNotEmpty()) return juce::String::fromUTF8 ("AUTO MIX : ") + why;
        return "Click to open the module, drag it left or right to change the order.";
    }
    return {};
}

//==============================================================================
ModulePicker::ModulePicker (OniriqueVocalBoxAudioProcessor& p) : processor (p)
{
    setVisible (false);
    setWantsKeyboardFocus (true);
}

void ModulePicker::open() { target = 1.0f; setVisible (true); toFront (true); grabKeyboardFocus(); }
void ModulePicker::close() { target = 0.0f; }

int ModulePicker::freeCount (int type) const
{
    int used = 0;
    for (int x : processor.getChain()) if (modules::typeOf (x) == type) ++used;
    return kInstances - used;
}

namespace
{
constexpr float kCardW = 1240.0f, kPad = 26.0f, kHead = 36.0f, kGroupLabel = 14.0f, kItemH = 72.0f, kGap = 10.0f, kGroupGap = 18.0f;
int rowsOf (int group) { int n = 0; for (int t = 0; t < modules::numTypes; ++t) if (modules::inPicker (t) && modules::info (t).group == group) ++n; return (n + 3) / 4; }
float cardHeight()
{
    float h = kPad + kHead;
    for (int gi = 0; gi < 4; ++gi) h += kGroupGap + kGroupLabel + 8.0f + (float) rowsOf (gi) * kItemH + (float) (rowsOf (gi) - 1) * kGap;
    return h + kPad;
}
} // namespace

juce::Rectangle<float> ModulePicker::card() const
{
    const float u = ui::unitOf (*this);
    return juce::Rectangle<float> (kCardW * u, cardHeight() * u).withCentre (getLocalBounds().toFloat().getCentre())
           .translated (0.0f, 18.0f * u * (1.0f - shown));
}

float ModulePicker::groupY (int group) const
{
    float y = kPad + kHead;
    for (int gi = 0; gi < group; ++gi) y += kGroupGap + kGroupLabel + 8.0f + (float) rowsOf (gi) * kItemH + (float) (rowsOf (gi) - 1) * kGap;
    return y + kGroupGap;
}

juce::Rectangle<float> ModulePicker::itemRect (int type) const
{
    const float u = ui::unitOf (*this);
    const auto c = card();
    const int group = modules::info (type).group;
    int idx = 0;
    if (! modules::inPicker (type)) return {};
    for (int t = 0; t < type; ++t) if (modules::inPicker (t) && modules::info (t).group == group) ++idx;
    const float cw = (kCardW - 2.0f * kPad - 3.0f * kGap) / 4.0f;
    const float x = kPad + (float) (idx % 4) * (cw + kGap), y = groupY (group) + kGroupLabel + 8.0f + (float) (idx / 4) * (kItemH + kGap);
    return { c.getX() + x * u, c.getY() + y * u, cw * u, kItemH * u };
}

juce::Rectangle<float> ModulePicker::closeRect() const
{
    const float u = ui::unitOf (*this);
    const auto c = card();
    return { c.getRight() - (kPad + 44.0f) * u, c.getY() + kPad * u, 44.0f * u, 36.0f * u };
}

int ModulePicker::itemAt (juce::Point<float> p) const
{
    for (int t = 0; t < modules::numTypes; ++t) if (modules::inPicker (t) && itemRect (t).contains (p)) return t;
    return -1;
}

void ModulePicker::tick()
{
    // l'ouverture et la fermeture : tout le menu bouge, il est redessiné ; le survol seul : seulement les cases qui changent
    const bool opening = ui::approach (shown, target, 0.16f);
    juce::Rectangle<int> dirty;
    for (int t = 0; t < modules::numTypes; ++t)
        if (ui::approach (hoverGlow[t], t == hover ? 1.0f : 0.0f, 0.16f))
            dirty = dirty.getUnion (itemRect (t).expanded (20.0f * ui::unitOf (*this)).getSmallestIntegerContainer());
    if (target < 0.5f && shown < 0.01f && isVisible()) { setVisible (false); cache = {}; }
    if (opening) repaint();
    else if (! dirty.isEmpty()) repaint (dirty);
}

// une case du catalogue ; hg : la lueur du survol (0 au repos)
void ModulePicker::paintItem (juce::Graphics& g, int t, float hg, bool overlayOnly) const
{
    const float u = ui::unitOf (*this);
    const auto& inf = modules::info (t);
    const bool free = freeCount (t) > 0;
    const auto r = itemRect (t);
    if (! overlayOnly)
    {
        if (! free) g.beginTransparencyLayer (0.4f);
        ui::drawDemoGlass (g, r, 16.0f * u, u, true);
    }
    if (hg > 0.01f)
    {
        g.setColour (onirique::look::ember.withAlpha (0.16f * hg));
        g.fillRoundedRectangle (r, 16.0f * u);
        g.setColour (onirique::look::emberLit.withAlpha (0.45f * hg));
        g.drawRoundedRectangle (r.reduced (0.5f), 16.0f * u, 1.0f);
    }
    // pastille de 44 : dégradé braise, liseré rose, lueur ; elle grossit au survol
    auto ic = juce::Rectangle<float> (44.0f * u, 44.0f * u).withPosition (r.getX() + 16.0f * u, r.getCentreY() - 22.0f * u);
    ic = ic.withSizeKeepingCentre (ic.getWidth() * (1.0f + 0.06f * hg), ic.getHeight() * (1.0f + 0.06f * hg));
    if (hg > 0.01f || ! overlayOnly) ui::softGlowRect (g, ic, 14.0f * u, onirique::look::ember.withAlpha (0.3f + 0.35f * hg), 16.0f * u);
    juce::ColourGradient grd (onirique::look::ember.withAlpha (0.45f), ic.getX() + ic.getWidth() * 0.3f, ic.getY() + ic.getHeight() * 0.2f,
                              onirique::look::emberDeep.withAlpha (0.35f), ic.getRight() + ic.getWidth() * 0.2f, ic.getBottom() + ic.getHeight() * 0.2f, true);
    g.setColour (onirique::look::litBottom.withMultipliedBrightness (0.55f).withAlpha (1.0f));
    g.fillRoundedRectangle (ic, 14.0f * u);
    g.setGradientFill (grd);
    g.fillRoundedRectangle (ic, 14.0f * u);
    g.setColour (onirique::look::emberPink.withAlpha (0.35f + 0.35f * hg));
    g.drawRoundedRectangle (ic.reduced (0.5f), 14.0f * u, 1.0f);
    ui::drawModuleIcon (g, t, ic.withSizeKeepingCentre (22.0f * u, 22.0f * u), onirique::look::emberPink.interpolatedWith (juce::Colours::white, 0.55f).interpolatedWith (juce::Colours::white, hg),
                        1.8f * 22.0f * u / 24.0f);
    if (overlayOnly) return;

    const auto tr = juce::Rectangle<float> (ic.getRight() + 12.0f * u, r.getY() + 12.0f * u, r.getRight() - ic.getRight() - 28.0f * u, r.getHeight() - 24.0f * u);
    const auto nf = ui::css (ui::Face::display, 13.0f * u);
    g.setColour (cBone);
    g.setFont (nf);
    g.drawText (inf.name, tr.withHeight (nf.getHeight()), juce::Justification::centredLeft, false);
    g.setColour (juce::Colour (0xffcdb8bb));
    g.setFont (ui::css (ui::Face::body, 11.0f * u));
    g.drawFittedText (juce::String::fromUTF8 (free ? inf.desc : "Trois exemplaires déjà dans la chaîne"),
                      tr.withTrimmedTop (nf.getHeight() + 4.0f * u).toNearestInt(), juce::Justification::topLeft, 2, 1.0f);
    if (! free) g.endTransparencyLayer();
}

// le menu au repos (carte, verres, pastilles, textes) : dessiné une fois, puis seulement recopié
void ModulePicker::paintCard (juce::Graphics& g) const
{
    const float u = ui::unitOf (*this);
    const auto c = card();
    const float rad = 30.0f * u;
    g.setColour (juce::Colour (24, 12, 15));
    g.fillRoundedRectangle (c, rad);
    ui::drawDemoGlass (g, c, rad, u, true);
    const auto head = juce::Rectangle<float> (c.getX() + kPad * u, c.getY() + kPad * u, c.getWidth() - 2.0f * kPad * u, kHead * u);
    g.setColour (cBone);
    g.setFont (ui::css (ui::Face::display, 20.0f * u, 0.05f));
    g.drawText ("AJOUTER UN MODULE", head, juce::Justification::centredLeft, false);
    for (int gi = 0; gi < 4; ++gi)
    {
        g.setColour (cHaze);
        g.setFont (ui::css (ui::Face::mono, 10.0f * u, 0.12f));
        g.drawText (modules::groupName (gi), juce::Rectangle<float> (c.getX() + kPad * u, c.getY() + groupY (gi) * u, 400.0f * u, kGroupLabel * u),
                    juce::Justification::centredLeft, false);
    }
    for (int t = 0; t < modules::numTypes; ++t)
        if (modules::inPicker (t)) paintItem (g, t, 0.0f, false);
}

void ModulePicker::paint (juce::Graphics& g)
{
    const float u = ui::unitOf (*this);
    g.setColour (juce::Colour (5, 3, 4).withAlpha (0.66f * shown));
    g.fillAll();
    const auto c = card();
    const bool settled = shown > 0.999f;
    if (settled)
    {
        // la carte en image (refaite si la taille, l'échelle de l'écran ou la chaîne ont changé), puis le survol par-dessus
        const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
        juce::int64 key = (juce::int64) getWidth() * 7919 + getHeight() * 31 + juce::roundToInt (scale * 100.0f);
        for (int x : processor.getChain()) key = key * 131 + x;
        if (! cache.isValid() || key != cacheKey)
        {
            cacheKey = key;
            cache = juce::Image (juce::Image::ARGB, std::max (1, juce::roundToInt ((float) getWidth() * scale)), std::max (1, juce::roundToInt ((float) getHeight() * scale)), true);   // image native : même moteur de rendu (et mêmes couleurs) que l'interface
            juce::Graphics cg (cache);
            cg.addTransform (juce::AffineTransform::scale (scale));
            paintCard (cg);
        }
        g.setOpacity (1.0f);   // l'image prend sinon l'opacité du voile sombre (66 %)
        g.drawImage (cache, getLocalBounds().toFloat());
    }
    else
    {
        g.beginTransparencyLayer (shown);
        paintCard (g);
    }
    for (int t = 0; t < modules::numTypes; ++t)
        if (modules::inPicker (t) && hoverGlow[t] > 0.01f) paintItem (g, t, hoverGlow[t], true);
    const auto cr = closeRect();
    drawLitButton (g, cr, 0.0f, closeHover ? 1.0f : 0.0f, u);
    drawCross (g, cr.withSizeKeepingCentre (13.0f * u, 13.0f * u), closeHover ? juce::Colours::white : cBtn);
    if (! settled) g.endTransparencyLayer();
    juce::ignoreUnused (c);
}

void ModulePicker::mouseMove (const juce::MouseEvent& e)
{
    const int h = itemAt (e.position);
    const bool ch = closeRect().contains (e.position);
    if (h != hover || ch != closeHover)
    {
        if (ch != closeHover) repaint (closeRect().expanded (4.0f).getSmallestIntegerContainer());
        hover = h; closeHover = ch;
        setMouseCursor (h >= 0 || ch ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }
}

void ModulePicker::mouseDown (const juce::MouseEvent& e)
{
    if (! card().contains (e.position) || closeRect().contains (e.position)) { close(); return; }
    const int t = itemAt (e.position);
    if (t >= 0 && freeCount (t) > 0 && onPick) onPick (t);
}

bool ModulePicker::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey) { close(); return true; }
    return false;
}

//==============================================================================
GainStrip::GainStrip (OniriqueVocalBoxAudioProcessor& p, const char* id, const juce::String& t, std::function<float (int)> c)
    : title (t), consume (std::move (c)), knob (p.apvts, id, t)
{
    knob.setShowText (false, false);
    knob.setTooltip (t == "IN" ? "Input gain, before the whole chain (double-click: 0 dB)." : "Output gain, after the whole chain (double-click: 0 dB).");
    addAndMakeVisible (knob);
}

void GainStrip::resized()
{
    const float u = ui::unitOf (*this);
    knob.setBounds (juce::Rectangle<float> (56.0f * u, 56.0f * u).withPosition ((float) getWidth() - (14.0f + 56.0f) * u, ((float) getHeight() - 56.0f * u) * 0.5f).toNearestInt());
}

void GainStrip::tick()
{
    knob.tick();
    float pk = 0.0f;
    for (int c = 0; c < 2; ++c) pk = std::max (pk, consume (c));
    const float db = pk > 1.0e-5f ? 20.0f * std::log10 (pk) : -100.0f;
    level = db > level ? db : std::max (-100.0f, level - 0.5f);
    repaint();
}

void GainStrip::paint (juce::Graphics& g)
{
    const float u = ui::unitOf (*this);
    const auto r = getLocalBounds().toFloat();
    ui::drawDemoGlass (g, r, 22.0f * u, u, true);
    // titre (Unbounded 14), mètre (10 de haut, rayon 5), valeur (JetBrains Mono 12, 74 de large), potard de 56 ; écarts de 14
    g.setColour (cBone);
    g.setFont (ui::css (ui::Face::display, 14.0f * u));
    g.drawText (title, juce::Rectangle<float> (r.getX() + 20.0f * u, r.getY(), 40.0f * u, r.getHeight()), juce::Justification::centredLeft, false);
    const float x0 = r.getX() + (20.0f + 40.0f + 14.0f) * u, x1 = (float) knob.getX() - (14.0f + 74.0f + 14.0f) * u;
    const auto bar = juce::Rectangle<float> (x0, r.getCentreY() - 5.0f * u, std::max (10.0f, x1 - x0), 10.0f * u);
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (bar, 5.0f * u);
    const float n = juce::jlimit (0.0f, 1.0f, (level + 60.0f) / 62.0f);
    if (n > 0.0f)
    {
        juce::ColourGradient gr (onirique::look::emberDeep, bar.getX(), 0.0f, onirique::look::emberPink.interpolatedWith (juce::Colours::white, 0.35f), bar.getRight(), 0.0f, false);
        gr.addColour (0.5, onirique::look::ember);
        g.setGradientFill (gr);
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (bar.withWidth (bar.getWidth() * n).toNearestInt());
        g.fillRoundedRectangle (bar, 5.0f * u);
    }
    g.setColour (cBone);
    g.setFont (ui::css (ui::Face::mono, 12.0f * u, 0.12f));
    g.drawText (level <= -99.0f ? juce::String ("-inf dB") : juce::String (level, 1) + " dB",
                juce::Rectangle<float> (bar.getRight() + 14.0f * u, r.getY(), 74.0f * u, r.getHeight()), juce::Justification::centredRight, false);
}

//==============================================================================
AutoGainSwitch::AutoGainSwitch (OniriqueVocalBoxAudioProcessor& p) : processor (p)
{
    param = p.apvts.getParameter (ids::autoGain);
    attachment = std::make_unique<juce::ParameterAttachment> (*param, [this] (float v) { state = v > 0.5f; repaint(); });
    attachment->sendInitialUpdate();
    lit = state ? 1.0f : 0.0f;
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setTooltip ("Auto gain: keeps the processed voice at the level it came in.");
}

void AutoGainSwitch::tick()
{
    if (ui::approach (lit, state ? 1.0f : 0.0f, 0.16f)) repaint();
}

void AutoGainSwitch::paint (juce::Graphics& g)
{
    const float u = ui::unitOf (*this);
    const auto r = getLocalBounds().toFloat();
    ui::drawDemoGlass (g, r, 22.0f * u, u, true);
    const auto f = ui::css (ui::Face::mono, 11.0f * u, 0.12f);
    const float tw = ui::textWidth (f, "AUTO GAIN"), total = tw + 12.0f * u + 58.0f * u, x = r.getCentreX() - total * 0.5f;
    g.setColour (cLabel);
    g.setFont (f);
    g.drawText ("AUTO GAIN", juce::Rectangle<float> (x, r.getY(), tw + 4.0f, r.getHeight()), juce::Justification::centredLeft, false);
    // l'interrupteur .toggle : 58 x 30, point de 22 qui glisse de 4 à 32
    const auto sw = juce::Rectangle<float> (x + tw + 12.0f * u, r.getCentreY() - 15.0f * u, 58.0f * u, 30.0f * u);
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (sw, 15.0f * u);
    if (lit > 0.01f)
    {
        g.setGradientFill (juce::ColourGradient (onirique::look::ember.withAlpha (0.75f * lit), sw.getX(), 0.0f,
                                                 juce::Colour (140, 16, 32).withAlpha (0.75f * lit), sw.getRight(), 0.0f, false));
        g.fillRoundedRectangle (sw, 15.0f * u);
    }
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawRoundedRectangle (sw.reduced (0.5f), 15.0f * u, 1.0f);
    const auto dot = juce::Rectangle<float> (22.0f * u, 22.0f * u).withPosition (sw.getX() + (4.0f + 28.0f * lit) * u, sw.getY() + 4.0f * u);
    if (lit > 0.01f)
        ui::softGlowEllipse (g, dot, onirique::look::emberPink.withAlpha (0.8f * lit), 12.0f * u);
    g.setColour (juce::Colour (0xffd9c6c9).interpolatedWith (juce::Colours::white, lit));
    g.fillEllipse (dot);
}

void AutoGainSwitch::mouseDown (const juce::MouseEvent&)
{
    attachment->setValueAsCompleteGesture (state ? 0.0f : 1.0f);
}

//==============================================================================
void MixPageButton::paint (juce::Graphics& g)
{
    const float u = ui::unitOf (*this);
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    drawLitButton (g, r, active ? 1.0f : 0.0f, hover ? 1.0f : 0.0f, u);
    if (! active)
    {
        g.setColour (juce::Colour (255, 220, 225).withAlpha (hover ? 0.24f : 0.14f));
        g.drawRoundedRectangle (r, 12.0f * u, 1.0f * u);
    }
    // l'étoile d'AUTO MIX
    juce::Path star;
    const juce::Point<float> c (r.getX() + 20.0f * u, r.getCentreY());
    const float s = 6.5f * u;
    star.startNewSubPath (c.x, c.y - s);
    star.quadraticTo (c.x, c.y, c.x + s, c.y); star.quadraticTo (c.x, c.y, c.x, c.y + s);
    star.quadraticTo (c.x, c.y, c.x - s, c.y); star.quadraticTo (c.x, c.y, c.x, c.y - s);
    g.setColour (active ? juce::Colours::white : ui::ember);
    g.fillPath (star);
    g.setColour (active || hover ? juce::Colours::white : cBtn);
    g.setFont (ui::css (ui::Face::mono, 11.0f * u, 0.14f));
    g.drawText ("AUTO MIX", r.withTrimmedLeft (34.0f * u), juce::Justification::centredLeft, false);
}

VocalBoxView::VocalBoxView (OniriqueVocalBoxAudioProcessor& p)
    : processor (p), strip (p),
      inStrip (p, ids::input, "IN", [&p] (int c) { return p.consumeInputLevel (c); }),
      outStrip (p, ids::output, "OUT", [&p] (int c) { return p.consumeOutputLevel (c); }),
      autoGain (p),
      oversampling (p.apvts, ids::oversampling, { "OFF", "2x", "4x", "8x" }),
      picker (p),
      start (p),
      rate (p)
{
    setLookAndFeel (&lnf);
    setOpaque (true);
    addAndMakeVisible (strip);
    addChildComponent (rate);
    addAndMakeVisible (inStrip);
    addAndMakeVisible (outStrip);
    addAndMakeVisible (autoGain);
    addAndMakeVisible (oversampling);
    oversampling.round = true;
    addChildComponent (start);
    addChildComponent (mixButton);
    addChildComponent (compare);
    addChildComponent (simpleBtn);
    simpleBtn.setState ("MODE SIMPLE", onirique::uipref::simple());
    simpleBtn.onClick = [this]
    {
        onirique::uipref::setSimple (! onirique::uipref::simple());
        simpleBtn.setState ("MODE SIMPLE", onirique::uipref::simple());
        if (! mixPage && panel != nullptr) { const int s = selected; removeChildComponent (panel.get()); panel.reset(); selected = -1; select (s); }
    };
    compare.onClick = [this] { processor.setCompare (! processor.isComparing()); };
    addChildComponent (picker);
    mixButton.onClick = [this] { if (mixPage) select (processor.getChain()[0]); else openMixPage(); };

    // ONR Neural Learning : la page AUTO MIX peut refaire le mix (une voix qui ressemble) ou monter une chaîne de la communauté
    processor.onRemix = [this] (const juce::String& text) { start.remix (text); };
    processor.onApplyChain = [this] (const juce::var& c) { start.applyChain (c); };
    strip.onSelect = [this] (int code) { select (code); };
    strip.onAdd = [this] { picker.open(); };
    picker.onPick = [this] (int type)
    {
        const int code = processor.addModule (type);
        picker.close();
        if (code != 0) { select (code); offerHabit (code); }
    };
    addChildComponent (habitYes);
    addChildComponent (habitNo);
    habitNo.onClick = [this] { closeHabit(); };
    habitYes.onClick = [this]
    {
        const int k = modules::instanceOf (habitCode);
        for (const auto& [id, v] : habitValues)
            if (auto* prm = processor.apvts.getParameter (ids::inst (id.toRawUTF8(), k)))
            {
                prm->beginChangeGesture();
                prm->setValueNotifyingHost (prm->convertTo0to1 (v));
                prm->endChangeGesture();
            }
        closeHabit();
    };

    const auto chain = p.getChain();
    select (processor.apvts.state.getProperty ("selectedModule", chain[0]));
   #if ONIRIQUE_UI_TEST
    // essais locaux : ONIRIQUE_UI_MODULE=<code> ouvre ce module, ONIRIQUE_UI_PICKER=1 ouvre le +
    if (const auto m = juce::SystemStats::getEnvironmentVariable ("ONIRIQUE_UI_MODULE", {}); m.isNotEmpty()) select (m.getIntValue());
    if (juce::SystemStats::getEnvironmentVariable ("ONIRIQUE_UI_PICKER", {}).isNotEmpty()) picker.open();
    // ONIRIQUE_UI_HABIT=1 : les suggestions des habitudes (page AUTO MIX et module affiché)
    if (juce::SystemStats::getEnvironmentVariable ("ONIRIQUE_UI_HABIT", {}).isNotEmpty()) { processor.habitOffer.store (true); offerHabit (selected); }
    // ONIRIQUE_UI_OFFER=voice ou chain : les propositions d'ONR Neural Learning sur la page AUTO MIX
    if (const auto o = juce::SystemStats::getEnvironmentVariable ("ONIRIQUE_UI_OFFER", {}); o == "voice") processor.offerVoice = "SDM";
    else if (o == "chain") processor.offerChain = juce::JSON::parse (R"({"artist":"Tiakola","installs":7,"modules":[]})");
    if (juce::SystemStats::getEnvironmentVariable ("ONIRIQUE_UI_MIXPAGE", {}).isNotEmpty()) openMixPage();
   #endif
    startTimerHz (60);
}

VocalBoxView::~VocalBoxView()
{
    stopTimer();
    processor.onRemix = nullptr;
    processor.onApplyChain = nullptr;
    setLookAndFeel (nullptr);
}

bool VocalBoxView::isOn (int code) const
{
    if (! modules::valid (code)) return false;
    auto* prm = processor.apvts.getRawParameterValue (ids::inst (modules::info (modules::typeOf (code)).onId, modules::instanceOf (code)));
    return prm != nullptr && prm->load() > 0.5f;
}

void VocalBoxView::themeChanged()
{
    backdrop = {};
    start.themeChanged();
    if (panel != nullptr) { const int s = selected; removeChildComponent (panel.get()); panel.reset(); selected = -1; select (s); }
    repaint();
}

// SES RÉGLAGES HABITUELS : un module qu'il a déjà réglé à sa façon (3 séances au moins) arrive avec une proposition, dans
// l'en-tête du module : les deux réglages qui s'écartent le plus de ceux d'usine, APPLIQUER ou NON MERCI
void VocalBoxView::offerHabit (int code)
{
    closeHabit();
    const int type = modules::typeOf (code), k = modules::instanceOf (code);
    auto values = onirique::habits::moduleHabit (type);
    if (values.empty()) return;
    std::vector<std::pair<float, juce::String>> gaps;
    for (const auto& [id, v] : values)
        if (auto* prm = processor.apvts.getParameter (ids::inst (id.toRawUTF8(), k)))
        {
            const float n = prm->convertTo0to1 (v), gap = std::abs (n - prm->getDefaultValue());
            if (gap > 0.02f)
                gaps.push_back ({ gap, upper (prm->getName (40).fromFirstOccurrenceOf (" ", false, false)) + " " + prm->getText (n, 24)
                                       + (prm->getLabel().isNotEmpty() && ! prm->getText (n, 24).containsIgnoreCase (prm->getLabel()) ? " " + prm->getLabel() : juce::String()) });
        }
    if (gaps.empty()) return;
    std::sort (gaps.begin(), gaps.end(), [] (const auto& a, const auto& b) { return a.first > b.first; });
    juce::StringArray top;
    for (size_t i = 0; i < std::min<size_t> (2, gaps.size()); ++i) top.add (gaps[i].second);
    habitValues = std::move (values);
    habitCode = code;
    habitText = juce::String::fromUTF8 ("Tes réglages habituels : ") + top.joinIntoString (juce::String::fromUTF8 (" · ")) + (gaps.size() > 2 ? juce::String::fromUTF8 ("…") : juce::String());
    habitYes.setVisible (true);
    habitNo.setVisible (true);
    resized();
    repaint();
}

void VocalBoxView::closeHabit()
{
    habitCode = 0;
    habitValues.clear();
    habitYes.setVisible (false);
    habitNo.setVisible (false);
    repaint();
}

// la bande de la suggestion : dans l'en-tête du module, à gauche du bouton ON
juce::Rectangle<float> VocalBoxView::habitArea() const
{
    const auto a = panelArea();
    const float u = ui::unitOf (*this);
    const auto pr = headerPower();
    return { a.getX() + a.getWidth() * 0.36f, pr.getY(), pr.getX() - 16.0f * u - (a.getX() + a.getWidth() * 0.36f), pr.getHeight() };
}

float TextPill::idealWidth() const
{
    const float u = ui::unitOf (*this);
    return ui::textWidth (ui::css (ui::Face::mono, 11.0f * u, 0.14f), text) + 36.0f * u;
}

void TextPill::paint (juce::Graphics& g)
{
    const float u = ui::unitOf (*this);
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    drawLitButton (g, r, lit ? 1.0f : 0.0f, hover ? 1.0f : 0.0f, u);
    if (! lit)
    {
        g.setColour (juce::Colour (255, 220, 225).withAlpha (hover ? 0.24f : 0.14f));
        g.drawRoundedRectangle (r, 12.0f * u, 1.0f * u);
    }
    g.setColour (lit || hover ? juce::Colours::white : cBtn);
    g.setFont (ui::css (ui::Face::mono, 11.0f * u, 0.14f));
    g.drawText (text, r, juce::Justification::centred, false);
}

// l'écran AUTO MIX : les macros, affiner en mots, le rôle ; NOUVEAU MIX vide la chaîne (l'écran de départ revient)
void VocalBoxView::openMixPage()
{
    if (! modules::valid (processor.getChain()[0])) return;
    closeHabit();
    mixPage = true;
    mixButton.active = true;
    mixButton.repaint();
    strip.setSelected (0);
    if (panel != nullptr) removeChildComponent (panel.get());
    panel = createMixPage (processor, [this]
    {
        juce::Component::SafePointer<VocalBoxView> safe (this);
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) { safe->mixPage = false; safe->processor.learnFlush ("newmix"); safe->processor.setChain ({}); } });
    });
    addAndMakeVisible (*panel);
    panel->toBehind (&picker);
    panelFade = 0.0f;
    resized();
    repaint();
}

void VocalBoxView::select (int code)
{
    const auto chain = processor.getChain();
    if (std::find (chain.begin(), chain.end(), code) == chain.end()) code = chain[0];
    if (code != habitCode) closeHabit();
    if (mixPage) { mixPage = false; mixButton.active = false; mixButton.repaint(); panel.reset(); selected = -1; }
    if (code == selected && (panel != nullptr || code == 0)) return;
    selected = code;
    processor.apvts.state.setProperty ("selectedModule", code, nullptr);
    strip.setSelected (code);
    panel = createModulePanel (processor, code);
    if (panel != nullptr)
    {
        addAndMakeVisible (*panel);
        panel->toBehind (&picker);
        panelFade = 0.0f;
        resized();
    }
    repaint();
}

// la chaîne a changé ailleurs (preset, A/B, module retiré) : le module affiché suit
void VocalBoxView::syncSelection()
{
    const auto chain = processor.getChain();
    if (mixPage)
    {
        // l'écran AUTO MIX reste tant que la chaîne a des modules ; il retient le premier pour l'en-tête
        if (modules::valid (chain[0])) { selected = std::find (chain.begin(), chain.end(), selected) != chain.end() ? selected : chain[0]; return; }
        mixPage = false; mixButton.active = false; panel.reset(); selected = -1;
    }
    if (std::find (chain.begin(), chain.end(), selected) == chain.end() || selected == 0)
    {
        panel.reset();
        selected = -1;
        select (chain[0]);
        if (chain[0] == 0) { selected = 0; strip.setSelected (0); repaint(); }
    }
}

// le panneau du module : 24 des bords, sous la chaîne (166) jusqu'au-dessus du bas (832)
juce::Rectangle<float> VocalBoxView::panelArea() const
{
    const float u = ui::unitOf (*this);
    return { 24.0f * u, 166.0f * u, (float) getWidth() - 48.0f * u, 666.0f * u };
}

// le bouton ON de l'en-tête du panneau (.btn : 36 de haut, 14 de marge intérieure)
juce::Rectangle<float> VocalBoxView::headerPower() const
{
    const auto a = panelArea();
    const float u = ui::unitOf (*this);
    const float w = 28.0f * u + 12.0f * u + 8.0f * u + ui::textWidth (ui::css (ui::Face::mono, 11.0f * u, 0.14f), "OFF");
    return { a.getRight() - 26.0f * u - w, a.getY() + 22.0f * u, w, 36.0f * u };
}

void VocalBoxView::resized()
{
    const float u = ui::unitOf (*this);
    // la chaîne : rangée de 108 sous son libellé (18 + 14 + 10)
    // (la rangée déborde de 20 en haut et en bas : les lueurs et les carrés soulevés ne sont pas coupés)
    strip.setBounds (juce::Rectangle<float> (0.0f, 22.0f * u, (float) getWidth(), 148.0f * u).toNearestInt());
    oversampling.setBounds (juce::Rectangle<float> (0.0f, 6.0f * u, 236.0f * u, 36.0f * u).withRightX ((float) getWidth() - 24.0f * u - 110.0f * u).toNearestInt());
    const auto a = panelArea();
    if (panel != nullptr)
        panel->setBounds (juce::Rectangle<float> (a.getX() + 26.0f * u, a.getY() + (22.0f + 36.0f + 16.0f) * u, a.getWidth() - 52.0f * u,
                                                  a.getHeight() - (22.0f + 36.0f + 16.0f + 22.0f) * u).toNearestInt());
    // le bas : IN (souple), AUTO GAIN (230), OUT (souple), 18 d'écart, boîtes de 72
    const float fy = (936.0f - 18.0f - 72.0f) * u, fh = 72.0f * u, w = (float) getWidth();
    const float side = (w - 48.0f * u - 230.0f * u - 36.0f * u) * 0.5f;
    inStrip.setBounds (juce::Rectangle<float> (24.0f * u, fy, side, fh).toNearestInt());
    autoGain.setBounds (juce::Rectangle<float> (24.0f * u + side + 18.0f * u, fy, 230.0f * u, fh).toNearestInt());
    outStrip.setBounds (juce::Rectangle<float> (w - 24.0f * u - side, fy, side, fh).toNearestInt());
    start.setBounds (a.toNearestInt());
    mixButton.setBounds (juce::Rectangle<float> (0.0f, 6.0f * u, 132.0f * u, 36.0f * u).withRightX ((float) oversampling.getX() - 14.0f * u).toNearestInt());
    compare.setBounds (juce::Rectangle<float> (0.0f, 6.0f * u, 210.0f * u, 36.0f * u).withRightX ((float) mixButton.getX() - 12.0f * u).toNearestInt());
    simpleBtn.setBounds (juce::Rectangle<float> (0.0f, 6.0f * u, 150.0f * u, 36.0f * u).withRightX ((float) compare.getX() - 12.0f * u).toNearestInt());
    rate.setBounds (juce::Rectangle<float> (0.0f, 6.0f * u, 300.0f * u, 36.0f * u).withRightX ((float) simpleBtn.getX() - 14.0f * u).toNearestInt());
    {
        // la suggestion des réglages habituels : ses deux boutons au bout de sa bande
        auto hb = habitArea();
        habitNo.setBounds (hb.removeFromRight (habitNo.idealWidth()).toNearestInt());
        hb.removeFromRight (8.0f * u);
        habitYes.setBounds (hb.removeFromRight (habitYes.idealWidth()).toNearestInt());
    }
    picker.setBounds (getLocalBounds());
    backdrop = {};
}

void VocalBoxView::timerCallback()
{
    strip.tick();
    {
        // l'écran de départ tant que la chaîne est vide
        const bool empty = ! modules::valid (selected);
        if (start.isVisible() != empty) start.setVisible (empty);
        if (empty) start.tick();
    }
    inStrip.tick();
    outStrip.tick();
    autoGain.tick();
    oversampling.tick();
    picker.tick();
    syncSelection();
    // AUTO MIX vient de monter la chaîne : son écran s'ouvre ; le bouton n'existe qu'avec une chaîne
    if (processor.showMixPage.exchange (false)) openMixPage();
    {
        const bool hasChain = modules::valid (processor.getChain()[0]);
        if (mixButton.isVisible() != hasChain) mixButton.setVisible (hasChain);
        if (compare.isVisible() != hasChain) compare.setVisible (hasChain);
        if (simpleBtn.isVisible() != hasChain) simpleBtn.setVisible (hasChain);
        const bool cmp = processor.isComparing();
        compare.setState (cmp ? juce::String::fromUTF8 ("AVANT · MÊME VOLUME") : juce::String ("COMPARER"), cmp);
    }
    if (panel != nullptr) panel->tick();
    {
        const bool ask = modules::valid (selected) && processor.learnPending() && onirique::learning::enabled();
        if (rate.isVisible() != ask) rate.setVisible (ask);
        if (ask) rate.tick();
    }
    // le module arrive comme dans la démo : il monte de 18 et sort du flou en 0.55 s
    bool moved = ui::approach (panelFade, 1.0f, 0.12f);
    moved |= ui::approach (powerLit, isOn (selected) ? 1.0f : 0.0f, 0.16f);
    moved |= ui::approach (powerHover, overPower ? 1.0f : 0.0f, 0.16f);
    if (panel != nullptr)
    {
        panel->setAlpha (panelFade);
        const float u = ui::unitOf (*this);
        panel->setTransform (juce::AffineTransform::translation (0.0f, 18.0f * u * (1.0f - panelFade)));
    }
    if (moved) repaint (panelArea().toNearestInt().withHeight (juce::roundToInt (80.0f * ui::unitOf (*this))));
}

void VocalBoxView::mouseDown (const juce::MouseEvent& e)
{
    if (! mixPage && headerPower().contains (e.position) && modules::valid (selected))
        if (auto* p = processor.param (modules::info (modules::typeOf (selected)).onId, modules::instanceOf (selected)))
            p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f);
}

void VocalBoxView::mouseMove (const juce::MouseEvent& e)
{
    const bool over = ! mixPage && headerPower().contains (e.position);
    if (over != overPower) { overPower = over; setMouseCursor (over ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor); }
}

void VocalBoxView::paint (juce::Graphics& g)
{
    const float u = ui::unitOf (*this);
    if (! backdrop.isValid() || backdrop.getWidth() != getWidth() || backdrop.getHeight() != getHeight())
    {
        // le fond de la démo : #09080A et trois taches floues (flou de 90) ; la démo les place sous son en-tête de 64
        backdrop = juce::Image (juce::Image::RGB, std::max (1, getWidth()), std::max (1, getHeight()), true, juce::SoftwareImageType());
        juce::Graphics bg (backdrop);
        bg.fillAll (ui::night);
        auto blob = [&] (float x, float y, float w, float h, juce::Colour c)
        {
            const float cx = (x + w * 0.5f) * u, cy = (y + h * 0.5f - 64.0f) * u, rx = (w * 0.5f + 90.0f) * u, ry = (h * 0.5f + 90.0f) * u;
            juce::ColourGradient gr (c, cx, cy, c.withAlpha (0.0f), cx + rx, cy, true);
            gr.addColour (0.55, c.withMultipliedAlpha (0.55f));
            bg.addTransform (juce::AffineTransform::scale (1.0f, ry / rx, cx, cy));
            bg.setGradientFill (gr);
            bg.fillEllipse (cx - rx, cy - rx, 2.0f * rx, 2.0f * rx);
            bg.addTransform (juce::AffineTransform::scale (1.0f, rx / ry, cx, cy));
        };
        blob (-160.0f, -200.0f, 720.0f, 520.0f, onirique::look::emberBlood.withAlpha (0.55f));
        blob (1600.0f + 200.0f - 640.0f, 1000.0f + 220.0f - 520.0f, 640.0f, 520.0f, onirique::look::emberDeep.withAlpha (0.45f));
        blob (640.0f, 380.0f, 420.0f, 320.0f, onirique::look::emberLit.withAlpha (0.10f));
    }
    g.drawImageAt (backdrop, 0, 0);

    // le libellé de la chaîne et le nombre de modules
    const auto chain = processor.getChain();
    int count = 0;
    for (int c : chain) if (c != 0) ++count;
    g.setColour (cHaze);
    g.setFont (ui::css (ui::Face::mono, 10.0f * u, 0.12f));
    const auto lr = juce::Rectangle<float> (24.0f * u, 18.0f * u, (float) getWidth() - 48.0f * u, 14.0f * u);
    {
        // le libellé s'arrête avant les boutons du haut (COMPARER, MODE SIMPLE, la note du mix)
        float stop = lr.getRight();
        for (juce::Component* c : { (juce::Component*) &rate, (juce::Component*) &simpleBtn, (juce::Component*) &compare, (juce::Component*) &mixButton })
            if (c->isVisible()) stop = std::min (stop, (float) c->getX() - 16.0f * u);
        g.drawText (juce::String::fromUTF8 ("CHAÎNE DE TRAITEMENT · GLISSE UN MODULE POUR CHANGER L'ORDRE"), lr.withRight (stop), juce::Justification::centredLeft, true);
    }
    g.drawText (juce::String (count) + (count > 1 ? " MODULES" : " MODULE"), lr, juce::Justification::centredRight, false);

    // le panneau de verre (rayon 28)
    const auto a = panelArea();
    g.setColour (juce::Colour (0xff0f0b0d).withAlpha (0.55f));
    g.fillRoundedRectangle (a, 28.0f * u);
    ui::drawDemoGlass (g, a, 28.0f * u, u, true);
    if (! modules::valid (selected))
    {
        return;   // l'écran de départ (StartScreen) occupe le panneau
    }

    if (mixPage)
    {
        // l'écran AUTO MIX : son étoile, son nom, ce qu'il fait (pas d'interrupteur)
        const float cy = a.getY() + (22.0f + 18.0f) * u;
        const auto dot = juce::Rectangle<float> (10.0f * u, 10.0f * u).withCentre ({ a.getX() + 26.0f * u + 5.0f * u, cy });
        ui::softGlowEllipse (g, dot, onirique::look::ember.withAlpha (0.9f), 12.0f * u);
        g.setColour (ui::ember);
        g.fillEllipse (dot);
        const auto nf = ui::css (ui::Face::display, 22.0f * u, 0.05f);
        const float nx = dot.getRight() + 14.0f * u, nw = ui::textWidth (nf, "AUTO MIX") + 2.0f;
        g.setColour (cBone);
        g.setFont (nf);
        g.drawText ("AUTO MIX", juce::Rectangle<float> (nx, cy - 18.0f * u, nw + 10.0f, 36.0f * u), juce::Justification::centredLeft, false);
        g.setColour (cHaze);
        g.setFont (ui::css (ui::Face::mono, 10.0f * u, 0.12f));
        g.drawText (juce::String::fromUTF8 ("LES MACROS · AFFINER EN MOTS · LE RÔLE DE LA VOIX"), juce::Rectangle<float> (nx + nw + 14.0f * u, cy - 18.0f * u, 800.0f * u, 36.0f * u),
                    juce::Justification::centredLeft, false);
        return;
    }

    // en-tête : point de 10 (lueur 14), nom (Unbounded 22, 0.05 em), ce qu'il fait (JetBrains Mono 10), ON
    const auto& inf = modules::info (modules::typeOf (selected));
    int nth = 0, same = 0;
    for (int c : chain) if (modules::typeOf (c) == modules::typeOf (selected)) { ++same; if (c == selected) nth = same; }
    const auto name = juce::String (inf.name) + (same > 1 ? " " + juce::String (nth) : juce::String());
    const float cy = a.getY() + (22.0f + 18.0f) * u;
    const auto dot = juce::Rectangle<float> (10.0f * u, 10.0f * u).withCentre ({ a.getX() + 26.0f * u + 5.0f * u, cy });
    ui::softGlowEllipse (g, dot, onirique::look::ember.withAlpha (0.9f * powerLit), 12.0f * u);
    g.setColour (ui::ember.interpolatedWith (juce::Colour (0xff4a3a3e), 1.0f - powerLit));
    g.fillEllipse (dot);
    const auto nf = ui::css (ui::Face::display, 22.0f * u, 0.05f);
    const float nx = dot.getRight() + 14.0f * u, nw = ui::textWidth (nf, name) + 2.0f;
    g.setColour (cBone);
    g.setFont (nf);
    g.drawText (name, juce::Rectangle<float> (nx, cy - 18.0f * u, nw + 10.0f, 36.0f * u), juce::Justification::centredLeft, false);
    g.setColour (cHaze);
    g.setFont (ui::css (ui::Face::mono, 10.0f * u, 0.12f));
    g.drawText (upper (juce::String::fromUTF8 (inf.desc)), juce::Rectangle<float> (nx + nw + 14.0f * u, cy - 18.0f * u, (habitYes.isVisible() ? std::max (0.0f, habitArea().getX() - 20.0f * u - (nx + nw + 14.0f * u)) : 800.0f * u), 36.0f * u),
                juce::Justification::centredLeft, false);

    if (habitYes.isVisible() && habitCode == selected)
    {
        // la suggestion : une bande braise, le texte, puis APPLIQUER et NON MERCI
        const auto band = habitArea();
        g.setColour (onirique::look::ember.withAlpha (0.10f));
        g.fillRoundedRectangle (band, 12.0f * u);
        g.setColour (onirique::look::ember.withAlpha (0.35f));
        g.drawRoundedRectangle (band.reduced (0.5f), 12.0f * u, 1.0f * u);
        g.setColour (cBone);
        g.setFont (ui::css (ui::Face::body, 13.0f * u));
        g.drawText (habitText, band.withTrimmedLeft (16.0f * u).withRight ((float) habitYes.getX() - 10.0f * u), juce::Justification::centredLeft, true);
    }

    const auto pr = headerPower();
    drawLitButton (g, pr, powerLit, powerHover, u);
    const auto txt = (powerLit > 0.5f || isOn (selected)) ? juce::String ("ON") : juce::String ("OFF");
    const auto col = powerLit > 0.5f || powerHover > 0.5f ? juce::Colours::white : cBtn;
    drawPower (g, juce::Rectangle<float> (12.0f * u, 12.0f * u).withPosition (pr.getX() + 14.0f * u, pr.getCentreY() - 6.0f * u), col, u);
    g.setColour (col);
    g.setFont (ui::css (ui::Face::mono, 11.0f * u, 0.14f));
    g.drawText (txt, pr.withTrimmedLeft (14.0f * u + 12.0f * u + 8.0f * u), juce::Justification::centredLeft, false);
}

//==============================================================================
OniriqueVocalBoxAudioProcessorEditor::OniriqueVocalBoxAudioProcessorEditor (OniriqueVocalBoxAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), view (p),
      bar (p.presets, p.apvts, "VOCALBOX", juce::ImageCache::getFromMemory (BinaryData::logo_png, BinaryData::logo_pngSize),
           (int) kBarUnits, (int) kBarUnits)
{
    jassert (nuit::Bar::heightFor ((int) kBarUnits) == (int) kBarH);
    onirique::look::load();
   #if ONIRIQUE_UI_TEST
    // essais locaux : ONIRIQUE_UI_THEME=RRGGBB pour une capture dans une autre teinte (sans toucher au choix gardé)
    if (const auto t = juce::SystemStats::getEnvironmentVariable ("ONIRIQUE_UI_THEME", {}); t.isNotEmpty())
        onirique::look::apply (juce::Colour ((juce::uint32) (0xff000000u | (t.getHexValue32() & 0xffffffu))));
   #endif
    bar.onThemeChanged = [this] { refreshTheme(); };
    // le menu ⋯ : ONR NEURAL LEARNING, l'apprentissage d’AUTO MIX (désactivé tant que l'utilisateur ne l'allume pas)
    bar.setMenuOption ("ONR NEURAL LEARNING", juce::String::fromUTF8 ("HELP VOCALBOX LEARN"),
                       juce::String::fromUTF8 ("Anonymous: your request, voice measurements, the chain, what you keep or change. Never audio."),
                       [] { return onirique::learning::enabled(); },
                       [] (bool on) { onirique::learning::setEnabled (on); });
    startTimer (1000);
    setOpaque (true);
    addAndMakeVisible (view);
    addAndMakeVisible (bar);

    // le titre de la démo (l'étoile à quatre branches, ONIRIQUE VOCALBOX) à la place de la marque de la barre
    bar.setBrand (juce::roundToInt (titleWidth()), [this] (juce::Graphics& g, juce::Rectangle<float> area) { paintTitle (g, area); });

    // Licence : pastille (dans la barre) et page d'activation, par-dessus tout le reste.
    licenseOverlay = std::make_unique<license::Overlay> ("ONIRIQUE VOCALBOX", processor.license);
    addAndMakeVisible (*licenseOverlay);

    // redimensionnable, proportions verrouillées : aucune déformation, aucun texte coupé
    const int w = window::openingSize (processor.apvts.state, "ONIRIQUE VOCALBOX", { 1900, heightFor (1900) },
                                       { kMinW, heightFor (kMinW) }, { kMaxW, heightFor (kMaxW) }).x;
    setResizable (true, true);
    setResizeLimits (kMinW, heightFor (kMinW), kMaxW, heightFor (kMaxW));
    if (auto* c = getConstrainer()) c->setFixedAspectRatio (1.0 / (double) kHeightPerWidth);
    setSize (w, heightFor (w));
    restoringSize = false;
    window::remember (processor.apvts.state, getWidth(), getHeight());
    resized();

   #if ONIRIQUE_UI_TEST
    // essais locaux uniquement : sans la page d'activation, et une image de l'interface (ONIRIQUE_UI_PNG=fichier.png,
    // rendue x2 par le moteur logiciel de JUCE)
    const auto test = juce::SystemStats::getEnvironmentVariable ("ONIRIQUE_UI_TEST", {});
    if (test.contains ("nolicense")) licenseOverlay->setVisible (false);
    if (test.contains ("menu")) { juce::Component::SafePointer<OniriqueVocalBoxAudioProcessorEditor> s (this); juce::Timer::callAfterDelay (600, [s] { if (s != nullptr) s->bar.openMenu(); }); }
    if (const auto png = juce::SystemStats::getEnvironmentVariable ("ONIRIQUE_UI_PNG", {}); png.isNotEmpty())
    {
        juce::Component::SafePointer<OniriqueVocalBoxAudioProcessorEditor> safe (this);
        const int delay = juce::jmax (500, juce::SystemStats::getEnvironmentVariable ("ONIRIQUE_UI_PNG_DELAY", "2500").getIntValue());
        juce::Timer::callAfterDelay (delay, [safe, png]
        {
            if (safe == nullptr) return;
            juce::Image img (juce::Image::ARGB, safe->getWidth() * 2, safe->getHeight() * 2, true, juce::SoftwareImageType());
            {
                juce::Graphics g2 (img);
                g2.addTransform (juce::AffineTransform::scale (2.0f));
                safe->paintEntireComponent (g2, true);
            }
            juce::File f (png);
            f.deleteFile();
            if (auto os = f.createOutputStream()) juce::PNGImageFormat().writeImageToStream (img, *os);
        });
    }
   #endif
}

OniriqueVocalBoxAudioProcessorEditor::~OniriqueVocalBoxAudioProcessorEditor()
{
    stopTimer();
    window::rememberOnThisComputer ("ONIRIQUE VOCALBOX", getWidth(), getHeight());
}

void OniriqueVocalBoxAudioProcessorEditor::resized()
{
    const float u = (float) getWidth() / kBaseW, b = (float) getWidth() / kBarUnits;
    view.setBounds (0, juce::roundToInt (kBarH * b), getWidth(), juce::roundToInt (kBaseH * u));

    bar.setPillWidth (licenseOverlay != nullptr ? licenseOverlay->pillWidth() : 0);
    bar.place (b, getLocalBounds());
    if (licenseOverlay != nullptr)
        licenseOverlay->fit (getLocalBounds(), b, bar.getPillArea(), juce::Justification::centredRight);

    if (! restoringSize)
        window::remember (processor.apvts.state, getWidth(), getHeight());   // taille mémorisée avec le projet
}

// le titre de la démo, en points de la barre : étoile de 34 (#F4ECEC), ONIRIQUE (Unbounded 19, 0.08 em), VOCALBOX en braise
float OniriqueVocalBoxAudioProcessorEditor::titleWidth() const
{
    const auto f = ui::css (ui::Face::display, 19.0f * 0.9f, 0.08f);
    return 30.0f + 12.0f + ui::textWidth (f, "ONIRIQUE ") + ui::textWidth (f, "VOCALBOX") + 6.0f;
}

void OniriqueVocalBoxAudioProcessorEditor::paintTitle (juce::Graphics& g, juce::Rectangle<float> area) const
{
    const float s = 30.0f;
    const auto star = juce::Rectangle<float> (s, s).withPosition (area.getX(), kBarH * 0.5f - s * 0.5f);
    auto p = juce::Drawable::parseSVGPath ("M20 2 C21.5 15 25 18.5 38 20 C25 21.5 21.5 25 20 38 C18.5 25 15 21.5 2 20 C15 18.5 18.5 15 20 2Z");
    p.applyTransform (juce::AffineTransform::scale (s / 40.0f).translated (star.getX(), star.getY()));
    g.setColour (juce::Colour (0xffF4ECEC));
    g.fillPath (p);
    const auto f = ui::css (ui::Face::display, 19.0f * 0.9f, 0.08f);
    const float x = star.getRight() + 12.0f, w1 = ui::textWidth (f, "ONIRIQUE ");
    g.setFont (f);
    g.drawText ("ONIRIQUE", juce::Rectangle<float> (x, 0.0f, w1, kBarH), juce::Justification::centredLeft, false);
    g.setColour (ui::ember);
    g.drawText ("VOCALBOX", juce::Rectangle<float> (x + w1, 0.0f, ui::textWidth (f, "VOCALBOX") + 10.0f, kBarH), juce::Justification::centredLeft, false);
}

void OniriqueVocalBoxAudioProcessorEditor::refreshTheme()
{
    view.themeChanged();
    // les potards et les panneaux gardés en image (setBufferedToImage) ne se redessinent pas avec leur parent : sans ceci,
    // ils gardaient l'ancienne couleur jusqu'au passage de la souris
    std::function<void (juce::Component&)> all = [&all] (juce::Component& c) { c.repaint(); for (auto* ch : c.getChildren()) all (*ch); };
    all (*this);
}

// une fois par seconde : la couleur a-t-elle été changée dans un autre plugin ONIRIQUE ?
void OniriqueVocalBoxAudioProcessorEditor::timerCallback()
{
    if (onirique::look::poll()) refreshTheme();
}

//==============================================================================
juce::Rectangle<float> RateChip::yesArea() const
{
    const float u = ui::unitOf (*this);
    const auto r = getLocalBounds().toFloat().reduced (4.0f * u);
    return r.withLeft (r.getRight() - 128.0f * u).withWidth (60.0f * u);
}

juce::Rectangle<float> RateChip::noArea() const
{
    const float u = ui::unitOf (*this);
    const auto r = getLocalBounds().toFloat().reduced (4.0f * u);
    return r.withLeft (r.getRight() - 64.0f * u);
}

void RateChip::tick()
{
    const float t = (float) (juce::Time::getMillisecondCounterHiRes() * 0.001);
    glow = 0.5f + 0.5f * std::sin (t * 2.0f);   // elle respire doucement : une question en attente
    repaint();
}

void RateChip::mouseDown (const juce::MouseEvent& e)
{
    if (yesArea().contains (e.position)) processor.learnSend (1);
    else if (noArea().contains (e.position)) processor.learnSend (-1);
    setVisible (false);
}

void RateChip::paint (juce::Graphics& g)
{
    const float u = ui::unitOf (*this);
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    juce::Path pill; pill.addRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (juce::Colour (0xff120c0e).withAlpha (0.9f));
    g.fillPath (pill);
    ui::drawDemoGlass (g, r, r.getHeight() * 0.5f, u, false);
    const auto& ember = onirique::look::ember;
    const auto dot = juce::Rectangle<float> (6.0f * u, 6.0f * u).withCentre ({ r.getX() + 16.0f * u, r.getCentreY() });
    ui::softGlowEllipse (g, dot, ember.withAlpha (0.4f + 0.4f * glow), 7.0f * u);
    g.setColour (ember);
    g.fillEllipse (dot);
    g.setColour (juce::Colour (0xffF4ECEC));
    g.setFont (ui::css (ui::Face::mono, 10.5f * u, 0.14f));
    g.drawText (juce::String::fromUTF8 ("CE MIX TE PLAÎT ?"), r.withTrimmedLeft (28.0f * u).withRight (yesArea().getX() - 4.0f * u),
                juce::Justification::centredLeft, true);
    for (int k = 0; k < 2; ++k)
    {
        const auto b = (k == 0 ? yesArea() : noArea()).reduced (0.0f, 2.0f * u);
        const bool hot = over == (k == 0 ? 1 : -1);
        if (hot)
        {
            g.setGradientFill (juce::ColourGradient (ember.withAlpha (0.6f), 0.0f, b.getY(), onirique::look::emberDeep.withAlpha (0.6f), 0.0f, b.getBottom(), false));
            g.fillRoundedRectangle (b, b.getHeight() * 0.5f);
        }
        g.setColour (juce::Colours::white.withAlpha (hot ? 0.0f : 0.08f));
        g.drawRoundedRectangle (b.reduced (0.5f), b.getHeight() * 0.5f, 1.0f);
        g.setColour (juce::Colour (0xffF4ECEC));
        g.setFont (ui::css (ui::Face::mono, 10.0f * u, 0.14f));
        g.drawText (k == 0 ? "OUI" : "BOF", b, juce::Justification::centred, false);
    }
}
