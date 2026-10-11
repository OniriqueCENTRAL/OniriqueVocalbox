#pragma once

#include "SatStage.h"

namespace onirique
{
/*  ANALOG : préampli classe A à transformateurs, inspiré du caractère d'un Neve 1073
    (modèle original, aucune donnée constructeur). Suréchantillonné x8.

    Chaîne : étage classe A (x8) -> transformateur de sortie (x8) -> couleur EQ -> blocage DC.

    - Classe A : sigmoïde algébrique z / sqrt(1 + z^2) avec un biais faible. Plus "granuleuse"
      que la tanh du TUBE (le coude est plus progressif, les harmoniques montent plus haut) et
      moins asymétrique : harmonique 2 présente mais l'harmonique 3 domine en poussant.
    - Transformateur : seule la partie grave (< ~180 Hz) sature, symétriquement (harmonique 3),
      comme un noyau magnétique dont le flux croît quand la fréquence baisse. Les graves
      s'épaississent et se tassent, le médium/aigu reste ouvert. Pente unitaire au repos :
      aucun effet à faible niveau.
    - Couleur (dosée par DRIVE, nulle à 0) : +1.5 dB de rondeur sous 110 Hz, +0.8 dB de
      présence vers 3 kHz (voix "devant"), -1.2 dB au-dessus de 15 kHz (haut arrondi).
    - Transitoires : préservés plus légèrement que le TUBE (le "punch" passe par l'étage).
    - Pas de sag : le classe A réagit vite, sans pompage.
    - Niveau compensé par SatStage : ni DRIVE ni le changement de type ne font sauter le volume.
*/
class SatAnalog final : public SatStage
{
public:
    void processStage (float* const* ch, int numCh, int n) noexcept override
    {
        // ---- passe 1 (cadence normale) : paramètres par échantillon
        for (int i = 0; i < n; ++i)
        {
            float mx = 0.0f;
            for (int c = 0; c < numCh; ++c) mx = std::max (mx, std::abs (ch[c][i]));
            const float tf = trans.process (mx);

            const float d = driveS.getNextValue();
            const float k = driveGain (d);
            const float b = (0.03f + 0.09f * d) * std::min (1.0f, d * 8.0f);

            const size_t s = (size_t) i;
            kBuf[s] = k;
            bBuf[s] = b;
            abBuf[s] = asig (b);
            gBuf[s] = 0.8f + 3.2f * d;                                   // charge du transformateur
            cBuf[s] = refLevel / (asig (refLevel * k + b) - abBuf[s]);
            wBuf[s] = std::min (1.0f, d * 5.0f) * (1.0f - 0.35f * tf);
            dBuf[s] = d;
        }

        // ---- passe 2 : classe A + transformateur à 8x
        juce::dsp::AudioBlock<float> block (ch, (size_t) numCh, (size_t) n);
        auto up = os->processSamplesUp (block);
        const size_t upN = up.getNumSamples();

        for (int c = 0; c < numCh; ++c)
        {
            float* d = up.getChannelPointer ((size_t) c);
            float lp = xfLp[c];
            for (size_t j = 0; j < upN; ++j)
            {
                const size_t s = j >> osOrder;
                const float x = d[j];
                const float a = asig (kBuf[s] * x + bBuf[s]) - abBuf[s];

                lp += xfCoef * (a - lp);
                const float g = gBuf[s];
                const float y = (a - lp + std::tanh (g * lp) / g) * cBuf[s];

                d[j] = x + wBuf[s] * (y - x);
            }
            xfLp[c] = lp;
        }

        os->processSamplesDown (block);

        // ---- passe 3 : couleur dosée par DRIVE, DC
        for (int i = 0; i < n; ++i)
        {
            const float d = dBuf[(size_t) i];
            const float dcMix = std::min (1.0f, d * 10.0f);
            for (int c = 0; c < numCh; ++c)
            {
                float v = ch[c][i];
                v += d * (lowCol.process (v, c) - v);
                v += d * (presCol.process (v, c) - v);
                v += d * (topCol.process (v, c) - v);
                ch[c][i] = v + dcMix * (dc.process (v, c) - v);
            }
        }
    }

protected:
    void prepareStage (int maxBlock) override
    {
        for (auto* v : { &kBuf, &bBuf, &abBuf, &gBuf, &cBuf, &wBuf, &dBuf })
            v->assign ((size_t) maxBlock, 0.0f);

        xfCoef = (float) (1.0 - std::exp (-6.283185307179586 * 180.0 / (osFactor * fs)));

        lowCol.setCoeffs  (BiquadCoeffs::lowShelf  (fs, 110.0, 0.7, 1.5));
        presCol.setCoeffs (BiquadCoeffs::peaking   (fs, 3000.0, 0.9, 0.8));
        topCol.setCoeffs  (BiquadCoeffs::highShelf (fs, 15000.0, 0.7, -1.2));
        dc.setCoeffs      (BiquadCoeffs::highpass  (fs, 8.0, 0.707));

        trans.prepare (fs);
    }

    void resetStage() noexcept override
    {
        lowCol.reset(); presCol.reset(); topCol.reset(); dc.reset();
        xfLp[0] = xfLp[1] = 0.0f;
        trans.reset();
    }

private:
    static float asig (float z) noexcept { return z / std::sqrt (1.0f + z * z); }

    std::vector<float> kBuf, bBuf, abBuf, gBuf, cBuf, wBuf, dBuf;
    Biquad lowCol, presCol, topCol, dc;
    TransientFollower trans;
    float xfCoef = 0.0f, xfLp[2] {};
};
} // namespace onirique
