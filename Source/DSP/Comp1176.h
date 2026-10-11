#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include "Biquad.h"
#include "Common.h"

namespace onirique
{
/*  1176 : modèle comportemental d'un limiteur FET (inspiré du UREI/UA 1176LN ; aucun schéma ni
    donnée constructeur, modèle original).

    - Topologie en contre-réaction comme l'original : le détecteur regarde la SORTIE de l'atténuateur
      FET. Résolue sans délai (formule implicite) : stable même à l'attaque la plus rapide.
      En contre-réaction, une pente k = ratio - 1 appliquée au niveau de sortie donne le ratio voulu.
    - Détecteur crête redressé échantillon par échantillon : aux temps rapides, la réduction suit
      l'onde elle-même -> la distorsion basse fréquence typique du 1176 réglé "à fond".
    - INPUT : gain avant l'étage (seuil fixe, comme le vrai : on "pousse" dans le compresseur).
      OUTPUT : niveau de sortie.
    - ATTACK 1..7 (7 = le plus rapide) : 800 µs -> 20 µs. RELEASE 1..7 : 1.1 s -> 50 ms,
      le relâchement ralentit un peu quand la réduction est profonde (comportement de programme).
    - RATIO 4 / 8 / 12 / 20 : le seuil monte avec le ratio et le genou se durcit, comme sur l'appareil.
      ALL (tous les boutons enfoncés) : ratio extrême + composante "sur-compression" (la sortie
      redescend quand l'entrée monte encore), relâchement plus court, distorsion accrue.
    - Couleur : le FET ajoute de l'harmonique 2 proportionnelle à la réduction, l'étage de sortie
      classe A / transformateur arrondit légèrement les crêtes. Blocage DC derrière.
    - Liaison stéréo : un seul détecteur (max des canaux), même gain sur les deux.
*/
class FetCompressor
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        inS.reset (fs, 0.03);
        outS.reset (fs, 0.03);
        dc.setCoeffs (BiquadCoeffs::highpass (fs, 10.0, 0.707));
        reset();
        updateTimes();
    }

    void reset() noexcept
    {
        gr = 0.0f;
        dc.reset();
        grMeter.store (0.0f, std::memory_order_relaxed);
    }

    // inDb -12..+36, outDb -24..+24, attack/release 1..7, ratioIdx 0..4 (4, 8, 12, 20, ALL)
    void setParams (float inDb, float outDb, float attack, float release, int ratioIdx, bool snap) noexcept
    {
        const float gi = dbToGain (inDb), go = dbToGain (outDb);
        if (snap) { inS.setCurrentAndTargetValue (gi); outS.setCurrentAndTargetValue (go); }
        else      { inS.setTargetValue (gi); outS.setTargetValue (go); }

        ratioIdx = juce::jlimit (0, 4, ratioIdx);
        if (attack != atkKnob || release != relKnob || ratioIdx != ratio)
        {
            atkKnob = juce::jlimit (1.0f, 7.0f, attack);
            relKnob = juce::jlimit (1.0f, 7.0f, release);
            ratio = ratioIdx;
            updateTimes();
        }
    }

    static float attackSeconds (float knob) noexcept  { return 800.0e-6f * std::pow (20.0f / 800.0f, (knob - 1.0f) / 6.0f); }
    static float releaseSeconds (float knob) noexcept { return 1.1f * std::pow (0.05f / 1.1f, (knob - 1.0f) / 6.0f); }

    void process (float* const* ch, int numCh, int n) noexcept
    {
        static constexpr float kSlope[] { 3.0f, 7.0f, 11.0f, 19.0f, 30.0f };      // ratio - 1 (contre-réaction)
        static constexpr float kThr[]   { -20.0f, -18.0f, -17.0f, -16.0f, -21.0f };  // le seuil monte avec le ratio
        static constexpr float kKnee[]  { 8.0f, 6.0f, 5.0f, 4.0f, 3.0f };
        const float k = kSlope[ratio], thr = kThr[ratio], knee = kKnee[ratio];
        const bool all = (ratio == 4);
        const float h2 = all ? 0.16f : 0.07f;

        float peakGr = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float gi = inS.getNextValue(), go = outS.getNextValue();

            float pk = 0.0f;
            for (int c = 0; c < numCh; ++c) pk = std::max (pk, std::abs (ch[c][i] * gi));
            const float lvl = pk > 1.0e-6f ? gainToDb (pk) : -120.0f;
            const float over = lvl - thr;

            // genou doux : la pente monte progressivement autour du seuil (estimée sur le niveau de sortie précédent)
            const float outEst = over - gr;
            const float kk = k * juce::jlimit (0.0f, 1.0f, (outEst + 0.5f * knee) / knee);
            const float extra = all ? 1.1f * std::max (0.0f, over) : 0.0f;    // sur-compression "ALL" : la sortie plafonne puis redescend

            float next;
            if (over <= 0.0f && extra <= 0.0f)
                next = gr * (1.0f - relCoef (gr));
            else
            {
                // contre-réaction résolue : gr' = gr + c (kk (over - gr') + extra - gr)
                auto solve = [&] (float c) { return (gr * (1.0f - c) + c * (kk * over + extra)) / (1.0f + c * kk); };
                next = solve (atk);
                if (next < gr) next = solve (relCoef (gr));
            }
            gr = juce::jlimit (0.0f, 40.0f, next);
            peakGr = std::max (peakGr, gr);

            const float g = dbToGain (-gr) * gi;
            const float fet = h2 * std::min (1.0f, gr / 12.0f);
            for (int c = 0; c < numCh; ++c)
            {
                float y = ch[c][i] * g;
                y += fet * y * y;                            // FET : harmonique 2 avec la réduction (DC retiré ensuite)
                y = y / (1.0f + 0.06f * y * y);              // étage de sortie : crêtes arrondies
                ch[c][i] = dc.process (y, c) * go;
            }
        }
        grMeter.store (peakGr, std::memory_order_relaxed);
    }

    float getGainReductionDb() const noexcept { return grMeter.load (std::memory_order_relaxed); }

    // copie de l'état complet (réduction, lissages, filtres) : sert au fondu d'inversion d'ordre
    void copyStateFrom (const FetCompressor& o) noexcept
    {
        fs = o.fs; atkKnob = o.atkKnob; relKnob = o.relKnob; ratio = o.ratio;
        atk = o.atk; relBase = o.relBase; gr = o.gr;
        inS = o.inS; outS = o.outS; dc = o.dc;
    }

private:
    float coefFor (double seconds) const noexcept { return (float) (1.0 - std::exp (-1.0 / (seconds * fs))); }

    void updateTimes() noexcept
    {
        const bool all = (ratio == 4);
        atk = coefFor (attackSeconds (atkKnob) * (all ? 1.4f : 1.0f));
        relBase = (float) (releaseSeconds (relKnob) * (all ? 0.7f : 1.0f));
    }

    // relâchement dépendant du programme : jusqu'à 1.6x plus lent pour une réduction profonde
    float relCoef (float depth) const noexcept
    {
        return coefFor (relBase * (1.0f + 0.6f * std::min (1.0f, depth / 20.0f)));
    }

    double fs = 44100.0;
    float atkKnob = 3.0f, relKnob = 5.0f;
    int ratio = 0;
    float atk = 0.5f, relBase = 0.2f, gr = 0.0f;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> inS { 1.0f }, outS { 1.0f };
    Biquad dc;
    std::atomic<float> grMeter { 0.0f };
};
} // namespace onirique
