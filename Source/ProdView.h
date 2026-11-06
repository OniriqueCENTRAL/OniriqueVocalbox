#pragma once
#include "ModulePanels.h"
#include "MixAnalysis.h"

/*  ADAPTATION À LA PROD : la voix comparée à l'instrumentale de la SIDECHAIN.

    - prodview : les outils partagés par l'écran CRÉER LE MIX et la page PROD : le spectre en tiers d'octave d'un signal
      (anneau de 4096 échantillons), le lissage (entre bandes voisines et dans le temps) et la courbe douce qui passe par
      les points (pas de pics en dents de scie).
    - ProdPage : la page PROD, ouverte depuis le bouton PROD de la chaîne une fois le mix appliqué. Les deux spectres en
      grand, les conflits, et ce que la prod fait à la voix : NETTOYAGE (les coupes de la boue partagée), CLARTÉ (les
      remontées quand l'instru masque la voix), l'adaptation en service ou non, et les bandes de l'EQ qui suivent la prod.
      VOCALBOX ne touche jamais à la prod : il écoute l'instru et ne règle que la voix. */
class OniriqueVocalBoxAudioProcessor;

namespace prodview
{
using Bands = std::array<float, onirique::mix::kBands>;

// le spectre d'un signal : les derniers 4096 échantillons, en tiers d'octave, ramené à sa moyenne 200 Hz .. 5 kHz
class Analyser
{
public:
    Analyser();
    void push (const float* x, int n);
    // false si rien ne passe (le signal est muet)
    bool compute (double sampleRate, Bands& out);

private:
    std::vector<float> ring, buf;
    int pos = 0;
    juce::dsp::FFT fft { 12 };
    juce::dsp::WindowingFunction<float> window { (size_t) 4096, juce::dsp::WindowingFunction<float>::hann, false };
};

// deux passes 1/4 1/2 1/4 entre bandes voisines : la forme générale, sans les pics d'un tiers d'octave isolé
void smoothBands (Bands& a);

// une courbe douce (Catmull-Rom) qui passe par les points
juce::Path smoothCurve (const std::vector<juce::Point<float>>& pts);

// le tracé comparatif dans plot : conflits en surbrillance, l'instru (remplie, couleur de l'interface), la voix (trait clair)
void drawCompare (juce::Graphics&, juce::Rectangle<float> plot, const Bands& voice, const Bands& inst, const std::vector<int>& conflicts,
                  float voiceAlpha, float instAlpha, float u);

// les conflits du direct : la prod plus forte que la voix là où la voix compte (200 Hz .. 8 kHz)
std::vector<int> liveConflicts (const Bands& voice, const Bands& inst);

// « 315 Hz · 2.5 kHz »
juce::String zonesText (const std::vector<int>& bands, int maxCount);
} // namespace prodview

// le bouton PROD de l'en-tête de la chaîne : la page ADAPTATION À LA PROD ; son témoin s'allume quand l'instru arrive
class ProdButton : public juce::Component, public Animated, public juce::SettableTooltipClient
{
public:
    explicit ProdButton (OniriqueVocalBoxAudioProcessor& p) : processor (p)
    {
        setTooltip (juce::String::fromUTF8 ("PROD : comment la voix s'adapte à l'instru de la SIDECHAIN (spectres, conflits, NETTOYAGE, CLARTÉ)."));
    }
    void tick() override;
    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true; }
    void mouseExit (const juce::MouseEvent&) override { hover = false; }
    void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); }
    std::function<void()> onClick;
    bool open = false;

private:
    OniriqueVocalBoxAudioProcessor& processor;
    float lit = 0.0f, hot = 0.0f, signal = 0.0f;
    bool hover = false;
};

class ProdPage : public juce::Component, public Animated
{
public:
    explicit ProdPage (OniriqueVocalBoxAudioProcessor&);
    void tick() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    std::function<void()> onClose;

private:
    juce::Rectangle<float> closeArea() const;
    juce::Rectangle<float> plotArea() const;
    juce::StringArray activeBands() const;   // les bandes « SC » de l'EQ, en clair

    OniriqueVocalBoxAudioProcessor& processor;
    prodview::Analyser voiceA, instA;
    prodview::Bands liveV {}, liveX {};
    float scLevel = 0.0f, voiceLit = 0.0f, instLit = 0.0f;
    int countdown = 0;
    std::unique_ptr<GlassToggle> onToggle;
    std::unique_ptr<StarKnob> clean, clarity;
    std::vector<Animated*> anim;
};
