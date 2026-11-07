#pragma once

#include <cmath>
#include <algorithm>

namespace n73
{
//==============================================================================
//  Petits filtres (bilinéaires), en double.
//==============================================================================
struct OnePole
{
    // H(s) = (b0·s + b1·w) / (s + w)
    double a1 = 0, c0 = 1, c1 = 0, z = 0;

    void setHighPass (double fc, double fs) { set (1.0, 0.0, fc, fs); }
    void setLowPass (double fc, double fs)  { set (0.0, 1.0, fc, fs); }
    void set (double b0, double b1, double fc, double fs)
    {
        const double w = 2.0 * fs * std::tan (3.14159265358979323846 * std::min (fc, 0.45 * fs) / fs);   // précompensé
        const double K = 2.0 * fs, norm = 1.0 / (K + w);
        c0 = (b0 * K + b1 * w) * norm;
        c1 = (-b0 * K + b1 * w) * norm;
        a1 = (w - K) * norm;
    }
    /** l'inverse exact d'un filtre à phase minimale (zéro dans le cercle unité). */
    void setInverseOf (const OnePole& f)
    {
        c0 = 1.0 / f.c0; c1 = f.a1 / f.c0; a1 = f.c1 / f.c0;
    }
    void reset() { z = 0; }
    inline double process (double x) noexcept
    {
        const double y = c0 * x + z;
        z = c1 * x - a1 * y;
        return y;
    }
};

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    /** passe-bas / passe-haut du second ordre, précompensés à fWarp (la réponse y est exacte). */
    void setLowPass (double f0, double q, double fs, double fWarp)  { setAnalog (0, 0, 1, f0, q, fs, fWarp); }
    void setHighPass (double f0, double q, double fs, double fWarp) { setAnalog (1, 0, 0, f0, q, fs, fWarp); }

    /** H(s) = (n2·s² + n1·w0·s + n0·w0²) / (s² + (w0/q)·s + w0²), bilinéaire précompensée à fWarp. */
    void setAnalog (double n2, double n1, double n0, double f0, double q, double fs, double fWarp)
    {
        const double pi = 3.14159265358979323846;
        const double ww = 2.0 * pi * std::min (fWarp, 0.49 * fs);
        const double K = ww / std::tan (ww / (2.0 * fs));
        const double w0 = 2.0 * pi * f0, K2 = K * K, w02 = w0 * w0;
        const double a0 = K2 + K * w0 / q + w02;
        b0 = (n2 * K2 + n1 * w0 * K + n0 * w02) / a0;
        b1 = (2.0 * n0 * w02 - 2.0 * n2 * K2) / a0;
        b2 = (n2 * K2 - n1 * w0 * K + n0 * w02) / a0;
        a1 = (2.0 * w02 - 2.0 * K2) / a0;
        a2 = (K2 - K * w0 / q + w02) / a0;
    }
    void reset() { z1 = z2 = 0; }
    inline double process (double x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

//==============================================================================
//  Étage d'amplification classe A à contre-réaction (cartes BA283 / BA284), modèle statique.
//
//  - gain linéaire, puis une courbe sans mémoire aux petits signaux :  u = x + a2·x² + a3·x³
//    (a3 < 0 : légère compression, harmonique 3 qui croît comme le carré du niveau — dominante à niveau nominal
//    d'après la mesure SOS du 1073N ; a2 : dissymétrie du transistor polarisé en classe A, harmonique 2) ;
//  - puis les butées, douces et dissymétriques +Vp / −Vn : y = u / (1 + |u/V|^8)^(1/8), très linéaire jusqu'aux
//    rails comme un ampli contre-réactionné ;
//  - la contre-réaction faiblit dans l'aigu : une accentuation avant la courbe et son inverse exact après font
//    monter la distorsion au-dessus de fEmph sans changer la réponse en fréquence ;
//  - condensateur de liaison en sortie (passe-haut), qui recentre le signal après un écrêtage dissymétrique.
//
//  hd2 / hd3 : harmoniques 2 et 3 (en fraction) pour une sortie de vRef volts crête, avant les butées.
//  Valeurs estimées et calées sur les mesures publiées (docs/MODELE.md).
//==============================================================================
class AmpStage
{
public:
    struct Params
    {
        double gain = 1.0;                 // gain linéaire de l'étage
        double hd2 = 1.0e-4, hd3 = 2.0e-4; // à vRef
        double vRef = 10.0;
        double vPos = 11.0, vNeg = 10.0;   // butées (V crête)
        double fEmph = 6000.0, emph = 3.0; // au-dessus de fEmph, la distorsion monte jusqu'à ×emph
        double fCouple = 1.5;              // condensateur de liaison (Hz)
    };

    void prepare (double sampleRate) { fs = sampleRate; setParams (P); reset(); }
    void reset() { pre.reset(); post.reset(); couple.reset(); }

    void setParams (const Params& p)
    {
        P = p;
        a2 = 2.0 * P.hd2 / P.vRef;
        a3 = -4.0 * P.hd3 / (P.vRef * P.vRef);
        // la courbe reste croissante : x est borné avant que la pente de la cubique ne tombe sous 1/4
        xMax = std::min (4.0 * std::max (P.vPos, P.vNeg), 0.5 / std::sqrt (std::max (1.0e-12, std::abs (a3))));
        pre.set (P.emph, 1.0, P.fEmph * P.emph, fs);   // gain 1 en bas, ×emph au-dessus de fEmph
        post.setInverseOf (pre);
        couple.setHighPass (P.fCouple, fs);
        invP = 1.0 / P.vPos; invN = 1.0 / P.vNeg;
    }

    void setGain (double g) noexcept { P.gain = g; }
    double getGain() const noexcept { return P.gain; }

    static inline double rail (double u, double inv) noexcept
    {
        const double t = u * inv, t2 = t * t, t4 = t2 * t2, t8 = t4 * t4;
        if (t8 < 1.0e-5) return u * (1.0 - 0.125 * t8);   // loin des rails : développement exact à 10⁻¹⁰ près
        return u / std::sqrt (std::sqrt (std::sqrt (1.0 + t8)));
    }

    inline double shape (double x) const noexcept
    {
        x = std::clamp (x, -xMax, xMax);
        const double u = x * (1.0 + x * (a2 + x * a3));
        return rail (u, u >= 0.0 ? invP : invN);
    }

    inline double process (double x) noexcept
    {
        const double e = pre.process (x * P.gain);
        return couple.process (post.process (shape (e)));
    }

private:
    Params P;
    double fs = 96000.0, xMax = 100.0, invP = 0.1, invN = 0.1, a2 = 0.0, a3 = 0.0;
    OnePole pre, post, couple;
};
} // namespace n73
