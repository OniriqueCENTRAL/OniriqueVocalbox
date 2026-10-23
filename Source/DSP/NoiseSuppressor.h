#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <vector>
#include "Biquad.h"
#include "Common.h"

namespace onirique
{
/*  NOISE SUPPRESSOR : un seul curseur (0..100 %), plus il monte, plus le bruit de fond (souffle, ventilation, pièce,
    bourdonnement large) descend, sans abîmer la voix. Premier module de la chaîne (avant le NOISE GATE), à la cadence de
    l'hôte.

    Analyse (commune aux deux modes) : spectre du signal (mono : moyenne des canaux) par trames courtes.
    - Bruit estimé bin par bin par "statistiques de minimum" : la puissance lissée de chaque bin, son minimum sur
      ~1.5 s (8 sous-fenêtres glissantes) corrigé de son biais. La voix ne descend jamais au plancher de façon
      durable, le bruit si : l'estimation suit le bruit sans jamais "apprendre" la voix, et sans bouton LEARN.
    - Gain par bin : filtre de Wiener à SNR a priori "decision-directed" (Ephraim-Malah). C'est ce qui évite le
      "bruit musical" (gazouillis) des débruiteurs simples : le gain ne saute pas d'une trame à l'autre.
    - Plancher : l'atténuation maximale suit le curseur (jusqu'à -30 dB à 100 %), et elle est réduite de moitié
      quand la voix est là (le bruit entre les harmoniques est masqué par la voix ; c'est ce qui garde le timbre
      intact quand on monte le curseur). Gains lissés en fréquence et relâchés en douceur dans le temps.

    MIX : STFT (trame ~21 ms, recouvrement 75 %, fenêtres racine de Hann), gains appliqués bin par bin.
          Latence = une trame, compensée par le DAW.
    LIVE : latence nulle. La même analyse tourne sur une copie du signal ; ses gains sont regroupés en 24 bandes
          (1/3 d'octave environ) et appliqués au son par 24 cloches IIR à phase minimale (un plateau grave et un plateau aigu aux
          deux bouts : le bruit sous 60 Hz et au-dessus de 16 kHz descend aussi), recalculées toutes les
          32 échantillons. L'atténuation maximale est limitée à -18 dB (au-delà, des cloches qui se chevauchent
          ne suivent plus fidèlement la courbe voulue).

    Curseur à 0 : le module rend le signal d'origine (retardé de sa latence en MIX), en fondu de 30 ms.
    L'analyse continue de tourner quand le module est coupé (le bruit reste estimé : à l'allumage, il agit tout de suite),
    mais le son, lui, n'est plus touché du tout, et la synthèse ne tourne plus : coupé, le module ne coûte plus qu'une FFT
    par trame. À l'allumage, la synthèse repart, le fondu attend qu'une trame complète soit reconstruite.
    Les deux canaux passent dans une seule FFT complexe (L en partie réelle, R en partie imaginaire).
*/
class NoiseSuppressor
{
public:
    static constexpr int kBands = 24;

    void prepare (double sampleRate, bool liveMode)
    {
        fs = sampleRate;
        live = liveMode;
        const double frameSec = live ? 0.0107 : 0.0213;
        order = juce::jlimit (8, 13, (int) std::ceil (std::log2 (frameSec * fs)));
        N = 1 << order;
        hop = N / 4;
        bins = N / 2 + 1;
        fft = std::make_unique<juce::dsp::FFT> (order);

        win.assign ((size_t) N, 0.0f);
        for (int i = 0; i < N; ++i)
            win[(size_t) i] = (float) std::sqrt (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / N));   // racine de Hann
        for (int c = 0; c < 2; ++c)
        {
            inFifo[c].assign ((size_t) N, 0.0f);
            outAcc[c].assign ((size_t) (2 * N), 0.0f);
            dryDelay[c].prepare (live ? 0 : N - 1);
        }
        monoFrame.assign ((size_t) (2 * N), 0.0f);
        zIn.assign ((size_t) N, {});
        zOut.assign ((size_t) N, {});
        synthOn = false;
        warm = 0;
        for (auto* v : { &smoothP, &curMin, &noise, &prevGain, &prevGamma, &gainS, &gain, &lastP })
            v->assign ((size_t) bins, 0.0f);
        std::fill (gain.begin(), gain.end(), 1.0f);
        std::fill (gainS.begin(), gainS.end(), 1.0f);
        std::fill (prevGain.begin(), prevGain.end(), 1.0f);
        std::fill (curMin.begin(), curMin.end(), 1.0e30f);
        for (auto& s : subMin) s.assign ((size_t) bins, 1.0e30f);
        subIndex = 0;
        framesInSub = 0;
        framesPerSub = std::max (4, (int) std::lround (1.5 / (kSub * (double) hop / fs)));
        fifoPos = 0;
        outPos = 0;
        frames = 0;
        mix.reset (fs, 0.03);
        mix.setCurrentAndTargetValue (amount > 0.0005f ? 1.0f : 0.0f);

        // LIVE : bandes ~1/3 d'octave de 60 Hz à 16 kHz, et leurs cloches
        for (int b = 0; b < kBands; ++b)
        {
            bandHz[b] = 60.0 * std::pow (16000.0 / 60.0, (double) b / (kBands - 1));
            bandGainDb[b] = bandTargetDb[b] = 0.0f;
            bell[b].reset();
        }
        updateBells (true);
        liveCount = 0;
    }

    int getLatencySamples() const noexcept { return live ? 0 : N - 1; }   // la trame la plus ancienne ressort N - 1 échantillons plus tard

    // amount01 : position du curseur 0..1
    void setAmount (float amount01) noexcept
    {
        amount = juce::jlimit (0.0f, 1.0f, amount01);
        mix.setTargetValue (amount > 0.0005f ? 1.0f : 0.0f);
    }

    void process (float* const* ch, int numCh, int n) noexcept
    {
        float redSum = 0.0f;
        int redN = 0;
        // synthèse seulement quand le son est touché ; au réveil, le fondu attend une trame complète (N + hop échantillons)
        const bool needSynth = ! live && (amount > 0.0005f || mix.isSmoothing() || mix.getCurrentValue() > 0.0f);
        if (needSynth && ! synthOn)
        {
            for (auto& a : outAcc) std::fill (a.begin(), a.end(), 0.0f);
            warm = N + hop;
            const float tgt = mix.getTargetValue();   // le fondu repart de zéro
            mix.setCurrentAndTargetValue (0.0f);
            mix.setTargetValue (tgt);
        }
        synthOn = needSynth;
        const int msk = N - 1, msk2 = 2 * N - 1;
        for (int i = 0; i < n; ++i)
        {
            // entrée de l'analyse (et de la STFT en MIX)
            for (int c = 0; c < numCh; ++c) inFifo[c][(size_t) fifoPos] = ch[c][i];
            if (numCh == 1) inFifo[1][(size_t) fifoPos] = ch[0][i];
            fifoPos = (fifoPos + 1) & msk;

            if (++hopCount >= hop)
            {
                hopCount = 0;
                analyseFrame (numCh);
                redSum += lastReductionDb; ++redN;
            }

            float m = 0.0f;
            if (warm > 0) --warm;
            else          m = sCurve (mix.getNextValue());
            if (live)
            {
                // cloches recalculées toutes les 32 échantillons, gains glissés vers la cible
                if (m <= 0.0f && ! mix.isSmoothing()) continue;   // curseur à 0 : rien à filtrer
                if (++liveCount >= 32) { liveCount = 0; updateBells (false); }
                for (int c = 0; c < numCh; ++c)
                {
                    const float x = ch[c][i];
                    float y = x;
                    for (int b = 0; b < kBands; ++b) y = bell[b].process (y, c);
                    ch[c][i] = x + m * (y - x);
                }
            }
            else
            {
                for (int c = 0; c < numCh; ++c)
                {
                    const float dry = dryDelay[c].process (ch[c][i]);
                    const float wet = outAcc[c][(size_t) outPos];
                    outAcc[c][(size_t) outPos] = 0.0f;
                    ch[c][i] = dry + m * (wet - dry);
                }
                if (numCh == 1) outAcc[1][(size_t) outPos] = 0.0f;
                outPos = (outPos + 1) & msk2;
            }
        }
        if (redN > 0) reductionMeter.store (redSum / (float) redN, std::memory_order_relaxed);
        if (amount <= 0.0005f) reductionMeter.store (0.0f, std::memory_order_relaxed);
    }

    // réduction moyenne appliquée (dB, positive) et bruit de fond estimé (dBFS), pour l'interface
    float getReductionDb() const noexcept { return reductionMeter.load (std::memory_order_relaxed); }
    float getNoiseFloorDb() const noexcept { return floorMeter.load (std::memory_order_relaxed); }

private:
    static constexpr int kSub = 8;

    void analyseFrame (int numCh) noexcept
    {
        ++frames;
        // trame : les N derniers échantillons, fenêtrés ; L en partie réelle, R en partie imaginaire, une seule FFT complexe
        // (une piste mono a sa copie dans inFifo[1])
        const int msk = N - 1;
        for (int k = 0; k < N; ++k)
        {
            const size_t idx = (size_t) ((fifoPos + k) & msk);
            const float w = win[(size_t) k];
            zIn[(size_t) k] = { inFifo[0][idx] * w, inFifo[1][idx] * w };
        }
        fft->perform (zIn.data(), zOut.data(), false);
        juce::ignoreUnused (numCh);

        // spectre mono (L + R) / 2 : L[k] = (Z[k] + conj Z[N-k]) / 2, R[k] = (Z[k] - conj Z[N-k]) / 2i
        float* mono = monoFrame.data();
        for (int k = 0; k < bins; ++k)
        {
            const auto a = zOut[(size_t) k], b = zOut[(size_t) ((N - k) & msk)];
            const float lr = a.real() + b.real(), li = a.imag() - b.imag();   // 2 L[k]
            const float rr = a.imag() + b.imag(), ri = b.real() - a.real();   // 2 R[k]
            mono[2 * k]     = 0.25f * (lr + rr);
            mono[2 * k + 1] = 0.25f * (li + ri);
        }

        const float norm = 1.0f / (float) N;
        const float att = 30.0f * std::pow (amount, 0.8f);                               // atténuation maximale (dB)
        const float beta = 1.0f + (live ? 3.0f : 1.5f) * amount;                       // sur-estimation du bruit (plus franche en LIVE : bandes)
        const float gMin = std::pow (10.0f, -att / 20.0f);

        // ---- bruit : minimum glissant de la puissance lissée
        double sigP = 0.0, sigN = 0.0;
        for (int k = 0; k < bins; ++k)
        {
            const float re = mono[2 * k], im = mono[2 * k + 1];
            const float p = (re * re + im * im) * norm;
            lastP[(size_t) k] = p;
            float& s = smoothP[(size_t) k];
            s = frames == 1 ? p : 0.8f * s + 0.2f * p;
            curMin[(size_t) k] = std::min (curMin[(size_t) k], s);
            float mn = curMin[(size_t) k];
            for (auto& sm : subMin) mn = std::min (mn, sm[(size_t) k]);
            noise[(size_t) k] = std::min (1.8f * mn, std::max (s, 1.0e-20f));
            if (k > 2) { sigP += p; sigN += noise[(size_t) k]; }
        }
        if (++framesInSub >= framesPerSub)
        {
            framesInSub = 0;
            subMin[(size_t) subIndex] = curMin;
            subIndex = (subIndex + 1) % kSub;
            std::fill (curMin.begin(), curMin.end(), 1.0e30f);
        }
        floorMeter.store (10.0f * std::log10 ((float) sigN * 2.0f * norm + 1.0e-12f), std::memory_order_relaxed);

        // présence de la voix (SNR de la trame) : l'atténuation est moins profonde quand la voix est là
        const float frameSnrDb = 10.0f * std::log10 ((float) (sigP / (sigN + 1.0e-20)) + 1.0e-9f);
        const float voice = juce::jlimit (0.0f, 1.0f, (frameSnrDb - 3.0f) / 9.0f);
        const float gFloor = std::pow (gMin, 1.0f - (live ? 0.75f : 0.5f) * voice);   // LIVE : cloches larges, plus de retenue sous la voix

        // ---- gains (Wiener, SNR a priori "decision-directed")
        for (int k = 0; k < bins; ++k)
        {
            const float re = mono[2 * k], im = mono[2 * k + 1];
            const float p = (re * re + im * im) * norm;
            const float nk = beta * noise[(size_t) k] + 1.0e-20f;
            const float gamma = p / nk;
            const float xi = 0.98f * prevGain[(size_t) k] * prevGain[(size_t) k] * prevGamma[(size_t) k] + 0.02f * std::max (gamma - 1.0f, 0.0f);
            float g = xi / (1.0f + xi);
            prevGain[(size_t) k] = g;
            prevGamma[(size_t) k] = gamma;
            g = std::max (g, gFloor);
            // relâchement doux (la montée est immédiate : aucune attaque de consonne mangée)
            float& gs = gainS[(size_t) k];
            gs = g > gs ? g : 0.55f * gs + 0.45f * g;
        }
        // lissage en fréquence (moins de "gazouillis")
        float red = 0.0f, wsum = 0.0f;
        for (int k = 0; k < bins; ++k)
        {
            const float a = gainS[(size_t) std::max (0, k - 1)], b = gainS[(size_t) k], c = gainS[(size_t) std::min (bins - 1, k + 1)];
            // lissage qui ne fait que remonter les creux isolés (moins de gazouillis) : il ne baisse jamais un pic
            // d'harmonique (le lissage simple écrêtait les harmoniques de la voix de plusieurs dB)
            gain[(size_t) k] = std::max (b, 0.25f * a + 0.5f * b + 0.25f * c);
            const float w = noise[(size_t) k];
            red += w * gain[(size_t) k] * gain[(size_t) k];
            wsum += w;
        }
        lastReductionDb = amount > 0.0005f ? -10.0f * std::log10 (red / (wsum + 1.0e-20f) + 1.0e-9f) : 0.0f;

        if (live || ! synthOn) return;

        // ---- MIX : gains appliqués (même gain réel sur L et R : sur Z[k] et son miroir Z[N - k]), synthèse et
        //      recouvrement (somme des fenêtres racine de Hann² à 75 % = 2) ; partie réelle -> L, imaginaire -> R
        for (int k = 0; k < bins; ++k)
        {
            const float g = gain[(size_t) k];
            zOut[(size_t) k] *= g;
            if (k > 0 && k < N / 2) zOut[(size_t) (N - k)] *= g;
        }
        fft->perform (zOut.data(), zIn.data(), true);
        const int msk2 = 2 * N - 1;
        for (int k = 0; k < N; ++k)
        {
            const size_t idx = (size_t) ((outPos + k) & msk2);
            const float w = win[(size_t) k] * 0.5f;
            outAcc[0][idx] += zIn[(size_t) k].real() * w;
            outAcc[1][idx] += zIn[(size_t) k].imag() * w;
        }
    }

    // LIVE : gains regroupés par bande -> cloches à phase minimale
    void updateBells (bool snap) noexcept
    {
        if (gain.empty()) return;
        const double binHz = fs / N;
        for (int b = 0; b < kBands; ++b)
        {
            const double lo = bandHz[b] * std::pow (2.0, -1.0 / 6.0), hi = bandHz[b] * std::pow (2.0, 1.0 / 6.0);
            // la première et la dernière bande s'étendent jusqu'au bout du spectre (plateaux) : rien n'échappe sous 60 Hz ni au-dessus de 16 kHz
            const int k0 = b == 0 ? 1 : juce::jlimit (1, bins - 1, (int) std::floor (lo / binHz));
            const int k1 = b == kBands - 1 ? bins - 1 : juce::jlimit (k0, bins - 1, (int) std::ceil (hi / binHz));
            float s = 0.0f;
            // gain de la bande = rapport d'énergie sortie / entrée : une harmonique de la voix (forte, gain ~1) domine sa
            // bande, la bande reste ouverte ; dans un silence, c'est le bruit qui compte, la bande descend
            float pw = 0.0f;
            for (int k = k0; k <= k1; ++k) { s += gain[(size_t) k] * gain[(size_t) k] * lastP[(size_t) k]; pw += lastP[(size_t) k]; }
            bandTargetDb[b] = 10.0f * std::log10 (s / (pw + 1.0e-20f) + 1.0e-9f);
        }
        // pente limitée entre bandes voisines (8 dB) : une cloche creusée ne mord pas sur l'harmonique de la bande d'à côté
        for (int b = 1; b < kBands; ++b) bandTargetDb[b] = std::max (bandTargetDb[b], bandTargetDb[b - 1] - 8.0f);
        for (int b = kBands - 2; b >= 0; --b) bandTargetDb[b] = std::max (bandTargetDb[b], bandTargetDb[b + 1] - 8.0f);
        for (int b = 0; b < kBands; ++b)
        {
            float& g = bandGainDb[b];
            const float before = g;
            g = snap ? bandTargetDb[b] : g + 0.25f * (bandTargetDb[b] - g);
            if (snap || std::abs (g - before) > 0.02f || bandHz[b] >= 0.45 * fs)
                bell[b].setCoeffs (bandHz[b] >= 0.45 * fs ? BiquadCoeffs {}
                                   : b == 0 ? BiquadCoeffs::lowShelf (fs, bandHz[b] * 1.12, 0.7, g)
                                   : b == kBands - 1 ? BiquadCoeffs::highShelf (fs, bandHz[b] * 0.89, 0.7, g)
                                   : BiquadCoeffs::peaking (fs, bandHz[b], 4.3, g));
        }
    }

    double fs = 48000.0;
    bool live = false;
    int order = 10, N = 1024, hop = 256, bins = 513;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> win, inFifo[2], outAcc[2], monoFrame;
    std::vector<juce::dsp::Complex<float>> zIn, zOut;
    bool synthOn = false;
    int warm = 0;
    std::vector<float> smoothP, curMin, noise, prevGain, prevGamma, gainS, gain, lastP;
    std::array<std::vector<float>, kSub> subMin;
    int subIndex = 0, framesInSub = 0, framesPerSub = 35;
    int fifoPos = 0, outPos = 0, hopCount = 0, frames = 0, liveCount = 0;
    DelayLine dryDelay[2];
    float amount = 0.0f, lastReductionDb = 0.0f;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mix;
    double bandHz[kBands] {};
    float bandGainDb[kBands] {}, bandTargetDb[kBands] {};
    Biquad bell[kBands];
    std::atomic<float> reductionMeter { 0.0f }, floorMeter { -120.0f };
};
} // namespace onirique
