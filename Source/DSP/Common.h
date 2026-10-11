#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace onirique
{
// Détection NaN/Inf par les bits de l'exposant : fonctionne même avec -ffast-math.
inline bool isBadFloat (float x) noexcept
{
    std::uint32_t u;
    std::memcpy (&u, &x, sizeof (u));
    return (u & 0x7f800000u) == 0x7f800000u;
}

// Remplace NaN/Inf par 0 et borne à +/-lim (~ +30 dB au-dessus de 0 dBFS par défaut).
inline float sanitize (float x, float lim = 32.0f) noexcept
{
    if (isBadFloat (x)) return 0.0f;
    return x > lim ? lim : (x < -lim ? -lim : x);
}

inline float dbToGain (float db) noexcept { return std::exp (db * 0.11512925465f); }
inline float gainToDb (float g) noexcept  { return 8.68588963807f * std::log (g); }

// Ligne à retard simple (délai entier), allocation uniquement dans prepare().
class DelayLine
{
public:
    void prepare (int delaySamples)
    {
        len = delaySamples;
        buf.assign ((size_t) std::max (1, delaySamples), 0.0f);
        idx = 0;
    }

    void reset() noexcept { std::fill (buf.begin(), buf.end(), 0.0f); idx = 0; }

    // longueur réglable dans la capacité allouée par prepare() (remet la ligne à zéro)
    void setLength (int delaySamples) noexcept
    {
        len = std::clamp (delaySamples, 0, (int) buf.size());
        reset();
    }

    inline float process (float in) noexcept
    {
        if (len <= 0) return in;
        const float out = buf[(size_t) idx];
        buf[(size_t) idx] = in;
        if (++idx >= len) idx = 0;
        return out;
    }

private:
    std::vector<float> buf;
    int len = 0, idx = 0;
};
} // namespace onirique
