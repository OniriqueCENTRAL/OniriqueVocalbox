#pragma once

#include "Widgets.h"

namespace onirique::ui
{
// échelle de la nouvelle interface : 1 = 1600 points de large (la fenêtre par défaut en fait 1900)
float unitOf (const juce::Component&);

// l'étoile à quatre branches du logo ONIRIQUE, côtés creusés, centrée en c, de rayon r, tournée de rot (radians)
juce::Path starPath (juce::Point<float> c, float r, float rot = 0.0f);

// police comme en CSS (la démo) : px = font-size (le corps de la lettre, pas la hauteur de ligne), letterEm = letter-spacing en em
juce::Font css (Face, float px, float letterEm = 0.0f);

// le verre de la démo (.glass) : dégradé, liseré, reflet haut, filet sombre bas et ombre portée douce
void drawDemoGlass (juce::Graphics&, juce::Rectangle<float>, float corner, float u, bool shadow = true);

// un onglet / bouton segmenté de la démo (.seg), allumé (lit) ou survolé
void drawSeg (juce::Graphics&, juce::Rectangle<float>, float lit, float hover, float u);
// une lueur floue et douce (box-shadow / drop-shadow de la démo), sans les anneaux en marches
void softGlow (juce::Graphics&, const juce::Path&, juce::Colour, float radius, float dy = 0.0f);
void softGlowRect (juce::Graphics&, juce::Rectangle<float>, float corner, juce::Colour, float radius, float dy = 0.0f);
void softGlowEllipse (juce::Graphics&, juce::Rectangle<float>, juce::Colour, float radius, float dy = 0.0f);

// verre sombre : le panneau de la nouvelle interface (fond translucide, liseré clair, reflet en haut)
void drawGlass (juce::Graphics&, juce::Rectangle<float>, float corner, float u, float lit = 0.0f);
} // namespace onirique::ui

//==============================================================================
/*  Le potard « Étoile » (celui de la page des potards ONIRIQUE) : un arc fin qui s'allume en braise jusqu'à la valeur, une
    petite étoile qui glisse sur l'arc, et au centre du corps sombre la grande étoile du logo qui tourne avec le réglage.
    Nom au-dessus, valeur dessous. Glisser verticalement (Maj : fin), molette, double-clic = valeur d'usine. */
class StarKnob : public juce::Component, public juce::SettableTooltipClient, public Animated
{
public:
    StarKnob (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& label);
    ~StarKnob() override;

    void setShowText (bool label, bool value) { showLabel = label; showValue = value; repaint(); }
    void setDimmed (bool d) { if (d != dimmed) { dimmed = d; repaint(); } }
    // façade de compresseur : nom en Unbounded espacé au-dessus, valeur dans une pastille
    void setFaceStyle (bool f) { faceStyle = f; repaint(); }
    std::function<void (bool)> onFocusChange;

    void paint (juce::Graphics&) override;
    void tick() override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    // le disque du potard (pour aligner un dessin autour)
    juce::Rectangle<float> knobArea() const noexcept;

private:
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    juce::String label;
    float norm = 0.0f, shown = 0.0f, glow = 0.0f, startNorm = 0.0f, startY = 0.0f;
    bool hovering = false, dragging = false, showLabel = true, showValue = true, dimmed = false;
    bool faceStyle = false;
    float cell = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StarKnob)
};

// Pastille de verre à libellé reliée à un réglage marche / arrêt (PING-PONG, SYNC, SOLO...) : braise quand elle est allumée.
class GlassToggle : public juce::Component, public Animated
{
public:
    GlassToggle (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& label);
    float idealWidth() const;   // le texte, 18 de marge de chaque côté, 4 autour
    void paint (juce::Graphics&) override;
    void tick() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true; }
    void mouseExit (const juce::MouseEvent&) override { hover = false; }

private:
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    juce::String label;
    bool state = false, hover = false;
    float lit = 0.0f, hot = 0.0f;
};

// Onglets de verre reliés à un réglage à choix (modes de la reverb, de la saturation, valeur de note...) : la pastille braise
// glisse vers le choix.
class GlassTabs : public juce::Component, public Animated
{
public:
    GlassTabs (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::StringArray& labels);
    // sans réglage : un choix de l'interface (la bande affichée du MULTIBAND...)
    explicit GlassTabs (const juce::StringArray& labels) : labels (labels) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    bool round = false;   // en gélule (bords tout ronds), comme la section OS
    std::function<void (int)> onChange;
    void setSelected (int i) { if (i != selected) { selected = i; repaint(); } }
    int getSelected() const noexcept { return selected; }
    void paint (juce::Graphics&) override;
    void tick() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    float idealWidth (float height) const;

private:
    int at (float x) const noexcept;
    juce::Rectangle<float> segment (int) const;
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    juce::StringArray labels;
    int selected = 0, hover = -1;
    float slide = -1.0f, vel = 0.0f;
    float hot[16] {};
};

//==============================================================================
/*  Le contenu d'un module, sur toute la place du panneau : chaque type a son écran (le 1176 et le LA-2A leur façade, les
    nettoyages leur forme d'onde avec ce qui est retiré, l'EQ celui d'ONIRIQUE EQ...). */
class ModulePanel : public juce::Component, public Animated
{
public:
    ModulePanel (OniriqueVocalBoxAudioProcessor&, int code);
    void tick() override { for (auto* a : anim) a->tick(); }
    const int code, type, inst;

    // pour composer un écran (ModuleScreens.cpp)
    void keep (std::unique_ptr<juce::Component> c) { owned.push_back (std::move (c)); }   // gardé, pas affiché (il vit dans un cadre)
    void addAnim (Animated* a) { anim.push_back (a); }
    juce::String P (const char* id) const { return onirique::ids::inst (id, inst); }
    float value (const char* id) const { return processor.apvts.getRawParameterValue (P (id))->load(); }
    StarKnob& knob (const char* id, const juce::String& label);
    GlassToggle& toggle (const char* id, const juce::String& label);
    GlassTabs& tabs (const char* id, const juce::StringArray& labels);
    template <typename C> C& own (std::unique_ptr<C> c)
    {
        auto& r = *c;
        addAndMakeVisible (r);
        if (auto* a = dynamic_cast<Animated*> (&r)) anim.push_back (a);
        owned.push_back (std::move (c));
        return r;
    }
    // une rangée de potards dans r (jusqu'à cols par rangée)
    static void layoutKnobs (const std::vector<StarKnob*>&, juce::Rectangle<float> r, int cols);

    OniriqueVocalBoxAudioProcessor& processor;
    onirique::ChainModule* engine = nullptr;
    std::vector<Animated*> anim;
    std::vector<std::unique_ptr<juce::Component>> owned;
};

std::unique_ptr<ModulePanel> createModulePanel (OniriqueVocalBoxAudioProcessor&, int code);   // l'écran du module (ModuleScreens.cpp)
std::unique_ptr<ModulePanel> createGenericPanel (OniriqueVocalBoxAudioProcessor&, int code);  // tous ses réglages en potards
