#pragma once

#include <cstdint>
#include <initializer_list>

namespace onirique::tune
{

/** Une gamme = un masque de 12 bits ; le bit i vaut 1 si le degré situé
    i demi-tons au-dessus de la tonique appartient à la gamme. */
struct ScaleDef
{
    const char* name;
    std::uint16_t mask;
};

constexpr std::uint16_t scaleBits (std::initializer_list<int> degrees) noexcept
{
    std::uint16_t m = 0;
    for (int d : degrees)
        m = (std::uint16_t) (m | (1u << d));
    return m;
}

inline constexpr ScaleDef scales[] = {
    { "Chromatic",        0x0fff },
    { "Major",            scaleBits ({ 0, 2, 4, 5, 7, 9, 11 }) },
    { "Minor",            scaleBits ({ 0, 2, 3, 5, 7, 8, 10 }) },
    { "Harmonic Minor",   scaleBits ({ 0, 2, 3, 5, 7, 8, 11 }) },
    { "Melodic Minor",    scaleBits ({ 0, 2, 3, 5, 7, 9, 11 }) },
    { "Dorian",           scaleBits ({ 0, 2, 3, 5, 7, 9, 10 }) },
    { "Phrygian",         scaleBits ({ 0, 1, 3, 5, 7, 8, 10 }) },
    { "Lydian",           scaleBits ({ 0, 2, 4, 6, 7, 9, 11 }) },
    { "Mixolydian",       scaleBits ({ 0, 2, 4, 5, 7, 9, 10 }) },
    { "Locrian",          scaleBits ({ 0, 1, 3, 5, 6, 8, 10 }) },
    { "Major Pentatonic", scaleBits ({ 0, 2, 4, 7, 9 }) },
    { "Minor Pentatonic", scaleBits ({ 0, 3, 5, 7, 10 }) },
    { "Blues",            scaleBits ({ 0, 3, 5, 6, 7, 10 }) },
    { "Whole Tone",       scaleBits ({ 0, 2, 4, 6, 8, 10 }) },
    { "Diminished",       scaleBits ({ 0, 1, 3, 4, 6, 7, 9, 10 }) },   // demi-ton, ton, en alternance
};

inline constexpr int numScales = (int) (sizeof (scales) / sizeof (scales[0]));

inline constexpr const char* noteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

/** Transpose le masque d'une gamme sur sa tonique : le résultat est indexé
    par classe de hauteur absolue (bit 0 = do, bit 9 = la...). */
constexpr std::uint16_t pitchClassMask (std::uint16_t scaleMask, int key) noexcept
{
    std::uint16_t m = 0;
    for (int i = 0; i < 12; ++i)
        if ((scaleMask & (1u << i)) != 0)
            m = (std::uint16_t) (m | (1u << ((i + key) % 12)));
    return m;
}

} // namespace onirique::tune
