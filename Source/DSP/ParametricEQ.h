#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <complex>
#include "Biquad.h"
#include "Common.h"

namespace onirique
{
/*  PARAMETRIC EQ : 6 bandes "ultra clean", complément des 3 boutons de l'EQ vocal (placé juste après).

    Filtres "appariés à l'analogique" (méthode de Vicanek, 2016) : les pôles sont placés comme ceux du
    filtre analogique idéal, puis le numérateur est calculé pour que la courbe numérique coïncide avec la
    courbe analogique en trois points (0 Hz, fréquence de la bande, Nyquist). Contrairement aux biquads
    classiques (transformée bilinéaire), une cloche ou un shelf à 10-20 kHz garde exactement sa forme :
    pas de "resserrement" près de Nyquist. Calcul en double précision, aucune latence, aucune coloration.

    Types : BELL, LOW SHELF, HIGH SHELF (gain +/-18 dB, Q 0.1..10), LOW CUT, HIGH CUT (12 dB/oct, Q de
    résonance). Réglages lissés (pas d'escalier en tournant un bouton), activation en fondu de 30 ms.
*/
class ParametricEQ
{
public:
    static constexpr int kBands = 6;
    enum Type { bell = 0, lowShelf, highShelf, lowCut, highCut, numTypes };

    struct Band
    {
        bool on = false;
        int type = bell;
        float freq = 1000.0f, gainDb = 0.0f, q = 1.0f;
    };

    // ---- conception (partagée avec l'interface : la courbe affichée est celle qui tourne)
    static BiquadCoeffs design (double fs, int type, double f0, double gainDb, double Q) noexcept
    {
        using C = std::complex<double>;
        f0 = juce::jlimit (10.0, 0.49 * fs, f0);
        Q = juce::jlimit (0.05, 20.0, Q);
        const double A = std::pow (10.0, gainDb / 40.0), sA = std::sqrt (A);

        // prototype analogique normalisé (s en unités de 2 pi f0) : N(s) / D(s), coefficients [s^0, s^1, s^2]
        double n[3] { 1, 0, 0 }, d[3] { 1, 1.0 / Q, 1 };
        switch (type)
        {
            case bell:      n[0] = 1; n[1] = A / Q; n[2] = 1;  d[0] = 1; d[1] = 1.0 / (A * Q); d[2] = 1; break;
            case lowShelf:  n[0] = A * A; n[1] = A * sA / Q; n[2] = A;  d[0] = 1; d[1] = sA / Q; d[2] = A; break;
            case highShelf: n[0] = A; n[1] = A * sA / Q; n[2] = A * A;  d[0] = A; d[1] = sA / Q; d[2] = 1; break;
            case lowCut:    n[0] = 0; n[1] = 0; n[2] = 1; break;
            case highCut:   n[0] = 1; n[1] = 0; n[2] = 0; break;
            default: break;
        }
        auto mag2 = [&] (double hz)
        {
            const C s (0.0, hz / f0);
            return std::norm ((n[0] + n[1] * s + n[2] * s * s) / (d[0] + d[1] * s + d[2] * s * s));
        };

        // pôles appariés : mêmes fréquence propre et amortissement que l'analogique
        const double wp = 2.0 * juce::MathConstants<double>::pi * f0 * std::sqrt (d[0] / d[2]) / fs;
        const double qp = d[1] / (2.0 * std::sqrt (d[0] * d[2]));
        double a1, a2 = std::exp (-2.0 * qp * wp);
        if (qp <= 1.0) a1 = -2.0 * std::exp (-qp * wp) * std::cos (std::sqrt (1.0 - qp * qp) * wp);
        else           a1 = -2.0 * std::exp (-qp * wp) * std::cosh (std::sqrt (qp * qp - 1.0) * wp);

        // numérateur : |H|^2 numérique = (B0 p0 + B1 p1 + B2 p2) / (A0 p0 + A1 p1 + A2 p2), appariement à 0 Hz,
        // à Nyquist et à la fréquence de la bande
        const double A0 = (1.0 + a1 + a2) * (1.0 + a1 + a2), A1 = (1.0 - a1 + a2) * (1.0 - a1 + a2), A2 = -4.0 * a2;
        const double fm = std::min (f0, 0.45 * fs);
        const double p1 = std::pow (std::sin (juce::MathConstants<double>::pi * fm / fs), 2.0), p0 = 1.0 - p1, p2 = 4.0 * p0 * p1;
        const double B0 = A0 * mag2 (0.0);
        const double B1 = A1 * mag2 (0.5 * fs);
        const double B2 = (mag2 (fm) * (A0 * p0 + A1 * p1 + A2 * p2) - B0 * p0 - B1 * p1) / p2;

        const double sB0 = std::sqrt (std::max (0.0, B0)), sB1 = std::sqrt (std::max (0.0, B1));
        const double W = 0.5 * (sB0 + sB1);
        const double b0 = 0.5 * (W + std::sqrt (std::max (0.0, W * W + B2)));
        const double b1 = 0.5 * (sB0 - sB1);
        const double b2 = b0 > 1.0e-12 ? -B2 / (4.0 * b0) : 0.0;

        BiquadCoeffs c;
        c.b0 = b0; c.b1 = b1; c.b2 = b2; c.a1 = a1; c.a2 = a2;
        return c;
    }

    // réponse (dB) de l'ensemble des bandes actives, pour l'affichage
    static float responseDb (double fs, const std::array<Band, kBands>& bands, double hz) noexcept
    {
        std::complex<double> h (1.0, 0.0);
        for (const auto& b : bands)
            if (b.on) h *= design (fs, b.type, b.freq, b.gainDb, b.q).response (fs, hz);
        return (float) (20.0 * std::log10 (std::max (1.0e-9, std::abs (h))));
    }

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        for (auto& s : st)
        {
            s.f.reset (fs, 0.05); s.g.reset (fs, 0.05); s.q.reset (fs, 0.05); s.mix.reset (fs, 0.03);
            s.f.setCurrentAndTargetValue (s.f.getTargetValue() > 0.0f ? s.f.getTargetValue() : 1000.0f);
            s.q.setCurrentAndTargetValue (s.q.getTargetValue() > 0.0f ? s.q.getTargetValue() : 1.0f);
            s.filter.reset();
            s.dirty = true;
        }
        countdown = 0;
    }

    void setBands (const std::array<Band, kBands>& b, bool snap) noexcept
    {
        for (int i = 0; i < kBands; ++i)
        {
            auto& s = st[(size_t) i];
            const auto& v = b[(size_t) i];
            const float f = juce::jlimit (20.0f, 20000.0f, v.freq), g = juce::jlimit (-18.0f, 18.0f, v.gainDb),
                        q = juce::jlimit (0.1f, 10.0f, v.q), m = v.on ? 1.0f : 0.0f;
            if (v.type != s.type) { s.type = v.type; s.dirty = true; }
            if (snap) { s.f.setCurrentAndTargetValue (f); s.g.setCurrentAndTargetValue (g); s.q.setCurrentAndTargetValue (q);
                        s.mix.setCurrentAndTargetValue (m); s.dirty = true; }
            else      { s.f.setTargetValue (f); s.g.setTargetValue (g); s.q.setTargetValue (q); s.mix.setTargetValue (m); }
        }
    }

    void process (float* const* ch, int numCh, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const bool recalc = --countdown <= 0;
            if (recalc) countdown = 16;

            for (auto& s : st)
            {
                const float f = s.f.getNextValue(), g = s.g.getNextValue(), q = s.q.getNextValue(), m = s.mix.getNextValue();
                if (m < 1.0e-5f && s.mix.getTargetValue() == 0.0f) continue;     // bande coupée : rien à calculer
                if (recalc && (s.dirty || f != s.lf || g != s.lg || q != s.lq))
                {
                    s.filter.setCoeffs (design (fs, s.type, f, g, q));
                    s.lf = f; s.lg = g; s.lq = q; s.dirty = false;
                }
                for (int c = 0; c < numCh; ++c)
                {
                    const float x = ch[c][i];
                    const float y = s.filter.process (x, c);
                    ch[c][i] = m >= 1.0f ? y : x + m * (y - x);
                }
            }
        }
    }

private:
    struct State
    {
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> f, q;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> g, mix;
        Biquad filter;
        int type = bell;
        float lf = -1.0f, lg = -100.0f, lq = -1.0f;
        bool dirty = true;
    };

    double fs = 44100.0;
    std::array<State, kBands> st;
    int countdown = 0;
};
} // namespace onirique
