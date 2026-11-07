#pragma once

#include "Fft.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace onirique::tune
{

/** Détecteur de hauteur monophonique (YIN, différence normalisée calculée
    par FFT). Il travaille sur le signal d'analyse décimé et rend une période
    fractionnaire. Une fois une note accrochée il la suit : il ne la lâche
    pas sur une mesure douteuse et ne change d'octave que sur une preuve qui
    dure plusieurs mesures.

    Les fenêtres sont ancrées sur l'échantillon le plus récent : la mesure
    décrit donc les (fenêtre + période) derniers échantillons, ce qui donne le
    retard le plus court possible pour une période donnée. */
class PitchDetector
{
public:
    struct Result
    {
        bool voiced = false;
        float period = 0.0f;       // en échantillons d'analyse
        float clarity = 0.0f;      // 1 = parfaitement périodique
        float rms = 0.0f;
        float centreLag = 0.0f;    // retard du centre de la mesure, en échantillons d'analyse
    };

    /** Alloue pour le pire cas (fréquence la plus basse jamais demandée). */
    void prepare (double analysisRate, double lowestFrequency);

    /** Change la plage de recherche. Sans allocation. */
    void configure (double minFrequency, double maxFrequency) noexcept;

    /** 0 = tolérant (voix soufflée, bruit), 1 = exigeant. */
    void setTracking (float amount) noexcept;

    void reset() noexcept;

    /** Analyse les derniers échantillons d'un tampon circulaire ; `end` est
        l'index absolu (exclusif) du dernier échantillon écrit. */
    Result analyse (const float* ring, int mask, std::int64_t end) noexcept;

    int getMaxLag() const noexcept { return tauMax; }

private:
    double analysisRate = 24000.0;
    int worstTau = 0;
    int tauMin = 2, tauMax = 2, window = 2;
    const Fft* fft = nullptr;

    std::vector<std::unique_ptr<Fft>> ffts;
    std::vector<float> re, im, cr, ci, frame, cmnd, evidence;
    std::vector<double> energy;

    float onsetLimit = 0.25f, stayLimit = 0.45f, coastLimit = 0.65f;
    bool wasVoiced = false;
    float lastTau = 0.0f;
    int weakFrames = 0;
    float rivalTau = 0.0f;
    int rivalCount = 0;
    int framesSinceOnset = 0;
    float lastDip = 0.0f;
};

} // namespace onirique::tune
