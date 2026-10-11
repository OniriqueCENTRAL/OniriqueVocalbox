#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace onirique
{
/*  Prise d'analyse : le thread audio y pousse un mélange mono du signal (sans allocation, sans verrou,
    une seule FIFO producteur -> consommateur) ; l'analyseur de l'interface la vide à son rythme.
    Si l'interface est fermée, la FIFO se remplit et les nouveaux échantillons sont simplement ignorés.
*/
class SpectrumTap
{
public:
    static constexpr int kSize = 1 << 15;

    SpectrumTap() : buf ((size_t) kSize, 0.0f) {}

    // thread audio
    void push (const float* l, const float* r, int n) noexcept
    {
        int s1, n1, s2, n2;
        fifo.prepareToWrite (n, s1, n1, s2, n2);
        auto mono = [&] (int i) { return r != nullptr ? 0.5f * (l[i] + r[i]) : l[i]; };
        for (int i = 0; i < n1; ++i) buf[(size_t) (s1 + i)] = mono (i);
        for (int i = 0; i < n2; ++i) buf[(size_t) (s2 + i)] = mono (n1 + i);
        fifo.finishedWrite (n1 + n2);
    }

    // thread de l'interface : copie jusqu'à maxN échantillons, renvoie le nombre lu
    int pull (float* dest, int maxN) noexcept
    {
        int s1, n1, s2, n2;
        fifo.prepareToRead (maxN, s1, n1, s2, n2);
        for (int i = 0; i < n1; ++i) dest[i] = buf[(size_t) (s1 + i)];
        for (int i = 0; i < n2; ++i) dest[n1 + i] = buf[(size_t) (s2 + i)];
        fifo.finishedRead (n1 + n2);
        return n1 + n2;
    }

private:
    juce::AbstractFifo fifo { kSize };
    std::vector<float> buf;
};
} // namespace onirique
