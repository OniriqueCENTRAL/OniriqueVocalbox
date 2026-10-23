#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>
#include "Common.h"

namespace onirique
{
/*  RESONANCE : suppresseur de résonances dynamique à détection spectrale (modèle original,
    dans l'esprit d'un "soothe"). STFT, aucune EQ statique : seules les crêtes étroites qui
    dépassent du spectre environnant, au moment où elles apparaissent, sont réduites.

    Analyse / synthèse : FFT 2048 points (4096 / 8192 aux fréquences d'échantillonnage
    supérieures, résolution ~23 Hz constante), fenêtre racine de Hann, recouvrement 87.5 %
    (la réduction évolue par pas de ~5 ms). Les deux canaux passent dans une seule FFT complexe (L en partie
    réelle, R en partie imaginaire) : deux fois moins de calcul qu'avec une FFT réelle par canal.
    Reconstruction parfaite : avec AMOUNT à 0 le signal ressort identique, retardé de N
    échantillons (latence déclarée, ~43 ms à 48 kHz).

    Détection "intelligente", par trame, sur la somme L+R (même gain sur les deux canaux :
    l'image stéréo ne bouge pas) :
      1. spectre en dB, normalisé pour qu'un sinus de crête A lise 20 log10 A (dBFS) ; chaque bin monte
         aussitôt et redescend en ~20 ms : le souffle et les bruits ne déclenchent presque plus rien ;
      2. enveloppe des crêtes : maximum glissant (~1/24 d'octave, au moins 4 bins) qui passe par
         le sommet des harmoniques de la voix, puis moyenne glissante sur 2/3 d'octave
         (fenêtre proportionnelle à la fréquence), en deux passes robustes (crêtes écrêtées à
         +2 dB de la première moyenne : une forte résonance ne relève pas sa propre référence).
         Les harmoniques normales de la voix sont donc à 0 dB d'excès : elles ne sont jamais
         aplaties, seule une crête qui dépasse ses voisines est visée ;
      3. excès = spectre - enveloppe. Au-delà de 2.5 dB (genou doux de 4 dB), l'excès est
         retiré (x 1.25) : la crête revient au niveau de son voisinage, le reste du spectre n'est
         pas touché. Réduction max 20 dB, le tout x AMOUNT x sensibilité de la zone ;
      4. lissage de la réduction en fréquence (triangle de ~1/70 d'octave, au moins 1-2-1) puis dans le
         temps (attaque 5 ms, relâchement 80 ms) : pas de "gargouillis" spectral ;
      5. bins sous -80 dBFS ignorés (silences, souffle).

    Bandes de contrôle LOW / MID / HIGH : chacune cible une zone (fréquence centrale + largeur du
    plateau en octaves, flancs d'une demi-octave) avec sa sensibilité 0..1 ; la sensibilité en un
    point est le maximum des bandes. Hors des bandes, rien n'est traité. Zone utile : 30 Hz - 20 kHz.

    Mesure pour l'interface : réduction (dB) et spectre d'entrée (dBFS) sur kBands bandes
    logarithmiques 20 Hz - 20 kHz, publiés en atomiques (lecture sans verrou côté GUI).
*/
class ResonanceSuppressor
{
public:
    static constexpr int kBands = 96;
    static constexpr float kMinHz = 20.0f, kMaxHz = 20000.0f;
    static constexpr float kMaxRedDb = 20.0f;
    static constexpr float kStrength = 1.25f;   // excès retiré x 1.25 : à AMOUNT égal, autant de résonance en moins qu'avant, moins de dégâts autour

    struct Meter
    {
        std::array<std::atomic<float>, kBands> reductionDb {};
        std::array<std::atomic<float>, kBands> spectrumDb {};
    };

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        // ~23 Hz de résolution à toutes les cadences (la chaîne peut tourner suréchantillonnée x2..x8)
        int order = juce::jlimit (11, 15, 11 + (int) std::ceil (std::log2 (std::max (1.0, fs / 50000.0))));
        N = 1 << order;
        mask = N - 1;
        hop = N / 8;   // recouvrement 87.5 % : la réduction évolue par pas de ~5 ms, sans marches
        nb = N / 2 + 1;
        fft = std::make_unique<juce::dsp::FFT> (order);

        window.resize ((size_t) N);
        double wsum = 0.0;
        for (int j = 0; j < N; ++j)
        {
            window[(size_t) j] = (float) std::sqrt (0.5 - 0.5 * std::cos (6.283185307179586 * j / N));
            wsum += window[(size_t) j];
        }
        powNorm = (float) (4.0 / (wsum * wsum));   // sinus d'amplitude A -> puissance A^2 sur son bin
        olaScale = 0.25f;                          // somme des Hann à 87.5 % de recouvrement = 4

        for (int c = 0; c < 2; ++c)
        {
            inBuf[c].assign ((size_t) N, 0.0f);
            outBuf[c].assign ((size_t) N, 0.0f);
        }
        zIn.assign ((size_t) N, {});
        zOut.assign ((size_t) N, {});
        for (auto* v : { &db, &pdet, &peak, &env, &clipped, &target, &tmp, &red, &weight })
            v->assign ((size_t) nb, 0.0f);
        prefix.assign ((size_t) nb + 1, 0.0);

        // fenêtre d'enveloppe : +/- 1/3 d'octave autour de chaque bin, au moins +/- 3 bins
        winLo.assign ((size_t) nb, 0);
        winHi.assign ((size_t) nb, 0);
        const double r = std::pow (2.0, 1.0 / 3.0);
        maxHalf.assign ((size_t) nb, 0);
        smHalf.assign ((size_t) nb, 1);
        for (int k = 0; k < nb; ++k)
        {
            maxHalf[(size_t) k] = std::max (4, (int) std::lround (k * 0.03));   // ~1/24 d'octave, au moins 4 bins
            smHalf[(size_t) k] = std::max (1, (int) std::lround (k * 0.01));    // lissage des réductions : ~1/70 d'octave, au moins 1 bin

            winLo[(size_t) k] = std::max (0, std::min (k - 3, (int) std::floor (k / r)));
            winHi[(size_t) k] = std::min (nb - 1, std::max (k + 3, (int) std::ceil (k * r)));
        }

        const double df = fs / N;
        kLo = std::max (1, (int) std::ceil (30.0 / df));
        kHi = std::min (nb - 2, (int) std::floor (std::min (20000.0, 0.45 * fs) / df));

        // bandes d'affichage
        for (int b = 0; b < kBands; ++b)
        {
            const double f0 = kMinHz * std::pow ((double) kMaxHz / kMinHz, (double) b / kBands);
            const double f1 = kMinHz * std::pow ((double) kMaxHz / kMinHz, (double) (b + 1) / kBands);
            int a = (int) std::ceil (f0 / df), z = (int) std::floor (f1 / df);
            if (z < a) a = z = (int) std::lround (std::sqrt (f0 * f1) / df);
            bandLo[(size_t) b] = juce::jlimit (0, nb - 1, a);
            bandHi[(size_t) b] = juce::jlimit (0, nb - 1, z);
        }

        // zone utile : rien n'est réduit au-dessus de 20 kHz ; au-delà de la fenêtre d'analyse de ce dernier bin, le spectre
        // ne sert plus à rien. Suréchantillonnée (x2..x8), la chaîne donne 2 à 8 fois plus de bins, presque tous vides :
        // ils ne sont plus calculés (le résultat est identique, la RESONANCE coûtait autant que tout le reste réuni).
        {
            // deux moyennes glissantes à la suite (enveloppe puis enveloppe des crêtes écrêtées) : deux fenêtres de marge
            const int kr = juce::jlimit (0, nb - 1, (int) std::ceil (std::ceil (kHi * r) * r) + 1);
            int top = kr + maxHalf[(size_t) kr] + 2;
            for (int b = 0; b < kBands; ++b) top = std::max (top, bandHi[(size_t) b]);
            nu = juce::jlimit (8, nb, top + 1);
            for (int k = 0; k < nu; ++k) winHi[(size_t) k] = std::min (winHi[(size_t) k], nu - 1);
            dq.assign ((size_t) nu, 0);
        }

        const double hopSec = (double) hop / fs;
        atk = (float) (1.0 - std::exp (-hopSec / 0.005));
        rel = (float) (1.0 - std::exp (-hopSec / 0.080));
        detRel = (float) (1.0 - std::exp (-hopSec / 0.020));

        weightsDirty = true;
        reset();
    }

    void reset() noexcept
    {
        for (int c = 0; c < 2; ++c)
        {
            std::fill (inBuf[c].begin(), inBuf[c].end(), 0.0f);
            std::fill (outBuf[c].begin(), outBuf[c].end(), 0.0f);
        }
        std::fill (red.begin(), red.end(), 0.0f);
        std::fill (pdet.begin(), pdet.end(), 0.0f);
        pos = 0;
        hopCount = 0;
        for (int b = 0; b < kBands; ++b)
        {
            meter.reductionDb[(size_t) b].store (0.0f, std::memory_order_relaxed);
            meter.spectrumDb[(size_t) b].store (-120.0f, std::memory_order_relaxed);
        }
    }

    int getLatencySamples() const noexcept { return N; }

    // Bande de contrôle : zone ciblée (centre en Hz, largeur du plateau en octaves) et sensibilité 0..1
    struct Band
    {
        float freq = 1000.0f, widthOct = 2.0f, sens = 1.0f;
        bool operator!= (const Band& o) const noexcept { return freq != o.freq || widthOct != o.widthOct || sens != o.sens; }
    };
    static constexpr int kNumBands = 3;   // LOW, MID, HIGH

    // Forme d'une bande en fonction de la fréquence : plateau de 'widthOct' octaves centré sur
    // 'freq', flancs en cosinus d'une demi-octave de chaque côté. Partagée avec l'interface.
    static float bandShape (float hz, float freq, float widthOct) noexcept
    {
        const float d = std::abs (std::log2 (std::max (1.0f, hz) / freq));
        const float half = 0.5f * widthOct;
        if (d <= half) return 1.0f;
        if (d >= half + 0.5f) return 0.0f;
        return 0.5f + 0.5f * std::cos (3.14159265f * (d - half) / 0.5f);
    }

    // sensibilité à une fréquence = maximum des bandes (deux bandes qui se recouvrent ne s'additionnent pas)
    static float sensitivityAt (float hz, const std::array<Band, kNumBands>& b) noexcept
    {
        float w = 0.0f;
        for (const auto& x : b) w = std::max (w, x.sens * bandShape (hz, x.freq, x.widthOct));
        return w;
    }

    // amount : 0..1 ; bandes lues à chaque trame (la réduction est lissée ensuite)
    void setParams (float amount01, const std::array<Band, kNumBands>& b) noexcept
    {
        amount = juce::jlimit (0.0f, 1.0f, amount01);
        for (int i = 0; i < kNumBands; ++i)
            if (b[(size_t) i] != bands[(size_t) i])
            {
                bands = b;
                weightsDirty = true;
                break;
            }
    }

    void process (float* const* ch, int numCh, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < numCh; ++c)
            {
                inBuf[c][(size_t) pos] = ch[c][i];
                ch[c][i] = outBuf[c][(size_t) pos];
                outBuf[c][(size_t) pos] = 0.0f;
            }
            pos = (pos + 1) & mask;

            if (++hopCount >= hop)
            {
                hopCount = 0;
                processFrame (numCh);
            }
        }
    }

    const Meter& getMeter() const noexcept { return meter; }

private:
    void updateWeights() noexcept
    {
        const double df = fs / N;
        for (int k = 0; k < nb; ++k)
            weight[(size_t) k] = sensitivityAt ((float) (k * df), bands);
        weightsDirty = false;
    }

    void boxMean (const std::vector<float>& src, std::vector<float>& dst) noexcept
    {
        prefix[0] = 0.0;
        for (int k = 0; k < nu; ++k) prefix[(size_t) k + 1] = prefix[(size_t) k] + src[(size_t) k];
        for (int k = 0; k < nu; ++k)
        {
            const int a = winLo[(size_t) k], z = winHi[(size_t) k];
            dst[(size_t) k] = (float) ((prefix[(size_t) z + 1] - prefix[(size_t) a]) / (z - a + 1));
        }
    }

    void processFrame (int numCh) noexcept
    {
        if (weightsDirty) updateWeights();

        // ---- analyse (pos = échantillon le plus ancien de la trame) : L en partie réelle, R en partie imaginaire, une seule
        //      FFT complexe pour les deux canaux. Z[k] = L[k] + i R[k] et |L[k]|^2 + |R[k]|^2 = (|Z[k]|^2 + |Z[N-k]|^2) / 2
        const float* inL = inBuf[0].data();
        const float* inR = inBuf[numCh > 1 ? 1 : 0].data();
        const float im = numCh > 1 ? 1.0f : 0.0f;
        for (int j = 0; j < N; ++j)
        {
            const int q = (pos + j) & mask;
            const float w = window[(size_t) j];
            zIn[(size_t) j] = { inL[q] * w, im * inR[q] * w };
        }
        fft->perform (zIn.data(), zOut.data(), false);

        const float pScale = 0.5f / (float) numCh * powNorm;
        for (int k = 0; k < nu; ++k)
        {
            const auto a = zOut[(size_t) k], b = zOut[(size_t) ((N - k) & mask)];
            const float p = (a.real() * a.real() + a.imag() * a.imag() + b.real() * b.real() + b.imag() * b.imag()) * pScale;
            // détection : la crête monte tout de suite, redescend en ~20 ms (moins de fausses alertes sur le souffle)
            float& d = pdet[(size_t) k];
            d = p > d ? p : d + detRel * (p - d);
            db[(size_t) k] = 4.342944819f * std::log (d + 1.0e-14f);
        }

        // ---- enveloppe des crêtes (maximum glissant : elle passe par le sommet des harmoniques),
        //      puis moyenne robuste -> niveau "normal" des crêtes voisines ; excès -> réduction
        //      maximum glissant en une passe (file monotone) : les deux bords de la fenêtre ne font qu'avancer
        {
            int head = 0, tail = 0, next = 0;
            for (int k = 0; k < nu; ++k)
            {
                const int h = maxHalf[(size_t) k];
                const int a = std::max (0, k - h), z = std::min (nu - 1, k + h);
                for (; next <= z; ++next)
                {
                    while (tail > head && db[(size_t) dq[(size_t) tail - 1]] <= db[(size_t) next]) --tail;
                    dq[(size_t) tail++] = next;
                }
                while (dq[(size_t) head] < a) ++head;
                peak[(size_t) k] = db[(size_t) dq[(size_t) head]];
            }
        }

        boxMean (peak, env);
        for (int k = 0; k < nu; ++k)
            clipped[(size_t) k] = std::min (peak[(size_t) k], env[(size_t) k] + 2.0f);
        boxMean (clipped, env);

        for (int k = 0; k < nu; ++k)
        {
            float t = 0.0f;
            if (amount > 0.0f && k >= kLo && k <= kHi && db[(size_t) k] > -80.0f)
            {
                const float x = db[(size_t) k] - env[(size_t) k] - 2.5f;
                const float knee = x <= -2.0f ? 0.0f : (x < 2.0f ? (x + 2.0f) * (x + 2.0f) * 0.125f : x);
                t = std::min (kMaxRedDb, kStrength * knee) * amount * weight[(size_t) k];
            }
            target[(size_t) k] = t;
        }

        // lissage des réductions en fréquence : triangle de +/- smHalf bins
        for (int k = 0; k < nu; ++k)
        {
            const int h = smHalf[(size_t) k];
            float acc = (float) (h + 1) * target[(size_t) k];
            for (int j = 1; j <= h; ++j)
                acc += (float) (h + 1 - j) * (target[(size_t) std::max (0, k - j)] + target[(size_t) std::min (nu - 1, k + j)]);
            tmp[(size_t) k] = acc / (float) ((h + 1) * (h + 1));
        }
        target.swap (tmp);

        // ---- lissage temporel + application du gain
        for (int k = 0; k < nu; ++k)   // au-dessus de la zone utile : aucune réduction (gain 1)
        {
            float& r = red[(size_t) k];
            const float t = target[(size_t) k];
            r += (t > r ? atk : rel) * (t - r);
            if (r > 1.0e-4f)
            {
                // même gain réel sur L et R : il s'applique tel quel à Z[k] et à son miroir Z[N - k]
                const float g = dbToGain (-r);
                zOut[(size_t) k] *= g;
                if (k > 0 && k < N / 2) zOut[(size_t) (N - k)] *= g;
            }
        }

        // ---- synthèse, recouvrement-addition (lu N échantillons plus tard) : partie réelle -> L, imaginaire -> R
        fft->perform (zOut.data(), zIn.data(), true);
        float* outL = outBuf[0].data();
        float* outR = outBuf[1].data();
        for (int j = 0; j < N; ++j)
        {
            const int q = (pos + j) & mask;
            const float w = window[(size_t) j] * olaScale;
            outL[q] += zIn[(size_t) j].real() * w;
            if (numCh > 1) outR[q] += zIn[(size_t) j].imag() * w;
        }

        // ---- mesure pour l'interface
        for (int b = 0; b < kBands; ++b)
        {
            float rm = 0.0f, sm = -120.0f;
            for (int k = bandLo[(size_t) b]; k <= bandHi[(size_t) b]; ++k)
            {
                rm = std::max (rm, red[(size_t) k]);
                sm = std::max (sm, db[(size_t) k]);
            }
            meter.reductionDb[(size_t) b].store (rm, std::memory_order_relaxed);
            meter.spectrumDb[(size_t) b].store (sm, std::memory_order_relaxed);
        }
    }

    double fs = 44100.0;
    int N = 2048, mask = 2047, hop = 512, nb = 1025, nu = 1025, pos = 0, hopCount = 0, kLo = 1, kHi = 1;   // nu : bins utiles
    std::vector<int> dq;   // file du maximum glissant
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window, inBuf[2], outBuf[2];
    std::vector<juce::dsp::Complex<float>> zIn, zOut;
    std::vector<float> db, pdet, peak, env, clipped, target, tmp, red, weight;
    std::vector<double> prefix;
    std::vector<int> winLo, winHi, maxHalf, smHalf;
    std::array<int, kBands> bandLo {}, bandHi {};
    float powNorm = 1.0f, olaScale = 0.25f, atk = 0.0f, rel = 0.0f, detRel = 1.0f;
    float amount = 0.0f;
    std::array<Band, kNumBands> bands {};
    bool weightsDirty = true;
    Meter meter;
};
} // namespace onirique
