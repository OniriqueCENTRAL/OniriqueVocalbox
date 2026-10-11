#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "Biquad.h"
#include "Common.h"

namespace onirique
{
/*  Auto Gain : compare la loudness de référence (après INPUT, avant EQ) à celle du signal
    traité (après saturation, avant OUTPUT) et compense l'écart.

    - Mesure K-weighted simplifiée (passe-haut 38 Hz + shelf +4 dB @ 1.7 kHz), énergie
      moyenne intégrée sur ~0.8 s : les deux mesures voient le même programme, donc le
      RAPPORT est stable même quand plusieurs boutons bougent.
    - Correction limitée à +/-12 dB, lissée en dB (constante ~0.6 s) et plafonnée à 4 dB/s :
      aucun pompage audible, même sur des transitoires ou des réglages automatisés.
    - La correction est appliquée en rampe exponentielle continue à l'intérieur de chaque
      bloc (pas d'escalier).
    - Silence : sous le seuil (~ -70 dB), la dernière correction est conservée.
    - Désactivé : retour progressif à 0 dB avec la même dynamique.
*/
class AutoGain
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        const auto hp = BiquadCoeffs::highpass (fs, 38.0, 0.5);
        const auto sh = BiquadCoeffs::highShelf (fs, 1700.0, 0.7, 4.0);
        for (auto* m : { &ref, &proc })
        {
            m->hp.setCoeffs (hp);
            m->sh.setCoeffs (sh);
            m->hp.reset();
            m->sh.reset();
            m->ms = 0.0;
        }
        meterCoef = 1.0 - std::exp (-1.0 / (0.8 * fs));
        gainDb = 0.0; targetDb = 0.0; gLin = 1.0f; mult = 1.0f;
    }

    void analyseReference (const float* const* ch, int numCh, int n) noexcept { ref.run (ch, numCh, n, meterCoef); }
    void analyseProcessed (const float* const* ch, int numCh, int n) noexcept { proc.run (ch, numCh, n, meterCoef); }

    // À appeler une fois par bloc (n échantillons) avant d'utiliser nextGain() n fois.
    void update (bool enabled, int n) noexcept
    {
        if (enabled && ref.ms > 1.0e-7 && proc.ms > 1.0e-8)
            targetDb = std::clamp (10.0 * std::log10 (ref.ms / proc.ms), -12.0, 12.0);

        const double tgt = enabled ? targetDb : 0.0;
        const double a = 1.0 - std::exp (-(double) n / (0.6 * fs));
        const double maxStep = 4.0 * (double) n / fs;
        gainDb += std::clamp ((tgt - gainDb) * a, -maxStep, maxStep);

        const double gEnd = std::pow (10.0, gainDb / 20.0);
        mult = (float) std::pow (gEnd / (double) gLin, 1.0 / (double) std::max (1, n));
    }

    inline float nextGain() noexcept { gLin *= mult; return gLin; }

    float getGainDb() const noexcept { return (float) gainDb; }

private:
    struct Meter
    {
        Biquad hp, sh;
        double ms = 0.0;

        void run (const float* const* ch, int numCh, int n, double c) noexcept
        {
            for (int i = 0; i < n; ++i)
            {
                double s = 0.0;
                for (int k = 0; k < numCh; ++k)
                {
                    const float x = sh.process (hp.process (ch[k][i], k), k);
                    s += (double) x * (double) x;
                }
                ms += c * (s - ms);
            }
        }
    };

    Meter ref, proc;
    double fs = 44100.0, meterCoef = 0.0, gainDb = 0.0, targetDb = 0.0;
    float gLin = 1.0f, mult = 1.0f;
};
} // namespace onirique
