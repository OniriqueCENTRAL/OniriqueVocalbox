#pragma once

#include "MiniDSP.h"

namespace onirique::mini
{
/*  DRIVE : la chaleur d'un préampli classe A à transformateurs (le moteur ANALOG de VOCALBOX, sans son
    suréchantillonnage propre : il tourne dans la chaîne x2 de VOCAL MINI).

    - Classe A : sigmoïde algébrique à léger biais (harmonique 2 discrète, l'harmonique 3 monte quand on pousse).
    - En LIVE (cadence du projet, sans suréchantillonnage), saturation à BANDE DIVISÉE : seule la voix sous 7 kHz
      passe dans l'étage (filtre du 4e ordre), le haut est ajouté intact à côté (complément exact : à faible drive
      le signal ressort identique). Les harmoniques restent sous Nyquist, rien ne se replie dans l'aigu, et l'air
      de la voix n'est jamais touché. En MIX, la chaîne x2 laisse toute la place aux harmoniques : pas de division.
    - Transformateur : seule la partie grave (< 180 Hz) sature, symétriquement : les graves s'épaississent et se
      tassent, le médium et l'aigu restent ouverts.
    - Couleur dosée par le bouton : un peu de rondeur sous 110 Hz, de présence vers 3 kHz, un haut arrondi.
    - Transitoires préservés : les attaques des consonnes passent un peu moins par l'étage.
    - Volume tenu : la sortie est ramenée au niveau de l'entrée (moyenne lente). On entend les harmoniques et la
      couleur arriver, pas le volume monter.
*/
class Warmth
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        drive.prepare (fs, 0.08, drive.target());
        match.prepare (fs, 0.8, 0.5);
        xfCoef = (float) (1.0 - std::exp (-6.283185307179586 * 180.0 / fs));
        lowCol.setCoeffs  (BiquadCoeffs::lowShelf  (fs, 110.0, 0.7, 1.5));
        presCol.setCoeffs (BiquadCoeffs::peaking   (fs, 3000.0, 0.9, 0.8));
        topCol.setCoeffs  (BiquadCoeffs::highShelf (fs, std::min (15000.0, 0.42 * fs), 0.7, -1.2));
        dc.setCoeffs      (BiquadCoeffs::highpass  (fs, 8.0, 0.707));
        fAtk = coefFor (0.00005, fs); fRel = coefFor (0.002, fs); sAtk = coefFor (0.003, fs); sRel = coefFor (0.030, fs);
        tAtk = coefFor (0.00004, fs); tRel = coefFor (0.012, fs);
        refC = coefFor (0.3, fs);
        fastPowC = coefFor (0.010, fs);
        split = fs < 60000.0;
        splitA.setCoeffs (BiquadCoeffs::lowpass (fs, 7000.0, 0.5412));   // Butterworth 4e ordre
        splitB.setCoeffs (BiquadCoeffs::lowpass (fs, 7000.0, 1.3066));
        reset();
    }

    void reset() noexcept
    {
        for (auto* b : { &lowCol, &presCol, &topCol, &dc, &splitA, &splitB }) b->reset();
        xfLp[0] = xfLp[1] = 0.0f;
        envF = envS = trans = 0.0f;
        refPow = 0.06f;
        fastPow = 0.0f;
        match.reset();
    }

    void set (float amount01, bool snap) noexcept { drive.set (juce::jlimit (0.0f, 1.0f, amount01), snap); }

    void process (float* const* ch, int numCh, int n) noexcept
    {
        if (drive.idle()) { if (! wasIdle) { reset(); wasIdle = true; } return; }
        wasIdle = false;
        const float inv = 1.0f / (float) numCh;
        for (int i = 0; i < n; ++i)
        {
            const float d = drive.next();
            float mx = 0.0f, pin = 0.0f;
            for (int c = 0; c < numCh; ++c) { mx = std::max (mx, std::abs (ch[c][i])); pin += ch[c][i] * ch[c][i]; }
            envF += (mx > envF ? fAtk : fRel) * (mx - envF);
            envS += (mx > envS ? sAtk : sRel) * (mx - envS);
            const float tr = juce::jlimit (0.0f, 1.0f, envF / (envS + 1.0e-4f) - 1.2f);
            trans += (tr > trans ? tAtk : tRel) * (tr - trans);
            // niveau de référence de la normalisation : appris pendant le chant seulement (gelé dans les silences, la
            // reprise sort au même niveau que la phrase d'avant)
            fastPow += fastPowC * (pin * inv - fastPow);
            if (fastPow > 1.0e-6f && fastPow > 0.03f * refPow) refPow += refC * (pin * inv - refPow);

            // gain d'attaque jusqu'à +24 dB, normalisé sur le niveau crête habituel de la voix
            const float k = dbToGain (24.0f * d);
            const float b = (0.03f + 0.09f * d) * std::min (1.0f, d * 8.0f);
            const float ab = asig (b);
            const float ref = juce::jlimit (0.02f, 1.0f, std::sqrt (2.0f * refPow));
            const float norm = ref / (asig (ref * k + b) - ab);
            const float gx = 0.8f + 3.2f * d;
            const float w = std::min (1.0f, d * 5.0f) * (1.0f - 0.35f * trans);

            float pout = 0.0f;
            float y[2] {};
            for (int c = 0; c < numCh; ++c)
            {
                const float x = ch[c][i];
                const float lo = split ? splitB.process (splitA.process (x, c), c) : x;   // LIVE : la partie sous 7 kHz
                const float a = asig (k * lo + b) - ab;
                xfLp[c] += xfCoef * (a - xfLp[c]);
                const float s = (a - xfLp[c] + std::tanh (gx * xfLp[c]) / gx) * norm;
                float v = x + w * (s - lo);   // le haut (x - lo) passe intact
                v += d * (lowCol.process (v, c) - v);
                v += d * (presCol.process (v, c) - v);
                v += d * (topCol.process (v, c) - v);
                v = dc.process (v, c);
                y[c] = v;
                pout += v * v;
            }
            const float g = match.next (pin * inv, pout * inv, 0.25f, 2.0f);
            const float on = std::min (1.0f, d * 20.0f);
            for (int c = 0; c < numCh; ++c) ch[c][i] += on * (y[c] * g - ch[c][i]);
        }
    }

private:
    double fs = 48000.0;
    Smooth drive;
    LevelMatch match;
    Biquad lowCol, presCol, topCol, dc;
    float xfCoef = 0.0f, xfLp[2] {};
    Biquad splitA, splitB;
    bool split = false;
    float fAtk = 0, fRel = 0, sAtk = 0, sRel = 0, tAtk = 0, tRel = 0, refC = 0;
    float envF = 0, envS = 0, trans = 0, refPow = 0.06f, fastPow = 0.0f, fastPowC = 0.0f;
    bool wasIdle = true;
};

/*  PHONE : de la voix pleine au téléphone, en un bouton.
    Bande de plus en plus étroite (coupe-bas 24 dB/oct de 120 à 520 Hz, coupe-haut 24 dB/oct de 9 à 3 kHz), une
    résonance de petit haut-parleur vers 1.7 kHz, et un écrêtage doux qui grandit avec le bouton. Le mélange arrive
    vite (plein dès 40 %), le caractère continue de se durcir jusqu'à 100 %. Volume tenu.
    L'écrêtage reçoit une voix déjà bornée par le premier coupe-haut : ses harmoniques restent sous Nyquist, propre en
    LIVE comme en MIX. */
class Phone
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        amt.prepare (fs, 0.05, amt.target());
        match.prepare (fs, 0.7, 0.4);
        last = -1.0f;
        reset();
    }
    void reset() noexcept { for (auto& f : flt) f.reset(); match.reset(); }
    void set (float amount01, bool snap) noexcept { amt.set (juce::jlimit (0.0f, 1.0f, amount01), snap); }

    void process (float* const* ch, int numCh, int n) noexcept
    {
        if (amt.idle()) { if (! wasIdle) { reset(); wasIdle = true; } return; }
        wasIdle = false;
        const float inv = 1.0f / (float) numCh;
        for (int i = 0; i < n; ++i)
        {
            const float p = amt.next();
            if (std::abs (p - last) > 1.0e-3f) update (p);
            const float drive = 1.0f + 6.0f * p * p;
            const float norm = 1.0f / std::tanh (drive * 0.5f);
            const float mix = juce::jlimit (0.0f, 1.0f, p * 2.5f);
            float pin = 0.0f, pout = 0.0f, y[2] {};
            for (int c = 0; c < numCh; ++c)
            {
                const float x = ch[c][i];
                pin += x * x;
                float v = flt[1].process (flt[0].process (x, c), c);
                v = flt[3].process (flt[2].process (v, c), c);   // résonance puis 1er coupe-haut, avant l'écrêtage
                v = std::tanh (drive * 0.5f * v) * norm * 0.5f + 0.5f * v * (1.0f - p);
                v = flt[4].process (v, c);
                y[c] = v;
                pout += v * v;
            }
            const float g = match.next (pin * inv, pout * inv, 0.25f, 8.0f);
            for (int c = 0; c < numCh; ++c) ch[c][i] += mix * (y[c] * g - ch[c][i]);
        }
    }

private:
    void update (float p) noexcept
    {
        last = p;
        const double hp = 120.0 + 400.0 * p, lp = std::min (9000.0 - 6000.0 * p, 0.42 * fs);
        flt[0].setCoeffs (BiquadCoeffs::highpass (fs, hp, 0.5412));
        flt[1].setCoeffs (BiquadCoeffs::highpass (fs, hp, 1.3066));
        flt[2].setCoeffs (BiquadCoeffs::peaking (fs, 1700.0, 1.2, 7.0 * p));
        flt[3].setCoeffs (BiquadCoeffs::lowpass (fs, lp, 0.5412));
        flt[4].setCoeffs (BiquadCoeffs::lowpass (fs, lp, 1.3066));
    }

    double fs = 48000.0;
    Smooth amt;
    LevelMatch match;
    Biquad flt[5];
    float last = -1.0f;
    bool wasIdle = true;
};
} // namespace onirique::mini
