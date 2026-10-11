#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include "Biquad.h"
#include "Common.h"

namespace onirique
{
/*  De-esser dynamique à bandes divisées, avec anticipation (lookahead ~0.7 ms).

    Deux bandes indépendantes, actionnées en PARALLÈLE sur le signal retardé :
      y = x_d + (g1 - 1) * bp1_d + (g2 - 1) * bp2_d
    Quand g = 1 le signal est strictement inchangé ; seule la zone sibilante bouge.

    BANDE 1 (de-esser) : passe-bande Q 1.1 centré sur la fréquence réglée par l'utilisateur.
    BANDE 2 (garde HIGH, "SMART S", activable) : passe-bande large Q 0.7 centré ~1.6x la fréquence (5.2-9.5 kHz),
        pilotée par le bouton HIGH quand SMART S est actif : elle freine en douceur (pente 0.5, 6 dB max) les S que le boost
        d'aigus fait ressortir, même quand l'intensité du de-esser est à 0.

    Détection RELATIVE : énergie de la bande / énergie large bande, donc indépendante
    du niveau de la voix. Genou doux de 4 dB, pente 0.85, attaque 0.5 ms, relâchement
    naturel (30 ms bande 1, 55 ms garde), réduction lissée en dB.
    L'anticipation retarde l'audio de 'lookahead' échantillons (latence déclarée).
*/
class DeEsser
{
public:
    // live = true : aucune anticipation (latence nulle, mode LIVE) ; sinon ~0.7 ms d'anticipation
    // multipleOf : la latence est arrondie à un multiple du facteur de suréchantillonnage de la chaîne
    void prepare (double sampleRate, bool live = false, int multipleOf = 1)
    {
        fs = sampleRate;
        multipleOf = std::max (1, multipleOf);
        lookahead = live ? 0 : std::max (multipleOf, (int) std::lround (0.0007 * fs / multipleOf) * multipleOf);
        for (int c = 0; c < 2; ++c)
        {
            dx[c].prepare (lookahead);
            d1[c].prepare (lookahead);
            d2[c].prepare (lookahead);
        }

        freqS.reset (fs, 0.03);
        amtS.reset (fs, 0.03);
        protS.reset (fs, 0.03);
        freqS.setCurrentAndTargetValue (4000.0f);
        amtS.setCurrentAndTargetValue (0.0f);
        protS.setCurrentAndTargetValue (0.0f);

        bandAtk = coef (0.2);  bandRel = coef (10.0);
        fullAtk = coef (3.0);  fullRel = coef (40.0);
        redAtk  = coef (0.5);
        redRel1 = coef (30.0); redRel2 = coef (55.0);

        env1 = env2 = envFull = 0.0f;
        red1 = red2 = 0.0f;
        bpDet.reset();
        bp1.reset();
        bp2.reset();
        countdown = 0;
        updateCoeffs (4000.0f);
    }

    int getLatencySamples() const noexcept { return lookahead; }

    // freqHz : 3000..10000, amount01 : 0..1, protect01 : 0..1
    void setParams (float freqHz, float amount01, float protect01, bool snap = false) noexcept
    {
        freqHz = juce::jlimit (3000.0f, 10000.0f, freqHz);
        amount01 = juce::jlimit (0.0f, 1.0f, amount01);
        protect01 = juce::jlimit (0.0f, 1.0f, protect01);

        if (snap)
        {
            freqS.setCurrentAndTargetValue (freqHz);
            amtS.setCurrentAndTargetValue (amount01);
            protS.setCurrentAndTargetValue (protect01);
            countdown = 0;
        }
        else
        {
            freqS.setTargetValue (freqHz);
            amtS.setTargetValue (amount01);
            protS.setTargetValue (protect01);
        }
    }

    void process (float* const* ch, int numCh, int n) noexcept
    {
        float peakRed = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float f   = freqS.getNextValue();
            const float amt = amtS.getNextValue();
            const float prt = protS.getNextValue();

            if (--countdown <= 0)
            {
                updateCoeffs (f);
                countdown = 16;
            }

            float b1[2] { 0.0f, 0.0f }, b2[2] { 0.0f, 0.0f };
            float band1 = 0.0f, band2 = 0.0f, full = 0.0f;
            for (int c = 0; c < numCh; ++c)
            {
                const float x = ch[c][i];
                band1 = std::max (band1, std::abs (bpDet.process (x, c)));
                b1[c] = bp1.process (x, c);
                b2[c] = bp2.process (x, c);
                band2 = std::max (band2, std::abs (b2[c]));
                full  = std::max (full,  std::abs (x));
            }

            env1 += (band1 > env1 ? bandAtk : bandRel) * (band1 - env1);
            env2 += (band2 > env2 ? bandAtk : bandRel) * (band2 - env2);
            envFull += (full > envFull ? fullAtk : fullRel) * (full - envFull);

            // ---- bande 1 : de-esser utilisateur
            float t1 = 0.0f;
            if (amt > 0.001f && env1 > 1.0e-4f)
            {
                const float ratio = env1 / (envFull + 1.0e-6f);
                lastRatio1 = ratio;
                const float thr = thrBase1 * std::pow (thrScale1, amt);
                // progressif : réduction max et pente suivent le bouton (1 % ~ 0.3 dB, 10 % ~ 2 dB, 50 % ~ 9 dB, 100 % = 16 dB)
                t1 = reduction (ratio, thr, 16.0f * std::pow (amt, 0.85f), 0.35f + 0.5f * amt);
            }

            // ---- bande 2 : garde pilotée par HIGH (SMART S), volontairement douce : seuil plus haut,
            //      pente 0.5, 6 dB max -> seuls les S qui ressortent vraiment sont freinés
            float t2 = 0.0f;
            if (prt > 0.001f && env2 > 1.0e-4f)
            {
                const float ratio = env2 / (envFull + 1.0e-6f);
                const float thr = 0.62f * std::pow (0.55f, prt);
                t2 = reduction (ratio, thr, 6.0f * prt, 0.5f);
            }

            red1 += (t1 > red1 ? redAtk : redRel1) * (t1 - red1);
            red2 += (t2 > red2 ? redAtk : redRel2) * (t2 - red2);
            peakRed = std::max (peakRed, red1 + red2);

            const float g1m = std::exp (-red1 * 0.11512925f) - 1.0f;
            const float g2m = std::exp (-red2 * 0.11512925f) - 1.0f;

            for (int c = 0; c < numCh; ++c)
            {
                const float xd  = dx[c].process (ch[c][i]);
                const float b1d = d1[c].process (b1[c]);
                const float b2d = d2[c].process (b2[c]);
                ch[c][i] = xd + g1m * b1d + g2m * b2d;
            }
        }
        grMeter.store (peakRed, std::memory_order_relaxed);
    }

    // réduction max du dernier bloc (bande utilisateur + garde SMART S), lisible depuis l'interface
    float getReductionDb() const noexcept { return grMeter.load (std::memory_order_relaxed); }
    float getDetectorRatio() const noexcept { return lastRatio1; }

private:
    static float reduction (float ratio, float thr, float maxRed, float slope) noexcept
    {
        if (ratio < thr * 0.6f) return 0.0f;
        constexpr float knee = 4.0f;
        const float over = 8.68588963807f * std::log (ratio / thr);
        float red;
        if (over <= -0.5f * knee)      return 0.0f;
        else if (over < 0.5f * knee)   red = slope * (over + 0.5f * knee) * (over + 0.5f * knee) / (2.0f * knee);
        else                           red = slope * over;
        return std::min (red, maxRed);
    }

    float coef (double ms) const noexcept
    {
        return (float) (1.0 - std::exp (-1.0 / (ms * 0.001 * fs)));
    }

    void updateCoeffs (float f) noexcept
    {
        bpDet.setCoeffs (BiquadCoeffs::bandpass (fs, f, 1.4));   // détection étroite
        bp1.setCoeffs   (BiquadCoeffs::bandpass (fs, f * 1.25, 0.65));   // action large, centrée un peu au-dessus
        bp2.setCoeffs (BiquadCoeffs::bandpass (fs, std::clamp (1.6 * (double) f, 5200.0, 9500.0), 0.7));
    }

    float thrBase1 = 0.55f, thrScale1 = 0.27f, lastRatio1 = 0.0f;
    double fs = 44100.0;
    int lookahead = 32, countdown = 0;
    float bandAtk = 0, bandRel = 0, fullAtk = 0, fullRel = 0, redAtk = 0, redRel1 = 0, redRel2 = 0;
    float env1 = 0, env2 = 0, envFull = 0, red1 = 0, red2 = 0;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> freqS;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> amtS, protS;
    Biquad bpDet, bp1, bp2;
    DelayLine dx[2], d1[2], d2[2];
    std::atomic<float> grMeter { 0.0f };
};
} // namespace onirique
