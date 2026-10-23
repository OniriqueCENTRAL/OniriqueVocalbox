#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include "Biquad.h"
#include "Common.h"

namespace onirique
{
/*  1176 : modèle comportemental d'un limiteur FET (inspiré du UREI/UA 1176LN ; aucun schéma ni
    donnée constructeur, modèle original). Le détecteur et la sortie retravaillés pour la voix (comme dans VOCAL MINI).

    - Topologie en contre-réaction comme l'original : le détecteur regarde la SORTIE de l'atténuateur
      FET. Résolue sans délai (formule implicite) : stable même à l'attaque la plus rapide.
      En contre-réaction, une pente k = ratio - 1 appliquée au niveau de sortie donne le ratio voulu.
    - INPUT : gain avant l'étage (seuil fixe, comme le vrai : on "pousse" dans le compresseur).
      OUTPUT : niveau de sortie.
    - ATTACK 1..7 (7 = le plus rapide) : 800 µs -> 20 µs. RELEASE 1..7 : 1.1 s -> 50 ms.
    - RATIO 4 / 8 / 12 / 20 : le seuil monte avec le ratio et le genou se durcit, comme sur l'appareil.
      ALL (tous les boutons enfoncés) : ratio extrême + composante "sur-compression" (la sortie
      redescend quand l'entrée monte encore), relâchement plus court, distorsion accrue.
    - Couleur : le FET ajoute de l'harmonique 2 proportionnelle à la réduction, l'étage de sortie
      classe A arrondit légèrement les crêtes. Blocage DC derrière.
    - Liaison stéréo : un seul détecteur (max des canaux), même gain sur les deux.

    Pour la voix :
    - DÉTECTEUR SANS GRAVES : passe-haut 24 dB/oct à 90 Hz dans le détecteur. Les plosives (P, B) et l'effet de proximité ne font
      plus plonger la compression : elle répond au corps de la voix, pas au souffle dans le micro. Le détecteur est aussi
      un peu moins sensible aux aigus (-3 dB au-dessus de 4 kHz) : les consonnes ne le déclenchent plus autant que la voix.
    - DÉTECTION CRÊTE + RMS COURT (3 ms) : le 1176 attrape toujours les pics, mais sur une voyelle tenue la réduction ne
      suit plus chaque oscillation de l'onde (plus de « grésillement » de modulation aux relâchements rapides). En ALL, la
      crête seule (le grain du « tous les boutons » vient justement de là).
    - RELÂCHEMENT DE PROGRAMME : plus lent quand la réduction est profonde, et quand elle DURE (une mémoire lente se charge
      sur une phrase compressée : jusqu'à 1.8x) ; sur un mot isolé il reste vif. Fini le pompage en fin de phrase.
    - CONSONNES IGNORÉES : une réduction très brève (un K, un T) se relâche en 25 ms : la voyelle qui suit n'est pas
      étouffée ; une réduction qui dure garde son relâchement normal.
    - FER DE SORTIE : le transformateur ne sature que dans le grave (< 120 Hz), un peu plus quand la réduction est forte :
      la voix comprimée garde son poids au lieu de s'amincir.
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
        // chaîne latérale : passe-haut Butterworth 24 dB/oct à 90 Hz (un grondement ou un grave fort ne fait pas plonger la voix)
        scHp.setCoeffs (BiquadCoeffs::highpass (fs, 90.0, 0.5412));
        scHp2.setCoeffs (BiquadCoeffs::highpass (fs, 90.0, 1.3066));
        scHf.setCoeffs (BiquadCoeffs::highShelf (fs, std::min (4000.0, 0.4 * fs), 0.7, -3.0));
        slowC = coefFor (0.040);
        briefRel = coefFor (0.025);
        rmsC = coefFor (0.003);
        memUp = coefFor (0.8);
        memDown = coefFor (2.5);
        ironC = (float) (1.0 - std::exp (-6.283185307179586 * 120.0 / fs));
        reset();
        updateTimes();
    }

    void reset() noexcept
    {
        gr = mem = ms = grSlow = 0.0f;
        dc.reset(); scHp.reset(); scHp2.reset(); scHf.reset();
        iron[0] = iron[1] = 0.0f;
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
        const float rmsShare = all ? 0.0f : 0.4f;   // ALL : la crête seule

        float peakGr = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float gi = inS.getNextValue(), go = outS.getNextValue();

            // ---- détecteur : sans graves, crête et RMS court mêlés
            float pk = 0.0f, p2 = 0.0f;
            for (int c = 0; c < numCh; ++c)
            {
                const float s = scHf.process (scHp2.process (scHp.process (ch[c][i] * gi, c), c), c);
                pk = std::max (pk, std::abs (s));
                p2 = std::max (p2, s * s);
            }
            ms += rmsC * (p2 - ms);
            const float det = std::sqrt ((1.0f - rmsShare) * pk * pk + rmsShare * 2.0f * ms);
            const float lvl = det > 1.0e-6f ? gainToDb (det) : -120.0f;
            const float over = lvl - thr;

            // genou doux : la pente monte progressivement autour du seuil (estimée sur le niveau de sortie précédent)
            const float outEst = over - gr;
            const float kk = k * juce::jlimit (0.0f, 1.0f, (outEst + 0.5f * knee) / knee);
            const float extra = all ? 1.1f * std::max (0.0f, over) : 0.0f;    // sur-compression "ALL" : la sortie plafonne puis redescend

            // réduction brève (consonne) : relâchement rapide ; réduction qui dure : relâchement normal
            const float brief = gr > 0.5f ? juce::jlimit (0.0f, 1.0f, 2.0f * (gr - grSlow) / gr) : 0.0f;
            const float rn = relCoef();
            const float rel = rn + brief * (briefRel - rn);

            float next;
            if (over <= 0.0f && extra <= 0.0f)
                next = gr * (1.0f - rel);
            else
            {
                // contre-réaction résolue : gr' = gr + c (kk (over - gr') + extra - gr)
                auto solve = [&] (float c) { return (gr * (1.0f - c) + c * (kk * over + extra)) / (1.0f + c * kk); };
                next = solve (atk);
                if (next < gr) next = solve (rel);
            }
            gr = juce::jlimit (0.0f, 40.0f, next);
            grSlow += slowC * (gr - grSlow);
            mem += (gr > mem ? memUp : memDown) * (gr - mem);
            peakGr = std::max (peakGr, gr);

            const float g = (gr > 1.0e-6f ? dbToGain (-gr) : 1.0f) * gi;
            const float fet = h2 * std::min (1.0f, gr / 12.0f);
            const float ironDrive = 1.2f + 0.08f * gr;
            for (int c = 0; c < numCh; ++c)
            {
                float y = ch[c][i] * g;
                y += fet * y * y;                            // FET : harmonique 2 avec la réduction (DC retiré ensuite)
                y = y / (1.0f + 0.06f * y * y);              // étage classe A : crêtes arrondies
                iron[c] += ironC * (y - iron[c]);            // fer : seul le grave sature
                y += std::tanh (ironDrive * iron[c]) / ironDrive - iron[c];
                ch[c][i] = dc.process (y, c) * go;
            }
        }
        grMeter.store (peakGr, std::memory_order_relaxed);
    }

    float getGainReductionDb() const noexcept { return grMeter.load (std::memory_order_relaxed); }

    // copie de l'état complet (réduction, mémoires, lissages, filtres) : sert au fondu d'inversion d'ordre
    void copyStateFrom (const FetCompressor& o) noexcept
    {
        fs = o.fs; atkKnob = o.atkKnob; relKnob = o.relKnob; ratio = o.ratio;
        atk = o.atk; relBase = o.relBase; invRel = o.invRel; gr = o.gr; mem = o.mem; ms = o.ms; grSlow = o.grSlow;
        rmsC = o.rmsC; memUp = o.memUp; memDown = o.memDown; ironC = o.ironC; slowC = o.slowC; briefRel = o.briefRel;
        iron[0] = o.iron[0]; iron[1] = o.iron[1];
        inS = o.inS; outS = o.outS; dc = o.dc; scHp = o.scHp; scHp2 = o.scHp2; scHf = o.scHf;
    }

private:
    float coefFor (double seconds) const noexcept { return (float) (1.0 - std::exp (-1.0 / (seconds * fs))); }

    void updateTimes() noexcept
    {
        const bool all = (ratio == 4);
        atk = coefFor (attackSeconds (atkKnob) * (all ? 1.4f : 1.0f));
        relBase = (float) (releaseSeconds (relKnob) * (all ? 0.7f : 1.0f));
        invRel = (float) (1.0 / (relBase * fs));
    }

    // relâchement dépendant du programme : plus lent quand la réduction est profonde, et quand elle dure (mémoire).
    // 1 - exp(-x) ~ x (1 - x/2) : x < 5e-4 ici (relâchement >= 35 ms), écart < 1e-7 ; pas d'exponentielle par échantillon.
    float relCoef() const noexcept
    {
        const float x = invRel / ((1.0f + 0.4f * std::min (1.0f, gr / 20.0f)) * (1.0f + 0.8f * std::min (1.0f, mem / 8.0f)));
        return x * (1.0f - 0.5f * x);
    }

    double fs = 44100.0;
    float atkKnob = 3.0f, relKnob = 5.0f;
    int ratio = 0;
    float atk = 0.5f, relBase = 0.2f, invRel = 1.0e-4f, gr = 0.0f, mem = 0.0f, ms = 0.0f, grSlow = 0.0f;
    float rmsC = 0.0f, memUp = 0.0f, memDown = 0.0f, ironC = 0.0f, slowC = 0.0f, briefRel = 0.0f, iron[2] {};
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> inS { 1.0f }, outS { 1.0f };
    Biquad dc, scHp, scHp2, scHf;
    std::atomic<float> grMeter { 0.0f };
};
} // namespace onirique
