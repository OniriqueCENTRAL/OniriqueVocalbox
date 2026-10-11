#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include "Biquad.h"
#include "Common.h"

namespace onirique
{
/*  LA-2A : modèle comportemental d'un niveleur optique à lampes (inspiré du Teletronix LA-2A ; aucun
    schéma ni donnée constructeur, modèle original).

    - Cellule optique (panneau électroluminescent + photorésistance) : c'est elle qui fait le son.
        · lumière L : attaque ~10 ms, chute rapide ~60 ms ;
        · mémoire M de la photorésistance : se charge lentement (~1.5 s) quand on compresse longtemps
          et se vide en ~3.5 s. La réduction réelle vaut max(L, 0.5 M) : relâchement en DEUX TEMPS,
          rapide sur les mots isolés, de plus en plus long sur une phrase compressée en continu.
    - Contre-réaction (le détecteur voit la sortie de la cellule), résolue sans délai.
    - PEAK REDUCTION 0..100 : gain du circuit de détection (= seuil qui descend de 0 à -45 dB).
      GAIN : niveau de sortie. COMPRESS : ~3:1 genou très doux. LIMIT : ~10:1 genou plus serré.
    - Couleur à lampes : un peu d'harmonique 2 constante + transformateur qui arrondit les crêtes.
    - Liaison stéréo : un seul détecteur, même gain sur les deux canaux.
*/
class OptoCompressor
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        gainS.reset (fs, 0.03);
        prS.reset (fs, 0.05);
        dc.setCoeffs (BiquadCoeffs::highpass (fs, 10.0, 0.707));
        lAtk = coef (0.010);
        lRel = coef (0.060);
        mUp = coef (1.5);
        mDown = coef (3.5);
        reset();
    }

    void reset() noexcept
    {
        L = M = gr = 0.0f;
        dc.reset();
        grMeter.store (0.0f, std::memory_order_relaxed);
    }

    // gainDb -12..+30, peak 0..100, limit = mode LIMIT
    void setParams (float gainDb, float peak, bool isLimit, bool snap) noexcept
    {
        const float g = dbToGain (gainDb), p = juce::jlimit (0.0f, 100.0f, peak);
        if (snap) { gainS.setCurrentAndTargetValue (g); prS.setCurrentAndTargetValue (p); }
        else      { gainS.setTargetValue (g); prS.setTargetValue (p); }
        limit = isLimit;
    }

    void process (float* const* ch, int numCh, int n) noexcept
    {
        const float k = limit ? 9.0f : 2.0f, knee = limit ? 6.0f : 14.0f;
        float peakGr = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            const float go = gainS.getNextValue();
            const float pr = prS.getNextValue();

            float pk = 0.0f;
            for (int c = 0; c < numCh; ++c) pk = std::max (pk, std::abs (ch[c][i]));
            // PEAK REDUCTION = gain du détecteur : à 0 rien ne compresse, à 100 le seuil est à -45 dBFS
            const float over = (pk > 1.0e-6f ? gainToDb (pk) : -120.0f) - (0.0f - 0.45f * pr);

            const float outEst = over - gr;
            const float kk = k * juce::jlimit (0.0f, 1.0f, (outEst + 0.5f * knee) / knee);

            // lumière : contre-réaction résolue (la cellule voit sa propre sortie)
            float next;
            if (over <= 0.0f || pr < 0.01f)
                next = L * (1.0f - lRel);
            else
            {
                auto solve = [&] (float c) { return (L * (1.0f - c) + c * kk * over) / (1.0f + c * kk); };
                next = solve (lAtk);
                if (next < L) next = solve (lRel);
            }
            L = juce::jlimit (0.0f, 40.0f, next);
            M += (L > M ? mUp : mDown) * (L - M);
            gr = std::max (L, 0.5f * M);
            peakGr = std::max (peakGr, gr);

            const float g = dbToGain (-gr);
            for (int c = 0; c < numCh; ++c)
            {
                float y = ch[c][i] * g;
                y += 0.025f * y * y;                      // lampes : harmonique 2 discrète (DC retiré ensuite)
                y = y / (1.0f + 0.04f * y * y);           // transformateur de sortie
                ch[c][i] = dc.process (y, c) * go;
            }
        }
        grMeter.store (peakGr, std::memory_order_relaxed);
    }

    float getGainReductionDb() const noexcept { return grMeter.load (std::memory_order_relaxed); }

    // copie de l'état complet (cellule optique, mémoire, lissages, filtres) : sert au fondu d'inversion d'ordre
    void copyStateFrom (const OptoCompressor& o) noexcept
    {
        fs = o.fs; limit = o.limit;
        lAtk = o.lAtk; lRel = o.lRel; mUp = o.mUp; mDown = o.mDown;
        L = o.L; M = o.M; gr = o.gr;
        gainS = o.gainS; prS = o.prS; dc = o.dc;
    }

private:
    float coef (double seconds) const noexcept { return (float) (1.0 - std::exp (-1.0 / (seconds * fs))); }

    double fs = 44100.0;
    bool limit = false;
    float lAtk = 0, lRel = 0, mUp = 0, mDown = 0;
    float L = 0, M = 0, gr = 0;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> gainS { 1.0f };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> prS;
    Biquad dc;
    std::atomic<float> grMeter { 0.0f };
};
} // namespace onirique
