#pragma once

#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

namespace onirique::tune
{

/** FFT complexe radix-2, en place, sur deux tableaux séparés (réel / imaginaire).
    Volontairement minuscule et sans dépendance : elle ne sert qu'à
    l'autocorrélation du détecteur de hauteur. */
class Fft
{
public:
    explicit Fft (int orderToUse)
        : order (orderToUse), size (1 << orderToUse)
    {
        const auto n = (size_t) size;

        reversed.resize (n);
        for (size_t i = 0; i < n; ++i)
        {
            size_t r = 0;
            for (int b = 0; b < order; ++b)
                if ((i & ((size_t) 1 << b)) != 0)
                    r |= (size_t) 1 << (order - 1 - b);

            reversed[i] = (int) r;
        }

        cosTable.resize (n / 2 + 1);
        sinTable.resize (n / 2 + 1);
        for (size_t k = 0; k < n / 2 + 1; ++k)
        {
            const double a = -2.0 * 3.14159265358979323846 * (double) k / (double) n;
            cosTable[k] = (float) std::cos (a);
            sinTable[k] = (float) std::sin (a);
        }
    }

    int getSize() const noexcept { return size; }

    void forward (float* re, float* im) const noexcept { transform (re, im, false); }

    /** Transformée inverse, facteur 1/N inclus. */
    void inverse (float* re, float* im) const noexcept
    {
        transform (re, im, true);

        const float scale = 1.0f / (float) size;
        for (int i = 0; i < size; ++i)
        {
            re[i] *= scale;
            im[i] *= scale;
        }
    }

private:
    void transform (float* re, float* im, bool inverse) const noexcept
    {
        for (int i = 0; i < size; ++i)
        {
            const int j = reversed[(size_t) i];
            if (j > i)
            {
                std::swap (re[i], re[j]);
                std::swap (im[i], im[j]);
            }
        }

        const float sign = inverse ? -1.0f : 1.0f;

        for (int len = 2; len <= size; len <<= 1)
        {
            const int half = len >> 1;
            const int step = size / len;

            for (int k = 0; k < half; ++k)
            {
                const float wr = cosTable[(size_t) (k * step)];
                const float wi = sign * sinTable[(size_t) (k * step)];

                for (int a = k; a < size; a += len)
                {
                    const int b = a + half;
                    const float xr = re[b] * wr - im[b] * wi;
                    const float xi = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - xr;
                    im[b] = im[a] - xi;
                    re[a] += xr;
                    im[a] += xi;
                }
            }
        }
    }

    int order = 0;
    int size = 0;
    std::vector<int> reversed;
    std::vector<float> cosTable, sinTable;
};

} // namespace onirique::tune
