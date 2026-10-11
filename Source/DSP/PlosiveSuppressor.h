#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include "Biquad.h"
#include "Common.h"

namespace onirique
{
/*  PLOSIVES : suppression intelligente des plosives (P, B, souffle dans la capsule). Aucune latence.

    Une plosive n'est pas "beaucoup de grave" : c'est une BOUFFÉE soudaine sous ~150 Hz qui dépasse
    nettement le grave habituel de la voix ET qui domine le médium (une voyelle grave ou un mot chaud
    ont du médium en même temps, une plosive presque pas). Détection par trois enveloppes liées en stéréo :
      - grave rapide (attaque 0.5 ms, relâchement 30 ms) et grave lent (référence ~0.6 s) : la bouffée ;
      - médium 300 Hz - 3 kHz rapide : la voix. Score = "bouffée" x "domination du grave".
    Action : un shelf grave DYNAMIQUE (180 Hz, minimum de phase) dont le gain suit la réduction. À 0 dB le
    filtre est exactement l'identité : hors plosive le signal sort strictement intact. Pendant la plosive
    tout ce qui est sous ~180 Hz descend d'un bloc, sans annulation de phase ni artefact.
    Réduction jusqu'à 18 dB x INTENSITÉ, attaque 0.3 ms, relâchement 60 ms : le "pop" disparaît, le corps
    de la voix revient tout de suite derrière.
*/
class PlosiveSuppressor
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        lpA.setCoeffs (BiquadCoeffs::lowpass (fs, 150.0, 0.7071));
        lpB.setCoeffs (BiquadCoeffs::lowpass (fs, 150.0, 0.7071));
        bp.setCoeffs (BiquadCoeffs::bandpass (fs, 950.0, 0.35));
        lfAtk = coef (0.5);  lfRel = coef (30.0);
        slowC = coef (600.0);
        midAtk = coef (1.0); midRel = coef (30.0);
        gAtk = coef (0.3);   gRel = coef (60.0);
        amtS.reset (fs, 0.05);
        reset();
    }

    void reset() noexcept
    {
        lpA.reset(); lpB.reset(); bp.reset(); shelf.reset();
        lastRed = 0.0f; countdown = 0;
        shelf.setCoeffs (BiquadCoeffs::lowShelf (fs, 180.0, 0.7, 0.0));
        lfF = lfS = midF = 0.0f;
        red = 0.0f;
        meter.store (0.0f, std::memory_order_relaxed);
    }

    void setAmount (float amount01, bool snap) noexcept
    {
        amount01 = juce::jlimit (0.0f, 1.0f, amount01);
        if (snap) amtS.setCurrentAndTargetValue (amount01); else amtS.setTargetValue (amount01);
    }

    void process (float* const* ch, int numCh, int n) noexcept
    {
        float peakRed = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float amt = amtS.getNextValue();
            float low[2] {}, lp = 0.0f, mp = 0.0f;
            for (int c = 0; c < numCh; ++c)
            {
                const float x = ch[c][i];
                low[c] = lpB.process (lpA.process (x, c), c);
                lp = std::max (lp, std::abs (low[c]));
                mp = std::max (mp, std::abs (bp.process (x, c)));
            }

            lfF += (lp > lfF ? lfAtk : lfRel) * (lp - lfF);
            lfS += slowC * (lfF - lfS);
            midF += (mp > midF ? midAtk : midRel) * (mp - midF);

            float target = 0.0f;
            if (amt > 0.001f && lfF > 0.003f)   // ~ -50 dBFS : en dessous, rien à faire
            {
                const float burstDb = gainToDb ((lfF + 1.0e-6f) / (lfS * 1.4f + 1.0e-5f));   // bouffée au-dessus du grave habituel
                const float domDb   = gainToDb ((lfF + 1.0e-6f) / (midF + 1.0e-5f));          // le grave écrase le médium
                const float s1 = juce::jlimit (0.0f, 1.0f, (burstDb - 2.0f) / 8.0f);
                const float s2 = juce::jlimit (0.0f, 1.0f, (domDb - 6.0f) / 10.0f);   // une voyelle grave reste sous +6 dB
                target = std::min (18.0f, (2.0f + 1.2f * std::max (0.0f, burstDb))) * s1 * s2 * amt;
            }
            red += (target > red ? gAtk : gRel) * (target - red);
            if (red < 1.0e-3f) red = 0.0f;
            peakRed = std::max (peakRed, red);

            // shelf dynamique : coefficients recalculés dès que la réduction bouge (tous les 4 échantillons)
            if (--countdown <= 0)
            {
                countdown = 4;
                if (std::abs (red - lastRed) > 0.05f || (red == 0.0f && lastRed != 0.0f))
                {
                    shelf.setCoeffs (BiquadCoeffs::lowShelf (fs, 180.0, 0.7, -red));
                    lastRed = red < 0.01f ? 0.0f : red;
                }
            }
            for (int c = 0; c < numCh; ++c) ch[c][i] = shelf.process (ch[c][i], c);   // à 0 dB : identité exacte
        }
        meter.store (peakRed, std::memory_order_relaxed);
    }

    float getReductionDb() const noexcept { return meter.load (std::memory_order_relaxed); }

private:
    float coef (double ms) const noexcept { return (float) (1.0 - std::exp (-1.0 / (ms * 0.001 * fs))); }

    double fs = 44100.0;
    Biquad lpA, lpB, bp, shelf;
    int countdown = 0;
    float lastRed = 0.0f;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> amtS;
    float lfAtk = 0, lfRel = 0, slowC = 0, midAtk = 0, midRel = 0, gAtk = 0, gRel = 0;
    float lfF = 0, lfS = 0, midF = 0, red = 0;
    std::atomic<float> meter { 0.0f };
};
} // namespace onirique
