#pragma once

#include <array>
#include <vector>
#include "SatTube.h"
#include "SatAnalog.h"
#include "SatTape.h"

namespace onirique
{
/*  Saturation "Lab" : trois types au choix, pilotés par le même DRIVE.

    - TUBE   : étage à lampe asymétrique (harmonique 2), transitoires préservés, sag léger.
    - ANALOG : classe A + transformateurs, caractère inspiré du Neve 1073 (graves épais, mediums
               devant, haut arrondi).
    - TAPE   : bande magnétique (aigus tassés, compression lente, head bump).

    Changement de type sans clic : l'étage entrant est remis à zéro puis les deux étages tournent
    en parallèle pendant un fondu de 60 ms. Seul l'étage actif calcule en dehors des fondus.
    Les trois étages ont exactement la même latence (même suréchantillonneur).
    Drive = 0 : chaque type est transparent (seule la latence du suréchantillonneur reste).
*/
class SaturationLab
{
public:
    enum Mode { tube = 0, analog, tape, numModes };

    void prepare (double sampleRate, int maxBlock, int oversamplingOrder = 3)
    {
        for (auto* s : stages) s->prepare (sampleRate, maxBlock, oversamplingOrder);
        jassert (tubeStage.getLatencySamples() == analogStage.getLatencySamples()
                 && tubeStage.getLatencySamples() == tapeStage.getLatencySamples());

        for (auto& b : scratch) b.assign ((size_t) maxBlock, 0.0f);
        fadeLen = std::max (1, juce::roundToInt (0.06 * sampleRate));
        current = target;
        fading = false;
    }

    int getLatencySamples() const noexcept { return tubeStage.getLatencySamples(); }

    void setDrive (float drive0to10, bool snap = false) noexcept
    {
        for (auto* s : stages) s->setDrive (drive0to10, snap);
    }

    void setMode (int mode, bool snap = false) noexcept
    {
        target = juce::jlimit (0, (int) numModes - 1, mode);
        if (snap)
        {
            current = target;
            fading = false;
        }
    }

    void process (float* const* ch, int numCh, int n) noexcept
    {
        if (! fading && target != current)
        {
            from = current;
            current = target;
            stages[(size_t) current]->resetState();
            fadePos = 0;
            fading = true;
        }

        if (! fading)
        {
            stages[(size_t) current]->process (ch, numCh, n);
            return;
        }

        // fondu : l'étage sortant traite une copie, l'étage entrant traite le signal en place
        float* old[2] { scratch[0].data(), scratch[1].data() };
        for (int c = 0; c < numCh; ++c)
            std::copy (ch[c], ch[c] + n, old[c]);

        stages[(size_t) from]->process (old, numCh, n);
        stages[(size_t) current]->process (ch, numCh, n);

        const float inc = 1.0f / (float) fadeLen;
        for (int i = 0; i < n; ++i)
        {
            const float t = std::min (1.0f, (float) (fadePos + i + 1) * inc);
            for (int c = 0; c < numCh; ++c)
                ch[c][i] = old[c][i] + t * (ch[c][i] - old[c][i]);
        }

        fadePos += n;
        if (fadePos >= fadeLen) fading = false;
    }

private:
    SatTube tubeStage;
    SatAnalog analogStage;
    SatTape tapeStage;
    std::array<SatStage*, numModes> stages { &tubeStage, &analogStage, &tapeStage };

    std::vector<float> scratch[2];
    int current = tube, target = tube, from = tube;
    int fadeLen = 1, fadePos = 0;
    bool fading = false;
};
} // namespace onirique
