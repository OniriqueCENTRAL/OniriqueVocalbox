#pragma once

#include "Widgets.h"

namespace onirique::ui
{
// échelle de la nouvelle interface : 1 = 1600 points de large (la fenêtre par défaut en fait 1900)
float unitOf (const juce::Component&);

// l'étoile à quatre branches du logo ONIRIQUE, côtés creusés, centrée en c, de rayon r, tournée de rot (radians)
juce::Path starPath (juce::Point<float> c, float r, float rot = 0.0f);

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StarKnob)
};

// Pastille de verre à libellé reliée à un réglage marche / arrêt (PING-PONG, SYNC, SOLO...) : braise quand elle est allumée.
class GlassToggle : public juce::Component, public Animated
{
public:
    GlassToggle (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& label);
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
    void paint (juce::Graphics&) override;
    void tick() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    float idealWidth (float height) const;

private:
    int at (float x) const noexcept;
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    juce::StringArray labels;
    int selected = 0, hover = -1;
    float slide = -1.0f, vel = 0.0f;
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

protected:
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

std::unique_ptr<ModulePanel> createModulePanel (OniriqueVocalBoxAudioProcessor&, int code);
