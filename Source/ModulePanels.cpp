#include "ModulePanels.h"

using namespace onirique;

namespace
{
constexpr float kPi = juce::MathConstants<float>::pi;
constexpr float kArc0 = -0.75f * kPi, kArc1 = 0.75f * kPi;   // course des potards : -135° .. +135° autour du haut
} // namespace

//==============================================================================
float ui::unitOf (const juce::Component& c)
{
    if (auto* ed = c.findParentComponentOfClass<juce::AudioProcessorEditor>()) return (float) ed->getWidth() / 1600.0f;
    if (auto* ed = dynamic_cast<const juce::AudioProcessorEditor*> (&c)) return (float) ed->getWidth() / 1600.0f;
    return 1.0f;
}

juce::Path ui::starPath (juce::Point<float> c, float r, float rot)
{
    juce::Path p;
    for (int i = 0; i < 4; ++i)
    {
        const float a = rot + (float) i * kPi * 0.5f - kPi * 0.5f, b = a + kPi * 0.25f, a2 = a + kPi * 0.5f;
        const juce::Point<float> tip { c.x + r * std::cos (a), c.y + r * std::sin (a) };
        if (i == 0) p.startNewSubPath (tip);
        p.quadraticTo (c.x + 0.16f * r * std::cos (b), c.y + 0.16f * r * std::sin (b), c.x + r * std::cos (a2), c.y + r * std::sin (a2));
    }
    p.closeSubPath();
    return p;
}

void ui::drawGlass (juce::Graphics& g, juce::Rectangle<float> r, float corner, float u, float lit)
{
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.075f), r.getX(), r.getY(),
                                             juce::Colours::white.withAlpha (0.02f), r.getX() + r.getWidth() * 0.4f, r.getBottom(), false));
    g.fillRoundedRectangle (r, corner);
    if (lit > 0.0f)
    {
        g.setColour (ember.withAlpha (0.10f * lit));
        g.fillRoundedRectangle (r, corner);
    }
    g.setColour (juce::Colour (0xffffebee).withAlpha (0.10f + 0.35f * lit));
    g.drawRoundedRectangle (r.reduced (0.5f), corner, std::max (1.0f, 1.0f * u));
    // reflet du haut
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawLine (r.getX() + corner, r.getY() + 1.0f, r.getRight() - corner, r.getY() + 1.0f, std::max (1.0f, 0.8f * u));
}

//==============================================================================
StarKnob::StarKnob (juce::AudioProcessorValueTreeState& s, const juce::String& id, const juce::String& l) : label (l)
{
    param = s.getParameter (id);
    jassert (param != nullptr);
    attachment = std::make_unique<juce::ParameterAttachment> (*param, [this] (float v)
    {
        norm = param->convertTo0to1 (v);
        repaint();
    });
    attachment->sendInitialUpdate();
    shown = norm;
    setRepaintsOnMouseActivity (false);
    setBufferedToImage (true);   // un potard redessiné coûte cher : seulement quand il change
}

StarKnob::~StarKnob() = default;

// la cellule de la démo (.kcell) : 8 de marge, le nom (JetBrains Mono 10), 4, le potard, 4, la valeur (JetBrains Mono 12) ;
// façade (1176, LA-2A) : le nom en Unbounded 13 espacé de 0.2 em, la valeur dans une pastille sombre
juce::Rectangle<float> StarKnob::knobArea() const noexcept
{
    auto r = getLocalBounds().toFloat();
    const float u = ui::unitOf (*this);
    r.reduce (4.0f * u, 8.0f * u);
    if (showLabel) r.removeFromTop ((faceStyle ? 18.0f + 14.0f : 13.0f + 4.0f) * u);
    if (showValue) r.removeFromBottom ((faceStyle ? 14.0f + 30.0f : 4.0f + 16.0f) * u);
    const float d = std::min (r.getWidth(), r.getHeight());
    return juce::Rectangle<float> (d, d).withCentre (r.getCentre());
}

void StarKnob::tick()
{
    bool moved = ui::approach (shown, norm, 0.35f);
    moved |= ui::approach (glow, dragging ? 1.0f : (hovering ? 0.6f : 0.0f), 0.16f);
    moved |= ui::approach (cell, hovering || dragging ? 1.0f : 0.0f, 0.16f);
    if (moved) repaint();
}

void StarKnob::paint (juce::Graphics& g)
{
    const float uu = ui::unitOf (*this);
    const auto k = knobArea();
    const float d = k.getWidth(), u = d / 100.0f;
    const auto c = k.getCentre();
    const float alpha = dimmed ? 0.35f : 1.0f;
    const float a = kArc0 + (kArc1 - kArc0) * shown;

    // survol de la cellule : fond blanc .035 et liseré rose .18 (rayon 18)
    if (cell > 0.01f && showLabel && ! faceStyle)
    {
        const auto cr = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.035f * cell));
        g.fillRoundedRectangle (cr, 18.0f * uu);
        g.setColour (juce::Colour (255, 140, 150).withAlpha (0.18f * cell));
        g.drawRoundedRectangle (cr.reduced (0.5f), 18.0f * uu, 1.0f);
    }

    if (showLabel)
    {
        const auto lr = juce::Rectangle<float> (0.0f, 8.0f * uu, (float) getWidth(), (faceStyle ? 18.0f : 13.0f) * uu);
        g.setColour ((faceStyle ? juce::Colour (0xffe9e9ec) : juce::Colour (0xffd9c6c9)).withMultipliedAlpha (alpha));
        g.setFont (faceStyle ? ui::css (ui::Face::display, 13.0f * uu, 0.2f) : ui::css (ui::Face::mono, 10.0f * uu, 0.12f));
        g.drawText (label, lr, juce::Justification::centred, false);
    }

    // survol du potard : lueur braise de 14 autour (drop-shadow de la démo)
    if (glow > 0.01f)
        ui::softGlowEllipse (g, k.reduced (d * 0.1f), juce::Colour (229, 56, 74).withAlpha (0.45f * glow * alpha), 11.0f * u);   // reste dans le potard : pas de bord coupé

    // le potard B (graduations) : 21 repères autour (un grand tous les 5), allumés en braise jusqu'à la valeur, depuis le
    // zéro pour un réglage qui va du négatif au positif ; au centre un capuchon mat et un trait blanc pour la position
    float from = 0.0f;
    if (param != nullptr)
    {
        const auto& range = param->getNormalisableRange();
        if (range.start < 0.0f && range.end > 0.0f) from = range.convertTo0to1 (0.0f);
    }
    const float lo = std::min (from, shown), hi = std::max (from, shown);
    for (int i = 0; i <= 20; ++i)
    {
        const float t = (float) i / 20.0f, ang = kArc0 + (kArc1 - kArc0) * t;
        const bool big = i % 5 == 0, lit = t >= lo - 1.0e-4f && t <= hi + 1.0e-4f && (hi - lo > 0.002f || t == from);
        const float r0 = d * (big ? 0.385f : 0.415f), r1 = d * 0.475f;
        const juce::Line<float> tick (c.x + r0 * std::sin (ang), c.y - r0 * std::cos (ang), c.x + r1 * std::sin (ang), c.y - r1 * std::cos (ang));
        const float w = std::max (1.0f, d * (big ? 0.024f : 0.016f));
        if (lit)
        {
            g.setColour (juce::Colour (229, 56, 74).withAlpha (0.28f * alpha));
            g.drawLine (tick, w * 2.6f);
        }
        g.setColour ((lit ? juce::Colour (0xffE5384A) : juce::Colour (255, 236, 238).withAlpha (0.18f)).withMultipliedAlpha (alpha));
        juce::Path p;
        p.startNewSubPath (tick.getStart()); p.lineTo (tick.getEnd());
        g.strokePath (p, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // le capuchon : ombre douce dessous, mat (#1c1412 -> #120c0d), liseré rosé, reflet léger en haut
    const float br = d * 0.32f;
    const auto cap = juce::Rectangle<float> (2.0f * br, 2.0f * br).withCentre (c);
    for (int q = 3; q >= 1; --q)
    {
        g.setColour (juce::Colours::black.withAlpha (0.16f * alpha));
        g.fillEllipse (cap.translated (0.0f, 2.5f * u).expanded (1.0f * u * (float) q));
    }
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1e1614), c.x, cap.getY(), juce::Colour (0xff110b0c), c.x, cap.getBottom(), false));
    g.setOpacity (alpha);
    g.fillEllipse (cap);
    g.setColour (juce::Colour (255, 220, 225).withAlpha (0.10f * alpha));
    g.drawEllipse (cap.reduced (0.5f), std::max (0.8f, 0.6f * u));
    g.setColour (juce::Colours::white.withAlpha (0.05f * alpha));
    g.drawEllipse (cap.reduced (2.0f * u), std::max (0.6f, 0.5f * u));

    // le trait de position
    // (a : l'angle de la valeur, calculé plus haut)
    juce::Path ptr;
    ptr.startNewSubPath (c.x + d * 0.12f * std::sin (a), c.y - d * 0.12f * std::cos (a));
    ptr.lineTo (c.x + d * 0.27f * std::sin (a), c.y - d * 0.27f * std::cos (a));
    g.setColour (juce::Colour (0xffF4ECEC).withAlpha (alpha));
    g.strokePath (ptr, juce::PathStrokeType (std::max (1.2f, d * 0.026f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (showValue && param != nullptr)
    {
        const auto text = param->getCurrentValueAsText();
        if (faceStyle)
        {
            const auto f = ui::css (ui::Face::mono, 13.0f * uu, 0.12f);
            const float tw = ui::textWidth (f, text) + 28.0f * uu;
            const auto pill = juce::Rectangle<float> (std::min (tw, (float) getWidth() - 4.0f * uu), 30.0f * uu)
                                  .withCentre ({ c.x, std::min (k.getBottom() + (14.0f + 15.0f) * uu, (float) getHeight() - (2.0f + 15.0f) * uu) });   // toujours en entier dans la cellule
            g.setColour (juce::Colours::black.withAlpha (0.45f));
            g.fillRoundedRectangle (pill, 10.0f * uu);
            g.setColour (juce::Colours::white.withAlpha (0.08f));
            g.drawRoundedRectangle (pill.reduced (0.5f), 10.0f * uu, 1.0f);
            g.setColour (juce::Colours::white.withAlpha (alpha));
            g.setFont (f);
            g.drawText (text, pill, juce::Justification::centred, false);
        }
        else
        {
            const auto vr = juce::Rectangle<float> (0.0f, k.getBottom() + 4.0f * uu, (float) getWidth(), 16.0f * uu);
            g.setColour (juce::Colours::white.withAlpha (alpha));
            g.setFont (ui::css (ui::Face::mono, 12.0f * uu, 0.12f));
            g.drawText (text, vr, juce::Justification::centred, false);
        }
    }
}

void StarKnob::mouseEnter (const juce::MouseEvent&) { hovering = true; if (onFocusChange) onFocusChange (true); }
void StarKnob::mouseExit (const juce::MouseEvent&)  { hovering = false; if (onFocusChange && ! dragging) onFocusChange (false); }

void StarKnob::mouseDown (const juce::MouseEvent& e)
{
    dragging = true;
    startNorm = norm;
    startY = e.position.y;
    attachment->beginGesture();
}

void StarKnob::mouseDrag (const juce::MouseEvent& e)
{
    const float span = (e.mods.isShiftDown() ? 900.0f : 220.0f) * ui::unitOf (*this);
    const float n = juce::jlimit (0.0f, 1.0f, startNorm + (startY - e.position.y) / std::max (40.0f, span));
    attachment->setValueAsPartOfGesture (param->convertFrom0to1 (n));
}

void StarKnob::mouseUp (const juce::MouseEvent&)
{
    dragging = false;
    attachment->endGesture();
    if (onFocusChange && ! hovering) onFocusChange (false);
}

void StarKnob::mouseDoubleClick (const juce::MouseEvent&)
{
    attachment->setValueAsCompleteGesture (param->convertFrom0to1 (param->getDefaultValue()));
}

void StarKnob::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const float step = (e.mods.isShiftDown() ? 0.004f : 0.02f) * (w.deltaY > 0.0f ? 1.0f : -1.0f) * (w.isReversed ? -1.0f : 1.0f);
    if (w.deltaY == 0.0f) return;
    attachment->setValueAsCompleteGesture (param->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, norm + step)));
}

//==============================================================================
GlassToggle::GlassToggle (juce::AudioProcessorValueTreeState& s, const juce::String& id, const juce::String& l) : label (l)
{
    param = s.getParameter (id);
    jassert (param != nullptr);
    attachment = std::make_unique<juce::ParameterAttachment> (*param, [this] (float v) { state = v > 0.5f; repaint(); });
    attachment->sendInitialUpdate();
    lit = state ? 1.0f : 0.0f;
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void GlassToggle::tick()
{
    bool moved = ui::approach (lit, state ? 1.0f : 0.0f, 0.16f);
    moved |= ui::approach (hot, hover ? 1.0f : 0.0f, 0.16f);
    if (moved) repaint();
}

// pastille de verre (rayon 14, 4 de marge) avec un .seg dedans (rayon 10, JetBrains Mono 11 espacé de 0.14 em)
void GlassToggle::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    const float u = ui::unitOf (*this);
    ui::drawDemoGlass (g, r, 14.0f * u, u, false);
    const auto seg = r.reduced (4.0f * u);
    ui::drawSeg (g, seg, lit, hot, u);
    g.setColour (juce::Colour (0xffbba7aa).interpolatedWith (juce::Colours::white, std::max (lit, hot)));
    g.setFont (ui::css (ui::Face::mono, 11.0f * u, 0.14f));
    g.drawText (label, seg, juce::Justification::centred, false);
}

void GlassToggle::mouseDown (const juce::MouseEvent&)
{
    attachment->setValueAsCompleteGesture (state ? 0.0f : 1.0f);
}

//==============================================================================
GlassTabs::GlassTabs (juce::AudioProcessorValueTreeState& s, const juce::String& id, const juce::StringArray& l) : labels (l)
{
    param = s.getParameter (id);
    jassert (param != nullptr);
    attachment = std::make_unique<juce::ParameterAttachment> (*param, [this] (float v) { selected = juce::roundToInt (v); repaint(); });
    attachment->sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

// chaque onglet : son texte et 18 de marge de chaque côté (padding: 0 18px), 4 entre deux, 4 autour
float GlassTabs::idealWidth (float) const
{
    const float u = ui::unitOf (*this);
    const auto f = ui::css (ui::Face::mono, 11.0f * u, 0.14f);
    float w = 4.0f * u;
    for (auto& l : labels) w += ui::textWidth (f, l) + 36.0f * u + 4.0f * u;
    return w;
}

juce::Rectangle<float> GlassTabs::segment (int i) const
{
    const float u = ui::unitOf (*this);
    const auto r = getLocalBounds().toFloat().reduced (4.0f * u);
    const auto f = ui::css (ui::Face::mono, 11.0f * u, 0.14f);
    std::vector<float> w;
    float total = 0.0f;
    for (auto& l : labels) { w.push_back (ui::textWidth (f, l) + 36.0f * u); total += w.back(); }
    const float gaps = 4.0f * u * (float) std::max (0, labels.size() - 1);
    const float k = total > 0.0f ? (r.getWidth() - gaps) / total : 1.0f;   // plus de place : les onglets s'élargissent ensemble
    float x = r.getX();
    for (int j = 0; j < i; ++j) x += w[(size_t) j] * k + 4.0f * u;
    return { x, r.getY(), w[(size_t) juce::jlimit (0, (int) w.size() - 1, i)] * k, r.getHeight() };
}

int GlassTabs::at (float x) const noexcept
{
    for (int i = 0; i < labels.size(); ++i)
        if (x < segment (i).getRight() + 2.0f) return i;
    return labels.size() - 1;
}

void GlassTabs::tick()
{
    bool moved = false;
    if (slide < 0.0f) { slide = (float) selected; moved = true; }
    moved |= ui::spring (slide, vel, (float) selected);
    for (int i = 0; i < 16; ++i) moved |= ui::approach (hot[i], i == hover ? 1.0f : 0.0f, 0.16f);
    if (moved) repaint();
}

void GlassTabs::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    const float u = ui::unitOf (*this);
    ui::drawDemoGlass (g, r, round ? r.getHeight() * 0.5f : 14.0f * u, u, false);
    const int n = labels.size();
    if (n == 0) return;
    // la pastille braise glisse d'un onglet à l'autre
    const int i0 = juce::jlimit (0, n - 1, (int) std::floor (slide)), i1 = juce::jlimit (0, n - 1, i0 + 1);
    const float t = juce::jlimit (0.0f, 1.0f, slide - (float) i0);
    const auto a = segment (i0), b = segment (i1);
    const juce::Rectangle<float> pill (a.getX() + (b.getX() - a.getX()) * t, a.getY(), a.getWidth() + (b.getWidth() - a.getWidth()) * t, a.getHeight());
    for (int i = 0; i < n; ++i)
        if (hot[std::min (i, 15)] > 0.01f && i != selected) ui::drawSeg (g, segment (i), 0.0f, hot[std::min (i, 15)], u);
    if (round)
    {
        // la pastille en gélule, sa lueur gardée dans la section (pas de bord coupé)
        const float pr = pill.getHeight() * 0.5f;
        g.setGradientFill (juce::ColourGradient (juce::Colour (229, 56, 74).withAlpha (0.6f), 0.0f, pill.getY(), juce::Colour (110, 10, 24).withAlpha (0.6f), 0.0f, pill.getBottom(), false));
        g.fillRoundedRectangle (pill, pr);
        g.setColour (juce::Colour (255, 120, 135).withAlpha (0.5f));
        g.drawRoundedRectangle (pill.reduced (0.5f), pr, 1.0f);
    }
    else ui::drawSeg (g, pill, 1.0f, 0.0f, u);
    g.setFont (ui::css (ui::Face::mono, 11.0f * u, 0.14f));
    for (int i = 0; i < n; ++i)
    {
        g.setColour (juce::Colour (0xffbba7aa).interpolatedWith (juce::Colours::white, i == selected ? 1.0f : hot[std::min (i, 15)]));
        g.drawText (labels[i], segment (i), juce::Justification::centred, false);
    }
}

void GlassTabs::mouseDown (const juce::MouseEvent& e)
{
    const int i = at (e.position.x);
    if (i == selected) return;
    if (attachment != nullptr) attachment->setValueAsCompleteGesture ((float) i);
    else { selected = i; repaint(); }
    if (onChange) onChange (i);
}

void GlassTabs::mouseMove (const juce::MouseEvent& e)
{
    const int i = at (e.position.x);
    if (i != hover) { hover = i; repaint(); }
}

//==============================================================================
ModulePanel::ModulePanel (OniriqueVocalBoxAudioProcessor& p, int c)
    : code (c), type (modules::typeOf (c)), inst (modules::instanceOf (c)), processor (p)
{
    engine = p.module (c);
}

StarKnob& ModulePanel::knob (const char* id, const juce::String& label)
{
    return own (std::make_unique<StarKnob> (processor.apvts, P (id), label));
}

GlassToggle& ModulePanel::toggle (const char* id, const juce::String& label)
{
    return own (std::make_unique<GlassToggle> (processor.apvts, P (id), label));
}

GlassTabs& ModulePanel::tabs (const char* id, const juce::StringArray& labels)
{
    return own (std::make_unique<GlassTabs> (processor.apvts, P (id), labels));
}

void ModulePanel::layoutKnobs (const std::vector<StarKnob*>& ks, juce::Rectangle<float> r, int cols)
{
    if (ks.empty()) return;
    cols = std::max (1, std::min (cols, (int) ks.size()));
    const int rows = ((int) ks.size() + cols - 1) / cols;
    const float cw = r.getWidth() / (float) cols, rh = r.getHeight() / (float) rows;
    const float size = std::min (cw * 0.92f, rh * 0.98f);
    for (size_t k = 0; k < ks.size(); ++k)
    {
        const int row = (int) k / cols, col = (int) k % cols;
        const int inRow = std::min (cols, (int) ks.size() - row * cols);
        const float x0 = r.getX() + (r.getWidth() - cw * (float) inRow) * 0.5f;
        const auto cell = juce::Rectangle<float> (x0 + cw * (float) col, r.getY() + rh * (float) row, cw, rh);
        ks[k]->setBounds (juce::Rectangle<float> (size * 0.86f, size).withCentre (cell.getCentre()).toNearestInt());
    }
}

//==============================================================================
namespace
{
// Écran par défaut : tous les réglages du module en potards étoile (interrupteurs et réglages de session à part)
class GenericPanel final : public ModulePanel
{
public:
    GenericPanel (OniriqueVocalBoxAudioProcessor& p, int c) : ModulePanel (p, c)
    {
        for (const auto& id : modules::paramIds (type))
        {
            if (id == modules::info (type).onId) continue;
            auto* prm = p.apvts.getParameter (ids::inst (id.toRawUTF8(), inst));
            if (prm == nullptr || ! prm->isAutomatable()) continue;
            if (dynamic_cast<juce::AudioParameterBool*> (prm) != nullptr)
                toggles.push_back (&toggle (id.toRawUTF8(), prm->getName (24).toUpperCase()));
            else
                knobs.push_back (&knob (id.toRawUTF8(), shortName (prm->getName (40))));
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().toFloat();
        const float u = ui::unitOf (*this);
        if (! toggles.empty())
        {
            auto row = r.removeFromTop (40.0f * u);
            float x = row.getX();
            for (auto* t : toggles) { t->setBounds (juce::Rectangle<float> (x, row.getY(), 150.0f * u, 34.0f * u).toNearestInt()); x += 160.0f * u; }
        }
        layoutKnobs (knobs, r.reduced (10.0f * u), knobs.size() > 8 ? 6 : (int) knobs.size());
    }

private:
    // « Reverb Pre-Delay 2 » -> « PRE-DELAY » : le nom du module et le numéro d'exemplaire sont déjà dans le titre
    juce::String shortName (juce::String n) const
    {
        for (const auto* w : { "Parallel Compression ", "Parallel ", "Multiband ", "Reverb ", "Delay ", "Chorus ", "Flanger ", "Gate ",
                               "Resonance ", "Saturation ", "De-Ess ", "Phone ", "Doubler ", "LA-2A ", "1176 " })
            if (n.startsWith (w)) { n = n.fromFirstOccurrenceOf (w, false, false); break; }
        if (inst > 0) n = n.upToLastOccurrenceOf (" ", false, false);
        return n.toUpperCase();
    }
    std::vector<StarKnob*> knobs;
    std::vector<GlassToggle*> toggles;
};
} // namespace

std::unique_ptr<ModulePanel> createGenericPanel (OniriqueVocalBoxAudioProcessor& p, int code)
{
    if (! modules::valid (code)) return nullptr;
    return std::make_unique<GenericPanel> (p, code);
}

//==============================================================================
juce::Font ui::css (Face f, float px, float letterEm)
{
    auto tf = ui::font (f, 12.0f).getTypefacePtr();
    juce::Font fnt = tf != nullptr ? juce::Font (juce::FontOptions (tf).withPointHeight (std::max (1.0f, px)))
                                   : ui::font (f, px * 1.25f);
    if (letterEm != 0.0f && fnt.getHeight() > 0.0f) fnt.setExtraKerningFactor (letterEm * px / fnt.getHeight());
    return fnt;
}

void ui::drawDemoGlass (juce::Graphics& g, juce::Rectangle<float> r, float corner, float u, bool shadow)
{
    if (shadow)   // 0 18px 40px rgba(0,0,0,.45), en quelques couches
        for (int k = 4; k >= 1; --k)
        {
            const float e = 10.0f * u * (float) k;
            g.setColour (juce::Colours::black.withAlpha (0.10f / (float) k));
            g.fillRoundedRectangle (r.translated (0.0f, 18.0f * u * 0.6f).expanded (e * 0.5f), corner + e * 0.5f);
        }
    // linear-gradient(160deg, .075, .02 40%, .035)
    const auto a = r.getTopLeft(), b = r.getBottomRight();
    juce::ColourGradient gr (juce::Colours::white.withAlpha (0.075f), a.x + r.getWidth() * 0.32f, a.y,
                             juce::Colours::white.withAlpha (0.035f), b.x - r.getWidth() * 0.32f, b.y, false);
    gr.addColour (0.4, juce::Colours::white.withAlpha (0.02f));
    g.setGradientFill (gr);
    g.fillRoundedRectangle (r, corner);
    g.setColour (juce::Colour (0xffffebee).withAlpha (0.10f));
    g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
    juce::Graphics::ScopedSaveState s (g);
    juce::Path clip; clip.addRoundedRectangle (r, corner);
    g.reduceClipRegion (clip);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.fillRect (r.withHeight (std::max (1.0f, u)).translated (0.0f, 1.0f));
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillRect (r.withTop (r.getBottom() - std::max (1.0f, u) - 1.0f).withHeight (std::max (1.0f, u)));
}

// le .seg de la démo : rayon 10 ; allumé : dégradé braise (.6), liseré rose (.5) et lueur de 18 ; survol : blanc .05
void ui::softGlow (juce::Graphics& g, const juce::Path& p, juce::Colour c, float radius, float dy)
{
    if (c.getAlpha() == 0 || radius < 0.5f) return;
    juce::DropShadow (c, std::max (1, juce::roundToInt (radius)), { 0, juce::roundToInt (dy) }).drawForPath (g, p);
}
void ui::softGlowRect (juce::Graphics& g, juce::Rectangle<float> r, float corner, juce::Colour c, float radius, float dy)
{
    juce::Path p; p.addRoundedRectangle (r, corner);
    softGlow (g, p, c, radius, dy);
}
void ui::softGlowEllipse (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, float radius, float dy)
{
    juce::Path p; p.addEllipse (r);
    softGlow (g, p, c, radius, dy);
}

void ui::drawSeg (juce::Graphics& g, juce::Rectangle<float> r, float lit, float hover, float u)
{
    const float rad = 10.0f * u;
    if (hover > 0.01f && lit < 0.99f)
    {
        g.setColour (juce::Colours::white.withAlpha (0.05f * hover * (1.0f - lit)));
        g.fillRoundedRectangle (r, rad);
    }
    if (lit <= 0.01f) return;
    ui::softGlowRect (g, r, rad, juce::Colour (229, 56, 74).withAlpha (0.55f * lit), 18.0f * u);
    g.setGradientFill (juce::ColourGradient (juce::Colour (229, 56, 74).withAlpha (0.6f * lit), 0.0f, r.getY(),
                                             juce::Colour (110, 10, 24).withAlpha (0.6f * lit), 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle (r, rad);
    g.setColour (juce::Colour (255, 120, 135).withAlpha (0.5f * lit));
    g.drawRoundedRectangle (r.reduced (0.5f), rad, 1.0f);
}

float GlassToggle::idealWidth() const
{
    const float u = ui::unitOf (*this);
    return ui::textWidth (ui::css (ui::Face::mono, 11.0f * u, 0.14f), label) + 36.0f * u + 8.0f * u;
}
