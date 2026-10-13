#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <complex>
#include "Biquad.h"
#include "Common.h"

namespace onirique
{
/*  PARAMETRIC EQ : 8 bandes "ultra clean", premier module de la chaîne (le nettoyage de la voix).

    Filtres "appariés à l'analogique" (méthode de Vicanek, 2016) : les pôles sont placés comme ceux du
    filtre analogique idéal, puis le numérateur est calculé pour que la courbe numérique coïncide avec la
    courbe analogique en trois points (0 Hz, fréquence de la bande, Nyquist). Contrairement aux biquads
    classiques (transformée bilinéaire), une cloche ou un shelf à 10-20 kHz garde exactement sa forme :
    pas de "resserrement" près de Nyquist. Calcul et états en double précision, aucune latence, aucune coloration.

    Types : BELL, LOW SHELF, HIGH SHELF (gain +/-18 dB, Q 0.1..10), NOTCH (zéro exact à la fréquence),
    LOW CUT, HIGH CUT : 12, 24, 36 ou 48 dB/oct (Butterworth en cascade ; le Q règle la résonance de la
    dernière section : à 0.71 la coupe est parfaitement plate).

    Mode DYNAMIQUE (cloche, shelfs) : la bande ne touche le son que lorsque sa zone dépasse le SEUIL.
    Détecteur : passe-bande (cloche) / passe-bas / passe-haut (shelfs) sur l'entrée de la bande, enveloppe crête
    3 ms / 90 ms, liaison stéréo. Le gain appliqué glisse de 0 dB (au seuil) jusqu'au gain réglé (9 dB au-dessus),
    genou doux, lissé sur 4 ms ; les coefficients suivent toutes les 8 échantillons. Une coupe dynamique ne creuse
    que quand la résonance / la dureté apparaît : la voix garde son corps le reste du temps.

    Réglages lissés (pas d'escalier en tournant un bouton), activation et passage statique / dynamique en fondu.
*/
class ParametricEQ
{
public:
    static constexpr int kBands = 8, kMaxStages = 4;
    static constexpr float kDynRangeDb = 9.0f;
    enum Type { bell = 0, lowShelf, highShelf, lowCut, highCut, notch, numTypes };

    struct Band
    {
        bool on = false;
        int type = bell;
        float freq = 1000.0f, gainDb = 0.0f, q = 1.0f;
        int slope = 0;                 // 0..3 : 12 / 24 / 36 / 48 dB/oct (coupe-bas / coupe-haut)
        bool dyn = false;              // mode dynamique (cloche, shelfs)
        float thrDb = -30.0f;
    };

    static bool hasGain (int type) noexcept      { return type == bell || type == lowShelf || type == highShelf; }
    static bool isCut (int type) noexcept        { return type == lowCut || type == highCut; }
    static bool canBeDynamic (int type) noexcept { return hasGain (type); }
    static int numStages (int type, int slope) noexcept { return isCut (type) ? juce::jlimit (1, kMaxStages, slope + 1) : 1; }

    // ---- conception (partagée avec l'interface : la courbe affichée est celle qui tourne)
    static BiquadCoeffs design (double fs, int type, double f0, double gainDb, double Q) noexcept
    {
        using C = std::complex<double>;
        f0 = juce::jlimit (10.0, 0.49 * fs, f0);
        Q = juce::jlimit (0.05, 30.0, Q);
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

        double a1, a2;
        matchedPoles (fs, f0, d, a1, a2);

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

    // NOTCH : pôles appariés, zéros exactement sur la fréquence (creux infini), gain 1 en continu
    static BiquadCoeffs designNotch (double fs, double f0, double Q) noexcept
    {
        f0 = juce::jlimit (10.0, 0.49 * fs, f0);
        const double d[3] { 1.0, 1.0 / juce::jlimit (0.05, 30.0, Q), 1.0 };
        double a1, a2;
        matchedPoles (fs, f0, d, a1, a2);
        const double cw = std::cos (2.0 * juce::MathConstants<double>::pi * f0 / fs);
        const double gn = (1.0 + a1 + a2) / std::max (1.0e-12, 2.0 - 2.0 * cw);
        BiquadCoeffs c;
        c.b0 = gn; c.b1 = -2.0 * cw * gn; c.b2 = gn; c.a1 = a1; c.a2 = a2;
        return c;
    }

    // section k d'une bande (les coupe-bas / coupe-haut en ont jusqu'à 4)
    static BiquadCoeffs designStage (double fs, int type, double f0, double gainDb, double Q, int slope, int stage) noexcept
    {
        if (isCut (type))
        {
            const int N = numStages (type, slope);
            double qk = 1.0 / (2.0 * std::cos ((2.0 * stage + 1.0) * juce::MathConstants<double>::pi / (4.0 * N)));
            if (stage == N - 1) qk *= Q / 0.70710678118;      // résonance réglable sur la section la plus pointue
            return design (fs, type, f0, 0.0, qk);
        }
        if (type == notch) return designNotch (fs, f0, Q);
        return design (fs, type, f0, gainDb, Q);
    }

    // réponse complexe d'une bande (toutes ses sections), avec un gain imposé (gain dynamique en cours)
    static std::complex<double> bandResponse (double fs, const Band& b, double hz, double gainDb) noexcept
    {
        std::complex<double> h (1.0, 0.0);
        const int ns = numStages (b.type, b.slope);
        for (int k = 0; k < ns; ++k)
            h *= designStage (fs, b.type, b.freq, gainDb, b.q, b.slope, k).response (fs, hz);
        return h;
    }

    // réponse (dB) de l'ensemble des bandes actives, pour l'affichage
    static float responseDb (double fs, const std::array<Band, kBands>& bands, double hz) noexcept
    {
        std::complex<double> h (1.0, 0.0);
        for (const auto& b : bands)
            if (b.on) h *= bandResponse (fs, b, hz, b.gainDb);
        return (float) (20.0 * std::log10 (std::max (1.0e-9, std::abs (h))));
    }

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        atkC = (float) (1.0 - std::exp (-1.0 / (0.003 * fs)));
        relC = (float) (1.0 - std::exp (-1.0 / (0.090 * fs)));
        dynC = (float) (1.0 - std::exp (-1.0 / (0.004 * fs)));
        for (auto& s : st)
        {
            s.f.reset (fs, 0.05); s.g.reset (fs, 0.05); s.q.reset (fs, 0.05); s.mix.reset (fs, 0.03);
            s.f.setCurrentAndTargetValue (s.f.getTargetValue() > 0.0f ? s.f.getTargetValue() : 1000.0f);
            s.q.setCurrentAndTargetValue (s.q.getTargetValue() > 0.0f ? s.q.getTargetValue() : 1.0f);
            for (auto& b : s.stage) b.reset();
            s.det.reset();
            s.env = 0.0f;
            s.dynS = s.dyn ? 0.0f : 1.0f;
            s.dirty = true;
            s.detDirty = true;
            s.countdown = 0;
        }
    }

    void setBands (const std::array<Band, kBands>& b, bool snap) noexcept
    {
        for (int i = 0; i < kBands; ++i)
        {
            auto& s = st[(size_t) i];
            const auto& v = b[(size_t) i];
            const float f = juce::jlimit (20.0f, 20000.0f, v.freq), g = juce::jlimit (-18.0f, 18.0f, v.gainDb),
                        q = juce::jlimit (0.1f, 10.0f, v.q), m = v.on ? 1.0f : 0.0f;
            const int type = juce::jlimit (0, numTypes - 1, v.type);
            const int ns = numStages (type, v.slope);
            if (type != s.type || v.slope != s.slope)
            {
                if (ns != s.nStages) for (int k = s.nStages; k < ns; ++k) s.stage[k].reset();   // sections ajoutées : état vierge
                s.type = type; s.slope = v.slope; s.nStages = ns;
                s.dirty = s.detDirty = true;
            }
            s.dyn = v.dyn && canBeDynamic (type);
            s.thr = juce::jlimit (-80.0f, 0.0f, v.thrDb);
            if (snap) { s.f.setCurrentAndTargetValue (f); s.g.setCurrentAndTargetValue (g); s.q.setCurrentAndTargetValue (q);
                        s.mix.setCurrentAndTargetValue (m); s.dynS = s.dyn ? 0.0f : 1.0f; s.dirty = s.detDirty = true; }
            else      { s.f.setTargetValue (f); s.g.setTargetValue (g); s.q.setTargetValue (q); s.mix.setTargetValue (m); }
        }
    }

    void process (float* const* ch, int numCh, int n) noexcept
    {
        for (size_t bi = 0; bi < (size_t) kBands; ++bi)
        {
            auto& s = st[bi];
            if (! s.mix.isSmoothing() && s.mix.getTargetValue() < 0.5f)
            {
                // bande coupée : rien à calculer (les réglages continuent de glisser vers leur cible)
                s.f.skip (n); s.g.skip (n); s.q.skip (n);
                s.dynS = s.dyn ? 0.0f : 1.0f;
                s.env = 0.0f;
                gainMeter[bi].store (0.0f, std::memory_order_relaxed);
                continue;
            }

            float shown = s.lg > -50.0f ? s.lg : 0.0f;
            for (int i = 0; i < n; ++i)
            {
                const float f = s.f.getNextValue(), g = s.g.getNextValue(), q = s.q.getNextValue(), m = s.mix.getNextValue();

                if (s.dyn)
                {
                    if (s.detDirty) updateDetector (s, f, q);
                    float d = 0.0f;
                    for (int c = 0; c < numCh; ++c) d = std::max (d, std::abs (s.det.process (ch[c][i], c)));
                    s.env += (d > s.env ? atkC : relC) * (d - s.env);
                }

                if (--s.countdown <= 0)
                {
                    s.countdown = s.dyn ? 8 : 16;
                    if (s.dyn)
                    {
                        if (f != s.detF || q != s.detQ) updateDetector (s, f, q);
                        // 0 au seuil -> 1 à kDynRangeDb au-dessus, genou doux (smoothstep)
                        const float over = gainToDb (s.env + 1.0e-9f) - s.thr;
                        const float t = juce::jlimit (0.0f, 1.0f, over / kDynRangeDb);
                        s.dynTarget = t * t * (3.0f - 2.0f * t);
                    }
                    else
                        s.dynTarget = 1.0f;

                    const float gEff = g * s.dynS;
                    if (s.dirty || f != s.lf || std::abs (gEff - s.lg) > 0.01f || q != s.lq)
                    {
                        for (int k = 0; k < s.nStages; ++k)
                            s.stage[k].setCoeffs (designStage (fs, s.type, f, gEff, q, s.slope, k));
                        s.lf = f; s.lg = gEff; s.lq = q; s.dirty = false;
                    }
                    shown = gEff;
                }
                s.dynS += dynC * (s.dynTarget - s.dynS);

                for (int c = 0; c < numCh; ++c)
                {
                    const float x = ch[c][i];
                    float y = x;
                    for (int k = 0; k < s.nStages; ++k) y = s.stage[k].process (y, c);
                    ch[c][i] = m >= 1.0f ? y : x + m * (y - x);
                }
            }
            gainMeter[bi].store (shown, std::memory_order_relaxed);
        }
    }

    // gain appliqué en ce moment par la bande (dB) : suit le mode dynamique, pour l'affichage
    float getBandGainDb (int b) const noexcept { return gainMeter[(size_t) juce::jlimit (0, kBands - 1, b)].load (std::memory_order_relaxed); }

private:
    // pôles appariés : mêmes fréquence propre et amortissement que l'analogique (dénominateur d = [s^0, s^1, s^2])
    static void matchedPoles (double fs, double f0, const double d[3], double& a1, double& a2) noexcept
    {
        const double wp = 2.0 * juce::MathConstants<double>::pi * f0 * std::sqrt (d[0] / d[2]) / fs;
        const double qp = d[1] / (2.0 * std::sqrt (d[0] * d[2]));
        a2 = std::exp (-2.0 * qp * wp);
        if (qp <= 1.0) a1 = -2.0 * std::exp (-qp * wp) * std::cos (std::sqrt (1.0 - qp * qp) * wp);
        else           a1 = -2.0 * std::exp (-qp * wp) * std::cosh (std::sqrt (qp * qp - 1.0) * wp);
    }

    struct State
    {
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> f { 1000.0f }, q { 1.0f };
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> g, mix;
        Biquad stage[kMaxStages];
        Biquad det;                    // détecteur du mode dynamique
        int type = bell, slope = 0, nStages = 1, countdown = 0;
        bool dyn = false, dirty = true, detDirty = true;
        float thr = -30.0f, env = 0.0f, dynS = 1.0f, dynTarget = 1.0f;
        float lf = -1.0f, lg = -100.0f, lq = -1.0f, detF = -1.0f, detQ = -1.0f;
    };

    void updateDetector (State& s, float f, float q) noexcept
    {
        if (s.type == lowShelf)       s.det.setCoeffs (BiquadCoeffs::lowpass (fs, f, 0.707));
        else if (s.type == highShelf) s.det.setCoeffs (BiquadCoeffs::highpass (fs, f, 0.707));
        else                          s.det.setCoeffs (BiquadCoeffs::bandpass (fs, f, std::max (0.7f, q)));
        s.detF = f; s.detQ = q; s.detDirty = false;
    }

    double fs = 44100.0;
    float atkC = 0.01f, relC = 0.001f, dynC = 0.01f;
    std::array<State, kBands> st;
    std::array<std::atomic<float>, kBands> gainMeter {};
};
} // namespace onirique
