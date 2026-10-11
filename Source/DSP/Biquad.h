#pragma once

#include <algorithm>
#include <cmath>
#include <complex>

namespace onirique
{
// Coefficients biquad (formules RBJ "Audio EQ Cookbook", domaine public).
struct BiquadCoeffs
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

    static BiquadCoeffs make (double b0, double b1, double b2, double a0, double a1, double a2) noexcept
    {
        BiquadCoeffs c;
        const double inv = 1.0 / a0;
        c.b0 = b0 * inv; c.b1 = b1 * inv; c.b2 = b2 * inv;
        c.a1 = a1 * inv; c.a2 = a2 * inv;
        return c;
    }

    // réponse en fréquence complexe à f Hz (pour les courbes affichées)
    std::complex<double> response (double fs, double f) const noexcept
    {
        const std::complex<double> z1 = std::polar (1.0, -6.283185307179586 * f / fs), z2 = z1 * z1;
        return (b0 + b1 * z1 + b2 * z2) / (1.0 + a1 * z1 + a2 * z2);
    }

    static double clampFreq (double fs, double f) noexcept
    {
        return std::clamp (f, 10.0, 0.45 * fs);
    }

    static BiquadCoeffs peaking (double fs, double f, double q, double gainDb) noexcept
    {
        const double w0 = 6.283185307179586 * clampFreq (fs, f) / fs;
        const double cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        const double A = std::pow (10.0, gainDb / 40.0);
        return make (1.0 + alpha * A, -2.0 * cw, 1.0 - alpha * A,
                     1.0 + alpha / A, -2.0 * cw, 1.0 - alpha / A);
    }

    static BiquadCoeffs lowShelf (double fs, double f, double q, double gainDb) noexcept
    {
        const double w0 = 6.283185307179586 * clampFreq (fs, f) / fs;
        const double cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        const double A = std::pow (10.0, gainDb / 40.0);
        const double t = 2.0 * std::sqrt (A) * alpha;
        return make (A * ((A + 1) - (A - 1) * cw + t),
                     2.0 * A * ((A - 1) - (A + 1) * cw),
                     A * ((A + 1) - (A - 1) * cw - t),
                     (A + 1) + (A - 1) * cw + t,
                     -2.0 * ((A - 1) + (A + 1) * cw),
                     (A + 1) + (A - 1) * cw - t);
    }

    static BiquadCoeffs highShelf (double fs, double f, double q, double gainDb) noexcept
    {
        const double w0 = 6.283185307179586 * clampFreq (fs, f) / fs;
        const double cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        const double A = std::pow (10.0, gainDb / 40.0);
        const double t = 2.0 * std::sqrt (A) * alpha;
        return make (A * ((A + 1) + (A - 1) * cw + t),
                     -2.0 * A * ((A - 1) + (A + 1) * cw),
                     A * ((A + 1) + (A - 1) * cw - t),
                     (A + 1) - (A - 1) * cw + t,
                     2.0 * ((A - 1) - (A + 1) * cw),
                     (A + 1) - (A - 1) * cw - t);
    }

    // Passe-bande, gain de crête 0 dB
    static BiquadCoeffs bandpass (double fs, double f, double q) noexcept
    {
        const double w0 = 6.283185307179586 * clampFreq (fs, f) / fs;
        const double cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        return make (alpha, 0.0, -alpha, 1.0 + alpha, -2.0 * cw, 1.0 - alpha);
    }

    static BiquadCoeffs lowpass (double fs, double f, double q) noexcept
    {
        const double w0 = 6.283185307179586 * clampFreq (fs, f) / fs;
        const double cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        return make ((1.0 - cw) * 0.5, 1.0 - cw, (1.0 - cw) * 0.5,
                     1.0 + alpha, -2.0 * cw, 1.0 - alpha);
    }

    // Passe-tout du 2e ordre (phase seule, gain 1 partout)
    static BiquadCoeffs allpass (double fs, double f, double q) noexcept
    {
        const double w0 = 6.283185307179586 * clampFreq (fs, f) / fs;
        const double cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        return make (1.0 - alpha, -2.0 * cw, 1.0 + alpha,
                     1.0 + alpha, -2.0 * cw, 1.0 - alpha);
    }

    // Passe-haut du 1er ordre (6 dB/oct), transformée bilinéaire
    static BiquadCoeffs highpass1 (double fs, double f) noexcept
    {
        const double K = std::tan (3.141592653589793 * clampFreq (fs, f) / fs);
        return make (1.0, -1.0, 0.0, 1.0 + K, K - 1.0, 0.0);
    }

    static BiquadCoeffs highpass (double fs, double f, double q) noexcept
    {
        const double w0 = 6.283185307179586 * clampFreq (fs, f) / fs;
        const double cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        return make ((1.0 + cw) * 0.5, -(1.0 + cw), (1.0 + cw) * 0.5,
                     1.0 + alpha, -2.0 * cw, 1.0 - alpha);
    }
};

// Biquad Transposed Direct Form II, jusqu'à 2 canaux, état en double précision.
class Biquad
{
public:
    void setCoeffs (const BiquadCoeffs& c) noexcept { k = c; }

    void reset() noexcept
    {
        for (int i = 0; i < 2; ++i) z1[i] = z2[i] = 0.0;
    }

    inline float process (float x, int ch) noexcept
    {
        const double in = (double) x;
        const double out = k.b0 * in + z1[ch];
        z1[ch] = k.b1 * in - k.a1 * out + z2[ch];
        z2[ch] = k.b2 * in - k.a2 * out;
        return (float) out;
    }

private:
    BiquadCoeffs k;
    double z1[2] {}, z2[2] {};
};
} // namespace onirique
