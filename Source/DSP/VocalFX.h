#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <vector>
#include "Biquad.h"
#include "Common.h"
#include "VoiceColor.h"
#include "VoiceReverb.h"
#include "VoiceWide.h"

namespace onirique::fx
{
/*  FX : effets de voix de la page FX, en fin de chaîne (après la SATURATION et l'AUTO GAIN, avant OUTPUT), à la
    cadence de l'hôte, sans latence. Ordre fixe : PHONE -> DOUBLER -> CHORUS -> FLANGER -> DELAY -> REVERB.

    PHONE, DOUBLER, la boucle du DELAY et la REVERB viennent de VOCAL MINI (DSP/VoiceColor.h, VoiceWide.h,
    VoiceReverb.h) :
    - PHONE : de la voix pleine au téléphone en un bouton (bande qui se resserre, résonance de petit haut-parleur,
      écrêtage doux), volume tenu.
    - DOUBLER : deux copies de la voix (+/-7 cents, 14 et 21 ms) qui dérivent lentement comme une prise rejouée, une à
      gauche et une à droite ; la voix principale reste au centre, intacte.
    - DELAY : la bande flotte à peine (+/-0.15 ms), et le DUCKING ne fait jamais regonfler l'écho quand la voix s'arrête.
    - REVERB : le moteur de VOCAL MINI (celui d'ONIRIQUE VERB : passe-tout dans la boucle, queue dense dès les premières
      millisecondes, modulation bornée en hauteur, sorties décorrélées), avec CLEAN (chaque phrase repart propre) et
      WIDTH. Les anciennes descriptions ci-dessous de la REVERB valent pour ses réglages, pas pour son moteur.

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
            const float s = sCurve (sw.on.getNextValue());
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
            const float s = sCurve (sw.on.getNextValue());
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
        wow.prepare (fs, 0.6f, 0xa5a5a5u);
        sw.prepare (fs, enabled);
    }
    void set (bool on, float timeSec, float feedback, float mix01, bool pingPong, float lowCutHz, float highCutHz, float ducking) noexcept
    {
        enabled = on;
        timeS.setTargetValue (juce::jlimit (0.002f, 4.0f, timeSec));
        fb = juce::jlimit (0.0f, 0.95f, feedback);
        mix = mix01; ping = pingPong; duckDepth = ducking;
        // le retour remonte moins vite que les répétitions ne s'éteignent : à l'arrêt de la voix, l'écho ne regonfle pas
        const float decayDbPerSec = -20.0f * std::log10 (std::max (fb, 0.001f)) / std::max (0.002f, timeS.getTargetValue());
        duck.setMaxRise (juce::jlimit (1.0f, 40.0f, 0.7f * decayDbPerSec));
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
            const float d = timeS.getNextValue() * (float) fs + 0.00015f * (float) fs * wow.next();   // la bande flotte à peine
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
            const float g = duck.next (0.5f * (xl + xr), duckDepth) * sendG * sCurve (sw.on.getNextValue());
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
    mini::Ducker duck;
    mini::Drift wow;
    FxSwitch sw;
};

//------------------------------------------------------------------------------
// REVERB : le moteur de VOCAL MINI (mini::VoiceReverb), avec les réglages de la page FX de VOCALBOX
class VocalReverb
{
public:
    using Mode = mini::VoiceReverb::Mode;

    struct Params
    {
        bool on = false;
        int mode = mini::VoiceReverb::plate1;
        float preDelayMs = 20.0f, decay = 2.2f, size = 0.6f, diffusion = 0.75f, modRate = 0.8f, modDepth = 0.35f;
        float mix = 0.2f, lowCut = 180.0f, highCut = 9000.0f, ducking = 0.0f;
        float width = 1.0f, clean = 0.3f;
    };

    void prepare (double sampleRate) { engine.prepare (sampleRate); engine.set (toEngine (prm), true); }
    void set (const Params& p) noexcept { prm = p; engine.set (toEngine (p), false); }
    float getTailLevel() const noexcept { return engine.getTail(); }
    const mini::VoiceReverb& getEngine() const noexcept { return engine; }   // son historique (voix, queue) pour l'affichage
    // piste auxiliaire : la voix (ch) entre dans la reverb, son retour s'ajoute dans wet (la voix reste à 100 %)
    void processSend (const float* const* ch, float* const* wet, int numCh, int n) noexcept
    {
        engine.process (ch, wet, numCh, n, 120.0, -1.0);
    }

private:
    static mini::VoiceReverb::Params toEngine (const Params& p) noexcept
    {
        mini::VoiceReverb::Params q;
        q.mode = p.mode;
        q.send = p.on ? p.mix : 0.0f;   // coupée : l'envoi se ferme en fondu, la queue déjà partie s'éteint
        q.decay = p.decay; q.preDelayMs = p.preDelayMs; q.size = p.size; q.diffusion = p.diffusion;
        q.modRate = p.modRate; q.modDepth = p.modDepth; q.lowCut = p.lowCut; q.highCut = p.highCut;
        q.width = p.width; q.ducking = p.ducking; q.clean = p.clean; q.pulse = 0.0f;
        return q;
    }

    Params prm;
    mini::VoiceReverb engine;
};

//------------------------------------------------------------------------------
// la page FX complète : PHONE -> DOUBLER -> CHORUS -> FLANGER -> DELAY -> REVERB
class FxRack
{
public:
    struct Params
    {
        bool phoneOn = false; float phoneAmount = 0.5f;
        bool doublerOn = false; float doublerAmount = 0.5f;
        bool chorusOn = false; float chorusRate = 0.6f, chorusDepth = 0.5f, chorusMix = 0.35f;
        bool flangerOn = false; float flangerRate = 0.25f, flangerDepth = 0.6f, flangerFeedback = 0.4f, flangerMix = 0.5f;
        bool delayOn = false; float delayTimeSec = 0.375f, delayFeedback = 0.35f, delayMix = 0.25f, delayLowCut = 200.0f, delayHighCut = 6000.0f, delayDucking = 0.0f;
        bool delayPingPong = false;
        VocalReverb::Params reverb;
    };

    void prepare (double fs)
    {
        phone.prepare (fs); doubler.prepare (fs);
        chorus.prepare (fs); flanger.prepare (fs); delay.prepare (fs); reverb.prepare (fs);
        for (auto& w : wetBuf) w.assign (4096, 0.0f);
    }
    void set (const Params& p) noexcept
    {
        // coupés : le bouton redescend à zéro en fondu (50 ms), puis l'effet ne coûte plus rien
        phone.set (p.phoneOn ? p.phoneAmount : 0.0f, false);
        doubler.set (p.doublerOn ? p.doublerAmount : 0.0f, false);
        chorus.set (p.chorusOn, p.chorusRate, p.chorusDepth, p.chorusMix);
        flanger.set (p.flangerOn, p.flangerRate, p.flangerDepth, p.flangerFeedback, p.flangerMix);
        delay.set (p.delayOn, p.delayTimeSec, p.delayFeedback, p.delayMix, p.delayPingPong, p.delayLowCut, p.delayHighCut, p.delayDucking);
        reverb.set (p.reverb);
    }
    void process (float* const* ch, int numCh, int n) noexcept
    {
        phone.process (ch, numCh, n);
        doubler.process (ch, numCh, n);
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
    const mini::VoiceReverb& getReverbEngine() const noexcept { return reverb.getEngine(); }

private:
    mini::Phone phone;
    mini::Doubler doubler;
    Chorus chorus;
    Flanger flanger;
    StereoDelay delay;
    VocalReverb reverb;
    std::vector<float> wetBuf[2];   // retours des deux pistes auxiliaires
};
} // namespace onirique::fx
