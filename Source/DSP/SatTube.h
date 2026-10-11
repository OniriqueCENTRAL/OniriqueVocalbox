#pragma once

#include "SatStage.h"

namespace onirique
{
/*  TUBE : étage à lampe/préampli, original, suréchantillonné x8 (algorithme d'origine).

    Chaîne : pré-emphase -> waveshaper tanh asymétrique (x8) -> compression "sag" très
    douce -> dé-emphase exactement inverse -> blocage DC.

    - Pré/dé-emphase (+4 dB / -4 dB @ 2.5 kHz) : les aigus saturent un peu plus (brillance)
      sans modifier la réponse linéaire (les deux shelves s'annulent exactement).
    - Waveshaper : y = tanh(k x + b) - tanh(b). Le biais b crée l'harmonique 2 (paire) comme
      un triode ; k croît avec DRIVE (0 -> +30 dB), toujours sans clip dur.
    - Transitoires : un détecteur rapide/lent (attaque 0.04 ms) fait glisser le mélange vers le
      signal propre pendant les attaques : consonnes et claquements restent nets, le corps de
      la voix se densifie.
    - Niveau compensé par SatStage : DRIVE ajoute des harmoniques, jamais de volume.
    - Sag : compresseur feed-forward 1:1 -> 1.33:1, genou 6 dB, attaque 5 ms, relâchement
      120 ms (au plus ~3 dB sur de fortes crêtes) : "colle" la voix très subtilement.
    - Drive = 0 : mélange w = 0, le signal traverse quand même le suréchantillonneur
      pour garder une latence constante (pas de saut de timing).
*/
class SatTube final : public SatStage
{
public:
    void processStage (float* const* ch, int numCh, int n) noexcept override
    {
        // ---- passe 1 (cadence normale) : pré-emphase + paramètres par échantillon
        for (int i = 0; i < n; ++i)
        {
            float mx = 0.0f;
            for (int c = 0; c < numCh; ++c)
            {
                const float v = pre.process (ch[c][i], c);
                ch[c][i] = v;
                mx = std::max (mx, std::abs (v));
            }

            const float tf = trans.process (mx);

            const float d = driveS.getNextValue();
            const float k = driveGain (d);
            const float b = (0.04f + 0.20f * d) * std::min (1.0f, d * 8.0f);

            const size_t s = (size_t) i;
            kBuf[s] = k;
            bBuf[s] = b;
            tbBuf[s] = std::tanh (b);
            cBuf[s] = refLevel / (std::tanh (refLevel * k + b) - tbBuf[s]);   // le niveau courant ressort au même niveau
            wBuf[s] = std::min (1.0f, d * 5.0f) * (1.0f - 0.6f * tf);   // transitoires : on mélange vers le signal propre
            dBuf[s] = d;
        }

        // ---- passe 2 : waveshaper à 8x
        juce::dsp::AudioBlock<float> block (ch, (size_t) numCh, (size_t) n);
        auto up = os->processSamplesUp (block);
        const size_t upN = up.getNumSamples();

        for (int c = 0; c < numCh; ++c)
        {
            float* d = up.getChannelPointer ((size_t) c);
            for (size_t j = 0; j < upN; ++j)
            {
                const size_t s = j >> osOrder;
                const float x = d[j];
                const float y = (std::tanh (kBuf[s] * x + bBuf[s]) - tbBuf[s]) * cBuf[s];
                d[j] = x + wBuf[s] * (y - x);
            }
        }

        os->processSamplesDown (block);

        // ---- passe 3 : sag, dé-emphase, DC
        for (int i = 0; i < n; ++i)
        {
            float mx = 0.0f;
            for (int c = 0; c < numCh; ++c) mx = std::max (mx, std::abs (ch[c][i]));
            sagEnv += (mx > sagEnv ? sagAtk : sagRel) * (mx - sagEnv);

            float g = 1.0f;
            const float slope = 0.25f * dBuf[(size_t) i];
            if (slope > 1.0e-4f && sagEnv > 1.0e-3f)
            {
                const float over = gainToDb (sagEnv) + 20.0f;   // seuil -20 dBFS
                float red = 0.0f;
                if (over > 3.0f)        red = slope * over;
                else if (over > -3.0f)  red = slope * (over + 3.0f) * (over + 3.0f) / 12.0f;
                g = dbToGain (-red);
            }

            // le blocage DC (utile seulement à cause du biais) n'entre qu'avec DRIVE
            const float dcMix = std::min (1.0f, dBuf[(size_t) i] * 10.0f);
            for (int c = 0; c < numCh; ++c)
            {
                const float v = post.process (ch[c][i] * g, c);
                ch[c][i] = v + dcMix * (dc.process (v, c) - v);
            }
        }
    }

protected:
    void prepareStage (int maxBlock) override
    {
        for (auto* v : { &kBuf, &bBuf, &tbBuf, &cBuf, &wBuf, &dBuf })
            v->assign ((size_t) maxBlock, 0.0f);

        pre.setCoeffs  (BiquadCoeffs::highShelf (fs, 2500.0, 0.6, 4.0));
        post.setCoeffs (BiquadCoeffs::highShelf (fs, 2500.0, 0.6, -4.0));
        dc.setCoeffs   (BiquadCoeffs::highpass  (fs, 8.0, 0.707));

        trans.prepare (fs);
        sagAtk = coef (5.0); sagRel = coef (120.0);
    }

    void resetStage() noexcept override
    {
        pre.reset(); post.reset(); dc.reset();
        trans.reset();
        sagEnv = 0.0f;
    }

private:
    std::vector<float> kBuf, bBuf, tbBuf, cBuf, wBuf, dBuf;
    Biquad pre, post, dc;
    TransientFollower trans;

    float sagAtk = 0, sagRel = 0, sagEnv = 0;
};
} // namespace onirique
