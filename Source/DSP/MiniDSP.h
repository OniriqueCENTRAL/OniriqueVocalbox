#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <vector>
#include "Biquad.h"
#include "Common.h"

namespace onirique::mini
{
// coefficient d'un lissage exponentiel (une constante de temps en secondes)
inline float coefFor (double seconds, double fs) noexcept { return (float) (1.0 - std::exp (-1.0 / (std::max (1.0e-6, seconds) * fs))); }

// tampon circulaire, lecture fractionnaire (Hermite 4 points)
class ModDelay
{
public:
    void prepare (int maxSamples)
    {
        int s = 1;
        while (s < maxSamples + 8) s <<= 1;
        buf.assign ((size_t) s, 0.0f);
        mask = s - 1;
        w = 0;
    }
    void reset() noexcept { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
    void push (float x) noexcept { buf[(size_t) w] = x; w = (w + 1) & mask; }
    // valeur écrite il y a d échantillons (d >= 2)
    float read (float d) const noexcept
    {
        d = juce::jlimit (2.0f, (float) (mask - 4), d);
        const float pos = (float) (w - 1) - d;
        const float fl = std::floor (pos);
        const int i = (int) fl;
        const float f = pos - fl;
        const float y0 = at (i - 1), y1 = at (i), y2 = at (i + 1), y3 = at (i + 2);
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }
    float tap (int d) const noexcept { return at (w - 1 - d); }

private:
    float at (int i) const noexcept { return buf[(size_t) (i & mask)]; }
    std::vector<float> buf;
    int mask = 0, w = 0;
};

/*  Biquad pour les filtres qu'on règle en jouant (EQ, filtres de la reverb) : forme directe I (l'état est fait des
    échantillons passés, il reste juste quels que soient les coefficients) et coefficients qui GLISSENT échantillon par
    échantillon vers leur nouvelle valeur. Une forme transposée dont les coefficients sautent toutes les quelques
    dizaines d'échantillons produit de petites cassures de pente (le « zipper » qu'on entend en tournant un bouton
    d'EQ) ; ici la courbe se déforme continûment. tick() une fois par échantillon, puis process() pour chaque canal. */
class SmoothBiquad
{
public:
    void reset() noexcept { for (int c = 0; c < 2; ++c) x1[c] = x2[c] = y1[c] = y2[c] = 0.0; }
    void setNow (const BiquadCoeffs& k) noexcept { cur = tgt = k; steps = 0; }
    void glideTo (const BiquadCoeffs& k, int numSamples) noexcept
    {
        tgt = k;
        steps = std::max (1, numSamples);
        const double inv = 1.0 / steps;
        inc.b0 = (k.b0 - cur.b0) * inv; inc.b1 = (k.b1 - cur.b1) * inv; inc.b2 = (k.b2 - cur.b2) * inv;
        inc.a1 = (k.a1 - cur.a1) * inv; inc.a2 = (k.a2 - cur.a2) * inv;
    }
    void tick() noexcept
    {
        if (steps <= 0) return;
        if (--steps == 0) { cur = tgt; return; }
        cur.b0 += inc.b0; cur.b1 += inc.b1; cur.b2 += inc.b2; cur.a1 += inc.a1; cur.a2 += inc.a2;
    }
    float process (float xf, int c) noexcept
    {
        const double x = xf;
        const double y = cur.b0 * x + cur.b1 * x1[c] + cur.b2 * x2[c] - cur.a1 * y1[c] - cur.a2 * y2[c];
        x2[c] = x1[c]; x1[c] = x; y2[c] = y1[c]; y1[c] = y;
        return (float) y;
    }

private:
    BiquadCoeffs cur, tgt, inc;
    int steps = 0;
    double x1[2] {}, x2[2] {}, y1[2] {}, y2[2] {};
};

// passe-tout de Schroeder à retard entier (diffusion)
class Allpass
{
public:
    void prepare (int lengthSamples) { len = std::max (1, lengthSamples); buf.assign ((size_t) len, 0.0f); pos = 0; }
    void reset() noexcept { std::fill (buf.begin(), buf.end(), 0.0f); pos = 0; }
    float process (float x, float g) noexcept
    {
        const float d = buf[(size_t) pos];
        const float v = x + g * d;
        buf[(size_t) pos] = v;
        if (++pos >= len) pos = 0;
        return d - g * v;
    }
    int length() const noexcept { return len; }

private:
    std::vector<float> buf;
    int len = 1, pos = 0;
};

/*  Niveau de sortie ramené au niveau d'entrée (puissance moyenne lente), pour qu'un effet s'entende sans changer le
    volume. Le niveau mesuré est celui de la sortie déjà corrigée : la correction s'ajuste jusqu'à ce que les deux
    moyennes soient égales. Elle n'apprend QUE pendant le chant (niveau sur 10 ms au-dessus de -60 dBFS et à moins de
    20 dB du chant récent) : dans les silences et les fins de phrases, tout est gelé. Rien ne dérive pendant une pause,
    la reprise sort exactement au même niveau que la phrase d'avant. */
class LevelMatch
{
public:
    void prepare (double fs, double seconds = 1.0, double glide = 0.6)
    {
        c = coefFor (seconds, fs);
        step = (float) (32.0 / (glide * fs));
        fastC = coefFor (0.010, fs);
        refFall = (float) std::exp (-1.0 / (3.0 * fs));
        reset();
    }
    void reset() noexcept { pin = pout = fast = ref = 0.0f; g = 1.0f; count = 0; }
    // inPow / outPow : puissances instantanées (moyenne des canaux) ; renvoie le gain à appliquer à la sortie
    float next (float inPow, float outPow, float lo = 0.1f, float hi = 4.0f) noexcept
    {
        fast += fastC * (inPow - fast);
        ref = std::max (fast, ref * refFall);
        const bool singing = fast > 1.0e-6f && fast > 0.01f * ref;
        if (singing)
        {
            pin += c * (inPow - pin);
            pout += c * (outPow * g * g - pout);
        }
        if (++count >= 32)
        {
            count = 0;
            if (singing && pin > 1.0e-7f && pout > 1.0e-10f)
                g = juce::jlimit (lo, hi, g * std::pow (pin / pout, 0.5f * step));
        }
        return g;
    }
    float gain() const noexcept { return g; }

private:
    float c = 0.001f, step = 0.001f, pin = 0.0f, pout = 0.0f, g = 1.0f;
    float fastC = 0.01f, refFall = 0.9999f, fast = 0.0f, ref = 0.0f;
    int count = 0;
};

// l'effet s'efface sous la voix : réduction selon le niveau de la voix au-dessus de -40 dBFS (jusqu'à -24 dB).
// Le retour descend vite (20 ms) quand la voix arrive, mais REMONTE au plus à maxRise dB/s : réglé sous la vitesse
// d'extinction de la queue, il ne peut jamais faire regonfler une reverb ou un écho quand la voix s'arrête net (pause
// du DAW, fin de phrase) : la queue ne fait que s'éteindre, sans bond. Et il attend 200 ms que la voix soit vraiment
// arrêtée avant de commencer à remonter (pendant que la queue se remplit encore de la dernière syllabe).
class Ducker
{
public:
    void prepare (double sampleRate, float maxRiseDbPerSec = 20.0f)
    {
        fs = sampleRate;
        atk = coefFor (0.010, fs); rel = coefFor (0.300, fs); fall = coefFor (0.020, fs);
        holdLen = (int) (0.2 * fs);
        setMaxRise (maxRiseDbPerSec);
        reset();
    }
    void setMaxRise (float dbPerSec) noexcept { rise = std::max (0.5f, dbPerSec) / (float) fs; }
    void reset() noexcept { env = 0.0f; gDb = 0.0f; held = 0; }
    float next (float key, float depth) noexcept
    {
        const float a = std::abs (key);
        env += (a > env ? atk : rel) * (a - env);
        float targetDb = 0.0f;
        if (depth > 0.001f)
        {
            const float over = gainToDb (env + 1.0e-9f) + 40.0f;
            targetDb = -std::min (24.0f * depth, 0.6f * std::max (0.0f, over));
        }
        if (targetDb < gDb - 0.05f) { gDb += fall * (targetDb - gDb); held = 0; }
        else if (held < holdLen) ++held;
        else gDb = std::min (targetDb, gDb + rise);
        return dbToGain (gDb);
    }

private:
    double fs = 48000.0;
    float atk = 0.01f, rel = 0.001f, fall = 0.01f, rise = 0.0004f, env = 0.0f, gDb = 0.0f;
    int holdLen = 9600, held = 0;
};

// valeur lissée linéairement, prête à l'emploi (0..1 en général)
struct Smooth
{
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> v;
    void prepare (double fs, double seconds, float initial) { v.reset (fs, seconds); v.setCurrentAndTargetValue (initial); }
    void set (float x, bool snap) noexcept { if (snap) v.setCurrentAndTargetValue (x); else v.setTargetValue (x); }
    float next() noexcept { return v.getNextValue(); }
    float current() const noexcept { return v.getCurrentValue(); }
    float target() const noexcept { return v.getTargetValue(); }
    bool moving() const noexcept { return v.isSmoothing(); }
    // à zéro et immobile : l'effet ne coûte rien
    bool idle() const noexcept { return ! v.isSmoothing() && v.getTargetValue() <= 0.0f; }
};

// petit générateur pseudo-aléatoire (temps réel, sans allocation) et marche aléatoire lissée
struct Noise
{
    uint32_t s = 0x9e3779b9u;
    float next() noexcept { s = s * 1664525u + 1013904223u; return (float) (int32_t) s / 2147483648.0f; }   // -1..1
};

// mouvement lent et irrégulier (-1..1) : bruit filtré deux fois, recalé en amplitude
class Drift
{
public:
    void prepare (double fs, float hz, uint32_t seed)
    {
        noise.s = seed;
        k = coefFor (1.0 / (6.2831853 * std::max (0.01f, hz)), fs);
        a = b = 0.0f;
    }
    float next() noexcept
    {
        a += k * (noise.next() * 3.2f - a);
        b += k * (a - b);
        return juce::jlimit (-1.0f, 1.0f, b * 2.2f);
    }

private:
    Noise noise;
    float k = 0.001f, a = 0.0f, b = 0.0f;
};

// sigmoïde algébrique (coude progressif, harmoniques douces)
inline float asig (float z) noexcept { return z / std::sqrt (1.0f + z * z); }

// courbe d'un bouton : progressive au début, pleine à la fin
inline float taper (float x, float p) noexcept { return std::pow (std::max (x, 0.0f), p); }
} // namespace onirique::mini
