#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include "Biquad.h"
#include "Common.h"

namespace onirique
{
/*  De-esser dynamique à bande divisée, avec anticipation (lookahead ~0.7 ms).

    Action en PARALLÈLE sur le signal retardé :
      y = x_d + (g - 1) * bp_d
    Quand g = 1 le signal est strictement inchangé ; seule la zone sibilante bouge (un passe-bande à 0 dB au centre
    ainsi soustrait est une vraie cloche à phase minimale : aucun creux de phase autour).

    Passe-bande de détection étroit (Q 1.4) sur la fréquence réglée, passe-bande d'action large (Q 0.65) centré un peu
    au-dessus : le S entier descend, pas seulement sa fondamentale.

    Détection RELATIVE : énergie de la bande / énergie large bande, donc indépendante du niveau de la voix. Genou doux
    de 4 dB, attaque 0.5 ms, relâchement naturel de 30 ms, réduction lissée en dB.
    L'anticipation retarde l'audio de 'lookahead' échantillons (latence déclarée).

    Économe : intensité à 0, seul le retard d'anticipation tourne (la latence ne change pas) ; les puissances du bouton
    ne sont recalculées que toutes les 16 échantillons et l'exponentielle du gain seulement pendant une réduction.
*/
class DeEsser
{
public:
    // live = true : aucune anticipation (latence nulle) ; sinon ~0.7 ms d'anticipation
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
        }

        freqS.reset (fs, 0.03);
        amtS.reset (fs, 0.03);
        freqS.setCurrentAndTargetValue (4000.0f);
        amtS.setCurrentAndTargetValue (0.0f);

        bandAtk = coef (0.2);  bandRel = coef (10.0);
        fullAtk = coef (3.0);  fullRel = coef (40.0);
        redAtk  = coef (0.5);  redRel  = coef (30.0);

        wake();
        updateCoeffs (4000.0f, 0.0f);
    }

    int getLatencySamples() const noexcept { return lookahead; }

    // freqHz : 3000..10000, amount01 : 0..1
    void setParams (float freqHz, float amount01, bool snap = false) noexcept
    {
        freqHz = juce::jlimit (3000.0f, 10000.0f, freqHz);
        amount01 = juce::jlimit (0.0f, 1.0f, amount01);

        if (snap)
        {
            freqS.setCurrentAndTargetValue (freqHz);
            amtS.setCurrentAndTargetValue (amount01);
            countdown = 0;
        }
        else
        {
            freqS.setTargetValue (freqHz);
            amtS.setTargetValue (amount01);
        }
    }

    void process (float* const* ch, int numCh, int n) noexcept
    {
        if (amtS.getTargetValue() <= 0.0f && ! amtS.isSmoothing() && red1 < 1.0e-4f)
        {
            // coupé : seul le retard d'anticipation tourne ; tout repart de zéro au réveil
            freqS.skip (n);
            for (int c = 0; c < numCh; ++c)
                for (int i = 0; i < n; ++i) ch[c][i] = dx[c].process (ch[c][i]);
            if (! asleep) { asleep = true; grMeter.store (0.0f, std::memory_order_relaxed); }
            return;
        }
        if (asleep) wake();

        float peakRed = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float f   = freqS.getNextValue();
            const float amt = amtS.getNextValue();

            if (--countdown <= 0)
            {
                updateCoeffs (f, amt);
                countdown = 16;
            }

            float b1[2] { 0.0f, 0.0f };
            float band1 = 0.0f, full = 0.0f;
            for (int c = 0; c < numCh; ++c)
            {
                const float x = ch[c][i];
                band1 = std::max (band1, std::abs (bpDet.process (x, c)));
                b1[c] = bp1.process (x, c);
                full  = std::max (full, std::abs (x));
            }

            env1 += (band1 > env1 ? bandAtk : bandRel) * (band1 - env1);
            envFull += (full > envFull ? fullAtk : fullRel) * (full - envFull);

            float t1 = 0.0f;
            if (amt > 0.001f && env1 > 1.0e-4f)
            {
                const float ratio = env1 / (envFull + 1.0e-6f);
                lastRatio1 = ratio;
                // progressif : réduction max et pente suivent le bouton (1 % ~ 0.3 dB, 10 % ~ 2 dB, 50 % ~ 9 dB, 100 % = 16 dB)
                t1 = reduction (ratio, thr, maxRed, slope);
            }

            red1 += (t1 > red1 ? redAtk : redRel) * (t1 - red1);
            peakRed = std::max (peakRed, red1);
            const float g1m = red1 > 1.0e-5f ? std::exp (-red1 * 0.11512925f) - 1.0f : 0.0f;

            for (int c = 0; c < numCh; ++c)
            {
                const float xd  = dx[c].process (ch[c][i]);
                const float b1d = d1[c].process (b1[c]);
                ch[c][i] = xd + g1m * b1d;
            }
        }
        grMeter.store (peakRed, std::memory_order_relaxed);
    }

    // réduction max du dernier bloc, lisible depuis l'interface
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

    // réveil (ou départ) : filtres, enveloppes et retard de la bande repartent de zéro
    void wake() noexcept
    {
        asleep = false;
        env1 = red1 = 0.0f;
        envFull = 1.0f;   // la référence part haut : aucune fausse réduction le temps que les enveloppes se calent
        bpDet.reset();
        bp1.reset();
        for (auto& d : d1) d.reset();
        countdown = 0;
    }

    void updateCoeffs (float f, float amt) noexcept
    {
        bpDet.setCoeffs (BiquadCoeffs::bandpass (fs, f, 1.4));          // détection étroite
        bp1.setCoeffs   (BiquadCoeffs::bandpass (fs, f * 1.25, 0.65));  // action large, centrée un peu au-dessus
        thr = thrBase1 * std::pow (thrScale1, amt);
        maxRed = 16.0f * std::pow (amt, 0.85f);
        slope = 0.35f + 0.5f * amt;
    }

    float thrBase1 = 0.55f, thrScale1 = 0.27f, lastRatio1 = 0.0f;
    float thr = 0.55f, maxRed = 0.0f, slope = 0.35f;
    double fs = 44100.0;
    int lookahead = 32, countdown = 0;
    bool asleep = false;
    float bandAtk = 0, bandRel = 0, fullAtk = 0, fullRel = 0, redAtk = 0, redRel = 0;
    float env1 = 0, envFull = 0, red1 = 0;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> freqS;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> amtS;
    Biquad bpDet, bp1;
    DelayLine dx[2], d1[2];
    std::atomic<float> grMeter { 0.0f };
};
} // namespace onirique
