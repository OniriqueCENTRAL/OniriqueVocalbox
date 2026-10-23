#pragma once

#include "MiniDSP.h"

namespace onirique::mini
{
// décaleur de hauteur à deux têtes en fondu (fenêtres en sinus carré) : pour de petits écarts (quelques cents), le
// son reste naturel, sans grain audible
class MicroShift
{
public:
    void prepare (double fs, double windowSeconds)
    {
        window = (float) (windowSeconds * fs);
        line.prepare ((int) (0.12 * fs + window + 8));
        phase = 0.0f;
    }
    void reset() noexcept { line.reset(); phase = 0.0f; }
    // ratio > 1 : plus aigu. baseDelay : retard moyen (échantillons)
    float process (float x, float ratio, float baseDelay) noexcept
    {
        line.push (x);
        phase -= (ratio - 1.0f) / window;
        phase -= std::floor (phase);
        const float pB = phase + 0.5f >= 1.0f ? phase - 0.5f : phase + 0.5f;
        const float a = line.read (baseDelay + window * phase), b = line.read (baseDelay + window * pB);
        const float wa = std::sin (3.1415927f * phase), wb = std::sin (3.1415927f * pB);
        return a * wa * wa + b * wb * wb;
    }

private:
    ModDelay line;
    float window = 1900.0f, phase = 0.0f;
};

/*  DOUBLER : une vraie double voix, sans la chanter deux fois.
    Deux copies de la voix, l'une à gauche (+7 cents, 14 ms), l'autre à droite (-7 cents, 21 ms) : chacune dérive
    lentement en hauteur et en placement, de façon irrégulière, comme un chanteur qui refait la prise. Les copies
    sont un peu adoucies (moins de grave, sifflantes en retrait) pour se ranger derrière la voix principale, qui reste
    au centre, intacte. Le bouton monte le niveau des doubles (jusqu'à -2 dB). */
class Doubler
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        amt.prepare (fs, 0.05, amt.target());
        for (auto& s : shift) s.prepare (fs, 0.040);
        drift[0].prepare (fs, 0.35f, 0x1234567u); drift[1].prepare (fs, 0.27f, 0x7654321u);
        drift[2].prepare (fs, 0.22f, 0x2468aceu); drift[3].prepare (fs, 0.31f, 0x13579bdu);
        for (int v = 0; v < 2; ++v)
        {
            hp[v].setCoeffs (BiquadCoeffs::highpass (fs, 140.0, 0.707));
            soft[v].setCoeffs (BiquadCoeffs::highShelf (fs, std::min (6500.0, 0.42 * fs), 0.7, -4.0));
            lp[v].setCoeffs (BiquadCoeffs::lowpass (fs, std::min (12000.0, 0.45 * fs), 0.707));
        }
        reset();
    }
    void reset() noexcept { for (auto& s : shift) s.reset(); for (int v = 0; v < 2; ++v) { hp[v].reset(); soft[v].reset(); lp[v].reset(); } }
    void set (float amount01, bool snap) noexcept { amt.set (juce::jlimit (0.0f, 1.0f, amount01), snap); }

    void process (float* const* ch, int numCh, int n) noexcept
    {
        if (amt.idle()) { if (! wasIdle) { reset(); wasIdle = true; } return; }
        wasIdle = false;
        const float ms = 0.001f * (float) fs;
        for (int i = 0; i < n; ++i)
        {
            const float a = amt.next();
            const float g = 0.8f * std::pow (a, 0.8f);
            const float x = numCh > 1 ? 0.5f * (ch[0][i] + ch[1][i]) : ch[0][i];
            const float cents[2] { 7.0f + 3.0f * drift[0].next(), -7.0f + 3.0f * drift[1].next() };
            const float base[2] { (14.0f + 1.5f * drift[2].next()) * ms, (21.0f + 1.8f * drift[3].next()) * ms };
            float v[2];
            for (int k = 0; k < 2; ++k)
            {
                const float r = std::exp2 (cents[k] / 1200.0f);
                v[k] = lp[k].process (soft[k].process (hp[k].process (shift[k].process (x, r, base[k]), 0), 0), 0);
            }
            if (numCh > 1) { ch[0][i] += g * v[0]; ch[1][i] += g * v[1]; }
            else ch[0][i] += 0.5f * g * (v[0] + v[1]);
        }
    }

private:
    double fs = 48000.0;
    Smooth amt;
    MicroShift shift[2];
    Drift drift[4];
    Biquad hp[2], soft[2], lp[2];
    bool wasIdle = true;
};

/*  CHORUS : élargissement doux, sans vibrato. Trois voix par côté, retards modulés de 12 à 26 ms (lectures Hermite),
    oscillateurs décalés ; passe-haut 120 Hz sur les voix modulées (le grave reste net et centré). Le bouton dose la
    profondeur et le mélange ensemble. Volume tenu. */
class Chorus
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        amt.prepare (fs, 0.05, amt.target());
        for (auto& d : line) d.prepare ((int) (0.045 * fs));
        for (auto& h : hp) h.setCoeffs (BiquadCoeffs::highpass (fs, 120.0, 0.707));
        match.prepare (fs, 1.0, 0.6);
        reset();
    }
    void reset() noexcept { for (auto& d : line) d.reset(); for (auto& h : hp) h.reset(); match.reset(); phase = 0.0; }
    void set (float amount01, bool snap) noexcept { amt.set (juce::jlimit (0.0f, 1.0f, amount01), snap); }

    void process (float* const* ch, int numCh, int n) noexcept
    {
        if (amt.idle()) { if (! wasIdle) { reset(); wasIdle = true; } return; }
        wasIdle = false;
        static constexpr float baseL[3] { 12.0f, 17.0f, 23.0f }, baseR[3] { 14.0f, 19.5f, 26.0f };
        static constexpr float rateMul[3] { 1.0f, 1.13f, 0.91f };
        const double inc = 2.0 * juce::MathConstants<double>::pi * 0.55 / fs;
        const float msS = 0.001f * (float) fs;
        for (int i = 0; i < n; ++i)
        {
            const float a = amt.next();
            const float dMs = 1.4f + 2.8f * a, mix = 0.12f + 0.38f * a;
            const float on = std::min (1.0f, a * 20.0f);
            const float xl = ch[0][i], xr = numCh > 1 ? ch[1][i] : xl;
            line[0].push (xl);
            line[1].push (xr);
            phase += inc;
            float wl = 0.0f, wr = 0.0f;
            for (int v = 0; v < 3; ++v)
            {
                const double ph = phase * rateMul[v] + v * 2.0943951;
                const float ml = (float) (0.5 * (1.0 + std::sin (ph))), mr = (float) (0.5 * (1.0 + std::sin (ph + 1.5707963)));
                wl += line[0].read ((baseL[v] + dMs * ml) * msS);
                wr += line[1].read ((baseR[v] + dMs * mr) * msS);
            }
            wl = hp[0].process (wl * 0.58f, 0);
            wr = hp[1].process (wr * 0.58f, 0);
            float yl = xl + mix * (wl - xl), yr = xr + mix * (wr - xr);
            const float g = match.next (0.5f * (xl * xl + xr * xr), 0.5f * (yl * yl + yr * yr), 0.5f, 2.0f);
            yl *= g; yr *= g;
            if (numCh > 1) { ch[0][i] = xl + on * (yl - xl); ch[1][i] = xr + on * (yr - xr); }
            else ch[0][i] = xl + on * (0.5f * (yl + yr) - xl);
        }
        phase = std::fmod (phase, 2.0 * juce::MathConstants<double>::pi * 1000.0);
    }

private:
    double fs = 48000.0, phase = 0.0;
    Smooth amt;
    ModDelay line[2];
    Biquad hp[2];
    LevelMatch match;
    bool wasIdle = true;
};
} // namespace onirique::mini
