#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "Biquad.h"
#include "VoiceEQ.h"

namespace onirique
{
/*  EQ ANALOG : EQ de coloration 4 boutons, placé après la compression multibande dans la chaîne.
    Implémentation originale, 100 % IIR minimum-phase (aucun problème de phase entre bandes : les filtres s'additionnent
    en dB). Aucune latence.

    LOW  (0..10, Pultec-like) : shelf large sur la fréquence choisie (80 / 100 / 125 Hz) + petite cloche de "punch" à
         0.76 x f + creux à 2.16 x f. Le creux reprend une partie de l'énergie low-mid : le grave devient plus gros et
         plus profond sans simplement monter le niveau. Le passe-haut subsonique monte légèrement avec le bouton pour
         garder le très bas propre.
    MUD  (0..10) : deux cloches larges (300 Hz, 450 Hz) + compensation de corps (140 Hz) et d'intelligibilité (2.3 kHz)
         pour ne jamais obtenir une voix creuse.
    HI et AIR (-10..+10) : ceux de VOCAL MINI (mini::ShineEQ, DSP/VoiceEQ.h) :
      HI +  : présence à 3.2 kHz (+1.8 dB au plus : la zone de la dureté n'est jamais trop poussée) + plateau à 6 kHz ;
      HI -  : retire la dureté (3.6 kHz) et adoucit le haut (6.5 kHz) ;
      AIR + : un plateau très large posé à 12 kHz et prolongé à 17.5 kHz, plus le GRAIN : de vraies harmoniques de la
              voix, nées au-dessus de 6.5 kHz (excitation douce à niveau constant) : une brillance soyeuse, jamais sifflante ;
      AIR - : haut arrondi, plus sombre (plateau à 9 kHz).
      Garde de sifflantes : ce que HI / AIR ajoutent recule de moitié pendant un S, et seulement pendant le S.
    LOW CUT : passe-haut Butterworth du 3e ordre (18 dB/oct) à 85 Hz, en fondu de 30 ms à l'enclenchement.

    Sans clic ni zipper : les boutons sont lissés (50 ms), les coefficients recalculés toutes les 16 échantillons et
    GLISSÉS échantillon par échantillon entre deux calculs (forme directe I). Un filtre dont le bouton est à 0 glisse
    jusqu'à 0 dB exact, laisse s'éteindre son état, puis sort de la chaîne : il ne coûte plus rien (précieux en
    suréchantillonnage x8), et le signal passe intact.
*/
class VocalEQ
{
public:
    static constexpr int kSlots = 7;   // LOW : shelf, cloche, creux ; MUD : deux cloches, corps, clarté

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        lowFreqS.reset (fs, 0.08);
        lowFreqS.setCurrentAndTargetValue (lowFreqS.getTargetValue());
        for (auto* s : { &lowS, &mudS })
        {
            s->reset (fs, 0.05);
            s->setCurrentAndTargetValue (0.0f);
        }
        for (auto& s : slots) s.reset();
        hpf.reset();
        lcA.reset(); lcB.reset();
        lcA.setCoeffs (BiquadCoeffs::highpass (fs, 85.0, 1.0));    // Butterworth 3e ordre = 1er ordre + biquad Q 1
        lcB.setCoeffs (BiquadCoeffs::highpass1 (fs, 85.0));
        lcMix.reset (fs, 0.03);
        lcMix.setCurrentAndTargetValue (lcMix.getTargetValue());
        shine.prepare (fs);
        lastL = lastM = lastF = -1.0f;
        countdown = 0;
    }

    // low / mud : 0..10
    void setTargets (float low, float mud, bool snap = false) noexcept
    {
        low = juce::jlimit (0.0f, 10.0f, low) * 0.1f;
        mud = juce::jlimit (0.0f, 10.0f, mud) * 0.1f;
        if (snap)
        {
            lowS.setCurrentAndTargetValue (low);
            mudS.setCurrentAndTargetValue (mud);
            lastL = lastM = lastF = -1.0f;
            countdown = 0;
        }
        else
        {
            lowS.setTargetValue (low);
            mudS.setTargetValue (mud);
        }
    }

    // hi / air : -10..+10 (ceux de VOCAL MINI)
    void setShine (float hi, float air, bool snap = false) noexcept
    {
        shine.set (juce::jlimit (-10.0f, 10.0f, hi) * 0.1f, juce::jlimit (-10.0f, 10.0f, air) * 0.1f, snap);
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

    // réduction de la garde de sifflantes de HI / AIR (0..1), pour l'interface
    float getGuard() const noexcept { return shine.getGuard(); }

    void process (float* const* ch, int numCh, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const float l = lowS.getNextValue();
            const float m = mudS.getNextValue();
            const float lf = lowFreqS.getNextValue();
            const float lcm = sCurve (lcMix.getNextValue());

            if (--countdown <= 0)
            {
                if (std::abs (l - lastL) > 1.0e-5f || std::abs (m - lastM) > 1.0e-5f || std::abs (lf - lastF) > 1.0e-3f)
                {
                    BiquadCoeffs c[kSlots + 1];
                    bool want[kSlots + 1];
                    design (fs, l, m, lf, c, want);
                    hpf.setCoeffs (c[0]);
                    for (int k = 0; k < kSlots; ++k) slots[k].update (c[k + 1], want[k + 1]);
                    lastL = l; lastM = m; lastF = lf;
                }
                for (auto& s : slots) s.retire (16);
                countdown = 16;
            }
            for (auto& s : slots) s.tick();

            // le passe-haut subsonique n'entre qu'avec LOW : à 0, aucun déphasage
            const float hm = std::min (1.0f, 4.0f * l);

            // LOW CUT et passe-haut subsonique tournent en permanence (deux filtres) : quand on les enclenche, ils sont déjà
            // établis et le fondu passe d'un signal à l'autre sans transitoire de démarrage
            for (int c = 0; c < numCh; ++c)
            {
                float s = ch[c][i];
                const float lcOut = lcB.process (lcA.process (s, c), c);
                if (lcm > 0.0f) s += lcm * (lcOut - s);   // LOW CUT 85 Hz
                const float hpOut = hpf.process (s, c);
                if (hm > 0.0f) s += hm * (hpOut - s);
                for (auto& f : slots) s = f.process (s, c);
                ch[c][i] = s;
            }
        }
        shine.process (ch, numCh, n);   // HI et AIR (VOCAL MINI)
    }

    // Coefficients du passe-haut subsonique (out[0]) et des 7 filtres LOW / MUD (out[1..7]) pour des réglages donnés :
    // l, m 0..1, f en Hz ; want : le filtre agit (gain non nul). Partagé avec l'affichage : la courbe est celle appliquée.
    static void design (double fs, float l, float m, float f, BiquadCoeffs out[kSlots + 1], bool want[kSlots + 1]) noexcept
    {
        // fin de course : au-delà de ~6, chaque bouton va chercher plus loin (jusqu'à +55 % à 10) ; en dessous de 5,
        // la courbe reste celle d'origine à 7 % près
        auto reach = [] (float x, float k) { return 1.0 + (double) k * std::pow ((double) x, 3.0); };

        // ---- LOW
        const double a = taper (l, 1.25f) * reach (l, 0.55f), cd = taper (l, 1.05f) * reach (l, 0.25f);
        out[0] = BiquadCoeffs::highpass (fs, 12.0 + 10.0 * l, 0.707);
        out[1] = BiquadCoeffs::lowShelf (fs, f, 0.55, 6.0 * a);
        out[2] = BiquadCoeffs::peaking  (fs, 0.76 * f, 1.0, 1.6 * a);
        out[3] = BiquadCoeffs::peaking  (fs, 2.16 * f, 0.9, -2.4 * cd);

        // ---- MUD
        const double t = taper (m, 1.15f) * reach (m, 0.45f), mc = m * reach (m, 0.45f);
        out[4] = BiquadCoeffs::peaking (fs, 300.0, 0.85, -6.5 * t);
        out[5] = BiquadCoeffs::peaking (fs, 450.0, 1.2, -1.0 * t);
        out[6] = BiquadCoeffs::peaking (fs, 140.0, 0.9, 1.6 * mc);
        out[7] = BiquadCoeffs::peaking (fs, 2300.0, 0.9, 0.6 * mc);

        want[0] = l > 1.0e-4f;
        want[1] = want[2] = a > 1.0e-6;
        want[3] = cd > 1.0e-6;
        want[4] = want[5] = t > 1.0e-6;
        want[6] = want[7] = mc > 1.0e-6;
    }

    // Courbe de réponse de l'EQ complet (LOW CUT, passe-haut subsonique, HI / AIR compris : sans le grain ni la garde),
    // pour l'affichage.
    struct Curve
    {
        double fs = 48000.0;
        BiquadCoeffs c[kSlots + 1], lcA, lcB;
        BiquadCoeffs cut[mini::ShineEQ::kCut], boost[mini::ShineEQ::kBoost];
        bool want[kSlots + 1] {}, cutOn[mini::ShineEQ::kCut] {}, boostOn[mini::ShineEQ::kBoost] {};
        float hpMix = 0.0f, lcMix = 0.0f;

        // l, m : 0..10 ; hi, air : -10..10 (valeurs des boutons) ; lowCut01 : 0..1 (fondu du LOW CUT)
        static Curve make (double fs, float l, float m, float hi, float air, float f, float lowCut01) noexcept
        {
            Curve k;
            k.fs = fs;
            l = juce::jlimit (0.0f, 10.0f, l) * 0.1f;
            design (fs, l, juce::jlimit (0.0f, 10.0f, m) * 0.1f, f, k.c, k.want);
            mini::ShineEQ::design (fs, juce::jlimit (-10.0f, 10.0f, hi) * 0.1f, juce::jlimit (-10.0f, 10.0f, air) * 0.1f,
                                   k.cut, k.cutOn, k.boost, k.boostOn);
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
            for (int i = 1; i <= kSlots; ++i) if (want[i]) r *= c[i].response (fs, hz);
            for (int i = 0; i < mini::ShineEQ::kCut; ++i) if (cutOn[i]) r *= cut[i].response (fs, hz);
            for (int i = 0; i < mini::ShineEQ::kBoost; ++i) if (boostOn[i]) r *= boost[i].response (fs, hz);
            return (float) (20.0 * std::log10 (std::max (1.0e-9, std::abs (r))));
        }
    };

private:
    static float taper (float x, float p) noexcept { return std::pow (std::max (x, 0.0f), p); }

    double fs = 44100.0;
    int countdown = 0;
    float lastL = -1.0f, lastM = -1.0f, lastF = -1.0f;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> lowS, mudS;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> lowFreqS { 125.0f };
    Biquad hpf;
    mini::EqSlot slots[kSlots];
    Biquad lcA, lcB;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> lcMix;
    mini::ShineEQ shine;
};
} // namespace onirique
