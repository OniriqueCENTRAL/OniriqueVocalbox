#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include "Comp1176.h"
#include "DeEsser.h"

namespace onirique
{
/*  PARALLEL : compression parallèle "New York" avec l'algorithme du 1176.

    Comme une piste auxiliaire : une copie de la voix part dans un 1176 réglé pour écraser
    (INPUT +16 dB, ratio ALL, attaque rapide, relâchement rapide : 15 à 30 dB de réduction sur une voix) ; ce bus très compressé est ramené
    au niveau RMS de la voix d'origine (fenêtre ~300 ms) puis REMIS DANS LA VOIX :
        sortie = voix + AMOUNT x bus écrasé
    AMOUNT 0 % : rien ne change ; 50 % : le bus revient ~6 dB sous la voix ; 100 % : au même niveau.
    Les détails, la fin des mots et le souffle remontent, les crêtes restent celles de la voix d'origine :
    on gagne en densité sans perdre les attaques. Aucune latence (même phase des deux côtés : pas de filtrage
    en peigne). Le volume monte un peu avec AMOUNT, comme sur une vraie console : l'AUTO GAIN peut compenser.
    DE-ESSER du bus : le même de-esser que la carte DE-ESSER (fréquence + intensité), sur le bus écrasé seulement.
    Écraser une voix fait ressortir ses S : on les calme sur le bus, la voix d'origine reste intacte. Sans
    anticipation (aucune latence, le bus reste en phase avec la voix).
*/
class ParallelComp
{
public:
    void prepare (double sampleRate, int maxBlock)
    {
        fs = sampleRate;
        crush.prepare (fs);
        deess.prepare (fs, true);   // sans anticipation : le bus reste aligné sur la voix
        crush.setParams (16.0f, 0.0f, 6.0f, 6.5f, 4, true);   // INPUT +16 dB, attaque ~40 us, release ~65 ms, ratio ALL
        for (auto& b : bus) b.assign ((size_t) maxBlock, 0.0f);
        amountS.reset (fs, 0.05);
        envC = (float) (1.0 - std::exp (-1.0 / (0.3 * fs)));
        matchC = (float) (1.0 - std::exp (-1.0 / (0.05 * fs)));
        reset();
    }

    void reset() noexcept
    {
        crush.reset();
        dryPow = busPow = 0.0f;
        match = 1.0f;
    }

    // AUTO : réglage d'usine ci-dessus. Sinon seuil (dBFS), attaque et relâchement (boutons 1..7 du 1176).
    // Le seuil du 1176 en ratio ALL est fixe (-21 dBFS) : régler le seuil revient à régler son INPUT.
    void setControl (bool autoMode, float thresholdDb, float attack, float release, bool snap) noexcept
    {
        if (autoMode) crush.setParams (16.0f, 0.0f, 6.0f, 6.5f, 4, snap);
        else          crush.setParams (juce::jlimit (-12.0f, 36.0f, -21.0f - thresholdDb), 0.0f, attack, release, 4, snap);
    }

    void setAmount (float amount01, bool snap) noexcept
    {
        amount01 = juce::jlimit (0.0f, 1.0f, amount01);
        if (snap) amountS.setCurrentAndTargetValue (amount01); else amountS.setTargetValue (amount01);
    }

    void process (float* const* ch, int numCh, int n) noexcept
    {
        float* b[2] { bus[0].data(), bus[1].data() };
        for (int c = 0; c < numCh; ++c) std::copy (ch[c], ch[c] + n, b[c]);
        crush.process (b, numCh, n);
        deess.process (b, numCh, n);   // les S que l'écrasement fait ressortir (à 0 %, le bus passe intact)

        for (int i = 0; i < n; ++i)
        {
            // le bus écrasé est ramené au niveau RMS de la voix (comme le fader d'une aux bien réglée)
            float pd = 0.0f, pb = 0.0f;
            for (int c = 0; c < numCh; ++c) { pd += ch[c][i] * ch[c][i]; pb += b[c][i] * b[c][i]; }
            dryPow += envC * (pd - dryPow);
            busPow += envC * (pb - busPow);
            if (dryPow > 1.0e-9f)
            {
                const float target = juce::jlimit (0.05f, 4.0f, std::sqrt (dryPow / (busPow + 1.0e-12f)));
                match += matchC * (target - match);
            }

            const float a = amountS.getNextValue() * match;
            for (int c = 0; c < numCh; ++c) ch[c][i] += a * b[c][i];
        }
    }

    // de-esser du bus : fréquence 3..10 kHz, intensité 0..1
    void setDeEsser (float freqHz, float amount01, bool snap) noexcept { deess.setParams (freqHz, amount01, 0.0f, snap); deessOn = amount01 > 0.0005f; }
    float getDeEssReductionDb() const noexcept { return deessOn ? deess.getReductionDb() : 0.0f; }

    float getGainReductionDb() const noexcept { return crush.getGainReductionDb(); }

private:
    double fs = 44100.0;
    FetCompressor crush;
    DeEsser deess;
    bool deessOn = false;
    std::vector<float> bus[2];
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> amountS;
    float envC = 0.0f, matchC = 0.0f, dryPow = 0.0f, busPow = 0.0f, match = 1.0f;
};
} // namespace onirique
