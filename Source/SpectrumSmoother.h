#pragma once

#include <juce_core/juce_core.h>
#include <cmath>
#include <vector>

namespace onirique::ui
{
/*  Lissage d'un spectre FFT pour l'analyseur de l'EQ (même code dans VOCALBOX et VOCAL MINI).

    Chaque point de l'écran (20 Hz - 20 kHz, échelle logarithmique) est une moyenne des bins pondérée par une gaussienne
    de 1/12 d'octave à mi-hauteur, jamais plus étroite que la résolution de la FFT dans les graves : pas de plateau, pas
    d'angle, les crêtes restent arrondies et les harmoniques de la voix se détachent dans le bas-médium.

    Étalonnage : pour chaque point, la réponse du lissage à un sinus pile à sa fréquence (lobe principal de la fenêtre
    de Hann) est calculée une fois ; divisée par elle, un sinus d'amplitude A se lit 20 log10 A partout. Un bruit se lit
    en puissance de bande : un bruit rose est plat.

    Les poids ne sont calculés qu'une fois par fréquence d'échantillonnage : une analyse ne coûte qu'une somme pondérée
    par point (quelques dizaines de milliers de multiplications), rien n'est alloué pendant l'affichage.
*/
class SpectrumSmoother
{
public:
    // nPts points de 20 Hz à 20 kHz, FFT de fftSize points, fwhmOct : largeur à mi-hauteur du lissage (octaves)
    void build (double fs, int fftSize, int nPts, double fwhmOct = 1.0 / 12.0)
    {
        pts.assign ((size_t) nPts, {});
        w.clear();
        const double binHz = fs / fftSize;
        const int maxBin = fftSize / 2;
        const double rel = std::pow (2.0, 0.5 * fwhmOct) - std::pow (2.0, -0.5 * fwhmOct);   // largeur relative à mi-hauteur
        for (int i = 0; i < nPts; ++i)
        {
            const double c = 20.0 * std::pow (1000.0, (double) i / (double) (nPts - 1)) / binHz;   // centre, en bins
            const double sigma = std::max (0.75, rel * c / 2.3548);                                 // jamais sous la résolution
            // à partir du bin 2 : le continu (et sa fuite dans le bin 1) ne remonte jamais le bas de la courbe
            const int b0 = juce::jlimit (2, maxBin, (int) std::floor (c - 3.0 * sigma));
            const int b1 = juce::jlimit (b0, maxBin, (int) std::ceil (c + 3.0 * sigma));
            auto& p = pts[(size_t) i];
            p.b0 = b0;
            p.n = b1 - b0 + 1;
            p.w0 = (int) w.size();
            double norm = 0.0;
            for (int k = b0; k <= b1; ++k)
            {
                const double d = (double) k - c;
                const double wk = std::exp (-0.5 * d * d / (sigma * sigma));
                w.push_back ((float) wk);
                norm += wk * hannLobe (d);
            }
            p.inv = (float) (1.0 / std::max (1.0e-9, norm));
        }
    }

    // power : puissance par bin (un sinus d'amplitude A centré sur un bin y vaut A^2) ; db[i] : niveau du point i
    template <typename Levels>
    void apply (const float* power, Levels& db) const noexcept
    {
        for (size_t i = 0; i < pts.size(); ++i)
        {
            const auto& p = pts[i];
            const float* x = power + p.b0;
            const float* wk = w.data() + p.w0;
            float e = 0.0f;
            for (int k = 0; k < p.n; ++k) e += wk[k] * x[k];
            db[i] = 10.0f * std::log10 (e * p.inv + 1.0e-14f);
        }
    }

private:
    // lobe principal de la fenêtre de Hann, en puissance (1 au centre), à d bins du sinus
    static double hannLobe (double d) noexcept
    {
        constexpr double pi = 3.141592653589793;
        d = std::abs (d);
        if (d < 1.0e-6) return 1.0;
        if (std::abs (d - 1.0) < 1.0e-6) return 0.25;
        const double h = std::sin (pi * d) / (pi * d * (1.0 - d * d));
        return h * h;
    }

    struct Point { int b0 = 1, n = 0, w0 = 0; float inv = 1.0f; };
    std::vector<Point> pts;
    std::vector<float> w;
};
} // namespace onirique::ui
