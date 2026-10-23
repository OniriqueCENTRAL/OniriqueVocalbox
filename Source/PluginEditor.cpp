#include "PluginEditor.h"
#include "OniriqueKnob.h"
#include "OniriqueWindowSize.h"
#include <BinaryData.h>
#include <OniriqueLicenseData.h>
#include <array>
#include <map>

using namespace onirique;

namespace
{
constexpr float kBaseW = 2208.0f, kBaseH = 1016.0f;  // 2208 : colonnes NOISE SUPPRESSOR + NOISE GATE à gauche, PARALLEL à droite ; 1016 : deux de-essers + EQ paramétrique agrandi
constexpr int kMinW = 1180, kMaxW = 3100;
constexpr float kPi = juce::MathConstants<float>::pi;

juce::Point<float> onCircle (juce::Point<float> c, float r, float ang)
{
    return { c.x + std::sin (ang) * r, c.y - std::cos (ang) * r };
}

// pastille de valeur (boutons, curseurs, champs) : fond sombre, liseré, point braise
void drawValuePill (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, float glow, float u)
{
    const float rad = r.getHeight() * 0.5f;
    g.setColour (juce::Colours::black.withAlpha (0.34f));
    g.fillRoundedRectangle (r, rad);
    g.setColour (ui::ember.withAlpha (0.14f * glow));
    g.fillRoundedRectangle (r, rad);
    g.setColour (ui::line.interpolatedWith (ui::ember.withAlpha (0.8f), glow));
    g.drawRoundedRectangle (r.reduced (0.5f), rad, std::max (1.0f, 1.2f * u));

    const float dot = r.getHeight() * 0.22f;
    const auto dotR = juce::Rectangle<float> (dot, dot).withCentre ({ r.getX() + rad * 0.95f, r.getCentreY() });
    g.setColour (ui::ember.withAlpha (0.25f + 0.35f * glow));
    g.fillEllipse (dotR.expanded (dot * 0.6f * glow));
    g.setColour (ui::ember.interpolatedWith (ui::bone, 0.35f * glow));
    g.fillEllipse (dotR);

    const auto textR = r.withTrimmedLeft (rad * 1.5f).withTrimmedRight (rad * 0.6f);
    g.setColour (ui::mist);
    g.setFont (ui::fitFont (ui::Face::mono, r.getHeight() * 0.56f, textR.getWidth(), text));
    g.drawText (text, textR, juce::Justification::centred, false);
}
} // namespace

//==============================================================================
namespace
{
// les polices de la charte, partagées par les interfaces ouvertes (thread de l'interface uniquement) : créées à
// l'ouverture de la première, détruites à la fermeture de la dernière. Seuls un compteur et un pointeur brut sont
// statiques : rien n'est détruit au déchargement du plugin.
struct Faces
{
    juce::Typeface::Ptr display, body, mono, monoBold;
};
Faces* faces = nullptr;
int faceUsers = 0;
} // namespace

ui::FontScope::FontScope()
{
    if (faceUsers++ == 0)
    {
        faces = new Faces();
        faces->display = juce::Typeface::createSystemTypefaceFor (OniriqueLicenseData::Unbounded800_ttf, OniriqueLicenseData::Unbounded800_ttfSize);
        faces->body = juce::Typeface::createSystemTypefaceFor (OniriqueLicenseData::HankenGrotesk600_ttf, OniriqueLicenseData::HankenGrotesk600_ttfSize);
        faces->mono = juce::Typeface::createSystemTypefaceFor (OniriqueLicenseData::JetBrainsMono500_ttf, OniriqueLicenseData::JetBrainsMono500_ttfSize);
        faces->monoBold = juce::Typeface::createSystemTypefaceFor (OniriqueLicenseData::JetBrainsMono600_ttf, OniriqueLicenseData::JetBrainsMono600_ttfSize);
    }
}

ui::FontScope::~FontScope()
{
    if (--faceUsers == 0) { delete faces; faces = nullptr; }
}

juce::Font ui::font (Face f, float h, bool bold, float kerning)
{
    h = std::max (4.0f, h);
    const juce::Typeface::Ptr tf = faces == nullptr ? nullptr
                                 : f == Face::display ? faces->display : f == Face::body ? faces->body : (bold ? faces->monoBold : faces->mono);
    // Unbounded est très large : un peu plus petit que la hauteur demandée, pour garder les mêmes encombrements
    juce::Font fnt = tf != nullptr ? juce::Font (juce::FontOptions (tf).withHeight (f == Face::display ? h * 0.86f : h))
                                   : juce::Font (juce::FontOptions (f == Face::display ? "Bahnschrift" : f == Face::body ? "Segoe UI" : "Cascadia Mono",
                                                                    h, bold ? juce::Font::bold : juce::Font::plain));
    if (kerning != 0.0f) fnt.setExtraKerningFactor (kerning);
    return fnt;
}

float ui::editorScale (const juce::Component& c)
{
    if (auto* ed = c.findParentComponentOfClass<juce::AudioProcessorEditor>()) return (float) ed->getWidth() / 2208.0f;
    if (auto* ed = dynamic_cast<const juce::AudioProcessorEditor*> (&c)) return (float) ed->getWidth() / 2208.0f;
    return 1.0f;
}

float ui::textWidth (const juce::Font& f, const juce::String& t)
{
    juce::GlyphArrangement ga;
    ga.addLineOfText (f, t, 0.0f, 0.0f);
    return ga.getBoundingBox (0, -1, true).getWidth();
}

juce::Font ui::fitFont (Face face, float maxH, float maxW, const juce::String& text, bool bold, float kerning)
{
    auto f = font (face, maxH, bold, kerning);
    const float w = textWidth (f, text);
    if (w > maxW && w > 0.0f) f.setHeight (std::max (4.0f, f.getHeight() * maxW / w));
    return f;
}

void ui::strokeGlow (juce::Graphics& g, const juce::Path& p, juce::Colour c, float width, float intensity)
{
    if (intensity <= 0.002f) return;
    for (int i = 4; i >= 1; --i)
    {
        g.setColour (c.withAlpha (juce::jlimit (0.0f, 1.0f, intensity * 0.17f / (float) i)));
        g.strokePath (p, juce::PathStrokeType (width * (1.0f + 1.15f * (float) i), juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    }
}

void ui::drawCachedShadow (juce::Graphics& g, ShadowCache& shadowCache, const juce::DropShadow& ds, juce::Rectangle<float> shape, float corner)
{
    // clé : taille et position dans le pixel de la forme, coin, flou, décalage (au 1/64 de pixel près)
    using Key = std::array<int, 8>;
    auto& cache = shadowCache.images;   // thread de l'interface uniquement

    const auto ib = shape.getSmallestIntegerContainer();
    const auto q = [] (float v) { return juce::roundToInt (v * 64.0f); };
    const Key key { q (shape.getWidth()), q (shape.getHeight()), q (shape.getX() - (float) ib.getX()), q (shape.getY() - (float) ib.getY()),
                    corner < 0.0f ? -1 : q (corner), ds.radius, ds.offset.x, ds.offset.y };

    // zone de l'ombre relative au coin de la forme, comme DropShadow::drawForPath (sans le découpage par la zone à redessiner)
    const auto area = (ib.withZeroOrigin() + ds.offset).expanded (ds.radius + 1);
    auto it = cache.find (key);
    if (it == cache.end())
    {
        if (cache.size() > 512) cache.clear();
        // image en mémoire ordinaire : rien qui dépende des ressources graphiques du système (Direct2D, Core Graphics)
        juce::Image pathImage (juce::Image::SingleChannel, area.getWidth(), area.getHeight(), true, juce::SoftwareImageType());
        {
            juce::Graphics g2 (pathImage);
            g2.setColour (juce::Colours::white);
            const auto local = shape.translated ((float) (ds.offset.x - ib.getX() - area.getX()), (float) (ds.offset.y - ib.getY() - area.getY()));
            if (corner < 0.0f) g2.fillEllipse (local);
            else               g2.fillRoundedRectangle (local, corner);
        }
        juce::Image blurred;
        juce::ImageEffects::applySingleChannelBoxBlurEffect (ds.radius, pathImage, blurred);
        it = cache.emplace (key, blurred).first;
    }
    g.setColour (ds.colour);
    g.drawImageAt (it->second, ib.getX() + area.getX(), ib.getY() + area.getY(), true);
}

//==============================================================================
OniriqueLookAndFeel::OniriqueLookAndFeel()
{
    setDefaultSansSerifTypefaceName ("Segoe UI");
    setColour (juce::TooltipWindow::backgroundColourId, ui::deep2);
    setColour (juce::TooltipWindow::textColourId, ui::mist);
    setColour (juce::TooltipWindow::outlineColourId, ui::line);
    setColour (juce::PopupMenu::backgroundColourId, ui::deep2.withAlpha (0.99f));   // non opaque : coins arrondis transparents
    setColour (juce::PopupMenu::textColourId, ui::mist);
    setColour (juce::PopupMenu::headerTextColourId, ui::haze);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, ui::ember.withAlpha (0.35f));
    setColour (juce::PopupMenu::highlightedTextColourId, ui::bone);
}

void OniriqueLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                            float a0, float a1, juce::Slider& s)
{
    const auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2.0f);
    const float D = juce::jmin (b.getWidth(), b.getHeight());
    if (D < 8.0f) return;

    const auto c = b.getCentre();
    const float R = D * 0.5f, u = D / 150.0f;
    const auto& props = s.getProperties();
    const float shown = (float) props.getWithDefault ("shown", pos);
    const float glow = (float) props.getWithDefault ("glow", 0.0f);
    const bool bipolar = (bool) props.getWithDefault ("bipolar", false);

    // ---- le potard ONIRIQUE, le même dans tous les plugins de la marque (OniriqueKnob.h) : graduations allumées
    //      jusqu'à la valeur (depuis le centre si bipolaire), corps biseauté, aiguille à l'étoile
    onirique::ui::drawKnob (g, b, shown, a0, a1, ui::ember, bipolar, s.isMouseOverOrDragging() || glow > 0.5f);

    // ---- bornes de l'échelle
    const auto minL = props["minLabel"].toString(), maxL = props["maxLabel"].toString();
    if (minL.isNotEmpty() || maxL.isNotEmpty())
    {
        g.setColour (ui::haze.withAlpha (0.8f));
        for (int side = 0; side < 2; ++side)
        {
            const auto& txt = side == 0 ? minL : maxL;
            const float es = ui::editorScale (s);   // plancher lisible même sur un petit bouton
            const auto f = ui::fitFont (ui::Face::mono, std::max (11.0f * u, 10.0f * es), std::max (0.34f * D, 30.0f * es), txt);
            const float tw = ui::textWidth (f, txt) + 2.0f;
            const auto pt = onCircle (c, R * 0.9f, side == 0 ? a0 : a1);
            const float cx = juce::jlimit (b.getX() + tw * 0.5f, b.getRight() - tw * 0.5f, pt.x);
            g.setFont (f);
            g.drawText (txt, juce::Rectangle<float> (tw, f.getHeight() * 1.3f).withCentre ({ cx, pt.y + 0.17f * R }),
                        juce::Justification::centred, false);
        }
    }
}

//==============================================================================
void OniriqueLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
    g.setGradientFill (juce::ColourGradient (ui::deep2, 0.0f, 0.0f, ui::deep, 0.0f, (float) height, false));
    g.fillRoundedRectangle (r, 12.0f);
    g.setGradientFill (juce::ColourGradient (ui::ember.withAlpha (0.10f), (float) width * 0.8f, 0.0f,
                                             ui::ember.withAlpha (0.0f), (float) width * 0.8f, (float) height * 0.6f, true));
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (ui::line);
    g.drawRoundedRectangle (r, 12.0f, 1.0f);
}

void OniriqueLookAndFeel::getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int, int& w, int& h)
{
    if (isSeparator) { w = 50; h = 11; return; }
    w = juce::roundToInt (ui::textWidth (getPopupMenuFont(), text)) + 56;
    h = 30;
}

void OniriqueLookAndFeel::drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& name)
{
    const auto r = area.toFloat().withTrimmedLeft (16.0f).withTrimmedTop (4.0f);
    const auto f = ui::font (ui::Face::mono, 10.5f, true, 0.18f);
    g.setFont (f);
    g.setColour (ui::haze);
    g.drawText (name, r, juce::Justification::centredLeft, false);
    const float tw = ui::textWidth (f, name);
    g.setColour (ui::line);
    g.fillRect (juce::Rectangle<float> (r.getX() + tw + 10.0f, r.getCentreY(), r.getWidth() - tw - 26.0f, 1.0f));
}

void OniriqueLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                                             bool isHighlighted, bool isTicked, bool, const juce::String& text,
                                             const juce::String&, const juce::Drawable*, const juce::Colour*)
{
    if (isSeparator)
    {
        g.setColour (ui::line);
        g.fillRect (juce::Rectangle<float> (14.0f, (float) area.getCentreY(), (float) area.getWidth() - 28.0f, 1.0f));
        return;
    }

    const auto r = area.toFloat().reduced (5.0f, 2.0f);
    if (isHighlighted && isActive)
    {
        juce::Path p;
        p.addRoundedRectangle (r, r.getHeight() * 0.5f);
        ui::strokeGlow (g, p, ui::ember, 1.0f, 0.45f);
        g.setGradientFill (juce::ColourGradient (ui::ember.withAlpha (0.38f), r.getX(), r.getY(),
                                                 ui::blood.withAlpha (0.35f), r.getRight(), r.getBottom(), false));
        g.fillPath (p);
        g.setColour (ui::ember.withAlpha (0.7f));
        g.strokePath (p, juce::PathStrokeType (1.0f));
    }

    // élément actif (preset chargé) : point braise lumineux
    const juce::Point<float> dot { r.getX() + 14.0f, r.getCentreY() };
    if (isTicked)
    {
        g.setColour (ui::ember.withAlpha (0.35f));
        g.fillEllipse (juce::Rectangle<float> (14.0f, 14.0f).withCentre (dot));
        g.setColour (ui::ember.interpolatedWith (ui::bone, 0.25f));
        g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (dot));
    }

    g.setColour (! isActive ? ui::haze.withAlpha (0.45f)
                            : (isHighlighted || isTicked ? ui::bone : ui::mist.withAlpha (0.88f)));
    g.setFont (getPopupMenuFont().withStyle (isTicked ? juce::Font::bold : juce::Font::plain));
    g.drawText (text, r.withTrimmedLeft (28.0f).withTrimmedRight (10.0f), juce::Justification::centredLeft, true);
}

// curseur vertical : la hauteur du curseur vaut D (même unité que les boutons rotatifs)
int OniriqueLookAndFeel::getSliderThumbRadius (juce::Slider& s)
{
    if (s.getSliderStyle() == juce::Slider::LinearVertical)
        return juce::roundToInt (s.getHeight() / 150.0f * 9.0f);
    if (s.getSliderStyle() == juce::Slider::LinearHorizontal)
        return juce::roundToInt (s.getHeight() * 0.32f);
    return LookAndFeel_V4::getSliderThumbRadius (s);
}

void OniriqueLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                            float minPos, float maxPos, juce::Slider::SliderStyle style, juce::Slider& s)
{
    if (style == juce::Slider::LinearHorizontal)
    {
        // curseur horizontal : rail fin, remplissage braise lumineux depuis la gauche, pastille claire
        const auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
        const float u = b.getHeight() / 30.0f, cy = b.getCentreY();
        const float r = (float) getSliderThumbRadius (s);
        const float left = b.getX() + r, right = b.getRight() - r;
        const float shown = (float) s.getProperties().getWithDefault ("shown", s.valueToProportionOfLength (s.getValue()));
        const float glow = (float) s.getProperties().getWithDefault ("glow", 0.0f);
        const float tx = left + shown * (right - left);
        const float trackH = std::max (3.0f, 4.0f * u);

        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.fillRoundedRectangle (juce::Rectangle<float> (left, cy - trackH * 0.5f, right - left, trackH), trackH * 0.5f);
        if (tx - left > 0.5f)
        {
            juce::Path fill;
            fill.startNewSubPath (left, cy);
            fill.lineTo (tx, cy);
            ui::strokeGlow (g, fill, ui::ember, trackH, 0.5f + 0.5f * glow);
            g.setGradientFill (juce::ColourGradient (ui::ember, left, cy, ui::bone, tx, cy, false));
            g.strokePath (fill, juce::PathStrokeType (trackH, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        const auto thumb = juce::Rectangle<float> (1.3f * r, 2.0f * r).withCentre ({ tx, cy });
        g.setColour (ui::ember.withAlpha (0.22f + 0.25f * glow));
        g.fillRoundedRectangle (thumb.expanded (3.0f * u), thumb.getWidth() * 0.5f + 3.0f * u);
        g.setGradientFill (juce::ColourGradient (ui::mist, thumb.getX(), thumb.getY(), juce::Colour (0xffd9c9cb), thumb.getX(), thumb.getBottom(), false));
        g.fillRoundedRectangle (thumb, thumb.getWidth() * 0.5f);
        g.setColour (ui::ember);
        g.drawLine (tx, thumb.getY() + 4.0f * u, tx, thumb.getBottom() - 4.0f * u, std::max (1.2f, 1.8f * u));
        return;
    }

    if (style != juce::Slider::LinearVertical)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, minPos, maxPos, style, s);
        return;
    }

    const auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const float u = b.getHeight() / 150.0f, cx = b.getCentreX();
    const float thumbR = (float) getSliderThumbRadius (s);
    const float top = b.getY() + thumbR, bottom = b.getBottom() - thumbR;
    const auto& props = s.getProperties();
    const float shown = (float) props.getWithDefault ("shown", s.valueToProportionOfLength (s.getValue()));
    const float glow = (float) props.getWithDefault ("glow", 0.0f);
    const float ty = bottom - shown * (bottom - top);

    // ---- graduations : points discrets, ceux atteints s'allument
    for (int i = 0; i <= 10; ++i)
    {
        const float ti = (float) i / 10.0f;
        const float yy = bottom - ti * (bottom - top);
        const float d = (i % 5 == 0 ? 3.4f : 2.2f) * u;
        g.setColour (ti <= shown + 1.0e-3f ? ui::ember.withAlpha (0.75f) : juce::Colours::white.withAlpha (0.16f));
        for (float dx : { -13.0f * u, 13.0f * u })
            g.fillEllipse (juce::Rectangle<float> (d, d).withCentre ({ cx + dx, yy }));
    }

    // ---- rail + remplissage lumineux du bas jusqu'au curseur
    const float trackW = std::max (3.0f, 5.0f * u);
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.fillRoundedRectangle (juce::Rectangle<float> (cx - trackW * 0.5f, top, trackW, bottom - top), trackW * 0.5f);
    if (bottom - ty > 0.5f)
    {
        juce::Path fill;
        fill.startNewSubPath (cx, bottom);
        fill.lineTo (cx, ty);
        ui::strokeGlow (g, fill, ui::ember, trackW, 0.5f + 0.5f * glow);
        g.setGradientFill (juce::ColourGradient (ui::ember, cx, bottom, ui::bone, cx, ty, false));
        g.strokePath (fill, juce::PathStrokeType (trackW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // ---- curseur : pastille claire avec halo braise (comme les curseurs du site)
    const float tw = std::min (b.getWidth() * 0.56f, 34.0f * u), th = 1.5f * thumbR;
    const auto thumb = juce::Rectangle<float> (tw, th).withCentre ({ cx, ty });
    g.setColour (ui::ember.withAlpha (0.22f + 0.25f * glow));
    g.fillRoundedRectangle (thumb.expanded (4.0f * u), th * 0.5f + 4.0f * u);
    juce::Path tp;
    tp.addRoundedRectangle (thumb, th * 0.5f);
    ui::drawCachedShadow (g, shadows, juce::DropShadow (juce::Colours::black.withAlpha (0.5f), juce::roundToInt (8.0f * u), { 0, juce::roundToInt (3.0f * u) }),
                          thumb, th * 0.5f);
    g.setGradientFill (juce::ColourGradient (ui::mist, thumb.getX(), thumb.getY(), juce::Colour (0xffd9c9cb),
                                             thumb.getX(), thumb.getBottom(), false));
    g.fillPath (tp);
    g.setColour (ui::ember);
    g.drawLine (thumb.getX() + 7.0f * u, ty, thumb.getRight() - 7.0f * u, ty, std::max (1.5f, 2.4f * u));
}

//==============================================================================
KnobComponent::KnobComponent (juce::AudioProcessorValueTreeState& apvts, const char* paramId,
                              const juce::String& t, const juce::String& minLabel, const juce::String& maxLabel,
                              Layout l, const juce::String& tooltip, juce::LookAndFeel& lnf, bool bipolar)
    : title (t), layout (l)
{
    slider.setSliderStyle (l == Layout::fader ? juce::Slider::LinearVertical : l == Layout::horizontal ? juce::Slider::LinearHorizontal
                                              : juce::Slider::RotaryHorizontalVerticalDrag);   // glisser vertical OU horizontal
    slider.setRotaryParameters (1.25f * kPi, 2.75f * kPi, true);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setMouseDragSensitivity (220);
    slider.setLookAndFeel (&lnf);
    slider.setTooltip (tooltip);
    auto& props = slider.getProperties();
    props.set ("minLabel", minLabel);
    props.set ("maxLabel", maxLabel);
    props.set ("bipolar", bipolar);
    props.set ("shown", 0.0f);     // à l'ouverture, les arcs se déploient jusqu'à leur valeur
    props.set ("glow", 0.0f);

    if (auto* param = apvts.getParameter (paramId))
        slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));   // double-clic = reset

    slider.onDragStart = [this] { setFocus (hovering, true); };
    slider.onDragEnd   = [this] { setFocus (hovering, false); };
    slider.onValueChange = [this] { repaint (valueArea.getSmallestIntegerContainer().expanded (2)); };
    slider.addMouseListener (this, false);

    addAndMakeVisible (slider);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, paramId, slider);
    setBufferedToImage (true);   // repos : une image prête (les photos des pages plein écran et les fondus ne le redessinent pas)
}

KnobComponent::~KnobComponent()
{
    slider.removeMouseListener (this);
    slider.setLookAndFeel (nullptr);
}

void KnobComponent::setFocus (bool hover, bool drag)
{
    const bool was = hovering || dragging;
    hovering = hover;
    dragging = drag;
    if ((hovering || dragging) != was && onFocusChange) onFocusChange (hovering || dragging);
}

void KnobComponent::mouseEnter (const juce::MouseEvent&) { setFocus (true, dragging); }
void KnobComponent::mouseExit (const juce::MouseEvent&)  { setFocus (false, dragging); }

void KnobComponent::tick()
{
    const float target = (float) slider.valueToProportionOfLength (slider.getValue());
    bool changed = ui::approach (shown, target, 0.30f);
    if (activity) changed |= ui::approach (act, activity() > 0.5f ? 1.0f : 0.0f, 0.3f);
    changed |= ui::approach (glow, dragging ? 1.0f : (hovering ? 0.6f : 0.0f), 0.18f);
    if (changed)
    {
        slider.getProperties().set ("shown", shown);
        slider.getProperties().set ("glow", glow);
        slider.repaint();
        repaint (nameArea.getUnion (valueArea).getSmallestIntegerContainer().expanded (2));
    }
}

void KnobComponent::resized()
{
    const auto area = getLocalBounds().toFloat();
    juce::Rectangle<float> knob;

    if (layout == Layout::horizontal)
    {
        // une seule ligne : nom (36 %) | curseur | pastille de valeur (24 %)
        const float h = area.getHeight();
        unit = h / 30.0f;
        nameArea = area.withWidth (area.getWidth() * 0.36f);
        valueArea = area.withLeft (area.getRight() - area.getWidth() * 0.24f).reduced (0.0f, h * 0.08f);
        knob = area.withLeft (nameArea.getRight() + 4.0f * unit).withRight (valueArea.getX() - 6.0f * unit);
    }
    else if (layout != Layout::textRight)
    {
        // nom + bouton ou curseur (D) + pastille de valeur. Le nom et la pastille ont une taille MINIMALE liée à
        // la fenêtre : sur les petits boutons (multibande, 1176...) les textes restent lisibles, c'est le bouton
        // qui rétrécit un peu pour leur laisser la place.
        const bool fader = (layout == Layout::fader);
        const float es = ui::editorScale (*this);
        float D = std::max (10.0f, juce::jmin (maxD, fader ? area.getHeight() / 1.47f
                                                           : juce::jmin (area.getWidth() - 2.0f, area.getHeight() / 1.47f)));
        // curseur vertical : textes calés sur la largeur (un grand curseur, comme celui du NOISE SUPPRESSOR, garde un nom et
        // une pastille de valeur à taille normale)
        const float Dt = fader ? std::min (D, area.getWidth() * 1.3f) : D;
        const float nameH = std::max (0.15f * Dt, 17.0f * es), pillH = std::max (0.21f * Dt, 23.0f * es);
        D = std::max (10.0f, std::min (D, (area.getHeight() - nameH - pillH) / 1.11f));
        unit = D / 150.0f;
        const float total = nameH + 0.05f * D + D + 0.06f * D + pillH;
        const float top = area.getY() + (area.getHeight() - total) * 0.5f;
        nameArea = { area.getX(), top, area.getWidth(), nameH };
        const float knobTop = top + nameH + 0.05f * D;
        knob = fader ? juce::Rectangle<float> (area.getX(), knobTop, area.getWidth(), D)
                     : juce::Rectangle<float> (D, D).withCentre ({ area.getCentreX(), knobTop + D * 0.5f });
        const float pillW = juce::jmin (area.getWidth() * (fader ? 0.98f : 0.92f), std::max (Dt * 0.95f, 92.0f * es));
        valueArea = juce::Rectangle<float> (pillW, pillH).withCentre ({ area.getCentreX(), knob.getBottom() + 0.06f * D + pillH * 0.5f });
    }
    else
    {
        const float D = std::max (10.0f, juce::jmin (area.getHeight() - 2.0f, area.getWidth() * 0.50f));
        unit = D / 150.0f;
        knob = juce::Rectangle<float> (D, D).withCentre ({ area.getX() + D * 0.5f + 2.0f, area.getCentreY() });
        const float tx = knob.getRight() + 0.12f * D;
        const float tw = std::max (10.0f, area.getRight() - tx - 2.0f);
        const float blockH = 0.17f * D + 0.08f * D + 0.28f * D;
        const float ty = area.getCentreY() - blockH * 0.5f;
        nameArea  = { tx, ty, tw, 0.17f * D };
        valueArea = { tx, ty + 0.25f * D, std::min (tw, D * 1.3f), 0.28f * D };
    }

    slider.setBounds (knob.toNearestInt());
}

void KnobComponent::paint (juce::Graphics& g)
{
    const float u = unit;
    auto nameR = nameArea;
    if (activity)
    {
        // LED d'activité devant le nom (s'allume quand le module agit, ex. plosive détectée)
        const float d = nameR.getHeight() * 0.36f;
        // LED (et son halo) entièrement dans le composant : le halo n'est plus coupé par le bord gauche
        const auto led = juce::Rectangle<float> (d, d).withCentre ({ nameR.getX() + d * 1.6f, nameR.getCentreY() });
        g.setColour (ui::ember.withAlpha (0.45f * act));
        g.fillEllipse (led.expanded (d * 0.8f));
        g.setColour (ui::haze.withAlpha (0.35f).interpolatedWith (ui::ember, act));
        g.fillEllipse (led);
        nameR.removeFromLeft (d * 3.0f);
    }
    g.setColour (ui::haze.interpolatedWith (ui::mist, glow));
    g.setFont (ui::fitFont (ui::Face::body, layout == Layout::horizontal ? nameArea.getHeight() * 0.48f
                                          : std::max ({ 8.0f, (layout == Layout::textRight ? 18.0f : 15.0f) * u, nameArea.getHeight() * 0.72f }),
                            nameR.getWidth() - 2.0f, title, true, 0.14f));
    g.drawText (title, nameR, (layout == Layout::textRight || layout == Layout::horizontal) ? juce::Justification::centredLeft
                                                                                           : juce::Justification::centred, false);

    drawValuePill (g, valueArea, slider.getTextFromValue (slider.getValue()), glow, u);
}

//==============================================================================
DragValue::DragValue (juce::AudioProcessorValueTreeState& apvts, const char* paramId, const juce::String& tooltip)
{
    setTooltip (tooltip);
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    param = apvts.getParameter (paramId);
    if (param != nullptr)
    {
        attachment = std::make_unique<juce::ParameterAttachment> (*param, [this] (float) { repaint(); });
        attachment->sendInitialUpdate();
    }
}

void DragValue::tick()
{
    if (ui::approach (glow, dragging ? 1.0f : (hovering ? 0.6f : 0.0f), 0.18f)) repaint();
}

void DragValue::mouseEnter (const juce::MouseEvent&)
{
    hovering = true;
    if (! dragging && onFocusChange) onFocusChange (true);
}

void DragValue::mouseExit (const juce::MouseEvent&)
{
    hovering = false;
    if (! dragging && onFocusChange) onFocusChange (false);
}

void DragValue::mouseDown (const juce::MouseEvent&)
{
    if (param == nullptr) return;
    dragging = true;
    startNorm = param->getValue();
    attachment->beginGesture();
}

void DragValue::mouseDrag (const juce::MouseEvent& e)
{
    if (param == nullptr) return;
    const float range = e.mods.isShiftDown() ? 900.0f : 220.0f;   // Maj = réglage fin
    const float norm = juce::jlimit (0.0f, 1.0f, startNorm - (float) e.getDistanceFromDragStartY() / range);
    attachment->setValueAsPartOfGesture (param->convertFrom0to1 (norm));
}

void DragValue::mouseUp (const juce::MouseEvent&)
{
    if (param == nullptr) return;
    dragging = false;
    attachment->endGesture();
    if (! hovering && onFocusChange) onFocusChange (false);
}

void DragValue::mouseDoubleClick (const juce::MouseEvent&)
{
    if (param != nullptr) attachment->setValueAsCompleteGesture (param->convertFrom0to1 (param->getDefaultValue()));
}

void DragValue::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (param == nullptr) return;
    const float norm = juce::jlimit (0.0f, 1.0f, param->getValue() + w.deltaY * 0.06f);
    attachment->setValueAsCompleteGesture (param->convertFrom0to1 (norm));
}

void DragValue::paint (juce::Graphics& g)
{
    if (param == nullptr) return;
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const float u = r.getHeight() / 30.0f;
    drawValuePill (g, r, prefix + param->getCurrentValueAsText(), glow, u);
}

//==============================================================================
void PowerButton::tick()
{
    const bool st = getToggleState();
    if (st && ! was) ripple = 1.0f;   // allumé : une onde braise part du bouton
    was = st;
    bool changed = ui::approach (on, st ? 1.0f : 0.0f, 0.2f);
    if (ripple > 0.0f) { ripple = std::max (0.0f, ripple - 0.045f); changed = true; }
    if (changed) repaint();
}

void PowerButton::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    const auto b = getLocalBounds().toFloat();
    const float d = std::min (b.getWidth(), b.getHeight()) * 0.72f;
    if (d < 6.0f) return;
    const auto c = b.getCentre();
    const auto disk = juce::Rectangle<float> (d, d).withCentre (c);
    const float u = d / 24.0f;

    juce::Path ring;
    ring.addEllipse (disk);
    if (ripple > 0.0f)
    {
        const float e = 1.0f - ripple * ripple;   // part vite, ralentit en s'élargissant
        g.setColour (ui::ember.withAlpha (0.55f * ripple));
        g.drawEllipse (disk.expanded (d * 0.18f * e), std::max (1.0f, 1.6f * u * ripple));
    }
    ui::strokeGlow (g, ring, ui::ember, 1.4f * u, on * (highlighted ? 1.0f : 0.7f));
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillEllipse (disk);
    g.setColour (ui::ember.withAlpha (0.16f * on));
    g.fillEllipse (disk);
    g.setColour (ui::line.interpolatedWith (ui::ember, on).withMultipliedAlpha (highlighted ? 1.0f : 0.9f));
    g.drawEllipse (disk, std::max (1.0f, 1.4f * u));

    // pictogramme power : arc ouvert en haut + barre verticale, extrémités arrondies
    const float r = 0.26f * d, stroke = std::max (1.3f, 0.09f * d);
    juce::Path p;
    p.addCentredArc (c.x, c.y + 0.02f * d, r, r, 0.0f, 0.23f * kPi, 1.77f * kPi, true);
    p.startNewSubPath (c.x, c.y - 0.32f * d);
    p.lineTo (c.x, c.y - 0.04f * d);
    g.setColour (ui::haze.interpolatedWith (ui::bone, juce::jmax (on, highlighted ? 0.5f : 0.0f)));
    g.strokePath (p, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

//==============================================================================
void PillToggle::tick()
{
    const bool st = getToggleState();
    if (st && ! was) ripple = 1.0f;
    was = st;
    bool changed = ui::approach (on, st ? 1.0f : 0.0f, 0.16f);
    if (ripple > 0.0f) { ripple = std::max (0.0f, ripple - 0.05f); changed = true; }
    if (changed) repaint();
}

void PillToggle::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    const auto b = getLocalBounds().toFloat();
    const bool big = (style == Style::big);
    const float u = big ? std::min (b.getWidth() / 220.0f, b.getHeight() / 100.0f) : b.getHeight() / 36.0f;
    const auto r = b.reduced (big ? 10.0f * u : (style == Style::mini ? 2.0f * u : 6.0f * u));
    const float radius = big ? 18.0f * u : r.getHeight() * 0.5f;

    juce::Path shape;
    shape.addRoundedRectangle (r, radius);
    ui::strokeGlow (g, shape, ui::ember, 1.5f * u, on * 0.8f);
    g.setColour (juce::Colours::black.withAlpha (highlighted ? 0.28f : 0.36f));
    g.fillPath (shape);
    if (ripple > 0.0f)
    {
        // allumé : un reflet traverse la pastille de gauche à droite
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (shape);
        const float x = r.getX() + (1.0f - ripple) * (r.getWidth() + r.getHeight()) - r.getHeight();
        juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.0f), x, 0.0f, juce::Colours::white.withAlpha (0.0f), x + r.getHeight() * 1.6f, 0.0f, false);
        sheen.addColour (0.5, juce::Colours::white.withAlpha (0.22f * std::sqrt (ripple)));
        g.setGradientFill (sheen);
        g.fillRect (juce::Rectangle<float> (x, r.getY(), r.getHeight() * 1.6f, r.getHeight()));
    }
    if (on > 0.001f)
    {
        g.setGradientFill (juce::ColourGradient (ui::ember.withAlpha (0.30f * on), r.getX(), r.getY(),
                                                 ui::blood.withAlpha (0.25f * on), r.getRight(), r.getBottom(), false));
        g.fillPath (shape);
    }
    g.setColour (ui::line.interpolatedWith (ui::ember, on));
    g.strokePath (shape, juce::PathStrokeType (std::max (1.0f, 1.4f * u)));

    if (big)
    {
        const bool hasCaption = caption.isNotEmpty() && on > 0.5f;
        auto top = r.reduced (12.0f * u, 8.0f * u);
        auto capArea = hasCaption ? top.removeFromBottom (top.getHeight() * 0.28f) : juce::Rectangle<float>();

        const juce::String main = getToggleState() ? "ON" : "OFF";
        g.setColour (ui::haze.interpolatedWith (ui::bone, on));
        g.setFont (ui::fitFont (ui::Face::display, top.getHeight() * 0.56f, top.getWidth(), main, true, 0.12f));
        g.drawText (main, top, juce::Justification::centred, false);

        if (hasCaption)
        {
            g.setColour (ui::mist.withAlpha (0.8f));
            g.setFont (ui::fitFont (ui::Face::mono, std::max (8.0f, 15.0f * u), capArea.getWidth(), caption));
            g.drawText (caption, capArea, juce::Justification::centred, false);
        }
    }
    else
    {
        const float d = r.getHeight() * 0.26f;
        const float pad = r.getHeight() * 0.40f;
        const auto ledR = juce::Rectangle<float> (d, d).withCentre ({ r.getX() + pad + d * 0.5f, r.getCentreY() });
        g.setColour (ui::ember.withAlpha (0.35f * on));
        g.fillEllipse (ledR.expanded (d * 0.8f));
        g.setColour (ui::haze.withAlpha (0.35f).interpolatedWith (ui::ember, on));
        g.fillEllipse (ledR);

        const auto textR = r.withTrimmedLeft (pad * 2.0f + d).withTrimmedRight (pad * 0.8f);
        g.setColour (ui::haze.interpolatedWith (ui::bone, juce::jmax (on, highlighted ? 0.6f : 0.0f)));
        g.setFont (ui::fitFont (ui::Face::body, r.getHeight() * (style == Style::mini ? 0.52f : 0.40f), textR.getWidth(), getButtonText(), true, 0.10f));
        g.drawText (getButtonText(), textR, juce::Justification::centred, false);
    }
}

//==============================================================================
SegmentSelector::SegmentSelector (juce::AudioProcessorValueTreeState& apvts, const char* paramId,
                                  const juce::StringArray& l, const juce::StringArray& t)
    : labels (l), tips (t)
{
    if (auto* param = apvts.getParameter (paramId))
    {
        attachment = std::make_unique<juce::ParameterAttachment> (*param, [this] (float v)
        {
            const int idx = juce::jlimit (0, labels.size() - 1, juce::roundToInt (v));
            if (idx != selected) { selected = idx; repaint(); }
        });
        attachment->sendInitialUpdate();
    }
}

juce::Rectangle<float> SegmentSelector::segmentArea() const noexcept
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    if (title.isNotEmpty())
    {
        const auto f = ui::font (ui::Face::mono, r.getHeight() * 0.40f, true, 0.08f);
        r.removeFromLeft (ui::textWidth (f, title) + r.getHeight() * 0.9f);
    }
    return r;
}

int SegmentSelector::segmentAt (float x) const noexcept
{
    const auto a = segmentArea();
    if (labels.isEmpty() || a.getWidth() <= 0.0f || x < a.getX()) return -1;
    return juce::jlimit (0, labels.size() - 1, (int) ((x - a.getX()) * (float) labels.size() / a.getWidth()));
}

void SegmentSelector::setLocked (bool shouldLock, const juce::String& text, const juce::String& tip)
{
    if (shouldLock == locked && text == lockedText) return;
    locked = shouldLock;
    lockedText = text;
    lockedTip = tip;
    setMouseCursor (locked ? juce::MouseCursor::NormalCursor : juce::MouseCursor::PointingHandCursor);
    repaint();
}

void SegmentSelector::tick()
{
    bool changed = false;
    if (slide < 0.0f) { slide = (float) selected; changed = true; }
    changed |= ui::spring (slide, slideVel, (float) selected);   // la pastille glisse avec un léger ressort
    changed |= ui::approach (lockAmt, locked ? 1.0f : 0.0f, 0.18f);
    if (changed) repaint();
}

void SegmentSelector::mouseDown (const juce::MouseEvent& e)
{
    if (locked) return;
    const int idx = segmentAt (e.position.x);
    if (idx >= 0 && idx != selected && attachment != nullptr)
        attachment->setValueAsCompleteGesture ((float) idx);
}

void SegmentSelector::mouseMove (const juce::MouseEvent& e)
{
    const int h = locked ? -1 : segmentAt (e.position.x);
    if (h != hover) { hover = h; repaint(); }
}

void SegmentSelector::mouseExit (const juce::MouseEvent&)
{
    if (hover != -1) { hover = -1; repaint(); }
}

juce::String SegmentSelector::getTooltip()
{
    if (locked && lockedTip.isNotEmpty()) return lockedTip;
    const int idx = segmentAt ((float) getMouseXYRelative().x);
    return juce::isPositiveAndBelow (idx, tips.size()) ? tips[idx] : titleTip;
}

void SegmentSelector::paint (juce::Graphics& g)
{
    const int n = labels.size();
    if (n == 0) return;

    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const float u = r.getHeight() / 26.0f;
    const float radius = r.getHeight() * 0.5f;
    const auto a = segmentArea();
    const float segW = a.getWidth() / (float) n;
    const float pos = slide < 0.0f ? (float) selected : slide;
    const float dim = 1.0f - 0.65f * lockAmt;   // verrouillé : tout s'estompe

    g.setColour (juce::Colours::black.withAlpha (0.34f));
    g.fillRoundedRectangle (r, radius);
    g.setColour (ui::line);
    g.drawRoundedRectangle (r.reduced (0.5f), radius, std::max (1.0f, 1.1f * u));

    if (title.isNotEmpty())
    {
        g.setColour (ui::haze.withMultipliedAlpha (dim));
        g.setFont (ui::font (ui::Face::mono, r.getHeight() * 0.40f, true, 0.08f));
        g.drawText (title, juce::Rectangle<float> (r.getX(), r.getY(), a.getX() - r.getX(), r.getHeight()), juce::Justification::centred, false);
    }

    // pastille braise qui glisse vers le choix
    // segment étroit (ex. OFF / 2X / 4X / 8X) : pastille un peu moins haute, jamais un ovale vertical ; le texte garde
    // toute la largeur du segment (plus de pastille ronde qui écrasait les libellés)
    auto pill = juce::Rectangle<float> (a.getX() + segW * pos, a.getY(), segW, a.getHeight()).reduced (2.0f * u, 2.5f * u);
    if (pill.getWidth() < pill.getHeight() * 1.2f)
        pill = pill.withSizeKeepingCentre (pill.getWidth(), std::max (pill.getHeight() * 0.72f, std::min (pill.getHeight(), pill.getWidth() * 0.8f)));
    juce::Path pp;
    pp.addRoundedRectangle (pill, pill.getHeight() * 0.5f);
    ui::strokeGlow (g, pp, ui::ember, 1.0f * u, 0.6f * dim);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffEE4555).withMultipliedAlpha (dim), pill.getX(), pill.getY(),
                                             juce::Colour (0xffB81F30).withMultipliedAlpha (dim), pill.getX(), pill.getBottom(), false));
    g.fillPath (pp);

    // une seule taille de texte pour tous les choix, qui tient dans la pastille (ronde si le segment est étroit)
    const float fitW = pill.getWidth() - 4.0f * u;
    auto labelFont = ui::font (ui::Face::body, r.getHeight() * 0.48f, true, 0.04f);
    for (const auto& l : labels)
    {
        const float tw = ui::textWidth (labelFont, l);
        if (tw > fitW && tw > 0.0f) labelFont.setHeight (std::max (4.0f, labelFont.getHeight() * fitW / tw));
    }

    for (int i = 0; i < n; ++i)
    {
        const juce::Rectangle<float> seg (a.getX() + segW * (float) i, a.getY(), segW, a.getHeight());
        const float sel = juce::jlimit (0.0f, 1.0f, 1.0f - std::abs (pos - (float) i));
        const auto base = i == hover ? ui::mist : ui::haze;
        g.setColour (base.interpolatedWith (ui::night, sel).withMultipliedAlpha (dim));
        g.setFont (labelFont);
        g.drawText (labels[i], seg, juce::Justification::centred, false);
    }

    // verrouillé : message par-dessus (ex. "MIX MODE ONLY")
    if (lockAmt > 0.01f && lockedText.isNotEmpty())
    {
        const auto f = ui::fitFont (ui::Face::mono, r.getHeight() * 0.42f, a.getWidth() - 10.0f * u, lockedText, true, 0.08f);
        const float tw = ui::textWidth (f, lockedText) + 16.0f * u;
        const auto box = juce::Rectangle<float> (tw, r.getHeight() - 6.0f * u).withCentre (a.getCentre());
        g.setColour (ui::night.withAlpha (0.92f * lockAmt));
        g.fillRoundedRectangle (box, box.getHeight() * 0.5f);
        g.setColour (ui::line.withMultipliedAlpha (2.0f * lockAmt));
        g.drawRoundedRectangle (box, box.getHeight() * 0.5f, std::max (1.0f, u));
        g.setColour (ui::mist.withAlpha (lockAmt));
        g.setFont (f);
        g.drawText (lockedText, box, juce::Justification::centred, false);
    }
}

//==============================================================================
ResonanceDisplay::ResonanceDisplay (juce::AudioProcessorValueTreeState& apvts)
{
    const char* f[] { ids::resoLowFreq, ids::resoMidFreq, ids::resoHighFreq };
    const char* w[] { ids::resoLowWidth, ids::resoMidWidth, ids::resoHighWidth };
    const char* s[] { ids::resoLow, ids::resoMid, ids::resoHigh };
    for (int b = 0; b < kNumCtl; ++b)
    {
        auto& c = ctl[(size_t) b];
        c.freq = apvts.getParameter (f[b]);
        c.width = apvts.getParameter (w[b]);
        c.sens = apvts.getParameter (s[b]);
        c.f = value (c.freq);
        c.w = value (c.width);
        c.s = 0.0f;   // les zones montent à l'ouverture
    }
    spectrum.fill (-120.0f);
}

juce::Rectangle<float> ResonanceDisplay::plotArea() const noexcept
{
    const float u = (float) getHeight() / 200.0f;
    return getLocalBounds().toFloat().reduced (12.0f * u, 10.0f * u).withTrimmedBottom (16.0f * u).withTrimmedRight (26.0f * u);
}

float ResonanceDisplay::xForHz (float hz, juce::Rectangle<float> r) const noexcept
{
    using RS = ResonanceSuppressor;
    const float t = std::log (hz / RS::kMinHz) / std::log (RS::kMaxHz / RS::kMinHz);
    return r.getX() + juce::jlimit (0.0f, 1.0f, t) * r.getWidth();
}

float ResonanceDisplay::hzForX (float x, juce::Rectangle<float> r) const noexcept
{
    using RS = ResonanceSuppressor;
    const float t = juce::jlimit (0.0f, 1.0f, (x - r.getX()) / r.getWidth());
    return RS::kMinHz * std::pow (RS::kMaxHz / RS::kMinHz, t);
}

juce::Point<float> ResonanceDisplay::handlePos (int band, juce::Rectangle<float> plot) const noexcept
{
    const auto& c = ctl[(size_t) band];
    return { xForHz (c.f, plot), plot.getBottom() - c.s * plot.getHeight() * 0.94f };
}

int ResonanceDisplay::handleAt (juce::Point<float> p) const noexcept
{
    const auto plot = plotArea();
    const float rMax = 16.0f * (float) getHeight() / 200.0f;
    int best = -1;
    float bestD = rMax;
    for (int b = 0; b < kNumCtl; ++b)
    {
        const float d = handlePos (b, plot).getDistanceFrom (p);
        if (d < bestD) { bestD = d; best = b; }
    }
    return best;
}

void ResonanceDisplay::updateMeter (const ResonanceSuppressor::Meter& m, bool active)
{
    float pk = 0.0f;
    for (int b = 0; b < kBands; ++b)
    {
        const float rt = active ? m.reductionDb[(size_t) b].load (std::memory_order_relaxed) : 0.0f;
        const float st = m.spectrumDb[(size_t) b].load (std::memory_order_relaxed);
        auto& r = red[(size_t) b];
        auto& s = spectrum[(size_t) b];
        r = rt > r ? r + 0.6f * (rt - r) : r + 0.18f * (rt - r);
        s = st > s ? s + 0.6f * (st - s) : s + 0.12f * (st - s);
        pk = std::max (pk, r);
    }
    peakRed = pk;
    isActive = active;
    repaint();
}

void ResonanceDisplay::setFocusBand (int band, bool focused)
{
    if (juce::isPositiveAndBelow (band, kNumCtl)) ctl[(size_t) band].focused = focused;
}

void ResonanceDisplay::tick()
{
    bool changed = false;
    for (int b = 0; b < kNumCtl; ++b)
    {
        auto& c = ctl[(size_t) b];
        changed |= ui::approach (c.f, value (c.freq), 0.3f);
        changed |= ui::approach (c.w, value (c.width), 0.3f);
        changed |= ui::approach (c.s, value (c.sens) * 0.01f, 0.2f);
        const bool f = c.focused || b == dragHandle || b == hoverHandle;
        changed |= ui::approach (c.focus, f ? 1.0f : 0.0f, 0.16f);
    }
    if (changed) repaint();
}

void ResonanceDisplay::mouseMove (const juce::MouseEvent& e)
{
    const int h = handleAt (e.position);
    if (h != hoverHandle)
    {
        hoverHandle = h;
        setMouseCursor (h >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
    }
}

void ResonanceDisplay::mouseExit (const juce::MouseEvent&)
{
    if (dragHandle < 0) hoverHandle = -1;
}

void ResonanceDisplay::mouseDown (const juce::MouseEvent& e)
{
    dragHandle = handleAt (e.position);
    if (dragHandle < 0) return;
    auto& c = ctl[(size_t) dragHandle];
    c.freq->beginChangeGesture();
    c.sens->beginChangeGesture();
}

void ResonanceDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (dragHandle < 0) return;
    auto& c = ctl[(size_t) dragHandle];
    const auto plot = plotArea();
    const auto range = c.freq->getNormalisableRange();
    setValue (c.freq, juce::jlimit (range.start, range.end, hzForX (e.position.x, plot)));
    const float s = juce::jlimit (0.0f, 1.0f, (plot.getBottom() - e.position.y) / (plot.getHeight() * 0.94f));
    setValue (c.sens, s * 100.0f);
}

void ResonanceDisplay::mouseUp (const juce::MouseEvent&)
{
    if (dragHandle < 0) return;
    auto& c = ctl[(size_t) dragHandle];
    c.freq->endChangeGesture();
    c.sens->endChangeGesture();
    dragHandle = -1;
}

void ResonanceDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int h = handleAt (e.position);
    if (h < 0) return;
    for (auto* p : { ctl[(size_t) h].freq, ctl[(size_t) h].width, ctl[(size_t) h].sens })
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->getDefaultValue());
        p->endChangeGesture();
    }
}

void ResonanceDisplay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    // largeur de la bande survolée (ou de celle dont la zone contient la souris)
    int h = handleAt (e.position);
    if (h < 0)
    {
        const float hz = hzForX (e.position.x, plotArea());
        float best = 0.05f;
        for (int b = 0; b < kNumCtl; ++b)
        {
            const auto& c = ctl[(size_t) b];
            const float v = ResonanceSuppressor::bandShape (hz, c.f, c.w);
            if (v > best) { best = v; h = b; }
        }
    }
    if (h < 0) return;
    auto* p = ctl[(size_t) h].width;
    p->beginChangeGesture();
    setValue (p, juce::jlimit (0.3f, 4.0f, value (p) + w.deltaY * 0.6f));
    p->endChangeGesture();
}

juce::String ResonanceDisplay::getTooltip()
{
    static const char* names[] { "LOW", "MID", "HIGH" };
    const int h = handleAt (getMouseXYRelative().toFloat());
    if (h >= 0)
        return juce::String (names[h]) + " band: drag left/right = frequency, up/down = sensitivity, "
               "mouse wheel = width, double-click = reset.";
    return "Resonance display: grey = input spectrum, glowing zones = targeted frequencies, "
           "red from the top = reduction applied. Mouse wheel over a zone changes its width.";
}

void ResonanceDisplay::paint (juce::Graphics& g)
{
    using RS = ResonanceSuppressor;
    static const char* names[] { "LOW", "MID", "HIGH" };
    const auto area = getLocalBounds().toFloat();
    const float u = area.getHeight() / 200.0f;
    const auto plot = plotArea();

    // ---- écran
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.55f), 0.0f, area.getY(),
                                             ui::night.withAlpha (0.85f), 0.0f, area.getBottom(), false));
    g.fillRoundedRectangle (area, 12.0f * u);
    g.setColour (ui::line);
    g.drawRoundedRectangle (area.reduced (0.5f), 12.0f * u, std::max (1.0f, 1.1f * u));

    // ---- grille
    g.setFont (ui::font (ui::Face::mono, std::max (10.5f * u, 10.5f * ui::editorScale (*this))));
    for (float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = xForHz (hz, plot);
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
        const juce::String txt = hz >= 1000.0f ? juce::String ((int) (hz / 1000.0f)) + "k" : juce::String ((int) hz);
        g.setColour (ui::haze.withAlpha (0.8f));
        g.drawText (txt, juce::Rectangle<float> (40.0f * u, 16.0f * u).withCentre ({ x, plot.getBottom() + 9.0f * u }),
                    juce::Justification::centred, false);
    }
    for (float db : { 6.0f, 12.0f, 18.0f })
    {
        const float y = plot.getY() + db / RS::kMaxRedDb * plot.getHeight();
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
        g.setColour (ui::haze.withAlpha (0.8f));
        g.drawText ("-" + juce::String ((int) db), juce::Rectangle<float> (plot.getRight() + 5.0f * u, y - 7.0f * u, 24.0f * u, 14.0f * u),
                    juce::Justification::centredLeft, false);
    }

    auto bandX = [&] (int b)
    {
        return xForHz (RS::kMinHz * std::pow (RS::kMaxHz / RS::kMinHz, ((float) b + 0.5f) / (float) kBands), plot);
    };

    // ---- spectre d'entrée (-90..0 dBFS)
    {
        juce::Path sp;
        sp.startNewSubPath (plot.getX(), plot.getBottom());
        for (int b = 0; b < kBands; ++b)
        {
            const float v = juce::jlimit (0.0f, 1.0f, (spectrum[(size_t) b] + 90.0f) / 90.0f);
            sp.lineTo (bandX (b), plot.getBottom() - v * plot.getHeight());
        }
        sp.lineTo (plot.getRight(), plot.getBottom());
        sp.closeSubPath();
        g.setGradientFill (juce::ColourGradient (ui::bone.withAlpha (0.10f), 0.0f, plot.getY(),
                                                 ui::bone.withAlpha (0.02f), 0.0f, plot.getBottom(), false));
        g.fillPath (sp);
        g.setColour (ui::haze.withAlpha (0.35f));
        g.strokePath (sp, juce::PathStrokeType (std::max (1.0f, 1.1f * u)));
    }

    // ---- zones ciblées : hauteur = sensibilité, forme = fréquence + largeur de chaque bande
    for (int b = 0; b < kNumCtl; ++b)
    {
        const auto& c = ctl[(size_t) b];
        if (c.s < 0.002f && c.focus < 0.002f) continue;
        juce::Path zone, edge;
        const int steps = 180;
        zone.startNewSubPath (plot.getX(), plot.getBottom());
        for (int i = 0; i <= steps; ++i)
        {
            const float x = plot.getX() + plot.getWidth() * (float) i / (float) steps;
            const float v = c.s * RS::bandShape (hzForX (x, plot), c.f, c.w);
            const float y = plot.getBottom() - v * plot.getHeight() * 0.94f;
            zone.lineTo (x, y);
            if (i == 0) edge.startNewSubPath (x, y); else edge.lineTo (x, y);
        }
        zone.lineTo (plot.getRight(), plot.getBottom());
        zone.closeSubPath();

        const float a = isActive ? 1.0f : 0.4f;
        const float topY = plot.getBottom() - c.s * plot.getHeight() * 0.94f;
        g.setGradientFill (juce::ColourGradient (ui::ember.withAlpha ((0.16f + 0.22f * c.focus) * a), 0.0f, topY,
                                                 ui::ember.withAlpha (0.02f * a), 0.0f, plot.getBottom(), false));
        g.fillPath (zone);
        ui::strokeGlow (g, edge, ui::ember, 1.2f * u, (0.25f + 0.75f * c.focus) * a);
        g.setColour (ui::ember.withAlpha ((0.55f + 0.45f * c.focus) * a));
        g.strokePath (edge, juce::PathStrokeType (std::max (1.0f, 1.6f * u)));
    }

    // ---- réduction appliquée : descend depuis le haut, dégradé os -> braise
    {
        juce::Path rp, edge;
        rp.startNewSubPath (plot.getX(), plot.getY());
        for (int b = 0; b < kBands; ++b)
        {
            const float y = plot.getY() + juce::jlimit (0.0f, 1.0f, red[(size_t) b] / RS::kMaxRedDb) * plot.getHeight();
            rp.lineTo (bandX (b), y);
            if (b == 0) edge.startNewSubPath (bandX (b), y); else edge.lineTo (bandX (b), y);
        }
        rp.lineTo (plot.getRight(), plot.getY());
        rp.closeSubPath();
        g.setGradientFill (juce::ColourGradient (ui::bone.withAlpha (0.9f), 0.0f, plot.getY(),
                                                 ui::ember.withAlpha (0.9f), 0.0f, plot.getY() + plot.getHeight() * 0.5f, false));
        g.fillPath (rp);
        if (peakRed > 0.05f) ui::strokeGlow (g, edge, ui::ember, 1.4f * u, 0.5f);
    }

    // ---- poignées de bande + étiquettes (fréquence et largeur quand la bande est active)
    for (int b = 0; b < kNumCtl; ++b)
    {
        const auto& c = ctl[(size_t) b];
        const auto hp = handlePos (b, plot);
        const float r = (6.5f + 2.0f * c.focus) * u;
        juce::Path ring;
        ring.addEllipse (juce::Rectangle<float> (2.0f * r, 2.0f * r).withCentre (hp));
        ui::strokeGlow (g, ring, ui::ember, 1.5f * u, 0.4f + 0.6f * c.focus);
        g.setColour (ui::night);
        g.fillPath (ring);
        g.setColour (ui::ember);
        g.strokePath (ring, juce::PathStrokeType (std::max (1.2f, 2.0f * u)));
        g.setColour (ui::bone);
        g.fillEllipse (juce::Rectangle<float> (r * 0.8f, r * 0.8f).withCentre (hp));

        juce::String label (names[b]);
        if (c.focus > 0.5f)
            label << "  " << c.freq->getCurrentValueAsText() << "  /  " << c.width->getCurrentValueAsText();
        const auto f = ui::font (c.focus > 0.5f ? ui::Face::mono : ui::Face::body, std::max (11.5f * u, 11.5f * ui::editorScale (*this)), c.focus <= 0.5f, 0.1f);
        const float tw = ui::textWidth (f, label) + 6.0f * u;
        float ly = hp.y - r - 15.0f * u;
        if (ly < plot.getY()) ly = hp.y + r + 3.0f * u;
        const float lx = juce::jlimit (plot.getX(), plot.getRight() - tw, hp.x - tw * 0.5f);
        g.setColour (ui::mist.withAlpha (0.65f + 0.35f * c.focus));
        g.setFont (f);
        g.drawText (label, juce::Rectangle<float> (lx, ly, tw, 13.0f * u), juce::Justification::centred, false);
    }

    // ---- réduction max
    {
        const juce::String txt = isActive ? (peakRed < 0.05f ? juce::String ("GR 0.0 dB") : "GR -" + juce::String (peakRed, 1) + " dB")
                                          : juce::String ("OFF");
        const auto f = ui::fitFont (ui::Face::mono, std::max (8.0f, 12.5f * u), 110.0f * u, txt, true);
        const float bw = ui::textWidth (f, txt) + 18.0f * u;
        const auto box = juce::Rectangle<float> (bw, 22.0f * u).withPosition (plot.getRight() - bw - 4.0f * u, plot.getY() + 4.0f * u);
        g.setColour (ui::night.withAlpha (0.85f));
        g.fillRoundedRectangle (box, box.getHeight() * 0.5f);
        g.setColour (peakRed > 0.05f ? ui::ember : ui::line);
        g.drawRoundedRectangle (box, box.getHeight() * 0.5f, std::max (1.0f, 1.2f * u));
        g.setColour (ui::mist);
        g.setFont (f);
        g.drawText (txt, box, juce::Justification::centred, false);
    }
}

//==============================================================================
void VuMeter::tick()
{
    const float target = active ? juce::jlimit (0.0f, 30.0f, read()) : 0.0f;
    bool changed = ui::approach (needle, target, 0.16f);      // balistique VU (~300 ms pour 99 %)
    changed |= ui::approach (shownDb, target, 0.10f);
    if (changed) repaint();
}

void VuMeter::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const float u = r.getHeight() / 100.0f;
    juce::Path face;
    face.addRoundedRectangle (r, 10.0f * u);

    g.setGradientFill (juce::ColourGradient (ui::deep2, 0.0f, r.getY(), ui::night, 0.0f, r.getBottom(), false));
    g.fillPath (face);
    g.setGradientFill (juce::ColourGradient (ui::ember.withAlpha (0.13f), r.getCentreX(), r.getBottom(),
                                             ui::ember.withAlpha (0.0f), r.getCentreX(), r.getY(), true));
    g.fillPath (face);

    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (face);

        const juce::Point<float> pivot { r.getCentreX(), r.getBottom() + r.getHeight() * 0.22f };
        const float R = r.getHeight() * 1.02f;
        auto angleFor = [] (float gr) { return 0.66f - 1.32f * std::pow (juce::jlimit (0.0f, 1.0f, gr / 20.0f), 0.6f); };
        auto at = [&] (float ang, float rad) { return juce::Point<float> (pivot.x + std::sin (ang) * rad, pivot.y - std::cos (ang) * rad); };

        // échelle : arc, graduations (rouge au-delà de 10 dB), chiffres
        juce::Path arc, red;
        arc.addCentredArc (pivot.x, pivot.y, R * 0.90f, R * 0.90f, 0.0f, angleFor (20.0f), angleFor (0.0f), true);
        red.addCentredArc (pivot.x, pivot.y, R * 0.90f, R * 0.90f, 0.0f, angleFor (20.0f), angleFor (10.0f), true);
        g.setColour (ui::haze.withAlpha (0.45f));
        g.strokePath (arc, juce::PathStrokeType (std::max (1.0f, 1.2f * u)));
        g.setColour (ui::ember.withAlpha (0.8f));
        g.strokePath (red, juce::PathStrokeType (std::max (1.5f, 2.6f * u)));

        g.setFont (ui::font (ui::Face::mono, std::max (10.0f * u, 10.0f * ui::editorScale (*this))));
        for (float db : { 0.0f, 1.0f, 2.0f, 3.0f, 5.0f, 7.0f, 10.0f, 15.0f, 20.0f })
        {
            const bool major = db == 0.0f || db == 3.0f || db == 5.0f || db == 10.0f || db == 20.0f;
            const float a = angleFor (db);
            g.setColour (db >= 10.0f ? ui::ember : ui::haze.withAlpha (0.8f));
            g.drawLine ({ at (a, R * 0.90f), at (a, R * (major ? 0.99f : 0.95f)) }, std::max (1.0f, (major ? 1.6f : 1.1f) * u));
            if (major)
                g.drawText (juce::String ((int) db), juce::Rectangle<float> (24.0f * u, 12.0f * u).withCentre (at (a, R * 0.80f)),
                            juce::Justification::centred, false);
        }

        // aiguille
        juce::Path nd;
        nd.startNewSubPath (pivot);
        nd.lineTo (at (angleFor (needle), R * 0.98f));
        ui::strokeGlow (g, nd, ui::ember, 1.4f * u, active ? 0.6f : 0.0f);
        g.setColour (active ? ui::bone : ui::haze.withAlpha (0.5f));
        g.strokePath (nd, juce::PathStrokeType (std::max (1.2f, 1.8f * u), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // valeur + reflet de verre
    const juce::String txt = "GR " + (shownDb < 0.05f ? juce::String ("0.0") : "-" + juce::String (shownDb, 1));
    g.setColour (ui::mist.withAlpha (active ? 0.9f : 0.4f));
    g.setFont (ui::fitFont (ui::Face::mono, 12.0f * u, r.getWidth() * 0.8f, txt, true));
    g.drawText (txt, r.withTrimmedTop (r.getHeight() * 0.62f).withTrimmedBottom (6.0f * u), juce::Justification::centredBottom, false);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.06f), 0.0f, r.getY(),
                                             juce::Colours::white.withAlpha (0.0f), 0.0f, r.getCentreY(), false));
    g.fillRoundedRectangle (r.withHeight (r.getHeight() * 0.5f), 10.0f * u);
    g.setColour (ui::line);
    g.strokePath (face, juce::PathStrokeType (std::max (1.0f, 1.1f * u)));
}

//==============================================================================
ChainOrder::ChainOrder (juce::AudioProcessorValueTreeState& apvts, const char* modeId, const char* orderId)
{
    if (auto* p = apvts.getParameter (modeId))
    {
        modeAtt = std::make_unique<juce::ParameterAttachment> (*p, [this] (float v) { mode = juce::jlimit (0, 2, juce::roundToInt (v)); repaint(); });
        modeAtt->sendInitialUpdate();
    }
    if (auto* p = apvts.getParameter (orderId))
    {
        orderAtt = std::make_unique<juce::ParameterAttachment> (*p, [this] (float v) { order = juce::jlimit (0, 1, juce::roundToInt (v)); repaint(); });
        orderAtt->sendInitialUpdate();
    }
}

juce::Rectangle<float> ChainOrder::slotRect (int slot) const noexcept
{
    const auto r = getLocalBounds().toFloat();
    const float arrowW = r.getHeight() * 1.5f;
    const float chipW = (r.getWidth() - arrowW) * 0.5f;
    return { slot == 0 ? r.getX() : r.getRight() - chipW, r.getY(), chipW, r.getHeight() };
}

int ChainOrder::chipAt (juce::Point<float> p) const noexcept
{
    const float w = slotRect (0).getWidth();
    for (int c = 0; c < 2; ++c)
        if (p.x >= chipX[c] && p.x <= chipX[c] + w && p.y >= 0.0f && p.y <= (float) getHeight()) return c;
    return -1;
}

void ChainOrder::tick()
{
    const float w = slotRect (0).getWidth();
    const float mid = (float) getWidth() * 0.5f;
    bool changed = false;

    // pendant un glisser, l'autre pastille glisse déjà vers la place libérée (aperçu de l'échange)
    int dragSlot = -1;
    if (dragChip >= 0) dragSlot = (dragX + w * 0.5f > mid) ? 1 : 0;

    for (int c = 0; c < 2; ++c)
    {
        float target;
        if (c == dragChip) target = dragX;
        else if (dragChip >= 0) target = slotRect (1 - dragSlot).getX();
        else target = slotRect (slotOf (c)).getX();

        if (chipX[c] < 0.0f) { chipX[c] = target; changed = true; }
        changed |= ui::approach (chipX[c], target, c == dragChip ? 1.0f : 0.25f);
        changed |= ui::approach (lit[c], compActive (c) ? 1.0f : 0.0f, 0.15f);
    }
    changed |= ui::approach (duoAmt, mode == 2 ? 1.0f : 0.0f, 0.15f);
    if (duoAmt > 0.01f) { flow = std::fmod (flow + 0.025f, 1.0f); changed = true; }
    if (changed) repaint();
}

void ChainOrder::mouseDown (const juce::MouseEvent& e)
{
    dragChip = chipAt (e.position);
    if (dragChip < 0) return;
    dragOffset = e.position.x - chipX[dragChip];
    dragX = chipX[dragChip];
    dragMoved = false;
}

void ChainOrder::mouseDrag (const juce::MouseEvent& e)
{
    if (dragChip < 0) return;
    const float w = slotRect (0).getWidth();
    dragX = juce::jlimit (0.0f, (float) getWidth() - w, e.position.x - dragOffset);
    if (e.getDistanceFromDragStart() > 3) dragMoved = true;
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

void ChainOrder::mouseUp (const juce::MouseEvent&)
{
    if (dragChip < 0) return;
    const float w = slotRect (0).getWidth();
    if (dragMoved)
    {
        const int slot = (dragX + w * 0.5f > (float) getWidth() * 0.5f) ? 1 : 0;
        if (slot != slotOf (dragChip) && orderAtt != nullptr)
            orderAtt->setValueAsCompleteGesture ((float) (1 - order));   // échange : l'autre prend la place libérée
    }
    else if (mode != 2 && modeAtt != nullptr)
        modeAtt->setValueAsCompleteGesture ((float) dragChip);           // en solo : un clic choisit le compresseur
    dragChip = -1;
    setMouseCursor (juce::MouseCursor::NormalCursor);
}

void ChainOrder::mouseMove (const juce::MouseEvent& e)
{
    const int h = chipAt (e.position);
    if (h != hoverChip) { hoverChip = h; repaint(); }
    setMouseCursor (h >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
}

void ChainOrder::mouseExit (const juce::MouseEvent&)
{
    if (hoverChip != -1) { hoverChip = -1; repaint(); }
}

juce::String ChainOrder::getTooltip()
{
    return mode == 2 ? "Processing order: drag one compressor onto the other to swap who comes first."
                     : "Click a compressor to use it alone. Switch to DUO to chain both, then drag to set the order.";
}

void ChainOrder::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    const float u = r.getHeight() / 40.0f;
    static const char* names[] { "1176", "LA-2A" };
    static const char* subs[] { "FET", "OPTO" };

    // flèche de flux entre les deux places : chevrons qui pulsent en DUO
    {
        const auto s0 = slotRect (0), s1 = slotRect (1);
        const float x0 = s0.getRight() + 6.0f * u, x1 = s1.getX() - 6.0f * u, cy = r.getCentreY();
        for (int i = 0; i < 3; ++i)
        {
            const float ph = std::fmod ((float) i / 3.0f - flow + 1.0f, 1.0f);
            const float x = x0 + (x1 - x0) * (0.2f + 0.3f * (float) i);
            const float a = 0.25f + 0.75f * duoAmt * (0.5f + 0.5f * std::sin (ph * 6.2831853f));
            juce::Path ch;
            ch.startNewSubPath (x - 3.0f * u, cy - 5.0f * u);
            ch.lineTo (x + 3.0f * u, cy);
            ch.lineTo (x - 3.0f * u, cy + 5.0f * u);
            g.setColour (ui::haze.interpolatedWith (ui::ember, duoAmt).withAlpha (a));
            g.strokePath (ch, juce::PathStrokeType (std::max (1.2f, 2.0f * u), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    // pastilles (celle qu'on glisse est dessinée par-dessus)
    const float w = slotRect (0).getWidth();
    for (int pass = 0; pass < 2; ++pass)
        for (int c = 0; c < 2; ++c)
        {
            if ((pass == 1) != (c == dragChip)) continue;
            const float x = chipX[c] < 0.0f ? slotRect (slotOf (c)).getX() : chipX[c];
            const auto chip = juce::Rectangle<float> (x, r.getY(), w, r.getHeight()).reduced (2.0f * u);
            const float l = lit[c];
            const float hov = (c == hoverChip || c == dragChip) ? 1.0f : 0.0f;
            const float rad = chip.getHeight() * 0.5f;
            juce::Path p;
            p.addRoundedRectangle (chip, rad);
            ui::strokeGlow (g, p, ui::ember, 1.2f * u, l * (0.5f + 0.4f * hov));
            g.setColour (juce::Colours::black.withAlpha (0.42f));
            g.fillPath (p);
            g.setGradientFill (juce::ColourGradient (ui::ember.withAlpha (0.28f * l), chip.getX(), chip.getY(),
                                                     ui::blood.withAlpha (0.20f * l), chip.getRight(), chip.getBottom(), false));
            g.fillPath (p);
            g.setColour (ui::line.interpolatedWith (ui::ember, l));
            g.strokePath (p, juce::PathStrokeType (std::max (1.0f, 1.3f * u)));

            // numéro de passage (1 / 2) en DUO
            const float bd = chip.getHeight() * 0.56f;
            const auto badge = juce::Rectangle<float> (bd, bd).withCentre ({ chip.getX() + rad, chip.getCentreY() });
            g.setColour (ui::night.withAlpha (0.8f));
            g.fillEllipse (badge);
            g.setColour (ui::ember.withAlpha (duoAmt * l));
            g.drawEllipse (badge, std::max (1.0f, 1.2f * u));
            g.setColour (ui::haze.interpolatedWith (ui::bone, duoAmt * l));
            g.setFont (ui::font (ui::Face::mono, bd * 0.62f, true));
            g.drawText (duoAmt > 0.5f ? juce::String (slotOf (c) + 1) : juce::String ("-"), badge, juce::Justification::centred, false);

            // nom + type + poignée de glisser
            auto text = chip.withTrimmedLeft (rad * 2.0f + 4.0f * u).withTrimmedRight (rad);
            const auto nf = ui::fitFont (ui::Face::display, chip.getHeight() * 0.44f, text.getWidth() * 0.62f, names[c], true, 0.06f);
            const float nw = ui::textWidth (nf, names[c]) + 4.0f * u;
            g.setColour (ui::haze.interpolatedWith (ui::bone, juce::jmax (l, 0.5f * hov)));
            g.setFont (nf);
            g.drawText (names[c], text.withWidth (nw), juce::Justification::centredLeft, false);
            g.setColour (ui::haze.withAlpha (0.8f));
            g.setFont (ui::font (ui::Face::mono, chip.getHeight() * 0.28f));
            g.drawText (subs[c], text.withTrimmedLeft (nw + 4.0f * u), juce::Justification::centredLeft, false);

            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = 0; dx < 2; ++dx)
                {
                    g.setColour (ui::haze.withAlpha (0.35f + 0.4f * hov));
                    g.fillEllipse (juce::Rectangle<float> (2.6f * u, 2.6f * u)
                                       .withCentre ({ chip.getRight() - rad * 0.9f - (float) dx * 5.0f * u, chip.getCentreY() + (float) dy * 5.0f * u }));
                }
        }
}

//==============================================================================
SpectrumAnalyzer::SpectrumAnalyzer (OniriqueVocalBoxAudioProcessor& p, bool fullScreen) : bubble (p.apvts, p.peqListenHold), processor (p), full (fullScreen)
{
    addChildComponent (bubble);
    bubble.onRemoved = [this] { if (onBackgroundClick) onBackgroundClick(); };
    window.resize ((size_t) kN);
    double sum = 0.0;
    for (int i = 0; i < kN; ++i)
    {
        window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (6.283185307179586 * i / kN));
        sum += window[(size_t) i];
    }
    windowGain = (float) (2.0 / sum);          // un sinus d'amplitude A lit 20 log10 A (dBFS)
    histIn.assign ((size_t) kN, 0.0f);
    histOut.assign ((size_t) kN, 0.0f);
    scratch.assign ((size_t) onirique::SpectrumTap::kSize, 0.0f);
    fftBuf.assign ((size_t) (2 * kN), 0.0f);
    specIn.fill (-140.0f);
    specOut.fill (-140.0f);
    for (auto* c : { &curve, &maxCurve, &selCurve, &analogCurve }) c->fill (0.0f);
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
    setOpaque (full);
}

SpectrumAnalyzer::~SpectrumAnalyzer() = default;

// bulle de la bande sélectionnée : au-dessus de son point (dessous s'il n'y a pas la place), gardée dans l'analyseur
void SpectrumAnalyzer::placeBubble()
{
    const auto bd = band (selected);
    const bool show = bd.on;
    if (show != bubble.isVisible()) bubble.setVisible (show);
    if (! show) return;
    bubble.setBand (selected);
    const float u = unit();
    const float w = 168.0f * u, bh = 70.0f * u;
    const auto c = nodePos (selected);
    const auto area = getLocalBounds().toFloat();
    const bool down = c.y - 14.0f * u - bh >= area.getY() + 2.0f;   // la bulle pointe vers le bas (elle est au-dessus du point)
    bubble.setPointsDown (down);
    auto r = juce::Rectangle<float> (w, bh).withCentre ({ c.x, down ? c.y - 14.0f * u - bh * 0.5f : c.y + 14.0f * u + bh * 0.5f });
    r.setX (juce::jlimit (area.getX() + 2.0f, area.getRight() - w - 2.0f, r.getX()));
    if (r.toNearestInt() != bubble.getBounds()) bubble.setBounds (r.toNearestInt());
}

// chaque point de l'écran : moyenne gaussienne de 1/12 d'octave des bins voisins (voir SpectrumSmoother.h)
void SpectrumAnalyzer::buildMap (double fs)
{
    mapFs = fs;
    smoother.build (fs, kN, kPts);
}

bool SpectrumAnalyzer::analyse (onirique::SpectrumTap& tap, std::vector<float>& hist, int& writePos, std::array<float, kPts>& spec)
{
    bool moved = false;
    // récupère tout ce qui est arrivé depuis la dernière image, garde les kN derniers échantillons
    const int got = tap.pull (scratch.data(), (int) scratch.size());
    for (int i = std::max (0, got - kN); i < got; ++i)
    {
        hist[(size_t) writePos] = scratch[(size_t) i];
        writePos = (writePos + 1) & (kN - 1);
    }

    for (int i = 0; i < kN; ++i)
        fftBuf[(size_t) i] = hist[(size_t) ((writePos + i) & (kN - 1))] * window[(size_t) i];
    std::fill (fftBuf.begin() + kN, fftBuf.end(), 0.0f);
    fft.performFrequencyOnlyForwardTransform (fftBuf.data(), true);
    for (int k = 0; k <= kN / 2; ++k)
    {
        const float a = fftBuf[(size_t) k] * windowGain;
        fftBuf[(size_t) k] = a * a;                // puissance
    }

    std::array<float, kPts> level;
    smoother.apply (fftBuf.data(), level);
    for (int i = 0; i < kPts; ++i)
    {
        const float db = level[(size_t) i];
        auto& s = spec[(size_t) i];
        const float before = s;
        s += (db > s ? 0.70f : 0.13f) * (db - s);  // montée vive, retombée douce (~250 ms), à 30 analyses par seconde
        moved |= std::abs (s - before) > 0.05f && std::max (s, before) > -105.0f;   // sous -105 dB : hors de l'affichage
    }
    return moved;
}

void SpectrumAnalyzer::tick()
{
    if (! active || ! isShowing()) return;
    const double fs = processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 48000.0;
    if (std::abs (fs - mapFs) > 0.5) buildMap (fs);

    flushDrag();
    bubble.tick();
    placeBubble();
    // spectres : 30 analyses par seconde ; rien n'est redessiné tant que ni le spectre ni l'EQ ne bougent (silence,
    // lecture arrêtée). Pendant un réglage, la courbe et les points restent à 60 images par seconde.
    bool specChanged = false;
    if ((++tickCount & 1) == 0)
    {
        specChanged |= analyse (processor.inputTap, histIn, writeIn, specIn);
        specChanged |= analyse (processor.outputTap, histOut, writeOut, specOut);
    }
    bool eqChanged = updateCurves (fs);

    for (int b = 0; b < kPeq; ++b)
        eqChanged |= ui::approach (nodeGlow[b], (b == selected ? 0.6f : 0.0f) + (b == hoverNode || b == dragNode || b == qFlashBand ? 0.4f : 0.0f), 0.2f);
    if (qFlashFrames > 0)
    {
        eqChanged = true;
        if (--qFlashFrames == 0) qFlashBand = -1;
    }
    if (eqChanged) eqDirty = true;
    if (eqChanged || specChanged) repaint();
}

// courbes : valeurs animées vers les réglages, puis réponse exacte des filtres (recalculée seulement si quelque chose bouge)
bool SpectrumAnalyzer::updateCurves (double fs)
{
    using namespace onirique;
    static constexpr float lowHz[] { 80.0f, 100.0f, 125.0f };
    bool moved = false;
    moved |= ui::approach (l, param (ids::low), 0.3f);
    moved |= ui::approach (m, param (ids::mud), 0.3f);
    moved |= ui::approach (h, param (ids::high), 0.3f);
    moved |= ui::approach (lf, lowHz[juce::jlimit (0, 2, juce::roundToInt (param (ids::lowFreq)))], 0.3f);
    moved |= ui::approach (ai, param (ids::air), 0.3f);
    moved |= ui::approach (lc, param (ids::lowCut) > 0.5f ? 1.0f : 0.0f, 0.25f);
    const bool byp = param (ids::bypass) > 0.5f;
    moved |= ui::approach (eqAmt, param (ids::peqPower) > 0.5f && ! byp ? 1.0f : 0.0f, 0.2f);
    moved |= ui::approach (analogAmt, param (ids::eqOn) > 0.5f && ! byp ? 1.0f : 0.0f, 0.2f);

    std::array<ParametricEQ::Band, kPeq> bd;
    anyDyn = false;
    for (int b = 0; b < kPeq; ++b)
    {
        bd[(size_t) b] = band (b);
        const auto& x = bd[(size_t) b];
        const bool dyn = x.on && x.dyn && ParametricEQ::canBeDynamic (x.type);
        anyDyn |= dyn;
        moved |= ui::approach (liveGain[b], dyn ? processor.getPeqBandGainDb (b) : x.gainDb, dyn ? 0.35f : 1.0f);
    }

    // signature des réglages : rien à recalculer tant qu'elle ne change pas
    std::array<float, kPeq * 8 + 2> sig {};
    for (int b = 0; b < kPeq; ++b)
    {
        const auto& x = bd[(size_t) b];
        const float s[8] { x.on ? 1.0f : 0.0f, (float) x.type, x.freq, x.gainDb, x.q, (float) x.slope, x.dyn ? 1.0f : 0.0f, liveGain[b] };
        std::copy (s, s + 8, sig.begin() + b * 8);
    }
    sig[(size_t) kPeq * 8] = (float) selected;
    sig[(size_t) kPeq * 8 + 1] = (float) fs;
    if (! moved && sig == lastSig) return false;
    lastSig = sig;

    // coefficients une fois par bande (gain en cours et, pour les bandes dynamiques, gain réglé)
    BiquadCoeffs live[kPeq][ParametricEQ::kMaxStages], maxC[kPeq][ParametricEQ::kMaxStages];
    int ns[kPeq] {};
    for (int b = 0; b < kPeq; ++b)
    {
        const auto& x = bd[(size_t) b];
        if (! x.on) continue;
        ns[b] = ParametricEQ::numStages (x.type, x.slope);
        for (int k = 0; k < ns[b]; ++k)
        {
            live[b][k] = ParametricEQ::designStage (fs, x.type, x.freq, liveGain[b], x.q, x.slope, k);
            maxC[b][k] = ParametricEQ::designStage (fs, x.type, x.freq, x.gainDb, x.q, x.slope, k);
        }
    }
    const auto ak = VocalEQ::Curve::make (fs, l, m, h, ai, lf, lc);
    const auto& sb = bd[(size_t) selected];
    for (int i = 0; i < kPts; ++i)
    {
        const double f = std::min ((double) hzAt (i), 0.49 * fs);
        std::complex<double> hl (1.0, 0.0), hm (1.0, 0.0), hs (1.0, 0.0);
        for (int b = 0; b < kPeq; ++b)
            for (int k = 0; k < ns[b]; ++k)
            {
                const auto rl = live[b][k].response (fs, f);
                hl *= rl;
                hm *= anyDyn ? maxC[b][k].response (fs, f) : rl;
                if (b == selected) hs *= maxC[b][k].response (fs, f);
            }
        auto db = [] (std::complex<double> z) { return (float) (20.0 * std::log10 (std::max (1.0e-9, std::abs (z)))); };
        curve[(size_t) i] = eqAmt * db (hl);
        maxCurve[(size_t) i] = eqAmt * db (hm);
        selCurve[(size_t) i] = sb.on ? db (hs) : 0.0f;
        analogCurve[(size_t) i] = analogAmt * ak.db (f);
    }
    return true;
}

juce::String SpectrumAnalyzer::getTooltip()
{
    if (hoverNode >= 0)
        return "Band " + juce::String (hoverNode + 1) + ": drag = frequency / gain (Shift = fine), mouse wheel = Q (up = narrower), "
               "double-click = on/off, right-click = type, slope and DYNAMIC mode.";
    return "Parametric EQ + live analyzer: double-click an empty spot to add a band there, drag the numbered dots to shape it, "
           "right-click a dot for its menu. Grey = input, white = output, red = EQ curve, dotted = EQ ANALOG colour.";
}

//------------------------------------------------------------------------------
float SpectrumAnalyzer::unit() const noexcept
{
    return ui::editorScale (*this) * (full ? 1.3f : 1.1f);
}

juce::Rectangle<float> SpectrumAnalyzer::plotArea() const noexcept
{
    const auto area = getLocalBounds().toFloat();
    const float u = unit();
    return area.reduced (12.0f * u, 10.0f * u).withTrimmedBottom (14.0f * u).withTrimmedRight (40.0f * u).withTrimmedTop (16.0f * u);
}

float SpectrumAnalyzer::xOf (float hz) const noexcept
{
    const auto p = plotArea();
    return p.getX() + std::log (hz / 20.0f) / std::log (1000.0f) * p.getWidth();
}

float SpectrumAnalyzer::hzOf (float x) const noexcept
{
    const auto p = plotArea();
    return 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, (x - p.getX()) / p.getWidth()));
}

float SpectrumAnalyzer::yEq (float db) const noexcept
{
    const auto p = plotArea();
    return p.getCentreY() - juce::jlimit (-kEqRange, kEqRange, db) / kEqRange * p.getHeight() * 0.5f;
}

float SpectrumAnalyzer::dbOf (float y) const noexcept
{
    const auto p = plotArea();
    return juce::jlimit (-18.0f, 18.0f, (p.getCentreY() - y) / (p.getHeight() * 0.5f) * kEqRange);
}

float SpectrumAnalyzer::ySpec (float db) const noexcept
{
    const auto p = plotArea();
    return p.getBottom() - juce::jlimit (0.0f, 1.0f, (db + 100.0f) / 90.0f) * p.getHeight();
}

onirique::ParametricEQ::Band SpectrumAnalyzer::band (int b) const
{
    using namespace onirique;
    ParametricEQ::Band bd;
    bd.on = param (ids::peqOn[b]) > 0.5f;
    bd.type = juce::roundToInt (param (ids::peqType[b]));
    bd.freq = param (ids::peqFreq[b]);
    bd.gainDb = param (ids::peqGain[b]);
    bd.q = param (ids::peqQ[b]);
    bd.slope = juce::roundToInt (param (ids::peqSlope[b]));
    bd.dyn = param (ids::peqDyn[b]) > 0.5f;
    bd.thrDb = param (ids::peqThr[b]);
    return bd;
}

juce::Point<float> SpectrumAnalyzer::nodePos (int b) const
{
    const auto bd = band (b);
    return { xOf (bd.freq), yEq (onirique::ParametricEQ::hasGain (bd.type) ? bd.gainDb : 0.0f) };
}

int SpectrumAnalyzer::nodeAt (juce::Point<float> p) const
{
    const float r = 11.0f * unit();
    int best = -1;
    float bestD = r;
    for (int b = 0; b < kPeq; ++b)
        if (const float d = nodePos (b).getDistanceFrom (p); d < bestD) { bestD = d; best = b; }
    return best;
}

void SpectrumAnalyzer::setParam (const char* id, float v, bool gesture)
{
    if (auto* p = processor.apvts.getParameter (id))
    {
        if (gesture) p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (v));
        if (gesture) p->endChangeGesture();
    }
}

void SpectrumAnalyzer::select (int b)
{
    selected = juce::jlimit (0, kPeq - 1, b);
    eqDirty = true;
    if (onSelect) onSelect (selected);
}

void SpectrumAnalyzer::mouseMove (const juce::MouseEvent& e)
{
    hoverNode = nodeAt (e.position);
    setMouseCursor (hoverNode >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::CrosshairCursor);
}

void SpectrumAnalyzer::mouseExit (const juce::MouseEvent&)
{
    if (dragNode < 0) hoverNode = -1;
}

void SpectrumAnalyzer::mouseDown (const juce::MouseEvent& e)
{
    using namespace onirique;
    const int n = nodeAt (e.position);
    if (n < 0) { if (onBackgroundClick) onBackgroundClick(); return; }
    select (n);
    if (e.mods.isPopupMenu()) { showBandMenu (*this, processor.apvts, n, e.getScreenPosition()); return; }
    dragNode = n;
    processor.peqListenHold.store (true);    // casque armé : on entend cette bande tant qu'on la tient
    dragStart = e.position;
    dragF0 = band (n).freq;
    dragG0 = band (n).gainDb;
    for (auto* id : { ids::peqFreq[n], ids::peqGain[n] })
        if (auto* p = processor.apvts.getParameter (id)) p->beginChangeGesture();
}

void SpectrumAnalyzer::mouseDrag (const juce::MouseEvent& e)
{
    using namespace onirique;
    if (dragNode < 0) return;
    if (band (dragNode).on == false) setParam (ids::peqOn[dragNode], 1.0f);   // tirer une bande coupée l'allume
    // Maj : réglage fin (le point avance 4 fois moins vite que la souris)
    const float k = e.mods.isShiftDown() ? 0.25f : 1.0f;
    const auto p = plotArea();
    const float oct = (e.position.x - dragStart.x) * k / p.getWidth() * std::log2 (1000.0f);
    pendingF = juce::jlimit (20.0f, 20000.0f, dragF0 * std::pow (2.0f, oct));
    const float dg = -(e.position.y - dragStart.y) * k / (p.getHeight() * 0.5f) * kEqRange;
    pendingG = juce::jlimit (-18.0f, 18.0f, std::round ((dragG0 + dg) * 10.0f) / 10.0f);
    pending = true;
}

void SpectrumAnalyzer::flushDrag()
{
    using namespace onirique;
    if (! pending || dragNode < 0) return;
    pending = false;
    setParam (ids::peqFreq[dragNode], pendingF, false);
    if (ParametricEQ::hasGain (band (dragNode).type)) setParam (ids::peqGain[dragNode], pendingG, false);
}

void SpectrumAnalyzer::mouseUp (const juce::MouseEvent&)
{
    using namespace onirique;
    if (dragNode < 0) return;
    flushDrag();   // la dernière position, avant de clore le geste
    for (auto* id : { ids::peqFreq[dragNode], ids::peqGain[dragNode] })
        if (auto* p = processor.apvts.getParameter (id)) p->endChangeGesture();
    dragNode = -1;
    processor.peqListenHold.store (false);   // bande relâchée : on réentend toute la voix
}

void SpectrumAnalyzer::mouseDoubleClick (const juce::MouseEvent& e)
{
    using namespace onirique;
    if (const int n = nodeAt (e.position); n >= 0)
    {
        setParam (ids::peqOn[n], band (n).on ? 0.0f : 1.0f);   // double-clic sur un point : allume / coupe la bande
        select (n);
        return;
    }
    // double-clic dans le vide : la première bande libre devient une cloche à cet endroit
    for (int b = 0; b < kPeq; ++b)
        if (! band (b).on)
        {
            setParam (ids::peqType[b], (float) ParametricEQ::bell);
            setParam (ids::peqFreq[b], juce::jlimit (20.0f, 20000.0f, hzOf (e.position.x)));
            setParam (ids::peqGain[b], std::round (dbOf (e.position.y) * 10.0f) / 10.0f);
            setParam (ids::peqQ[b], 1.0f);
            setParam (ids::peqDyn[b], 0.0f);
            setParam (ids::peqOn[b], 1.0f);
            select (b);
            return;
        }
}

void SpectrumAnalyzer::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    using namespace onirique;
    const float delta = std::abs (w.deltaY) > std::abs (w.deltaX) ? w.deltaY : -w.deltaX;
    if (delta == 0.0f) return;

    // bande visée : le point sous la souris, sinon la bande active la plus proche en fréquence
    // (jusqu'à 3/4 d'octave autour de son centre), sinon la bande sélectionnée
    int n = nodeAt (e.position);
    if (n < 0)
    {
        const float hz = hzOf (e.position.x);
        float best = 0.75f;
        for (int b = 0; b < kPeq; ++b)
        {
            const auto bd = band (b);
            const float d = std::abs (std::log2 (hz / bd.freq));
            if (bd.on && d < best) { best = d; n = b; }
        }
    }
    if (n < 0) n = selected;

    // pas proportionnel au défilement (molette crantée ou trackpad) : environ x1.3 par cran, vers le haut = plus étroit
    const float q = band (n).q * std::pow (2.0f, delta * (e.mods.isShiftDown() ? 0.4f : 1.6f));
    setParam (ids::peqQ[n], juce::jlimit (0.1f, 10.0f, q));
    select (n);
    qFlashBand = n;
    qFlashFrames = 80;   // l'étiquette "Q" reste ~1.3 s après le dernier cran
}

void SpectrumAnalyzer::showBandMenu (juce::Component& target, juce::AudioProcessorValueTreeState& apvts, int b, juce::Point<int> screenPos)
{
    using namespace onirique;
    static const char* names[] { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch" };
    static const char* slopes[] { "12 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct" };
    const int cur = juce::roundToInt (apvts.getRawParameterValue (ids::peqType[b])->load());
    const int slope = juce::roundToInt (apvts.getRawParameterValue (ids::peqSlope[b])->load());
    const bool on = apvts.getRawParameterValue (ids::peqOn[b])->load() > 0.5f;
    const bool dyn = apvts.getRawParameterValue (ids::peqDyn[b])->load() > 0.5f;
    juce::PopupMenu menu;
    menu.setLookAndFeel (&target.getLookAndFeel());
    menu.addSectionHeader ("BAND " + juce::String (b + 1));
    for (int t = 0; t < ParametricEQ::numTypes; ++t) menu.addItem (t + 1, names[t], true, t == cur);
    {
        juce::PopupMenu sm;
        for (int s = 0; s < 4; ++s) sm.addItem (50 + s, slopes[s], true, s == slope);
        menu.addSubMenu ("Slope", sm, ParametricEQ::isCut (cur));
    }
    menu.addSeparator();
    menu.addItem (102, ParametricEQ::canBeDynamic (cur) ? "Dynamic (acts only above its threshold)" : "Dynamic (bell and shelves only)",
                  ParametricEQ::canBeDynamic (cur), dyn && ParametricEQ::canBeDynamic (cur));
    menu.addSeparator();
    menu.addItem (100, on ? "Switch band off" : "Switch band on");
    menu.addItem (101, "Reset band");

    auto* ap = &apvts;
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (juce::Rectangle<int> (screenPos.x, screenPos.y, 1, 1))
                            .withParentComponent (target.findParentComponentOfClass<juce::AudioProcessorEditor>()),
                        [ap, b] (int r)
                        {
                            auto set = [ap] (const char* id, float v)
                            {
                                if (auto* p = ap->getParameter (id))
                                { p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 (v)); p->endChangeGesture(); }
                            };
                            auto get = [ap] (const char* id) { return ap->getRawParameterValue (id)->load(); };
                            if (r >= 1 && r <= ParametricEQ::numTypes) { set (ids::peqType[b], (float) (r - 1)); set (ids::peqOn[b], 1.0f); }
                            else if (r >= 50 && r < 54) set (ids::peqSlope[b], (float) (r - 50));
                            else if (r == 102)
                            {
                                const bool nowDyn = get (ids::peqDyn[b]) < 0.5f;
                                set (ids::peqDyn[b], nowDyn ? 1.0f : 0.0f);
                                // passage en dynamique d'une bande sans gain : une coupe de départ utile
                                if (nowDyn && std::abs (get (ids::peqGain[b])) < 0.5f) set (ids::peqGain[b], -4.0f);
                                set (ids::peqOn[b], 1.0f);
                            }
                            else if (r == 100) set (ids::peqOn[b], get (ids::peqOn[b]) > 0.5f ? 0.0f : 1.0f);
                            else if (r == 101)
                                for (auto* id : { ids::peqType[b], ids::peqFreq[b], ids::peqGain[b], ids::peqQ[b], ids::peqSlope[b],
                                                  ids::peqDyn[b], ids::peqThr[b], ids::peqOn[b] })
                                    if (auto* p = ap->getParameter (id))
                                    { p->beginChangeGesture(); p->setValueNotifyingHost (p->getDefaultValue()); p->endChangeGesture(); }
                        });
}

//------------------------------------------------------------------------------
void PeqBandChip::tick()
{
    using namespace onirique;
    static const char* names[] { "BELL", "LOW SHELF", "HIGH SHELF", "LOW CUT", "HIGH CUT", "NOTCH" };
    const bool on = apvts.getRawParameterValue (ids::peqOn[band])->load() > 0.5f;
    const int type = juce::jlimit (0, 5, juce::roundToInt (apvts.getRawParameterValue (ids::peqType[band])->load()));
    juce::String t = names[type];
    if (ParametricEQ::isCut (type)) t << " " << 12 * (1 + juce::roundToInt (apvts.getRawParameterValue (ids::peqSlope[band])->load()));
    if (ParametricEQ::canBeDynamic (type) && apvts.getRawParameterValue (ids::peqDyn[band])->load() > 0.5f) t << " DYN";
    bool changed = t != shownType;
    shownType = t;
    changed |= ui::approach (lit, on ? 1.0f : 0.0f, 0.2f);
    if (changed) repaint();
}

void PeqBandChip::mouseDown (const juce::MouseEvent& e)
{
    using namespace onirique;
    if (onClick) onClick();
    if (e.position.x < (float) getHeight() * 1.1f)   // LED : allume / coupe
    {
        if (auto* p = apvts.getParameter (ids::peqOn[band]))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f);
            p->endChangeGesture();
        }
    }
    else
        SpectrumAnalyzer::showBandMenu (*this, apvts, band, e.getScreenPosition());
}

void PeqBandChip::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const float h = r.getHeight(), u = h / 24.0f;
    g.setColour (juce::Colours::black.withAlpha (0.36f));
    g.fillRoundedRectangle (r, h * 0.5f);
    if (highlighted)
    {
        g.setColour (ui::ember.withAlpha (0.16f));
        g.fillRoundedRectangle (r, h * 0.5f);
    }
    g.setColour (ui::line.interpolatedWith (ui::ember, juce::jmax (0.7f * lit, highlighted ? 0.9f : 0.0f)));
    g.drawRoundedRectangle (r.reduced (0.5f), h * 0.5f, std::max (1.0f, u));

    const float d = h * 0.32f;
    const auto led = juce::Rectangle<float> (d, d).withCentre ({ r.getX() + h * 0.55f, r.getCentreY() });
    g.setColour (ui::ember.withAlpha (0.4f * lit));
    g.fillEllipse (led.expanded (d * 0.7f));
    g.setColour (ui::haze.withAlpha (0.4f).interpolatedWith (ui::ember, lit));
    g.fillEllipse (led);

    auto t = r.withTrimmedLeft (h * 1.0f).withTrimmedRight (h * 0.6f);
    const juce::String num = juce::String (band + 1);
    const auto f = ui::font (ui::Face::mono, h * 0.54f, true, 0.04f);
    const float nw = ui::textWidth (f, num) + 7.0f * u;
    g.setColour (ui::mist);
    g.setFont (f);
    g.drawText (num, t.removeFromLeft (nw), juce::Justification::centredLeft, false);
    g.setColour (ui::haze.interpolatedWith (ui::bone, lit));
    g.setFont (ui::fitFont (ui::Face::body, h * 0.50f, t.getWidth(), shownType, true, 0.05f));
    g.drawText (shownType, t, juce::Justification::centredLeft, false);

    const float tx = r.getRight() - h * 0.45f, ty = r.getCentreY();
    juce::Path tri;
    tri.addTriangle (tx - 3.5f * u, ty - 2.0f * u, tx + 3.5f * u, ty - 2.0f * u, tx, ty + 2.5f * u);
    g.setColour (ui::haze);
    g.fillPath (tri);
}

// courbe lisse (Catmull-Rom) à travers les points de l'écran
// step > 1 : un point sur step (le halo flou n'a pas besoin du détail, et chaque segment coûte à chaque trait)
juce::Path SpectrumAnalyzer::smoothPath (const std::array<float, kPts>& ys, bool closeToBottom, float bottom, int step) const
{
    std::array<juce::Point<float>, kPts> pt;
    int n = 0;
    for (int i = 0; i < kPts; i += step) pt[(size_t) n++] = { xOf (hzAt (i)), ys[(size_t) i] };
    if (pt[(size_t) n - 1].x < xOf (hzAt (kPts - 1))) pt[(size_t) n++] = { xOf (hzAt (kPts - 1)), ys[(size_t) kPts - 1] };
    juce::Path p;
    p.preallocateSpace (n * 7 + 8);
    if (closeToBottom) { p.startNewSubPath (pt[0].x, bottom); p.lineTo (pt[0]); }
    else p.startNewSubPath (pt[0]);
    for (int i = 0; i < n - 1; ++i)
    {
        const auto& p0 = pt[(size_t) std::max (0, i - 1)];
        const auto& p1 = pt[(size_t) i];
        const auto& p2 = pt[(size_t) i + 1];
        const auto& p3 = pt[(size_t) std::min (n - 1, i + 2)];
        p.cubicTo (p1 + (p2 - p0) / 6.0f, p2 - (p3 - p1) / 6.0f, p2);
    }
    if (closeToBottom) { p.lineTo (pt[(size_t) n - 1].x, bottom); p.closeSubPath(); }
    return p;
}

// fond + grille (calque refait seulement au redimensionnement)
void SpectrumAnalyzer::paintGrid (juce::Graphics& g)
{
    using namespace onirique;
    const auto area = getLocalBounds().toFloat();
    const float u = unit(), es = ui::editorScale (*this);
    if (full)
    {
        // opaque : le dégradé de la vue plein écran (EqFullView::paint), recalé sur la position de l'analyseur
        const float parentH = getParentComponent() != nullptr ? (float) getParentHeight() : area.getHeight();
        g.setGradientFill (juce::ColourGradient (ui::deep2, 0.0f, (float) -getY(), ui::night, 0.0f, parentH - (float) getY(), false));
        g.fillAll();
    }
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.55f), 0.0f, area.getY(),
                                             ui::night.withAlpha (0.88f), 0.0f, area.getBottom(), false));
    g.fillRoundedRectangle (area, 12.0f * u);
    g.setColour (ui::line);
    g.drawRoundedRectangle (area.reduced (0.5f), 12.0f * u, std::max (1.0f, u));

    const auto plot = plotArea();

    // ---- grille : décades nettes, divisions fines, dB tous les 6 (tous les 3 en plein écran)
    const float labelH = std::max (10.0f * u, 10.0f * es);
    g.setFont (ui::font (ui::Face::mono, labelH));
    for (float dec : { 10.0f, 100.0f, 1000.0f, 10000.0f })
        for (int k = 1; k <= 9; ++k)
        {
            const float hz = dec * (float) k;
            if (hz < 20.0f || hz > 20000.0f) continue;
            const float x = xOf (hz);
            const bool major = k == 1;
            g.setColour (ui::line.withMultipliedAlpha (major ? 0.9f : 0.32f));
            g.fillRect (juce::Rectangle<float> (x, plot.getY() - (major ? 6.0f * u : 0.0f), 1.0f, plot.getHeight() + (major ? 6.0f * u : 0.0f)));
            const bool label = major || k == 2 || k == 5 || (full && (k == 3 || k == 7));
            if (label)
            {
                const juce::String txt = hz >= 1000.0f ? juce::String ((int) (hz / 1000.0f)) + "k" : juce::String ((int) hz);
                g.setColour (ui::haze.withAlpha (major ? 0.9f : 0.65f));
                g.drawText (txt, juce::Rectangle<float> (x + 3.0f * u, plot.getBottom() + 2.0f * u, 40.0f * u, 12.0f * u),
                            juce::Justification::centredLeft, false);
            }
        }
    for (int dbi = -18; dbi <= 18; dbi += full ? 3 : 6)
    {
        const float db = (float) dbi, y = yEq (db);
        g.setColour (dbi == 0 ? ui::line.withMultipliedAlpha (1.8f) : ui::line.withMultipliedAlpha (dbi % 6 == 0 ? 0.5f : 0.25f));
        g.fillRect (juce::Rectangle<float> (plot.getX(), y, plot.getWidth(), 1.0f));
        if (dbi % 6 == 0)
        {
            g.setColour (ui::haze.withAlpha (0.6f));
            g.drawText ((db > 0.0f ? "+" : "") + juce::String (dbi) + " dB",
                        juce::Rectangle<float> (plot.getRight() + 4.0f * u, y - 6.0f * u, 40.0f * u, 12.0f * u), juce::Justification::centredLeft, false);
        }
    }

}

// courbes d'EQ, points, étiquettes, légende (calque refait seulement quand un réglage ou un survol change)
void SpectrumAnalyzer::paintEq (juce::Graphics& g)
{
    using namespace onirique;
    const auto area = getLocalBounds().toFloat();
    const float u = unit(), es = ui::editorScale (*this);
    const auto plot = plotArea();
    const float labelH = std::max (10.0f * u, 10.0f * es);
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (plot.toNearestInt());

        // ---- EQ ANALOG : couleur ajoutée plus loin dans la chaîne, en pointillés discrets
        float analogMax = 0.0f;
        for (float v : analogCurve) analogMax = std::max (analogMax, std::abs (v));
        if (analogMax > 0.05f)
        {
            std::array<float, kPts> ya;
            for (int i = 0; i < kPts; ++i) ya[(size_t) i] = yEq (analogCurve[(size_t) i]);
            juce::Path dashed;
            const float dash[] { 4.0f * u, 4.0f * u };
            juce::PathStrokeType (std::max (1.0f, 1.3f * u)).createDashedStroke (dashed, smoothPath (ya, false, 0.0f), dash, 2);
            g.setColour (ui::mist.withAlpha (0.5f * analogAmt));
            g.fillPath (dashed);
        }

        // ---- bandes dynamiques : courbe au gain réglé (pointillés braise) ; la courbe pleine suit le gain en direct
        std::array<float, kPts> yEqC;
        for (int i = 0; i < kPts; ++i) yEqC[(size_t) i] = yEq (curve[(size_t) i]);
        if (anyDyn)
        {
            std::array<float, kPts> ym;
            for (int i = 0; i < kPts; ++i) ym[(size_t) i] = yEq (maxCurve[(size_t) i]);
            juce::Path dashed;
            const float dash[] { 3.0f * u, 3.0f * u };
            juce::PathStrokeType (std::max (1.0f, 1.2f * u)).createDashedStroke (dashed, smoothPath (ym, false, 0.0f), dash, 2);
            g.setColour (ui::ember.withAlpha (0.55f * eqAmt));
            g.fillPath (dashed);
        }

        // ---- courbe de l'EQ paramétrique : remplissage jusqu'à 0 dB + trait braise lumineux
        const auto eq = smoothPath (yEqC, false, 0.0f);
        juce::Path fill (eq);
        fill.lineTo (plot.getRight(), yEq (0.0f));
        fill.lineTo (plot.getX(), yEq (0.0f));
        fill.closeSubPath();
        g.setColour (ui::ember.withAlpha (0.12f * (0.3f + 0.7f * eqAmt)));
        g.fillPath (fill);
        ui::strokeGlow (g, smoothPath (yEqC, false, 0.0f, 3), ui::ember, 2.5f * u, 0.3f + 0.7f * eqAmt);
        g.setColour (ui::ember.withAlpha (0.45f + 0.55f * eqAmt));
        g.strokePath (eq, juce::PathStrokeType (std::max (1.5f, 2.4f * u), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // ---- zone de la bande sélectionnée
        if (band (selected).on)
        {
            std::array<float, kPts> ys;
            for (int i = 0; i < kPts; ++i) ys[(size_t) i] = yEq (selCurve[(size_t) i]);
            juce::Path p = smoothPath (ys, false, 0.0f);
            p.lineTo (plot.getRight(), yEq (0.0f));
            p.lineTo (plot.getX(), yEq (0.0f));
            p.closeSubPath();
            g.setColour (ui::bone.withAlpha (0.10f * eqAmt));
            g.fillPath (p);
        }
    }

    // ---- points numérotés (glissables) ; bande dynamique : rail jusqu'au gain réglé + point qui suit le gain en direct
    for (int b = 0; b < kPeq; ++b)
    {
        const auto bd = band (b);
        const auto c = nodePos (b);
        const float gl = nodeGlow[b];
        const float r = (7.0f + 2.0f * gl) * u * (full ? 1.1f : 1.0f);
        const bool dyn = bd.on && bd.dyn && ParametricEQ::canBeDynamic (bd.type);
        if (dyn)
        {
            const float y0 = yEq (0.0f);
            g.setColour (ui::ember.withAlpha (0.35f));
            g.fillRoundedRectangle (juce::Rectangle<float> (c.x - 1.5f * u, std::min (y0, c.y), 3.0f * u, std::abs (c.y - y0)), 1.5f * u);
            const float yl = yEq (liveGain[b]);
            const float dd = 5.0f * u;
            g.setColour (ui::bone);
            g.fillEllipse (juce::Rectangle<float> (dd, dd).withCentre ({ c.x, yl }));
        }
        const auto dot = juce::Rectangle<float> (2.0f * r, 2.0f * r).withCentre (c);
        juce::Path ring;
        ring.addEllipse (dot);
        if (bd.on)
        {
            ui::strokeGlow (g, ring, ui::ember, 1.4f * u, (0.4f + 0.6f * gl) * eqAmt);
            g.setColour (b == selected ? ui::ember : ui::night);
            g.fillPath (ring);
            g.setColour (b == selected ? ui::bone : ui::ember);
            g.strokePath (ring, juce::PathStrokeType (std::max (1.2f, 1.8f * u)));
            if (dyn)
            {
                const float dash[] { 2.0f * u, 2.0f * u };
                juce::Path halo;
                juce::PathStrokeType (std::max (1.0f, 1.0f * u)).createDashedStroke (halo, [&] { juce::Path e; e.addEllipse (dot.expanded (3.5f * u)); return e; }(), dash, 2);
                g.setColour (ui::ember.withAlpha (0.8f));
                g.fillPath (halo);
            }
            g.setColour (b == selected ? ui::night : ui::bone);
        }
        else
        {
            // bande coupée : point creux discret, prêt à être tiré ou double-cliqué
            g.setColour (ui::night.withAlpha (0.7f));
            g.fillPath (ring);
            g.setColour (ui::haze.withAlpha (0.35f + 0.4f * gl));
            g.strokePath (ring, juce::PathStrokeType (std::max (1.0f, 1.2f * u)));
            g.setColour (ui::haze.withAlpha (0.5f + 0.4f * gl));
        }
        g.setFont (ui::font (ui::Face::mono, r * 1.15f, true));
        g.drawText (juce::String (b + 1), dot, juce::Justification::centred, false);
    }

    // ---- étiquette de la bande (Q à la molette ; en plein écran : fréquence / gain / Q de la bande sélectionnée)
    auto label = [&] (int b, const juce::String& txt, float a)
    {
        const auto c = nodePos (b);
        const auto f = ui::font (ui::Face::mono, std::max (8.0f, 11.0f * u), true);
        const float tw = ui::textWidth (f, txt) + 14.0f * u;
        auto box = juce::Rectangle<float> (tw, 18.0f * u).withCentre ({ c.x, c.y - 22.0f * u });
        if (box.getY() < area.getY() + 2.0f) box.setCentre (c.x, c.y + 22.0f * u);
        box = box.withX (juce::jlimit (area.getX() + 2.0f, area.getRight() - tw - 2.0f, box.getX()));
        g.setColour (ui::night.withAlpha (0.92f * a));
        g.fillRoundedRectangle (box, box.getHeight() * 0.5f);
        g.setColour (ui::ember.withAlpha (a));
        g.drawRoundedRectangle (box, box.getHeight() * 0.5f, std::max (1.0f, u));
        g.setColour (ui::bone.withAlpha (a));
        g.setFont (f);
        g.drawText (txt, box, juce::Justification::centred, false);
    };
    // la bande sélectionnée a sa bulle : l'étiquette ne sert qu'aux autres bandes réglées à la molette
    if (qFlashBand >= 0 && ! (qFlashBand == selected && band (selected).on))
        label (qFlashBand, "Q " + juce::String (band (qFlashBand).q, 2), juce::jlimit (0.0f, 1.0f, (float) qFlashFrames / 20.0f));

    // ---- légende
    {
        float x = plot.getX();
        const float y = area.getY() + 7.0f * u;
        g.setFont (ui::font (ui::Face::mono, labelH));
        for (auto [c, txt] : { std::pair { ui::ember, "EQ" }, { ui::mist.withAlpha (0.6f), "ANALOG" }, { ui::bone, "OUTPUT" }, { ui::haze, "INPUT" } })
        {
            g.setColour (c);
            g.fillRoundedRectangle (juce::Rectangle<float> (x, y + 4.5f * u, 14.0f * u, 3.0f * u), 1.5f * u);
            g.setColour (ui::haze);
            const float tw = ui::textWidth (g.getCurrentFont(), txt) + 4.0f * u;
            g.drawText (txt, juce::Rectangle<float> (x + 20.0f * u, y, tw, 12.0f * u), juce::Justification::centredLeft, false);
            x += 20.0f * u + tw + 16.0f * u;
        }
        if (anyDyn)
        {
            g.setColour (ui::ember.withAlpha (0.8f));
            g.drawText ("DYNAMIC: dotted = full cut, solid = live", juce::Rectangle<float> (x, y, 300.0f * u, 12.0f * u),
                        juce::Justification::centredLeft, false);
        }
    }
}

// chaque image : grille (calque) + spectres (seule partie redessinée à chaque fois) + courbes d'EQ (calque)
void SpectrumAnalyzer::paint (juce::Graphics& g)
{
    const float ps = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const int w = juce::jmax (1, juce::roundToInt ((float) getWidth() * ps)), hh = juce::jmax (1, juce::roundToInt ((float) getHeight() * ps));
    auto render = [&] (juce::Image& img, void (SpectrumAnalyzer::*fn) (juce::Graphics&))
    {
        // même taille : le calque est vidé et réutilisé (en réallouer un de plusieurs Mo à chaque image faisait ramer le glisser)
        if (img.isValid() && img.getWidth() == w && img.getHeight() == hh) img.clear (img.getBounds());
        else img = juce::Image (full && fn == &SpectrumAnalyzer::paintGrid ? juce::Image::RGB : juce::Image::ARGB, w, hh, true);   // plein écran : la grille est opaque, plus rapide à recopier
        juce::Graphics lg (img);
        lg.addTransform (juce::AffineTransform::scale (ps));
        (this->*fn) (lg);
    };
    if (! gridLayer.isValid() || gridLayer.getWidth() != w || gridLayer.getHeight() != hh)
    {
        render (gridLayer, &SpectrumAnalyzer::paintGrid);
        eqDirty = true;
    }
   #if JUCE_WINDOWS
    // Windows : JUCE dessine par la carte graphique (Direct2D) ; dessiner dans une image puis la relire force une
    // synchronisation qui coûtait plus de 15 ms par image en grand. Les courbes sont donc dessinées directement.
    constexpr bool eqInLayer = false;
   #else
    constexpr bool eqInLayer = true;    // Mac : rendu par le processeur, le calque évite de tout redessiner à chaque image
   #endif
    if (eqInLayer && (eqDirty || ! eqLayer.isValid()))
    {
        render (eqLayer, &SpectrumAnalyzer::paintEq);
        eqDirty = false;
    }
    const auto toLogical = juce::AffineTransform::scale (1.0f / ps);
    g.setOpacity (1.0f);
    g.drawImageTransformed (gridLayer, toLogical);

    {
        // ---- spectres : entrée (voile discret), sortie (dégradé os + trait net)
        const auto plot = plotArea();
        const float u = unit();
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (plot.toNearestInt());
        std::array<float, kPts> yIn, yOut;
        for (int i = 0; i < kPts; ++i) { yIn[(size_t) i] = ySpec (specIn[(size_t) i]); yOut[(size_t) i] = ySpec (specOut[(size_t) i]); }
        g.setGradientFill (juce::ColourGradient (ui::haze.withAlpha (0.16f), 0.0f, plot.getY(), ui::haze.withAlpha (0.0f), 0.0f, plot.getBottom(), false));
        g.fillPath (smoothPath (yIn, true, plot.getBottom()));
        g.setColour (ui::haze.withAlpha (0.42f));
        g.strokePath (smoothPath (yIn, false, 0.0f), juce::PathStrokeType (std::max (1.0f, 1.0f * u)));

        juce::ColourGradient og (ui::bone.withAlpha (0.34f), 0.0f, plot.getY(), ui::bone.withAlpha (0.02f), 0.0f, plot.getBottom(), false);
        og.addColour (0.55, ui::bone.withAlpha (0.12f));
        g.setGradientFill (og);
        g.fillPath (smoothPath (yOut, true, plot.getBottom()));
        g.setColour (ui::bone.withAlpha (0.85f));
        g.strokePath (smoothPath (yOut, false, 0.0f), juce::PathStrokeType (std::max (1.0f, 1.3f * u), juce::PathStrokeType::curved));
    }

    g.setOpacity (1.0f);
    if (eqInLayer) g.drawImageTransformed (eqLayer, toLogical);
    else { paintEq (g); eqDirty = false; }
}

//==============================================================================
GateDisplay::GateDisplay (OniriqueVocalBoxAudioProcessor& p) : processor (p)
{
    thrP = p.apvts.getParameter (ids::gateThr);
    level.fill (-100.0f);
    gainHist.fill (0.0f);
    thrShown = param (ids::gateThr);
    hystShown = param (ids::gateHyst);
}

juce::Rectangle<float> GateDisplay::plotArea() const noexcept
{
    const float u = ui::editorScale (*this);
    return getLocalBounds().toFloat().reduced (10.0f * u, 8.0f * u).withTrimmedRight (30.0f * u).withTrimmedTop (26.0f * u);
}

juce::Rectangle<float> GateDisplay::learnArea() const noexcept
{
    const float u = ui::editorScale (*this);
    const auto a = getLocalBounds().toFloat();
    return { a.getRight() - 104.0f * u, a.getY() + 6.0f * u, 96.0f * u, 22.0f * u };
}

float GateDisplay::yOf (float db) const noexcept
{
    const auto p = plotArea();
    return p.getY() + juce::jlimit (0.0f, 1.0f, db / kMinDb) * p.getHeight();
}

float GateDisplay::dbOf (float y) const noexcept
{
    const auto p = plotArea();
    return kMinDb * juce::jlimit (0.0f, 1.0f, (y - p.getY()) / p.getHeight());
}

void GateDisplay::tick()
{
    const auto& m = processor.getGateMeter();
    const bool on = param (ids::gateOn) > 0.5f && param (ids::bypass) < 0.5f;
    const float lv = m.levelDb.load (std::memory_order_relaxed);
    level[(size_t) head] = lv;
    const bool bypassed = param (ids::bypass) > 0.5f;
    gainHist[(size_t) head] = bypassed ? 0.0f : m.gainDb.load (std::memory_order_relaxed);
    head = (head + 1) % kHist;

    ui::approach (thrShown, param (ids::gateThr), 0.3f);
    ui::approach (hystShown, param (ids::gateHyst), 0.3f);
    ui::approach (floorShown, m.floorDb.load (std::memory_order_relaxed), 0.05f);
    ui::approach (openLed, on && m.open.load (std::memory_order_relaxed) ? 1.0f : 0.0f, 0.25f);
    ui::approach (active, on ? 1.0f : 0.0f, 0.15f);
    ui::approach (thrGlow, (dragging || hoverThr) ? 1.0f : 0.0f, 0.2f);
    ui::approach (learnGlow, (hoverLearn || learnFrames > 0) ? 1.0f : 0.0f, 0.2f);

    // LEARN : on écoute le bruit de la pièce 1.5 s, le seuil se place 6 dB au-dessus de sa crête
    if (learnFrames > 0)
    {
        learnMin = std::max (learnMin, lv);
        if (--learnFrames == 0 && thrP != nullptr)
        {
            const float t = juce::jlimit (-80.0f, -20.0f, learnMin + 6.0f);
            thrP->beginChangeGesture();
            thrP->setValueNotifyingHost (thrP->convertTo0to1 (t));
            thrP->endChangeGesture();
            learnMessage = learnMin + 6.0f > -20.0f ? juce::String ("TOO LOUD - STAY SILENT AND RETRY")
                                                    : "THRESHOLD " + juce::String (t, 1) + " dB";
            messageFrames = 150;
        }
    }
    if (messageFrames > 0) --messageFrames;
    repaint();
}

void GateDisplay::mouseMove (const juce::MouseEvent& e)
{
    hoverLearn = learnArea().contains (e.position);
    hoverThr = ! hoverLearn && std::abs (e.position.y - yOf (thrShown)) < 8.0f * ui::editorScale (*this);
    setMouseCursor (hoverLearn ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::UpDownResizeCursor);
}

void GateDisplay::mouseExit (const juce::MouseEvent&)
{
    hoverLearn = false;
    if (! dragging) hoverThr = false;
}

void GateDisplay::mouseDown (const juce::MouseEvent& e)
{
    if (learnArea().contains (e.position))
    {
        learnFrames = 90;
        learnMin = -120.0f;
        messageFrames = 0;
        return;
    }
    if (thrP == nullptr) return;
    dragging = true;
    thrP->beginChangeGesture();
    thrP->setValueNotifyingHost (thrP->convertTo0to1 (juce::jlimit (-80.0f, 0.0f, dbOf (e.position.y))));
}

void GateDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging && thrP != nullptr)
        thrP->setValueNotifyingHost (thrP->convertTo0to1 (juce::jlimit (-80.0f, 0.0f, dbOf (e.position.y))));
}

void GateDisplay::mouseUp (const juce::MouseEvent&)
{
    if (dragging && thrP != nullptr) thrP->endChangeGesture();
    dragging = false;
}

void GateDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (thrP == nullptr || learnArea().contains (e.position)) return;
    thrP->beginChangeGesture();
    thrP->setValueNotifyingHost (thrP->getDefaultValue());
    thrP->endChangeGesture();
}

juce::String GateDisplay::getTooltip()
{
    if (hoverLearn)
        return "LEARN: stay silent for 1.5 s - the gate measures your room noise and sets the threshold just above it.";
    return "Noise gate: the voice level scrolls from right to left. Drag up/down to set the threshold (red line); the dashed "
           "line below is where it closes again (hysteresis). The white curve on top is the gain applied (drops by RANGE when closed). Double-click = reset.";
}

void GateDisplay::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    const float u = ui::editorScale (*this);
    const float es = ui::editorScale (*this);
    const auto plot = plotArea();

    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.55f), 0.0f, area.getY(),
                                             ui::night.withAlpha (0.85f), 0.0f, area.getBottom(), false));
    g.fillRoundedRectangle (area, 12.0f * u);
    g.setColour (ui::line);
    g.drawRoundedRectangle (area.reduced (0.5f), 12.0f * u, std::max (1.0f, u));

    // ---- graduations
    g.setFont (ui::font (ui::Face::mono, std::max (9.5f * u, 9.5f * es)));
    for (int db = -10; db >= -70; db -= 10)
    {
        const float y = yOf ((float) db);
        g.setColour (juce::Colours::white.withAlpha (0.04f));
        g.fillRect (juce::Rectangle<float> (plot.getX(), y, plot.getWidth(), 1.0f));
        if (db % 20 == 0)
        {
            g.setColour (ui::haze.withAlpha (0.6f));
            g.drawText (juce::String (db), juce::Rectangle<float> (plot.getRight() + 4.0f * u, y - 6.0f * u, 26.0f * u, 12.0f * u),
                        juce::Justification::centredLeft, false);
        }
    }

    const float a = 0.45f + 0.55f * active;
    const float colW = plot.getWidth() / (float) (kHist - 1);
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (plot.toNearestInt());

        // ---- niveau : colonnes os quand le gate est ouvert, estompées quand il ferme
        for (int i = 0; i < kHist; ++i)
        {
            const int idx = (head + i) % kHist;
            const float x = plot.getX() + (float) i * colW;
            const float y = yOf (level[(size_t) idx]);
            if (y >= plot.getBottom() - 0.5f) continue;
            const float openness = juce::jlimit (0.0f, 1.0f, 1.0f + gainHist[(size_t) idx] / 12.0f);
            g.setColour (ui::haze.withAlpha (0.18f * a).interpolatedWith (ui::bone.withAlpha (0.55f * a), openness));
            g.fillRect (juce::Rectangle<float> (x, y, colW + 0.6f, plot.getBottom() - y));
        }

        // ---- bruit de fond mesuré
        if (floorShown > kMinDb)
        {
            const float fy = yOf (floorShown);
            const float dash[] { 2.0f * u, 4.0f * u };
            g.setColour (ui::haze.withAlpha (0.5f));
            g.drawDashedLine ({ plot.getX(), fy, plot.getRight(), fy }, dash, 2, std::max (1.0f, u));
            g.setFont (ui::font (ui::Face::mono, std::max (9.0f * u, 9.0f * es), true));
            g.drawText ("ROOM " + juce::String (floorShown, 0), juce::Rectangle<float> (plot.getX() + 6.0f * u, fy + 2.0f * u, 90.0f * u, 12.0f * u),
                        juce::Justification::centredLeft, false);
        }

        // ---- courbe de gain : en haut quand le gate est ouvert, descend de RANGE quand il ferme
        juce::Path gp;
        for (int i = 0; i < kHist; ++i)
        {
            const int idx = (head + i) % kHist;
            const float x = plot.getX() + (float) i * colW;
            const float y = plot.getY() + 2.0f * u + juce::jlimit (0.0f, 1.0f, -gainHist[(size_t) idx] / 80.0f) * plot.getHeight();
            if (i == 0) gp.startNewSubPath (x, y); else gp.lineTo (x, y);
        }
        ui::strokeGlow (g, gp, ui::bone, 1.0f * u, 0.25f * active);
        g.setColour (ui::bone.withAlpha (0.25f + 0.5f * active));
        g.strokePath (gp, juce::PathStrokeType (std::max (1.2f, 1.8f * u), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // ---- hystérésis (fermeture) puis seuil (ouverture)
        const float hy = yOf (thrShown - hystShown);
        const float dash[] { 4.0f * u, 4.0f * u };
        g.setColour (ui::ember.withAlpha (0.35f * a));
        g.drawDashedLine ({ plot.getX(), hy, plot.getRight(), hy }, dash, 2, std::max (1.0f, u));
    }

    const float ty = yOf (thrShown);
    juce::Path tl;
    tl.startNewSubPath (plot.getX(), ty);
    tl.lineTo (plot.getRight(), ty);
    ui::strokeGlow (g, tl, ui::ember, 1.4f * u, (0.4f + 0.6f * thrGlow) * a);
    g.setColour (ui::ember.withAlpha ((0.75f + 0.25f * thrGlow) * a));
    g.strokePath (tl, juce::PathStrokeType (std::max (1.2f, (1.6f + 0.6f * thrGlow) * u)));
    {
        const juce::String txt = "THR " + juce::String (thrShown, 1);
        const auto f = ui::font (ui::Face::mono, std::max (10.0f * u, 10.0f * es), true);
        const float tw = ui::textWidth (f, txt) + 14.0f * u;
        auto box = juce::Rectangle<float> (tw, 17.0f * u).withRightX (plot.getRight() - 4.0f * u).withCentre ({ plot.getRight() - 4.0f * u - tw * 0.5f, ty - 11.0f * u });
        if (box.getY() < plot.getY()) box.setY (ty + 3.0f * u);
        g.setColour (ui::night.withAlpha (0.92f));
        g.fillRoundedRectangle (box, box.getHeight() * 0.5f);
        g.setColour (ui::ember.withAlpha (0.6f + 0.4f * thrGlow));
        g.drawRoundedRectangle (box, box.getHeight() * 0.5f, std::max (1.0f, u));
        g.setColour (ui::bone);
        g.setFont (f);
        g.drawText (txt, box, juce::Justification::centred, false);
    }

    // ---- état : OPEN / CLOSED
    {
        const float d = 8.0f * u;
        const auto led = juce::Rectangle<float> (d, d).withCentre ({ area.getX() + 18.0f * u, area.getY() + 18.0f * u });
        const auto c = active < 0.5f ? ui::haze : juce::Colour (0xff3BD98A).interpolatedWith (ui::ember, 1.0f - openLed);
        g.setColour (c.withAlpha (0.4f * active));
        g.fillEllipse (led.expanded (d * 0.8f));
        g.setColour (c.withAlpha (0.4f + 0.6f * active));
        g.fillEllipse (led);
        const juce::String st = active < 0.5f ? "BYPASSED" : (openLed > 0.5f ? "OPEN" : "CLOSED");
        g.setColour (ui::mist.withAlpha (0.85f));
        g.setFont (ui::font (ui::Face::mono, std::max (11.0f * u, 10.5f * es), true, 0.1f));
        g.drawText (st, juce::Rectangle<float> (led.getRight() + 8.0f * u, led.getCentreY() - 8.0f * u, 140.0f * u, 16.0f * u),
                    juce::Justification::centredLeft, false);
    }

    // ---- LEARN
    {
        const auto r = learnArea();
        juce::Path p;
        p.addRoundedRectangle (r, r.getHeight() * 0.5f);
        const float pulse = learnFrames > 0 ? 0.5f + 0.5f * std::sin ((float) learnFrames * 0.35f) : 0.0f;
        ui::strokeGlow (g, p, ui::ember, 1.0f * u, 0.6f * learnGlow + 0.4f * pulse);
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillPath (p);
        g.setColour (ui::ember.withAlpha (0.12f * learnGlow + 0.25f * pulse));
        g.fillPath (p);
        g.setColour (ui::line.interpolatedWith (ui::ember, learnGlow));
        g.strokePath (p, juce::PathStrokeType (std::max (1.0f, u)));
        const juce::String txt = learnFrames > 0 ? "LISTENING..." : "LEARN";
        g.setColour (ui::haze.interpolatedWith (ui::bone, learnGlow));
        g.setFont (ui::fitFont (ui::Face::body, r.getHeight() * 0.48f, r.getWidth() - 12.0f * u, txt, true, 0.12f));
        g.drawText (txt, r, juce::Justification::centred, false);
    }

    if (messageFrames > 0)
    {
        const float al = juce::jlimit (0.0f, 1.0f, (float) messageFrames / 20.0f);
        const auto f = ui::font (ui::Face::mono, std::max (10.5f * u, 10.5f * es), true);
        const float tw = ui::textWidth (f, learnMessage) + 20.0f * u;
        const auto box = juce::Rectangle<float> (tw, 20.0f * u).withCentre (plot.getCentre());
        g.setColour (ui::night.withAlpha (0.92f * al));
        g.fillRoundedRectangle (box, box.getHeight() * 0.5f);
        g.setColour (ui::ember.withAlpha (al));
        g.drawRoundedRectangle (box, box.getHeight() * 0.5f, std::max (1.0f, u));
        g.setColour (ui::bone.withAlpha (al));
        g.setFont (f);
        g.drawText (learnMessage, box, juce::Justification::centred, false);
    }
}

//==============================================================================
namespace
{
juce::String hzText (float f)
{
    if (f >= 1000.0f) return juce::String (f / 1000.0f, f >= 10000.0f ? 2 : 3) + " kHz";
    return juce::String (f, f < 100.0f ? 2 : 1) + " Hz";
}

// pictogrammes fins (bulle et panneau), dans un carré r
void iconPower (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, float w)
{
    juce::Path p;
    const auto ctr = r.getCentre();
    const float rad = r.getWidth() * 0.36f;
    p.addCentredArc (ctr.x, ctr.y, rad, rad, 0.0f, 0.75f, 2.0f * juce::MathConstants<float>::pi - 0.75f, true);
    p.startNewSubPath (ctr.x, ctr.y - rad * 1.25f);
    p.lineTo (ctr.x, ctr.y - rad * 0.15f);
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void iconHeadphones (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, float w)
{
    juce::Path p;
    const auto ctr = r.getCentre();
    const float rad = r.getWidth() * 0.38f;
    p.addCentredArc (ctr.x, ctr.y + rad * 0.15f, rad, rad, 0.0f, -1.75f, 1.75f, true);
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const float cw = rad * 0.42f, ch = rad * 0.75f;
    g.fillRoundedRectangle (ctr.x - rad - cw * 0.35f, ctr.y + rad * 0.05f, cw, ch, cw * 0.3f);
    g.fillRoundedRectangle (ctr.x + rad - cw * 0.65f, ctr.y + rad * 0.05f, cw, ch, cw * 0.3f);
}

void iconCross (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, float w)
{
    const auto q = r.reduced (r.getWidth() * 0.3f);
    juce::Path p;
    p.startNewSubPath (q.getTopLeft()); p.lineTo (q.getBottomRight());
    p.startNewSubPath (q.getTopRight()); p.lineTo (q.getBottomLeft());
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// petite courbe du type de bande (coupe-bas, cloche...)
void iconType (juce::Graphics& g, juce::Rectangle<float> r, int type, juce::Colour c, float w)
{
    const auto q = r.reduced (r.getWidth() * 0.12f, r.getHeight() * 0.25f);
    const float x0 = q.getX(), x1 = q.getRight(), yt = q.getY(), yb = q.getBottom(), ym = q.getCentreY(), xm = q.getCentreX();
    juce::Path p;
    switch (type)
    {
        case ParametricEQ::lowCut:    p.startNewSubPath (x0, yb); p.quadraticTo (xm - q.getWidth() * 0.1f, yt, x1, yt); break;
        case ParametricEQ::highCut:   p.startNewSubPath (x0, yt); p.quadraticTo (xm + q.getWidth() * 0.1f, yt, x1, yb); break;
        case ParametricEQ::lowShelf:  p.startNewSubPath (x0, yt); p.cubicTo (xm, yt, xm, yb, x1, yb); break;
        case ParametricEQ::highShelf: p.startNewSubPath (x0, yb); p.cubicTo (xm, yb, xm, yt, x1, yt); break;
        case ParametricEQ::notch:     p.startNewSubPath (x0, yt); p.lineTo (xm - 2.0f * w, yt); p.lineTo (xm, yb); p.lineTo (xm + 2.0f * w, yt); p.lineTo (x1, yt); break;
        default:                      p.startNewSubPath (x0, yb); p.cubicTo (xm - q.getWidth() * 0.15f, yb, xm - q.getWidth() * 0.12f, yt, xm, yt);
                                      p.cubicTo (xm + q.getWidth() * 0.12f, yt, xm + q.getWidth() * 0.15f, yb, x1, yb); break;
    }
    juce::ignoreUnused (ym);
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void triangle (juce::Graphics& g, juce::Rectangle<float> r, bool up, juce::Colour c)
{
    juce::Path p;
    if (up) p.addTriangle (r.getX(), r.getBottom(), r.getRight(), r.getBottom(), r.getCentreX(), r.getY());
    else    p.addTriangle (r.getX(), r.getY(), r.getRight(), r.getY(), r.getCentreX(), r.getBottom());
    g.setColour (c);
    g.fillPath (p);
}

const char* const kTypeNames[] { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch" };
const char* const kSlopeNames[] { "12 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct" };
} // namespace

//==============================================================================
BandBubble::BandBubble (juce::AudioProcessorValueTreeState& s, std::atomic<bool>& h) : apvts (s), hold (h)
{
    setBufferedToImage (true);   // redessinée seulement quand une valeur change (l'analyseur dessous bouge à chaque image)
    setRepaintsOnMouseActivity (false);
}

void BandBubble::set (const char* id, float v)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (v));
        p->endChangeGesture();
    }
}

void BandBubble::resized()
{
    const auto r = getLocalBounds().toFloat();
    const float u = r.getHeight() / 70.0f, tip = 7.0f * u;
    box = pointsDown ? r.withTrimmedBottom (tip) : r.withTrimmedTop (tip);
    box = box.reduced (1.0f);
    const float rowH = (box.getHeight() - 8.0f * u) / 3.0f, ic = rowH * 0.95f;
    const auto row = [&] (int i) { return juce::Rectangle<float> (box.getX(), box.getY() + 4.0f * u + rowH * (float) i, box.getWidth(), rowH); };
    zPower  = row (0).withWidth (ic + 8.0f * u).withTrimmedLeft (4.0f * u);
    zClose  = row (0).withLeft (box.getRight() - ic - 8.0f * u).withTrimmedRight (4.0f * u);
    zListen = zClose.translated (-zClose.getWidth() - 2.0f * u, 0.0f);
    const auto r3 = row (2);
    zUp   = r3.withLeft (box.getRight() - ic - 8.0f * u).withTrimmedRight (4.0f * u).withTrimmedBottom (rowH * 0.5f);
    zDown = zUp.translated (0.0f, rowH * 0.5f);
    zType = r3.withRight (zUp.getX());
}

BandBubble::Zone BandBubble::zoneAt (juce::Point<float> p) const noexcept
{
    const bool cut = ParametricEQ::isCut (juce::roundToInt (get (ids::peqType[band])));
    if (zPower.contains (p)) return power;
    if (zListen.contains (p)) return listen;
    if (zClose.contains (p)) return close;
    if (cut && zUp.contains (p)) return slopeUp;
    if (cut && zDown.contains (p)) return slopeDown;
    if (zType.contains (p)) return typeMenu;
    return none;
}

void BandBubble::tick()
{
    const int type = juce::roundToInt (get (ids::peqType[band]));
    juce::String t;
    t << get (ids::peqOn[band]) << "|" << type << "|" << get (ids::peqFreq[band]) << "|" << get (ids::peqGain[band]) << "|"
      << get (ids::peqQ[band]) << "|" << get (ids::peqSlope[band]) << "|" << (listening ? 1 : 0);
    if (t != shownText) { shownText = t; repaint(); }
}

void BandBubble::paint (juce::Graphics& g)
{
    const float u = (float) getHeight() / 70.0f;
    const int type = juce::jlimit (0, 5, juce::roundToInt (get (ids::peqType[band])));
    const bool on = get (ids::peqOn[band]) > 0.5f, cut = ParametricEQ::isCut (type);

    // bulle + pointe vers le point de la bande
    juce::Path shape;
    shape.addRoundedRectangle (box, 6.0f * u);
    const float tx = box.getCentreX(), tw = 6.0f * u;
    juce::Path tipP;
    if (pointsDown) tipP.addTriangle (tx - tw, box.getBottom() - 1.0f, tx + tw, box.getBottom() - 1.0f, tx, (float) getHeight() - 1.0f);
    else            tipP.addTriangle (tx - tw, box.getY() + 1.0f, tx + tw, box.getY() + 1.0f, tx, 1.0f);
    shape.addPath (tipP);
    g.setColour (ui::night.withAlpha (0.93f));
    g.fillPath (shape);
    g.setColour (ui::line.interpolatedWith (ui::ember, 0.45f));
    g.strokePath (shape, juce::PathStrokeType (std::max (1.0f, u)));

    const float sw = std::max (1.2f, 1.5f * u);
    auto hl = [&] (Zone z) { return hover == (int) z ? ui::bone : ui::haze; };
    iconPower (g, zPower, on ? ui::ember : hl (power), sw);
    iconHeadphones (g, zListen, listening ? ui::ember : hl (listen), sw);
    iconCross (g, zClose, hl (close), sw);

    const auto f = ui::font (ui::Face::mono, 12.0f * u, true);
    g.setFont (f);
    const auto textX = zPower.getRight() + 2.0f * u;
    const float rowH = (box.getHeight() - 8.0f * u) / 3.0f;
    auto row = [&] (int i) { return juce::Rectangle<float> (textX, box.getY() + 4.0f * u + rowH * (float) i, zListen.getX() - textX, rowH); };

    g.setColour (on ? ui::bone : ui::haze);
    g.drawText (hzText (get (ids::peqFreq[band])), row (0), juce::Justification::centredLeft, false);

    juce::String l2;
    if (ParametricEQ::hasGain (type))
    {
        const float gd = get (ids::peqGain[band]);
        l2 << (gd > 0.0f ? "+" : "") << juce::String (gd, 1) << " dB   ";
    }
    l2 << "Q: " << juce::String (get (ids::peqQ[band]), 3);
    g.setColour (ui::mist.withAlpha (on ? 0.95f : 0.6f));
    g.drawText (l2, row (1).withRight (box.getRight() - 6.0f * u), juce::Justification::centredLeft, false);

    // 3e ligne : type (menu) + pente des coupes (flèches)
    const auto r3 = zType;
    iconType (g, r3.withWidth (r3.getHeight() * 1.3f).withX (box.getX() + 5.0f * u), type, hover == typeMenu ? ui::bone : ui::ember, sw);
    const juce::String t3 = cut ? juce::String (kSlopeNames[juce::jlimit (0, 3, juce::roundToInt (get (ids::peqSlope[band])))]) : juce::String (kTypeNames[type]);
    g.setColour (hover == typeMenu ? ui::bone : ui::mist);
    g.drawText (t3, r3.withTrimmedLeft (r3.getHeight() * 1.3f + 8.0f * u), juce::Justification::centredLeft, false);
    if (cut)
    {
        triangle (g, zUp.withSizeKeepingCentre (zUp.getHeight() * 0.9f, zUp.getHeight() * 0.5f), true, hl (slopeUp));
        triangle (g, zDown.withSizeKeepingCentre (zDown.getHeight() * 0.9f, zDown.getHeight() * 0.5f), false, hl (slopeDown));
    }
    else
        triangle (g, juce::Rectangle<float> (zUp.getX(), r3.getY(), zUp.getWidth(), r3.getHeight()).withSizeKeepingCentre (7.0f * u, 4.0f * u), false, hl (typeMenu));
}

void BandBubble::mouseMove (const juce::MouseEvent& e)
{
    const int z = (int) zoneAt (e.position);
    if (z != hover) { hover = z; repaint(); }
    setMouseCursor (z >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void BandBubble::mouseDown (const juce::MouseEvent& e)
{
    switch (zoneAt (e.position))
    {
        case power:  set (ids::peqOn[band], get (ids::peqOn[band]) > 0.5f ? 0.0f : 1.0f); break;
        case listen:
            // casque : on entend la bande seule tant qu'on tient le bouton
            listenWas = get (ids::peqListen) > 0.5f;
            set (ids::peqListenBand, (float) band);
            set (ids::peqListen, 1.0f);
            hold.store (true);
            listening = true;
            repaint();
            break;
        case close:
            for (auto* id : { ids::peqType[band], ids::peqFreq[band], ids::peqGain[band], ids::peqQ[band], ids::peqSlope[band],
                              ids::peqDyn[band], ids::peqThr[band], ids::peqOn[band] })
                if (auto* p = apvts.getParameter (id))
                { p->beginChangeGesture(); p->setValueNotifyingHost (p->getDefaultValue()); p->endChangeGesture(); }
            set (ids::peqOn[band], 0.0f);
            if (onRemoved) onRemoved();
            break;
        case slopeUp:   set (ids::peqSlope[band], (float) juce::jmin (3, juce::roundToInt (get (ids::peqSlope[band])) + 1)); break;
        case slopeDown: set (ids::peqSlope[band], (float) juce::jmax (0, juce::roundToInt (get (ids::peqSlope[band])) - 1)); break;
        case typeMenu:  SpectrumAnalyzer::showBandMenu (*this, apvts, band, e.getScreenPosition()); break;
        case none: default: break;
    }
}

void BandBubble::mouseUp (const juce::MouseEvent&)
{
    if (! listening) return;
    hold.store (false);
    set (ids::peqListen, listenWas ? 1.0f : 0.0f);
    listening = false;
    repaint();
}

//==============================================================================
BandPanel::BandPanel (juce::AudioProcessorValueTreeState& s) : apvts (s)
{
    using Layout = KnobComponent::Layout;
    for (int b = 0; b < 8; ++b)
    {
        auto& x = *(sets[(size_t) b] = std::make_unique<Set>());
        const juce::String n = "Band " + juce::String (b + 1);
        x.freq = std::make_unique<KnobComponent> (apvts, ids::peqFreq[b], "FREQ", "20", "20k", Layout::textBelow,
                                                  n + " frequency. Double-click = reset.", lnf);
        x.gain = std::make_unique<KnobComponent> (apvts, ids::peqGain[b], "GAIN", "-18", "+18", Layout::textBelow,
                                                  n + " gain (bell and shelves). Double-click = 0 dB.", lnf, true);
        x.q    = std::make_unique<KnobComponent> (apvts, ids::peqQ[b], "Q", "0.1", "10", Layout::textBelow,
                                                  n + " Q: width of a bell, resonance of a cut. Double-click = reset.", lnf);
        x.thr  = std::make_unique<DragValue> (apvts, ids::peqThr[b], n + " dynamic threshold: the band acts only when its zone goes above it.");
        x.thr->setPrefix ("THR ");
        x.power.setTooltip (n + " on/off.");
        x.powerAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, ids::peqOn[b], x.power);
        x.dyn.setTooltip (n + " dynamic mode: the bell / shelf only acts when its zone goes above the threshold.");
        x.dynAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, ids::peqDyn[b], x.dyn);
        for (juce::Component* c : std::initializer_list<juce::Component*> { x.freq.get(), x.gain.get(), x.q.get(), x.thr.get(), &x.power, &x.dyn })
            addChildComponent (c);
    }
    setBufferedToImage (true);   // l'analyseur dessous bouge à chaque image : le panneau (ombre comprise) reste une image prête
    setInterceptsMouseClicks (true, true);
    setAlpha (0.0f);
    setVisible (false);
    setBand (2);
}

BandPanel::~BandPanel()
{
    for (auto& s : sets) { s->freq.reset(); s->gain.reset(); s->q.reset(); }   // avant leur LookAndFeel
}

void BandPanel::setBand (int b)
{
    band = juce::jlimit (0, 7, b);
    for (int i = 0; i < 8; ++i)
    {
        auto& x = *sets[(size_t) i];
        const bool v = i == band;
        for (juce::Component* c : std::initializer_list<juce::Component*> { x.freq.get(), x.gain.get(), x.q.get(), x.thr.get(), &x.power, &x.dyn })
            c->setVisible (v);
    }
    shownType = shownSlope = -1;
    repaint();
}

void BandPanel::setOpen (bool shouldOpen)
{
    open = shouldOpen;
    if (open) setVisible (true);
}

void BandPanel::resized()
{
    const auto r = getLocalBounds().toFloat();
    const float u = r.getHeight() / 160.0f;
    const float hump = 18.0f * u, m = 12.0f * u;   // marge : l'ombre portée reste dans le composant
    body = r.reduced (m).withTrimmedTop (hump);
    const float w = body.getWidth();
    // colonnes : gauche (marche, type, pente) | FREQ | GAIN (bosse) | Q | droite (bandes, fermer, DYN, seuil)
    const float sideW = w * 0.20f, knobW = (w - 2.0f * sideW) / 3.2f, gainW = knobW * 1.2f;
    auto left = body.withWidth (sideW).reduced (10.0f * u, 10.0f * u);
    auto right = body.withLeft (body.getRight() - sideW).reduced (10.0f * u, 10.0f * u);
    const float kx = body.getX() + sideW;
    const auto freqR = juce::Rectangle<float> (kx, body.getY() + 14.0f * u, knobW, body.getHeight() - 20.0f * u);
    const auto gainR = juce::Rectangle<float> (kx + knobW, r.getY() + m + 2.0f * u, gainW, body.getBottom() - r.getY() - m - 8.0f * u);
    const auto qR    = juce::Rectangle<float> (kx + knobW + gainW, freqR.getY(), knobW, freqR.getHeight());
    gainCentre = gainR;

    // forme : rectangle arrondi + bosse douce au-dessus du GAIN
    shape.clear();
    const float rad = 16.0f * u, hx0 = gainR.getX() - 26.0f * u, hx1 = gainR.getRight() + 26.0f * u, top = body.getY(), ht = r.getY() + m;
    shape.startNewSubPath (body.getX() + rad, top);
    shape.lineTo (hx0, top);
    shape.cubicTo (hx0 + 30.0f * u, top, hx0 + 22.0f * u, ht, hx0 + 56.0f * u, ht);
    shape.lineTo (hx1 - 56.0f * u, ht);
    shape.cubicTo (hx1 - 22.0f * u, ht, hx1 - 30.0f * u, top, hx1, top);
    shape.lineTo (body.getRight() - rad, top);
    shape.quadraticTo (body.getRight(), top, body.getRight(), top + rad);
    shape.lineTo (body.getRight(), body.getBottom() - rad);
    shape.quadraticTo (body.getRight(), body.getBottom(), body.getRight() - rad, body.getBottom());
    shape.lineTo (body.getX() + rad, body.getBottom());
    shape.quadraticTo (body.getX(), body.getBottom(), body.getX(), body.getBottom() - rad);
    shape.lineTo (body.getX(), top + rad);
    shape.quadraticTo (body.getX(), top, body.getX() + rad, top);
    shape.closeSubPath();

    const float rowH = 30.0f * u;
    zType = left.withTrimmedTop (rowH + 12.0f * u).withHeight (rowH);
    // de droite à gauche : x, puis < n > (jamais l'un sur l'autre)
    zClose = right.withHeight (rowH).withLeft (right.getRight() - rowH);
    zNext = zClose.translated (-rowH - 10.0f * u, 0.0f).withWidth (rowH * 0.8f).translated (rowH * 0.2f, 0.0f);
    zPrev = zNext.translated (-rowH * 1.7f, 0.0f);
    for (auto& s : sets)
    {
        auto& x = *s;
        x.power.setBounds (left.withHeight (rowH).withWidth (rowH).toNearestInt());
        x.freq->setBounds (freqR.toNearestInt());
        x.gain->setBounds (gainR.toNearestInt());
        x.q->setBounds (qR.toNearestInt());
        auto rr = right.withTrimmedTop (rowH + 12.0f * u);
        x.dyn.setBounds (rr.removeFromTop (rowH * 0.85f).toNearestInt());
        rr.removeFromTop (8.0f * u);
        x.thr->setBounds (rr.removeFromTop (rowH * 0.85f).toNearestInt());
    }
}

BandPanel::Zone BandPanel::zoneAt (juce::Point<float> p) const noexcept
{
    if (zPrev.contains (p)) return prev;
    if (zNext.contains (p)) return next;
    if (zClose.contains (p)) return close;
    if (zType.contains (p)) return typeMenu;
    return none;
}

void BandPanel::tick()
{
    // apparition / disparition : fondu + glissement
    const float target = open ? 1.0f : 0.0f;
    if (ui::approach (shown, target, 0.22f))
    {
        setAlpha (shown);
        setTransform (juce::AffineTransform::translation (0.0f, (1.0f - shown) * 14.0f * (float) getHeight() / 160.0f));
        if (! open && shown <= 0.0f) setVisible (false);
    }
    if (! isVisible()) return;
    auto& x = *sets[(size_t) band];
    x.freq->tick(); x.gain->tick(); x.q->tick(); x.thr->tick(); x.power.tick(); x.dyn.tick();
    const int type = juce::roundToInt (get (ids::peqType[band])), slope = juce::roundToInt (get (ids::peqSlope[band]));
    const bool can = ParametricEQ::canBeDynamic (type), dyn = can && get (ids::peqDyn[band]) > 0.5f;
    x.gain->setAlpha (ParametricEQ::hasGain (type) ? 1.0f : 0.3f);
    x.gain->setEnabled (ParametricEQ::hasGain (type));
    x.dyn.setEnabled (can);
    x.dyn.setAlpha (can ? 1.0f : 0.35f);
    x.thr->setAlpha (dyn ? 1.0f : 0.35f);
    if (type != shownType || slope != shownSlope) { shownType = type; shownSlope = slope; repaint(); }
}

void BandPanel::paint (juce::Graphics& g)
{
    const float u = (float) getHeight() / 160.0f;
    juce::DropShadow (juce::Colours::black.withAlpha (0.6f), juce::roundToInt (22.0f * u), { 0, juce::roundToInt (8.0f * u) }).drawForPath (g, shape);
    g.setGradientFill (juce::ColourGradient (ui::deep2.withAlpha (0.97f), 0.0f, 0.0f, ui::night.withAlpha (0.97f), 0.0f, (float) getHeight(), false));
    g.fillPath (shape);
    g.setColour (ui::line.interpolatedWith (ui::ember, 0.35f));
    g.strokePath (shape, juce::PathStrokeType (std::max (1.0f, 1.2f * u)));

    const int type = juce::jlimit (0, 5, shownType < 0 ? 0 : shownType);
    const bool cut = ParametricEQ::isCut (type);
    const float sw = std::max (1.2f, 1.6f * u);

    // type de la bande (menu) + pente
    {
        const auto r = zType;
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (hover == typeMenu ? ui::ember : ui::line);
        g.drawRoundedRectangle (r.reduced (0.5f), r.getHeight() * 0.5f, std::max (1.0f, u));
        iconType (g, r.withWidth (r.getHeight() * 1.2f).translated (8.0f * u, 0.0f), type, ui::ember, sw);
        g.setColour (ui::mist);
        g.setFont (ui::fitFont (ui::Face::body, r.getHeight() * 0.48f, r.getWidth() - r.getHeight() * 1.9f, kTypeNames[type], true));
        g.drawText (kTypeNames[type], r.withTrimmedLeft (r.getHeight() * 1.2f + 12.0f * u), juce::Justification::centredLeft, false);
        if (cut)
        {
            g.setColour (ui::haze);
            g.setFont (ui::font (ui::Face::mono, 12.0f * u, true));
            g.drawText (kSlopeNames[juce::jlimit (0, 3, shownSlope)], r.translated (0.0f, r.getHeight() + 6.0f * u).withTrimmedLeft (10.0f * u),
                        juce::Justification::centredLeft, false);
        }
    }

    // < n >   x
    {
        auto hc = [&] (Zone z) { return hover == (int) z ? ui::bone : ui::haze; };
        g.setColour (ui::mist);
        g.setFont (ui::font (ui::Face::mono, 14.0f * u, true));
        g.drawText (juce::String (band + 1), juce::Rectangle<float> (zPrev.getRight(), zPrev.getY(), zNext.getX() - zPrev.getRight(), zPrev.getHeight()),
                    juce::Justification::centred, false);
        iconCross (g, zClose, hc (close), sw);
        // flèches gauche / droite (triangles couchés)
        juce::Path pl, pr;
        const auto a = zPrev.withSizeKeepingCentre (zPrev.getHeight() * 0.25f, zPrev.getHeight() * 0.38f);
        const auto b = zNext.withSizeKeepingCentre (zNext.getHeight() * 0.25f, zNext.getHeight() * 0.38f);
        pl.addTriangle (a.getRight(), a.getY(), a.getRight(), a.getBottom(), a.getX(), a.getCentreY());
        pr.addTriangle (b.getX(), b.getY(), b.getX(), b.getBottom(), b.getRight(), b.getCentreY());
        g.setColour (hc (prev)); g.fillPath (pl);
        g.setColour (hc (next)); g.fillPath (pr);
    }
}

void BandPanel::mouseMove (const juce::MouseEvent& e)
{
    const int z = (int) zoneAt (e.position);
    if (z != hover) { hover = z; repaint(); }
    setMouseCursor (z >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void BandPanel::mouseDown (const juce::MouseEvent& e)
{
    switch (zoneAt (e.position))
    {
        case prev:     if (onBand) onBand ((band + 7) % 8); break;
        case next:     if (onBand) onBand ((band + 1) % 8); break;
        case close:    if (onClose) onClose(); break;
        case typeMenu: SpectrumAnalyzer::showBandMenu (*this, apvts, band, e.getScreenPosition()); break;
        case none: default: break;
    }
}

//==============================================================================
void IconButton::tick()
{
    bool changed = ui::approach (glow, isDown() ? 1.0f : (isOver() ? 0.7f : 0.0f), 0.2f);
    changed |= ui::approach (lit, getToggleState() ? 1.0f : 0.0f, 0.2f);
    if (changed || lit > 0.01f) repaint();   // allumé : la pastille respire
}

void IconButton::paintButton (juce::Graphics& g, bool, bool)
{
    const auto b = getLocalBounds().toFloat().reduced (1.0f);
    const float h = b.getHeight(), u = h / 28.0f;
    juce::Path shape;
    shape.addRoundedRectangle (b, h * 0.5f);
    ui::strokeGlow (g, shape, ui::ember, 1.2f * u, 0.6f * glow);
    g.setColour (juce::Colours::black.withAlpha (0.38f));
    g.fillPath (shape);
    g.setColour (ui::ember.withAlpha (0.14f * glow));
    g.fillPath (shape);
    if (lit > 0.001f)
    {
        // allumé (écoute) : pastille braise qui respire doucement
        const float breathe = 0.85f + 0.15f * std::sin ((float) juce::Time::getMillisecondCounter() * 0.004f);
        g.setGradientFill (juce::ColourGradient (ui::ember.withAlpha (0.75f * lit * breathe), b.getX(), b.getY(),
                                                 ui::blood.withAlpha (0.8f * lit), b.getRight(), b.getBottom(), false));
        g.fillPath (shape);
    }
    g.setColour (ui::line.interpolatedWith (ui::ember, juce::jmax (lit, 0.25f + 0.75f * glow)));
    g.strokePath (shape, juce::PathStrokeType (std::max (1.0f, 1.2f * u)));

    const float s = h * 0.42f;
    const float cx = text.isEmpty() ? b.getCentreX() : b.getX() + h * 0.62f, cy = b.getCentreY();
    if (icon == Icon::headphones)
    {
        // casque : arceau + deux écouteurs arrondis
        juce::Path hp;
        const float r = s * 0.62f;
        hp.addCentredArc (cx, cy + r * 0.15f, r, r, 0.0f, -kPi * 0.62f, kPi * 0.62f, true);
        g.setColour (ui::haze.interpolatedWith (ui::bone, juce::jmax (0.45f, juce::jmax (glow, lit))));
        g.strokePath (hp, juce::PathStrokeType (std::max (1.3f, 1.8f * u), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        for (float sx : { -1.0f, 1.0f })
        {
            const auto cup = juce::Rectangle<float> (r * 0.5f, r * 0.82f).withCentre ({ cx + sx * r * 0.86f, cy + r * 0.62f });
            g.fillRoundedRectangle (cup, r * 0.2f);
        }
        if (text.isNotEmpty())
        {
            const auto tr = b.withTrimmedLeft (h * 1.15f).withTrimmedRight (h * 0.45f);
            g.setFont (ui::fitFont (ui::Face::body, h * 0.44f, tr.getWidth(), text, true, 0.10f));
            g.drawText (text, tr, juce::Justification::centred, false);
        }
        return;
    }

    // pictogramme : 4 coins vers l'extérieur (plein écran) ou vers l'intérieur (sortie)
    const float k = s * 0.36f;
    juce::Path ic;
    for (int q = 0; q < 4; ++q)
    {
        const float sx = (q & 1) ? 1.0f : -1.0f, sy = (q & 2) ? 1.0f : -1.0f;
        const float x = cx + sx * s * 0.5f, y = cy + sy * s * 0.5f;
        if (icon == Icon::expand)
        {
            ic.startNewSubPath (x - sx * k, y);
            ic.lineTo (x, y);
            ic.lineTo (x, y - sy * k);
        }
        else
        {
            const float ix = x - sx * k, iy = y - sy * k;
            ic.startNewSubPath (ix + sx * k, iy);
            ic.lineTo (ix, iy);
            ic.lineTo (ix, iy + sy * k);
        }
    }
    g.setColour (ui::haze.interpolatedWith (ui::bone, juce::jmax (0.4f, glow)));
    g.strokePath (ic, juce::PathStrokeType (std::max (1.3f, 1.9f * u), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (text.isNotEmpty())
    {
        const auto tr = b.withTrimmedLeft (h * 1.15f).withTrimmedRight (h * 0.45f);
        g.setColour (ui::haze.interpolatedWith (ui::bone, juce::jmax (0.5f, glow)));
        g.setFont (ui::fitFont (ui::Face::body, h * 0.44f, tr.getWidth(), text, true, 0.10f));
        g.drawText (text, tr, juce::Justification::centred, false);
    }
}

//==============================================================================
EqFullView::EqFullView (OniriqueVocalBoxAudioProcessor& p) : analyzer (p, true), processor (p), panel (p.apvts)
{
    setWantsKeyboardFocus (true);
    setOpaque (true);
    analyzer.setActive (false);
    addAndMakeVisible (analyzer);
    children.push_back (&analyzer);
    addAndMakeVisible (exitButton);
    exitButton.setText ("EXIT FULL SCREEN");
    exitButton.setTooltip ("Back to the plugin (Esc).");
    exitButton.onClick = [this] { if (onClose) onClose(); };
    children.push_back (&exitButton);
    addAndMakeVisible (power);
    power.setTooltip ("Turn the parametric EQ on/off (click-free).");
    powerAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, ids::peqPower, power);
    addAndMakeVisible (listenButton);
    listenButton.setText ("LISTEN");
    listenButton.setTooltip ("Band listen: when lit, click and hold a band's dot to hear only that band (the frequencies it acts on); release it to hear the whole voice again.");
    listenAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, ids::peqListen, listenButton);
    children.push_back (&listenButton);
    children.push_back (&power);

    // panneau flottant de la bande : s'ouvre quand on choisit une bande, se ferme d'un clic dans le vide ou sur x
    addChildComponent (panel);
    children.push_back (&panel);
    panel.onBand = [this] (int b) { selectBand (b); if (onSelect) onSelect (b); };
    panel.onClose = [this] { panel.setOpen (false); };
    analyzer.onSelect = [this] (int b) { selectBand (b); panel.setOpen (true); if (onSelect) onSelect (b); };
    analyzer.onBackgroundClick = [this] { panel.setOpen (false); };
}

EqFullView::~EqFullView() = default;

void EqFullView::setShown (bool shown)
{
    motion.show (*this, shown);   // fondu + zoom ; la vue se cache à la fin de la fermeture
    analyzer.setActive (shown);
    if (shown) grabKeyboardFocus();
}

void EqFullView::selectBand (int b)
{
    selected = juce::jlimit (0, 7, b);
    analyzer.setSelectedSilently (selected);
    panel.setBand (selected);
}

void EqFullView::tick()
{
    if (motion.tick (*this) || ! isVisible()) return;   // en transition : le calque seul s'anime
    for (auto* a : children) a->tick();
}

void EqFullView::resized()
{
    const float u = ui::editorScale (*this);
    auto r = getLocalBounds().toFloat().reduced (16.0f * u, 12.0f * u);
    titleArea = r.removeFromTop (40.0f * u);
    exitButton.setBounds (juce::Rectangle<float> (206.0f * u, 32.0f * u).withRightX (titleArea.getRight()).withCentre ({ titleArea.getRight() - 103.0f * u, titleArea.getCentreY() }).toNearestInt());
    power.setBounds (juce::Rectangle<float> (32.0f * u, 32.0f * u).withCentre ({ (float) exitButton.getX() - 26.0f * u, titleArea.getCentreY() }).toNearestInt());
    listenButton.setBounds (juce::Rectangle<float> (126.0f * u, 30.0f * u).withRightX ((float) power.getX() - 10.0f * u).withY (titleArea.getCentreY() - 15.0f * u).toNearestInt());
    r.removeFromTop (8.0f * u);
    analyzer.setBounds (r.toNearestInt());

    // panneau de bande : centré en bas de l'analyseur, au-dessus de l'échelle des fréquences
    const float pw = 760.0f * u, ph = 210.0f * u;
    panel.setBounds (juce::Rectangle<float> (pw, ph).withCentre ({ r.getCentreX() - 20.0f * u, r.getBottom() - 34.0f * u - ph * 0.5f }).toNearestInt());
}

void EqFullView::paint (juce::Graphics& g)
{
    const float u = ui::editorScale (*this);
    const auto r = getLocalBounds().toFloat();
    const float rad = 16.0f * u;
    g.fillAll (ui::night);   // opaque (coins compris) : rien du plugin n'est redessiné derrière la vue
    g.setGradientFill (juce::ColourGradient (ui::deep2, 0.0f, r.getY(), ui::night, 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle (r, rad);
    g.setColour (ui::line.interpolatedWith (ui::ember, 0.25f));
    g.drawRoundedRectangle (r.reduced (0.5f), rad, std::max (1.0f, u));

    // titre + aide
    {
        const float dot = 7.0f * u;
        const auto dr = juce::Rectangle<float> (dot, dot).withCentre ({ titleArea.getX() + 8.0f * u, titleArea.getCentreY() });
        g.setColour (ui::ember.withAlpha (0.35f));
        g.fillEllipse (dr.expanded (dot * 0.7f));
        g.setColour (ui::ember);
        g.fillEllipse (dr);
        const auto f = ui::font (ui::Face::display, 22.0f * u, true, 0.08f);
        const juce::String t = "PARAMETRIC EQ";
        const float tw = ui::textWidth (f, t) + 10.0f * u;
        g.setFont (f);
        g.setColour (ui::mist);
        g.drawText (t, titleArea.withX (titleArea.getX() + 24.0f * u).withWidth (tw), juce::Justification::centredLeft, false);
        const float hintX = titleArea.getX() + 24.0f * u + tw + 14.0f * u;
        const auto hint = juce::Rectangle<float> (hintX, titleArea.getY(), (float) listenButton.getX() - 16.0f * u - hintX, titleArea.getHeight());
        const juce::String h = juce::String (juce::CharPointer_UTF8 ("8 bands  \xc2\xb7  drag the dots (Shift = fine)  \xc2\xb7  wheel = Q  \xc2\xb7  "
                                                                     "click a dot = its settings  \xc2\xb7  double-click = add  \xc2\xb7  right-click = menu  \xc2\xb7  Esc = exit"));
        g.setColour (ui::haze);
        g.setFont (ui::fitFont (ui::Face::body, 13.0f * u, hint.getWidth(), h, false, 0.03f));
        g.drawText (h, hint, juce::Justification::centredLeft, false);
    }

}

bool EqFullView::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey) { if (onClose) onClose(); return true; }
    return false;
}

//==============================================================================
void FxButton::tick()
{
    phase += 1.0f / (60.0f * 4.0f);   // un tour du liseré en 4 s
    if (phase > 1.0f) phase -= 1.0f;
    ui::approach (glow, isDown() ? 1.0f : (isOver() ? 0.8f : 0.0f), 0.2f);
    ui::approach (litAmt, lit ? 1.0f : 0.0f, 0.12f);
    repaint();
}

void FxButton::paintButton (juce::Graphics& g, bool, bool)
{
    const auto b = getLocalBounds().toFloat().reduced (3.0f);
    const float h = b.getHeight(), u = h / 30.0f;
    juce::Path shape;
    shape.addRoundedRectangle (b, h * 0.5f);
    // éteint (aucun effet) : verre sombre discret ; allumé : pastille braise lumineuse, liseré qui tourne
    ui::strokeGlow (g, shape, ui::ember, 1.4f * u, 0.15f + 0.4f * litAmt + 0.4f * glow);
    g.setColour (juce::Colours::black.withAlpha (0.38f));
    g.fillPath (shape);
    g.setGradientFill (juce::ColourGradient (ui::ember.withAlpha ((0.10f + 0.25f * litAmt) + 0.15f * glow), b.getX(), b.getY(),
                                             ui::blood.withAlpha (0.25f + 0.35f * litAmt), b.getRight(), b.getBottom(), false));
    g.fillPath (shape);

    // liseré lumineux qui tourne lentement
    const auto c = b.getCentre();
    const float ang = phase * juce::MathConstants<float>::twoPi;
    const juce::Point<float> d (std::cos (ang) * b.getWidth() * 0.5f, std::sin (ang) * b.getHeight() * 0.5f);
    juce::ColourGradient rim (ui::bone, c.x + d.x, c.y + d.y, ui::ember.withAlpha (0.35f), c.x - d.x, c.y - d.y, false);
    rim.addColour (0.5, ui::ember);
    g.setGradientFill (rim);
    g.strokePath (shape, juce::PathStrokeType (std::max (1.2f, 1.6f * u)));

    // étincelle (étoile à 4 branches du logo) + petite sœur
    auto star = [&] (juce::Point<float> p, float r, float alpha)
    {
        juce::Path s;
        const float k = 0.2f * r;
        s.startNewSubPath (p.x, p.y - r);
        s.lineTo (p.x + k, p.y - k); s.lineTo (p.x + r, p.y); s.lineTo (p.x + k, p.y + k);
        s.lineTo (p.x, p.y + r);     s.lineTo (p.x - k, p.y + k); s.lineTo (p.x - r, p.y); s.lineTo (p.x - k, p.y - k);
        s.closeSubPath();
        g.setColour (ui::bone.withAlpha (alpha));
        g.fillPath (s);
    };
    const float tw = 0.5f + 0.5f * std::sin (phase * juce::MathConstants<float>::twoPi * 2.0f);
    star ({ b.getX() + h * 0.62f, b.getCentreY() + 1.0f * u }, h * 0.30f, 1.0f);
    star ({ b.getX() + h * 0.98f, b.getCentreY() - h * 0.22f }, h * 0.12f, 0.5f + 0.5f * tw);

    const auto tr = b.withTrimmedLeft (h * 1.2f).withTrimmedRight (h * 0.45f);
    g.setColour (ui::bone);
    g.setFont (ui::fitFont (ui::Face::display, h * 0.46f, tr.getWidth(), getButtonText(), true, 0.10f));
    g.drawText (getButtonText(), tr, juce::Justification::centred, false);
}

//==============================================================================
PresetBar::PresetBar (onirique::PresetManager& p) : pm (p)
{
    addChildComponent (editor);
    editor.setJustification (juce::Justification::centred);
    editor.setColour (juce::TextEditor::backgroundColourId, ui::night.withAlpha (0.9f));
    editor.setColour (juce::TextEditor::textColourId, ui::mist);
    editor.setColour (juce::TextEditor::outlineColourId, ui::ember);
    editor.setColour (juce::TextEditor::focusedOutlineColourId, ui::ember);
    editor.setColour (juce::TextEditor::highlightColourId, ui::ember.withAlpha (0.4f));
    editor.setColour (juce::CaretComponent::caretColourId, ui::bone);
    editor.setTextToShowWhenEmpty ("Preset name", ui::haze);
    editor.onReturnKey = [this] { endSave (true); };
    editor.onEscapeKey = [this] { endSave (false); };
    editor.onFocusLost = [this] { endSave (false); };
    shownName = pm.getCurrentName();
}

void PresetBar::resized()
{
    const auto r = getLocalBounds().toFloat();
    const float h = r.getHeight();
    auto rest = r;
    zones[save] = rest.removeFromRight (h * 2.1f);
    rest.removeFromRight (h * 0.2f);
    zones[prev] = rest.removeFromLeft (h);
    zones[next] = rest.removeFromRight (h);
    zones[name] = rest;
    editor.setBounds (zones[name].reduced (2.0f, h * 0.14f).toNearestInt());
    editor.setFont (ui::font (ui::Face::body, h * 0.40f, true));
}

PresetBar::Zone PresetBar::zoneAt (juce::Point<float> p) const noexcept
{
    for (int z = 0; z < 4; ++z) if (zones[z].contains (p)) return (Zone) z;
    return none;
}

void PresetBar::tick()
{
    bool changed = false;
    for (int z = 0; z < 4; ++z) changed |= ui::approach (glow[z], hover == z ? 1.0f : 0.0f, 0.2f);
    if ((++frame % 10) == 0)
    {
        const auto n = pm.getCurrentName();
        const bool mod = pm.isModified();
        if (n != shownName || mod != modified) { shownName = n; modified = mod; changed = true; }
    }
    if (changed) repaint();
}

void PresetBar::mouseMove (const juce::MouseEvent& e)
{
    const auto z = zoneAt (e.position);
    if (z != hover) { hover = z; repaint(); }
}

void PresetBar::mouseExit (const juce::MouseEvent&) { hover = none; }

void PresetBar::mouseDown (const juce::MouseEvent& e)
{
    switch (zoneAt (e.position))
    {
        case prev: pm.step (-1); break;
        case next: pm.step (1); break;
        case name: showMenu(); break;
        case save: startSave(); break;
        default: break;
    }
    shownName = pm.getCurrentName();
    modified = pm.isModified();
    repaint();
}

void PresetBar::showMenu()
{
    const auto entries = pm.list();
    const auto current = pm.getCurrentName();
    juce::PopupMenu menu;
    bool userHeader = false;
    menu.addSectionHeader ("FACTORY");
    for (int i = 0; i < (int) entries.size(); ++i)
    {
        const auto& e = entries[(size_t) i];
        if (! e.factory && ! userHeader) { menu.addSectionHeader ("USER"); userHeader = true; }
        menu.addItem (i + 1, e.name, true, e.name == current);
    }
    menu.addSeparator();
    menu.addItem (1000, "Save as new preset...");
    bool currentIsUser = false;
    for (const auto& e : entries) if (! e.factory && e.name == current) currentIsUser = true;
    menu.addItem (1001, "Delete \"" + current + "\"", currentIsUser);
    menu.addItem (1002, "Open presets folder");

    menu.setLookAndFeel (&getLookAndFeel());   // sinon JUCE juge la fenêtre opaque avec le style système (coins blancs)
    juce::Component::SafePointer<PresetBar> self (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (getWidth())
                            .withParentComponent (findParentComponentOfClass<juce::AudioProcessorEditor>()),
                        [self, entries] (int r)
                        {
                            if (self == nullptr || r == 0) return;
                            auto& bar = *self;
                            if (r >= 1 && r <= (int) entries.size()) bar.pm.load (entries[(size_t) (r - 1)]);
                            else if (r == 1000) bar.startSave();
                            else if (r == 1001)
                            {
                                for (const auto& e : entries)
                                    if (! e.factory && e.name == bar.pm.getCurrentName()) bar.pm.remove (e);
                                bar.pm.load (entries.front());   // retour sur Init
                            }
                            else if (r == 1002) onirique::PresetManager::folder().startAsProcess();
                            bar.repaint();
                        });
}

void PresetBar::startSave()
{
    editor.setText (shownName == "Init" ? juce::String() : shownName, false);
    editor.setVisible (true);
    editor.selectAll();
    editor.grabKeyboardFocus();
    repaint();
}

void PresetBar::endSave (bool commit)
{
    if (! editor.isVisible()) return;
    const auto text = editor.getText();
    editor.setVisible (false);
    if (commit) pm.save (text);
    shownName = pm.getCurrentName();
    modified = pm.isModified();
    repaint();
}

void PresetBar::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const float h = r.getHeight(), u = h / 40.0f;

    // conteneur : précédent | nom | suivant
    const auto box = zones[prev].getUnion (zones[next]).reduced (0.0f, 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.36f));
    g.fillRoundedRectangle (box, box.getHeight() * 0.5f);
    g.setColour (ui::line.interpolatedWith (ui::ember, 0.6f * glow[name]));
    g.drawRoundedRectangle (box.reduced (0.5f), box.getHeight() * 0.5f, std::max (1.0f, 1.2f * u));

    for (int z : { (int) prev, (int) next })
    {
        const auto c = zones[z].getCentre();
        const float d = (z == prev ? -1.0f : 1.0f) * 4.0f * u;
        juce::Path chev;
        chev.startNewSubPath (c.x - d, c.y - 6.0f * u);
        chev.lineTo (c.x + d, c.y);
        chev.lineTo (c.x - d, c.y + 6.0f * u);
        g.setColour (ui::haze.interpolatedWith (ui::bone, glow[z]));
        g.strokePath (chev, juce::PathStrokeType (std::max (1.3f, 2.0f * u), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    if (! editor.isVisible())
    {
        auto nr = zones[name].reduced (4.0f * u, 0.0f);
        g.setColour (ui::haze.withAlpha (0.8f));
        g.setFont (ui::font (ui::Face::mono, 9.0f * u, false, 0.1f));
        g.drawText ("PRESET", nr.removeFromTop (h * 0.42f).withTrimmedTop (h * 0.10f), juce::Justification::centredBottom, false);
        const juce::String txt = shownName + (modified ? " *" : "");
        g.setColour (ui::mist.interpolatedWith (ui::bone, glow[name]));
        g.setFont (ui::fitFont (ui::Face::body, h * 0.36f, nr.getWidth() - 18.0f * u, txt, true, 0.04f));
        g.drawText (txt, nr.withTrimmedBottom (h * 0.08f), juce::Justification::centredTop, false);
        // petit triangle : la liste s'ouvre au clic
        const float tx = zones[name].getRight() - 8.0f * u, ty = zones[name].getCentreY();
        juce::Path tri;
        tri.addTriangle (tx - 4.0f * u, ty - 2.0f * u, tx + 4.0f * u, ty - 2.0f * u, tx, ty + 3.0f * u);
        g.setColour (ui::haze.interpolatedWith (ui::ember, glow[name]));
        g.fillPath (tri);
    }

    // SAVE
    const auto s = zones[save].reduced (0.0f, 1.0f);
    juce::Path sp;
    sp.addRoundedRectangle (s, s.getHeight() * 0.5f);
    ui::strokeGlow (g, sp, ui::ember, 1.2f * u, glow[save] * 0.8f);
    g.setColour (juce::Colours::black.withAlpha (0.36f));
    g.fillPath (sp);
    g.setColour (ui::ember.withAlpha (0.18f * glow[save]));
    g.fillPath (sp);
    g.setColour (ui::line.interpolatedWith (ui::ember, glow[save]));
    g.strokePath (sp, juce::PathStrokeType (std::max (1.0f, 1.2f * u)));
    g.setColour (ui::haze.interpolatedWith (ui::bone, glow[save]));
    g.setFont (ui::fitFont (ui::Face::body, h * 0.36f, s.getWidth() - 10.0f * u, "SAVE", true, 0.16f));
    g.drawText ("SAVE", s, juce::Justification::centred, false);
}

//==============================================================================
void TabBar::tick()
{
    bool changed = false;
    if (slide < 0.0f) { slide = (float) selected; changed = true; }
    changed |= ui::spring (slide, slideVel, (float) selected);
    for (int t = 0; t < 2; ++t) changed |= ui::approach (led[t], moduleOn (t) ? 1.0f : 0.0f, 0.15f);
    if (changed) repaint();
}

void TabBar::mouseDown (const juce::MouseEvent& e)
{
    const int t = juce::jlimit (0, labels.size() - 1, (int) (e.position.x * (float) labels.size() / (float) getWidth()));
    if (t != selected)
    {
        selected = t;
        if (onChange) onChange (t);
        repaint();
    }
}

juce::Rectangle<int> TabBar::iconArea (int t) const
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const float u = r.getHeight() / 28.0f, w = r.getWidth() / (float) labels.size();
    const auto cell = juce::Rectangle<float> (r.getX() + w * (float) t, r.getY(), w, r.getHeight());
    const auto f = ui::fitFont (ui::Face::body, r.getHeight() * 0.44f, cell.getWidth() - 40.0f * u, labels[t], true, 0.14f);
    const float d = r.getHeight() * 0.86f;
    const float x0 = cell.getCentreX() - (ui::textWidth (f, labels[t]) + d + 8.0f * u) * 0.5f;
    return juce::Rectangle<float> (d, d).withPosition (x0, cell.getCentreY() - d * 0.5f).toNearestInt() + getPosition();
}

void TabBar::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const float u = r.getHeight() / 28.0f;
    const int n = labels.size();
    const float w = r.getWidth() / (float) n;

    // onglets : trait de base + pastille qui glisse sous l'onglet choisi
    g.setColour (ui::line);
    g.fillRect (juce::Rectangle<float> (r.getX(), r.getBottom() - 1.0f, r.getWidth(), 1.0f));
    const auto pill = juce::Rectangle<float> (r.getX() + w * slide, r.getY(), w, r.getHeight()).reduced (3.0f * u, 2.0f * u);
    g.setColour (ui::ember.withAlpha (0.14f));
    g.fillRoundedRectangle (pill, 8.0f * u);
    juce::Path underline;
    underline.startNewSubPath (pill.getX() + 10.0f * u, r.getBottom() - 1.0f);
    underline.lineTo (pill.getRight() - 10.0f * u, r.getBottom() - 1.0f);
    ui::strokeGlow (g, underline, ui::ember, 2.0f * u, 0.8f);
    g.setColour (ui::ember);
    g.strokePath (underline, juce::PathStrokeType (2.2f * u, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    for (int t = 0; t < n; ++t)
    {
        const auto cell = juce::Rectangle<float> (r.getX() + w * (float) t, r.getY(), w, r.getHeight());
        const float sel = juce::jlimit (0.0f, 1.0f, 1.0f - std::abs (slide - (float) t));
        const auto f = ui::fitFont (ui::Face::body, r.getHeight() * 0.44f, cell.getWidth() - 40.0f * u, labels[t], true, 0.14f);
        const float tw = ui::textWidth (f, labels[t]);
        // place réservée à gauche du nom pour le bouton power du sous-module (posé par l'éditeur, voir iconArea)
        const float d = r.getHeight() * 0.86f;
        const float x0 = cell.getCentreX() - (tw + d + 8.0f * u) * 0.5f;
        g.setColour (ui::haze.interpolatedWith (ui::bone, sel));
        g.setFont (f);
        g.drawText (labels[t], juce::Rectangle<float> (x0 + d + 8.0f * u, cell.getY(), tw + 4.0f, cell.getHeight()),
                    juce::Justification::centredLeft, false);
    }
}

//==============================================================================
MultibandDisplay::MultibandDisplay (OniriqueVocalBoxAudioProcessor& p) : processor (p)
{
    using namespace onirique;
    for (int i = 0; i < 3; ++i) { xP[i] = p.apvts.getParameter (ids::mbX[i]); x[i] = value (xP[i]); }
    for (int b = 0; b < kB; ++b)
    {
        thrP[b] = p.apvts.getParameter (ids::mbThr[b]);
        gainP[b] = p.apvts.getParameter (ids::mbGain[b]);
        thr[b] = value (thrP[b]);
        gain[b] = value (gainP[b]);
    }
}

juce::Rectangle<float> MultibandDisplay::plotArea() const noexcept
{
    const float u = (float) getHeight() / 200.0f;
    return getLocalBounds().toFloat().reduced (10.0f * u, 8.0f * u).withTrimmedBottom (16.0f * u).withTrimmedRight (30.0f * u);
}

float MultibandDisplay::xOf (float hz) const noexcept
{
    const auto p = plotArea();
    return p.getX() + juce::jlimit (0.0f, 1.0f, std::log (hz / 20.0f) / std::log (1000.0f)) * p.getWidth();
}

float MultibandDisplay::hzOf (float px) const noexcept
{
    const auto p = plotArea();
    return 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, (px - p.getX()) / p.getWidth()));
}

float MultibandDisplay::yOf (float db) const noexcept   // échelle de niveau : 0 dB en haut, -60 en bas
{
    const auto p = plotArea();
    return p.getY() + juce::jlimit (0.0f, 1.0f, -db / 60.0f) * p.getHeight();
}

float MultibandDisplay::dbOf (float py) const noexcept
{
    const auto p = plotArea();
    return -60.0f * juce::jlimit (0.0f, 1.0f, (py - p.getY()) / p.getHeight());
}

float MultibandDisplay::bandLeft (int b) const noexcept  { return b == 0 ? plotArea().getX() : xOf (x[b - 1]); }
float MultibandDisplay::bandRight (int b) const noexcept { return b == kB - 1 ? plotArea().getRight() : xOf (x[b]); }

int MultibandDisplay::crossoverAt (float px) const noexcept
{
    const float tol = 7.0f * (float) getHeight() / 200.0f;
    for (int i = 0; i < 3; ++i) if (std::abs (px - xOf (x[i])) < tol) return i;
    return -1;
}

int MultibandDisplay::bandAt (float px) const noexcept
{
    for (int b = 0; b < kB; ++b) if (px >= bandLeft (b) && px < bandRight (b)) return b;
    return px < plotArea().getX() ? 0 : kB - 1;
}

void MultibandDisplay::tick()
{
    using namespace onirique;
    bool changed = false;
    for (int i = 0; i < 3; ++i)
    {
        changed |= ui::approach (x[i], value (xP[i]), 0.3f);
        changed |= ui::approach (xGlow[i], (i == dragX || i == hoverX) ? 1.0f : 0.0f, 0.2f);
    }
    for (int b = 0; b < kB; ++b)
    {
        changed |= ui::approach (thr[b], value (thrP[b]), 0.3f);
        changed |= ui::approach (gain[b], value (gainP[b]), 0.3f);
        changed |= ui::approach (hoverGlow[b], (b == dragBand || (b == hoverBand && hoverX < 0)) ? 1.0f : 0.0f, 0.2f);
    }

    active = processor.apvts.getRawParameterValue (ids::mbOn)->load() > 0.5f
             && processor.apvts.getRawParameterValue (ids::bypass)->load() < 0.5f;
    const auto& m = processor.getMultibandMeter();
    for (int b = 0; b < kB; ++b)
    {
        const float tg = active ? m.grDb[(size_t) b].load (std::memory_order_relaxed) : 0.0f;
        const float tl = m.levelDb[(size_t) b].load (std::memory_order_relaxed);
        gr[b] += (tg > gr[b] ? 0.5f : 0.12f) * (tg - gr[b]);
        lvl[b] += (tl > lvl[b] ? 0.6f : 0.10f) * (tl - lvl[b]);
    }
    if (changed || active) repaint();
}

void MultibandDisplay::mouseMove (const juce::MouseEvent& e)
{
    hoverX = crossoverAt (e.position.x);
    hoverBand = bandAt (e.position.x);
    setMouseCursor (hoverX >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::UpDownResizeCursor);
}

void MultibandDisplay::mouseExit (const juce::MouseEvent&)
{
    if (dragX < 0 && dragBand < 0) { hoverX = -1; hoverBand = -1; }
}

void MultibandDisplay::mouseDown (const juce::MouseEvent& e)
{
    dragX = crossoverAt (e.position.x);
    if (dragX >= 0) { xP[dragX]->beginChangeGesture(); return; }
    dragBand = bandAt (e.position.x);
    thrP[dragBand]->beginChangeGesture();
    setValue (thrP[dragBand], juce::jlimit (-60.0f, 0.0f, dbOf (e.position.y)));
}

void MultibandDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (dragX >= 0)
    {
        // coupures toujours ordonnées, au moins 1/3 d'octave entre deux
        const auto range = xP[dragX]->getNormalisableRange();
        float lo = range.start, hi = range.end;
        if (dragX > 0) lo = std::max (lo, value (xP[dragX - 1]) * 1.26f);
        if (dragX < 2) hi = std::min (hi, value (xP[dragX + 1]) / 1.26f);
        setValue (xP[dragX], juce::jlimit (lo, std::max (lo, hi), hzOf (e.position.x)));
    }
    else if (dragBand >= 0)
        setValue (thrP[dragBand], juce::jlimit (-60.0f, 0.0f, dbOf (e.position.y)));
}

void MultibandDisplay::mouseUp (const juce::MouseEvent&)
{
    if (dragX >= 0) xP[dragX]->endChangeGesture();
    if (dragBand >= 0) thrP[dragBand]->endChangeGesture();
    dragX = dragBand = -1;
}

void MultibandDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    std::vector<juce::RangedAudioParameter*> reset;
    if (const int c = crossoverAt (e.position.x); c >= 0) reset.push_back (xP[c]);
    else { const int b = bandAt (e.position.x); reset.push_back (thrP[b]); reset.push_back (gainP[b]); }
    for (auto* p : reset)
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->getDefaultValue());
        p->endChangeGesture();
    }
}

void MultibandDisplay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    auto* p = gainP[bandAt (e.position.x)];
    p->beginChangeGesture();
    setValue (p, juce::jlimit (-12.0f, 12.0f, value (p) + (w.deltaY > 0.0f ? 0.5f : -0.5f)));
    p->endChangeGesture();
}

juce::String MultibandDisplay::getTooltip()
{
    return "Multiband: drag inside a band to set its threshold, drag a separator to move a crossover, "
           "mouse wheel = band gain, double-click = reset. Red curtain from the top = gain reduction.";
}

void MultibandDisplay::paint (juce::Graphics& g)
{
    static const char* names[kB] { "LOW", "LO-MID", "HI-MID", "HIGH" };
    const auto area = getLocalBounds().toFloat();
    const float u = area.getHeight() / 200.0f;
    const auto plot = plotArea();

    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.55f), 0.0f, area.getY(),
                                             ui::night.withAlpha (0.85f), 0.0f, area.getBottom(), false));
    g.fillRoundedRectangle (area, 12.0f * u);
    g.setColour (ui::line);
    g.drawRoundedRectangle (area.reduced (0.5f), 12.0f * u, std::max (1.0f, u));

    // ---- grille
    g.setFont (ui::font (ui::Face::mono, std::max (10.0f * u, 10.0f * ui::editorScale (*this))));
    for (auto [hz, txt] : { std::pair { 50.0f, "50" }, { 100.0f, "100" }, { 200.0f, "200" }, { 500.0f, "500" },
                            { 1000.0f, "1k" }, { 2000.0f, "2k" }, { 5000.0f, "5k" }, { 10000.0f, "10k" } })
    {
        const float px = xOf (hz);
        g.setColour (juce::Colours::white.withAlpha (0.04f));
        g.fillRect (juce::Rectangle<float> (px, plot.getY(), 1.0f, plot.getHeight()));
        g.setColour (ui::haze.withAlpha (0.7f));
        g.drawText (txt, juce::Rectangle<float> (40.0f * u, 14.0f * u).withCentre ({ px, plot.getBottom() + 8.0f * u }),
                    juce::Justification::centred, false);
    }
    for (int db = -10; db >= -50; db -= 10)
    {
        const float py = yOf ((float) db);
        g.setColour (juce::Colours::white.withAlpha (0.04f));
        g.fillRect (juce::Rectangle<float> (plot.getX(), py, plot.getWidth(), 1.0f));
        g.setColour (ui::haze.withAlpha (0.6f));
        g.drawText (juce::String (db), juce::Rectangle<float> (plot.getRight() + 4.0f * u, py - 6.0f * u, 26.0f * u, 12.0f * u),
                    juce::Justification::centredLeft, false);
    }

    const float a = active ? 1.0f : 0.45f;
    for (int b = 0; b < kB; ++b)
    {
        const float x0 = bandLeft (b), x1 = bandRight (b), bw = x1 - x0, cx = 0.5f * (x0 + x1);
        const float grN = juce::jlimit (0.0f, 1.0f, gr[b] / 18.0f);

        // la bande s'éclaire quand elle compresse
        g.setColour (ui::ember.withAlpha ((0.03f + 0.12f * grN + 0.05f * hoverGlow[b]) * a));
        g.fillRect (juce::Rectangle<float> (x0, plot.getY(), bw, plot.getHeight()));

        // niveau d'entrée de la bande (barre), partie au-dessus du seuil teintée braise
        const float barW = std::min (26.0f * u, bw * 0.34f);
        const float ly = yOf (lvl[b]), ty = yOf (thr[b]);
        const auto bar = juce::Rectangle<float> (cx - barW * 0.5f, ly, barW, plot.getBottom() - ly);
        if (bar.getHeight() > 0.5f)
        {
            g.setGradientFill (juce::ColourGradient (ui::bone.withAlpha (0.38f * a), 0.0f, ly, ui::bone.withAlpha (0.05f), 0.0f, plot.getBottom(), false));
            g.fillRoundedRectangle (bar, 3.0f * u);
            if (ly < ty)
            {
                g.setGradientFill (juce::ColourGradient (ui::ember.withAlpha (0.85f * a), 0.0f, ly, ui::ember.withAlpha (0.35f * a), 0.0f, ty, false));
                g.fillRoundedRectangle (bar.withBottom (ty), 3.0f * u);
            }
        }

        // rideau de réduction de gain depuis le haut
        if (gr[b] > 0.05f)
        {
            const auto curtain = juce::Rectangle<float> (x0 + 3.0f * u, plot.getY(), bw - 6.0f * u, grN * plot.getHeight() * 0.55f);
            g.setGradientFill (juce::ColourGradient (ui::bone.withAlpha (0.85f * a), 0.0f, curtain.getY(),
                                                     ui::ember.withAlpha (0.85f * a), 0.0f, curtain.getBottom(), false));
            g.fillRoundedRectangle (curtain, 4.0f * u);
            juce::Path edge;
            edge.startNewSubPath (curtain.getX(), curtain.getBottom());
            edge.lineTo (curtain.getRight(), curtain.getBottom());
            ui::strokeGlow (g, edge, ui::ember, 1.5f * u, 0.7f * a);
            g.setColour (ui::night);
            g.setFont (ui::font (ui::Face::mono, std::max (10.5f * u, 10.5f * ui::editorScale (*this)), true));
            if (curtain.getHeight() > 13.0f * u)
                g.drawText ("-" + juce::String (gr[b], 1), curtain.withTrimmedBottom (1.0f * u), juce::Justification::centredBottom, false);
        }

        // seuil : trait braise + poignée centrale + valeur
        juce::Path tl;
        tl.startNewSubPath (x0 + 4.0f * u, ty);
        tl.lineTo (x1 - 4.0f * u, ty);
        ui::strokeGlow (g, tl, ui::ember, 1.2f * u, (0.3f + 0.7f * hoverGlow[b]) * a);
        g.setColour (ui::ember.withAlpha ((0.7f + 0.3f * hoverGlow[b]) * a));
        g.strokePath (tl, juce::PathStrokeType (std::max (1.0f, 1.6f * u)));
        const float hr = (4.5f + 1.5f * hoverGlow[b]) * u;
        g.setColour (ui::night);
        g.fillEllipse (juce::Rectangle<float> (2.0f * hr, 2.0f * hr).withCentre ({ cx, ty }));
        g.setColour (ui::ember);
        g.drawEllipse (juce::Rectangle<float> (2.0f * hr, 2.0f * hr).withCentre ({ cx, ty }), std::max (1.0f, 1.5f * u));
        g.setColour (ui::mist.withAlpha (0.55f + 0.45f * hoverGlow[b]));
        g.setFont (ui::font (ui::Face::mono, std::max (9.5f * u, 9.5f * ui::editorScale (*this))));
        g.drawText (juce::String (thr[b], 1), juce::Rectangle<float> (bw - 8.0f * u, 12.0f * u).withPosition (x0 + 4.0f * u, ty + 3.0f * u),
                    juce::Justification::centred, false);

        // nom de bande + gain (si réglé)
        juce::String label (names[b]);
        if (std::abs (gain[b]) > 0.05f) label << (gain[b] > 0.0f ? "  +" : "  ") << juce::String (gain[b], 1);
        g.setColour (ui::haze.interpolatedWith (ui::mist, hoverGlow[b]));
        g.setFont (ui::fitFont (ui::Face::body, std::max (10.5f * u, 11.5f * ui::editorScale (*this)), bw - 6.0f * u, label, true, 0.1f));
        g.drawText (label, juce::Rectangle<float> (x0, plot.getBottom() - 16.0f * u, bw, 14.0f * u), juce::Justification::centred, false);
    }

    // séparations (fréquences de coupure) : glissables
    for (int i = 0; i < 3; ++i)
    {
        const float px = xOf (x[i]);
        juce::Path l;
        l.startNewSubPath (px, plot.getY());
        l.lineTo (px, plot.getBottom());
        ui::strokeGlow (g, l, ui::ember, 1.0f * u, 0.15f + 0.6f * xGlow[i]);
        g.setColour (ui::ember.withAlpha (0.45f + 0.5f * xGlow[i]));
        g.strokePath (l, juce::PathStrokeType (std::max (1.0f, (1.2f + 0.8f * xGlow[i]) * u)));
        const auto grip = juce::Rectangle<float> (8.0f * u, 18.0f * u).withCentre ({ px, plot.getCentreY() });
        g.setColour (ui::night);
        g.fillRoundedRectangle (grip, 4.0f * u);
        g.setColour (ui::ember);
        g.drawRoundedRectangle (grip, 4.0f * u, std::max (1.0f, 1.2f * u));
        if (xGlow[i] > 0.05f)
        {
            const auto txt = xP[i]->getCurrentValueAsText();
            const auto f = ui::font (ui::Face::mono, std::max (10.0f * u, 10.0f * ui::editorScale (*this)), true);
            const float tw = ui::textWidth (f, txt) + 12.0f * u;
            const auto box = juce::Rectangle<float> (tw, 16.0f * u).withCentre ({ px, plot.getY() + 12.0f * u });
            g.setColour (ui::night.withAlpha (0.9f * xGlow[i]));
            g.fillRoundedRectangle (box, 8.0f * u);
            g.setColour (ui::ember.withAlpha (xGlow[i]));
            g.drawRoundedRectangle (box, 8.0f * u, 1.0f);
            g.setColour (ui::bone.withAlpha (xGlow[i]));
            g.setFont (f);
            g.drawText (txt, box, juce::Justification::centred, false);
        }
    }
}

//==============================================================================
void MbPresetBox::tick()
{
    const auto n = apvts.state.getProperty ("mbPreset", "Init").toString();
    bool changed = n != shown;
    shown = n;
    changed |= ui::approach (glow, hover ? 1.0f : 0.0f, 0.2f);
    if (changed) repaint();
}

void MbPresetBox::mouseDown (const juce::MouseEvent&)
{
    juce::PopupMenu menu;
    menu.setLookAndFeel (&getLookAndFeel());
    menu.addSectionHeader ("MULTIBAND PRESETS");
    const auto& list = onirique::multibandPresets();
    for (int i = 0; i < (int) list.size(); ++i)
        menu.addItem (i + 1, list[(size_t) i].name, true, shown == list[(size_t) i].name);

    juce::Component::SafePointer<MbPresetBox> self (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (getWidth())
                            .withParentComponent (findParentComponentOfClass<juce::AudioProcessorEditor>()),
                        [self] (int r)
                        {
                            const auto& l = onirique::multibandPresets();
                            if (self != nullptr && r >= 1 && r <= (int) l.size())
                                onirique::applyMultibandPreset (self->apvts, l[(size_t) (r - 1)]);
                        });
}

void MbPresetBox::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const float h = r.getHeight(), u = h / 28.0f;
    juce::Path p;
    p.addRoundedRectangle (r, h * 0.5f);
    ui::strokeGlow (g, p, ui::ember, 1.0f * u, 0.5f * glow);
    g.setColour (juce::Colours::black.withAlpha (0.36f));
    g.fillPath (p);
    g.setColour (ui::line.interpolatedWith (ui::ember, glow));
    g.strokePath (p, juce::PathStrokeType (std::max (1.0f, 1.1f * u)));

    auto inner = r.reduced (h * 0.5f, 0.0f);
    g.setColour (ui::haze);
    g.setFont (ui::font (ui::Face::mono, h * 0.38f, false, 0.10f));
    const juce::String tag ("PRESET");
    const float tw = ui::textWidth (g.getCurrentFont(), tag) + 10.0f * u;
    g.drawText (tag, inner.removeFromLeft (tw), juce::Justification::centredLeft, false);

    // description du preset en cours, en clair
    juce::String desc;
    for (const auto& pr : onirique::multibandPresets()) if (shown == pr.name) desc = pr.description;

    const auto nf = ui::fitFont (ui::Face::body, h * 0.44f, inner.getWidth() * 0.45f, shown, true);
    const float nw = ui::textWidth (nf, shown) + 8.0f * u;
    g.setColour (ui::mist.interpolatedWith (ui::bone, glow));
    g.setFont (nf);
    g.drawText (shown, inner.removeFromLeft (nw), juce::Justification::centredLeft, false);
    inner.removeFromRight (16.0f * u);
    g.setColour (ui::haze.withAlpha (0.8f));
    g.setFont (ui::fitFont (ui::Face::body, h * 0.42f, inner.getWidth(), desc));
    g.drawText (desc, inner, juce::Justification::centredLeft, true);

    const float tx = r.getRight() - h * 0.55f, ty = r.getCentreY();
    juce::Path tri;
    tri.addTriangle (tx - 4.0f * u, ty - 2.0f * u, tx + 4.0f * u, ty - 2.0f * u, tx, ty + 3.0f * u);
    g.setColour (ui::haze.interpolatedWith (ui::ember, glow));
    g.fillPath (tri);
}

//==============================================================================
ProcessingOrderBar::ProcessingOrderBar (juce::AudioProcessorValueTreeState& s) : apvts (s)
{
    if (auto* p = apvts.getParameter (ids::chainOrder))
    {
        attachment = std::make_unique<juce::ParameterAttachment> (*p, [this] (float v)
        {
            order = decodeOrder (juce::roundToInt (v));
            repaint();
        });
        attachment->sendInitialUpdate();
    }
}

float ProcessingOrderBar::chipW() const noexcept
{
    return ((float) getWidth() - (float) (kN + 2) * gapW()) / (float) (kN + 3);   // + pastilles fixes NS, GATE (début) et FX (fin)
}

bool ProcessingOrderBar::moduleOn (int module) const
{
    static const char* powerIds[kN] { ids::peqPower, ids::resoOn, ids::compOn, ids::mbOn, ids::eqOn, ids::deessOn, ids::satOn };
    const bool on = apvts.getRawParameterValue (powerIds[module])->load() > 0.5f;
    // COMP et MULTI dépendent aussi de l'interrupteur général de la carte COMPRESSION
    return (module == 2 || module == 3) ? on && apvts.getRawParameterValue (ids::compMaster)->load() > 0.5f : on;
}

int ProcessingOrderBar::moduleAt (juce::Point<float> p) const noexcept
{
    if (p.y < 0.0f || p.y > (float) getHeight()) return -1;
    for (int m = 0; m < kN; ++m)
        if (p.x >= chipX[(size_t) m] && p.x <= chipX[(size_t) m] + chipW()) return m;
    return -1;
}

// ordre affiché pendant un glisser : la pastille tirée s'insère à la place sous son centre
std::array<int, ProcessingOrderBar::kN> ProcessingOrderBar::previewOrder() const noexcept
{
    if (dragModule < 0) return order;
    const float centre = dragX + chipW() * 0.5f;
    const int slot = juce::jlimit (0, kN - 1, (int) std::floor ((centre - slotX (0) + gapW() * 0.5f) / (chipW() + gapW())));
    std::vector<int> rest;
    for (int m : order) if (m != dragModule) rest.push_back (m);
    rest.insert (rest.begin() + slot, dragModule);
    std::array<int, kN> out {};
    std::copy (rest.begin(), rest.end(), out.begin());
    return out;
}

void ProcessingOrderBar::tick()
{
    const auto shown = previewOrder();
    bool changed = false;
    for (int s = 0; s < kN; ++s)
    {
        const int m = shown[(size_t) s];
        const float target = m == dragModule ? dragX : slotX (s);
        if (! placed) chipX[(size_t) m] = target;
        if (m == dragModule) { changed |= chipX[(size_t) m] != target; chipX[(size_t) m] = target; chipVel[(size_t) m] = 0.0f; }
        else changed |= ui::spring (chipX[(size_t) m], chipVel[(size_t) m], target, 0.14f, 0.68f);   // les voisines s'écartent avec un léger ressort
    }
    if (! placed) { placed = true; changed = true; }
    for (int m = 0; m < kN; ++m)
    {
        changed |= ui::approach (lit[(size_t) m], moduleOn (m) ? 1.0f : 0.0f, 0.15f);
        changed |= ui::approach (hoverGlow[(size_t) m], (m == hoverModule || m == dragModule) ? 1.0f : 0.0f, 0.2f);
    }
    changed |= ui::approach (gateLit, apvts.getRawParameterValue (ids::gateOn)->load() > 0.5f ? 1.0f : 0.0f, 0.15f);
    changed |= ui::approach (nsLit, apvts.getRawParameterValue (ids::nsAmount)->load() > 0.05f ? 1.0f : 0.0f, 0.15f);
    bool anyFx = false;
    for (auto* id : { ids::phoneOn, ids::doublerOn, ids::chorusOn, ids::flangerOn, ids::delayOn, ids::reverbOn }) anyFx |= apvts.getRawParameterValue (id)->load() > 0.5f;
    changed |= ui::approach (fxLit, anyFx ? 1.0f : 0.0f, 0.15f);
    if (changed) repaint();
}

void ProcessingOrderBar::mouseDown (const juce::MouseEvent& e)
{
    dragModule = moduleAt (e.position);
    if (dragModule < 0) return;
    dragOffset = e.position.x - chipX[(size_t) dragModule];
    dragX = chipX[(size_t) dragModule];
    dragMoved = false;
}

void ProcessingOrderBar::mouseDrag (const juce::MouseEvent& e)
{
    if (dragModule < 0) return;
    dragX = juce::jlimit (slotX (0), slotX (kN - 1), e.position.x - dragOffset);
    if (e.getDistanceFromDragStart() > 3) dragMoved = true;
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

void ProcessingOrderBar::mouseUp (const juce::MouseEvent&)
{
    if (dragModule < 0) return;
    const auto next = previewOrder();
    dragModule = -1;
    if (dragMoved && next != order && attachment != nullptr)
    {
        order = next;
        attachment->setValueAsCompleteGesture ((float) encodeOrder (next));
    }
    setMouseCursor (juce::MouseCursor::NormalCursor);
}

void ProcessingOrderBar::mouseMove (const juce::MouseEvent& e)
{
    const int h = moduleAt (e.position);
    if (h != hoverModule) { hoverModule = h; repaint(); }
    setMouseCursor (h >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
}

void ProcessingOrderBar::mouseExit (const juce::MouseEvent&) { hoverModule = -1; }

void ProcessingOrderBar::mouseDoubleClick (const juce::MouseEvent&)
{
    if (attachment != nullptr) attachment->setValueAsCompleteGesture (0.0f);   // ordre d'origine
}

juce::String ProcessingOrderBar::getTooltip()
{
    return "Processing order: the noise suppressor (NS) and the GATE always come first and the FX page last; drag a module left or right to move it in the chain (smooth 15 ms crossfade). "
           "Lit = module on. Double-click = original order.";
}

void ProcessingOrderBar::paint (juce::Graphics& g)
{
    const float h = (float) getHeight(), w = chipW(), u = h / 20.0f;

    // chevrons fixes entre les places
    for (int s = -2; s < kN; ++s)
    {
        const float cx = slotX (s) + w + gapW() * 0.5f, cy = h * 0.5f;
        juce::Path ch;
        ch.startNewSubPath (cx - 2.0f * u, cy - 4.0f * u);
        ch.lineTo (cx + 2.0f * u, cy);
        ch.lineTo (cx - 2.0f * u, cy + 4.0f * u);
        g.setColour (ui::haze.withAlpha (0.6f));
        g.strokePath (ch, juce::PathStrokeType (std::max (1.0f, 1.5f * u), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // pastilles fixes (cadre en pointillés = non déplaçables) : NS et GATE toujours en premier, FX toujours en dernier
    auto fixedChip = [&] (int slot, const char* name, float l)
    {
        const auto r = juce::Rectangle<float> (slotX (slot), 0.0f, w, h).reduced (0.5f, 1.0f);
        juce::Path p;
        p.addRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillPath (p);
        g.setColour (ui::ember.withAlpha (0.10f + 0.14f * l));
        g.fillPath (p);
        juce::Path dashed;
        const float dash[] { 2.5f * u, 2.0f * u };
        juce::PathStrokeType (std::max (1.0f, 1.0f * u)).createDashedStroke (dashed, p, dash, 2);
        g.setColour (ui::line.interpolatedWith (ui::ember, 0.35f * l));
        g.fillPath (dashed);
        g.setColour (ui::haze.interpolatedWith (ui::bone, 0.55f * l));
        g.setFont (ui::fitFont (ui::Face::mono, h * 0.56f, r.getWidth() - 6.0f * u, name, true, 0.02f));
        g.drawText (name, r, juce::Justification::centred, false);
    };
    fixedChip (-2, "NS", nsLit);
    fixedChip (-1, "GATE", gateLit);
    fixedChip (kN, "FX", fxLit);

    // pastilles (celle qu'on tire par-dessus)
    for (int pass = 0; pass < 2; ++pass)
        for (int m = 0; m < kN; ++m)
        {
            if ((pass == 1) != (m == dragModule)) continue;
            const auto r = juce::Rectangle<float> (chipX[(size_t) m], 0.0f, w, h).reduced (0.5f, 1.0f);
            const float l = lit[(size_t) m], hv = hoverGlow[(size_t) m];
            juce::Path p;
            p.addRoundedRectangle (r, r.getHeight() * 0.5f);
            ui::strokeGlow (g, p, ui::ember, 1.0f * u, 0.7f * hv);
            g.setColour (juce::Colours::black.withAlpha (0.4f));
            g.fillPath (p);
            g.setColour (ui::ember.withAlpha (0.10f + 0.14f * l + 0.12f * hv));
            g.fillPath (p);
            g.setColour (ui::line.interpolatedWith (ui::ember, 0.35f * l + 0.65f * hv));
            g.strokePath (p, juce::PathStrokeType (std::max (1.0f, 1.0f * u)));
            g.setColour (ui::haze.interpolatedWith (ui::bone, juce::jmax (0.55f * l, hv)));
            g.setFont (ui::fitFont (ui::Face::mono, h * 0.56f, r.getWidth() - 6.0f * u, orderName (m), true, 0.02f));
            g.drawText (orderName (m), r, juce::Justification::centred, false);
        }
}

//==============================================================================
void LevelMeter::tick()
{
    float peakNow = -100.0f;
    for (int c = 0; c < 2; ++c)
    {
        const float pk = consume ? consume (c) : 0.0f;
        const float db = pk > 1.0e-5f ? 20.0f * std::log10 (pk) : -100.0f;
        peakNow = std::max (peakNow, db);
        levelDb[c] = db > levelDb[c] ? db : std::max (-100.0f, levelDb[c] - 0.33f);   // retombée ~20 dB/s
        if (db >= holdDb[c]) { holdDb[c] = db; holdFrames[c] = 90; }                // maintien 1.5 s
        else if (--holdFrames[c] <= 0) holdDb[c] = std::max (levelDb[c], holdDb[c] - 0.5f);
    }
    if (peakNow >= maxDb) { maxDb = peakNow; maxFrames = 180; }                      // valeur affichée : 3 s
    else if (--maxFrames <= 0) maxDb = std::max (holdDb[0], holdDb[1]);
    repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    const float u = area.getWidth() / 46.0f;
    auto r = area;
    const auto readout = r.removeFromBottom (18.0f * u);
    r.removeFromBottom (4.0f * u);
    const auto scale = r.removeFromRight (18.0f * u);
    const float barW = (r.getWidth() - 3.0f * u) * 0.5f;
    auto yOf = [&] (float db) { return r.getBottom() - norm (db) * r.getHeight(); };

    // graduations
    g.setFont (ui::font (ui::Face::mono, std::max (8.0f * u, 9.0f * ui::editorScale (*this))));
    for (int db : { 0, -6, -12, -24, -48 })
    {
        const float y = yOf ((float) db);
        g.setColour (db == 0 ? ui::ember.withAlpha (0.8f) : ui::haze.withAlpha (0.75f));
        g.fillRect (juce::Rectangle<float> (scale.getX() + 1.0f * u, y, 3.0f * u, 1.0f));
        if (db != -6)   // -6 : simple repère, sinon les chiffres du haut se chevauchent
            g.drawText (juce::String (db), juce::Rectangle<float> (scale.getX() + 4.0f * u, y - 6.0f * u, scale.getWidth() - 4.0f * u, 12.0f * u),
                    juce::Justification::centredLeft, false);
    }

    for (int c = 0; c < 2; ++c)
    {
        const auto bar = juce::Rectangle<float> (r.getX() + (float) c * (barW + 3.0f * u), r.getY(), barW, r.getHeight());
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (bar, 2.5f * u);
        g.setColour (ui::line);
        g.drawRoundedRectangle (bar.reduced (0.5f), 2.5f * u, 1.0f);

        // remplissage : os -> braise près de 0 dBFS -> rouge au-delà
        const float top = yOf (levelDb[c]);
        if (top < bar.getBottom() - 0.5f)
        {
            juce::ColourGradient cg (ui::bone.withAlpha (0.30f), 0.0f, bar.getBottom(), juce::Colour (0xffff2a3a), 0.0f, yOf (2.0f), false);
            cg.addColour ((double) norm (-12.0f), ui::bone.withAlpha (0.85f));   // positions = niveau normalisé (bas = -60, haut = +2)
            cg.addColour ((double) norm (-3.0f), ui::ember);
            g.setGradientFill (cg);
            g.fillRoundedRectangle (bar.withTop (top).reduced (1.0f, 0.0f), 2.0f * u);
        }
        // maintien de crête
        if (holdDb[c] > -60.0f)
        {
            g.setColour (holdDb[c] > 0.0f ? juce::Colour (0xffff2a3a) : ui::bone);
            g.fillRect (juce::Rectangle<float> (bar.getX() + 1.0f, yOf (holdDb[c]) - 1.0f * u, bar.getWidth() - 2.0f, 2.0f * u));
        }
    }

    // valeur crête (3 s) en clair
    const juce::String txt = maxDb <= -99.0f ? juce::String ("-inf") : (maxDb > 0.0f ? "+" : "") + juce::String (maxDb, 1);
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillRoundedRectangle (readout, readout.getHeight() * 0.5f);
    g.setColour (maxDb > 0.0f ? juce::Colour (0xffff2a3a) : ui::line);
    g.drawRoundedRectangle (readout.reduced (0.5f), readout.getHeight() * 0.5f, 1.0f);
    g.setColour (maxDb > 0.0f ? ui::bone : ui::mist);
    g.setFont (ui::fitFont (ui::Face::mono, readout.getHeight() * 0.62f, readout.getWidth() - 6.0f * u, txt, true));
    g.drawText (txt, readout, juce::Justification::centred, false);
}

//==============================================================================
void GainReductionBar::tick()
{
    const float target = active && read ? juce::jlimit (0.0f, range + 6.0f, read()) : 0.0f;
    const float before = gr, beforeHold = hold;
    gr = target > gr ? target : gr + 0.12f * (target - gr);              // attaque immédiate, retour ~150 ms
    if (gr >= hold) { hold = gr; holdFrames = 120; }                       // valeur affichée : max des 2 dernières s
    else if (--holdFrames <= 0) hold = std::max (gr, hold - 0.1f);
    if (std::abs (gr - before) > 1.0e-3f || std::abs (hold - beforeHold) > 1.0e-3f) repaint();
}

void GainReductionBar::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    if (area.getWidth() > 2.0f * area.getHeight())
    {
        // à plat (carte PARALLEL) : « GR » et réduction max récente sur la première ligne, puis la barre qui s'allonge
        // depuis 0 dB vers la droite, graduée dessous
        const float u = area.getHeight() / 54.0f, es = ui::editorScale (*this);
        auto r = area;
        auto row = r.removeFromTop (20.0f * u);
        const auto readout = row.removeFromRight (std::min (72.0f * u, row.getWidth() * 0.5f));
        const auto title = row;
        r.removeFromTop (5.0f * u);
        const auto bar = r.removeFromTop (12.0f * u);
        const auto scale = r;
        auto xOf = [&] (float db) { return bar.getX() + juce::jlimit (0.0f, 1.0f, db / range) * bar.getWidth(); };

        g.setColour (ui::haze);
        g.setFont (ui::font (ui::Face::body, std::max (13.0f * u, 10.5f * es), true, 0.12f));
        g.drawText ("GR", title, juce::Justification::centredLeft, false);

        g.setFont (ui::font (ui::Face::mono, std::max (10.0f * u, 9.0f * es)));
        for (int db : { 0, juce::roundToInt (range / 3.0f), juce::roundToInt (range * 2.0f / 3.0f), juce::roundToInt (range) })
        {
            const float x = xOf ((float) db);
            g.setColour (ui::haze.withAlpha (0.75f));
            g.fillRect (juce::Rectangle<float> (x, scale.getY() + 1.0f * u, 1.0f, 3.0f * u));
            const auto t = db == 0 ? juce::String ("0") : "-" + juce::String (db);
            // 0 calé à gauche de la barre, le dernier calé à droite : rien ne dépasse de la carte
            const bool first = db == 0, last = db == juce::roundToInt (range);
            const float lx = first ? x : (last ? x - 40.0f * u : x - 20.0f * u);
            g.drawText (t, juce::Rectangle<float> (lx, scale.getY() + 4.0f * u, 40.0f * u, scale.getHeight() - 4.0f * u),
                        first ? juce::Justification::centredLeft : last ? juce::Justification::centredRight : juce::Justification::centred, false);
        }

        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (bar, 3.0f * u);
        g.setColour (ui::line);
        g.drawRoundedRectangle (bar.reduced (0.5f), 3.0f * u, 1.0f);
        if (gr > 0.02f)
        {
            const auto fill = bar.withRight (xOf (gr)).reduced (1.0f, 1.5f * u);
            juce::Path p;
            p.addRoundedRectangle (fill, 2.0f * u);
            ui::strokeGlow (g, p, ui::ember, 1.0f * u, 0.5f);
            g.setGradientFill (juce::ColourGradient (ui::bone, bar.getX(), 0.0f, ui::ember, bar.getX() + bar.getWidth() * 0.5f, 0.0f, false));
            g.fillPath (p);
        }

        const juce::String txt = hold < 0.05f ? juce::String ("0.0") : "-" + juce::String (hold, 1);
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillRoundedRectangle (readout, readout.getHeight() * 0.5f);
        g.setColour (hold > 0.05f ? ui::ember.withAlpha (0.8f) : ui::line);
        g.drawRoundedRectangle (readout.reduced (0.5f), readout.getHeight() * 0.5f, 1.0f);
        g.setColour (ui::mist.withAlpha (active ? 1.0f : 0.5f));
        g.setFont (ui::fitFont (ui::Face::mono, readout.getHeight() * 0.58f, readout.getWidth() - 8.0f * u, txt, true));
        g.drawText (txt, readout, juce::Justification::centred, false);
        return;
    }
    const float u = area.getWidth() / 46.0f;
    const float es = ui::editorScale (*this);
    auto r = area;
    const auto title = r.removeFromTop (16.0f * u);
    const auto readout = r.removeFromBottom (18.0f * u);
    r.removeFromBottom (4.0f * u);
    r.removeFromTop (2.0f * u);
    const auto scale = r.removeFromRight (18.0f * u);
    const auto bar = r.reduced (2.0f * u, 0.0f);
    // échelle : 0 .. -range dB (18 pour le de-esser, plus pour le bus de compression parallèle)
    auto yOf = [&] (float db) { return bar.getY() + juce::jlimit (0.0f, 1.0f, db / range) * bar.getHeight(); };

    g.setColour (ui::haze);
    g.setFont (ui::font (ui::Face::body, std::max (11.0f * u, 10.5f * es), true, 0.12f));
    g.drawText ("GR", title, juce::Justification::centred, false);

    g.setFont (ui::font (ui::Face::mono, std::max (8.0f * u, 9.0f * es)));
    for (int db : { 0, juce::roundToInt (range / 3.0f), juce::roundToInt (range * 2.0f / 3.0f), juce::roundToInt (range) })
    {
        const float y = yOf ((float) db);
        g.setColour (ui::haze.withAlpha (0.75f));
        g.fillRect (juce::Rectangle<float> (scale.getX() + 1.0f * u, y, 3.0f * u, 1.0f));
        g.drawText (db == 0 ? juce::String ("0") : "-" + juce::String (db),
                    juce::Rectangle<float> (scale.getX() + 4.0f * u, y - 6.0f * u, scale.getWidth() - 3.0f * u, 12.0f * u),
                    juce::Justification::centredLeft, false);
    }

    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (bar, 3.0f * u);
    g.setColour (ui::line);
    g.drawRoundedRectangle (bar.reduced (0.5f), 3.0f * u, 1.0f);
    if (gr > 0.02f)
    {
        const auto fill = bar.withBottom (yOf (gr)).reduced (1.5f * u, 1.0f);
        juce::Path p;
        p.addRoundedRectangle (fill, 2.0f * u);
        ui::strokeGlow (g, p, ui::ember, 1.0f * u, 0.5f);
        g.setGradientFill (juce::ColourGradient (ui::bone, 0.0f, bar.getY(), ui::ember, 0.0f, bar.getY() + bar.getHeight() * 0.5f, false));
        g.fillPath (p);
    }

    const juce::String txt = hold < 0.05f ? juce::String ("0.0") : "-" + juce::String (hold, 1);
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillRoundedRectangle (readout, readout.getHeight() * 0.5f);
    g.setColour (hold > 0.05f ? ui::ember.withAlpha (0.8f) : ui::line);
    g.drawRoundedRectangle (readout.reduced (0.5f), readout.getHeight() * 0.5f, 1.0f);
    g.setColour (ui::mist.withAlpha (active ? 1.0f : 0.5f));
    g.setFont (ui::fitFont (ui::Face::mono, readout.getHeight() * 0.62f, readout.getWidth() - 6.0f * u, txt, true));
    g.drawText (txt, readout, juce::Justification::centred, false);
}

//==============================================================================
OniriqueVocalBoxAudioProcessorEditor::OniriqueVocalBoxAudioProcessorEditor (OniriqueVocalBoxAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    tooltipWindow.setLookAndFeel (&lnf);
    setLookAndFeel (&lnf);   // menus (presets) et champs de texte dans le style du plugin
    setOpaque (true);

    for (auto* k : { &lowKnob, &midKnob, &highKnob, &airKnob, &freqKnob, &amtKnob, &driveKnob, &inKnob, &outKnob,
                     &resoAmtKnob, &resoLowFader, &resoMidFader, &resoHighFader,
                     &fetInKnob, &fetOutKnob, &fetAtkKnob, &fetRelKnob, &laGainKnob, &laPeakKnob,
                     &mbThr1, &mbThr2, &mbThr3, &mbThr4, &mbAttackKnob, &mbReleaseKnob, &mbOutKnob })
    {
        addAndMakeVisible (k);
        animated.push_back (k);
    }
    for (auto* c : std::initializer_list<juce::Component*> { &lowFreq, &lowCutButton, &satType, &resoDisplay, &resoLowHz, &resoMidHz, &resoHighHz,
                                                             &compMode, &compOrder, &fetRatio, &laMode, &vu1176, &vuLA2A,
                                                             &compTabs, &mbDisplay, &mbPresets, &mbGain1, &mbGain2, &mbGain3, &mbGain4 })
        addAndMakeVisible (c);
    for (auto* a : std::initializer_list<Animated*> { &lowFreq, &lowCutButton, &satType, &resoDisplay, &resoLowHz, &resoMidHz, &resoHighHz,
                                                      &compMode, &compOrder, &fetRatio, &laMode, &vu1176, &vuLA2A,
                                                      &compTabs, &mbDisplay, &mbPresets, &mbGain1, &mbGain2, &mbGain3, &mbGain4,
                                                      &autoButton, &bypassButton })
        animated.push_back (a);

    // curseurs et fréquences de bande : la zone correspondante s'illumine dans l'afficheur
    KnobComponent* faders[] { &resoLowFader, &resoMidFader, &resoHighFader };
    DragValue* hz[] { &resoLowHz, &resoMidHz, &resoHighHz };
    for (int b = 0; b < 3; ++b)
    {
        faders[b]->onFocusChange = [this, b] (bool f) { resoDisplay.setFocusBand (b, f); };
        hz[b]->onFocusChange     = [this, b] (bool f) { resoDisplay.setFocusBand (b, f); };
    }

    // boutons power : un par module, actifs par défaut
    struct PowerDef { PowerButton& b; std::unique_ptr<ButtonAttachment>& att; const char* id; const char* name; };
    for (auto& d : { PowerDef { eqPower, eqPowerAtt, ids::eqOn, "EQ ANALOG (colour)" },
                     PowerDef { peqPower, peqPowerAtt, ids::peqPower, "Parametric EQ" },
                     PowerDef { gatePower, gatePowerAtt, ids::gateOn, "Noise gate" },
                     PowerDef { resoPower, resoPowerAtt, ids::resoOn, "Resonance" },
                     PowerDef { cardPower, cardPowerAtt, ids::compMaster, "whole COMPRESSION section (1176 / LA-2A and MULTIBAND)" },
                     PowerDef { compPower, compPowerAtt, ids::compOn, "1176 / LA-2A" },
                     PowerDef { mbPower, mbPowerAtt, ids::mbOn, "Multiband" },
                     PowerDef { parPower, parPowerAtt, ids::parOn, "parallel compression" },
                     PowerDef { deessPower, deessPowerAtt, ids::deessOn, "De-esser" },
                     PowerDef { satPower, satPowerAtt, ids::satOn, "Saturation Lab" } })
    {
        addAndMakeVisible (d.b);
        animated.push_back (&d.b);
        d.b.setTooltip (juce::String ("Turn the ") + d.name + " on/off (click-free, latency-compensated).");
        d.att = std::make_unique<ButtonAttachment> (processor.apvts, d.id, d.b);
    }

    addAndMakeVisible (autoButton);
    addAndMakeVisible (bypassButton);
    autoButton.setTooltip ("Auto Gain: keeps the perceived loudness the same before and after the processing.");
    bypassButton.setTooltip ("Bypass the whole plugin (latency-compensated, click-free).");
    autoAttachment   = std::make_unique<ButtonAttachment> (processor.apvts, ids::autoGain, autoButton);
    bypassAttachment = std::make_unique<ButtonAttachment> (processor.apvts, ids::bypass, bypassButton);
    lowCutButton.setTooltip ("Low cut: 85 Hz high-pass, 18 dB/oct. Removes rumble, plosives and handling noise.");
    lowCutAttachment = std::make_unique<ButtonAttachment> (processor.apvts, ids::lowCut, lowCutButton);

    logo = juce::ImageCache::getFromMemory (BinaryData::logo_png, BinaryData::logo_pngSize);

    // carte COMPRESSION : onglets 1176 / LA-2A et MULTIBAND (l'onglet affiché revient avec le projet)
    for (auto* gv : { &mbGain1, &mbGain2, &mbGain3, &mbGain4 }) gv->setPrefix ("GAIN ");
    compTabs.onChange = [this] (int t) { showCompTab (t); };
    compTabs.setSelected (juce::jlimit (0, 1, (int) processor.apvts.state.getProperty ("compTab", 0)));

    // EQ paramétrique : réglages précis de chaque bande (seule la bande sélectionnée est affichée)
    for (int b = 0; b < 8; ++b)
    {
        const juce::String n = "Band " + juce::String (b + 1);
        peqFreqV[(size_t) b] = std::make_unique<DragValue> (processor.apvts, ids::peqFreq[b], n + " frequency: drag up/down (Shift = fine), double-click = reset.");
        peqGainV[(size_t) b] = std::make_unique<DragValue> (processor.apvts, ids::peqGain[b], n + " gain (bell / shelves): drag up/down, double-click = 0 dB.");
        peqQV[(size_t) b]    = std::make_unique<DragValue> (processor.apvts, ids::peqQ[b],    n + " Q (width / resonance): drag up/down or mouse wheel on the dot.");
        peqThrV[(size_t) b]  = std::make_unique<DragValue> (processor.apvts, ids::peqThr[b],
                                                            n + " dynamic threshold: the band only acts when its zone goes above this level "
                                                            "(full gain 9 dB above). Drag up/down, double-click = reset.");
        peqFreqV[(size_t) b]->setPrefix ("FREQ ");
        peqGainV[(size_t) b]->setPrefix ("GAIN ");
        peqQV[(size_t) b]->setPrefix ("Q ");
        peqThrV[(size_t) b]->setPrefix ("THR ");
        peqSlopeV[(size_t) b] = std::make_unique<DragValue> (processor.apvts, ids::peqSlope[b],
                                                             n + " slope (low cut / high cut): 12, 24, 36 or 48 dB per octave. Drag up/down.");
        for (auto* v : { peqFreqV[(size_t) b].get(), peqGainV[(size_t) b].get(), peqQV[(size_t) b].get(), peqThrV[(size_t) b].get(), peqSlopeV[(size_t) b].get() })
        {
            addChildComponent (v);
            animated.push_back (v);
        }
    }
    addAndMakeVisible (peqChip);
    animated.push_back (&peqChip);
    analyzer.onSelect = [this] (int b) { selectPeqBand (b); };
    selectPeqBand (analyzer.getSelected());
    addAndMakeVisible (fullButton);
    animated.push_back (&fullButton);
    fullButton.setTooltip ("Open the parametric EQ full screen, for precise editing of all 8 bands.");
    fullButton.onClick = [this] { showEqFull (true); };

    // NOISE SUPPRESSOR : curseur vertical + réduction appliquée
    addAndMakeVisible (nsFader);
    addAndMakeVisible (nsGr);
    animated.push_back (&nsFader);
    animated.push_back (&nsGr);
    nsFader.activity = [this] { return processor.getSuppressorReductionDb(); };

    // NOISE GATE : écran (seuil à glisser, LEARN), 5 boutons, hystérésis
    for (auto* k : { &gateThrKnob, &gateRangeKnob, &gateAtkKnob, &gateHoldKnob, &gateRelKnob })
    {
        addAndMakeVisible (k);
        animated.push_back (k);
    }
    for (auto* c : std::initializer_list<juce::Component*> { &gateDisplay, &gateHystV })
        addAndMakeVisible (c);
    animated.push_back (&gateDisplay);
    animated.push_back (&gateHystV);
    gateHystV.setPrefix ("HYST ");

    // FX (en-tête) : ouvre la page des effets ; écoute de la bande de l'EQ (casque)
    addAndMakeVisible (fxButton);
    animated.push_back (&fxButton);
    fxButton.setTooltip ("FX: chorus, flanger, delay and reverb for the voice, at the end of the chain (full page).");
    fxButton.onClick = [this] { showFx (! fxView.isShown()); };
    addAndMakeVisible (peqListenButton);
    animated.push_back (&peqListenButton);
    peqListenButton.setTooltip ("Band listen: when lit, click and hold a band's dot to hear only that band (the frequencies it acts on); release it to hear the whole voice again.");
    peqListenAtt = std::make_unique<ButtonAttachment> (processor.apvts, ids::peqListen, peqListenButton);

    // suréchantillonnage, plosives, écoute RESONANCE
    for (auto* c : std::initializer_list<juce::Component*> { &osSel, &plosiveFader, &listenButton })
        addAndMakeVisible (c);
    for (auto* a : std::initializer_list<Animated*> { &osSel, &plosiveFader, &listenButton })
        animated.push_back (a);
    osSel.setTitle ("OS", "Oversampling of the whole plugin (MIX mode only).");
    plosiveFader.activity = [this] { return processor.getPlosiveReductionDb(); };
    listenButton.setTooltip ("Listen: hear only what the resonance suppressor removes (to check it never eats the voice).");
    listenAttachment = std::make_unique<ButtonAttachment> (processor.apvts, ids::resoListen, listenButton);

    // ordre de traitement (en-tête) + écoute par bande du multibande
    addAndMakeVisible (orderBar);
    addAndMakeVisible (deessGr);
    addAndMakeVisible (parAmountKnob);
    addAndMakeVisible (parDeessKnob);
    addAndMakeVisible (parDeessFreqV);
    animated.push_back (&parDeessKnob);
    animated.push_back (&parDeessFreqV);
    parDeessFreqV.setPrefix ("FREQ ");
    parDeessKnob.activity = [this] { return processor.getParallelDeEssDb(); };
    addAndMakeVisible (parGr);
    animated.push_back (&parAmountKnob);
    animated.push_back (&parGr);
    animated.push_back (&deessGr);

    // second de-esser (+) et réglages manuels de la compression parallèle : affichés selon leur bouton
    for (auto* c : std::initializer_list<juce::Component*> { &deess2Button, &parAutoButton })
        addAndMakeVisible (c);
    for (auto* c : std::initializer_list<juce::Component*> { &deess2FreqKnob, &deess2AmtKnob, &deess2Gr, &parThreshKnob, &parAttackKnob, &parReleaseKnob })
        addChildComponent (c);
    for (auto* a : std::initializer_list<Animated*> { &deess2Button, &parAutoButton, &deess2FreqKnob, &deess2AmtKnob, &deess2Gr,
                                                      &parThreshKnob, &parAttackKnob, &parReleaseKnob })
        animated.push_back (a);
    deess2Button.setTooltip ("Add a second de-esser with its own frequency and intensity (click again to remove it).");
    parAutoButton.setTooltip ("AUTO: the factory setting (1176 ALL, fast attack and release, ~-37 dB threshold). "
                              "Turn it off to set the threshold, attack and release yourself.");
    deess2Attachment = std::make_unique<ButtonAttachment> (processor.apvts, ids::deess2On, deess2Button);
    parAutoAttachment = std::make_unique<ButtonAttachment> (processor.apvts, ids::parAuto, parAutoButton);
    addAndMakeVisible (inMeter);
    addAndMakeVisible (outMeter);
    animated.push_back (&inMeter);
    animated.push_back (&outMeter);
    animated.push_back (&orderBar);
    {
        PillToggle* lb[] { &mbListen1, &mbListen2, &mbListen3, &mbListen4 };
        static const char* bandNames[] { "LOW", "LO-MID", "HI-MID", "HIGH" };
        for (int b = 0; b < 4; ++b)
        {
            addAndMakeVisible (lb[b]);
            animated.push_back (lb[b]);
            lb[b]->setTooltip (juce::String ("Listen to the ") + bandNames[b] + " band only (several bands can be listened to together).");
            mbListenAtt[b] = std::make_unique<ButtonAttachment> (processor.apvts, ids::mbSolo[b], *lb[b]);
        }
    }

    addAndMakeVisible (analyzer);
    addAndMakeVisible (presetBar);
    animated.push_back (&analyzer);
    animated.push_back (&presetBar);

    // redimensionnable, proportions verrouillées : aucune déformation, aucun texte coupé.
    // La taille mémorisée est lue AVANT tout appel qui redimensionne : setResizeLimits() déclenche un
    // resized() à la taille minimale, qui écrasait la largeur sauvegardée (le plugin se rouvrait petit).
    // taille d'ouverture : la règle commune des plugins ONIRIQUE (OniriqueWindowSize.h)
    const int w = onirique::window::openingSize (processor.apvts.state, "ONIRIQUE VOCALBOX",
                                                 { 1900, juce::roundToInt (1900.0f * kBaseH / kBaseW) },
                                                 { kMinW, juce::roundToInt ((float) kMinW * kBaseH / kBaseW) },
                                                 { kMaxW, juce::roundToInt ((float) kMaxW * kBaseH / kBaseW) }).x;
    setResizable (true, true);
    setResizeLimits (kMinW, juce::roundToInt (kMinW * kBaseH / kBaseW), kMaxW, juce::roundToInt (kMaxW * kBaseH / kBaseW));
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) (kBaseW / kBaseH));
    setSize (w, juce::roundToInt ((float) w * kBaseH / kBaseW));
    restoringSize = false;
    veils.draw = [this] (juce::Graphics& g) { paintVeils (g); };
    addAndMakeVisible (veils);   // ajoutée en dernier : au-dessus des modules, sous les menus ouverts ensuite

    // EQ plein écran et page FX, au-dessus des voiles (la licence reste par-dessus tout)
    addChildComponent (eqFull);
    eqFull.onClose = [this] { showEqFull (false); };
    eqFull.onSelect = [this] (int b) { analyzer.setSelectedSilently (b); selectPeqBand (b); };
    addChildComponent (fxView);
    fxView.onClose = [this] { showFx (false); };
    showCompTab (compTabs.getSelected());
    onirique::window::remember (processor.apvts.state, getWidth(), getHeight());

    // Licence : pastille et page d'activation, par-dessus tout le reste.
    licenseOverlay = std::make_unique<onirique::license::Overlay> ("ONIRIQUE VOCALBOX", processor.license);
    addAndMakeVisible (*licenseOverlay);
    resized();

   #if ONIRIQUE_UI_TEST
    // essais locaux uniquement (jamais dans les versions publiées) : ONIRIQUE_UI_TEST=eqfull ou fx
    {
        const auto t = juce::SystemStats::getEnvironmentVariable ("ONIRIQUE_UI_TEST", {});
        if (t.startsWith ("eqfull")) showEqFull (true);
        if (t == "eqfullpanel") eqFull.showBandPanel (0);
        if (t == "fx") showFx (true);
    }
   #endif

    startTimerHz (60);
}

OniriqueVocalBoxAudioProcessorEditor::~OniriqueVocalBoxAudioProcessorEditor()
{
    stopTimer();
    onirique::window::rememberOnThisComputer ("ONIRIQUE VOCALBOX", getWidth(), getHeight());
    tooltipWindow.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void OniriqueVocalBoxAudioProcessorEditor::timerCallback()
{
    clock += 1.0 / 60.0;
    ++frame;
   #if ONIRIQUE_UI_TEST
    // essai local : ONIRIQUE_UI_TEST=cycle ouvre et ferme l'EQ plein écran puis la page FX en boucle (mesure de fluidité)
    if (frame % 45 == 0 && juce::SystemStats::getEnvironmentVariable ("ONIRIQUE_UI_TEST", {}) == "cycle")
    {
        const int step = (frame / 45) % 4;
        if (step == 0) showEqFull (true); else if (step == 1) showEqFull (false); else if (step == 2) showFx (true); else showFx (false);
    }
    // essai local : ONIRIQUE_UI_PNG=fichier.png rend toute l'interface (x2) par le moteur logiciel de JUCE, pour les aperçus
    if (frame == 90)
        if (const auto png = juce::SystemStats::getEnvironmentVariable ("ONIRIQUE_UI_PNG", {}); png.isNotEmpty())
        {
            juce::Image img (juce::Image::ARGB, getWidth() * 2, getHeight() * 2, true, juce::SoftwareImageType());
            {
                juce::Graphics g2 (img);
                g2.addTransform (juce::AffineTransform::scale (2.0f));
                paintEntireComponent (g2, true);
            }
            juce::File f (png);
            f.deleteFile();
            if (auto os = f.createOutputStream()) juce::PNGImageFormat().writeImageToStream (img, *os);
        }
   #endif
    {
        bool anyFx = false;
        for (auto* id : { ids::phoneOn, ids::doublerOn, ids::chorusOn, ids::flangerOn, ids::delayOn, ids::reverbOn }) anyFx |= processor.apvts.getRawParameterValue (id)->load() > 0.5f;
        fxButton.setLit (anyFx);   // le bouton FX s'allume quand un effet tourne
    }

    // page plein écran ouverte (EQ, FX) : elle couvre les modules, elle seule s'anime (chaque image des modules,
    // dessous, la forçait à se redessiner). Le bouton FX de l'en-tête reste vivant. Pendant la transition d'ouverture
    // ou de fermeture, un calque opaque montre des photos figées des modules : eux non plus ne s'animent pas.
    if (eqFull.isVisible() || fxView.isVisible() || eqFull.isAnimating() || fxView.isAnimating())
    {
        eqFull.tick();
        fxView.tick();
        fxButton.tick();
        orderBar.tick();   // l'en-tête reste vivant
        return;
    }
    for (auto* a : animated) a->tick();

    // afficheur RESONANCE à 30 images/s
    if ((frame & 1) == 0)
        resoDisplay.updateMeter (processor.getResonanceMeter(), resoPower.getToggleState());

    // voile des modules coupés (fondu), aussi quand l'automation les change ; sous-panneaux 1176 / LA-2A
    // voilés selon le mode de compression (solo de l'autre)
    const int cm = juce::roundToInt (processor.apvts.getRawParameterValue (ids::compMode)->load());
    for (auto& s : sections)
    {
        float target = -1.0f;
        if (s.power != nullptr) target = s.power->getToggleState() ? 0.0f : 1.0f;
        else if (s.plain)       target = (s.title == "MBAREA" ? mbPower : compPower).getToggleState() ? 0.0f : 1.0f;   // sous-module coupé
        else if (s.sub)         target = (s.title == "1176" ? cm == 1 : cm == 0) ? 1.0f : 0.0f;
        if (target >= 0.0f && ui::approach (s.veil, target, 0.14f))
            repaint (s.bounds.getSmallestIntegerContainer());
    }
    vu1176.setActive (compPower.getToggleState() && cm != 1);
    deessGr.setActive (deessPower.getToggleState() && processor.apvts.getRawParameterValue (ids::bypass)->load() < 0.5f);
    deess2Gr.setActive (deessPower.getToggleState() && processor.apvts.getRawParameterValue (ids::bypass)->load() < 0.5f);
    parGr.setActive (parPower.getToggleState() && processor.apvts.getRawParameterValue (ids::bypass)->load() < 0.5f);

    // second de-esser ajouté / retiré, AUTO coché / décoché (aussi par l'automation) : la carte se réorganise
    if (deess2Button.getToggleState() != deess2Shown || parAutoButton.getToggleState() == parManualShown)
        resized();
    // bande sélectionnée passée en dynamique (menu, automation) : son seuil apparaît dans la barre de l'EQ
    {
        const int b = analyzer.getSelected();
        const int type = juce::roundToInt (processor.apvts.getRawParameterValue (ids::peqType[b])->load());
        const bool dyn = processor.apvts.getRawParameterValue (ids::peqDyn[b])->load() > 0.5f && ParametricEQ::canBeDynamic (type);
        if (dyn != peqDynShown || ParametricEQ::isCut (type) != peqCutShown) selectPeqBand (b);
    }
    vuLA2A.setActive (compPower.getToggleState() && cm != 0);

    // témoin OVER : sortie au-dessus de 0 dBFS, maintenu ~1.5 s, s'éteint en fondu
    if (processor.consumeOutputPeak() > 1.0f) overHold = 90;
    else if (overHold > 0) --overHold;
    if (ui::approach (overGlow, overHold > 0 ? 1.0f : 0.0f, overHold > 0 ? 0.5f : 0.08f))
        repaint (overRect.getSmallestIntegerContainer().expanded (8));

    // correction d'Auto Gain affichée sous ON
    juce::String cap;
    if (autoButton.getToggleState())
    {
        const float db = processor.getAutoGainDb();
        cap = juce::String (std::abs (db) < 0.05f ? 0.0f : db, 1);
        if (db > 0.05f) cap = "+" + cap;
        cap << " dB";
    }
    autoButton.setCaption (cap);

    // en-tête vivant : lueur du logo, signal qui parcourt la ligne, étoiles qui scintillent
    // (lueur du logo à 30 images/s : elle respire en 5 s ; le signal : seulement la zone de l'impulsion, pas toute la largeur)
    if ((frame & 1) == 0)
        repaint (logoRect.expanded (logoRect.getHeight() * 0.4f).getSmallestIntegerContainer());
    {
        const float u = scale(), W = (float) getWidth(), len = 130.0f * u, span = W - 44.0f * u + len;
        const float px = 22.0f * u - len + std::fmod ((float) clock * 0.24f, 1.0f) * span;
        const auto streak = juce::Rectangle<float> (px, signalRect.getY(), len, signalRect.getHeight()).expanded (6.0f * u, 0.0f);
        repaint (streak.getUnion (lastStreak).getIntersection (signalRect).getSmallestIntegerContainer());
        lastStreak = streak;
    }
    if ((frame & 1) == 0)
        for (const auto& st : stars)
            repaint (juce::Rectangle<float> (st.second * 2.4f, st.second * 2.4f).withCentre (st.first).getSmallestIntegerContainer());
}

void OniriqueVocalBoxAudioProcessorEditor::resized()
{
    backdropLayer = {};   // fond et cartes redessinés à la nouvelle taille au prochain paint()

    const float u = scale();
    const float W = (float) getWidth();
    // colonne gauche (EQ / DE-ESSER / SATURATION, RESONANCE) + colonne droite (COMPRESSION), puis rangée INPUT / AUTO GAIN / OUTPUT
    const float m = 22.0f * u, gap = 16.0f * u;
    const float leftW = 956.0f * u;                                  // +40 : DE-ESSER élargi (mètre GR)
    const float topY = 96.0f * u, topH = 352.0f * u, resoY = topY + topH + gap, resoH = 264.0f * u;
    const float botY = resoY + resoH + gap, botH = 250.0f * u;                 // rangée du bas : EQ paramétrique agrandi
    // COMPRESSION (largeur fixe) puis colonne PARALLEL à sa droite
    // colonne NOISE GATE à gauche (rangées du haut et du milieu), puis les modules ; COMPRESSION (largeur fixe), PARALLEL à droite
    const float nsW = 170.0f * u, gateW = 300.0f * u, x0 = m + nsW + gap + gateW + gap;   // colonnes NOISE SUPPRESSOR puis NOISE GATE
    const float compX = x0 + leftW + gap, compW = 504.0f * u;
    const float parX = compX + compW + gap, parW = W - m - parX;
    const float availW = leftW;
    const float deExtra = 40.0f * u;
    const float cell = (availW - deExtra - 2.0f * gap) / 6.5f;
    const float botW = 210.0f * u;                                   // INPUT, AUTO GAIN, OUTPUT ; l'analyseur prend le reste
    const float anaW = W - 2.0f * m - 3.0f * botW - 3.0f * gap;

    // ---- en-tête : presets + BYPASS à droite
    logoRect = { 22.0f * u, 10.0f * u, 64.0f * u * 985.0f / 895.0f, 64.0f * u };
    signalRect = { 0.0f, 80.0f * u, W, 12.0f * u };
    bypassButton.setBounds (juce::Rectangle<float> (136.0f * u, 40.0f * u).withRightX (W - m).withY (22.0f * u).toNearestInt());
    // La pastille de licence : un bouton de l'en-tête, entre les presets et BYPASS, à leur hauteur ; sa place suit son texte.
    const float badgeW = licenseOverlay != nullptr ? (float) licenseOverlay->pillWidth() : 0.0f;
    const float badgeRight = (float) bypassButton.getX() / u - 10.0f;
    if (licenseOverlay != nullptr)
        licenseOverlay->fit (getLocalBounds(), u, juce::Rectangle<float> (badgeRight - badgeW, 22.0f, badgeW, 40.0f).toNearestInt());
    presetBar.setBounds (juce::Rectangle<float> (336.0f * u, 40.0f * u).withRightX ((badgeRight - badgeW - 12.0f) * u).withY (22.0f * u).toNearestInt());
    osSel.setBounds (juce::Rectangle<float> (204.0f * u, 40.0f * u).withRightX ((float) presetBar.getX() - 12.0f * u).withY (22.0f * u).toNearestInt());   // suréchantillonnage, à gauche des presets
    // ordre de traitement, sous le titre, jusqu'au sélecteur de suréchantillonnage
    orderBar.setBounds (juce::Rectangle<float> (logoRect.getRight() + 16.0f * u, 55.0f * u, (float) osSel.getX() - 14.0f * u - (logoRect.getRight() + 16.0f * u), 19.0f * u).toNearestInt());
    // ligne du haut, entre le titre et le suréchantillonnage : le bouton de l'ASSISTANT
    {
        const float titleRight = logoRect.getRight() + 14.0f * u
                               + ui::textWidth (ui::font (ui::Face::display, 30.0f * u, true, 0.10f), "ONIRIQUE") + 2.0f + 10.0f * u
                               + ui::textWidth (ui::font (ui::Face::display, 30.0f * u, false, 0.10f), "VOCALBOX") + 2.0f;
        const float fxX = titleRight + 18.0f * u;
        fxButton.setBounds (juce::Rectangle<float> (fxX, 17.0f * u, 150.0f * u, 32.0f * u).toNearestInt());
    }

    if (logo.isValid())
    {
        // logo pré-réduit (net) + version floutée pour la lueur, recalculés seulement au redimensionnement
        const int lh = juce::jlimit (16, 600, juce::roundToInt (logoRect.getHeight() * 2.0f));
        logoScaled = logo.rescaled (juce::roundToInt ((float) lh * 985.0f / 895.0f), lh, juce::Graphics::highResamplingQuality);
        const int pad = lh / 4;
        juce::Image src (juce::Image::ARGB, logoScaled.getWidth() + 2 * pad, logoScaled.getHeight() + 2 * pad, true);
        { juce::Graphics lg (src); lg.drawImageAt (logoScaled, pad, pad); }
        logoGlow = juce::Image (juce::Image::ARGB, src.getWidth(), src.getHeight(), true);
        juce::ImageConvolutionKernel k (15);
        k.createGaussianBlur ((float) lh / 22.0f);
        k.applyToImage (logoGlow, src, src.getBounds());
    }

    stars.clear();
    juce::Random rnd (11);
    for (int i = 0; i < 60 && stars.size() < 14; ++i)
    {
        const float x = W * 0.33f + ((float) osSel.getX() - 12.0f * u - W * 0.33f) * rnd.nextFloat();   // entre le titre et les réglages de l'en-tête
        const float y = (8.0f + 66.0f * rnd.nextFloat()) * u;
        const float size = (2.5f + 4.5f * rnd.nextFloat() * rnd.nextFloat()) * u;
        if (fxButton.getBounds().toFloat().expanded (8.0f * u).contains (x, y)) continue;   // pas d'étoile sous le bouton FX
        stars.push_back ({ { x, y }, size });
    }

    auto inner = [u] (juce::Rectangle<float> r)
    {
        return r.withTrimmedTop (38.0f * u).withTrimmedBottom (12.0f * u).reduced (12.0f * u, 0.0f);
    };

    // ---- cartes : ordre fixe 0 EQ, 1 DE-ESSER, 2 SATURATION LAB, 3 RESONANCE, 4 INPUT, 5 AUTO GAIN, 6 OUTPUT,
    //      7 COMPRESSION, ses sous-panneaux 8 "1176" et 9 "LA-2A", puis 10 ANALYZER
    const std::vector<Section> old = sections;
    sections.clear();
    sections.push_back ({ "EQ ANALOG",      { x0, topY, 3.0f * cell, topH }, &eqPower });
    sections.push_back ({ "DE-ESSER / PLOSIVES", { x0 + 3.0f * cell + gap, topY, 2.0f * cell + deExtra, topH }, &deessPower });
    sections.push_back ({ "SATURATION LAB", { x0 + 5.0f * cell + deExtra + 2.0f * gap, topY, 1.5f * cell, topH }, &satPower });
    sections.push_back ({ "RESONANCE",      { x0, resoY, availW, resoH }, &resoPower });
    sections.push_back ({ "INPUT",          { m, botY, botW, botH } });
    sections.push_back ({ "AUTO GAIN",      { W - m - 2.0f * botW - gap, botY, botW, botH } });
    sections.push_back ({ "OUTPUT",         { W - m - botW, botY, botW, botH } });
    const bool mbTab = compTabs.getSelected() == 1;
    sections.push_back ({ "COMPRESSION",    { compX, topY, compW, resoY + resoH - topY }, &cardPower });   // coupe 1176 / LA-2A + MULTIBAND

    // COMPRESSION : onglets, puis
    //   1176 / LA-2A : mode | ordre (glisser-déposer) | panneau 1176 | panneau LA-2A
    //   MULTIBAND    : presets | écran | seuils + gains par bande | attaque / relâchement
    juce::Rectangle<float> fetPanel, laPanel, mbArea;
    {
        auto cc = inner (sections[7].bounds);
        compTabs.setBounds (cc.removeFromTop (28.0f * u).toNearestInt());
        cc.removeFromTop (10.0f * u);
        const auto content = cc;

        compMode.setBounds (cc.removeFromTop (28.0f * u).toNearestInt());
        cc.removeFromTop (10.0f * u);
        compOrder.setBounds (cc.removeFromTop (42.0f * u).toNearestInt());
        cc.removeFromTop (12.0f * u);
        fetPanel = cc.removeFromTop ((cc.getHeight() - 10.0f * u) * 0.53f);
        cc.removeFromTop (10.0f * u);
        laPanel = cc;

        auto mb = content;
        mbArea = content;
        mbPresets.setBounds (mb.removeFromTop (28.0f * u).toNearestInt());
        mb.removeFromTop (10.0f * u);
        const float D = 76.0f * u;                                     // boutons du multibande
        const auto global = mb.removeFromBottom (1.47f * D);
        mb.removeFromBottom (8.0f * u);
        const auto solos = mb.removeFromBottom (24.0f * u);              // LISTEN par bande
        mb.removeFromBottom (4.0f * u);
        const auto gains = mb.removeFromBottom (22.0f * u);
        mb.removeFromBottom (4.0f * u);
        const auto thrs = mb.removeFromBottom (1.47f * D);
        mb.removeFromBottom (10.0f * u);
        mbDisplay.setBounds (mb.toNearestInt());

        KnobComponent* t[] { &mbThr1, &mbThr2, &mbThr3, &mbThr4 };
        DragValue* gv[] { &mbGain1, &mbGain2, &mbGain3, &mbGain4 };
        PillToggle* lb[] { &mbListen1, &mbListen2, &mbListen3, &mbListen4 };
        const float cw = thrs.getWidth() / 4.0f;
        for (int b = 0; b < 4; ++b)
        {
            const float cx = thrs.getX() + cw * ((float) b + 0.5f);
            t[b]->setMaxDiameter (D);
            t[b]->setBounds (juce::Rectangle<float> (cw, thrs.getHeight()).withCentre ({ cx, thrs.getCentreY() }).toNearestInt());
            gv[b]->setBounds (juce::Rectangle<float> (cw - 10.0f * u, gains.getHeight()).withCentre ({ cx, gains.getCentreY() }).toNearestInt());
            lb[b]->setBounds (juce::Rectangle<float> (std::min (cw - 10.0f * u, 96.0f * u), solos.getHeight()).withCentre ({ cx, solos.getCentreY() }).toNearestInt());
        }
        // ATTACK / RELEASE / OUTPUT centrés sous les 4 bandes
        for (auto [k, side] : { std::pair { &mbAttackKnob, -1.0f }, std::pair { &mbReleaseKnob, 0.0f }, std::pair { &mbOutKnob, 1.0f } })
        {
            k->setMaxDiameter (D);
            k->setBounds (juce::Rectangle<float> (cw, global.getHeight())
                              .withCentre ({ global.getCentreX() + side * cw * 1.05f, global.getCentreY() }).toNearestInt());
        }
    }
    sections.push_back ({ "1176",  fetPanel, nullptr, 0.0f, true, mbTab });
    sections.push_back ({ "LA-2A", laPanel,  nullptr, 0.0f, true, mbTab });
    sections.push_back ({ "PARAMETRIC EQ", { m + botW + gap, botY, anaW, botH }, &peqPower });
    sections.push_back ({ "MBAREA",   mbArea, nullptr, 0.0f, true, ! mbTab, true });   // zone voilée quand le MULTIBAND est coupé
    sections.push_back ({ "COMPAREA", mbArea, nullptr, 0.0f, true, mbTab, true });     // idem pour 1176 / LA-2A
    sections.push_back ({ "PARALLEL", { parX, topY, parW, resoY + resoH - topY }, &parPower });   // index 13
    sections.push_back ({ "NOISE GATE", { m + nsW + gap, topY, gateW, resoY + resoH - topY }, &gatePower });   // index 14
    sections.push_back ({ "NOISE SUPPRESSOR", { m, topY, nsW, resoY + resoH - topY } });                      // index 15
    {
        // NOISE SUPPRESSOR : grand curseur vertical (comme un fader de console) + mètre de la réduction appliquée
        auto nc = inner (sections[15].bounds);
        nc.removeFromTop (6.0f * u);
        const auto grCol = nc.removeFromRight (46.0f * u);
        nc.removeFromRight (8.0f * u);
        nsGr.setBounds (grCol.withSizeKeepingCentre (grCol.getWidth(), std::min (grCol.getHeight() - 10.0f * u, 420.0f * u)).toNearestInt());
        nsFader.setMaxDiameter (nc.getHeight() / 1.47f);
        nsFader.setBounds (nc.toNearestInt());
    }
    {
        // NOISE GATE : écran en haut, THRESHOLD / RANGE, puis ATTACK / HOLD / RELEASE, HYST en bas
        auto gc = inner (sections[14].bounds);
        gateDisplay.setBounds (gc.removeFromTop (218.0f * u).toNearestInt());
        gc.removeFromTop (10.0f * u);
        gateHystV.setBounds (gc.removeFromBottom (26.0f * u).withSizeKeepingCentre (150.0f * u, 26.0f * u).toNearestInt());
        gc.removeFromBottom (8.0f * u);
        auto row1 = gc.removeFromTop (gc.getHeight() * 0.52f);
        auto row2 = gc;
        const float d1 = std::min (100.0f * u, (row1.getHeight() - 20.0f * u) / 1.47f), d2 = std::min (78.0f * u, (row2.getHeight() - 16.0f * u) / 1.47f);
        for (auto [k, i] : { std::pair { &gateThrKnob, 0 }, std::pair { &gateRangeKnob, 1 } })
        {
            k->setMaxDiameter (d1);
            k->setBounds (juce::Rectangle<float> (row1.getX() + row1.getWidth() * 0.5f * (float) i, row1.getY(), row1.getWidth() * 0.5f, row1.getHeight()).toNearestInt());
        }
        int i = 0;
        for (auto* k : { &gateAtkKnob, &gateHoldKnob, &gateRelKnob })
        {
            k->setMaxDiameter (d2);
            k->setBounds (juce::Rectangle<float> (row2.getX() + row2.getWidth() / 3.0f * (float) i++, row2.getY(), row2.getWidth() / 3.0f, row2.getHeight()).toNearestInt());
        }
    }
    {
        // PARALLEL : AMOUNT en haut, AUTO dessous, puis le mètre de réduction du bus écrasé, à plat, sur toute la largeur.
        //   AUTO : le DE-ESS du bus prend tout le reste (grand bouton, fréquence dessous) : rien de minuscule, pas de vide
        //   AUTO décoché : grille 2 x 2 de vrais boutons (THRESHOLD | ATTACK, RELEASE | DE-ESS), fréquence du DE-ESS dessous
        auto pc = inner (sections[13].bounds);
        pc.removeFromTop (6.0f * u);
        parManualShown = ! parAutoButton.getToggleState();
        const float D = std::min (pc.getWidth() - 12.0f * u, parManualShown ? 84.0f * u : 124.0f * u);
        parAmountKnob.setMaxDiameter (D);
        parAmountKnob.setBounds (pc.removeFromTop (1.47f * D + 24.0f * u).toNearestInt());
        pc.removeFromTop (parManualShown ? 6.0f * u : 12.0f * u);
        parAutoButton.setBounds (pc.removeFromTop (36.0f * u).reduced (4.0f * u, 0.0f).toNearestInt());
        pc.removeFromTop (parManualShown ? 8.0f * u : 14.0f * u);
        parGr.setBounds (pc.removeFromTop (54.0f * u).reduced (4.0f * u, 0.0f).toNearestInt());
        pc.removeFromTop (parManualShown ? 6.0f * u : 12.0f * u);
        parDeessFreqV.setBounds (pc.removeFromBottom (26.0f * u).reduced (4.0f * u, 0.0f).toNearestInt());
        pc.removeFromBottom (6.0f * u);

        if (! parManualShown)
        {
            const float dd = std::min ({ pc.getWidth() - 12.0f * u, 112.0f * u, (pc.getHeight() - 28.0f * u) / 1.47f });
            parDeessKnob.setMaxDiameter (dd);
            parDeessKnob.setBounds (pc.withSizeKeepingCentre (pc.getWidth(), std::min (pc.getHeight(), 1.47f * dd + 28.0f * u)).toNearestInt());
        }
        const float cw = pc.getWidth() * 0.5f, rh = pc.getHeight() * 0.5f;
        const float kd = std::min (cw - 8.0f * u, (rh - 26.0f * u) / 1.47f);
        KnobComponent* grid[] { &parThreshKnob, &parAttackKnob, &parReleaseKnob, &parDeessKnob };
        for (int i = 0; i < 4; ++i)
        {
            if (i == 3 && ! parManualShown) break;   // AUTO : le DE-ESS est déjà placé en grand
            grid[i]->setMaxDiameter (kd);
            grid[i]->setBounds (juce::Rectangle<float> (pc.getX() + cw * (float) (i % 2), pc.getY() + rh * (float) (i / 2), cw, rh).toNearestInt());
            if (i < 3) grid[i]->setVisible (parManualShown);
        }
    }

    for (auto& s : sections)
    {
        for (const auto& o : old) if (o.title == s.title) s.veil = o.veil;
        if (s.power == nullptr) continue;
        if (old.empty()) s.veil = s.power->getToggleState() ? 0.0f : 1.0f;
        s.power->setBounds (juce::Rectangle<float> (32.0f * u, 32.0f * u)
                                .withCentre ({ s.bounds.getRight() - 24.0f * u, s.bounds.getY() + 19.0f * u }).toNearestInt());
    }
    {
        const auto& rs = sections[3].bounds;   // RESONANCE : bouton LISTEN à gauche du bouton power
        listenButton.setBounds (juce::Rectangle<float> (96.0f * u, 24.0f * u).withRightX (rs.getRight() - 46.0f * u).withY (rs.getY() + 7.0f * u).toNearestInt());
        // COMPRESSION : un bouton power par sous-module, dans son onglet (le bouton de la carte coupe tout)
        compPower.setBounds (compTabs.iconArea (0));
        mbPower.setBounds (compTabs.iconArea (1));
    }
    auto place = [&] (juce::Component& c, juce::Rectangle<float> r) { c.setBounds (r.toNearestInt()); };
    // colonnes de largeur identique dans toute la rangée : tous les boutons ont la même taille
    const float colW = cell - 20.0f * u;
    auto splitCols = [colW] (juce::Rectangle<float> r, int n, int i)
    {
        const float w = r.getWidth() / (float) n;
        return juce::Rectangle<float> (r.getX() + w * (float) i, r.getY(), w, r.getHeight())
                   .withSizeKeepingCentre (std::min (w, colW), r.getHeight());
    };

    // ---- rangée du haut : bande de sélecteurs au-dessus des boutons (alignés dans les 3 modules)
    auto eq = inner (sections[0].bounds), de = inner (sections[1].bounds), sat = inner (sections[2].bounds);
    // deux bandes : LOW CUT puis la fréquence du LOW, juste au-dessus des boutons
    const float stripH = 26.0f * u, stripGap = 6.0f * u;
    const auto eqToggles = eq.removeFromTop (stripH);
    eq.removeFromTop (stripGap);
    const auto eqSelectors = eq.removeFromTop (stripH);
    eq.removeFromTop (stripGap);
    {
        // DE-ESSER / PLOSIVES : curseur PLOSIVES dans la bande au-dessus des boutons
        // second de-esser ajouté : une seule bande, pour laisser la hauteur aux deux rangées de boutons
        const auto plos = de.removeFromTop ((deess2Button.getToggleState() ? 1.0f : 2.0f) * (stripH + stripGap));
        place (plosiveFader, plos.withSizeKeepingCentre (plos.getWidth() - 8.0f * u, 30.0f * u));
    }
    sat.removeFromTop (stripH + stripGap);
    place (lowCutButton, splitCols (eqToggles, 2, 0));                // LOW CUT 85 Hz
    place (lowFreq,  splitCols (eqSelectors, 2, 0));                  // 80 / 100 / 125 Hz (le LOW)
    place (satType, sat.removeFromTop (stripH));                      // TUBE / ANALOG / TAPE
    sat.removeFromTop (stripGap);
    place (lowKnob,   splitCols (eq, 4, 0));
    place (midKnob,   splitCols (eq, 4, 1));
    place (highKnob,  splitCols (eq, 4, 2));
    place (airKnob,   splitCols (eq, 4, 3));
    {
        // en bas de la carte : le bouton + (second de-esser, REMOVE une fois ajouté). Le second de-esser prend une
        // seconde rangée, construite comme la première : FREQUENCY 2 | INTENSITY 2 | son propre mètre de réduction.
        deess2Shown = deess2Button.getToggleState();
        deess2Button.setButtonText (deess2Shown ? "REMOVE" : "+ DE-ESSER");
        if (deess2Shown)
        {
            // REMOVE dans la barre de titre de la carte, à gauche du bouton power (comme LISTEN dans RESONANCE)
            const auto& ds = sections[1].bounds;
            place (deess2Button, juce::Rectangle<float> (88.0f * u, 24.0f * u).withRightX (ds.getRight() - 46.0f * u).withY (ds.getY() + 7.0f * u));
        }
        else
        {
            const auto buttonRow = de.removeFromBottom (24.0f * u);
            de.removeFromBottom (4.0f * u);
            place (deess2Button, buttonRow.withSizeKeepingCentre (std::min (buttonRow.getWidth(), 120.0f * u), 24.0f * u));
        }

        auto layoutRow = [&] (juce::Rectangle<float> row, KnobComponent& f, KnobComponent& a, GainReductionBar& gr)
        {
            const auto grArea = row.removeFromRight (46.0f * u);
            row.removeFromRight (4.0f * u);
            place (gr, grArea.withSizeKeepingCentre (grArea.getWidth(), std::min (grArea.getHeight(), 150.0f * u)));
            place (f, splitCols (row, 2, 0));
            place (a, splitCols (row, 2, 1));
        };
        if (deess2Shown)
        {
            auto second = de.removeFromBottom ((de.getHeight() - 6.0f * u) * 0.5f);
            de.removeFromBottom (6.0f * u);
            layoutRow (second, deess2FreqKnob, deess2AmtKnob, deess2Gr);
        }
        for (auto* c : std::initializer_list<juce::Component*> { &deess2FreqKnob, &deess2AmtKnob, &deess2Gr })
            c->setVisible (deess2Shown);

        // DE-ESSER : FREQUENCY | INTENSITY | mètre de réduction (GR) à droite
        layoutRow (de, freqKnob, amtKnob, deessGr);
    }
    place (driveKnob, sat.withSizeKeepingCentre (std::min (sat.getWidth(), colW), sat.getHeight()));

    // ---- RESONANCE : afficheur | LOW / MID / HIGH (sensibilité + fréquence) | AMOUNT
    {
        auto rr = inner (sections[3].bounds);
        const float Dk = std::min (colW - 2.0f, rr.getHeight() / 1.74f);
        const float groupTop = rr.getY() + (rr.getHeight() - 1.74f * Dk) * 0.5f;
        const float faderW = 70.0f * u;

        const auto amtCol = rr.removeFromRight (colW);
        resoAmtKnob.setMaxDiameter (Dk);
        place (resoAmtKnob, { amtCol.getX(), groupTop, amtCol.getWidth(), 1.47f * Dk });
        rr.removeFromRight (12.0f * u);

        KnobComponent* faders[] { &resoHighFader, &resoMidFader, &resoLowFader };
        DragValue* hz[] { &resoHighHz, &resoMidHz, &resoLowHz };
        for (int i = 0; i < 3; ++i)
        {
            const auto col = rr.removeFromRight (faderW);
            faders[i]->setMaxDiameter (Dk);
            place (*faders[i], { col.getX(), groupTop, col.getWidth(), 1.47f * Dk });
            place (*hz[i], juce::Rectangle<float> (col.getWidth() * 0.98f, 0.21f * Dk)
                               .withCentre ({ col.getCentreX(), groupTop + 1.47f * Dk + 0.06f * Dk + 0.105f * Dk }));
        }
        rr.removeFromRight (14.0f * u);
        place (resoDisplay, rr);
    }

    // ---- panneaux 1176 / LA-2A : titre + sélecteur, puis VU-mètre et boutons
    auto panelBody = [u] (juce::Rectangle<float> r, juce::Rectangle<float>& titleRow)
    {
        auto pi = r.reduced (12.0f * u, 10.0f * u);
        titleRow = pi.removeFromTop (26.0f * u);
        pi.removeFromTop (6.0f * u);
        return pi;
    };
    {
        juce::Rectangle<float> title;
        auto body = panelBody (fetPanel, title);
        place (fetRatio, title.removeFromRight (title.getWidth() * 0.58f));
        const auto vu = body.removeFromLeft (128.0f * u);
        place (vu1176, vu.withSizeKeepingCentre (vu.getWidth(), std::min (vu.getHeight(), 96.0f * u)));
        body.removeFromLeft (8.0f * u);
        KnobComponent* k[] { &fetInKnob, &fetOutKnob, &fetAtkKnob, &fetRelKnob };
        for (int i = 0; i < 4; ++i)
            place (*k[i], juce::Rectangle<float> (body.getX() + body.getWidth() * (float) i / 4.0f, body.getY(), body.getWidth() / 4.0f, body.getHeight()));
    }
    {
        juce::Rectangle<float> title;
        auto body = panelBody (laPanel, title);
        place (laMode, title.removeFromRight (title.getWidth() * 0.50f));
        const auto vu = body.removeFromLeft (128.0f * u);
        place (vuLA2A, vu.withSizeKeepingCentre (vu.getWidth(), std::min (vu.getHeight(), 96.0f * u)));
        body.removeFromLeft (8.0f * u);
        place (laGainKnob, body.removeFromLeft (body.getWidth() * 0.5f));
        place (laPeakKnob, body);
    }

    // INPUT / OUTPUT : bouton + mètre vertical stéréo à sa droite
    {
        auto ia = inner (sections[4].bounds), oa = inner (sections[6].bounds);
        place (inMeter,  ia.removeFromRight (46.0f * u));
        place (outMeter, oa.removeFromRight (46.0f * u));
        ia.removeFromRight (6.0f * u);
        oa.removeFromRight (6.0f * u);
        place (inKnob, ia);
        place (outKnob, oa);
    }
    const auto ag = inner (sections[5].bounds);
    place (autoButton, ag.withSizeKeepingCentre (ag.getWidth(), std::min (ag.getHeight(), ag.getWidth() * 0.62f)));
    place (analyzer, sections[10].bounds.reduced (12.0f * u).withTrimmedTop (26.0f * u));
    {
        // barre de la bande sélectionnée, à droite du titre de la carte :
        // BANDE + type | FREQ | GAIN | Q | THR (bande dynamique) | plein écran | power
        const auto card = sections[10].bounds;
        fullButton.setBounds (juce::Rectangle<float> (30.0f * u, 26.0f * u).withRightX (card.getRight() - 46.0f * u).withY (card.getY() + 6.0f * u).toNearestInt());
        peqListenButton.setBounds (juce::Rectangle<float> (40.0f * u, 26.0f * u).withRightX ((float) fullButton.getX() - 8.0f * u).withY (card.getY() + 6.0f * u).toNearestInt());
        auto strip = juce::Rectangle<float> ((float) peqListenButton.getX() - 10.0f * u - 600.0f * u, card.getY() + 7.0f * u, 600.0f * u, 24.0f * u);
        peqChip.setBounds (strip.removeFromLeft (160.0f * u).toNearestInt());
        strip.removeFromLeft (6.0f * u);
        const auto fR = strip.removeFromLeft (112.0f * u);
        strip.removeFromLeft (6.0f * u);
        const auto gR = strip.removeFromLeft (104.0f * u);
        strip.removeFromLeft (6.0f * u);
        const auto qR = strip.removeFromLeft (84.0f * u);
        strip.removeFromLeft (6.0f * u);
        const auto tR = strip;
        for (int i = 0; i < 8; ++i)
        {
            if (peqFreqV[(size_t) i] == nullptr) continue;
            peqFreqV[(size_t) i]->setBounds (fR.toNearestInt());
            peqGainV[(size_t) i]->setBounds (gR.toNearestInt());
            peqQV[(size_t) i]->setBounds (qR.toNearestInt());
            peqThrV[(size_t) i]->setBounds (tR.toNearestInt());
            peqSlopeV[(size_t) i]->setBounds (gR.toNearestInt());   // coupe-bas / coupe-haut : la pente prend la place du gain
        }
    }
    // EQ plein écran : toute la surface des modules (l'en-tête reste visible) ; ASSISTANT : toute la fenêtre
    eqFull.setBounds (juce::Rectangle<float> (m, topY, W - 2.0f * m, (float) getHeight() - topY - m).toNearestInt());
    fxView.setBounds (eqFull.getBounds());

    const auto& o = sections[6].bounds;
    overRect = { o.getRight() - 84.0f * u, o.getY() + 10.0f * u, 64.0f * u, 18.0f * u };

    if (! restoringSize)
        onirique::window::remember (processor.apvts.state, getWidth(), getHeight());   // taille mémorisée avec le projet
    veils.setBounds (getLocalBounds());
}

// EQ paramétrique : la barre de réglage suit la bande sélectionnée dans l'analyseur (seuil affiché si elle est dynamique)
void OniriqueVocalBoxAudioProcessorEditor::selectPeqBand (int b)
{
    peqChip.setBand (b);
    // l'écoute (casque) suit la bande sélectionnée
    if (auto* lb = processor.apvts.getParameter (ids::peqListenBand); lb != nullptr && juce::roundToInt (lb->convertFrom0to1 (lb->getValue())) != b)
        lb->setValueNotifyingHost (lb->convertTo0to1 ((float) b));
    const int type = juce::roundToInt (processor.apvts.getRawParameterValue (ids::peqType[b])->load());
    peqDynShown = processor.apvts.getRawParameterValue (ids::peqDyn[b])->load() > 0.5f && ParametricEQ::canBeDynamic (type);
    peqCutShown = ParametricEQ::isCut (type);
    for (int i = 0; i < 8; ++i)
    {
        if (peqFreqV[(size_t) i] == nullptr) continue;
        peqFreqV[(size_t) i]->setVisible (i == b);
        peqQV[(size_t) i]->setVisible (i == b);
        peqGainV[(size_t) i]->setVisible (i == b && ! peqCutShown);
        peqSlopeV[(size_t) i]->setVisible (i == b && peqCutShown);
        peqThrV[(size_t) i]->setVisible (i == b && peqDynShown);
    }
    if (eqFull.isVisible()) eqFull.selectBand (b);
}

// EQ paramétrique en plein écran : un seul analyseur lit les prises (celui qui est affiché)
void OniriqueVocalBoxAudioProcessorEditor::showEqFull (bool show)
{
    if (show) { eqFull.selectBand (analyzer.getSelected()); if (fxView.isShown()) fxView.setShown (false); }
    eqFull.setShown (show);
    analyzer.setActive (! show);
}

// page FX : une seule page plein écran à la fois
void OniriqueVocalBoxAudioProcessorEditor::showFx (bool show)
{
    if (show && eqFull.isShown()) showEqFull (false);
    fxView.setShown (show);
}

// carte COMPRESSION : affiche l'onglet 1176 / LA-2A (0) ou MULTIBAND (1)
void OniriqueVocalBoxAudioProcessorEditor::showCompTab (int tab)
{
    tab = juce::jlimit (0, 1, tab);
    processor.apvts.state.setProperty ("compTab", tab, nullptr);
    if (compTabs.getSelected() != tab) compTabs.setSelected (tab);

    const bool single = tab == 0;
    for (auto* c : std::initializer_list<juce::Component*> { &compMode, &compOrder, &fetRatio, &laMode, &vu1176, &vuLA2A,
                                                             &fetInKnob, &fetOutKnob, &fetAtkKnob, &fetRelKnob, &laGainKnob, &laPeakKnob })
        c->setVisible (single);
    for (auto* c : std::initializer_list<juce::Component*> { &mbDisplay, &mbPresets, &mbThr1, &mbThr2, &mbThr3, &mbThr4,
                                                             &mbGain1, &mbGain2, &mbGain3, &mbGain4, &mbAttackKnob, &mbReleaseKnob, &mbOutKnob,
                                                             &mbListen1, &mbListen2, &mbListen3, &mbListen4 })
        c->setVisible (! single);
    resized();
    repaint();
}

void OniriqueVocalBoxAudioProcessorEditor::paintStar (juce::Graphics& g, juce::Point<float> c, float r, float alpha)
{
    // étoile à 4 branches, reprise du logo
    juce::Path s;
    const float k = 0.16f * r;
    s.startNewSubPath (c.x, c.y - r);
    s.lineTo (c.x + k, c.y - k); s.lineTo (c.x + r, c.y); s.lineTo (c.x + k, c.y + k);
    s.lineTo (c.x, c.y + r);     s.lineTo (c.x - k, c.y + k); s.lineTo (c.x - r, c.y); s.lineTo (c.x - k, c.y - k);
    s.closeSubPath();
    g.setColour (ui::ember.withAlpha (0.25f * alpha));
    g.fillEllipse (juce::Rectangle<float> (r * 1.1f, r * 1.1f).withCentre (c));
    g.setColour (ui::mist.withAlpha (alpha));
    g.fillPath (s);
}

void OniriqueVocalBoxAudioProcessorEditor::paintHeader (juce::Graphics& g)
{
    const float u = scale();
    const float W = (float) getWidth();
    const float t = (float) clock;

    for (size_t i = 0; i < stars.size(); ++i)
    {
        const float a = 0.15f + 0.75f * (0.5f + 0.5f * std::sin (t * (0.9f + 0.37f * (float) (i % 5)) + (float) i * 1.7f));
        paintStar (g, stars[i].first, stars[i].second, a);
    }

    // logo : lueur braise qui respire + sigil net
    if (logoScaled.isValid())
    {
        const float breathe = 0.5f + 0.5f * std::sin (t * 2.0f * kPi / 5.0f);
        const float pad = logoRect.getHeight() / 4.0f;
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.setColour (ui::ember.withAlpha (0.55f + 0.35f * breathe));
        g.drawImage (logoGlow, logoRect.expanded (pad), juce::RectanglePlacement::stretchToFit, true);
        g.setColour (juce::Colours::white.withAlpha (0.08f + 0.10f * breathe));
        g.drawImage (logoGlow, logoRect.expanded (pad), juce::RectanglePlacement::stretchToFit, true);
        g.setOpacity (1.0f);
        g.drawImage (logoScaled, logoRect, juce::RectanglePlacement::stretchToFit);
    }

    // titre + chaîne de traitement
    {
        const float x = logoRect.getRight() + 14.0f * u;
        auto f1 = ui::font (ui::Face::display, 30.0f * u, true, 0.10f);
        auto f2 = ui::font (ui::Face::display, 30.0f * u, false, 0.10f);
        const float w1 = ui::textWidth (f1, "ONIRIQUE") + 2.0f;
        const float w2 = ui::textWidth (f2, "VOCALBOX") + 2.0f;
        const float y = 16.0f * u, hh = 36.0f * u;
        g.setFont (f1);
        g.setColour (ui::mist);
        g.drawText ("ONIRIQUE", juce::Rectangle<float> (x, y, w1, hh), juce::Justification::centredLeft, false);
        g.setColour (ui::ember);
        g.drawText ("VOCALBOX", juce::Rectangle<float> (x + w1 + 10.0f * u, y, w2 + 20.0f * u, hh), juce::Justification::centredLeft, false);

    }

    // ligne de signal : filet qui s'estompe aux bords + impulsion lumineuse qui la parcourt
    {
        const float ly = 86.0f * u;
        juce::ColourGradient cg (juce::Colours::transparentBlack, 22.0f * u, ly, juce::Colours::transparentBlack, W - 22.0f * u, ly, false);
        cg.addColour (0.08, ui::line);
        cg.addColour (0.92, ui::line);
        g.setGradientFill (cg);
        g.fillRect (juce::Rectangle<float> (22.0f * u, ly, W - 44.0f * u, std::max (1.0f, u)));

        const float len = 130.0f * u;
        const float span = W - 44.0f * u + len;
        const float px = 22.0f * u - len + std::fmod (t * 0.24f, 1.0f) * span;
        const auto streak = juce::Rectangle<float> (px, ly - 1.5f * u, len, 3.5f * u);
        juce::ColourGradient sg (ui::bone.withAlpha (0.0f), streak.getX(), ly, ui::ember, streak.getRight(), ly, false);
        sg.addColour (0.6, ui::bone);
        g.reduceClipRegion (juce::Rectangle<float> (22.0f * u, signalRect.getY(), W - 44.0f * u, signalRect.getHeight()).toNearestInt());
        g.setColour (ui::ember.withAlpha (0.25f));
        g.fillRoundedRectangle (streak.expanded (0.0f, 2.5f * u).withTrimmedLeft (len * 0.5f), 3.0f * u);
        g.setGradientFill (sg);
        g.fillRoundedRectangle (streak, streak.getHeight() * 0.5f);
    }
}

void OniriqueVocalBoxAudioProcessorEditor::paint (juce::Graphics& g)
{
    const float u = scale();

    // fond et cartes : images prêtes, à la résolution réelle de l'écran (Retina : x2)
    const float ps = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    if (! backdropLayer.isValid() || std::abs (ps - layersScale) > 1.0e-3f)
        rebuildLayers (ps);
    const auto toLogical = juce::AffineTransform::scale (1.0f / layersScale);
    g.setOpacity (1.0f);
    g.drawImageTransformed (backdropLayer, toLogical);

    {
        juce::Graphics::ScopedSaveState ss (g);
        paintHeader (g);
    }

    g.setOpacity (1.0f);
    g.drawImageTransformed (cardsLayer, toLogical);

    // ---- point braise des cartes : suit le bouton power du module
    for (const auto& s : sections)
    {
        if (s.hidden || s.plain || s.sub) continue;
        const auto r = s.bounds;
        const bool on = s.power == nullptr || s.power->getToggleState();
        const float dot = 6.0f * u;
        const auto dotR = juce::Rectangle<float> (dot, dot).withCentre ({ r.getX() + 20.0f * u, r.getY() + 19.0f * u });
        g.setColour (ui::ember.withAlpha (on ? 0.35f : 0.0f));
        g.fillEllipse (dotR.expanded (dot * 0.7f));
        g.setColour (on ? ui::ember : ui::haze.withAlpha (0.5f));
        g.fillEllipse (dotR);
    }

    // ---- témoin OVER (carte OUTPUT)
    {
        const float d = 8.0f * u;
        const auto dot = juce::Rectangle<float> (d, d).withCentre ({ overRect.getX() + d * 0.5f, overRect.getCentreY() });
        g.setColour (ui::ember.withAlpha (0.45f * overGlow));
        g.fillEllipse (dot.expanded (d * 0.9f));
        g.setColour (ui::haze.withAlpha (0.35f).interpolatedWith (ui::ember, overGlow));
        g.fillEllipse (dot);
        g.setColour (ui::haze.interpolatedWith (ui::bone, overGlow));
        g.setFont (ui::fitFont (ui::Face::mono, 11.0f * u, overRect.getWidth() - d - 8.0f * u, "OVER", true, 0.1f));
        g.drawText ("OVER", overRect.withTrimmedLeft (d + 8.0f * u), juce::Justification::centredLeft, false);
    }
}

void OniriqueVocalBoxAudioProcessorEditor::rebuildLayers (float pixelScale)
{
    layersScale = pixelScale;
    const int w = juce::jmax (1, juce::roundToInt ((float) getWidth() * pixelScale));
    const int h = juce::jmax (1, juce::roundToInt ((float) getHeight() * pixelScale));
    const auto toPixels = juce::AffineTransform::scale (pixelScale);

    backdropLayer = juce::Image (juce::Image::RGB, w, h, false);
    {
        juce::Graphics bg (backdropLayer);
        bg.addTransform (toPixels);
        paintBackdrop (bg);
    }
    cardsLayer = juce::Image (juce::Image::ARGB, w, h, true);
    {
        juce::Graphics cg (cardsLayer);
        cg.addTransform (toPixels);
        paintCards (cg);
    }
}

// ---- fond : sang -> nuit, halo braise en haut à droite
void OniriqueVocalBoxAudioProcessorEditor::paintBackdrop (juce::Graphics& g)
{
    const float u = scale();
    const float W = (float) getWidth(), H = (float) getHeight();

    // DA PRO SERIES : noir profond, lueurs rouges très discrètes, grain fin
    g.fillAll (ui::night);
    g.setGradientFill (juce::ColourGradient (ui::blood.withAlpha (0.55f), W * 0.22f, H * 0.45f, ui::night.withAlpha (0.0f), W * 0.22f + W * 0.6f, H * 0.45f, true));
    g.fillRect (getLocalBounds());
    g.setGradientFill (juce::ColourGradient (ui::blood.withAlpha (0.22f), W * 0.92f, H * 1.05f, ui::night.withAlpha (0.0f), W * 0.92f - W * 0.45f, H * 1.05f, true));
    g.fillRect (getLocalBounds());
    juce::Random rnd (21);
    for (int i = 0; i < 140; ++i)
    {
        const float x = rnd.nextFloat() * W, y = rnd.nextFloat() * H, s = (0.5f + 1.2f * rnd.nextFloat() * rnd.nextFloat()) * u;
        g.setColour (ui::mist.withAlpha (0.03f + 0.07f * rnd.nextFloat()));
        g.fillEllipse (x, y, s, s);
    }
}

// ---- cartes : verre sombre, filet fin, ombre portée, titre (le point braise est dessiné à part, il suit le module)
void OniriqueVocalBoxAudioProcessorEditor::paintCards (juce::Graphics& g)
{
    const float u = scale();

    for (const auto& s : sections)
    {
        const auto r = s.bounds;

        if (s.hidden || s.plain) continue;   // sous-panneaux de l'onglet non affiché, zones de voile seules
        if (s.sub)
        {
            // sous-panneau de la carte COMPRESSION : creux sombre, nom de l'appareil + type
            const float rad = 12.0f * u;
            g.setColour (juce::Colours::black.withAlpha (0.26f));
            g.fillRoundedRectangle (r, rad);
            g.setColour (ui::line);
            g.drawRoundedRectangle (r.reduced (0.5f), rad, std::max (1.0f, 1.0f * u));
            const auto title = r.reduced (12.0f * u, 10.0f * u).removeFromTop (26.0f * u);
            const auto f = ui::font (ui::Face::display, 19.0f * u, true, 0.06f);
            const float tw = ui::textWidth (f, s.title) + 8.0f * u;
            g.setColour (ui::mist);
            g.setFont (f);
            g.drawText (s.title, title.withWidth (tw), juce::Justification::centredLeft, false);
            g.setColour (ui::haze.withAlpha (0.85f));
            g.setFont (ui::font (ui::Face::mono, 10.0f * u, false, 0.06f));
            g.drawText (s.title == "1176" ? "FET LIMITER" : "OPTO LEVELER", title.withTrimmedLeft (tw).withWidth (100.0f * u),
                        juce::Justification::centredLeft, false);
            continue;
        }

        // verre sombre (DA PRO SERIES) : charbon, reflet rose pâle très léger en haut, filet fin
        const float rad = 12.0f * u;
        juce::Path card;
        card.addRoundedRectangle (r, rad);
        juce::DropShadow (juce::Colours::black.withAlpha (0.55f), juce::roundToInt (28.0f * u), { 0, juce::roundToInt (12.0f * u) })
            .drawForPath (g, card);
        g.setColour (ui::deep.withAlpha (0.94f));
        g.fillPath (card);
        g.setGradientFill (juce::ColourGradient (ui::lilac.withAlpha (0.035f), 0.0f, r.getY(), ui::lilac.withAlpha (0.0f), 0.0f, r.getY() + r.getHeight() * 0.7f, false));
        g.fillPath (card);
        g.setColour (ui::line);
        g.strokePath (card, juce::PathStrokeType (std::max (1.0f, 1.0f * u)));

        const float maxW = r.getWidth() - (s.power != nullptr ? 90.0f : 60.0f) * u;
        g.setColour (ui::mist);
        g.setFont (ui::fitFont (ui::Face::display, 12.5f * u, maxW, s.title, true, 0.06f));
        g.drawText (s.title, juce::Rectangle<float> (r.getX() + 32.0f * u, r.getY() + 8.0f * u, maxW, 22.0f * u),
                    juce::Justification::centredLeft, false);
    }
}

// module coupé : voile nuit sur le contenu de sa carte (titre et bouton power restent nets)
void OniriqueVocalBoxAudioProcessorEditor::paintVeils (juce::Graphics& g)
{
    const float u = scale();
    for (const auto& s : sections)
    {
        if (s.hidden || (s.power == nullptr && ! s.sub) || s.veil < 0.002f) continue;
        g.setColour (ui::deep.withAlpha (0.64f * s.veil));   // coupé : estompé, mais ses réglages restent lisibles
        if (s.sub) g.fillRoundedRectangle (s.bounds, 12.0f * u);   // compresseur non utilisé dans le mode choisi
        else       g.fillRoundedRectangle (s.bounds.reduced (1.0f).withTrimmedTop (36.0f * u), 14.0f * u);
    }
}
