#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <vector>
#include "Common.h"

namespace onirique
{
/*  DE-REVERB : la pièce retirée de la voix (chambre, salon, cabine mal traitée), sans toucher au son direct.

    Le principe : une voix prise dans une pièce, c'est la voix directe plus une queue qui décroît d'environ 60 dB en
    RT60 secondes. Ce qui arrive plus de 50 ms après un son n'est plus lui, c'est la pièce : à chaque trame, la
    réverbe tardive est prédite d'après le spectre d'il y a 50 ms, diminué de la décroissance de la pièce (modèle de
    Lebart et Habets). Le gain de chaque fréquence suit le rapport voix / réverbe estimé, avec le lissage « decision
    directed » d'Ephraim-Malah (le même que le NOISE SUPPRESSOR) : pas de gazouillis.

    - AUTO : la décroissance de la pièce est mesurée en continu sur les fins de phrases (la pente des queues libres,
      après la chute du son direct). Sinon, TAIL la donne (0,1 .. 3 s).
    - AMOUNT : jusqu'où la queue descend (jusqu'à -24 dB) et à quel point on fait confiance à la prédiction.
    - Les graves résonnent plus longtemps que les aigus dans une pièce : la décroissance est plus lente sous 500 Hz et
      plus rapide au-dessus de 4 kHz.
    - LISTEN : seulement ce qui est retiré (la pièce), pour régler.
    STFT : trame ~21 ms, recouvrement 75 %, racine de Hann ; latence d'une trame. */
class DeReverb
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        order = juce::jlimit (8, 13, (int) std::ceil (std::log2 (0.0213 * fs)));
        N = 1 << order;
        hop = N / 4;
        bins = N / 2 + 1;
        fft = std::make_unique<juce::dsp::FFT> (order);
        win.assign ((size_t) N, 0.0f);
        for (int i = 0; i < N; ++i)
            win[(size_t) i] = (float) std::sqrt (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / N));
        for (int c = 0; c < 2; ++c)
        {
            inFifo[c].assign ((size_t) N, 0.0f);
            outAcc[c].assign ((size_t) (2 * N), 0.0f);
            dryDelay[c].prepare (N - 1);
        }
        zIn.assign ((size_t) N, {});
        zOut.assign ((size_t) N, {});
        delayFrames = std::max (2, (int) std::lround (0.05 * fs / hop));
        histLen = delayFrames + 1;
        hist.assign ((size_t) (histLen * bins), 0.0f);
        for (auto* v : { &smoothP, &prevGain, &prevGamma, &gainS, &gain, &decayScale })
            v->assign ((size_t) bins, 0.0f);
        std::fill (prevGain.begin(), prevGain.end(), 1.0f);
        std::fill (gainS.begin(), gainS.end(), 1.0f);
        std::fill (gain.begin(), gain.end(), 1.0f);
        for (int k = 0; k < bins; ++k)
        {
            const double f = (double) k * fs / N;
            // la queue dure plus dans le grave (x 1,25 sous 250 Hz), moins dans l'aigu (x 0,65 au-dessus de 6 kHz)
            const double t = juce::jlimit (0.0, 1.0, std::log2 (std::max (f, 1.0) / 250.0) / std::log2 (6000.0 / 250.0));
            decayScale[(size_t) k] = (float) (1.25 - 0.6 * t);
        }
        histPos = 0; frames = 0; fifoPos = 0; outPos = 0; hopCount = 0;
        energyHist.fill (-120.0f); ePos = 0; eSmooth = -120.0f; prevDb = -120.0f; tailStartDb = -120.0f; tailFrames = 0; measureCount = 0; runFrames = 0; runPeak = -120.0f; runStartDb = -120.0f;
        rtEstimate = -1.0f;
        mix.reset (fs, 0.03);
        mix.setCurrentAndTargetValue (amount > 0.0005f ? 1.0f : 0.0f);
    }

    int getLatencySamples() const noexcept { return N - 1; }
    void reset() noexcept
    {
        for (int c = 0; c < 2; ++c) { std::fill (inFifo[c].begin(), inFifo[c].end(), 0.0f); std::fill (outAcc[c].begin(), outAcc[c].end(), 0.0f); dryDelay[c].reset(); }
        std::fill (hist.begin(), hist.end(), 0.0f);
        std::fill (smoothP.begin(), smoothP.end(), 0.0f);
        std::fill (prevGain.begin(), prevGain.end(), 1.0f);
        std::fill (prevGamma.begin(), prevGamma.end(), 0.0f);
        std::fill (gainS.begin(), gainS.end(), 1.0f);
        frames = 0;
    }

    // amount01 0..1, tailSec : la décroissance donnée à la main (AUTO coupé), listen : seulement ce qui est retiré
    void setParams (float amount01, float tailSec, bool autoTail, bool listenRemoved) noexcept
    {
        amount = juce::jlimit (0.0f, 1.0f, amount01);
        manualTail = juce::jlimit (0.1f, 3.0f, tailSec);
        useAuto = autoTail;
        listen = listenRemoved;
        mix.setTargetValue (amount > 0.0005f ? 1.0f : 0.0f);
    }

    void process (float* const* ch, int numCh, int n) noexcept
    {
        const int msk = N - 1, msk2 = 2 * N - 1;
        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < numCh; ++c) inFifo[c][(size_t) fifoPos] = ch[c][i];
            if (numCh == 1) inFifo[1][(size_t) fifoPos] = ch[0][i];
            fifoPos = (fifoPos + 1) & msk;
            if (++hopCount >= hop) { hopCount = 0; frame(); }

            const float m = sCurve (mix.getNextValue());
            for (int c = 0; c < numCh; ++c)
            {
                const float dry = dryDelay[c].process (ch[c][i]);
                const float wet = outAcc[c][(size_t) outPos];
                outAcc[c][(size_t) outPos] = 0.0f;
                const float y = listen ? dry - wet : wet;
                ch[c][i] = dry + m * (y - dry);
            }
            if (numCh == 1) outAcc[1][(size_t) outPos] = 0.0f;
            outPos = (outPos + 1) & msk2;
        }
    }

    float getReductionDb() const noexcept { return reductionMeter.load (std::memory_order_relaxed); }
    float getTailSeconds() const noexcept { return tailMeter.load (std::memory_order_relaxed); }   // la décroissance utilisée
    bool tailMeasured() const noexcept { return rtEstimate > 0.0f; }

private:
    float currentTail() const noexcept { return useAuto && rtEstimate > 0.0f ? rtEstimate : manualTail; }

    // la pente des queues libres : une suite de trames qui ne remontent pas, après la chute du son direct (les 6 premiers dB)
    void measureTail (float rawDb) noexcept
    {
        const float hopSec = (float) hop / (float) fs;
        // une queue de pièce est du bruit : son énergie tremble de trame en trame ; lissée sur ~3 trames
        eSmooth = eSmooth < -119.0f ? rawDb : 0.55f * eSmooth + 0.45f * rawDb;
        const float eDb = eSmooth;
        float loud = -120.0f;
        for (float e : energyHist) loud = std::max (loud, e);
        energyHist[(size_t) ePos] = eDb; ePos = (ePos + 1) % (int) energyHist.size();

        if (runFrames > 0 && eDb <= prevDb + 1.0f)
        {
            ++runFrames;
            if (runStartDb - eDb > 5.0f && tailStartDb < -100.0f) { tailStartDb = eDb; tailFrames = 0; }
            if (tailStartDb > -100.0f && tailFrames < (int) tailCurve.size()) tailCurve[(size_t) tailFrames++] = eDb;
        }
        else
        {
            // fin d'une descente (après la chute du son direct, 5 dB) : la pente de sa seconde moitié, par régression, si elle
            // dure assez (60 ms) et descend assez (6 dB). Un son ne peut pas s'éteindre plus vite que la pièce : la pièce, ce
            // sont les queues les plus rapides (le 25e centile des dernières mesures ; une note relâchée lentement compte peu).
            if (tailStartDb > -100.0f && tailFrames * hopSec >= 0.06f && tailCurve[0] - tailCurve[(size_t) tailFrames - 1] >= 6.0f
                && tailCurve[(size_t) tailFrames - 1] > -95.0f)
            {
                const int a = tailFrames * 2 / 5, m = tailFrames - a;
                double sx = 0, sy = 0, sxx = 0, sxy = 0;
                for (int j = a; j < tailFrames; ++j)
                {
                    const double x = (double) (j - a) * hopSec, y = tailCurve[(size_t) j];
                    sx += x; sy += y; sxx += x * x; sxy += x * y;
                }
                const double den = m * sxx - sx * sx;
                const float slope = den > 1.0e-12 ? (float) (-(m * sxy - sx * sy) / den) : 0.0f;   // dB par seconde (positif)
                if (slope > 2.0f)
                {
                    measures[(size_t) (measureCount++ % (int) measures.size())] = juce::jlimit (0.1f, 3.0f, 60.0f / slope);
                    const int nm = std::min (measureCount, (int) measures.size());
                    std::array<float, 24> s {};
                    std::copy (measures.begin(), measures.begin() + nm, s.begin());
                    std::sort (s.begin(), s.begin() + nm);
                    const float rt = s[(size_t) (nm / 4)];
                    rtEstimate = rtEstimate < 0.0f ? rt : 0.7f * rtEstimate + 0.3f * rt;
                }
            }
            runFrames = eDb > loud - 20.0f ? 1 : 0;   // une descente compte si elle part d'un son présent
            runStartDb = eDb;
            tailStartDb = -120.0f;
            tailEndDb = 0.0f;
            tailFrames = 0;
        }
        prevDb = eDb;
    }

    void frame() noexcept
    {
        ++frames;
        const int msk = N - 1;
        for (int k = 0; k < N; ++k)
        {
            const size_t idx = (size_t) ((fifoPos + k) & msk);
            const float w = win[(size_t) k];
            zIn[(size_t) k] = { inFifo[0][idx] * w, inFifo[1][idx] * w };
        }
        fft->perform (zIn.data(), zOut.data(), false);

        const float norm = 1.0f / (float) N;
        // puissance mono de chaque bin, et l'énergie 300 Hz .. 4 kHz pour la mesure de la pièce
        double eBand = 0.0;
        const int kLo = (int) (300.0 * N / fs), kHi = (int) (4000.0 * N / fs);
        float* past = hist.data() + (size_t) (((histPos + 1) % histLen) * bins);   // il y a delayFrames trames
        float* nowRow = hist.data() + (size_t) (histPos * bins);

        const float tail = currentTail();
        const float hopSec = (float) hop / (float) fs;
        const float td = (float) delayFrames * hopSec;
        const float att = 24.0f * std::pow (amount, 0.8f);
        const float gMin = std::pow (10.0f, -att / 20.0f);
        const float beta = 0.5f + 0.9f * amount;
        float red = 0.0f, wsum = 0.0f;

        for (int k = 0; k < bins; ++k)
        {
            const auto a = zOut[(size_t) k], b = zOut[(size_t) ((N - k) & msk)];
            const float lr = a.real() + b.real(), li = a.imag() - b.imag();
            const float rr = a.imag() + b.imag(), ri = b.real() - a.real();
            const float re = 0.25f * (lr + rr), im = 0.25f * (li + ri);
            const float p = (re * re + im * im) * norm;
            if (k >= kLo && k <= kHi) eBand += p;

            float& s = smoothP[(size_t) k];
            s = frames == 1 ? p : 0.6f * s + 0.4f * p;
            nowRow[k] = s;

            // la réverbe tardive prédite : le spectre d'il y a 50 ms, diminué de 60 dB par RT60 (2·δ·td en puissance)
            const float rt = tail * decayScale[(size_t) k];
            const float decay = std::exp (-2.0f * 6.9078f / rt * td);
            const float late = beta * decay * past[k] + 1.0e-20f;

            const float gamma = (0.5f * p + 0.5f * s) / late;   // un peu lissé : la queue est du bruit, sa puissance tremble d'un bin à l'autre
            const float xi = 0.85f * prevGain[(size_t) k] * prevGain[(size_t) k] * prevGamma[(size_t) k] + 0.15f * std::max (gamma - 1.0f, 0.0f);
            // Wiener : le son direct, qui domine, n'est presque pas touché ; la queue descend
            const float w = xi / (1.0f + xi);
            prevGain[(size_t) k] = w;
            prevGamma[(size_t) k] = gamma;
            float g = std::max (std::pow (w, 0.75f), gMin);   // un peu plus doux que Wiener : le direct garde son corps
            float& gs = gainS[(size_t) k];
            gs = g > gs ? 0.3f * gs + 0.7f * g : 0.6f * gs + 0.4f * g;   // monte vite (attaques), relâche en douceur
            red += p * gs * gs; wsum += p;
        }
        histPos = (histPos + 1) % histLen;
        for (int k = 0; k < bins; ++k)
        {
            const float a = gainS[(size_t) std::max (0, k - 1)], b = gainS[(size_t) k], c = gainS[(size_t) std::min (bins - 1, k + 1)];
            gain[(size_t) k] = std::max (b, 0.25f * a + 0.5f * b + 0.25f * c);
        }
        measureTail (10.0f * std::log10 ((float) eBand + 1.0e-12f));
        tailMeter.store (tail, std::memory_order_relaxed);
        const float rdb = amount > 0.0005f && wsum > 1.0e-12f ? -10.0f * std::log10 (red / wsum + 1.0e-9f) : 0.0f;
        reductionMeter.store (0.8f * reductionMeter.load (std::memory_order_relaxed) + 0.2f * rdb, std::memory_order_relaxed);

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

    double fs = 48000.0;
    int order = 10, N = 1024, hop = 256, bins = 513;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> win, inFifo[2], outAcc[2];
    std::vector<juce::dsp::Complex<float>> zIn, zOut;
    std::vector<float> hist, smoothP, prevGain, prevGamma, gainS, gain, decayScale;
    int delayFrames = 9, histLen = 10, histPos = 0, frames = 0, fifoPos = 0, outPos = 0, hopCount = 0;
    DelayLine dryDelay[2];
    float amount = 0.0f, manualTail = 0.5f;
    bool useAuto = true, listen = false;
    // mesure de la pièce
    std::array<float, 128> energyHist {};
    std::array<float, 256> tailCurve {};
    std::array<float, 24> measures {};
    int measureCount = 0;
    int ePos = 0, runFrames = 0, tailFrames = 0;
    float prevDb = -120.0f, runPeak = -120.0f, runStartDb = -120.0f, tailStartDb = -120.0f, tailEndDb = 0.0f, eSmooth = -120.0f, rtEstimate = -1.0f;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mix;
    std::atomic<float> reductionMeter { 0.0f }, tailMeter { 0.5f };
};
} // namespace onirique
