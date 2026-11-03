#pragma once

#include "License/LicenseOverlay.h"
#include "ModulePanels.h"

namespace onirique::ui
{
// le pictogramme d'un module (carré de la chaîne, catalogue du +), dessiné en trait dans r
void drawModuleIcon (juce::Graphics&, int type, juce::Rectangle<float> r, juce::Colour, float thickness);
} // namespace onirique::ui

/*  La chaîne : les modules en carrés de verre, dans l'ordre du traitement. Un clic choisit le module (il s'affiche en grand
    dessous) ; glisser un carré à gauche ou à droite le déplace (les autres s'écartent) ; au survol, power et retirer. Le +
    au bout ouvre le catalogue des modules. */
class ChainStrip : public juce::Component, public Animated, public juce::TooltipClient
{
public:
    explicit ChainStrip (OniriqueVocalBoxAudioProcessor&);
    std::function<void (int code)> onSelect;
    std::function<void()> onAdd;
    void setSelected (int code) { if (code != selected) { selected = code; repaint(); } }

    void paint (juce::Graphics&) override;
    void tick() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    juce::String getTooltip() override;

private:
    struct Tile { int code = 0; float x = 0.0f, vel = 0.0f, hover = 0.0f, on = 1.0f, sel = 0.0f, appear = 0.0f; };
    juce::Rectangle<float> tileRect (float x) const;
    juce::Rectangle<float> plusRect() const;
    float slotX (int index) const;
    int tileAt (juce::Point<float>) const;
    juce::Rectangle<float> powerRect (const juce::Rectangle<float>& t) const;
    juce::Rectangle<float> closeRect (const juce::Rectangle<float>& t) const;
    float tileW() const;
    void sync();
    bool isOn (int code) const;

    OniriqueVocalBoxAudioProcessor& processor;
    std::vector<Tile> tiles;
    int selected = 0, hoverTile = -1, dragTile = -1, dropIndex = -1;
    bool plusHover = false, dragMoved = false;
    float dragOffset = 0.0f, dragX = 0.0f, plusGlow = 0.0f, plusSpin = 0.0f;
    juce::Point<float> downPos;
};

// Le catalogue du + : chaque module avec son pictogramme, son nom et ce qu'il fait, rangés par usage. Autant d'exemplaires
// qu'on veut (trois par module).
class ModulePicker : public juce::Component, public Animated
{
public:
    explicit ModulePicker (OniriqueVocalBoxAudioProcessor&);
    std::function<void (int type)> onPick;
    std::function<void()> onClose;
    void open();
    void close();
    bool isOpen() const noexcept { return target > 0.5f; }

    void paint (juce::Graphics&) override;
    void tick() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    juce::Rectangle<float> card() const;
    juce::Rectangle<float> itemRect (int type) const;
    int itemAt (juce::Point<float>) const;
    int freeCount (int type) const;
    OniriqueVocalBoxAudioProcessor& processor;
    float shown = 0.0f, target = 0.0f, hoverGlow[onirique::modules::numTypes] {};
    int hover = -1;
};

// Potard IN / OUT du bas (gain d'entrée, de sortie), avec son mètre stéréo
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
    float level[2] { -100.0f, -100.0f }, hold[2] { -100.0f, -100.0f };
    int holdFrames[2] {};
};

// l'interrupteur AUTO GAIN du bas (avec la correction appliquée)
class AutoGainSwitch : public juce::Component, public Animated
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
    float lit = 0.0f, shownDb = 0.0f;
};

//==============================================================================
/*  L'interface sous la barre : la chaîne en haut, le module choisi en grand (son en-tête : nom, ce qu'il fait, power), IN /
    AUTO GAIN / OUT en bas. Le catalogue du + s'ouvre par-dessus. Une seule horloge à 60 Hz anime tout. */
class VocalBoxView : public juce::Component, private juce::Timer
{
public:
    explicit VocalBoxView (OniriqueVocalBoxAudioProcessor&);
    ~VocalBoxView() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void select (int code);
    void syncSelection();
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
    juce::TooltipWindow tooltips { this, 600 };
    int selected = 0;
    float panelFade = 1.0f, powerLit = 0.0f, drift = 0.0f;
    juce::Image backdrop;   // le fond (taches de braise floues) : dessiné une fois par taille
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

    OniriqueVocalBoxAudioProcessor& processor;
    onirique::ui::FontScope fontScope;   // polices de la charte, le temps que l'interface est ouverte
    VocalBoxView view;
    onirique::nuit::Bar bar;
    juce::Image logo, logoGlow;   // le logo du titre et sa lueur, qui respire
    std::unique_ptr<onirique::license::Overlay> licenseOverlay;
    bool restoringSize = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OniriqueVocalBoxAudioProcessorEditor)
};
