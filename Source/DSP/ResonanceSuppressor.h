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
    supérieures, résolution ~23 Hz constante), fenêtre racine de Hann, recouvrement 75 %.
    Reconstruction parfaite : avec AMOUNT à 0 le signal ressort identique, retardé de N
    échantillons (latence déclarée, ~43 ms à 48 kHz).

    Détection "intelligente", par trame, sur la somme L+R (même gain sur les deux canaux :
    l'image stéréo ne bouge pas) :
      1. spectre en dB, normalisé pour qu'un sinus de crête A lise 20 log10 A (dBFS) ;
      2. enveloppe des crêtes : maximum glissant (~1/24 d'octave, au moins 4 bins) qui passe par
         le sommet des harmoniques de la voix, puis moyenne glissante sur 2/3 d'octave
         (fenêtre proportionnelle à la fréquence), en deux passes robustes (crêtes écrêtées à
         +2 dB de la première moyenne : une forte résonance ne relève pas sa propre référence).
         Les harmoniques normales de la voix sont donc à 0 dB d'excès : elles ne sont jamais
         aplaties, seule une crête qui dépasse ses voisines est visée ;
      3. excès = spectre - enveloppe. Au-delà de 2.5 dB (genou doux de 4 dB), l'excès est
         retiré : la crête revient au niveau de son voisinage, le reste du spectre n'est pas
         touché. Réduction max 20 dB, le tout x AMOUNT x sensibilité de la zone ;
      4. lissage de la réduction en fréquence (1-2-1) puis dans le temps
         (attaque 5 ms, relâchement 80 ms) : pas de "gargouillis" spectral ;
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

    struct Meter
    {
        std::array<std::atomic<float>, kBands> reductionDb {};
        std::array<std::atomic<float>, kBands> spectrumDb {};
    };

    // live = true : mode LIVE, aucune latence (voir processLive)
    void prepare (double sampleRate, bool liveMode = false)
    {
        fs = sampleRate;
        live = liveMode;
        // ~23 Hz de résolution à toutes les cadences (la chaîne peut tourner suréchantillonnée x2..x8)
        int order = juce::jlimit (11, 15, 11 + (int) std::ceil (std::log2 (std::max (1.0, fs / 50000.0))));
        if (live) --order;                         // analyse plus courte : la détection réagit plus vite
        N = 1 << order;
        mask = N - 1;
        hop = live ? N / 8 : N / 4;

        // banc d'EQ dynamique du mode LIVE : 28 cloches de 100 Hz à 16 kHz (~1/3.7 d'octave), Q 4.5
        const double ratio = std::pow (16000.0 / 100.0, 1.0 / (kDyn - 1));
        for (int j = 0; j < kDyn; ++j)
        {
            dynHz[(size_t) j] = (float) std::min (100.0 * std::pow (ratio, j), 0.45 * fs);
            dynFilter[(size_t) j].reset();
            dynTarget[(size_t) j] = dynCur[(size_t) j] = dynLast[(size_t) j] = 0.0f;
            dynFilter[(size_t) j].setCoeffs (BiquadCoeffs::peaking (fs, dynHz[(size_t) j], 4.5, 0.0));
        }
        dynSmooth = (float) (1.0 - std::exp (-1.0 / (0.003 * fs)));
        dynCount = 0;
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
        olaScale = 0.5f;                           // somme des Hann à 75 % de recouvrement = 2

        for (int c = 0; c < 2; ++c)
        {
            inBuf[c].assign ((size_t) N, 0.0f);
            outBuf[c].assign ((size_t) N, 0.0f);
            spec[c].assign ((size_t) (2 * N), 0.0f);
        }
        for (auto* v : { &db, &peak, &env, &clipped, &target, &tmp, &red, &weight })
            v->assign ((size_t) nb, 0.0f);
        prefix.assign ((size_t) nb + 1, 0.0);

        // fenêtre d'enveloppe : +/- 1/3 d'octave autour de chaque bin, au moins +/- 3 bins
        winLo.assign ((size_t) nb, 0);
        winHi.assign ((size_t) nb, 0);
        const double r = std::pow (2.0, 1.0 / 3.0);
        maxHalf.assign ((size_t) nb, 0);
        for (int k = 0; k < nb; ++k)
        {
            maxHalf[(size_t) k] = std::max (4, (int) std::lround (k * 0.03));   // ~1/24 d'octave, au moins 4 bins

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

        const double hopSec = (double) hop / fs;
        atk = (float) (1.0 - std::exp (-hopSec / 0.005));
        rel = (float) (1.0 - std::exp (-hopSec / 0.080));

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
        pos = 0;
        hopCount = 0;
        for (int b = 0; b < kBands; ++b)
        {
            meter.reductionDb[(size_t) b].store (0.0f, std::memory_order_relaxed);
            meter.spectrumDb[(size_t) b].store (-120.0f, std::memory_order_relaxed);
        }
    }

    int getLatencySamples() const noexcept { return live ? 0 : N; }

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
        if (live) { processLive (ch, numCh, n); return; }

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
    static constexpr int kDyn = 28;

    /*  Mode LIVE, latence nulle : la même analyse spectrale tourne sur le signal entrant, mais le son
        ne passe plus par la FFT. Les réductions calculées pilotent 28 cloches dynamiques (minimum de
        phase) appliquées directement au signal : rien n'est retardé. Un peu moins chirurgical que le
        mode MIX (une cloche couvre ~1/4 d'octave) et la détection a quelques ms de retard. */
    void processLive (float* const* ch, int numCh, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < numCh; ++c) inBuf[c][(size_t) pos] = ch[c][i];
            pos = (pos + 1) & mask;
            if (++hopCount >= hop) { hopCount = 0; processFrame (numCh); }

            const bool recalc = --dynCount <= 0;
            if (recalc) dynCount = 32;
            for (int j = 0; j < kDyn; ++j)
            {
                const auto s = (size_t) j;
                dynCur[s] += dynSmooth * (dynTarget[s] - dynCur[s]);
                if (recalc && std::abs (dynCur[s] - dynLast[s]) > 0.02f)
                {
                    dynFilter[s].setCoeffs (BiquadCoeffs::peaking (fs, dynHz[s], 4.5, -dynCur[s]));
                    dynLast[s] = dynCur[s];
                }
                for (int c = 0; c < numCh; ++c) ch[c][i] = dynFilter[s].process (ch[c][i], c);
            }
        }
    }

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
        for (int k = 0; k < nb; ++k) prefix[(size_t) k + 1] = prefix[(size_t) k] + src[(size_t) k];
        for (int k = 0; k < nb; ++k)
        {
            const int a = winLo[(size_t) k], z = winHi[(size_t) k];
            dst[(size_t) k] = (float) ((prefix[(size_t) z + 1] - prefix[(size_t) a]) / (z - a + 1));
        }
    }

    void processFrame (int numCh) noexcept
    {
        if (weightsDirty) updateWeights();

        // ---- analyse (pos = échantillon le plus ancien de la trame)
        for (int c = 0; c < numCh; ++c)
        {
            float* s = spec[c].data();
            for (int j = 0; j < N; ++j)
                s[j] = inBuf[c][(size_t) ((pos + j) & mask)] * window[(size_t) j];
            std::fill (s + N, s + 2 * N, 0.0f);
            fft->performRealOnlyForwardTransform (s, true);
        }

        const float invCh = 1.0f / (float) numCh;
        for (int k = 0; k < nb; ++k)
        {
            float p = 0.0f;
            for (int c = 0; c < numCh; ++c)
            {
                const float re = spec[c][(size_t) (2 * k)], im = spec[c][(size_t) (2 * k + 1)];
                p += re * re + im * im;
            }
            db[(size_t) k] = 4.342944819f * std::log (p * invCh * powNorm + 1.0e-14f);
        }

        // ---- enveloppe des crêtes (maximum glissant : elle passe par le sommet des harmoniques),
        //      puis moyenne robuste -> niveau "normal" des crêtes voisines ; excès -> réduction
        for (int k = 0; k < nb; ++k)
        {
            const int h = maxHalf[(size_t) k];
            const int a = std::max (0, k - h), z = std::min (nb - 1, k + h);
            float m = db[(size_t) a];
            for (int j = a + 1; j <= z; ++j) m = std::max (m, db[(size_t) j]);
            peak[(size_t) k] = m;
        }

        boxMean (peak, env);
        for (int k = 0; k < nb; ++k)
            clipped[(size_t) k] = std::min (peak[(size_t) k], env[(size_t) k] + 2.0f);
        boxMean (clipped, env);

        for (int k = 0; k < nb; ++k)
        {
            float t = 0.0f;
            if (amount > 0.0f && k >= kLo && k <= kHi && db[(size_t) k] > -80.0f)
            {
                const float x = db[(size_t) k] - env[(size_t) k] - 2.5f;
                const float knee = x <= -2.0f ? 0.0f : (x < 2.0f ? (x + 2.0f) * (x + 2.0f) * 0.125f : x);
                t = std::min (kMaxRedDb, knee) * amount * weight[(size_t) k];
            }
            target[(size_t) k] = t;
        }

        tmp[0] = target[0];
        tmp[(size_t) nb - 1] = target[(size_t) nb - 1];
        for (int k = 1; k < nb - 1; ++k)
            tmp[(size_t) k] = 0.25f * target[(size_t) k - 1] + 0.5f * target[(size_t) k] + 0.25f * target[(size_t) k + 1];
        target.swap (tmp);

        // ---- lissage temporel + application du gain
        if (live)
        {
            // mode LIVE : la réduction de chaque cloche = la plus forte réduction dans sa zone
            const double df = fs / N;
            for (int k = 0; k < nb; ++k)
            {
                float& r = red[(size_t) k];
                const float t = target[(size_t) k];
                r += (t > r ? atk : rel) * (t - r);
            }
            const double half = std::pow (16000.0 / 100.0, 0.5 / (kDyn - 1));
            for (int j = 0; j < kDyn; ++j)
            {
                const int a = juce::jlimit (0, nb - 1, (int) std::floor (dynHz[(size_t) j] / half / df));
                const int z = juce::jlimit (a, nb - 1, (int) std::ceil (dynHz[(size_t) j] * half / df));
                float mx = 0.0f;
                for (int k = a; k <= z; ++k) mx = std::max (mx, red[(size_t) k]);
                dynTarget[(size_t) j] = mx;
            }
        }

        for (int k = 0; k < nb && ! live; ++k)
        {
            float& r = red[(size_t) k];
            const float t = target[(size_t) k];
            r += (t > r ? atk : rel) * (t - r);
            const float g = r > 1.0e-4f ? dbToGain (-r) : 1.0f;
            for (int c = 0; c < numCh; ++c)
            {
                spec[c][(size_t) (2 * k)] *= g;
                spec[c][(size_t) (2 * k + 1)] *= g;
            }
        }

        // ---- synthèse, recouvrement-addition (lu N échantillons plus tard) ; pas en mode LIVE
        for (int c = 0; c < numCh && ! live; ++c)
        {
            float* s = spec[c].data();
            fft->performRealOnlyInverseTransform (s);
            for (int j = 0; j < N; ++j)
                outBuf[c][(size_t) ((pos + j) & mask)] += s[j] * window[(size_t) j] * olaScale;
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
    int N = 2048, mask = 2047, hop = 512, nb = 1025, pos = 0, hopCount = 0, kLo = 1, kHi = 1;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window, inBuf[2], outBuf[2], spec[2];
    std::vector<float> db, peak, env, clipped, target, tmp, red, weight;
    std::vector<double> prefix;
    std::vector<int> winLo, winHi, maxHalf;
    std::array<int, kBands> bandLo {}, bandHi {};
    float powNorm = 1.0f, olaScale = 0.5f, atk = 0.0f, rel = 0.0f;
    float amount = 0.0f;
    std::array<Band, kNumBands> bands {};
    bool weightsDirty = true;
    Meter meter;

    bool live = false;
    std::array<Biquad, kDyn> dynFilter;
    std::array<float, kDyn> dynHz {}, dynTarget {}, dynCur {}, dynLast {};
    float dynSmooth = 0.01f;
    int dynCount = 0;
};
} // namespace onirique
