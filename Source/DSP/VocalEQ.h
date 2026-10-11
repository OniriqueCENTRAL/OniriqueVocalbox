#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "Biquad.h"

namespace onirique
{
/*  EQ vocal 3 boutons (0..10), implémentation originale, 100 % IIR minimum-phase
    (aucun problème de phase entre bandes : les filtres s'additionnent en dB).

    LOW  (Pultec-like) : shelf large sur la fréquence choisie (80 / 100 / 125 Hz, 125 Hz à
         l'origine) + petite cloche de "punch" à 0.76 x f (95 Hz)
         + creux à 2.16 x f (270 Hz). Le creux reprend une partie de l'énergie low-mid : le grave
         devient plus gros et plus profond sans simplement monter le niveau. Le passe-haut
         subsonique monte légèrement avec le bouton pour garder le très bas propre.
    MUD  : deux cloches larges (300 Hz, 430 Hz) + compensation de corps (160 Hz) et
         d'intelligibilité (2.3 kHz) pour ne jamais obtenir une voix creuse.
    HIGH (Avalon-like) : définition 2.6 kHz + shelf 5.5 kHz + air sur la fréquence choisie
         (8 / 10 / 20 kHz) + extension à 1.41 x f : ouverture progressive, très large donc sans
         dureté. 20K = "air" façon bande Air : shelf très large posé tout en haut du spectre.

    LOW CUT : passe-haut Butterworth du 3e ordre (18 dB/oct) à 85 Hz, enclenché par un bouton,
         fondu de 30 ms à l'enclenchement (les filtres tournent en permanence : aucun clic).

    Tous les gains suivent des courbes progressives (puissance) et les 3 boutons sont
    lissés (50 ms). Coefficients recalculés toutes les 16 échantillons seulement
    quand un bouton bouge.
*/
class VocalEQ
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        lowFreqS.reset (fs, 0.08);
        highFreqS.reset (fs, 0.08);
        highFreqS.setCurrentAndTargetValue (highFreqS.getTargetValue());
        lowFreqS.setCurrentAndTargetValue (lowFreqS.getTargetValue());
        for (auto* s : { &lowS, &mudS, &highS })
        {
            s->reset (fs, 0.05);
            s->setCurrentAndTargetValue (0.0f);
        }
        for (auto* f : filters) f->reset();
        lcA.reset(); lcB.reset();
        lcA.setCoeffs (BiquadCoeffs::highpass (fs, 85.0, 1.0));    // Butterworth 3e ordre = 1er ordre + biquad Q 1
        lcB.setCoeffs (BiquadCoeffs::highpass1 (fs, 85.0));
        lcMix.reset (fs, 0.03);
        lcMix.setCurrentAndTargetValue (lcMix.getTargetValue());
        lastL = lastM = lastH = lastF = lastHF = -1.0f;
        countdown = 0;
    }

    // low / mud / high : 0..10
    void setTargets (float low, float mud, float high, bool snap = false) noexcept
    {
        low = juce::jlimit (0.0f, 10.0f, low) * 0.1f;
        mud = juce::jlimit (0.0f, 10.0f, mud) * 0.1f;
        high = juce::jlimit (0.0f, 10.0f, high) * 0.1f;

        if (snap)
        {
            lowS.setCurrentAndTargetValue (low);
            mudS.setCurrentAndTargetValue (mud);
            highS.setCurrentAndTargetValue (high);
            lastL = lastM = lastH = lastF = lastHF = -1.0f;
            countdown = 0;
        }
        else
        {
            lowS.setTargetValue (low);
            mudS.setTargetValue (mud);
            highS.setTargetValue (high);
        }
    }

    // fréquence du LOW (Hz) : glisse en 80 ms vers la nouvelle valeur, sans clic
    void setLowFreq (float hz, bool snap = false) noexcept
    {
        hz = juce::jlimit (40.0f, 250.0f, hz);
        if (snap) { lowFreqS.setCurrentAndTargetValue (hz); lastF = -1.0f; }
        else      lowFreqS.setTargetValue (hz);
    }

    void setLowCut (bool on, bool snap = false) noexcept
    {
        if (snap) lcMix.setCurrentAndTargetValue (on ? 1.0f : 0.0f);
        else      lcMix.setTargetValue (on ? 1.0f : 0.0f);
    }

    // fréquence de l'air du HIGH (Hz) : même glissement sans clic
    void setHighFreq (float hz, bool snap = false) noexcept
    {
        hz = juce::jlimit (4000.0f, 20000.0f, hz);
        if (snap) { highFreqS.setCurrentAndTargetValue (hz); lastHF = -1.0f; }
        else      highFreqS.setTargetValue (hz);
    }

    void process (float* const* ch, int numCh, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const float l = lowS.getNextValue();
            const float m = mudS.getNextValue();
            const float h = highS.getNextValue();
            const float lf = lowFreqS.getNextValue();
            const float hf = highFreqS.getNextValue();
            const float lcm = lcMix.getNextValue();

            if (--countdown <= 0)
            {
                if (std::abs (l - lastL) > 1.0e-5f || std::abs (m - lastM) > 1.0e-5f
                    || std::abs (h - lastH) > 1.0e-5f || std::abs (lf - lastF) > 1.0e-3f
                    || std::abs (hf - lastHF) > 1.0e-2f)
                {
                    updateCoeffs (l, m, h, lf, hf);
                    lastL = l; lastM = m; lastH = h; lastF = lf; lastHF = hf;
                }
                countdown = 16;
            }

            // le passe-haut subsonique n'entre qu'avec LOW : à 0, aucun déphasage
            const float hm = std::min (1.0f, 4.0f * l);

            for (int c = 0; c < numCh; ++c)
            {
                float s = ch[c][i];
                s += lcm * (lcB.process (lcA.process (s, c), c) - s);   // LOW CUT 85 Hz
                s += hm * (hpf.process (s, c) - s);
                for (auto* f : filters) if (f != &hpf) s = f->process (s, c);
                ch[c][i] = s;
            }
        }
    }

private:
    static float taper (float x, float p) noexcept { return std::pow (std::max (x, 0.0f), p); }

    void updateCoeffs (float l, float m, float h, float f, float hf) noexcept
    {
        BiquadCoeffs c[12];
        design (fs, l, m, h, f, hf, c);
        for (int i = 0; i < 12; ++i) filters[i]->setCoeffs (c[i]);
    }

public:
    // Coefficients des 12 filtres (ordre de `filters`) pour des réglages donnés : l, m, h 0..1, f et hf en Hz.
    // Partagé avec l'analyseur de l'interface : la courbe affichée est exactement celle appliquée.
    static void design (double fs, float l, float m, float h, float f, float hf, BiquadCoeffs out[12]) noexcept
    {
        // ---- LOW
        const double a = taper (l, 1.25f), cd = taper (l, 1.05f);
        out[0] = BiquadCoeffs::highpass (fs, 12.0 + 10.0 * l, 0.707);
        out[1] = BiquadCoeffs::lowShelf (fs, f, 0.55, 6.0 * a);
        out[2] = BiquadCoeffs::peaking  (fs, 0.76 * f, 1.0, 1.6 * a);
        out[3] = BiquadCoeffs::peaking  (fs, 2.16 * f, 0.9, -2.4 * cd);

        // ---- MUD
        const double t = taper (m, 1.15f);
        out[4] = BiquadCoeffs::peaking (fs, 300.0, 0.85, -6.5 * t);
        out[5] = BiquadCoeffs::peaking (fs, 450.0, 1.2, -1.0 * t);
        out[6] = BiquadCoeffs::peaking (fs, 140.0, 0.9, 1.6 * m);
        out[7] = BiquadCoeffs::peaking (fs, 2300.0, 0.9, 0.6 * m);

        // ---- HIGH
        const double u = taper (h, 1.1f);
        const double fAir = std::min ((double) hf, 0.42 * fs);
        const double fTop = std::min (1.41 * hf, 0.45 * fs);
        const double qAir = hf > 15000.0f ? 0.35 : 0.5;     // 20K : pente très large, l'air descend en douceur
        out[8]  = BiquadCoeffs::peaking   (fs, 2600.0, 0.7, 1.4 * u);
        out[9]  = BiquadCoeffs::highShelf (fs, 5500.0, 0.5, 2.8 * u);
        out[10] = BiquadCoeffs::highShelf (fs, fAir, qAir, 5.5 * u);
        out[11] = BiquadCoeffs::highShelf (fs, fTop, 0.55, 3.5 * u);
    }

    // Courbe de réponse de l'EQ complet (LOW CUT et passe-haut subsonique compris), pour l'affichage.
    struct Curve
    {
        double fs = 48000.0;
        BiquadCoeffs c[12], lcA, lcB;
        float hpMix = 0.0f, lcMix = 0.0f;

        // l, m, h : 0..10 (valeurs des boutons) ; lowCut01 : 0..1 (fondu du LOW CUT)
        static Curve make (double fs, float l, float m, float h, float f, float hf, float lowCut01) noexcept
        {
            Curve k;
            k.fs = fs;
            l = juce::jlimit (0.0f, 10.0f, l) * 0.1f;
            design (fs, l, juce::jlimit (0.0f, 10.0f, m) * 0.1f, juce::jlimit (0.0f, 10.0f, h) * 0.1f, f, hf, k.c);
            k.hpMix = std::min (1.0f, 4.0f * l);
            k.lcA = BiquadCoeffs::highpass (fs, 85.0, 1.0);
            k.lcB = BiquadCoeffs::highpass1 (fs, 85.0);
            k.lcMix = lowCut01;
            return k;
        }

        float db (double hz) const noexcept
        {
            using C = std::complex<double>;
            C hp = c[0].response (fs, hz);
            C r = (1.0 + (double) hpMix * (hp - 1.0)) * (1.0 + (double) lcMix * (lcA.response (fs, hz) * lcB.response (fs, hz) - 1.0));
            for (int i = 1; i < 12; ++i) r *= c[i].response (fs, hz);
            return (float) (20.0 * std::log10 (std::max (1.0e-9, std::abs (r))));
        }
    };

private:

    double fs = 44100.0;
    int countdown = 0;
    float lastL = -1.0f, lastM = -1.0f, lastH = -1.0f, lastF = -1.0f, lastHF = -1.0f;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> lowS, mudS, highS;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> lowFreqS { 125.0f }, highFreqS { 10000.0f };
    Biquad hpf, lowShelf, lowBell, lowDip, mudA, mudB, mudBody, mudClar, presBell, presShelf, air, top;
    Biquad lcA, lcB;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> lcMix;
    Biquad* filters[12] { &hpf, &lowShelf, &lowBell, &lowDip, &mudA, &mudB, &mudBody, &mudClar,
                          &presBell, &presShelf, &air, &top };
};
} // namespace onirique
