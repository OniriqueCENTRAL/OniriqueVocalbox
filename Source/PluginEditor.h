#pragma once

#include "License/LicenseOverlay.h"
#include "ModulePanels.h"
#include "StartScreen.h"

namespace onirique::ui
{
// le pictogramme d'un module (carré de la chaîne, catalogue du +), tracé comme dans la démo (grille de 24, trait arrondi)
void drawModuleIcon (juce::Graphics&, int type, juce::Rectangle<float> r, juce::Colour, float thickness);
} // namespace onirique::ui

/*  La chaîne : les modules en carrés de verre de 132 x 96, dans l'ordre du traitement, une flèche entre deux. Un clic choisit
    le module (il s'affiche en grand dessous) ; glisser un carré à gauche ou à droite le déplace (les autres s'écartent) ; au
    survol, il se soulève, un reflet passe, power et retirer apparaissent. Le + au bout ouvre le catalogue. Trop de modules
    pour la largeur : la rangée défile (molette, ou en approchant un bord pendant un glisser). */
class ChainStrip : public juce::Component, public Animated, public juce::TooltipClient
{
public:
    explicit ChainStrip (OniriqueVocalBoxAudioProcessor&);
    std::function<void (int code)> onSelect;
    std::function<void()> onAdd;
    void setSelected (int code);

    void paint (juce::Graphics&) override;
    void tick() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    juce::String getTooltip() override;

private:
    struct Tile { int code = 0; float x = 0.0f, vel = 0.0f, hover = 0.0f, on = 1.0f, sel = 0.0f, sheen = 0.0f, meter = 0.0f; };   // meter : ce que le module retire (dB), pour le mini-mètre
    juce::Rectangle<float> tileRect (float x) const;   // x : position dans la rangée (avant défilement)
    juce::Rectangle<float> plusRect() const;
    float slotX (int index) const;
    float contentWidth() const;
    int tileAt (juce::Point<float>) const;
    juce::Rectangle<float> powerRect (const juce::Rectangle<float>& t) const;
    juce::Rectangle<float> closeRect (const juce::Rectangle<float>& t) const;
    void sync();
    bool isOn (int code) const;
    void reveal (int index);

    OniriqueVocalBoxAudioProcessor& processor;
    std::vector<Tile> tiles;
    int selected = 0, hoverTile = -1, dragTile = -1, dropIndex = -1, hoverButton = -1;
    int meterFrame = 0;
    bool plusHover = false, dragMoved = false;
    float dragOffset = 0.0f, dragX = 0.0f, plusGlow = 0.0f, scroll = 0.0f, scrollTarget = 0.0f;
    juce::Point<float> downPos;
};

// Le catalogue du + : chaque module avec son pictogramme, son nom et ce qu'il fait, rangés par usage (quatre colonnes).
// Trois exemplaires de chaque module au plus.
class ModulePicker : public juce::Component, public Animated
{
public:
    explicit ModulePicker (OniriqueVocalBoxAudioProcessor&);
    std::function<void (int type)> onPick;
    void open();
    void close();
    bool isOpen() const noexcept { return target > 0.5f; }

    void paint (juce::Graphics&) override;
    void tick() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; closeHover = false; repaint(); }
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    juce::Rectangle<float> card() const;
    juce::Rectangle<float> itemRect (int type) const;
    juce::Rectangle<float> closeRect() const;
    float groupY (int group) const;   // haut du titre du groupe, dans la carte
    int itemAt (juce::Point<float>) const;
    int freeCount (int type) const;
    void paintItem (juce::Graphics&, int type, float hoverGlow, bool overlayOnly) const;
    void paintCard (juce::Graphics&) const;
    juce::Image cache;            // la carte au repos, dessinée une fois : le survol ne redessine que sa case
    juce::int64 cacheKey = 0;
    OniriqueVocalBoxAudioProcessor& processor;
    float shown = 0.0f, target = 0.0f, hoverGlow[onirique::modules::numTypes] {};
    int hover = -1;
    bool closeHover = false;
};

// IN / OUT du bas : le titre, le niveau, sa valeur, le potard étoile du gain
class GainStrip : public juce::Component, public Animated
{
public:
    GainStrip (OniriqueVocalBoxAudioProcessor&, const char* paramId, const juce::String& title, std::function<float (int)> consume);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick() override;

private:
    juce::String title;
    std::function<float (int)> consume;
    StarKnob knob;
    float level = -100.0f;
};

// AUTO GAIN du bas : le libellé et l'interrupteur
class AutoGainSwitch : public juce::Component, public Animated, public juce::SettableTooltipClient
{
public:
    explicit AutoGainSwitch (OniriqueVocalBoxAudioProcessor&);
    void paint (juce::Graphics&) override;
    void tick() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    OniriqueVocalBoxAudioProcessor& processor;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    juce::RangedAudioParameter* param = nullptr;
    bool state = false;
    float lit = 0.0f;
};

// L'APPRENTISSAGE : « CE MIX TE PLAÎT ? OUI · BOF », dans l'en-tête de la chaîne, tant qu'un mix d’AUTO MIX attend sa note
class RateChip : public juce::Component, public Animated
{
public:
    explicit RateChip (OniriqueVocalBoxAudioProcessor& p) : processor (p) {}
    void tick() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent& e) override { over = yesArea().contains (e.position) ? 1 : noArea().contains (e.position) ? -1 : 0; }
    void mouseExit (const juce::MouseEvent&) override { over = 0; }
private:
    juce::Rectangle<float> yesArea() const;
    juce::Rectangle<float> noArea() const;
    OniriqueVocalBoxAudioProcessor& processor;
    int over = 0;
    float glow = 0.0f;
};

// une gélule de texte cliquable (APPLIQUER, NON MERCI de la suggestion des réglages habituels)
class TextPill : public juce::Component
{
public:
    explicit TextPill (juce::String t, bool litPill) : text (std::move (t)), lit (litPill) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    std::function<void()> onClick;
    void setState (const juce::String& t, bool litPill) { if (t != text || litPill != lit) { text = t; lit = litPill; repaint(); } }
    float idealWidth() const;
    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseUp (const juce::MouseEvent& e) override { if (getLocalBounds().contains (e.getPosition()) && onClick) onClick(); }
private:
    juce::String text;
    bool lit, hover = false;
};

// MON PROFIL, en haut à droite du plugin (dans la barre, à gauche de la pastille ACTIVÉ) : gélule de verre, silhouette braise
class ProfileTopButton : public juce::Component
{
public:
    ProfileTopButton (juce::String text, bool chainIcon) : label (std::move (text)), chains (chainIcon)
    { setMouseCursor (juce::MouseCursor::PointingHandCursor); setTitle (label); }
    std::function<void()> onClick;
    bool open = false;
    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseUp (const juce::MouseEvent& e) override { if (getLocalBounds().contains (e.getPosition()) && onClick) onClick(); }
private:
    bool hover = false;
    juce::String label;
    bool chains = false;   // le pictogramme : trois maillons (CHAÎNES) ou une silhouette (MON PROFIL)
};

// AUTO MIX, en haut à droite de la chaîne : ouvre l'écran des macros (et « affiner en mots ») ; allumé quand il est affiché
class MixPageButton : public juce::Component
{
public:
    MixPageButton() { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    std::function<void()> onClick;
    bool active = false;
    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseUp (const juce::MouseEvent& e) override { if (getLocalBounds().contains (e.getPosition()) && onClick) onClick(); }
private:
    bool hover = false;
};

//==============================================================================
/*  L'interface sous la barre, telle que la démo validée (1600 x 936) : la chaîne en haut, le module choisi en grand dans son
    panneau de verre (son en-tête : point braise, nom, ce qu'il fait, ON), IN / AUTO GAIN / OUT en bas. Le catalogue du +
    s'ouvre par-dessus. Une seule horloge à 60 Hz anime tout. */
class VocalBoxView : public juce::Component, private juce::Timer
{
public:
    explicit VocalBoxView (OniriqueVocalBoxAudioProcessor&);
    ~VocalBoxView() override;
    void themeChanged();   // une autre couleur d'interface : fonds en cache et écran du module refaits
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    // NOUVEAU MIX (page AUTO MIX, MON PROFIL) : le retour du mix part, la chaîne est vidée, l'écran de départ revient
    void startNewMix();

private:
    void timerCallback() override;
    juce::Point<int> lastScreenPos;   // la fenêtre bouge : animations en pause (timerCallback)
    double movingUntil = 0.0;
    double openedAt = juce::Time::getMillisecondCounterHiRes();   // l'ouverture ne compte pas comme un déplacement
    void select (int code);
    void syncSelection();
    void openMixPage();
    juce::Rectangle<float> panelArea() const;
    juce::Rectangle<float> headerPower() const;
    bool isOn (int code) const;

    OniriqueVocalBoxAudioProcessor& processor;
    OniriqueLookAndFeel lnf;
    ChainStrip strip;
    std::unique_ptr<ModulePanel> panel;
    GainStrip inStrip, outStrip;
    AutoGainSwitch autoGain;
    GlassTabs oversampling;
    ModulePicker picker;
    StartScreen start;   // chaîne vide : la sphère et la barre de recherche
    RateChip rate;       // la note du mix (apprentissage)
    MixPageButton mixButton;
    TextPill compare { "COMPARER", false };   // la prise brute au volume du mix
    TextPill simpleBtn { "MODE SIMPLE", false };   // les potards essentiels seulement (ModulePanels.h, uipref)
    bool mixPage = false;   // l'écran AUTO MIX (macros) à la place d'un module
    // ses réglages habituels pour le module qu'il vient d'ajouter (habitudes, sur son ordinateur)
    void offerHabit (int code);
    void closeHabit();
    juce::Rectangle<float> habitArea() const;
    std::map<juce::String, float> habitValues;
    juce::String habitText;
    int habitCode = 0;
    TextPill habitYes { "APPLIQUER", true }, habitNo { "NON MERCI", false };
    juce::TooltipWindow tooltips { this, 600 };
    int selected = 0;
    float panelFade = 1.0f, powerLit = 0.0f, powerHover = 0.0f;
    bool overPower = false;
    juce::Image backdrop;   // le fond (taches de braise floues) : dessiné une fois par taille
    bool backdropNative = false;   // puis passé en image native (gardée par la carte graphique)
};

//==============================================================================
// La fenêtre du plugin : la barre du haut des plugins ONIRIQUE (le titre, les presets, IN, A/B, copier / coller, la
// licence, la taille de la fenêtre), et l'interface dessous. Les deux se zooment ensemble.
class OniriqueVocalBoxAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit OniriqueVocalBoxAudioProcessorEditor (OniriqueVocalBoxAudioProcessor&);
    ~OniriqueVocalBoxAudioProcessorEditor() override;

    void paint (juce::Graphics&) override {}
    void resized() override;

private:
    void timerCallback() override;
    float titleWidth() const;
    void paintTitle (juce::Graphics&, juce::Rectangle<float> area) const;   // le titre, dans la barre (en points)
    void refreshTheme();

    OniriqueVocalBoxAudioProcessor& processor;
    onirique::ui::FontScope fontScope;   // polices de la charte, le temps que l'interface est ouverte
    VocalBoxView view;
    onirique::nuit::Bar bar;
    std::unique_ptr<onirique::license::Overlay> licenseOverlay;
    // MON PROFIL, dans la barre du haut à gauche de la pastille de licence ; sa carte s'ouvre par-dessus tout le plugin
    ProfileTopButton profileTop { "MON PROFIL", false }, libraryTop { juce::String::fromUTF8 ("CHAÎNES"), true };
    std::unique_ptr<juce::Component> profileCard, libraryCard;
    bool restoringSize = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OniriqueVocalBoxAudioProcessorEditor)
};
