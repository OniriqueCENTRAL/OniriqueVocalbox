#pragma once

#include "SatStage.h"

namespace onirique
{
/*  TAPE : magnétophone à bande, modèle original simplifié, suréchantillonné x8.

    Chaîne : pré-emphase d'enregistrement -> saturation de bande (x8) -> dé-emphase de
    lecture exactement inverse -> compression de bande -> couleur (head bump, perte d'aigu) -> DC.

    - Emphase forte (+6 dB / -6 dB @ 3.2 kHz) : comme sur une vraie bande, les aigus atteignent
      la saturation en premier et se tassent. Sibilances et attaques s'arrondissent : c'est la
      "douceur" de la bande. La réponse linéaire reste plate (les deux shelves s'annulent).
    - Saturation : tanh quasi symétrique (biais minime) -> surtout harmonique 3, coude doux.
    - Aucune préservation des transitoires : la bande arrondit volontairement les crêtes.
    - Compression de bande : 1:1 -> ~1.5:1 au-dessus de -22 dBFS, genou 6 dB, attaque 10 ms,
      relâchement 200 ms : plus lente et plus marquée que le sag du TUBE, effet "glue".
    - Couleur (dosée par DRIVE, nulle à 0) : head bump +1.8 dB vers 65 Hz, -2.5 dB au-dessus
      de 12 kHz.
    - Niveau compensé par SatStage (compression de bande comprise) : seul le grain change.
*/
class SatTape final : public SatStage
{
public:
    void processStage (float* const* ch, int numCh, int n) noexcept override
    {
        // ---- passe 1 (cadence normale) : pré-emphase + paramètres par échantillon
        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < numCh; ++c)
                ch[c][i] = pre.process (ch[c][i], c);

            const float d = driveS.getNextValue();
            const float k = driveGain (d);
            const float b = 0.012f * d;

            const size_t s = (size_t) i;
            kBuf[s] = k;
            bBuf[s] = b;
            tbBuf[s] = std::tanh (b);
            cBuf[s] = refLevel / (std::tanh (refLevel * k + b) - tbBuf[s]);   // le niveau courant ressort au même niveau
            wBuf[s] = std::min (1.0f, d * 5.0f);
            dBuf[s] = d;
        }

        // ---- passe 2 : saturation de bande à 8x
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

        // ---- passe 3 : dé-emphase, compression de bande, couleur, DC
        for (int i = 0; i < n; ++i)
        {
            float mx = 0.0f;
            for (int c = 0; c < numCh; ++c)
            {
                const float v = post.process (ch[c][i], c);
                ch[c][i] = v;
                mx = std::max (mx, std::abs (v));
            }
            env += (mx > env ? cmpAtk : cmpRel) * (mx - env);

            const float d = dBuf[(size_t) i];
            float g = 1.0f;
            const float slope = 0.35f * d;
            if (slope > 1.0e-4f && env > 1.0e-3f)
            {
                const float over = gainToDb (env) + 22.0f;   // seuil -22 dBFS
                float red = 0.0f;
                if (over > 3.0f)        red = slope * over;
                else if (over > -3.0f)  red = slope * (over + 3.0f) * (over + 3.0f) / 12.0f;
                g = dbToGain (-red);
            }

            const float dcMix = std::min (1.0f, d * 10.0f);
            for (int c = 0; c < numCh; ++c)
            {
                float v = ch[c][i] * g;
                v += d * (bump.process (v, c) - v);
                v += d * (hfLoss.process (v, c) - v);
                ch[c][i] = v + dcMix * (dc.process (v, c) - v);
            }
        }
    }

protected:
    void prepareStage (int maxBlock) override
    {
        for (auto* v : { &kBuf, &bBuf, &tbBuf, &cBuf, &wBuf, &dBuf })
            v->assign ((size_t) maxBlock, 0.0f);

        pre.setCoeffs    (BiquadCoeffs::highShelf (fs, 3200.0, 0.6, 6.0));
        post.setCoeffs   (BiquadCoeffs::highShelf (fs, 3200.0, 0.6, -6.0));
        bump.setCoeffs   (BiquadCoeffs::peaking   (fs, 65.0, 1.2, 1.8));
        hfLoss.setCoeffs (BiquadCoeffs::highShelf (fs, 12000.0, 0.6, -2.5));
        dc.setCoeffs     (BiquadCoeffs::highpass  (fs, 8.0, 0.707));

        cmpAtk = coef (10.0); cmpRel = coef (200.0);
    }

    void resetStage() noexcept override
    {
        pre.reset(); post.reset(); bump.reset(); hfLoss.reset(); dc.reset();
        env = 0.0f;
    }

private:
    std::vector<float> kBuf, bBuf, tbBuf, cBuf, wBuf, dBuf;
    Biquad pre, post, bump, hfLoss, dc;
    float cmpAtk = 0, cmpRel = 0, env = 0;
};
} // namespace onirique
