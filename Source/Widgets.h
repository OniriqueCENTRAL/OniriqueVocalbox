#pragma once

#include "License/LicenseOverlay.h"
#include "PluginProcessor.h"
#include "MultibandPresets.h"
#include "SpectrumSmoother.h"
#include "ui/OniriqueBar.h"
#include "ui/OniriqueTheme.h"

#include <array>
#include <map>

namespace onirique::ui
{
// ---- VOCAL MINI PRO : le noir et rouge d'ONIRIQUE, en plus moderne : noir profond, charbon neutre, rouge sombre pour la
// profondeur ; le rouge vif est l'accent, utilisé avec mesure (arcs, témoins, choix actif) ; rose pâle pour les reflets ;
// blanc cassé pour les textes, gris doux pour le secondaire. Pas de néon : des lueurs discrètes, du verre sombre.
// (Les noms historiques sont gardés : « ember » est l'accent, « lilac » le reflet clair.)
inline const juce::Colour night  { 0xff09080A };   // noir profond (fond)
inline const juce::Colour& blood = onirique::look::litBottom;   // rouge sombre (dégradés, profondeur)
inline const juce::Colour deep   { 0xff121012 };   // charbon très sombre (panneaux)
inline const juce::Colour deep2  { 0xff1C1517 };   // charbon chaud (menus, info-bulles, panneaux ouverts)
inline const juce::Colour mist   { 0xffF2ECEC };   // blanc cassé (textes)
inline const juce::Colour haze   { 0xffAEA3A6 };   // gris doux (informations secondaires) : assez clair pour se lire au premier regard
inline const juce::Colour& ember = onirique::look::ember;   // rouge ONIRIQUE (accent)  (la couleur de l'interface)
inline const juce::Colour& lilac = onirique::look::emberPink;   // rose pâle (queues de reverb, reflets, extrémités d'arcs)
inline const juce::Colour bone   { 0xffFFF4F5 };   // blanc le plus clair (aiguilles, crêtes)
inline const juce::Colour mint   { 0xff7DE3B6 };   // témoin discret « ça agit » (comme le voyant d'un rack)
inline const juce::Colour line   { (juce::uint8) 255, (juce::uint8) 214, (juce::uint8) 218, 0.10f };

// Polices : display (titres), body (libellés), mono (valeurs) : Unbounded, Hanken Grotesk et JetBrains Mono, les polices
// du site ONIRIQUE (fournies avec le module de licence). Elles sont chargées par FontScope (une par interface ouverte,
// rien de statique qui survive au déchargement du plugin) ; sans interface ouverte, des équivalents système.
enum class Face { display, body, mono };
juce::Font font (Face, float height, bool bold = false, float kerning = 0.0f);
struct FontScope
{
    FontScope();
    ~FontScope();
    JUCE_DECLARE_NON_COPYABLE (FontScope)
};
float textWidth (const juce::Font&, const juce::String&);
// police de hauteur maxH, réduite si nécessaire pour que le texte tienne dans maxW (jamais coupé)
juce::Font fitFont (Face, float maxH, float maxW, const juce::String&, bool bold = false, float kerning = 0.0f);

// échelle de la fenêtre (1 = 1480 px de large) : sert de PLANCHER aux tailles de texte, pour qu'un petit bouton
// garde des libellés lisibles
float editorScale (const juce::Component&);

// trait lumineux : plusieurs passes de plus en plus larges et transparentes sous le trait net
void strokeGlow (juce::Graphics&, const juce::Path&, juce::Colour, float width, float intensity);

// ombre portée d'un rectangle arrondi (corner >= 0) ou d'une ellipse (corner < 0) : même rendu que
// juce::DropShadow::drawForPath, mais le flou est calculé une seule fois par forme puis gardé en cache
// (sur Mac, Core Graphics redessine de grandes zones à chaque image : recalculer les flous y faisait ramer l'interface).
// Le cache appartient à l'interface (jamais statique) : ses images disparaissent avec elle, avant que l'hôte
// décharge le plugin (un cache statique libéré au déchargement de la DLL bloquait FL Studio à la suppression).
struct ShadowCache
{
    std::map<std::array<int, 8>, juce::Image> images;
};
void drawCachedShadow (juce::Graphics&, ShadowCache&, const juce::DropShadow&, juce::Rectangle<float> shape, float corner);

// approche exponentielle d'une valeur animée ; renvoie true tant qu'elle bouge
inline bool approach (float& v, float target, float k) noexcept
{
    if (std::abs (target - v) < 1.0e-3f) { const bool moved = v != target; v = target; return moved; }
    v += k * (target - v);
    return true;
}

// ressort légèrement sous-amorti (glissements de pastilles, d'onglets, de pastilles d'ordre) : part vite, dépasse à peine
// (~3 %), se pose sans à-coup. vel : vitesse gardée entre deux images (60 Hz).
inline bool spring (float& v, float& vel, float target, float stiffness = 0.16f, float damping = 0.70f) noexcept
{
    vel = vel * damping + (target - v) * stiffness;
    v += vel;
    if (std::abs (target - v) < 1.0e-3f && std::abs (vel) < 1.0e-3f) { const bool moved = v != target; v = target; vel = 0.0f; return moved; }
    return true;
}
} // namespace onirique::ui

// Un écran ou un réglage d'un exemplaire de module (0 = le premier) : P (id) donne l'identifiant de son réglage.
struct InstanceBound
{
    explicit InstanceBound (int k) : inst (k) {}
    juce::String P (const char* id) const { return onirique::ids::inst (id, inst); }
    const int inst;
};

// Tout ce qui s'anime reçoit tick() à 60 Hz depuis l'éditeur (une seule horloge pour toute l'interface).
struct Animated
{
    virtual ~Animated() = default;
    virtual void tick() = 0;
};

// Pages plein écran (EQ, FX) : fondu + léger zoom depuis le centre, dans le temps (et non image par image) : ouverture
// en ease-out (260 ms), fermeture en ease-in (180 ms). Rouvrir pendant une fermeture repart de là où elle en est.
// La transition ne redessine rien de vivant : au départ, on photographie une fois le plugin dessous et la page, puis
// un calque opaque mêle ces deux images à chaque image (deux copies d'image : 60 images/s même en très grand, sur
// Windows comme sur Mac). La vraie page reste cachée pendant la transition et prend le relais à la fin.
class OverlayMotion
{
public:
    void show (juce::Component& c, bool shouldShow)
    {
        if (shouldShow == target && c.isVisible() == shouldShow && ! running) return;
        auto* parent = c.getParentComponent();
        if (parent == nullptr || c.getWidth() <= 0 || c.getHeight() <= 0)   // pas encore en place : sans transition
        {
            target = shouldShow; value = shouldShow ? 1.0f : 0.0f; running = false;
            c.setVisible (shouldShow);
            if (shouldShow) c.toFront (false);
            return;
        }
        target = shouldShow;
        from = value;
        if (! running)   // nouvelle transition : les deux photos (une reprise en cours garde les siennes)
        {
            float sc = juce::Component::getApproximateScaleFactorForComponent (&c);   // pixels de l'écran (Retina, 150 %...)
            if (auto* d = juce::Desktop::getInstance().getDisplays().getDisplayForRect (c.getScreenBounds())) sc *= (float) d->scale;
            sc = juce::jlimit (1.0f, 4.0f, sc);
            c.setVisible (false);
            ghost.setVisible (false);
            ghost.back  = snapshot (*parent, c.getBounds(), sc);
            ghost.front = snapshot (c, c.getLocalBounds(), sc);
        }
        start = juce::Time::getMillisecondCounterHiRes();   // après les photos : la première image du mouvement est bien montrée
        running = true;
        c.setVisible (false);
        if (ghost.getParentComponent() != parent) parent->addChildComponent (ghost);
        ghost.setBounds (c.getBounds());
        ghost.value = value;
        ghost.setVisible (true);
        ghost.toFront (false);
        ghost.repaint();
    }
    // renvoie true tant que la transition tourne (la vraie page est alors cachée derrière le calque)
    bool tick (juce::Component& c)
    {
        if (! running) return false;
        const float t = (float) juce::jlimit (0.0, 1.0, (juce::Time::getMillisecondCounterHiRes() - start) / (target ? 260.0 : 180.0));
        const float e = target ? 1.0f - std::pow (1.0f - t, 3.0f) : t * t * t;
        value = target ? from + (1.0f - from) * e : from * (1.0f - e);
        ghost.value = value;
        ghost.repaint();
        if (t >= 1.0f)
        {
            running = false;
            if (target) { c.setVisible (true); c.toFront (false); if (c.getWantsKeyboardFocus()) c.grabKeyboardFocus(); }
            ghost.setVisible (false);
            ghost.back = {};
            ghost.front = {};
        }
        return true;
    }
    bool isAnimating() const noexcept { return running; }
    bool isShown() const noexcept { return target; }

private:
    // photo d'une zone d'un composant, en ARGB (createComponentSnapshot prend du RGB pour un composant opaque, où
    // recopier les calques ARGB de l'interface est bien plus lent)
    static juce::Image snapshot (juce::Component& comp, juce::Rectangle<int> area, float sc)
    {
        juce::Image im (juce::Image::ARGB, juce::roundToInt (sc * (float) area.getWidth()), juce::roundToInt (sc * (float) area.getHeight()), true);
        juce::Graphics g (im);
        g.addTransform (juce::AffineTransform::scale (sc));
        g.setOrigin (-area.getPosition());
        comp.paintEntireComponent (g, true);
        return im;
    }
    struct Ghost : juce::Component
    {
        Ghost() { setOpaque (true); }
        void paint (juce::Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat();
            if (back.isValid()) g.drawImage (back, r);
            if (! front.isValid() || value <= 0.0f) return;
            const float s = 0.965f + 0.035f * value;
            g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
            g.setOpacity (juce::jlimit (0.0f, 1.0f, value));
            g.drawImageTransformed (front, juce::AffineTransform::scale (r.getWidth() / (float) front.getWidth(), r.getHeight() / (float) front.getHeight())
                                              .scaled (s, s, r.getCentreX(), r.getCentreY()));
        }
        juce::Image back, front;
        float value = 0.0f;
    };
    Ghost ghost;
    double start = 0.0;
    float from = 0.0f, value = 0.0f;
    bool target = false, running = false;
};

class OniriqueLookAndFeel : public juce::LookAndFeel_V4
{
public:
    OniriqueLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    int getSliderThumbRadius (juce::Slider&) override;
    juce::Font getPopupMenuFont() override { return onirique::ui::font (onirique::ui::Face::body, 15.0f); }

    // menus (presets) : verre sombre arrondi, pastille braise au survol, point braise sur l'élément actif
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                            bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                            const juce::String& shortcutKeyText, const juce::Drawable* icon, const juce::Colour* textColour) override;
    void drawPopupMenuSectionHeader (juce::Graphics&, const juce::Rectangle<int>& area, const juce::String& name) override;
    void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardMenuItemHeight,
                                    int& idealWidth, int& idealHeight) override;
    int getPopupMenuBorderSize() override { return 7; }

private:
    onirique::ui::ShadowCache shadows;   // ombres des boutons et des curseurs
};

// Bouton rotatif (ou curseur vertical, Layout::fader) complet : nom + contrôle + valeur dans une
// pastille. L'arc lumineux suit la valeur en glissant ; il s'illumine au survol et pendant le réglage.
class KnobComponent : public juce::Component, public Animated
{
public:
    enum class Layout { textBelow, textRight, fader, horizontal };   // horizontal : NOM  ━━●━━  [valeur]

    KnobComponent (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& title,
                   const juce::String& minLabel, const juce::String& maxLabel, Layout layout,
                   const juce::String& tooltip, juce::LookAndFeel& lnf, bool bipolar = false);
    ~KnobComponent() override;

    // taille de référence max (diamètre d'un bouton) : aligne un curseur sur les boutons voisins
    void setMaxDiameter (float d) { maxD = d; resized(); }
    // appelé quand le survol / réglage commence ou finit (surbrillance de la bande dans l'afficheur)
    std::function<void (bool)> onFocusChange;
    // activité (ex. réduction en dB) : une LED braise s'allume à côté du nom quand elle dépasse 0.5
    std::function<float()> activity;

    void paint (juce::Graphics&) override;
    void resized() override;
    void tick() override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    void setFocus (bool hover, bool drag);

    juce::Slider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    juce::String title;
    Layout layout;
    bool dragging = false, hovering = false;
    float shown = 0.0f, glow = 0.0f, act = 0.0f;
    juce::Rectangle<float> nameArea, valueArea;
    float unit = 1.0f, maxD = 1.0e6f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KnobComponent)
};

// Valeur réglable dans une pastille (glisser verticalement, molette, double-clic = défaut).
class DragValue : public juce::Component, public juce::SettableTooltipClient, public Animated
{
public:
    DragValue (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& tooltip);
    std::function<void (bool)> onFocusChange;
    void setPrefix (const juce::String& p) { prefix = p; repaint(); }   // ex. "GAIN " devant la valeur

    void paint (juce::Graphics&) override;
    void tick() override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    float startNorm = 0.0f, glow = 0.0f;
    juce::String prefix;
    bool hovering = false, dragging = false;
};

// Bouton power d'un module : anneau braise lumineux quand le module est actif.
class PowerButton : public juce::ToggleButton, public Animated
{
public:
    PowerButton() { setClickingTogglesState (true); }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    void tick() override;

private:
    float on = 1.0f, ripple = 0.0f;   // ripple : onde qui part du bouton quand on l'allume
    bool was = true;
};

// Interrupteur : grand (AUTO GAIN, ON/OFF + correction en dB) ou petit (BYPASS avec LED)
class PillToggle : public juce::ToggleButton, public Animated
{
public:
    enum class Style { big, small, mini };   // mini : interrupteur compact dans une bande de sélecteurs
    PillToggle (const juce::String& label, Style s) : juce::ToggleButton (label), style (s) { setClickingTogglesState (true); }

    void setCaption (const juce::String& c) { if (c != caption) { caption = c; repaint(); } }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    void tick() override;

private:
    Style style;
    juce::String caption;
    float on = 0.0f, ripple = 0.0f;
    bool was = true;
};

// Sélecteur segmenté relié à un paramètre à choix ; la pastille braise glisse vers le choix.
class SegmentSelector : public juce::Component, public juce::TooltipClient, public Animated
{
public:
    SegmentSelector (juce::AudioProcessorValueTreeState&, const juce::String& paramId,
                     const juce::StringArray& labels, const juce::StringArray& tooltips);

    void paint (juce::Graphics&) override;
    void tick() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    juce::String getTooltip() override;

    // titre court à gauche, dans la pastille (ex. "OVERSAMPLING")
    void setTitle (const juce::String& t, const juce::String& tip = {}) { title = t; titleTip = tip; repaint(); }
    // verrouillé : estompé, clics ignorés, message par-dessus et info-bulle d'explication
    void setLocked (bool locked, const juce::String& text, const juce::String& tip);

private:
    int segmentAt (float x) const noexcept;
    juce::Rectangle<float> segmentArea() const noexcept;

    juce::StringArray labels, tips;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    int selected = 0, hover = -1;
    float slide = -1.0f, slideVel = 0.0f, lockAmt = 0.0f;
    juce::String title, titleTip, lockedText, lockedTip;
    bool locked = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SegmentSelector)
};

// Afficheur interactif du module RESONANCE :
//  - spectre d'entrée, réduction appliquée (descend depuis le haut, dégradé braise) ;
//  - les 3 bandes LOW / MID / HIGH dessinées comme zones lumineuses dont la hauteur suit la
//    sensibilité : on voit exactement quelles fréquences sont ciblées ;
//  - poignée par bande : glisser horizontalement = fréquence, verticalement = sensibilité,
//    molette = largeur, double-clic = valeurs par défaut.
class ResonanceDisplay : public InstanceBound, public juce::Component, public juce::TooltipClient, public Animated
{
public:
    static constexpr int kBands = onirique::ResonanceSuppressor::kBands;
    static constexpr int kNumCtl = onirique::ResonanceSuppressor::kNumBands;

    ResonanceDisplay (juce::AudioProcessorValueTreeState&, int instance);

    void updateMeter (const onirique::ResonanceSuppressor::Meter&, bool active);
    void setFocusBand (int band, bool focused);

    void paint (juce::Graphics&) override;
    void tick() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    juce::String getTooltip() override;

private:
    struct Ctl
    {
        juce::RangedAudioParameter *freq = nullptr, *width = nullptr, *sens = nullptr;
        float f = 1000.0f, w = 2.0f, s = 0.0f;   // valeurs affichées (animées)
        float focus = 0.0f;
        bool focused = false;
    };

    juce::Rectangle<float> plotArea() const noexcept;
    float xForHz (float hz, juce::Rectangle<float> r) const noexcept;
    float hzForX (float x, juce::Rectangle<float> r) const noexcept;
    static float value (juce::RangedAudioParameter* p) { return p->convertFrom0to1 (p->getValue()); }
    static void setValue (juce::RangedAudioParameter* p, float v) { p->setValueNotifyingHost (p->convertTo0to1 (v)); }
    juce::Point<float> handlePos (int band, juce::Rectangle<float> plot) const noexcept;
    int handleAt (juce::Point<float> p) const noexcept;

    std::array<Ctl, kNumCtl> ctl;
    std::array<float, kBands> red {}, spectrum {};
    float peakRed = 0.0f;
    bool isActive = true;
    int hoverHandle = -1, dragHandle = -1;
};

// VU-mètre à aiguille de réduction de gain (0 à droite, -20 dB à gauche), balistique VU (~300 ms).
class VuMeter : public juce::Component, public Animated
{
public:
    explicit VuMeter (std::function<float()> source) : read (std::move (source)) {}
    void setActive (bool a) { active = a; }
    void paint (juce::Graphics&) override;
    void tick() override;

private:
    std::function<float()> read;
    float needle = 0.0f, shownDb = 0.0f;
    bool active = true;
};


// Bulle au-dessus du point de la bande sélectionnée : marche / arrêt, fréquence, écoute (casque, maintenir), retirer la
// bande (x) ; Q (et gain) ; pente des coupes (flèches) ou type de la bande (menu). Pointe vers le point de la bande.
class BandBubble : public InstanceBound, public juce::Component, public Animated
{
public:
    BandBubble (juce::AudioProcessorValueTreeState&, std::atomic<bool>& listenHold, int instance);
    void setBand (int b) { if (b != band) { band = b; shownText.clear(); repaint(); } }
    void setPointsDown (bool down) { if (down != pointsDown) { pointsDown = down; resized(); repaint(); } }
    std::function<void()> onRemoved;   // bande retirée (x) : le panneau se ferme
    std::function<void()> onResize;    // le texte a changé de largeur : l'analyseur replace la bulle
    float idealWidth() const;
    // échelle des fréquences de l'analyseur (x dans l'analyseur) : le casque tenu promène la bande en fréquence
    std::function<float (float)> xOfHz, hzOfX;

    void paint (juce::Graphics&) override;
    void resized() override;
    void tick() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    enum Zone { none = -1, power, listen, close, slopeUp, slopeDown, typeMenu, dragFreq, dragGain, dragQ };
    Zone zoneAt (juce::Point<float>) const noexcept;
    float get (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }
    void set (const juce::String& id, float v);

    juce::AudioProcessorValueTreeState& apvts;
    std::atomic<bool>& hold;
    int band = 2, hover = -1;
    bool pointsDown = true, listening = false, listenWas = false;
    bool sweeping = false;
    float sweepStartX = 0.0f;
    // glisser vers le haut / le bas sur la fréquence, le gain ou le Q : la valeur suit (Maj = fin)
    Zone dragging = none;
    float dragFrom = 0.0f;
    juce::Rectangle<float> zGain, zQ;
    juce::String shownText;
    juce::Rectangle<float> box, zPower, zListen, zClose, zUp, zDown, zType, zFreq, zValue, zSlope;
    juce::String valueText() const;
};

// Panneau flottant de la bande sélectionnée (en bas de l'analyseur, plein écran) : marche / arrêt, type et pente à gauche ;
// FREQ, GAIN, Q au centre (le GAIN, plus grand, sous la bosse du panneau) ; bande précédente / suivante, fermer, mode
// dynamique et son seuil à droite. Une rangée de réglages par bande, seule celle de la bande choisie est visible.
class BandPanel : public InstanceBound, public juce::Component, public Animated
{
public:
    BandPanel (juce::AudioProcessorValueTreeState&, int instance);
    ~BandPanel() override;
    void setBand (int b);
    void setOpen (bool shouldOpen);
    bool isOpen() const noexcept { return open; }
    std::function<void (int)> onBand;     // < >
    std::function<void()> onClose;        // x

    void paint (juce::Graphics&) override;
    void resized() override;
    void tick() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    void mouseDown (const juce::MouseEvent&) override;
    bool hitTest (int x, int y) override { return shape.contains ((float) x, (float) y); }

private:
    enum Zone { none = -1, prev, next, close, typeMenu };
    Zone zoneAt (juce::Point<float>) const noexcept;
    float get (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }

    juce::AudioProcessorValueTreeState& apvts;
    OniriqueLookAndFeel lnf;   // les boutons rotatifs du panneau
    struct Set
    {
        std::unique_ptr<KnobComponent> freq, gain, q;
        std::unique_ptr<DragValue> thr;
        PowerButton power;
        PillToggle dyn { "DYN", PillToggle::Style::mini };
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAtt, dynAtt;
    };
    std::array<std::unique_ptr<Set>, 8> sets;
    int band = 2, hover = -1, shownType = -1, shownSlope = -1;
    bool open = false;
    float shown = 0.0f;   // apparition (fondu + léger glissement vers le haut)
    juce::Path shape;
    juce::Rectangle<float> body, zPrev, zNext, zClose, zType, gainCentre;
};

// Analyseur en temps réel + EQ paramétrique : spectres d'entrée et de sortie (FFT 8192, lissage 1/6 d'octave et
// retombée douce, courbes lissées), courbe EXACTE de l'EQ paramétrique (mêmes filtres que l'audio, gain dynamique en
// direct) et, en pointillés, la couleur de l'EQ ANALOG. Les points numérotés se tirent à la souris ; clic droit = menu
// de la bande (type, pente, mode dynamique). fullScreen : version plein écran (plus de repères, étiquettes de bande).
class SpectrumAnalyzer : public InstanceBound, public juce::Component, public juce::TooltipClient, public Animated
{
public:
    SpectrumAnalyzer (OniriqueVocalBoxAudioProcessor&, int instance, bool fullScreen = true);
    ~SpectrumAnalyzer() override;

    // clic dans le vide (pas sur un point) : ferme le panneau de bande
    std::function<void()> onBackgroundClick;
    BandBubble bubble;   // au-dessus du point de la bande sélectionnée
    // clic dans le vide (ici ou ailleurs dans le plugin) : la bulle se ferme ; elle revient dès qu'on choisit une bande
    void closeBubble() noexcept { bubbleClosed = true; }

    // EQ paramétrique : bande sélectionnée (affichée dans la barre de réglage de la carte)
    std::function<void (int)> onSelect;
    int getSelected() const noexcept { return selected; }
    void select (int band);
    void setSelectedSilently (int band) { selected = juce::jlimit (0, kPeq - 1, band); eqDirty = true; }
    // un seul analyseur lit les prises à la fois (celui qui est affiché)
    void setActive (bool a) { active = a; }

    void paint (juce::Graphics&) override;
    void tick() override;
    juce::String getTooltip() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    // menu de bande : type, pente (coupe-bas / coupe-haut), mode dynamique, allumer / couper, remise à zéro
    // le menu s'ouvre à l'endroit du clic (screenPos), plus jamais au bord de l'analyseur
    static void showBandMenu (juce::Component& target, juce::AudioProcessorValueTreeState&, int instance, int band, juce::Point<int> screenPos);

private:
    onirique::chainmods::Peq* peqEngine() const { return dynamic_cast<onirique::chainmods::Peq*> (processor.module (onirique::modules::code (onirique::modules::peq, inst))); }
    static constexpr int kOrder = 13, kN = 1 << kOrder, kPts = 384, kPeq = onirique::ParametricEQ::kBands;
    static constexpr float kEqRange = 20.0f;

    void buildMap (double fs);
    bool analyse (onirique::SpectrumTap&, std::vector<float>& hist, int& writePos, std::array<float, kPts>& spec);   // true : le spectre a bougé
    int tickCount = 0;
    bool updateCurves (double fs);
    void paintGrid (juce::Graphics&);
    void paintEq (juce::Graphics&);
    float hzAt (int i) const noexcept { return 20.0f * std::pow (1000.0f, (float) i / (float) (kPts - 1)); }
    float param (const juce::String& id) const { return processor.apvts.getRawParameterValue (id)->load(); }
    float unit() const noexcept;
    juce::Rectangle<float> plotArea() const noexcept;
    float xOf (float hz) const noexcept;
    float hzOf (float x) const noexcept;
    float yEq (float db) const noexcept;
    float dbOf (float y) const noexcept;
    float ySpec (float db) const noexcept;
    onirique::ParametricEQ::Band band (int b) const;
    juce::Point<float> nodePos (int b) const;
    int nodeAt (juce::Point<float>) const;
    void setParam (const juce::String& id, float v, bool gesture = true);
    juce::Path smoothPath (const std::array<float, kPts>& ys, bool closeToBottom, float bottom, int step = 1) const;
    void flushDrag();

    OniriqueVocalBoxAudioProcessor& processor;
    const bool full;
    bool active = true;
    juce::dsp::FFT fft { kOrder };
    std::vector<float> window, histIn, histOut, scratch, fftBuf;
    onirique::ui::SpectrumSmoother smoother;   // lissage gaussien des spectres, poids calculés une fois
    double mapFs = 0.0;
    int writeIn = 0, writeOut = 0;
    float windowGain = 1.0f;
    std::array<float, kPts> specIn {}, specOut {}, curve {}, maxCurve {}, selCurve {}, analogCurve {};
    std::array<float, kPeq * 8 + 2> lastSig {};
    juce::Image gridLayer, eqLayer;   // calques : grille (taille) et courbes d'EQ (réglages) ; seuls les spectres sont redessinés à chaque image
    bool eqDirty = true;
    float l = 0.0f, m = 0.0f, h = 0.0f, ai = 0.0f, lf = 125.0f, lc = 0.0f, eqAmt = 1.0f, analogAmt = 1.0f;
    bool anyDyn = false;
    int selected = 2, hoverNode = -1, dragNode = -1;
    float nodeGlow[kPeq] {}, liveGain[kPeq] {};
    juce::Point<float> dragStart;
    float dragF0 = 1000.0f, dragG0 = 0.0f;
    // point tiré : la position voulue est retenue à chaque mouvement, et envoyée à l'hôte une fois par image
    // (une souris rapide envoie des centaines de mouvements par seconde : autant de notifications pour le DAW)
    float pendingF = 0.0f, pendingG = 0.0f;
    bool pending = false;
    int qFlashBand = -1, qFlashFrames = 0;   // étiquette "Q x.xx" affichée pendant le réglage à la molette
    void placeBubble();
    bool bubbleClosed = false;
};

// Barre de réglage d'une bande de l'EQ paramétrique : n° + LED (on/off) + type (menu), "DYN" quand elle est dynamique.
class PeqBandChip : public InstanceBound, public juce::Component, public juce::SettableTooltipClient, public Animated
{
public:
    PeqBandChip (juce::AudioProcessorValueTreeState& s, int instance) : InstanceBound (instance), apvts (s)
    {
        setTooltip ("Parametric band: click the LED to switch it on/off, click the name for its menu "
                    "(type, slope, dynamic mode).");
    }
    void setBand (int b) { band = b; repaint(); }
    void setHighlighted (bool h) { if (h != highlighted) { highlighted = h; repaint(); } }
    std::function<void()> onClick;
    void paint (juce::Graphics&) override;
    void tick() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::AudioProcessorValueTreeState& apvts;
    int band = 2;
    float lit = 0.0f;
    bool highlighted = false;
    juce::String shownType;
};

// Petit bouton à pictogramme (plein écran / sortie du plein écran), même verre sombre que les pastilles.
class IconButton : public juce::Button, public Animated
{
public:
    enum class Icon { expand, collapse, headphones };   // headphones : interrupteur (écoute de la bande), braise quand il est allumé
    explicit IconButton (Icon i) : juce::Button ("icon"), icon (i) { if (i == Icon::headphones) setClickingTogglesState (true); }
    void setText (const juce::String& t) { text = t; repaint(); }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    void tick() override;

private:
    Icon icon;
    juce::String text;
    float glow = 0.0f, lit = 0.0f;
};

// Visuel animé d'une carte de la page FX : voix du chorus, peigne du flanger, échos du delay, rubans de la reverb.
class FxVisual : public InstanceBound, public juce::Component, public Animated
{
public:
    enum class Kind { chorus, flanger, delay, reverb, doubler, phone };
    FxVisual (OniriqueVocalBoxAudioProcessor& p, Kind k, int instance) : InstanceBound (instance), processor (p), kind (k) { setInterceptsMouseClicks (false, false); }
    void paint (juce::Graphics&) override;
    void tick() override;

private:
    onirique::chainmods::Reverb* reverbModule() const { return dynamic_cast<onirique::chainmods::Reverb*> (processor.module (onirique::modules::code (onirique::modules::reverb, inst))); }
    float param (const juce::String& id) const { return processor.apvts.getRawParameterValue (id)->load(); }
    OniriqueVocalBoxAudioProcessor& processor;
    Kind kind;
    double phase = 0.0;
    float on = 0.0f, level = 0.0f;
    juce::Image backdrop;   // DELAY : le décor fixe, dessiné une fois (à la taille réelle en pixels)
};

// Écran du MULTIBAND : 4 bandes avec niveau d'entrée, seuil (glisser verticalement dans la bande),
// réduction de gain (rideau braise depuis le haut), fréquences de coupure (glisser les séparations).
class MultibandDisplay : public InstanceBound, public juce::Component, public juce::TooltipClient, public Animated
{
public:
    MultibandDisplay (OniriqueVocalBoxAudioProcessor&, int instance);

    void paint (juce::Graphics&) override;
    void tick() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    juce::String getTooltip() override;

private:
    static constexpr int kB = 4;
    juce::Rectangle<float> plotArea() const noexcept;
    float xOf (float hz) const noexcept;
    float hzOf (float x) const noexcept;
    float yOf (float db) const noexcept;
    float dbOf (float y) const noexcept;
    float bandLeft (int b) const noexcept;
    float bandRight (int b) const noexcept;
    int crossoverAt (float x) const noexcept;
    int bandAt (float x) const noexcept;
    static float value (juce::RangedAudioParameter* p) { return p->convertFrom0to1 (p->getValue()); }
    static void setValue (juce::RangedAudioParameter* p, float v) { p->setValueNotifyingHost (p->convertTo0to1 (v)); }

    OniriqueVocalBoxAudioProcessor& processor;
    juce::RangedAudioParameter *xP[3] {}, *thrP[kB] {}, *gainP[kB] {};
    float x[3] { 200.0f, 1500.0f, 6000.0f }, thr[kB] {}, gain[kB] {}, gr[kB] {}, lvl[kB] { -100, -100, -100, -100 };
    float hoverGlow[kB] {}, xGlow[3] {};
    bool active = false;
    int dragX = -1, dragBand = -1, hoverX = -1, hoverBand = -1;
};

// Sélecteur des presets internes du MULTIBAND (menu dans le style du plugin).
class MbPresetBox : public InstanceBound, public juce::Component, public juce::SettableTooltipClient, public Animated
{
public:
    MbPresetBox (juce::AudioProcessorValueTreeState& s, int instance) : InstanceBound (instance), apvts (s)
    {
        setTooltip ("Multiband presets: starting points for vocals (only the multiband settings change).");
    }
    void paint (juce::Graphics&) override;
    void tick() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true; }
    void mouseExit (const juce::MouseEvent&) override  { hover = false; }

private:
    juce::AudioProcessorValueTreeState& apvts;
    juce::String shown;
    bool hover = false;
    float glow = 0.0f;
};

// Mètre de niveau vertical stéréo (L / R) : crête instantanée, retombée douce (~20 dB/s), maintien de crête 1.5 s,
// zone braise près de 0 dBFS, rouge au-delà ; valeur crête maintenue affichée sous les barres (clic = remise à zéro).
class LevelMeter : public juce::Component, public juce::SettableTooltipClient, public Animated
{
public:
    explicit LevelMeter (std::function<float (int)> consumePeak) : consume (std::move (consumePeak))
    {
        setTooltip ("Peak level (dBFS), left / right. The number is the highest recent peak: click to reset it.");
    }
    void paint (juce::Graphics&) override;
    void tick() override;
    void mouseDown (const juce::MouseEvent&) override { maxDb = -100.0f; repaint(); }

private:
    static float norm (float db) noexcept { return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 62.0f); }   // -60 .. +2 dBFS

    std::function<float (int)> consume;
    float levelDb[2] { -100.0f, -100.0f }, holdDb[2] { -100.0f, -100.0f }, maxDb = -100.0f;
    int holdFrames[2] {};
    int maxFrames = 0;
};

// Mètre de réduction de gain vertical (de-esser) : la barre descend depuis 0 dB (échelle 0..-18 dB),
// attaque immédiate, retour doux ; réduction max récente affichée dessous.
class GainReductionBar : public juce::Component, public juce::SettableTooltipClient, public Animated
{
public:
    GainReductionBar (std::function<float()> source, const juce::String& tip, float rangeDb = 18.0f)
        : read (std::move (source)), range (rangeDb) { setTooltip (tip); }
    void setActive (bool a) { active = a; }
    void paint (juce::Graphics&) override;
    void tick() override;

private:
    std::function<float()> read;
    float gr = 0.0f, hold = 0.0f, range = 18.0f;   // échelle 0 .. -range dB
    int holdFrames = 0;
    bool active = true;
};

// Écran du NOISE GATE (repris d'ONIRIQUE BROADCAST) : historique défilant du niveau (5 s), état ouvert / fermé,
// courbe de gain, seuil (glisser verticalement), hystérésis, bruit de fond mesuré et bouton LEARN.
class GateDisplay : public InstanceBound, public juce::Component, public juce::TooltipClient, public Animated
{
public:
    GateDisplay (OniriqueVocalBoxAudioProcessor&, int instance);

    void paint (juce::Graphics&) override;
    void tick() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    juce::String getTooltip() override;

private:
    static constexpr int kHist = 300;
    static constexpr float kMinDb = -80.0f;
    juce::Rectangle<float> plotArea() const noexcept;
    juce::Rectangle<float> learnArea() const noexcept;
    float yOf (float db) const noexcept;
    float dbOf (float y) const noexcept;
    float param (const juce::String& id) const { return processor.apvts.getRawParameterValue (id)->load(); }

    OniriqueVocalBoxAudioProcessor& processor;
    juce::RangedAudioParameter* thrP = nullptr;
    std::array<float, kHist> level {}, gainHist {};
    int head = 0;
    float thrShown = -48.0f, hystShown = 5.0f, floorShown = -90.0f, openLed = 0.0f, active = 0.0f;
    float thrGlow = 0.0f, learnGlow = 0.0f;
    bool dragging = false, hoverThr = false, hoverLearn = false;
    int learnFrames = 0;             // > 0 : écoute du bruit de fond en cours
    float learnMin = 0.0f;
    juce::String learnMessage;
    int messageFrames = 0;
};

