#pragma once

#include <cmath>
#include <algorithm>

namespace n73
{
//==============================================================================
//  Transformateur audio : inductance magnétisante non linéaire vue par la résistance de la source.
//
//      source u, résistance Rs ──┬── primaire (flux λ, dλ/dt = v) ──┬── charge ramenée RL
//                                 └── courant magnétisant i(λ)       ┘
//
//      v = (u − Rs·i(λ)) / (1 + Rs/RL)        i(λ) = (λs/Lm)·[sinh((λ+λdc)/λs) − sinh(λdc/λs)]
//
//  Ce qui en découle, sans rien ajouter : la coupure grave (Rs ∥ RL face à Lm), et une distorsion qui grandit
//  vers le grave (le flux est l'intégrale de la tension) et avec la résistance de la source. λdc : flux continu
//  d'un primaire traversé par le courant de repos (sortie classe A à un transistor) — la courbe devient
//  dissymétrique, d'où des harmoniques paires dans le grave. Hystérésis non modélisée.
//
//  Intégration trapézoïdale implicite sur λ, résolue par Newton (prédiction explicite, une ou deux itérations).
//  Les paramètres sont rapportés au secondaire (tensions de sortie) ; l'état est un seul flux.
//==============================================================================
class Transformer
{
public:
    struct Params
    {
        double Lm = 10.0;        // inductance magnétisante aux petits signaux (H)
        double Rs = 150.0;       // résistance de la source (Ω)
        double RL = 1200.0;      // charge ramenée (Ω)
        double lambdaS = 0.5;    // flux de saturation (V·s)
        double lambdaDC = 0.0;   // flux continu de repos (V·s)
        double ratio = 1.0;      // rapport de transformation appliqué à la tension de sortie
    };

    void prepare (double sampleRate) { T = 1.0 / sampleRate; halfT = 0.5 * T; setParams (P); reset(); }
    void reset() { lambda = 0.0; vPrev = 0.0; iNow = 0.0; }

    void setParams (const Params& p)
    {
        P = p;
        k = 1.0 + P.Rs / P.RL;
        invK = 1.0 / k;
        invLs = 1.0 / P.lambdaS;
        invLm = 1.0 / P.Lm;
        newtonGain = halfT * P.Rs * invK * invLm;
        scale = P.lambdaS / P.Lm;
        dcOff = P.lambdaDC * invLs;
        i0 = scale * std::sinh (dcOff);
        iNow = 0.0;
    }

    /** sinh et cosh ensemble : polynôme exact à 3·10⁻⁶ près pour |x| < 1, une seule exponentielle au-delà. */
    static inline void sinhCosh (double x, double& sh, double& ch) noexcept
    {
        if (std::abs (x) < 1.0)
        {
            const double x2 = x * x;
            sh = x * (1.0 + x2 * (1.0 / 6.0 + x2 * (1.0 / 120.0 + x2 * (1.0 / 5040.0 + x2 * (1.0 / 362880.0)))));
            ch = 1.0 + x2 * (0.5 + x2 * (1.0 / 24.0 + x2 * (1.0 / 720.0 + x2 * (1.0 / 40320.0 + x2 * (1.0 / 3628800.0)))));
        }
        else
        {
            const double e = std::exp (std::clamp (x, -40.0, 40.0)), ie = 1.0 / e;
            sh = 0.5 * (e - ie);
            ch = 0.5 * (e + ie);
        }
    }

    inline double process (double u) noexcept
    {
        // prédiction : pas explicite avec le courant actuel
        double l = lambda + T * (u - P.Rs * iNow) * invK;
        double sh, ch, i = 0.0;
        for (int it = 0; it < 2; ++it)
        {
            sinhCosh (l * invLs + dcOff, sh, ch);
            i = scale * sh - i0;
            const double v = (u - P.Rs * i) * invK;
            const double F = l - lambda - halfT * (v + vPrev);
            const double didl = ch * invLm;
            const double step = F / (1.0 + newtonGain * ch);
            l -= step;
            // le courant au nouveau flux, au premier ordre (l'erreur est du second ordre en « step », négligeable
            // tant que le flux reste dans la partie presque linéaire) ; sinon, une seconde itération complète
            i -= step * didl;
            if (std::abs (l * invLs) < 0.35) break;
        }
        const double v = (u - P.Rs * i) * invK;
        if (! (std::isfinite (l) && std::isfinite (v))) { reset(); return 0.0; }
        lambda = l; vPrev = v; iNow = i;
        return v * P.ratio;
    }

    /** courant magnétisant actuel (A), utilisé par l'étage qui attaque le transformateur. */
    double magnetizingCurrent() const noexcept { return iNow; }

private:
    Params P;
    double T = 1.0 / 96000.0, halfT = 0.5 / 96000.0, k = 1.0, invK = 1.0, invLs = 2.0, invLm = 0.1, newtonGain = 0.0;
    double scale = 1.0, i0 = 0.0, dcOff = 0.0;
    double lambda = 0.0, vPrev = 0.0, iNow = 0.0;
};
} // namespace n73
