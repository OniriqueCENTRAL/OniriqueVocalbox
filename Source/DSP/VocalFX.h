#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <vector>
#include "Biquad.h"
#include "Common.h"

namespace onirique::fx
{
/*  FX : effets de voix de la page FX, en fin de chaîne (après la SATURATION et l'AUTO GAIN, avant OUTPUT), à la
    cadence de l'hôte, sans latence. Ordre fixe : CHORUS -> FLANGER -> DELAY -> REVERB (le delay part dans la reverb).

    - CHORUS : 3 voix par côté, retards modulés de 12 à 26 ms (lectures interpolées Hermite), LFO décalés : un
      élargissement doux, sans vibrato. Passe-haut 120 Hz sur les voix modulées.
    - FLANGER : retard très court (0.3 à 6 ms) modulé, réinjection réglable (+/-), côtés décalés de 90°.
      CHORUS et FLANGER : GAIN MATCH automatique, le niveau de sortie (RMS lent) est ramené à celui d'entrée : on
      entend l'effet, pas une différence de volume.
    - DELAY et REVERB sont des PISTES AUXILIAIRES : la voix reste à 100 %, leur retour (SEND) s'ajoute par-dessus.
    - DELAY : temps libre ou calé sur le tempo de l'hôte, ping-pong, filtres coupe-bas / coupe-haut dans la boucle,
      légère saturation dans la réinjection (les répétitions s'arrondissent comme sur bande), changement de temps
      glissé (aucun clic), DUCKING.
    - REVERB : réseau de 16 lignes à retards modulés (FDN, matrice de Hadamard), diffusion d'entrée par 4
      passe-tout, amortissement dépendant de la fréquence calculé pour le temps de DECAY (méthode de Jot : graves et
      aigus décroissent chacun à leur temps), réflexions précoces pour ROOM et HALL, octave réinjectée pour SHIMMER.
      Modes : ROOM, PLATE 1 (brillante, dense), PLATE 2 (plus sombre et plus large), HALL, SHIMMER. Coupe-bas et
      coupe-haut sur la queue, DUCKING.
    - DUCKING (DELAY, REVERB) : l'effet s'efface sous la voix et revient dans les silences (suiveur d'enveloppe de
      la voix, attaque 10 ms, relâchement 250 ms, jusqu'à -24 dB).
    Chaque effet s'allume et se coupe en fondu de 30 ms ; coupé, il ne coûte rien.
*/

//------------------------------------------------------------------------------
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

inline float coefFor (double seconds, double fs) noexcept { return (float) (1.0 - std::exp (-1.0 / (seconds * fs))); }

// niveau de sortie ramené au niveau d'entrée (RMS sur ~350 ms), compensation bornée à +/-6 dB et lissée
// Le niveau mesuré est celui de la sortie DÉJÀ corrigée : la correction s'ajuste jusqu'à ce que les deux moyennes
// soient égales (une simple division des niveaux se trompe quand l'effet fait varier le niveau au rythme de son LFO).
class GainMatch
{
public:
    void prepare (double fs)
    {
        c = coefFor (1.0, fs);                   // moyenne sur ~1 s : suit le niveau moyen, pas chaque passage du LFO
        step = (float) (32.0 / (0.6 * fs));    // la correction parcourt ~63 % de l'écart en 0.6 s (pas de pompage)
        pin = pout = 0.0f; g = 1.0f; count = 0;
    }
    float next (float inPow, float outPow) noexcept
    {
        pin += c * (inPow - pin);
        pout += c * (outPow * g * g - pout);
        if (++count >= 32)
        {
            count = 0;
            if (pin > 1.0e-8f && pout > 1.0e-10f)
                g = juce::jlimit (0.1f, 2.0f, g * std::pow (pin / pout, 0.5f * step));
        }
        return g;
    }

private:
    float c = 0.001f, step = 0.001f, pin = 0.0f, pout = 0.0f, g = 1.0f;
    int count = 0;
};

// l'effet s'efface sous la voix : réduction (dB) selon le niveau de la voix au-dessus de -40 dBFS, jusqu'à 24 dB
class Ducker
{
public:
    void prepare (double fs) { atk = coefFor (0.010, fs); rel = coefFor (0.250, fs); gs = coefFor (0.015, fs); env = 0.0f; g = 1.0f; }
    float next (float key, float depth) noexcept
    {
        const float a = std::abs (key);
        env += (a > env ? atk : rel) * (a - env);
        float target = 1.0f;
        if (depth > 0.001f)
        {
            const float over = gainToDb (env + 1.0e-9f) + 40.0f;
            target = dbToGain (-std::min (24.0f * depth, 0.6f * std::max (0.0f, over)));
        }
        g += gs * (target - g);
        return g;
    }

private:
    float atk = 0.01f, rel = 0.001f, gs = 0.01f, env = 0.0f, g = 1.0f;
};

// interrupteur d'effet en fondu
struct FxSwitch
{
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> on;
    bool wasIdle = true;
    void prepare (double fs, bool state) { on.reset (fs, 0.03); on.setCurrentAndTargetValue (state ? 1.0f : 0.0f); wasIdle = ! state; }
    void set (bool state) noexcept { on.setTargetValue (state ? 1.0f : 0.0f); }
    bool idle() const noexcept { return ! on.isSmoothing() && on.getTargetValue() <= 0.0f; }
};

//------------------------------------------------------------------------------
class Chorus
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        for (auto& d : line) d.prepare ((int) (0.045 * fs));
        for (auto& h : hp) h.setCoeffs (BiquadCoeffs::highpass (fs, 120.0, 0.707));
        gm.prepare (fs);
        sw.prepare (fs, enabled);
        phase = 0.0;
    }
    void set (bool on, float rateHz, float depth01, float mix01) noexcept
    {
        enabled = on; rate = rateHz; depth = depth01; mix = mix01;
        sw.set (on);
    }
    void process (float* const* ch, int numCh, int n) noexcept
    {
        if (sw.idle()) { sw.wasIdle = true; return; }
        if (sw.wasIdle) { for (auto& d : line) d.reset(); for (auto& h : hp) h.reset(); sw.wasIdle = false; }
        static constexpr float baseL[3] { 12.0f, 17.0f, 23.0f }, baseR[3] { 14.0f, 19.5f, 26.0f };
        static constexpr float rateMul[3] { 1.0f, 1.13f, 0.91f };
        const double inc = 2.0 * juce::MathConstants<double>::pi * rate / fs;
        const float dMs = 4.0f * depth;
        for (int i = 0; i < n; ++i)
        {
            const float xl = ch[0][i], xr = numCh > 1 ? ch[1][i] : xl;
            line[0].push (xl);
            line[1].push (xr);
            phase += inc;
            float wl = 0.0f, wr = 0.0f;
            for (int v = 0; v < 3; ++v)
            {
                const double ph = phase * rateMul[v] + v * 2.0943951;
                const float ml = (float) (0.5 * (1.0 + std::sin (ph))), mr = (float) (0.5 * (1.0 + std::sin (ph + 1.5707963)));
                wl += line[0].read ((baseL[v] + dMs * ml) * 0.001f * (float) fs);
                wr += line[1].read ((baseR[v] + dMs * mr) * 0.001f * (float) fs);
            }
            wl = hp[0].process (wl * 0.58f, 0);
            wr = hp[1].process (wr * 0.58f, 0);
            float yl = xl + mix * (wl - xl), yr = xr + mix * (wr - xr);
            const float g = gm.next (0.5f * (xl * xl + xr * xr), 0.5f * (yl * yl + yr * yr));
            yl *= g; yr *= g;
            const float s = sw.on.getNextValue();
            ch[0][i] = xl + s * (yl - xl);
            if (numCh > 1) ch[1][i] = xr + s * (yr - xr);
            else ch[0][i] = xl + s * (0.5f * (yl + yr) - xl);
        }
        phase = std::fmod (phase, 2.0 * juce::MathConstants<double>::pi * 1000.0);
    }

private:
    double fs = 48000.0, phase = 0.0;
    bool enabled = false;
    float rate = 0.6f, depth = 0.5f, mix = 0.35f;
    ModDelay line[2];
    Biquad hp[2];
    GainMatch gm;
    FxSwitch sw;
};

//------------------------------------------------------------------------------
class Flanger
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        for (auto& d : line) d.prepare ((int) (0.015 * fs));
        gm.prepare (fs);
        sw.prepare (fs, enabled);
        phase = 0.0;
        fbState[0] = fbState[1] = 0.0f;
    }
    void set (bool on, float rateHz, float depth01, float feedback, float mix01) noexcept
    {
        enabled = on; rate = rateHz; depth = depth01; fb = juce::jlimit (-0.95f, 0.95f, feedback); mix = mix01;
        sw.set (on);
    }
    void process (float* const* ch, int numCh, int n) noexcept
    {
        if (sw.idle()) { sw.wasIdle = true; return; }
        if (sw.wasIdle) { for (auto& d : line) d.reset(); fbState[0] = fbState[1] = 0.0f; sw.wasIdle = false; }
        const double inc = 2.0 * juce::MathConstants<double>::pi * rate / fs;
        const float dMs = 0.3f + 5.7f * depth;
        for (int i = 0; i < n; ++i)
        {
            const float x[2] { ch[0][i], numCh > 1 ? ch[1][i] : ch[0][i] };
            phase += inc;
            float y[2];
            for (int c = 0; c < 2; ++c)
            {
                const float m = (float) (0.5 * (1.0 - std::cos (phase + (c == 1 ? 1.5707963 : 0.0))));
                const float d = (0.3f + (dMs - 0.3f) * m) * 0.001f * (float) fs;
                const float r = line[c].read (std::max (2.0f, d));
                line[c].push (x[c] + fb * std::tanh (r));
                y[c] = x[c] * (1.0f - 0.5f * mix) + r * (0.5f * mix);
            }
            const float g = gm.next (0.5f * (x[0] * x[0] + x[1] * x[1]), 0.5f * (y[0] * y[0] + y[1] * y[1]));
            const float s = sw.on.getNextValue();
            if (numCh > 1)
            {
                ch[0][i] = x[0] + s * (y[0] * g - x[0]);
                ch[1][i] = x[1] + s * (y[1] * g - x[1]);
            }
            else ch[0][i] = x[0] + s * (0.5f * (y[0] + y[1]) * g - x[0]);
        }
        phase = std::fmod (phase, 2.0 * juce::MathConstants<double>::pi * 1000.0);
    }

private:
    double fs = 48000.0, phase = 0.0;
    bool enabled = false;
    float rate = 0.25f, depth = 0.6f, fb = 0.4f, mix = 0.5f;
    float fbState[2] {};
    ModDelay line[2];
    GainMatch gm;
    FxSwitch sw;
};

//------------------------------------------------------------------------------
class StereoDelay
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        for (auto& d : line) d.prepare ((int) (4.1 * fs));
        timeS.reset (fs, 0.25);
        timeS.setCurrentAndTargetValue (std::max (0.002f, time));
        for (int c = 0; c < 2; ++c) { hp[c].reset(); lp[c].reset(); }
        updateFilters();
        duck.prepare (fs);
        sw.prepare (fs, enabled);
    }
    void set (bool on, float timeSec, float feedback, float mix01, bool pingPong, float lowCutHz, float highCutHz, float ducking) noexcept
    {
        enabled = on;
        timeS.setTargetValue (juce::jlimit (0.002f, 4.0f, timeSec));
        fb = juce::jlimit (0.0f, 0.95f, feedback);
        mix = mix01; ping = pingPong; duckDepth = ducking;
        if (lowCutHz != lowCut || highCutHz != highCut) { lowCut = lowCutHz; highCut = highCutHz; updateFilters(); }
        sw.set (on);
    }
    // piste auxiliaire : la voix (ch) entre dans le delay, son retour (au niveau SEND, avec le ducking) s'ajoute dans wet
    void processSend (const float* const* ch, float* const* wet, int numCh, int n) noexcept
    {
        if (sw.idle()) { sw.wasIdle = true; return; }
        if (sw.wasIdle) { for (auto& d : line) d.reset(); for (int c = 0; c < 2; ++c) { hp[c].reset(); lp[c].reset(); } sw.wasIdle = false; }
        const float sendG = mix;   // niveau du retour : la voix, elle, reste à 100 %
        for (int i = 0; i < n; ++i)
        {
            const float xl = ch[0][i], xr = numCh > 1 ? ch[1][i] : xl;
            const float d = timeS.getNextValue() * (float) fs;
            float r[2] { line[0].read (d), line[1].read (d) };
            for (int c = 0; c < 2; ++c) r[c] = lp[c].process (hp[c].process (r[c], 0), 0);
            auto sat = [] (float v) { return v / (1.0f + 0.25f * std::abs (v)); };   // répétitions arrondies (bande)
            if (ping)
            {
                line[0].push (0.5f * (xl + xr) + fb * sat (r[1]));
                line[1].push (fb * sat (r[0]));
            }
            else
            {
                line[0].push (xl + fb * sat (r[0]));
                line[1].push (xr + fb * sat (r[1]));
            }
            const float g = duck.next (0.5f * (xl + xr), duckDepth) * sendG * sw.on.getNextValue();
            if (numCh > 1) { wet[0][i] += r[0] * g; wet[1][i] += r[1] * g; }
            else wet[0][i] += 0.5f * (r[0] + r[1]) * g;
        }
    }

private:
    void updateFilters() noexcept
    {
        for (int c = 0; c < 2; ++c)
        {
            hp[c].setCoeffs (BiquadCoeffs::highpass (fs, juce::jlimit (20.0f, 2000.0f, lowCut), 0.707));
            lp[c].setCoeffs (BiquadCoeffs::lowpass (fs, std::min ((double) juce::jlimit (1000.0f, 20000.0f, highCut), 0.45 * fs), 0.707));
        }
    }

    double fs = 48000.0;
    bool enabled = false, ping = false;
    float time = 0.375f, fb = 0.35f, mix = 0.25f, lowCut = 200.0f, highCut = 6000.0f, duckDepth = 0.0f;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> timeS { 0.375f };
    ModDelay line[2];
    Biquad hp[2], lp[2];
    Ducker duck;
    FxSwitch sw;
};

//------------------------------------------------------------------------------
class VocalReverb
{
public:
    static constexpr int kLines = 16;
    enum Mode { room = 0, plate1, plate2, hall, shimmer, numModes };

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        const double r = fs / 48000.0;
        for (auto& l : lines) l.prepare ((int) (0.25 * fs));
        preDelay.prepare ((int) (0.26 * fs));
        static constexpr int apLen[4] { 142, 107, 379, 277 };      // passe-tout de diffusion (à 29 761 Hz)
        for (int a = 0; a < 4; ++a) { ap[a].prepare ((int) (apLen[a] * fs / 29761.0) + 4); apD[a] = (int) (apLen[a] * fs / 29761.0); }
        erLine.prepare ((int) (0.12 * fs));
        shimBuf.prepare ((int) (0.2 * fs));
        for (int c = 0; c < 2; ++c) { lc[c].reset(); hc[c].reset(); }
        erLp.setCoeffs (BiquadCoeffs::lowpass (fs, 6500.0, 0.707));
        shimLp.setCoeffs (BiquadCoeffs::lowpass (fs, std::min (7000.0, 0.45 * fs), 0.707));
        shimHp.setCoeffs (BiquadCoeffs::highpass (fs, 300.0, 0.707));
        for (int k = 0; k < kLines; ++k) { lpState[k] = 0.0f; lfo[k] = (double) k * 0.73; }
        duck.prepare (fs);
        sw.prepare (fs, enabled);
        shimPhase = 0.0f;
        juce::ignoreUnused (r);
        dirty = true;
        update();
    }

    struct Params
    {
        bool on = false;
        int mode = plate1;
        float preDelayMs = 20.0f, decay = 2.2f, size = 0.6f, diffusion = 0.75f, modRate = 0.8f, modDepth = 0.35f;
        float mix = 0.2f, lowCut = 180.0f, highCut = 9000.0f, ducking = 0.0f;
    };

    void set (const Params& p) noexcept
    {
        if (p.mode != prm.mode || p.decay != prm.decay || p.size != prm.size || p.diffusion != prm.diffusion
            || p.lowCut != prm.lowCut || p.highCut != prm.highCut) dirty = true;
        prm = p;
        enabled = p.on;
        sw.set (p.on);
        if (dirty) update();
    }

    // niveau de la queue (pour l'interface)
    float getTailLevel() const noexcept { return tailMeter.load (std::memory_order_relaxed); }

    // piste auxiliaire : la voix (ch) entre dans la reverb, son retour (au niveau SEND, avec le ducking) s'ajoute dans wet
    void processSend (const float* const* ch, float* const* wet, int numCh, int n) noexcept
    {
        if (sw.idle()) { sw.wasIdle = true; tailMeter.store (0.0f, std::memory_order_relaxed); return; }
        if (sw.wasIdle) { clearState(); sw.wasIdle = false; }
        const float sendG = prm.mix;   // niveau du retour : la voix, elle, reste à 100 %
        const float pre = std::max (2.0f, prm.preDelayMs * 0.001f * (float) fs);
        const float apG[4] { 0.75f * diffAmt, 0.75f * diffAmt, 0.625f * diffAmt, 0.625f * diffAmt };
        const double lfoInc = 2.0 * juce::MathConstants<double>::pi * prm.modRate / fs;
        const float modSamples = (0.0003f + 0.0012f * prm.modDepth) * (float) fs;
        float peak = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            const float xl = ch[0][i], xr = numCh > 1 ? ch[1][i] : xl;
            const float in = 0.5f * (xl + xr);

            preDelay.push (in);
            float x = preDelay.read (pre);

            // réflexions précoces (ROOM, HALL)
            erLine.push (x);
            float erL = 0.0f, erR = 0.0f;
            if (erGain > 0.0f)
            {
                for (int t = 0; t < kTaps; ++t)
                {
                    const float v = erLine.tap (erTap[t]) * erAmp[t];
                    (t & 1 ? erR : erL) += v;
                }
                erL = erLp.process (erL, 0) * erGain;
                erR = erLp.process (erR, 1) * erGain;
            }

            // diffusion d'entrée : 4 passe-tout en série
            for (int a = 0; a < 4; ++a)
            {
                const float d = ap[a].tap (apD[a] - 1);
                const float v = x + apG[a] * d;
                ap[a].push (v);
                x = d - apG[a] * v;
            }

            // octave réinjectée (SHIMMER) : décaleur de hauteur à deux têtes, fenêtres en sinus
            float shim = 0.0f;
            if (shimmerAmt > 0.0f)
            {
                shimPhase += 1.0f / shimWin;
                if (shimPhase >= 1.0f) shimPhase -= 1.0f;
                const float pA = shimPhase, pB = std::fmod (shimPhase + 0.5f, 1.0f);
                const float a = shimBuf.read (2.0f + (1.0f - pA) * shimWin), b = shimBuf.read (2.0f + (1.0f - pB) * shimWin);
                shim = a * std::sin (pA * 3.1415927f) + b * std::sin (pB * 3.1415927f);
                shim = shimLp.process (shimHp.process (shim, 0), 0) * shimmerAmt;
            }

            // réseau de 16 lignes : lecture modulée, amortissement (graves / aigus), Hadamard, réinjection
            float y[kLines];
            for (int k = 0; k < kLines; ++k)
            {
                lfo[k] += lfoInc * lfoMul[k];
                const float d = lineLen[k] + modSamples * (float) std::sin (lfo[k]);
                const float v = lines[k].read (d);
                lpState[k] = absB[k] * v + absA[k] * lpState[k];
                y[k] = lpState[k];
            }
            float h[kLines];
            std::copy (y, y + kLines, h);
            for (int len = 1; len < kLines; len <<= 1)            // Hadamard rapide (normalisée par 1/4)
                for (int s = 0; s < kLines; s += len << 1)
                    for (int j = s; j < s + len; ++j)
                    {
                        const float a = h[j], b = h[j + len];
                        h[j] = a + b; h[j + len] = a - b;
                    }
            const float inj = x + shim;
            for (int k = 0; k < kLines; ++k)
                lines[k].push (0.25f * h[k] + inj * inSign[k] * 0.35f);

            float wl = 0.0f, wr = 0.0f;
            for (int k = 0; k < kLines; ++k) { wl += y[k] * outL[k]; wr += y[k] * outR[k]; }
            wl *= outGain; wr *= outGain;
            if (shimmerAmt > 0.0f) shimBuf.push (0.5f * (wl + wr));

            wl = hc[0].process (lc[0].process (wl + erL, 0), 0);
            wr = hc[1].process (lc[1].process (wr + erR, 0), 0);
            peak = std::max (peak, std::abs (wl) + std::abs (wr));

            const float g = duck.next (in, prm.ducking) * sendG * sw.on.getNextValue();
            if (numCh > 1) { wet[0][i] += wl * g; wet[1][i] += wr * g; }
            else wet[0][i] += 0.5f * (wl + wr) * g;
        }
        tailMeter.store (peak, std::memory_order_relaxed);
        for (auto& l : lfo) l = std::fmod (l, 2.0 * juce::MathConstants<double>::pi);
    }

private:
    static constexpr int kTaps = 12;

    struct Character { float sizeMul, erGain, erSize, hfRatio, diff, shimmer, bright; };
    static Character character (int mode) noexcept
    {
        switch (mode)
        {
            case room:    return { 0.45f, 0.55f, 0.55f, 0.45f, 0.80f, 0.0f, 0.0f };
            case plate2:  return { 0.95f, 0.00f, 0.00f, 0.50f, 1.00f, 0.0f, -0.3f };
            case hall:    return { 1.45f, 0.30f, 1.20f, 0.55f, 0.90f, 0.0f, 0.0f };
            case shimmer: return { 1.25f, 0.00f, 0.00f, 0.65f, 1.00f, 0.30f, 0.1f };
            default:      return { 0.75f, 0.00f, 0.00f, 0.75f, 1.00f, 0.0f, 0.3f };   // PLATE 1
        }
    }

    void update() noexcept
    {
        dirty = false;
        const auto ch = character (prm.mode);
        static constexpr float baseMs[kLines] { 29.7f, 37.1f, 41.1f, 43.7f, 47.9f, 53.3f, 59.0f, 61.4f,
                                                67.3f, 71.1f, 73.9f, 79.6f, 83.5f, 89.2f, 97.3f, 101.6f };
        const float sizeScale = ch.sizeMul * (0.45f + 1.1f * prm.size);
        const float rt = juce::jlimit (0.15f, 30.0f, prm.decay);
        const float rtHi = std::max (0.08f, rt * ch.hfRatio);
        float gAvg = 0.0f;
        for (int k = 0; k < kLines; ++k)
        {
            lineLen[k] = std::min ((float) (0.24 * fs), baseMs[k] * sizeScale * 0.001f * (float) fs);
            // gain par passage pour que les graves (DC) et les aigus (Nyquist) décroissent de 60 dB en rt / rtHi
            const float gdc = std::pow (10.0f, -3.0f * lineLen[k] / (rt * (float) fs));
            const float gny = std::pow (10.0f, -3.0f * lineLen[k] / (rtHi * (float) fs));
            const float a = (gdc - gny) / (gdc + gny);
            absA[k] = a;
            absB[k] = gdc * (1.0f - a);
            gAvg += gdc;
            lfoMul[k] = 0.7f + 0.6f * (float) ((k * 7) % 11) / 10.0f;
            inSign[k] = (k * 5 + 3) % 4 < 2 ? 1.0f : -1.0f;
            // sorties gauche / droite : deux rangées de signes décorrélées
            outL[k] = ((k & 1) ? -1.0f : 1.0f) * 0.25f;
            outR[k] = (((k >> 1) ^ (k >> 2)) & 1 ? -1.0f : 1.0f) * 0.25f;
        }
        gAvg /= (float) kLines;
        outGain = 1.6f * std::pow (std::max (0.02f, 1.0f - gAvg * gAvg), 0.35f) * (1.0f + 0.25f * ch.bright);
        diffAmt = juce::jlimit (0.0f, 1.0f, prm.diffusion * ch.diff);
        erGain = ch.erGain;
        shimmerAmt = ch.shimmer;
        shimWin = (float) (0.06 * fs);
        static constexpr float erMs[kTaps] { 7.1f, 11.3f, 13.7f, 17.2f, 19.9f, 23.4f, 29.1f, 31.3f, 37.7f, 41.2f, 43.9f, 47.8f };
        for (int t = 0; t < kTaps; ++t)
        {
            erTap[t] = std::max (1, (int) (erMs[t] * std::max (0.2f, ch.erSize * (0.6f + 0.8f * prm.size)) * 0.001f * (float) fs));
            erTap[t] = std::min (erTap[t], (int) (0.115 * fs));
            erAmp[t] = 0.55f * std::pow (0.82f, (float) t);
        }
        for (int c = 0; c < 2; ++c)
        {
            lc[c].setCoeffs (BiquadCoeffs::highpass (fs, juce::jlimit (20.0f, 1000.0f, prm.lowCut), 0.707));
            hc[c].setCoeffs (BiquadCoeffs::lowpass (fs, std::min ((double) juce::jlimit (1000.0f, 20000.0f, prm.highCut), 0.45 * fs), 0.707));
        }
    }

    void clearState() noexcept
    {
        for (auto& l : lines) l.reset();
        preDelay.reset(); erLine.reset(); shimBuf.reset();
        for (auto& a : ap) a.reset();
        for (auto& s : lpState) s = 0.0f;
        for (int c = 0; c < 2; ++c) { lc[c].reset(); hc[c].reset(); erLp.reset(); }
        shimLp.reset(); shimHp.reset();
    }

    double fs = 48000.0;
    bool enabled = false, dirty = true;
    Params prm;
    ModDelay lines[kLines], preDelay, ap[4], erLine, shimBuf;
    int apD[4] {};
    float lineLen[kLines] {}, absA[kLines] {}, absB[kLines] {}, lpState[kLines] {}, lfoMul[kLines] {}, inSign[kLines] {};
    float outL[kLines] {}, outR[kLines] {};
    double lfo[kLines] {};
    float outGain = 1.0f, diffAmt = 0.75f, erGain = 0.0f, shimmerAmt = 0.0f, shimWin = 2880.0f, shimPhase = 0.0f;
    int erTap[kTaps] {};
    float erAmp[kTaps] {};
    Biquad lc[2], hc[2], erLp, shimLp, shimHp;
    Ducker duck;
    FxSwitch sw;
    std::atomic<float> tailMeter { 0.0f };
};

//------------------------------------------------------------------------------
// la page FX complète : CHORUS -> FLANGER -> DELAY -> REVERB
class FxRack
{
public:
    struct Params
    {
        bool chorusOn = false; float chorusRate = 0.6f, chorusDepth = 0.5f, chorusMix = 0.35f;
        bool flangerOn = false; float flangerRate = 0.25f, flangerDepth = 0.6f, flangerFeedback = 0.4f, flangerMix = 0.5f;
        bool delayOn = false; float delayTimeSec = 0.375f, delayFeedback = 0.35f, delayMix = 0.25f, delayLowCut = 200.0f, delayHighCut = 6000.0f, delayDucking = 0.0f;
        bool delayPingPong = false;
        VocalReverb::Params reverb;
    };

    void prepare (double fs)
    {
        chorus.prepare (fs); flanger.prepare (fs); delay.prepare (fs); reverb.prepare (fs);
        for (auto& w : wetBuf) w.assign (4096, 0.0f);
    }
    void set (const Params& p) noexcept
    {
        chorus.set (p.chorusOn, p.chorusRate, p.chorusDepth, p.chorusMix);
        flanger.set (p.flangerOn, p.flangerRate, p.flangerDepth, p.flangerFeedback, p.flangerMix);
        delay.set (p.delayOn, p.delayTimeSec, p.delayFeedback, p.delayMix, p.delayPingPong, p.delayLowCut, p.delayHighCut, p.delayDucking);
        reverb.set (p.reverb);
    }
    void process (float* const* ch, int numCh, int n) noexcept
    {
        chorus.process (ch, numCh, n);
        flanger.process (ch, numCh, n);
        // DELAY et REVERB : deux pistes auxiliaires en parallèle, nourries par la voix (après chorus / flanger) ; leurs
        // retours s'ajoutent à la voix, qui reste à 100 % : monter la reverb ne noie jamais la voix, et le DUCKING
        // creuse le retour sous la voix
        for (int pos = 0; pos < n; pos += 4096)
        {
            const int m = std::min (4096, n - pos);
            float* in[2] { ch[0] + pos, numCh > 1 ? ch[1] + pos : ch[0] + pos };
            float* wet[2] { wetBuf[0].data(), wetBuf[1].data() };
            std::fill (wet[0], wet[0] + m, 0.0f);
            std::fill (wet[1], wet[1] + m, 0.0f);
            delay.processSend (in, wet, numCh, m);
            reverb.processSend (in, wet, numCh, m);
            for (int c = 0; c < numCh; ++c)
                for (int i = 0; i < m; ++i) in[c][i] += wet[c][i];
        }
    }
    float getReverbTail() const noexcept { return reverb.getTailLevel(); }

private:
    Chorus chorus;
    Flanger flanger;
    StereoDelay delay;
    VocalReverb reverb;
    std::vector<float> wetBuf[2];   // retours des deux pistes auxiliaires
};
} // namespace onirique::fx
