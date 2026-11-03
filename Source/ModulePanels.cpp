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

juce::Rectangle<float> StarKnob::knobArea() const noexcept
{
    auto r = getLocalBounds().toFloat();
    const float u = r.getWidth() / 110.0f;
    if (showLabel) r.removeFromTop (std::min (20.0f * u, r.getHeight() * 0.16f));
    if (showValue) r.removeFromBottom (std::min (22.0f * u, r.getHeight() * 0.18f));
    const float d = std::min (r.getWidth(), r.getHeight());
    return juce::Rectangle<float> (d, d).withCentre (r.getCentre());
}

void StarKnob::tick()
{
    bool moved = ui::approach (shown, norm, 0.35f);
    moved |= ui::approach (glow, dragging ? 1.0f : (hovering ? 0.6f : 0.0f), 0.18f);
    if (moved) repaint();
}

void StarKnob::paint (juce::Graphics& g)
{
    const auto k = knobArea();
    const float d = k.getWidth(), u = d / 100.0f;
    const auto c = k.getCentre();
    const float alpha = dimmed ? 0.35f : 1.0f;
    const float a = kArc0 + (kArc1 - kArc0) * shown;

    if (showLabel)
    {
        const auto lr = getLocalBounds().toFloat().withBottom (k.getY());
        g.setColour (ui::mist.withAlpha (0.85f * alpha));
        g.setFont (ui::fitFont (ui::Face::mono, std::max (8.0f, std::min (13.0f * u + 2.0f, lr.getHeight() * 0.8f)), lr.getWidth(), label));
        g.drawText (label, lr, juce::Justification::centred, false);
    }

    const float R = d * 0.44f, w = std::max (1.2f, d * 0.03f);
    juce::Path track, arc;
    track.addCentredArc (c.x, c.y, R, R, 0.0f, kArc0, kArc1, true);
    g.setColour (juce::Colour (0xffffecee).withAlpha (0.12f * alpha));
    g.strokePath (track, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (shown > 0.002f)
    {
        arc.addCentredArc (c.x, c.y, R, R, 0.0f, kArc0, a, true);
        ui::strokeGlow (g, arc, ui::ember, w, (0.55f + 0.45f * glow) * alpha);
        g.setColour (ui::ember.withAlpha (alpha));
        g.strokePath (arc, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // le corps sombre et son ombre
    const float br = d * 0.326f;
    g.setColour (juce::Colours::black.withAlpha (0.55f * alpha));
    g.fillEllipse (juce::Rectangle<float> (2.0f * br, 2.0f * br).withCentre (c.translated (0.0f, 2.5f * u)).expanded (1.5f * u));
    juce::ColourGradient body (juce::Colour (0xff36161a), c.x - br * 0.35f, c.y - br * 0.4f, juce::Colour (0xff080405), c.x + br, c.y + br, true);
    body.addColour (0.6, juce::Colour (0xff160a0c));
    g.setGradientFill (body);
    g.setOpacity (alpha);
    g.fillEllipse (juce::Rectangle<float> (2.0f * br, 2.0f * br).withCentre (c));
    g.setColour (juce::Colour (0xffffdce1).withAlpha (0.08f + 0.12f * glow));
    g.drawEllipse (juce::Rectangle<float> (2.0f * br, 2.0f * br).withCentre (c), std::max (1.0f, 0.5f * u));

    // les deux étoiles : la petite sur l'arc, la grande au centre (elles tournent avec la valeur)
    const auto tip = juce::Point<float> (c.x + R * std::sin (a), c.y - R * std::cos (a));
    const auto small = ui::starPath (tip, d * 0.045f, a);
    const auto big = ui::starPath (c, d * 0.194f, a);
    for (const auto* p : { &small, &big })
    {
        g.setColour (ui::bone.withAlpha ((0.20f + 0.15f * glow) * alpha));
        g.strokePath (*p, juce::PathStrokeType (3.0f * u, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (juce::Colour (0xffffecee).withAlpha (alpha));
        g.fillPath (*p);
    }

    if (showValue && param != nullptr)
    {
        const auto vr = getLocalBounds().toFloat().withTop (k.getBottom());
        const auto text = param->getCurrentValueAsText();
        g.setColour (ui::bone.withAlpha (alpha));
        g.setFont (ui::fitFont (ui::Face::mono, std::max (8.0f, std::min (14.0f * u + 2.0f, vr.getHeight() * 0.8f)), vr.getWidth(), text));
        g.drawText (text, vr, juce::Justification::centred, false);
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
    bool moved = ui::approach (lit, state ? 1.0f : 0.0f, 0.2f);
    moved |= ui::approach (hot, hover ? 1.0f : 0.0f, 0.2f);
    if (moved) repaint();
}

void GlassToggle::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const float u = ui::unitOf (*this), rad = r.getHeight() * 0.32f;
    ui::drawGlass (g, r, rad, u, 0.0f);
    if (lit > 0.01f)
    {
        g.setGradientFill (juce::ColourGradient (ui::ember.withAlpha (0.62f * lit), r.getX(), r.getY(), juce::Colour (0xff6e0a18).withAlpha (0.62f * lit),
                                                 r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, rad);
        g.setColour (juce::Colour (0xffff7887).withAlpha (0.5f * lit));
        g.drawRoundedRectangle (r.reduced (0.5f), rad, 1.0f);
    }
    g.setColour (ui::mist.withAlpha (0.65f + 0.35f * std::max (lit, hot)));
    g.setFont (ui::fitFont (ui::Face::mono, r.getHeight() * 0.36f, r.getWidth() * 0.86f, label));
    g.drawText (label, r, juce::Justification::centred, false);
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

float GlassTabs::idealWidth (float h) const
{
    float w = 0.0f;
    const auto f = ui::font (ui::Face::mono, h * 0.34f);
    for (auto& l : labels) w += ui::textWidth (f, l) + h * 0.9f;
    return w + h * 0.25f;
}

int GlassTabs::at (float x) const noexcept
{
    const float w = (float) getWidth() / (float) std::max (1, labels.size());
    return juce::jlimit (0, labels.size() - 1, (int) (x / w));
}

void GlassTabs::tick()
{
    if (slide < 0.0f) { slide = (float) selected; repaint(); return; }
    if (ui::spring (slide, vel, (float) selected)) repaint();
}

void GlassTabs::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const float u = ui::unitOf (*this), rad = r.getHeight() * 0.32f;
    ui::drawGlass (g, r, rad, u);
    const int n = std::max (1, labels.size());
    const float w = (r.getWidth() - 6.0f * u) / (float) n;
    auto seg = [&] (float i) { return juce::Rectangle<float> (r.getX() + 3.0f * u + i * w, r.getY() + 3.0f * u, w, r.getHeight() - 6.0f * u); };
    const auto pill = seg (std::max (0.0f, slide));
    g.setGradientFill (juce::ColourGradient (ui::ember.withAlpha (0.62f), pill.getX(), pill.getY(), juce::Colour (0xff6e0a18).withAlpha (0.62f),
                                             pill.getX(), pill.getBottom(), false));
    g.fillRoundedRectangle (pill, rad * 0.8f);
    g.setColour (juce::Colour (0xffff7887).withAlpha (0.5f));
    g.drawRoundedRectangle (pill.reduced (0.5f), rad * 0.8f, 1.0f);
    for (int i = 0; i < n; ++i)
    {
        const auto sr = seg ((float) i);
        g.setColour (i == selected ? ui::bone : (i == hover ? ui::mist : ui::haze));
        g.setFont (ui::fitFont (ui::Face::mono, r.getHeight() * 0.34f, sr.getWidth() * 0.9f, labels[i]));
        g.drawText (labels[i], sr, juce::Justification::centred, false);
    }
}

void GlassTabs::mouseDown (const juce::MouseEvent& e)
{
    const int i = at (e.position.x);
    if (i != selected) attachment->setValueAsCompleteGesture ((float) i);
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

std::unique_ptr<ModulePanel> createModulePanel (OniriqueVocalBoxAudioProcessor& p, int code)
{
    if (! modules::valid (code)) return nullptr;
    return std::make_unique<GenericPanel> (p, code);
}
