#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include "Biquad.h"
#include "Common.h"

namespace onirique
{
/*  MULTIBAND : compresseur multibande optique pensé pour le mixage des voix (inspiré du rendu d'un
    Waves C6 ; modèle original). Aucune latence.

    Découpage : 4 bandes (LOW / LO-MID / HI-MID / HIGH) par filtres Linkwitz-Riley 24 dB/oct
    (2 Butterworth en cascade). Les bandes basses passent par des passe-tout aux fréquences de
    coupure supérieures : toutes les bandes ont la même phase, leur somme est plate (sans compression,
    le module ne change que la phase, pas le timbre). Les fréquences de coupure glissent sans clic.

    Compression par bande, texture "crémeuse" :
      - détection RMS (fenêtre ~5 ms) : réagit au corps du son, pas aux crêtes isolées ;
      - genou très large (12 dB) et ratio doux 3:1, plafond 18 dB ;
      - cellule optique : la réduction suit ATTACK / RELEASE avec une mémoire lente (une phrase
        compressée longtemps relâche plus lentement), puis un lissage final de 2 ms : aucun angle,
        aucun pompage sec ;
      - temps adaptés à chaque bande : les graves 1.6x plus lents (pas de distorsion de modulation),
        les aigus 0.7x (sibilances tenues) ;
      - GAIN par bande après compression (makeup / égalisation dynamique).
    Liaison stéréo par bande. Mesures (niveau et réduction par bande) publiées pour l'interface.
*/
class MultibandOpto
{
public:
    static constexpr int kBands = 4;

    struct Meter
    {
        std::array<std::atomic<float>, kBands> grDb {}, levelDb {};
    };

    struct Params
    {
        float x1 = 200.0f, x2 = 1500.0f, x3 = 6000.0f;
        std::array<float, kBands> thr { -20.0f, -20.0f, -20.0f, -20.0f }, gain {};
        float attackMs = 15.0f, releaseMs = 200.0f;
    };

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        for (auto* s : { &x1S, &x2S, &x3S }) s->reset (fs, 0.08);
        for (auto& g : gainS) { g.reset (fs, 0.05); g.setCurrentAndTargetValue (1.0f); }
        for (auto& s : soloS) { s.reset (fs, 0.015); s.setCurrentAndTargetValue (1.0f); }
        rmsC = coef (0.005);
        smoothC = coef (0.002);
        memUp = coef (2.0);
        memDown = coef (4.0);
        reset();
        updateTimes();
        updateFilters (x1S.getTargetValue(), x2S.getTargetValue(), x3S.getTargetValue());
    }

    void reset() noexcept
    {
        for (auto* f : allFilters()) f->reset();
        for (int b = 0; b < kBands; ++b)
        {
            ms[(size_t) b] = L[(size_t) b] = M[(size_t) b] = gr[(size_t) b] = 0.0f;
            meter.grDb[(size_t) b].store (0.0f, std::memory_order_relaxed);
            meter.levelDb[(size_t) b].store (-100.0f, std::memory_order_relaxed);
        }
        countdown = 0;
    }

    void setParams (const Params& p, bool snap) noexcept
    {
        // fréquences de coupure toujours ordonnées, au moins 1/3 d'octave d'écart
        const float a = juce::jlimit (40.0f, 1000.0f, p.x1);
        const float b = juce::jlimit (a * 1.26f, 8000.0f, p.x2);
        const float c = juce::jlimit (b * 1.26f, 16000.0f, p.x3);
        if (snap) { x1S.setCurrentAndTargetValue (a); x2S.setCurrentAndTargetValue (b); x3S.setCurrentAndTargetValue (c); countdown = 0; }
        else      { x1S.setTargetValue (a); x2S.setTargetValue (b); x3S.setTargetValue (c); }

        for (int i = 0; i < kBands; ++i)
        {
            thr[(size_t) i] = juce::jlimit (-60.0f, 0.0f, p.thr[(size_t) i]);
            const float g = dbToGain (juce::jlimit (-12.0f, 12.0f, p.gain[(size_t) i]));
            if (snap) gainS[(size_t) i].setCurrentAndTargetValue (g); else gainS[(size_t) i].setTargetValue (g);
        }

        if (p.attackMs != attackMs || p.releaseMs != releaseMs)
        {
            attackMs = juce::jlimit (1.0f, 200.0f, p.attackMs);
            releaseMs = juce::jlimit (20.0f, 2000.0f, p.releaseMs);
            updateTimes();
        }
    }

    void process (float* const* ch, int numCh, int n) noexcept
    {
        std::array<float, kBands> peakGr {}, peakLvl;
        peakLvl.fill (-100.0f);

        for (int i = 0; i < n; ++i)
        {
            const float f1 = x1S.getNextValue(), f2 = x2S.getNextValue(), f3 = x3S.getNextValue();
            if (--countdown <= 0)
            {
                if (f1 != lastX[0] || f2 != lastX[1] || f3 != lastX[2]) updateFilters (f1, f2, f3);
                countdown = 16;
            }

            // ---- découpage en 4 bandes, phases alignées
            float band[kBands][2] {};
            for (int c = 0; c < numCh; ++c)
            {
                const float x = ch[c][i];
                const float lp1 = lp1b.process (lp1a.process (x, c), c);
                const float hp1 = hp1b.process (hp1a.process (x, c), c);
                const float lp2 = lp2b.process (lp2a.process (hp1, c), c);
                const float hp2 = hp2b.process (hp2a.process (hp1, c), c);
                band[0][c] = ap3a.process (ap2a.process (lp1, c), c);
                band[1][c] = ap3b.process (lp2, c);
                band[2][c] = lp3b.process (lp3a.process (hp2, c), c);
                band[3][c] = hp3b.process (hp3a.process (hp2, c), c);
            }

            // ---- compression optique par bande
            float g[kBands];
            for (int b = 0; b < kBands; ++b)
            {
                const auto s = (size_t) b;
                float p = 0.0f;
                for (int c = 0; c < numCh; ++c) p = std::max (p, band[b][c] * band[b][c]);
                ms[s] += rmsC * (p - ms[s]);
                const float lvl = 10.0f * std::log10 (ms[s] * 2.0f + 1.0e-12f);   // RMS ramené à la crête d'un sinus
                peakLvl[s] = std::max (peakLvl[s], lvl);

                constexpr float knee = 12.0f, slope = 1.0f - 1.0f / 3.0f;
                const float over = lvl - thr[s];
                float target = 0.0f;
                if (over >= 0.5f * knee)      target = slope * over;
                else if (over > -0.5f * knee) target = slope * (over + 0.5f * knee) * (over + 0.5f * knee) / (2.0f * knee);
                target = std::min (target, 18.0f);

                L[s] += (target > L[s] ? atkC[s] : relC[s]) * (target - L[s]);
                M[s] += (L[s] > M[s] ? memUp : memDown) * (L[s] - M[s]);
                const float want = std::max (L[s], 0.35f * M[s]);
                gr[s] += smoothC * (want - gr[s]);
                peakGr[s] = std::max (peakGr[s], gr[s]);

                g[b] = dbToGain (-gr[s]) * gainS[s].getNextValue() * soloS[s].getNextValue();
            }

            for (int c = 0; c < numCh; ++c)
                ch[c][i] = band[0][c] * g[0] + band[1][c] * g[1] + band[2][c] * g[2] + band[3][c] * g[3];
        }

        for (int b = 0; b < kBands; ++b)
        {
            meter.grDb[(size_t) b].store (peakGr[(size_t) b], std::memory_order_relaxed);
            meter.levelDb[(size_t) b].store (peakLvl[(size_t) b], std::memory_order_relaxed);
        }
    }

    const Meter& getMeter() const noexcept { return meter; }

    // LISTEN par bande : si au moins une bande est en écoute, seules les bandes en écoute sortent (fondu 15 ms)
    void setSolo (const std::array<bool, kBands>& solo, bool snap) noexcept
    {
        const bool any = solo[0] || solo[1] || solo[2] || solo[3];
        for (int b = 0; b < kBands; ++b)
        {
            const float t = (! any || solo[(size_t) b]) ? 1.0f : 0.0f;
            if (snap) soloS[(size_t) b].setCurrentAndTargetValue (t); else soloS[(size_t) b].setTargetValue (t);
        }
    }

private:
    float coef (double seconds) const noexcept { return (float) (1.0 - std::exp (-1.0 / (seconds * fs))); }

    void updateTimes() noexcept
    {
        static constexpr float scale[kBands] { 1.6f, 1.0f, 0.85f, 0.7f };   // graves plus lents, aigus plus vifs
        for (int b = 0; b < kBands; ++b)
        {
            atkC[(size_t) b] = coef (attackMs * 0.001 * scale[b]);
            relC[(size_t) b] = coef (releaseMs * 0.001 * scale[b]);
        }
    }

    void updateFilters (float f1, float f2, float f3) noexcept
    {
        constexpr double q = 0.7071067811865476;   // Butterworth : 2 en cascade = Linkwitz-Riley 24 dB/oct
        const auto l1 = BiquadCoeffs::lowpass (fs, f1, q), h1 = BiquadCoeffs::highpass (fs, f1, q);
        const auto l2 = BiquadCoeffs::lowpass (fs, f2, q), h2 = BiquadCoeffs::highpass (fs, f2, q);
        const auto l3 = BiquadCoeffs::lowpass (fs, f3, q), h3 = BiquadCoeffs::highpass (fs, f3, q);
        lp1a.setCoeffs (l1); lp1b.setCoeffs (l1); hp1a.setCoeffs (h1); hp1b.setCoeffs (h1);
        lp2a.setCoeffs (l2); lp2b.setCoeffs (l2); hp2a.setCoeffs (h2); hp2b.setCoeffs (h2);
        lp3a.setCoeffs (l3); lp3b.setCoeffs (l3); hp3a.setCoeffs (h3); hp3b.setCoeffs (h3);
        // somme LP + HP d'un Linkwitz-Riley 24 dB = passe-tout du 2e ordre, Q Butterworth
        ap2a.setCoeffs (BiquadCoeffs::allpass (fs, f2, q));
        ap3a.setCoeffs (BiquadCoeffs::allpass (fs, f3, q));
        ap3b.setCoeffs (BiquadCoeffs::allpass (fs, f3, q));
        lastX[0] = f1; lastX[1] = f2; lastX[2] = f3;
    }

    std::array<Biquad*, 15> allFilters() noexcept
    {
        return { &lp1a, &lp1b, &hp1a, &hp1b, &lp2a, &lp2b, &hp2a, &hp2b, &lp3a, &lp3b, &hp3a, &hp3b, &ap2a, &ap3a, &ap3b };
    }

    double fs = 44100.0;
    Biquad lp1a, lp1b, hp1a, hp1b, lp2a, lp2b, hp2a, hp2b, lp3a, lp3b, hp3a, hp3b, ap2a, ap3a, ap3b;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> x1S { 200.0f }, x2S { 1500.0f }, x3S { 6000.0f };
    std::array<juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>, kBands> gainS;
    std::array<juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>, kBands> soloS;
    float lastX[3] { -1.0f, -1.0f, -1.0f };
    int countdown = 0;

    std::array<float, kBands> thr { -20.0f, -20.0f, -20.0f, -20.0f }, ms {}, L {}, M {}, gr {}, atkC {}, relC {};
    float attackMs = 15.0f, releaseMs = 200.0f;
    float rmsC = 0.0f, smoothC = 0.0f, memUp = 0.0f, memDown = 0.0f;
    Meter meter;
};
} // namespace onirique
