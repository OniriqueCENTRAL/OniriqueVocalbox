#pragma once
#include "ModulePanels.h"

/*  L'écran de départ (chaîne vide) : le texte en haut, au milieu la sphère de points braise parcourue d'ondes qui suivent la
    voix, en bas la barre de recherche où l'on décrit le mix voulu (un artiste, un style). On peut aussi glisser un morceau de
    l'artiste sur la sphère : il servira de référence à l'analyse. */
class StartScreen : public juce::Component, public Animated, public juce::FileDragAndDropTarget
{
public:
    explicit StartScreen (OniriqueVocalBoxAudioProcessor&);
    void tick() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragging = true; }
    void fileDragExit (const juce::StringArray&) override { dragging = false; }
    void filesDropped (const juce::StringArray& files, int, int) override;

    // la demande (texte et morceau de référence) : branchée sur l'analyse à l'étape suivante
    std::function<void (const juce::String& prompt, const juce::File& reference)> onCreate;

private:
    void submit();
    juce::Rectangle<float> sphereArea() const;
    juce::Rectangle<float> barArea() const;
    juce::Rectangle<float> goArea() const;

    OniriqueVocalBoxAudioProcessor& processor;
    juce::TextEditor search;
    juce::File reference;
    juce::String status;

    struct Dot { float x, y, z; };
    std::vector<Dot> dots;
    juce::Image halo;
    float level = 0.0f, energy = 0.0f, dropGlow = 0.0f, goHot = 0.0f, listen = 0.0f;
    double t0 = 0.0, listenUntil = 0.0;
    bool dragging = false, overGo = false;
};
