#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include "Biquad.h"
#include "Common.h"

namespace onirique
{
/*  LA-2A : modèle comportemental d'un niveleur optique à lampes (inspiré du Teletronix LA-2A ; aucun
    schéma ni donnée constructeur, modèle original). Le détecteur et les lampes retravaillés pour la voix (comme dans
    VOCAL MINI).

    - Cellule optique (panneau électroluminescent + photorésistance) : c'est elle qui fait le son.
        · lumière L : attaque ~10 ms (~4 ms quand ça déborde fort : un cri, une consonne explosive est saisi sans que
          rien ne déborde, une montée douce garde ses 10 ms), chute rapide ~60 ms ;
        · mémoire M de la photorésistance : se charge lentement (~1.5 s) quand on compresse longtemps
          et se vide en ~3.5 s. La réduction réelle vaut max(L, 0.3 M) : relâchement en DEUX TEMPS,
          rapide sur les mots isolés, plus long sur une phrase compressée en continu ; après un long silence, la
          reprise est traitée comme le reste de la phrase.
    - Contre-réaction (le détecteur voit la sortie de la cellule), résolue sans délai.
    - DÉTECTEUR VOCAL : comme la compensation d'aigus du circuit de détection des vrais appareils, il est légèrement plus
      sensible au-dessus de 3 kHz (+1.5 dB : assez pour niveler la présence, trop peu pour qu'une consonne le déclenche),
      et ignore le grave sous 70 Hz (24 dB/oct). Il mêle la crête et le niveau moyen sur 12 ms : en COMPRESS, surtout le CORPS de la
      voix (85 % moyen, 15 % crête) : la voix est tenue dans un bloc régulier ; en LIMIT, à parts égales (les pointes
      sont tenues).
    - PEAK REDUCTION 0..100 : gain du circuit de détection (= seuil qui descend de 0 à -45 dB).
      GAIN : niveau de sortie. COMPRESS : ~3:1 genou très doux. LIMIT : ~10:1 genou plus serré.
    - LAMPES VIVANTES : l'harmonique 2 grandit avec le niveau (discrète sur une voix douce, chaleureuse quand elle pousse),
      le transformateur de sortie arrondit les crêtes.
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
        // chaîne latérale : passe-haut Butterworth 24 dB/oct à 70 Hz (un grondement ou un grave fort ne fait pas plonger la voix)
        scHp.setCoeffs (BiquadCoeffs::highpass (fs, 70.0, 0.5412));
        scHp2.setCoeffs (BiquadCoeffs::highpass (fs, 70.0, 1.3066));
        scHf.setCoeffs (BiquadCoeffs::highShelf (fs, std::min (3000.0, 0.4 * fs), 0.7, 1.5));
        lAtkSlow = coef (0.010);
        lAtkFast = coef (0.004);
        lRel = coef (0.060);
        mUp = coef (1.5);
        mDown = coef (3.5);
        lvlC = coef (0.050);
        msC = coef (0.012);
        reset();
    }

    void reset() noexcept
    {
        L = M = gr = lvl = ms = 0.0f;
        dc.reset(); scHp.reset(); scHp2.reset(); scHf.reset();
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
        const float peakShare = limit ? 0.5f : 0.15f;
        float peakGr = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            const float go = gainS.getNextValue();
            const float pr = prS.getNextValue();

            float pk = 0.0f;
            for (int c = 0; c < numCh; ++c) pk = std::max (pk, std::abs (scHf.process (scHp2.process (scHp.process (ch[c][i], c), c), c)));
            ms += msC * (pk * pk - ms);
            const float det = std::sqrt (peakShare * pk * pk + (1.0f - peakShare) * 2.0f * ms);
            // PEAK REDUCTION = gain du détecteur : à 0 rien ne compresse, à 100 le seuil est à -45 dBFS
            const float over = (det > 1.0e-6f ? gainToDb (det) : -120.0f) - (0.0f - 0.45f * pr);

            const float outEst = over - gr;
            const float kk = k * juce::jlimit (0.0f, 1.0f, (outEst + 0.5f * knee) / knee);

            // lumière : contre-réaction résolue (la cellule voit sa propre sortie) ; attaque plus vive si ça déborde fort
            float next;
            if (over <= 0.0f || pr < 0.01f)
                next = L * (1.0f - lRel);
            else
            {
                const float atk = lAtkSlow + (lAtkFast - lAtkSlow) * juce::jlimit (0.0f, 1.0f, (outEst - 4.0f) / 10.0f);
                auto solve = [&] (float c) { return (L * (1.0f - c) + c * kk * over) / (1.0f + c * kk); };
                next = solve (atk);
                if (next < L) next = solve (lRel);
            }
            L = juce::jlimit (0.0f, 40.0f, next);
            M += (L > M ? mUp : mDown) * (L - M);
            gr = std::max (L, 0.3f * M);
            peakGr = std::max (peakGr, gr);

            const float g = gr > 1.0e-6f ? dbToGain (-gr) : 1.0f;
            float mx = 0.0f;
            for (int c = 0; c < numCh; ++c) mx = std::max (mx, std::abs (ch[c][i] * g));
            lvl += lvlC * (mx - lvl);
            const float h2 = 0.015f + 0.05f * std::min (1.0f, lvl * 2.0f);   // lampes : plus elles sont poussées, plus elles chantent
            for (int c = 0; c < numCh; ++c)
            {
                float y = ch[c][i] * g;
                y += h2 * y * y;                          // harmonique 2 (DC retiré ensuite)
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
        lAtkSlow = o.lAtkSlow; lAtkFast = o.lAtkFast; lRel = o.lRel; mUp = o.mUp; mDown = o.mDown; lvlC = o.lvlC; msC = o.msC;
        L = o.L; M = o.M; gr = o.gr; lvl = o.lvl; ms = o.ms;
        gainS = o.gainS; prS = o.prS; dc = o.dc; scHp = o.scHp; scHp2 = o.scHp2; scHf = o.scHf;
    }

private:
    float coef (double seconds) const noexcept { return (float) (1.0 - std::exp (-1.0 / (seconds * fs))); }

    double fs = 44100.0;
    bool limit = false;
    float lAtkSlow = 0, lAtkFast = 0, lRel = 0, mUp = 0, mDown = 0, lvlC = 0, msC = 0;
    float L = 0, M = 0, gr = 0, lvl = 0, ms = 0;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> gainS { 1.0f };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> prS;
    Biquad dc, scHp, scHp2, scHf;
    std::atomic<float> grMeter { 0.0f };
};
} // namespace onirique
