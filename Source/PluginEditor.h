#pragma once

#include "License/LicenseOverlay.h"
#include "PluginProcessor.h"
#include "MultibandPresets.h"
#include "Assistant/AssistantEngine.h"

#include <array>
#include <map>

namespace onirique::ui
{
// ---- DA du site ONIRIQUE : nuit / sang, accent braise, lignes fines, lueurs
inline const juce::Colour night  { 0xff0D0809 };
inline const juce::Colour blood  { 0xff5C0008 };
inline const juce::Colour deep   { 0xff1A080A };
inline const juce::Colour deep2  { 0xff2A0A0E };
inline const juce::Colour mist   { 0xffF6EEEE };
inline const juce::Colour haze   { 0xffA88F92 };
inline const juce::Colour ember  { 0xffE23444 };
inline const juce::Colour bone   { 0xffFFECEE };
inline const juce::Colour line   { (juce::uint8) 255, (juce::uint8) 210, (juce::uint8) 214, 0.13f };

// Polices : display (titres), body (libellés), mono (valeurs). Le site utilise Unbounded / Hanken
// Grotesk / JetBrains Mono ; ici leurs équivalents système (tout passe par font()).
enum class Face { display, body, mono };
juce::Font font (Face, float height, bool bold = false, float kerning = 0.0f);
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
} // namespace onirique::ui

// Tout ce qui s'anime reçoit tick() à 60 Hz depuis l'éditeur (une seule horloge pour toute l'interface).
struct Animated
{
    virtual ~Animated() = default;
    virtual void tick() = 0;
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

    KnobComponent (juce::AudioProcessorValueTreeState&, const char* paramId, const juce::String& title,
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
    DragValue (juce::AudioProcessorValueTreeState&, const char* paramId, const juce::String& tooltip);
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
    float on = 1.0f;
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
    float on = 0.0f;
};

// Sélecteur segmenté relié à un paramètre à choix ; la pastille braise glisse vers le choix.
class SegmentSelector : public juce::Component, public juce::TooltipClient, public Animated
{
public:
    SegmentSelector (juce::AudioProcessorValueTreeState&, const char* paramId,
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
    float slide = -1.0f, lockAmt = 0.0f;
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
class ResonanceDisplay : public juce::Component, public juce::TooltipClient, public Animated
{
public:
    static constexpr int kBands = onirique::ResonanceSuppressor::kBands;
    static constexpr int kNumCtl = onirique::ResonanceSuppressor::kNumBands;

    explicit ResonanceDisplay (juce::AudioProcessorValueTreeState&);

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

// Ordre de la compression : deux pastilles (1176 / LA-2A) à glisser-déposer l'une sur l'autre.
// En DUO, l'ordre gauche -> droite est l'ordre de traitement ; en solo, un clic choisit le compresseur.
class ChainOrder : public juce::Component, public juce::TooltipClient, public Animated
{
public:
    ChainOrder (juce::AudioProcessorValueTreeState&, const char* modeId, const char* orderId);

    void paint (juce::Graphics&) override;
    void tick() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    juce::String getTooltip() override;

private:
    juce::Rectangle<float> slotRect (int slot) const noexcept;
    int slotOf (int comp) const noexcept { return (order == 0) == (comp == 0) ? 0 : 1; }   // comp 0 = 1176, 1 = LA-2A
    int chipAt (juce::Point<float>) const noexcept;
    bool compActive (int comp) const noexcept { return mode == 2 || mode == comp; }

    std::unique_ptr<juce::ParameterAttachment> modeAtt, orderAtt;
    int mode = 0, order = 0;
    float chipX[2] { -1.0f, -1.0f }, lit[2] {}, flow = 0.0f, duoAmt = 0.0f;
    int dragChip = -1, hoverChip = -1;
    float dragOffset = 0.0f, dragX = 0.0f;
    bool dragMoved = false;
};

// Analyseur en temps réel + EQ paramétrique : spectres d'entrée et de sortie (FFT 8192, lissage 1/6 d'octave et
// retombée douce, courbes lissées), courbe EXACTE de l'EQ paramétrique (mêmes filtres que l'audio, gain dynamique en
// direct) et, en pointillés, la couleur de l'EQ ANALOG. Les points numérotés se tirent à la souris ; clic droit = menu
// de la bande (type, pente, mode dynamique). fullScreen : version plein écran (plus de repères, étiquettes de bande).
class SpectrumAnalyzer : public juce::Component, public juce::TooltipClient, public Animated
{
public:
    SpectrumAnalyzer (OniriqueVocalBoxAudioProcessor&, bool fullScreen = false);

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
    static void showBandMenu (juce::Component& target, juce::AudioProcessorValueTreeState&, int band);

private:
    static constexpr int kOrder = 13, kN = 1 << kOrder, kPts = 320, kPeq = onirique::ParametricEQ::kBands;
    static constexpr float kEqRange = 20.0f;

    struct PointMap { int b0 = 1, b1 = 1; float pos = 1.0f, width = 1.0f; bool interp = true; };
    void buildMap (double fs);
    void analyse (onirique::SpectrumTap&, std::vector<float>& hist, int& writePos, std::array<float, kPts>& spec);
    bool updateCurves (double fs);
    void paintGrid (juce::Graphics&);
    void paintEq (juce::Graphics&);
    float hzAt (int i) const noexcept { return 20.0f * std::pow (1000.0f, (float) i / (float) (kPts - 1)); }
    float param (const char* id) const { return processor.apvts.getRawParameterValue (id)->load(); }
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
    void setParam (const char* id, float v, bool gesture = true);
    juce::Path smoothPath (const std::array<float, kPts>& ys, bool closeToBottom, float bottom, int step = 1) const;
    void flushDrag();

    OniriqueVocalBoxAudioProcessor& processor;
    const bool full;
    bool active = true;
    juce::dsp::FFT fft { kOrder };
    std::vector<float> window, histIn, histOut, scratch, fftBuf;
    std::array<PointMap, kPts> pmap {};
    double mapFs = 0.0;
    int writeIn = 0, writeOut = 0;
    float windowGain = 1.0f;
    std::array<float, kPts> specIn {}, specOut {}, curve {}, maxCurve {}, selCurve {}, analogCurve {};
    std::array<float, kPeq * 8 + 2> lastSig {};
    juce::Image gridLayer, eqLayer;   // calques : grille (taille) et courbes d'EQ (réglages) ; seuls les spectres sont redessinés à chaque image
    bool eqDirty = true;
    float l = 0.0f, m = 0.0f, h = 0.0f, lf = 125.0f, hf = 10000.0f, lc = 0.0f, eqAmt = 1.0f, analogAmt = 1.0f;
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
};

// Barre de réglage d'une bande de l'EQ paramétrique : n° + LED (on/off) + type (menu), "DYN" quand elle est dynamique.
class PeqBandChip : public juce::Component, public juce::SettableTooltipClient, public Animated
{
public:
    explicit PeqBandChip (juce::AudioProcessorValueTreeState& s) : apvts (s)
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
    enum class Icon { expand, collapse };
    explicit IconButton (Icon i) : juce::Button ("icon"), icon (i) {}
    void setText (const juce::String& t) { text = t; repaint(); }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    void tick() override;

private:
    Icon icon;
    juce::String text;
    float glow = 0.0f;
};

// EQ paramétrique en plein écran (dans la fenêtre du plugin) : grand analyseur + les 8 bandes réglables d'un coup d'œil.
class EqFullView : public juce::Component, public Animated
{
public:
    explicit EqFullView (OniriqueVocalBoxAudioProcessor&);
    ~EqFullView() override;
    std::function<void()> onClose;
    std::function<void (int)> onSelect;
    void selectBand (int b);
    void setShown (bool shown);

    void paint (juce::Graphics&) override;
    void resized() override;
    void tick() override;
    bool keyPressed (const juce::KeyPress&) override;

    SpectrumAnalyzer analyzer;

private:
    OniriqueVocalBoxAudioProcessor& processor;
    IconButton exitButton { IconButton::Icon::collapse };
    PowerButton power;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAtt;
    std::array<std::unique_ptr<PeqBandChip>, 8> chips;
    std::array<std::unique_ptr<DragValue>, 8> freqV, gainV, qV, thrV;
    std::array<std::unique_ptr<PillToggle>, 8> dynB;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>, 8> dynAtt;
    std::vector<Animated*> children;
    juce::Rectangle<float> titleArea, bandsArea;
    int selected = 2;
};

// Barre de presets (en-tête) : précédent / nom (menu) / suivant + SAVE (champ de nom intégré).
class PresetBar : public juce::Component, public Animated
{
public:
    explicit PresetBar (onirique::PresetManager&);

    void paint (juce::Graphics&) override;
    void resized() override;
    void tick() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    enum Zone { none = -1, prev, name, next, save };
    Zone zoneAt (juce::Point<float>) const noexcept;
    void showMenu();
    void startSave();
    void endSave (bool commit);

    onirique::PresetManager& pm;
    juce::TextEditor editor;
    juce::Rectangle<float> zones[4];
    juce::String shownName;
    bool modified = false;
    Zone hover = none;
    float glow[4] {};
    int frame = 0;
};

// Onglets de la carte COMPRESSION (1176 / LA-2A | MULTIBAND) : pastille qui glisse, LED = module allumé.
class TabBar : public juce::Component, public Animated
{
public:
    TabBar (juce::StringArray names, std::function<bool (int)> isOn) : labels (std::move (names)), moduleOn (std::move (isOn)) {}

    std::function<void (int)> onChange;
    void setSelected (int t) { selected = t; if (slide < 0.0f) slide = (float) t; repaint(); }
    int getSelected() const noexcept { return selected; }
    // emplacement (coordonnées du parent) du bouton power du sous-module, à gauche du nom de l'onglet
    juce::Rectangle<int> iconArea (int tab) const;

    void paint (juce::Graphics&) override;
    void tick() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::StringArray labels;
    std::function<bool (int)> moduleOn;
    int selected = 0;
    float slide = -1.0f, led[2] {};
};

// Écran du MULTIBAND : 4 bandes avec niveau d'entrée, seuil (glisser verticalement dans la bande),
// réduction de gain (rideau braise depuis le haut), fréquences de coupure (glisser les séparations).
class MultibandDisplay : public juce::Component, public juce::TooltipClient, public Animated
{
public:
    explicit MultibandDisplay (OniriqueVocalBoxAudioProcessor&);

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
class MbPresetBox : public juce::Component, public juce::SettableTooltipClient, public Animated
{
public:
    explicit MbPresetBox (juce::AudioProcessorValueTreeState& s) : apvts (s)
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

// Ordre de traitement (en-tête) : 7 pastilles EQ > RESO > COMP > MULTI > ANALOG > DE-ESS > SAT à glisser à la souris.
// Les autres pastilles s'écartent pendant le glisser ; au lâcher, la chaîne se réordonne (fondu de 15 ms).
class ProcessingOrderBar : public juce::Component, public juce::TooltipClient, public Animated
{
public:
    explicit ProcessingOrderBar (juce::AudioProcessorValueTreeState&);

    void paint (juce::Graphics&) override;
    void tick() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    juce::String getTooltip() override;

private:
    static constexpr int kN = onirique::kOrderModules;
    float chipW() const noexcept;
    float gapW() const noexcept { return (float) getHeight() * 0.75f; }
    // place 0 = pastille GATE fixe (le noise gate est toujours en premier), puis les 7 modules déplaçables
    float slotX (int slot) const noexcept { return (float) (slot + 1) * (chipW() + gapW()); }
    int moduleAt (juce::Point<float>) const noexcept;
    std::array<int, kN> previewOrder() const noexcept;
    bool moduleOn (int module) const;

    juce::AudioProcessorValueTreeState& apvts;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    std::array<int, kN> order { 0, 1, 2, 3, 4, 5, 6 };
    std::array<float, kN> chipX {}, lit {}, hoverGlow {};
    float gateLit = 0.0f;
    bool placed = false;
    int dragModule = -1, hoverModule = -1;
    float dragOffset = 0.0f, dragX = 0.0f;
    bool dragMoved = false;
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
class GateDisplay : public juce::Component, public juce::TooltipClient, public Animated
{
public:
    explicit GateDisplay (OniriqueVocalBoxAudioProcessor&);

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
    float param (const char* id) const { return processor.apvts.getRawParameterValue (id)->load(); }

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

// Bouton de l'en-tête qui ouvre l'ASSISTANT : pastille braise, étincelle, liseré qui tourne doucement.
class AiButton : public juce::Button, public Animated
{
public:
    AiButton() : juce::Button ("AI ASSISTANT") {}
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    void tick() override;

private:
    float phase = 0.0f, glow = 0.0f;
};

// ASSISTANT IA : fenêtre (dans le plugin) qui écoute la voix pendant que le morceau joue, l'analyse, puis règle tous
// les modules (voir Assistant/AssistantEngine.h). Choix du style et de l'intensité, résultats détaillés, APPLIQUER /
// ANNULER. L'analyse et l'optimisation tournent sur un fil à part : l'interface et l'audio restent fluides.
class AssistantPanel : public juce::Component, public Animated, private juce::Thread, private juce::AsyncUpdater
{
public:
    explicit AssistantPanel (OniriqueVocalBoxAudioProcessor&);
    ~AssistantPanel() override;
    void open();
    void close();
   #if ONIRIQUE_UI_TEST
    void testWithFile (const juce::File&);   // essais locaux : analyse un fichier au lieu d'écouter le DAW
   #endif

    void paint (juce::Graphics&) override;
    void resized() override;
    void tick() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    enum class State { idle, listening, analysing, ready, applied, failed };
    enum Zone { zNone = -1, zClose = 0, zStyle = 10, zIntensity = 20, zListen = 30, zApply = 31, zUndo = 32, zAnalyseNow = 33, zBackdrop = 40 };
    static constexpr float kTargetVoiceSec = 12.0f, kMinVoiceSec = 4.0f;

    void run() override;
    void startListening();
    void finishListening();
    void startWork (bool reuseAnalysis);
    void apply();
    void undo();
    float u() const noexcept;
    juce::Rectangle<float> card() const noexcept;
    int zoneAt (juce::Point<float>) const;
    void addZone (int id, juce::Rectangle<float> r) const { zones.push_back ({ id, r }); }

    void paintLeft (juce::Graphics&, juce::Rectangle<float>);
    void paintRight (juce::Graphics&, juce::Rectangle<float>);
    void paintListening (juce::Graphics&, juce::Rectangle<float>);
    void paintProgress (juce::Graphics&, juce::Rectangle<float>);
    void paintResults (juce::Graphics&, juce::Rectangle<float>);
    void paintIntro (juce::Graphics&, juce::Rectangle<float>);
    void paintButtonPill (juce::Graphics&, juce::Rectangle<float>, const juce::String&, bool primary, bool enabled, int zone) const;

    OniriqueVocalBoxAudioProcessor& processor;
    State state = State::idle;
    int style = 0, intensity = 1, hover = zNone;
    mutable std::vector<std::pair<int, juce::Rectangle<float>>> zones;

    // capture de la voix
    std::vector<float> capture, scratch;
    double captureFs = 48000.0;
    float voiceSec = 0.0f, silentSec = 0.0f, levelNow = -100.0f;
    std::vector<float> levelHist;           // niveau des 0.1 s passées (onde affichée pendant l'écoute)
    double blockAcc = 0.0; int blockN = 0;

    // travail en arrière-plan
    juce::CriticalSection lock;
    float progress = 0.0f, shownProgress = 0.0f;
    juce::String stage;
    std::atomic<bool> cancel { false }, workDone { false };
    bool reuse = false;
    std::unique_ptr<onirique::assist::Engine> engine;
    onirique::assist::Analysis analysis;
    onirique::assist::Proposal proposal;
    juce::String failure;

    std::map<juce::String, float> undoValues;
    juce::String undoName;
    float appear = 0.0f, pulse = 0.0f;       // appear : 0..1 (fondu + flou du fond), animé dans le temps
    float cardScale = 1.0f, cardShift = 0.0f; // la fenêtre grandit avec un léger rebond et monte un peu à l'ouverture
    bool closing = false;
    double animStart = 0.0;
    juce::Image still;                        // le fond aux couleurs de la charte, dessiné une fois par taille
    juce::Image cardImage;                    // la fenêtre en image : c'est elle qui grandit et se fond pendant l'animation
    juce::Rectangle<int> cardImageArea;
    bool animating() const noexcept { return appear < 0.999f || closing; }
    void updateAnimation();
    void paintBackdrop (juce::Graphics&);
    void paintCard (juce::Graphics&);
    void cacheCard();
    void handleAsyncUpdate() override;
    juce::Rectangle<int> animArea() const;
    void repaintIdle();
};

class OniriqueVocalBoxAudioProcessorEditor : public juce::AudioProcessorEditor,
                                             private juce::Timer
{
public:
    explicit OniriqueVocalBoxAudioProcessorEditor (OniriqueVocalBoxAudioProcessor&);
    ~OniriqueVocalBoxAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void paintVeils (juce::Graphics&);   // voiles des modules coupés, dessinés par la couche "veils"
    void resized() override;

private:
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using Layout = KnobComponent::Layout;

    struct Section
    {
        juce::String title;
        juce::Rectangle<float> bounds;
        PowerButton* power = nullptr;
        float veil = 0.0f;           // 0 = module actif, 1 = module coupé (voile animé)
        bool sub = false;            // sous-panneau (1176 / LA-2A) dans la carte COMPRESSION
        bool hidden = false;         // sous-panneau de l'onglet non affiché
        bool plain = false;          // zone de voile seule (pas de panneau dessiné)
    };

    void timerCallback() override;
    float scale() const noexcept { return (float) getWidth() / 2022.0f; }
    void paintHeader (juce::Graphics&);
    void paintStar (juce::Graphics&, juce::Point<float> c, float r, float alpha);
    // fond et cartes ne bougent pas : dessinés une fois (à la résolution réelle de l'écran) dans deux images,
    // recopiées à chaque image ; seuls l'en-tête animé, les points des cartes et le témoin OVER sont redessinés
    void paintBackdrop (juce::Graphics&);
    void paintCards (juce::Graphics&);
    void rebuildLayers (float pixelScale);
    void showCompTab (int tab);
    void selectPeqBand (int band);

    OniriqueVocalBoxAudioProcessor& processor;
    OniriqueLookAndFeel lnf;
    juce::TooltipWindow tooltipWindow { this, 600 };

    KnobComponent lowKnob   { processor.apvts, onirique::ids::low,       "LOW",       "0",   "10",   Layout::textBelow,
                              "Pultec-style low end: boosts and tightens around the selected frequency. Double-click to reset.", lnf };
    SegmentSelector lowFreq { processor.apvts, onirique::ids::lowFreq, { "80", "100", "125" },
                              { "LOW centred on 80 Hz: deep, sub-weighted low end.",
                                "LOW centred on 100 Hz: chest and weight.",
                                "LOW centred on 125 Hz (original): warmth and body." } };
    KnobComponent midKnob   { processor.apvts, onirique::ids::mud,       "MID",       "0",   "10",   Layout::textBelow,
                              "Mud cleaner: clears the 300 Hz zone without thinning the voice. Double-click to reset.", lnf };
    KnobComponent highKnob  { processor.apvts, onirique::ids::high,      "HIGH",      "0",   "10",   Layout::textBelow,
                              "Air and clarity on the selected frequency. Clean boost; turn SMART S on to tame the sibilance it brings out. Double-click to reset.", lnf };
    SegmentSelector highFreq { processor.apvts, onirique::ids::highFreq, { "8K", "10K", "20K" },
                              { "HIGH air from 8 kHz: presence and brightness.",
                                "HIGH air from 10 kHz: open, airy top.",
                                "HIGH air at 20 kHz: very wide 'air band' shelf, the silkiest top end." } };
    PillToggle smartSButton { "SMART S", PillToggle::Style::mini };
    PillToggle lowCutButton { "LOW CUT", PillToggle::Style::mini };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> smartSAttachment, lowCutAttachment;
    KnobComponent freqKnob  { processor.apvts, onirique::ids::deessFreq, "FREQUENCY", "3k",  "10k",  Layout::textBelow,
                              "De-esser frequency, 3 to 10 kHz (default 4000 Hz). Double-click to reset.", lnf };
    KnobComponent amtKnob   { processor.apvts, onirique::ids::deessAmt,  "INTENSITY", "0",   "100",  Layout::textBelow,
                              "De-esser intensity: how much the sibilance is reduced. Double-click to reset.", lnf };
    KnobComponent driveKnob { processor.apvts, onirique::ids::drive,     "DRIVE",     "0",   "10",   Layout::textBelow,
                              "Saturation amount for the selected type. The level stays constant: only the harmonics change. Double-click to reset.", lnf };
    SegmentSelector satType { processor.apvts, onirique::ids::satType, { "TUBE", "ANALOG", "TAPE" },
                              { "Tube: asymmetric tube stage, even harmonics, transients kept crisp.",
                                "Analog: class A + transformers, 1073-style. Thick lows, forward mids, rounded top.",
                                "Tape: softened highs and peaks, slow tape compression, head bump." } };

    // ---- MIX / LIVE (en-tête), suréchantillonnage (SATURATION), plosives (DE-ESSER), écoute RESONANCE
    SegmentSelector modeSel { processor.apvts, onirique::ids::liveMode, { "MIX", "LIVE" },
                              { "MIX: full quality. All the processing, latency compensated by your DAW (~44 ms).",
                                "LIVE: zero latency, to record yourself through the plugin. Resonance switches to a "
                                "real-time dynamic EQ, no oversampling, no look-ahead." } };
    SegmentSelector osSel   { processor.apvts, onirique::ids::oversampling, { "OFF", "2X", "4X", "8X" },
                              { "Oversampling off: lowest CPU.",
                                "Whole plugin oversampled x2 (default): cleaner compressors, de-esser and saturation.",
                                "Whole plugin oversampled x4.",
                                "Whole plugin oversampled x8: the cleanest, but about 5x the CPU of x2." } };
    KnobComponent plosiveFader { processor.apvts, onirique::ids::plosive, "PLOSIVES", "", "", Layout::horizontal,
                                 "Plosive suppressor: detects P / B pops (sudden low-end bursts) and pulls them down only "
                                 "while they happen. The LED lights up when a plosive is caught. Double-click to reset.", lnf };
    PillToggle listenButton { "LISTEN", PillToggle::Style::mini };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> listenAttachment;

    ResonanceDisplay resoDisplay { processor.apvts };
    KnobComponent resoAmtKnob { processor.apvts, onirique::ids::resoAmt, "AMOUNT",  "0",   "100",  Layout::textBelow,
                              "Resonance reduction intensity: how much the detected resonances are pulled down. Double-click to reset.", lnf };
    KnobComponent resoLowFader  { processor.apvts, onirique::ids::resoLow,  "LOW",  "", "", Layout::fader,
                              "LOW band sensitivity. The targeted frequencies light up in the display. Double-click to reset.", lnf };
    KnobComponent resoMidFader  { processor.apvts, onirique::ids::resoMid,  "MID",  "", "", Layout::fader,
                              "MID band sensitivity. The targeted frequencies light up in the display. Double-click to reset.", lnf };
    KnobComponent resoHighFader { processor.apvts, onirique::ids::resoHigh, "HIGH", "", "", Layout::fader,
                              "HIGH band sensitivity. The targeted frequencies light up in the display. Double-click to reset.", lnf };
    DragValue resoLowHz  { processor.apvts, onirique::ids::resoLowFreq,
                           "LOW band centre frequency: drag up/down (or drag the LOW handle in the display). Double-click to reset." };
    DragValue resoMidHz  { processor.apvts, onirique::ids::resoMidFreq,
                           "MID band centre frequency: drag up/down (or drag the MID handle in the display). Double-click to reset." };
    DragValue resoHighHz { processor.apvts, onirique::ids::resoHighFreq,
                           "HIGH band centre frequency: drag up/down (or drag the HIGH handle in the display). Double-click to reset." };

    // ---- COMPRESSION
    SegmentSelector compMode { processor.apvts, onirique::ids::compMode, { "1176", "LA-2A", "DUO" },
                               { "1176 alone: fast FET compression, punch and grit.",
                                 "LA-2A alone: smooth optical levelling, slow two-stage release.",
                                 "Both in series: drag the chips below to choose which one comes first." } };
    ChainOrder compOrder { processor.apvts, onirique::ids::compMode, onirique::ids::compOrder };
    KnobComponent fetInKnob  { processor.apvts, onirique::ids::fetIn,      "INPUT",   "-12", "+36", Layout::textBelow,
                               "1176 input: drives the signal into the compressor (fixed threshold, like the hardware). Double-click to reset.", lnf };
    KnobComponent fetOutKnob { processor.apvts, onirique::ids::fetOut,     "OUTPUT",  "-24", "+24", Layout::textBelow,
                               "1176 output level. Double-click to reset.", lnf, true };
    KnobComponent fetAtkKnob { processor.apvts, onirique::ids::fetAttack,  "ATTACK",  "1",   "7",   Layout::textBelow,
                               "1176 attack, 1 (800 us) to 7 (20 us, fastest) - like the hardware, higher = faster. Double-click to reset.", lnf };
    KnobComponent fetRelKnob { processor.apvts, onirique::ids::fetRelease, "RELEASE", "1",   "7",   Layout::textBelow,
                               "1176 release, 1 (1.1 s) to 7 (50 ms, fastest). Double-click to reset.", lnf };
    SegmentSelector fetRatio { processor.apvts, onirique::ids::fetRatio, { "4", "8", "12", "20", "ALL" },
                               { "Ratio 4:1 - softest knee, the vocal classic.", "Ratio 8:1.", "Ratio 12:1.",
                                 "Ratio 20:1 - limiting.",
                                 "All buttons in: extreme ratio, over-compression and aggressive distortion." } };
    KnobComponent laGainKnob { processor.apvts, onirique::ids::laGain,     "GAIN",    "-12", "+30", Layout::textBelow,
                               "LA-2A output gain. Double-click to reset.", lnf, true };
    KnobComponent laPeakKnob { processor.apvts, onirique::ids::laPeak,     "PEAK REDUCTION", "0", "100", Layout::textBelow,
                               "LA-2A peak reduction: the more you turn, the more it compresses. Double-click to reset.", lnf };
    SegmentSelector laMode   { processor.apvts, onirique::ids::laLimit, { "COMPRESS", "LIMIT" },
                               { "Compress: ~3:1, very soft knee.", "Limit: ~10:1, firmer knee." } };
    VuMeter vu1176 { [this] { return processor.get1176ReductionDb(); } };
    VuMeter vuLA2A { [this] { return processor.getLA2AReductionDb(); } };

    // ---- MULTIBAND (onglet de la carte COMPRESSION)
    TabBar compTabs { { "1176 / LA-2A", "MULTIBAND" },
                      [this] (int t) { return t == 0 ? compPower.getToggleState() : mbPower.getToggleState(); } };
    MultibandDisplay mbDisplay { processor };
    MbPresetBox mbPresets { processor.apvts };
    KnobComponent mbThr1 { processor.apvts, onirique::ids::mbThr[0], "LOW",    "-60", "0", Layout::textBelow,
                           "LOW band threshold: below the first crossover (boom, plosives). Double-click to reset.", lnf };
    KnobComponent mbThr2 { processor.apvts, onirique::ids::mbThr[1], "LO-MID", "-60", "0", Layout::textBelow,
                           "LO-MID band threshold: body and boxiness. Double-click to reset.", lnf };
    KnobComponent mbThr3 { processor.apvts, onirique::ids::mbThr[2], "HI-MID", "-60", "0", Layout::textBelow,
                           "HI-MID band threshold: presence and harshness. Double-click to reset.", lnf };
    KnobComponent mbThr4 { processor.apvts, onirique::ids::mbThr[3], "HIGH",   "-60", "0", Layout::textBelow,
                           "HIGH band threshold: air, breaths and sibilance. Double-click to reset.", lnf };
    DragValue mbGain1 { processor.apvts, onirique::ids::mbGain[0], "LOW band gain after compression: drag up/down. Double-click to reset." };
    DragValue mbGain2 { processor.apvts, onirique::ids::mbGain[1], "LO-MID band gain after compression: drag up/down. Double-click to reset." };
    DragValue mbGain3 { processor.apvts, onirique::ids::mbGain[2], "HI-MID band gain after compression: drag up/down. Double-click to reset." };
    DragValue mbGain4 { processor.apvts, onirique::ids::mbGain[3], "HIGH band gain after compression: drag up/down. Double-click to reset." };
    KnobComponent mbAttackKnob  { processor.apvts, onirique::ids::mbAttack,  "ATTACK",  "1", "200", Layout::textBelow,
                                  "Attack of all bands (lows a bit slower, highs a bit faster, automatically). Double-click to reset.", lnf };
    KnobComponent mbReleaseKnob { processor.apvts, onirique::ids::mbRelease, "RELEASE", "20", "2k", Layout::textBelow,
                                  "Release of all bands, optical: slows down after long compressed phrases. Double-click to reset.", lnf };

    SpectrumAnalyzer analyzer { processor };
    PeqBandChip peqChip { processor.apvts };
    std::array<std::unique_ptr<DragValue>, 8> peqFreqV, peqGainV, peqQV, peqThrV;   // réglages précis de la bande sélectionnée
    IconButton fullButton { IconButton::Icon::expand };
    bool peqDynShown = false;
    AiButton aiButton;
    PresetBar presetBar { processor.presets };
    ProcessingOrderBar orderBar { processor.apvts };
    // ---- PARALLEL : compression parallèle (1176 écrasé remis dans la voix)
    KnobComponent parAmountKnob { processor.apvts, onirique::ids::parAmount, "AMOUNT", "0", "100", Layout::textBelow,
                                  "Parallel compression amount: how much of the crushed 1176 bus is blended back into the voice "
                                  "(50 % = bus 6 dB under the voice, 100 % = same level). Double-click to reset.", lnf };
    GainReductionBar parGr { [this] { return processor.getParallelReductionDb(); },
                             "How hard the parallel bus is being crushed (dB of gain reduction).", 36.0f };
    GainReductionBar deessGr { [this] { return processor.getDeEssReductionDb(); },
                               "De-esser gain reduction (dB): how much the sibilance is being pulled down right now." };

    // ---- second de-esser (bouton + de la carte DE-ESSER) : une seconde rangée de boutons quand il est ajouté
    PillToggle deess2Button { "+ DE-ESSER", PillToggle::Style::mini };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> deess2Attachment;
    KnobComponent deess2FreqKnob { processor.apvts, onirique::ids::deess2Freq, "FREQUENCY 2", "3k", "10k", Layout::textBelow,
                                   "Second de-esser frequency, 3 to 10 kHz (default 7500 Hz): catch a second sibilance zone, "
                                   "for example the sharp 'ts' above the main 'sh'. Double-click to reset.", lnf };
    KnobComponent deess2AmtKnob  { processor.apvts, onirique::ids::deess2Amt, "INTENSITY 2", "0", "100", Layout::textBelow,
                                   "Second de-esser intensity: how much the second sibilance zone is reduced. Double-click to reset.", lnf };
    GainReductionBar deess2Gr { [this] { return processor.getDeEss2ReductionDb(); },
                                "Second de-esser gain reduction (dB): how much the second sibilance zone is being pulled down right now." };
    bool deess2Shown = false;

    // ---- PARALLEL : AUTO (réglage d'usine) ou seuil / attaque / relâchement à la main
    PillToggle parAutoButton { "AUTO", PillToggle::Style::small };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> parAutoAttachment;
    KnobComponent parThreshKnob  { processor.apvts, onirique::ids::parThresh, "THRESHOLD", "-57", "-9", Layout::textBelow,
                                   "Level above which the parallel bus is crushed (dBFS). Lower = harder. Double-click to reset.", lnf };
    KnobComponent parAttackKnob  { processor.apvts, onirique::ids::parAttack, "ATTACK", "", "", Layout::textBelow,
                                   "Attack of the parallel 1176 (20 us to 0.8 ms). Double-click to reset.", lnf };
    KnobComponent parReleaseKnob { processor.apvts, onirique::ids::parRelease, "RELEASE", "", "", Layout::textBelow,
                                   "Release of the parallel 1176 (50 ms to 1.1 s). Double-click to reset.", lnf };
    bool parManualShown = false;
    LevelMeter inMeter  { [this] (int c) { return processor.consumeInputLevel (c); } };
    LevelMeter outMeter { [this] (int c) { return processor.consumeOutputLevel (c); } };
    PillToggle mbListen1 { "LISTEN", PillToggle::Style::mini }, mbListen2 { "LISTEN", PillToggle::Style::mini },
               mbListen3 { "LISTEN", PillToggle::Style::mini }, mbListen4 { "LISTEN", PillToggle::Style::mini };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> mbListenAtt[4];

    KnobComponent inKnob    { processor.apvts, onirique::ids::input,     "INPUT",     "-24", "+24",  Layout::textBelow,
                              "Input gain in dB. Double-click to reset to 0 dB.", lnf, true };
    KnobComponent outKnob   { processor.apvts, onirique::ids::output,    "OUTPUT",    "-24", "+24",  Layout::textBelow,
                              "Output level in dB. Double-click to reset to 0 dB.", lnf, true };

    PillToggle autoButton   { "AUTO GAIN", PillToggle::Style::big };
    PillToggle bypassButton { "BYPASS", PillToggle::Style::small };
    std::unique_ptr<ButtonAttachment> autoAttachment, bypassAttachment;

    PowerButton eqPower, resoPower, cardPower, compPower, mbPower, parPower, deessPower, satPower, peqPower, gatePower;
    std::unique_ptr<ButtonAttachment> eqPowerAtt, resoPowerAtt, cardPowerAtt, compPowerAtt, mbPowerAtt, parPowerAtt, deessPowerAtt, satPowerAtt, peqPowerAtt, gatePowerAtt;

    // ---- NOISE GATE (premier module)
    GateDisplay gateDisplay { processor };
    KnobComponent gateThrKnob   { processor.apvts, onirique::ids::gateThr,     "THRESHOLD", "-80", "0",  Layout::textBelow,
                                  "Level above which the gate opens. Drag the red line in the display or use LEARN. Double-click = reset.", lnf };
    KnobComponent gateRangeKnob { processor.apvts, onirique::ids::gateRange,   "RANGE",     "0",   "80", Layout::textBelow,
                                  "How much the gate turns the sound down when closed (80 = mute). 20-30 dB keeps the room natural.", lnf };
    KnobComponent gateAtkKnob   { processor.apvts, onirique::ids::gateAttack,  "ATTACK",    "0.1", "50", Layout::textBelow,
                                  "How fast the gate opens. With the 1.5 ms look-ahead (MIX), even fast attacks never eat the first consonant.", lnf };
    KnobComponent gateHoldKnob  { processor.apvts, onirique::ids::gateHold,    "HOLD",      "0",   "500", Layout::textBelow,
                                  "Time the gate stays open after the voice drops: keeps word endings and short pauses.", lnf };
    KnobComponent gateRelKnob   { processor.apvts, onirique::ids::gateRelease, "RELEASE",   "10",  "2k", Layout::textBelow,
                                  "How smoothly the gate closes after HOLD.", lnf };
    DragValue gateHystV { processor.apvts, onirique::ids::gateHyst,
                          "Hysteresis: the gate closes this many dB below the threshold (no chattering). Drag up/down." };

    // EQ paramétrique plein écran et ASSISTANT : par-dessus les modules (et leurs voiles), sous la licence
    EqFullView eqFull { processor };
    AssistantPanel assistant { processor };
    void showEqFull (bool show);


    std::vector<Animated*> animated;
    std::vector<Section> sections;
    juce::Image logo, logoScaled, logoGlow;
    juce::Image backdropLayer, cardsLayer;   // vidées au redimensionnement
    float layersScale = 0.0f;
    juce::Rectangle<float> logoRect, signalRect, overRect;
    std::vector<std::pair<juce::Point<float>, float>> stars;   // position, taille
    double clock = 0.0;
    int overHold = 0, frame = 0;
    bool restoringSize = true;   // pendant la construction : ne pas écraser la taille mémorisée

    // Couche transparente au-dessus des modules (mais sous les menus, qui s'ouvrent après) pour les voiles
    // des modules coupés ; elle laisse passer tous les clics.
    struct Overlay : juce::Component
    {
        std::function<void (juce::Graphics&)> draw;
        Overlay() { setInterceptsMouseClicks (false, false); }
        void paint (juce::Graphics& g) override { if (draw) draw (g); }
    } veils;
    float overGlow = 0.0f;

    std::unique_ptr<onirique::license::Overlay> licenseOverlay;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OniriqueVocalBoxAudioProcessorEditor)
};
