#include "PluginEditor.h"
#include "OniriqueWindowSize.h"
#include <BinaryData.h>

using namespace onirique;

namespace
{
constexpr float kPi = juce::MathConstants<float>::pi;

// la fenêtre : la barre du haut (en points, 1900 à la taille par défaut), l'interface dessous (en unités de 1600 x 780)
constexpr float kBaseW = 1600.0f, kBaseH = 780.0f;
constexpr float kBarUnits = 1900.0f, kBarH = 50.0f;   // nuit::Bar::heightFor (1900)
constexpr float kHeightPerWidth = kBarH / kBarUnits + kBaseH / kBaseW;
constexpr int kMinW = 1180, kMaxW = 3800;
int heightFor (int width) { return juce::roundToInt ((float) width * kHeightPerWidth); }

// le titre, à la hauteur de la barre
constexpr float kTitleScale = 0.6f;
const float kLogoW = 64.0f * 985.0f / 895.0f;

// les pictogrammes des modules (tracés sur une grille de 24), ceux de la démo validée
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
    };
    return p[juce::jlimit (0, (int) modules::numTypes - 1, type)];
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
    for (auto& t : tiles) t.appear = 1.0f;
}

bool ChainStrip::isOn (int code) const
{
    auto* prm = processor.apvts.getRawParameterValue (ids::inst (modules::info (modules::typeOf (code)).onId, modules::instanceOf (code)));
    return prm != nullptr && prm->load() > 0.5f;
}

float ChainStrip::tileW() const
{
    const float u = ui::unitOf (*this);
    const float gap = 12.0f * u, plus = 96.0f * u;
    const float avail = (float) getWidth() - plus - gap;
    const int n = std::max (1, (int) tiles.size());
    return std::min (124.0f * u, avail / (float) n - gap);
}

float ChainStrip::slotX (int index) const { return (float) index * (tileW() + 12.0f * ui::unitOf (*this)); }

juce::Rectangle<float> ChainStrip::tileRect (float x) const
{
    const float u = ui::unitOf (*this);
    return { x, 4.0f * u, tileW(), (float) getHeight() - 8.0f * u };
}

juce::Rectangle<float> ChainStrip::plusRect() const
{
    const float u = ui::unitOf (*this);
    const float x = tiles.empty() ? 0.0f : slotX ((int) tiles.size());
    const float h = (float) getHeight() - 8.0f * u;
    return { x, 4.0f * u, std::min (96.0f * u, h), h };
}

juce::Rectangle<float> ChainStrip::powerRect (const juce::Rectangle<float>& t) const
{
    const float u = ui::unitOf (*this);
    return { t.getRight() - 52.0f * u, t.getY() + 8.0f * u, 22.0f * u, 22.0f * u };
}

juce::Rectangle<float> ChainStrip::closeRect (const juce::Rectangle<float>& t) const
{
    const float u = ui::unitOf (*this);
    return { t.getRight() - 28.0f * u, t.getY() + 8.0f * u, 22.0f * u, 22.0f * u };
}

int ChainStrip::tileAt (juce::Point<float> pt) const
{
    for (int i = (int) tiles.size() - 1; i >= 0; --i)
        if (tileRect (tiles[(size_t) i].x).contains (pt)) return i;
    return -1;
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
        else { Tile t; t.code = c; t.x = slotX ((int) next.size()); next.push_back (t); }
    }
    const bool changed = next.size() != tiles.size()
                      || ! std::equal (next.begin(), next.end(), tiles.begin(), [] (const Tile& a, const Tile& b) { return a.code == b.code; });
    if (changed)
    {
        // le carré tiré garde sa place dans la nouvelle liste
        const int dragCode = dragTile >= 0 && dragTile < (int) tiles.size() ? tiles[(size_t) dragTile].code : 0;
        tiles = std::move (next);
        dragTile = -1;
        for (int i = 0; i < (int) tiles.size(); ++i) if (tiles[(size_t) i].code == dragCode) dragTile = i;
        hoverTile = -1;
    }
}

void ChainStrip::tick()
{
    sync();
    bool moved = false;
    // pendant un glisser : les autres carrés s'écartent pour laisser la place à l'endroit où il tomberait
    int k = 0;
    for (int i = 0; i < (int) tiles.size(); ++i)
    {
        auto& t = tiles[(size_t) i];
        if (i == dragTile && dragMoved) { moved |= t.x != dragX; t.x = dragX; continue; }
        if (dragTile >= 0 && dragMoved && k == dropIndex) ++k;
        moved |= ui::spring (t.x, t.vel, slotX (k), 0.2f, 0.66f);
        ++k;
        moved |= ui::approach (t.hover, i == hoverTile ? 1.0f : 0.0f, 0.2f);
        moved |= ui::approach (t.on, isOn (t.code) ? 1.0f : 0.0f, 0.2f);
        moved |= ui::approach (t.sel, t.code == selected ? 1.0f : 0.0f, 0.2f);
        moved |= ui::approach (t.appear, 1.0f, 0.15f);
    }
    if (dragTile >= 0)
    {
        auto& t = tiles[(size_t) dragTile];
        moved |= ui::approach (t.on, isOn (t.code) ? 1.0f : 0.0f, 0.2f);
        moved |= ui::approach (t.sel, t.code == selected ? 1.0f : 0.0f, 0.2f);
    }
    moved |= ui::approach (plusGlow, plusHover ? 1.0f : 0.0f, 0.18f);
    moved |= ui::approach (plusSpin, plusHover ? 1.0f : 0.0f, 0.14f);
    if (moved) repaint();
}

void ChainStrip::paint (juce::Graphics& g)
{
    const float u = ui::unitOf (*this);
    // les flèches entre les carrés
    for (int i = 0; i + 1 < (int) tiles.size(); ++i)
    {
        const auto a = tileRect (slotX (i));
        const float x = a.getRight() + 6.0f * u, y = a.getCentreY();
        juce::Path p;
        p.startNewSubPath (x - 3.0f * u, y - 4.0f * u); p.lineTo (x + 1.5f * u, y); p.lineTo (x - 3.0f * u, y + 4.0f * u);
        g.setColour (juce::Colour (0xff6e5a5e));
        g.strokePath (p, juce::PathStrokeType (1.6f * u, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    auto paintTile = [&] (const Tile& t, bool lifted)
    {
        auto r = tileRect (t.x);
        const float lift = lifted ? 1.0f : t.hover;
        r = r.translated (0.0f, -4.0f * u * lift).withSizeKeepingCentre (r.getWidth() * (1.0f + 0.03f * lift), r.getHeight() * (1.0f + 0.03f * lift));
        const float rad = 18.0f * u;
        const int type = modules::typeOf (t.code);
        const auto& inf = modules::info (type);
        g.setOpacity (1.0f);
        if (lifted || t.sel > 0.01f)
        {
            g.setColour (ui::ember.withAlpha ((lifted ? 0.30f : 0.28f * t.sel)));
            g.fillRoundedRectangle (r.expanded (6.0f * u), rad + 6.0f * u);
        }
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (r.translated (0.0f, 6.0f * u), rad);
        g.setColour (juce::Colour (0xff140d10).withAlpha (0.92f));
        g.fillRoundedRectangle (r, rad);
        ui::drawGlass (g, r, rad, u, t.sel * 0.6f + 0.25f * t.hover);
        if (t.sel > 0.01f)
        {
            g.setColour (juce::Colour (0xffff6e7d).withAlpha (0.75f * t.sel));
            g.drawRoundedRectangle (r.reduced (0.5f), rad, 1.6f * u);
        }
        // reflet qui passe au survol
        if (t.hover > 0.01f)
        {
            juce::Graphics::ScopedSaveState s (g);
            juce::Path clip; clip.addRoundedRectangle (r, rad);
            g.reduceClipRegion (clip);
            const float sx = r.getX() - r.getWidth() * 0.6f + r.getWidth() * 1.6f * t.hover;
            juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.0f), sx - 40.0f * u, r.getY(),
                                        juce::Colours::white.withAlpha (0.0f), sx + 40.0f * u, r.getY(), false);
            sheen.addColour (0.5, juce::Colours::white.withAlpha (0.14f));
            g.setGradientFill (sheen);
            g.fillRect (r);
        }

        const float dim = 0.42f + 0.58f * t.on;
        // témoin
        const auto led = juce::Rectangle<float> (9.0f * u, 9.0f * u).withPosition (r.getX() + 12.0f * u, r.getY() + 13.0f * u);
        if (t.on > 0.01f)
        {
            g.setColour (ui::ember.withAlpha (0.35f * t.on));
            g.fillEllipse (led.expanded (4.0f * u));
        }
        g.setColour (ui::ember.interpolatedWith (juce::Colour (0xff4a3a3e), 1.0f - t.on));
        g.fillEllipse (led);

        // power et retirer, au survol
        if (t.hover > 0.01f && ! lifted)
        {
            for (int b = 0; b < 2; ++b)
            {
                const auto br = b == 0 ? powerRect (r) : closeRect (r);
                g.setColour (ui::mist.withAlpha (0.85f * t.hover));
                if (b == 0)
                {
                    juce::Path pw;
                    const auto c = br.getCentre();
                    const float rr = br.getWidth() * 0.3f;
                    pw.addCentredArc (c.x, c.y, rr, rr, 0.0f, 0.75f, 2.0f * kPi - 0.75f, true);
                    pw.startNewSubPath (c.x, c.y - rr * 1.25f); pw.lineTo (c.x, c.y - rr * 0.1f);
                    g.strokePath (pw, juce::PathStrokeType (1.8f * u, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                }
                else
                {
                    const auto x = br.reduced (br.getWidth() * 0.3f);
                    g.drawLine ({ x.getTopLeft(), x.getBottomRight() }, 1.8f * u);
                    g.drawLine ({ x.getTopRight(), x.getBottomLeft() }, 1.8f * u);
                }
            }
        }

        // pictogramme, nom (avec son numéro s'il y a plusieurs exemplaires), groupe
        int count = 0, nth = 0;
        for (const auto& o : tiles) if (modules::typeOf (o.code) == type) { ++count; if (o.code == t.code) nth = count; }
        const auto name = juce::String (inf.shortName) + (count > 1 ? " " + juce::String (nth) : juce::String());
        const auto bottom = r.withTrimmedTop (r.getHeight() * 0.42f).reduced (12.0f * u, 10.0f * u);
        ui::drawModuleIcon (g, type, juce::Rectangle<float> (16.0f * u, 16.0f * u).withPosition (bottom.getX(), bottom.getY() + 2.0f * u),
                            juce::Colour (0xffff8e9a).withAlpha (dim), 1.8f * u);
        g.setColour (ui::bone.withAlpha (dim));
        g.setFont (ui::fitFont (ui::Face::display, 13.0f * u, bottom.getWidth() - 22.0f * u, name, true));
        g.drawText (name, bottom.withTrimmedLeft (22.0f * u).withHeight (20.0f * u), juce::Justification::centredLeft, false);
        g.setColour (ui::haze.withAlpha (dim));
        g.setFont (ui::fitFont (ui::Face::mono, 9.0f * u, bottom.getWidth(), modules::groupName (inf.group)));
        g.drawText (modules::groupName (inf.group), bottom.withTrimmedTop (24.0f * u).withHeight (14.0f * u), juce::Justification::centredLeft, false);
    };

    for (int i = 0; i < (int) tiles.size(); ++i)
        if (i != dragTile || ! dragMoved) paintTile (tiles[(size_t) i], false);
    if (dragTile >= 0 && dragMoved)
    {
        // l'endroit où il tombera : un trait braise
        const auto at = tileRect (slotX (dropIndex));
        g.setColour (ui::ember.withAlpha (0.8f));
        g.fillRoundedRectangle (juce::Rectangle<float> (3.0f * u, at.getHeight() * 0.7f).withCentre ({ at.getX() - 6.0f * u, at.getCentreY() }), 1.5f * u);
        paintTile (tiles[(size_t) dragTile], true);
    }

    // le +
    const auto pr = plusRect();
    g.setColour (juce::Colour (0xff140d10).withAlpha (0.9f));
    g.fillRoundedRectangle (pr, 18.0f * u);
    ui::drawGlass (g, pr, 18.0f * u, u, 0.5f * plusGlow);
    if (plusGlow > 0.01f)
    {
        g.setColour (ui::ember.withAlpha (0.22f * plusGlow));
        g.fillRoundedRectangle (pr.expanded (5.0f * u), 22.0f * u);
    }
    {
        const auto c = pr.getCentre();
        const float s = 15.0f * u * (1.0f + 0.06f * plusGlow), a = plusSpin * kPi * 0.5f;
        juce::Path p;
        p.startNewSubPath (c.x, c.y - s); p.lineTo (c.x, c.y + s);
        p.startNewSubPath (c.x - s, c.y); p.lineTo (c.x + s, c.y);
        p.applyTransform (juce::AffineTransform::rotation (a, c.x, c.y));
        g.setColour (juce::Colour (0xffffb3bb).interpolatedWith (ui::bone, plusGlow));
        g.strokePath (p, juce::PathStrokeType (2.2f * u, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    if (tiles.empty())
    {
        g.setColour (ui::haze);
        g.setFont (ui::font (ui::Face::mono, 12.0f * u));
        g.drawText ("CLIQUE SUR + POUR AJOUTER UN MODULE A LA CHAINE", pr.withX (pr.getRight() + 20.0f * u).withWidth (700.0f * u),
                    juce::Justification::centredLeft, false);
    }
}

void ChainStrip::mouseMove (const juce::MouseEvent& e)
{
    const int h = tileAt (e.position);
    const bool ph = plusRect().contains (e.position);
    if (h != hoverTile || ph != plusHover) { hoverTile = h; plusHover = ph; repaint(); }
    setMouseCursor (h >= 0 ? juce::MouseCursor::DraggingHandCursor : (ph ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor));
}

void ChainStrip::mouseExit (const juce::MouseEvent&)
{
    hoverTile = -1; plusHover = false;
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
    if (closeRect (r).contains (e.position))
    {
        processor.removeModule (code);
        return;
    }
    dragTile = i;
    dragOffset = e.position.x - tiles[(size_t) i].x;
    dragX = tiles[(size_t) i].x;
    dropIndex = i;
    if (onSelect) onSelect (code);
}

void ChainStrip::mouseDrag (const juce::MouseEvent& e)
{
    if (dragTile < 0) return;
    if (! dragMoved && e.position.getDistanceFrom (downPos) < 6.0f) return;
    dragMoved = true;
    const float step = tileW() + 12.0f * ui::unitOf (*this);
    dragX = juce::jlimit (0.0f, slotX ((int) tiles.size() - 1), e.position.x - dragOffset);
    dropIndex = juce::jlimit (0, (int) tiles.size() - 1, juce::roundToInt (dragX / step));
    repaint();
}

void ChainStrip::mouseUp (const juce::MouseEvent&)
{
    if (dragTile >= 0 && dragMoved && dropIndex != dragTile)
    {
        tiles[(size_t) dragTile].x = dragX;
        processor.moveModule (dragTile, dropIndex);
        // l'ordre local suit tout de suite (sync le confirme à la prochaine image)
        auto t = tiles[(size_t) dragTile];
        tiles.erase (tiles.begin() + dragTile);
        tiles.insert (tiles.begin() + dropIndex, t);
    }
    dragTile = -1;
    dragMoved = false;
    repaint();
}

juce::String ChainStrip::getTooltip()
{
    if (plusHover) return "Add a module to the chain (as many as you like: three of each).";
    if (hoverTile >= 0) return "Click to open the module, drag left or right to change the order. Power and remove on the top right.";
    return {};
}

//==============================================================================
ModulePicker::ModulePicker (OniriqueVocalBoxAudioProcessor& p) : processor (p)
{
    setVisible (false);
    setWantsKeyboardFocus (true);
}

void ModulePicker::open() { target = 1.0f; setVisible (true); toFront (true); grabKeyboardFocus(); }
void ModulePicker::close() { target = 0.0f; if (onClose) onClose(); }

int ModulePicker::freeCount (int type) const
{
    const auto c = processor.getChain();
    int used = 0;
    for (int x : c) if (modules::typeOf (x) == type) ++used;
    return kInstances - used;
}

juce::Rectangle<float> ModulePicker::card() const
{
    const float u = ui::unitOf (*this);
    return juce::Rectangle<float> (1240.0f * u, 640.0f * u).withCentre (getLocalBounds().toFloat().getCentre())
           .translated (0.0f, 14.0f * u * (1.0f - shown));
}

juce::Rectangle<float> ModulePicker::itemRect (int type) const
{
    const float u = ui::unitOf (*this);
    const auto c = card().reduced (28.0f * u).withTrimmedTop (56.0f * u);
    const int group = modules::info (type).group;
    int col = 0;
    for (int t = 0; t < type; ++t) if (modules::info (t).group == group) ++col;
    const float rowH = c.getHeight() / 4.0f, cw = c.getWidth() / 6.0f;
    return { c.getX() + cw * (float) col + 4.0f * u, c.getY() + rowH * (float) group + 24.0f * u, cw - 8.0f * u, rowH - 32.0f * u };
}

int ModulePicker::itemAt (juce::Point<float> p) const
{
    for (int t = 0; t < modules::numTypes; ++t) if (itemRect (t).contains (p)) return t;
    return -1;
}

void ModulePicker::tick()
{
    bool moved = ui::approach (shown, target, 0.2f);
    for (int t = 0; t < modules::numTypes; ++t) moved |= ui::approach (hoverGlow[t], t == hover ? 1.0f : 0.0f, 0.2f);
    if (target < 0.5f && shown < 0.01f && isVisible()) setVisible (false);
    if (moved) repaint();
}

void ModulePicker::paint (juce::Graphics& g)
{
    const float u = ui::unitOf (*this);
    g.setColour (juce::Colour (0xff050304).withAlpha (0.62f * shown));
    g.fillAll();
    g.setOpacity (shown);
    const auto c = card();
    g.setColour (juce::Colour (0xff180c0f).withAlpha (0.97f * shown));
    g.fillRoundedRectangle (c, 30.0f * u);
    ui::drawGlass (g, c, 30.0f * u, u, 0.15f);

    g.setColour (ui::bone.withAlpha (shown));
    g.setFont (ui::font (ui::Face::display, 20.0f * u, true));
    g.drawText ("AJOUTER UN MODULE", c.reduced (28.0f * u).withHeight (40.0f * u), juce::Justification::centredLeft, false);
    g.setColour (ui::haze.withAlpha (shown));
    g.setFont (ui::font (ui::Face::mono, 10.0f * u));
    g.drawText ("JUSQU'A TROIS FOIS CHAQUE MODULE  ·  ECHAP POUR FERMER", c.reduced (28.0f * u).withHeight (40.0f * u),
                juce::Justification::centredRight, false);

    const auto inner = c.reduced (28.0f * u).withTrimmedTop (56.0f * u);
    for (int gi = 0; gi < 4; ++gi)
    {
        g.setColour (ui::haze.withAlpha (shown));
        g.setFont (ui::font (ui::Face::mono, 10.0f * u));
        g.drawText (modules::groupName (gi), inner.withY (inner.getY() + inner.getHeight() / 4.0f * (float) gi).withHeight (20.0f * u),
                    juce::Justification::centredLeft, false);
    }
    for (int t = 0; t < modules::numTypes; ++t)
    {
        const auto r = itemRect (t);
        const auto& inf = modules::info (t);
        const bool free = freeCount (t) > 0;
        const float hg = hoverGlow[t], a = shown * (free ? 1.0f : 0.4f);
        ui::drawGlass (g, r.translated (0.0f, -2.0f * u * hg), 16.0f * u, u, 0.55f * hg);
        const auto ic = juce::Rectangle<float> (44.0f * u, 44.0f * u).withPosition (r.getX() + 12.0f * u, r.getCentreY() - 22.0f * u - 2.0f * u * hg);
        juce::ColourGradient grd (ui::ember.withAlpha (0.45f * a), ic.getX(), ic.getY(), juce::Colour (0xff5a0814).withAlpha (0.35f * a), ic.getRight(), ic.getBottom(), false);
        g.setGradientFill (grd);
        g.fillRoundedRectangle (ic, 14.0f * u);
        g.setColour (juce::Colour (0xffff8c96).withAlpha ((0.35f + 0.4f * hg) * a));
        g.drawRoundedRectangle (ic.reduced (0.5f), 14.0f * u, 1.0f);
        if (hg > 0.01f)
        {
            g.setColour (ui::ember.withAlpha (0.25f * hg * a));
            g.fillRoundedRectangle (ic.expanded (5.0f * u), 18.0f * u);
        }
        ui::drawModuleIcon (g, t, ic.reduced (11.0f * u), juce::Colour (0xffffd6db).interpolatedWith (ui::bone, hg).withAlpha (a), 1.8f * u);
        const auto tr = r.withTrimmedLeft (68.0f * u).reduced (0.0f, 8.0f * u);
        g.setColour (ui::bone.withAlpha (a));
        g.setFont (ui::fitFont (ui::Face::display, 13.0f * u, tr.getWidth() - 6.0f * u, inf.name, true));
        g.drawText (inf.name, tr.withHeight (tr.getHeight() * 0.5f), juce::Justification::bottomLeft, false);
        g.setColour (juce::Colour (0xffcdb8bb).withAlpha (a));
        g.setFont (ui::font (ui::Face::body, 11.5f * u));
        g.drawFittedText (free ? inf.desc : "Trois exemplaires déjà dans la chaîne", tr.withTrimmedTop (tr.getHeight() * 0.5f + 2.0f * u).toNearestInt(),
                          juce::Justification::topLeft, 2, 0.9f);
    }
}

void ModulePicker::mouseMove (const juce::MouseEvent& e)
{
    const int h = itemAt (e.position);
    if (h != hover) { hover = h; setMouseCursor (h >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor); repaint(); }
}

void ModulePicker::mouseDown (const juce::MouseEvent& e)
{
    if (! card().contains (e.position)) { close(); return; }
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
    const float d = (float) getHeight() - 14.0f * u;
    knob.setBounds (juce::Rectangle<float> (d, d).withPosition ((float) getWidth() - d - 10.0f * u, 7.0f * u).toNearestInt());
}

void GainStrip::tick()
{
    knob.tick();
    bool moved = false;
    for (int c = 0; c < 2; ++c)
    {
        const float pk = consume (c);
        const float db = pk > 1.0e-5f ? 20.0f * std::log10 (pk) : -100.0f;
        if (db > level[c]) level[c] = db; else level[c] = std::max (-100.0f, level[c] - 0.6f);
        if (db >= hold[c]) { hold[c] = db; holdFrames[c] = 60; } else if (--holdFrames[c] < 0) hold[c] = std::max (-100.0f, hold[c] - 0.4f);
        moved = true;
    }
    if (moved) repaint();
}

void GainStrip::paint (juce::Graphics& g)
{
    const float u = ui::unitOf (*this);
    const auto r = getLocalBounds().toFloat();
    ui::drawGlass (g, r, 22.0f * u, u);
    g.setColour (ui::bone);
    g.setFont (ui::font (ui::Face::display, 14.0f * u, true));
    g.drawText (title, r.withTrimmedLeft (20.0f * u).withWidth (50.0f * u), juce::Justification::centredLeft, false);

    const auto kb = knob.getBounds().toFloat();
    auto bar = juce::Rectangle<float> (r.getX() + 76.0f * u, r.getCentreY() - 9.0f * u, kb.getX() - r.getX() - 176.0f * u, 18.0f * u);
    const auto prm = knob.getTooltip();
    for (int c = 0; c < 2; ++c)
    {
        const auto lane = bar.withHeight (7.0f * u).translated (0.0f, (float) c * 11.0f * u);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (lane, 3.5f * u);
        const float n = juce::jlimit (0.0f, 1.0f, (level[c] + 60.0f) / 62.0f);
        if (n > 0.0f)
        {
            const auto fill = lane.withWidth (lane.getWidth() * n);
            juce::ColourGradient gr (juce::Colour (0xff7a0d1c), lane.getX(), 0.0f, juce::Colour (0xffffc4ca), lane.getRight(), 0.0f, false);
            gr.addColour (0.7, ui::ember);
            g.setGradientFill (gr);
            g.fillRoundedRectangle (fill, 3.5f * u);
        }
        const float hn = juce::jlimit (0.0f, 1.0f, (hold[c] + 60.0f) / 62.0f);
        if (hn > 0.0f)
        {
            g.setColour (ui::bone.withAlpha (0.8f));
            g.fillRect (juce::Rectangle<float> (lane.getX() + lane.getWidth() * hn - 1.0f * u, lane.getY(), 2.0f * u, lane.getHeight()));
        }
    }
    // la valeur du gain
    if (auto* ed = findParentComponentOfClass<juce::AudioProcessorEditor>())
        juce::ignoreUnused (ed);
    const auto vr = juce::Rectangle<float> (bar.getRight() + 10.0f * u, r.getY(), 86.0f * u, r.getHeight());
    const float peak = std::max (hold[0], hold[1]);
    g.setColour (ui::mist);
    g.setFont (ui::font (ui::Face::mono, 12.0f * u));
    g.drawText (peak <= -99.0f ? juce::String ("-inf") : juce::String (peak, 1) + " dB", vr, juce::Justification::centredRight, false);
    juce::ignoreUnused (prm);
}

//==============================================================================
AutoGainSwitch::AutoGainSwitch (OniriqueVocalBoxAudioProcessor& p) : processor (p)
{
    param = p.apvts.getParameter (ids::autoGain);
    attachment = std::make_unique<juce::ParameterAttachment> (*param, [this] (float v) { state = v > 0.5f; repaint(); });
    attachment->sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void AutoGainSwitch::tick()
{
    bool moved = ui::approach (lit, state ? 1.0f : 0.0f, 0.2f);
    moved |= ui::approach (shownDb, processor.getAutoGainDb(), 0.2f);
    if (moved) repaint();
}

void AutoGainSwitch::paint (juce::Graphics& g)
{
    const float u = ui::unitOf (*this);
    const auto r = getLocalBounds().toFloat();
    ui::drawGlass (g, r, 22.0f * u, u, 0.3f * lit);
    g.setColour (ui::mist);
    g.setFont (ui::font (ui::Face::mono, 11.0f * u));
    g.drawText ("AUTO GAIN", r.withTrimmedLeft (20.0f * u).withHeight (r.getHeight() * 0.6f), juce::Justification::bottomLeft, false);
    g.setColour (ui::haze);
    g.setFont (ui::font (ui::Face::mono, 10.0f * u));
    const auto db = state ? (shownDb >= 0.0f ? "+" : "") + juce::String (shownDb, 1) + " dB" : juce::String ("OFF");
    g.drawText (db, r.withTrimmedLeft (20.0f * u).withTrimmedTop (r.getHeight() * 0.6f), juce::Justification::topLeft, false);

    const auto sw = juce::Rectangle<float> (58.0f * u, 30.0f * u).withCentre ({ r.getRight() - 50.0f * u, r.getCentreY() });
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (sw, 15.0f * u);
    if (lit > 0.01f)
    {
        g.setGradientFill (juce::ColourGradient (ui::ember.withAlpha (0.75f * lit), sw.getX(), 0.0f, juce::Colour (0xff8c1020).withAlpha (0.75f * lit), sw.getRight(), 0.0f, false));
        g.fillRoundedRectangle (sw, 15.0f * u);
    }
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawRoundedRectangle (sw.reduced (0.5f), 15.0f * u, 1.0f);
    const auto dot = juce::Rectangle<float> (22.0f * u, 22.0f * u).withCentre ({ sw.getX() + 15.0f * u + 28.0f * u * lit, sw.getCentreY() });
    if (lit > 0.01f) { g.setColour (juce::Colour (0xffff8c96).withAlpha (0.5f * lit)); g.fillEllipse (dot.expanded (4.0f * u)); }
    g.setColour (juce::Colour (0xffd9c6c9).interpolatedWith (juce::Colours::white, lit));
    g.fillEllipse (dot);
}

void AutoGainSwitch::mouseDown (const juce::MouseEvent&)
{
    attachment->setValueAsCompleteGesture (state ? 0.0f : 1.0f);
}

//==============================================================================
VocalBoxView::VocalBoxView (OniriqueVocalBoxAudioProcessor& p)
    : processor (p), strip (p),
      inStrip (p, ids::input, "IN", [&p] (int c) { return p.consumeInputLevel (c); }),
      outStrip (p, ids::output, "OUT", [&p] (int c) { return p.consumeOutputLevel (c); }),
      autoGain (p),
      oversampling (p.apvts, ids::oversampling, { "OFF", "2x", "4x", "8x" },
                    { "No oversampling: lowest CPU.", "2x oversampling (default).", "4x oversampling.", "8x oversampling: highest CPU." }),
      picker (p)
{
    setLookAndFeel (&lnf);
    setOpaque (true);
    addAndMakeVisible (strip);
    addAndMakeVisible (inStrip);
    addAndMakeVisible (outStrip);
    addAndMakeVisible (autoGain);
    oversampling.setTitle ("OS", "Oversampling of the whole chain (more = smoother saturation and compression, more CPU).");
    addAndMakeVisible (oversampling);
    addChildComponent (picker);

    strip.onSelect = [this] (int code) { select (code); };
    strip.onAdd = [this] { picker.open(); };
    picker.onPick = [this] (int type)
    {
        const int code = processor.addModule (type);
        picker.close();
        if (code != 0) select (code);
    };

    const auto chain = p.getChain();
    select (processor.apvts.state.getProperty ("selectedModule", chain[0]));
    startTimerHz (60);
}

VocalBoxView::~VocalBoxView()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

bool VocalBoxView::isOn (int code) const
{
    if (! modules::valid (code)) return false;
    auto* prm = processor.apvts.getRawParameterValue (ids::inst (modules::info (modules::typeOf (code)).onId, modules::instanceOf (code)));
    return prm != nullptr && prm->load() > 0.5f;
}

void VocalBoxView::select (int code)
{
    const auto chain = processor.getChain();
    if (std::find (chain.begin(), chain.end(), code) == chain.end()) code = chain[0];
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
    if (std::find (chain.begin(), chain.end(), selected) == chain.end() || selected == 0)
    {
        panel.reset();
        selected = -1;
        select (chain[0]);
        if (chain[0] == 0) { selected = 0; strip.setSelected (0); repaint(); }
    }
}

juce::Rectangle<float> VocalBoxView::panelArea() const
{
    const float u = ui::unitOf (*this);
    return { 20.0f * u, 136.0f * u, (float) getWidth() - 40.0f * u, 530.0f * u };
}

juce::Rectangle<float> VocalBoxView::headerPower() const
{
    const auto a = panelArea();
    const float u = ui::unitOf (*this);
    return { a.getRight() - 112.0f * u, a.getY() + 16.0f * u, 92.0f * u, 32.0f * u };
}

void VocalBoxView::resized()
{
    const float u = ui::unitOf (*this);
    strip.setBounds (juce::Rectangle<float> (24.0f * u, 18.0f * u, (float) getWidth() - 48.0f * u, 108.0f * u).toNearestInt());
    const auto a = panelArea();
    if (panel != nullptr)
        panel->setBounds (a.reduced (22.0f * u, 0.0f).withTrimmedTop (62.0f * u).withTrimmedBottom (18.0f * u).toNearestInt());
    const float fy = 680.0f * u, fh = 80.0f * u;
    const float w = (float) getWidth(), side = (w - 48.0f * u - 2.0f * 18.0f * u - 230.0f * u - 240.0f * u - 18.0f * u) * 0.5f;
    float x = 24.0f * u;
    inStrip.setBounds (juce::Rectangle<float> (x, fy, side, fh).toNearestInt()); x += side + 18.0f * u;
    autoGain.setBounds (juce::Rectangle<float> (x, fy, 230.0f * u, fh).toNearestInt()); x += 230.0f * u + 18.0f * u;
    oversampling.setBounds (juce::Rectangle<float> (x, fy + 18.0f * u, 240.0f * u, fh - 36.0f * u).toNearestInt()); x += 240.0f * u + 18.0f * u;
    outStrip.setBounds (juce::Rectangle<float> (x, fy, side, fh).toNearestInt());
    picker.setBounds (getLocalBounds());
    backdrop = {};
}

void VocalBoxView::timerCallback()
{
    strip.tick();
    inStrip.tick();
    outStrip.tick();
    autoGain.tick();
    oversampling.tick();
    picker.tick();
    syncSelection();
    if (panel != nullptr) panel->tick();
    bool moved = ui::approach (panelFade, 1.0f, 0.18f);
    moved |= ui::approach (powerLit, isOn (selected) ? 1.0f : 0.0f, 0.2f);
    if (panel != nullptr && panelFade < 1.0f) panel->setAlpha (panelFade);
    if (moved) repaint (panelArea().toNearestInt().withHeight (70));
}

void VocalBoxView::mouseDown (const juce::MouseEvent& e)
{
    if (headerPower().contains (e.position) && modules::valid (selected))
        if (auto* p = processor.param (modules::info (modules::typeOf (selected)).onId, modules::instanceOf (selected)))
            p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f);
}

void VocalBoxView::paint (juce::Graphics& g)
{
    const float u = ui::unitOf (*this);
    if (! backdrop.isValid() || backdrop.getWidth() != getWidth() || backdrop.getHeight() != getHeight())
    {
        // le fond : noir profond et trois taches de braise floues (dessinées une fois par taille)
        backdrop = juce::Image (juce::Image::RGB, std::max (1, getWidth()), std::max (1, getHeight()), true, juce::SoftwareImageType());
        juce::Graphics bg (backdrop);
        bg.fillAll (ui::night);
        auto blob = [&] (float cx, float cy, float rx, float ry, juce::Colour c)
        {
            juce::ColourGradient gr (c, cx, cy, c.withAlpha (0.0f), cx + rx, cy, true);
            bg.setGradientFill (gr);
            bg.fillEllipse (cx - rx, cy - ry, 2.0f * rx, 2.0f * ry);
        };
        blob (200.0f * u, 0.0f, 700.0f * u, 420.0f * u, juce::Colour (0xff96101e).withAlpha (0.45f));
        blob ((float) getWidth() - 100.0f * u, (float) getHeight(), 650.0f * u, 420.0f * u, juce::Colour (0xff780818).withAlpha (0.40f));
        blob (840.0f * u, 520.0f * u, 420.0f * u, 260.0f * u, juce::Colour (0xffff5a6e).withAlpha (0.07f));
    }
    g.drawImageAt (backdrop, 0, 0);

    // le panneau du module
    const auto a = panelArea();
    g.setColour (juce::Colour (0xff120c0e).withAlpha (0.88f));
    g.fillRoundedRectangle (a, 28.0f * u);
    ui::drawGlass (g, a, 28.0f * u, u);
    if (! modules::valid (selected))
    {
        g.setColour (ui::haze);
        g.setFont (ui::font (ui::Face::display, 18.0f * u));
        g.drawText ("La chaîne est vide : ajoute un module avec le +", a, juce::Justification::centred, false);
        return;
    }
    const auto& inf = modules::info (modules::typeOf (selected));
    const auto chain = processor.getChain();
    int count = 0, nth = 0;
    for (int c : chain) if (modules::typeOf (c) == modules::typeOf (selected)) { ++count; if (c == selected) nth = count; }
    const auto name = juce::String (inf.name) + (count > 1 ? " " + juce::String (nth) : juce::String());
    const auto head = juce::Rectangle<float> (a.getX() + 26.0f * u, a.getY() + 14.0f * u, a.getWidth() - 160.0f * u, 36.0f * u);
    const auto dot = juce::Rectangle<float> (10.0f * u, 10.0f * u).withCentre ({ head.getX() + 5.0f * u, head.getCentreY() });
    g.setColour (ui::ember.withAlpha (0.4f * powerLit));
    g.fillEllipse (dot.expanded (5.0f * u));
    g.setColour (ui::ember.interpolatedWith (juce::Colour (0xff4a3a3e), 1.0f - powerLit));
    g.fillEllipse (dot);
    const auto nf = ui::font (ui::Face::display, 22.0f * u, true);
    g.setFont (nf);
    g.setColour (ui::bone);
    const float nw = ui::textWidth (nf, name) + 4.0f;
    g.drawText (name, head.withTrimmedLeft (22.0f * u).withWidth (nw), juce::Justification::centredLeft, false);
    g.setColour (ui::haze);
    g.setFont (ui::font (ui::Face::mono, 10.0f * u));
    g.drawText (juce::String (inf.desc).toUpperCase(), head.withTrimmedLeft (22.0f * u + nw + 14.0f * u), juce::Justification::centredLeft, false);

    // power du module
    const auto pr = headerPower();
    if (powerLit > 0.01f)
    {
        g.setGradientFill (juce::ColourGradient (ui::ember.withAlpha (0.55f * powerLit), pr.getX(), pr.getY(), juce::Colour (0xff780c1c).withAlpha (0.55f * powerLit),
                                                 pr.getX(), pr.getBottom(), false));
        g.fillRoundedRectangle (pr, 12.0f * u);
    }
    ui::drawGlass (g, pr, 12.0f * u, u, 0.4f * powerLit);
    g.setColour (ui::bone);
    g.setFont (ui::font (ui::Face::mono, 11.0f * u));
    g.drawText (powerLit > 0.5f ? "ON" : "OFF", pr.withTrimmedLeft (30.0f * u), juce::Justification::centredLeft, false);
    juce::Path pw;
    const auto c = juce::Point<float> (pr.getX() + 18.0f * u, pr.getCentreY());
    pw.addCentredArc (c.x, c.y, 6.0f * u, 6.0f * u, 0.0f, 0.75f, 2.0f * kPi - 0.75f, true);
    pw.startNewSubPath (c.x, c.y - 8.0f * u); pw.lineTo (c.x, c.y - 1.0f * u);
    g.strokePath (pw, juce::PathStrokeType (1.8f * u, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

//==============================================================================
OniriqueVocalBoxAudioProcessorEditor::OniriqueVocalBoxAudioProcessorEditor (OniriqueVocalBoxAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), view (p),
      bar (p.presets, p.apvts, "VOCALBOX", juce::ImageCache::getFromMemory (BinaryData::logo_png, BinaryData::logo_pngSize),
           (int) kBarUnits, (int) kBarUnits)
{
    jassert (nuit::Bar::heightFor ((int) kBarUnits) == (int) kBarH);
    setOpaque (true);
    addAndMakeVisible (view);
    addAndMakeVisible (bar);

    // le titre de VOCALBOX tient la place de la marque de la barre : il n'est écrit qu'une fois
    logo = juce::ImageCache::getFromMemory (BinaryData::logo_png, BinaryData::logo_pngSize);
    if (logo.isValid())
    {
        const int lh = 160, pad = lh / 4;
        logo = logo.rescaled (juce::roundToInt ((float) lh * 985.0f / 895.0f), lh, juce::Graphics::highResamplingQuality);
        juce::Image src (juce::Image::ARGB, logo.getWidth() + 2 * pad, logo.getHeight() + 2 * pad, true, juce::SoftwareImageType());
        { juce::Graphics lg (src); lg.drawImageAt (logo, pad, pad); }
        logoGlow = juce::Image (juce::Image::ARGB, src.getWidth(), src.getHeight(), true, juce::SoftwareImageType());
        juce::ImageConvolutionKernel k (15);
        k.createGaussianBlur ((float) lh / 22.0f);
        k.applyToImage (logoGlow, src, src.getBounds());
    }
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
    // rendue x2 par le moteur logiciel de JUCE) ; ONIRIQUE_UI_MODULE=<code> ouvre un module, "picker" ouvre le +
    const auto test = juce::SystemStats::getEnvironmentVariable ("ONIRIQUE_UI_TEST", {});
    if (test.contains ("nolicense")) licenseOverlay->setVisible (false);
    if (const auto png = juce::SystemStats::getEnvironmentVariable ("ONIRIQUE_UI_PNG", {}); png.isNotEmpty())
    {
        juce::Component::SafePointer<OniriqueVocalBoxAudioProcessorEditor> safe (this);
        juce::Timer::callAfterDelay (1500, [safe, png]
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

    startTimerHz (30);   // le logo respire
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

float OniriqueVocalBoxAudioProcessorEditor::titleWidth() const
{
    const float k = kTitleScale;
    const auto f1 = ui::font (ui::Face::display, 30.0f * k, true, 0.10f);
    const auto f2 = ui::font (ui::Face::display, 30.0f * k, false, 0.10f);
    return (kLogoW + 14.0f) * k + ui::textWidth (f1, "ONIRIQUE") + 2.0f + 10.0f * k + ui::textWidth (f2, "VOCALBOX") + 6.0f;
}

// le titre (logo à la lueur braise qui respire, ONIRIQUE VOCALBOX), dans la zone de la marque de la barre
void OniriqueVocalBoxAudioProcessorEditor::paintTitle (juce::Graphics& g, juce::Rectangle<float> area) const
{
    const float k = kTitleScale;
    const auto logoRect = juce::Rectangle<float> (area.getX(), kBarH * 0.5f - 32.0f * k, kLogoW * k, 64.0f * k);
    if (logo.isValid())
    {
        const float t = (float) (juce::Time::getMillisecondCounterHiRes() * 0.001);
        const float breathe = 0.5f + 0.5f * std::sin (t * 2.0f * kPi / 5.0f);
        const float pad = logoRect.getHeight() / 4.0f;
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.setColour (ui::ember.withAlpha (0.55f + 0.35f * breathe));
        g.drawImage (logoGlow, logoRect.expanded (pad), juce::RectanglePlacement::stretchToFit, true);
        g.setColour (juce::Colours::white.withAlpha (0.08f + 0.10f * breathe));
        g.drawImage (logoGlow, logoRect.expanded (pad), juce::RectanglePlacement::stretchToFit, true);
        g.setOpacity (1.0f);
        g.drawImage (logo, logoRect, juce::RectanglePlacement::stretchToFit);
    }

    const float x = logoRect.getRight() + 14.0f * k;
    const auto f1 = ui::font (ui::Face::display, 30.0f * k, true, 0.10f);
    const auto f2 = ui::font (ui::Face::display, 30.0f * k, false, 0.10f);
    const float w1 = ui::textWidth (f1, "ONIRIQUE") + 2.0f;
    const float w2 = ui::textWidth (f2, "VOCALBOX") + 2.0f;
    const float y = kBarH * 0.5f - 18.0f * k, hh = 36.0f * k;
    g.setFont (f1);
    g.setColour (ui::mist);
    g.drawText ("ONIRIQUE", juce::Rectangle<float> (x, y, w1, hh), juce::Justification::centredLeft, false);
    g.setColour (ui::ember);
    g.drawText ("VOCALBOX", juce::Rectangle<float> (x + w1 + 10.0f * k, y, w2 + 20.0f * k, hh), juce::Justification::centredLeft, false);
}

void OniriqueVocalBoxAudioProcessorEditor::timerCallback()
{
    // le logo respire : seule la zone du titre se redessine
    bar.repaint (bar.getBrandArea().withY (0).withHeight ((int) kBarH).expanded (10, 0));
}
