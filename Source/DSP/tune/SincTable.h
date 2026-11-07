#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace onirique::tune
{

/** Interpolateur à sinus cardinal fenêtré (Kaiser), tabulé par phase.
    Sert à relire un grain à une vitesse différente de 1. */
class SincTable
{
public:
    void build (int tapsToUse, int phasesToUse, double cutoff, double kaiserBeta)
    {
        taps = tapsToUse;
        half = tapsToUse / 2;
        phases = phasesToUse;
        table.assign ((size_t) (phases + 1) * (size_t) taps, 0.0f);

        const double pi = 3.14159265358979323846;
        const double i0Beta = besselI0 (kaiserBeta);

        for (int p = 0; p <= phases; ++p)
        {
            const double frac = (double) p / (double) phases;
            double sum = 0.0;

            for (int k = 0; k < taps; ++k)
            {
                // Le tap k lit l'échantillon (base - half + 1 + k) ; la
                // position visée est base + frac.
                const double x = (double) (k - half + 1) - frac;
                const double arg = pi * cutoff * x;
                const double sinc = std::abs (arg) < 1.0e-9 ? 1.0 : std::sin (arg) / arg;
                const double r = x / (double) half;
                const double win = std::abs (r) >= 1.0 ? 0.0
                                                       : besselI0 (kaiserBeta * std::sqrt (1.0 - r * r)) / i0Beta;
                const double v = cutoff * sinc * win;
                table[(size_t) p * (size_t) taps + (size_t) k] = (float) v;
                sum += v;
            }

            // Gain unité en continu, quelle que soit la phase.
            if (std::abs (sum) > 1.0e-12)
                for (int k = 0; k < taps; ++k)
                    table[(size_t) p * (size_t) taps + (size_t) k] = (float) ((double) table[(size_t) p * (size_t) taps + (size_t) k] / sum);
        }
    }

    int halfLength() const noexcept { return half; }

    /** Valeur interpolée à la position (index + frac), 0 <= frac < 1. */
    float read (const float* ring, std::int64_t mask, std::int64_t index, float frac) const noexcept
    {
        const float phase = frac * (float) phases;
        int p = (int) phase;
        if (p >= phases)
            p = phases - 1;

        const float t = phase - (float) p;
        const float* c0 = table.data() + (size_t) p * (size_t) taps;
        const float* c1 = c0 + taps;

        float a = 0.0f, b = 0.0f;
        const std::int64_t base = index - half + 1;
        const std::int64_t first = base & mask;

        if (first + taps <= mask + 1)
        {
            const float* s = ring + first;
            for (int k = 0; k < taps; ++k)
            {
                a += s[k] * c0[k];
                b += s[k] * c1[k];
            }
        }
        else
        {
            for (int k = 0; k < taps; ++k)
            {
                const float s = ring[(size_t) ((base + k) & mask)];
                a += s * c0[k];
                b += s * c1[k];
            }
        }

        return a + t * (b - a);
    }

private:
    static double besselI0 (double x) noexcept
    {
        double sum = 1.0, term = 1.0;
        const double q = x * x * 0.25;
        for (int k = 1; k < 40; ++k)
        {
            term *= q / ((double) k * (double) k);
            sum += term;
            if (term < 1.0e-12 * sum)
                break;
        }
        return sum;
    }

    int taps = 0, half = 0, phases = 0;
    std::vector<float> table;
};

/** Jeu de tables à bande passante décroissante : quand un grain est relu plus
    vite (voix transposée vers l'aigu en mode classique), on choisit la table
    qui coupe sous la nouvelle fréquence de Nyquist pour éviter le repliement. */
class SincBank
{
public:
    void build (int taps, int phases)
    {
        for (size_t i = 0; i < tables.size(); ++i)
            tables[i].build (taps, phases, cutoffs[i], 6.5);
    }

    const SincTable& forRatio (double ratio) const noexcept
    {
        const double wanted = ratio <= 1.03 ? 1.0 : 0.97 / ratio;
        size_t pick = 0;
        while (pick + 1 < tables.size() && cutoffs[pick] > wanted + 1.0e-9)
            ++pick;
        return tables[pick];
    }

    int halfLength() const noexcept { return tables[0].halfLength(); }

private:
    static constexpr std::array<double, 7> cutoffs { 0.97, 0.87, 0.77, 0.68, 0.6, 0.53, 0.47 };
    std::array<SincTable, 7> tables;
};

} // namespace onirique::tune
