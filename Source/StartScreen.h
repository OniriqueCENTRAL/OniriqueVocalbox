#pragma once
#include "ModulePanels.h"
#include "MixAnalysis.h"
#include <thread>

/*  L'écran de départ (chaîne vide) : le texte en haut, au milieu la sphère de points braise parcourue d'ondes qui suivent la
    voix, en bas la barre de recherche où l'on décrit le mix voulu (un artiste, un style). On peut aussi glisser un morceau de
    l'artiste sur la sphère : il servira de référence à l'analyse. */
class StartScreen : public juce::Component, public Animated, public juce::FileDragAndDropTarget
{
public:
    explicit StartScreen (OniriqueVocalBoxAudioProcessor&);
    ~StartScreen() override;
    void tick() override;
    void themeChanged() { halo = {}; repaint(); }
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragging = true; }
    void fileDragExit (const juce::StringArray&) override { dragging = false; }
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    void submit();
    void finish();                // la prise et la référence sont là : le plan, le compte rendu
    void apply();                 // la chaîne montée dans le plugin
    void stopWorker();
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

    // CRÉER LE MIX : idle → listening (référence en calcul, prise en écoute) → report (compte rendu) → la chaîne
    enum class Stage { idle, listening, report };
    Stage stage = Stage::idle;
    onirique::mix::Hints hints;
    onirique::mix::Profile refProfile;
    std::atomic<int> refState { 0 };        // 0 aucune, 1 en calcul, 2 prête, 3 illisible
    std::atomic<bool> cancel { false };
    std::atomic<float> refProgress { 0.0f };
    std::atomic<bool> refAi { false };
    std::thread worker;
    onirique::mix::Plan plan;
    double stageStart = 0.0, lastHeard = 0.0;
    float progress = 0.0f;
    bool forceBuild = false;

    // SIDECHAIN : l'instrumentale qui arrive, et la comparaison de son spectre avec celui de la voix
    void pushRing (std::vector<float>& ring, int& pos, const float* x, int n);
    void bandsOf (const std::vector<float>& ring, int pos, std::array<float, onirique::mix::kBands>& out);
    void paintSidechain (juce::Graphics&, float u);
    juce::Rectangle<float> scPanelArea() const;
    std::vector<float> vRing, xRing, fftBuf;
    int vPos = 0, xPos = 0, fftCountdown = 0;
    std::array<float, onirique::mix::kBands> liveV {}, liveX {};   // dB lissés, chacun ramené à sa moyenne 200 Hz .. 5 kHz
    float scLevel = 0.0f, liveVoice = 0.0f, liveInst = 0.0f;         // 0..1 : le signal reçu
    onirique::mix::Context context;                                 // l'instrumentale pendant la prise (compte rendu)
    std::unique_ptr<juce::dsp::FFT> fft;
    std::unique_ptr<juce::dsp::WindowingFunction<float>> window;
};
