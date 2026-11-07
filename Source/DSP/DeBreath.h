#pragma once

#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <vector>
#include "Biquad.h"
#include "Common.h"

namespace onirique
{
/*  DE-BREATH : les respirations baissées (pas coupées : la voix garde son naturel), sans toucher aux S, aux F ni aux fins
    de mots.

    Une respiration, c'est un souffle : aucune hauteur (pas de période), plus faible que la voix (entre -8 et -55 dB sous
    ses phrases), sans le pic d'aigu d'une sifflante, et qui dure (plus de 70 ms). Toutes les 5 ms :
    - la hauteur : autocorrélation normalisée sur 30 ms (le son ramené à 8 kHz), entre 70 et 500 Hz ;
    - le niveau, comparé à celui des phrases chantées (suivi sur quelques secondes) ;
    - la part de l'aigu au-dessus de 5 kHz (une sifflante y a l'essentiel de son énergie, un souffle non).
    Une suite d'au moins 70 ms de trames « souffle » est une respiration ; 100 ms d'avance permettent de la baisser
    dès son début. Le gain glisse (10 ms à la descente, 25 ms à la remontée).
    - REDUCTION : de combien (0 .. 30 dB). SENSITIVITY : jusqu'à quel niveau un souffle compte. LISTEN : seulement les
      respirations. */
class DeBreath
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        hopLen = std::max (16, (int) std::lround (0.005 * fs));
        look = 20 * hopLen;   // 100 ms
        decim = std::max (1, (int) std::lround (fs / 8000.0));
        for (auto& d : delay) d.prepare (look);
        aa[0].setCoeffs (BiquadCoeffs::lowpass (fs, 3000.0, 0.54));
        aa[1].setCoeffs (BiquadCoeffs::lowpass (fs, 3000.0, 1.31));
        hf.setCoeffs (BiquadCoeffs::highpass (fs, 5000.0, 0.707));
        lc.setCoeffs (BiquadCoeffs::highpass (fs, 120.0, 0.707));
        dsRing.assign ((size_t) kDs, 0.0f);
        flags.assign ((size_t) kFrames, 0);
        marks.assign ((size_t) kFrames, 0);
        attC = (float) std::exp (-1.0 / (0.010 * fs));
        relC = (float) std::exp (-1.0 / (0.025 * fs));
        reset();
    }

    int getLatencySamples() const noexcept { return look; }

    void reset() noexcept
    {
        for (auto& d : delay) d.reset();
        for (auto& b : aa) b.reset();
        hf.reset(); lc.reset();
        std::fill (dsRing.begin(), dsRing.end(), 0.0f);
        std::fill (flags.begin(), flags.end(), (char) 0);
        std::fill (marks.begin(), marks.end(), (char) 0);
        dsPos = 0; dsPhase = 0; hopCount = 0; frameIdx = 0; run = 0; gapRun = 0;
        eAll = eHf = 0.0; voiceLevel = 1.0e-3f; gNow = 1.0f; outCount = 0; counted = false; breathCount = 0; breathLevelSum = 0.0f;
    }

    void setParams (float reductionDb, float sensitivity01, bool listenOnly) noexcept
    {
        target = dbToGain (-juce::jlimit (0.0f, 30.0f, reductionDb));
        sens = juce::jlimit (0.0f, 1.0f, sensitivity01);
        listen = listenOnly;
        active = reductionDb > 0.05f;
    }

    void process (float* const* ch, int numCh, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const float mono = lc.process (numCh > 1 ? 0.5f * (ch[0][i] + ch[1][i]) : ch[0][i], 0);
            const float a = aa[1].process (aa[0].process (mono, 0), 0);
            if (++dsPhase >= decim) { dsPhase = 0; dsRing[(size_t) dsPos] = a; dsPos = (dsPos + 1) & (kDs - 1); }
            const float h = hf.process (mono, 0);
            eAll += (double) mono * mono;
            eHf += (double) h * h;
            if (++hopCount >= hopLen) { hopCount = 0; analyse(); }

            // le son retardé de 100 ms : sa trame est celle d'il y a 20 analyses
            const int outFrame = frameIdx - 20;
            const bool breath = outFrame >= 0 && marks[(size_t) (outFrame & (kFrames - 1))] != 0;
            const float t = breath && active ? target : 1.0f;
            gNow = t < gNow ? attC * gNow + (1.0f - attC) * t : relC * gNow + (1.0f - relC) * t;
            for (int c = 0; c < numCh; ++c)
            {
                const float x = delay[c].process (ch[c][i]);
                ch[c][i] = listen ? x * (1.0f - gNow) : x * gNow;
            }
        }
        meter.store (gainToDb (std::max (gNow, 1.0e-4f)), std::memory_order_relaxed);
    }

    float getReductionDb() const noexcept { return meter.load (std::memory_order_relaxed); }   // négatif pendant une respiration
    // pour AUTO MIX (analyse hors ligne de la prise) : respirations trouvées et leur niveau moyen sous les phrases
    int breathsFound() const noexcept { return breathCount; }
    float breathLevelDb() const noexcept { return breathCount > 0 ? breathLevelSum / (float) breathCount : -60.0f; }
    bool breathNow() const noexcept { return meter.load (std::memory_order_relaxed) < -1.0f; }

private:
    static constexpr int kDs = 512, kFrames = 128, kWin = 240, kMinLag = 16, kMaxLag = 114;

    void analyse() noexcept
    {
        const float rms = (float) std::sqrt (eAll / hopLen);
        const float hfShare = (float) (eHf / (eAll + 1.0e-20));
        eAll = eHf = 0.0;

        // hauteur : autocorrélation normalisée sur les 30 dernières ms à 8 kHz
        float best = 0.0f;
        const int base = dsPos - kWin + kDs;
        double e0 = 0.0;
        for (int j = 0; j < kWin; ++j) { const float v = dsRing[(size_t) ((base + j) & (kDs - 1))]; e0 += (double) v * v; }
        if (e0 > 1.0e-12)
            for (int lag = kMinLag; lag <= kMaxLag; ++lag)
            {
                double xy = 0.0, e1 = 0.0;
                for (int j = 0; j < kWin; ++j)
                {
                    const float v = dsRing[(size_t) ((base + j) & (kDs - 1))], w = dsRing[(size_t) ((base + j - lag) & (kDs - 1))];
                    xy += (double) v * w; e1 += (double) w * w;
                }
                best = std::max (best, (float) (xy / std::sqrt (e0 * e1 + 1.0e-20)));
            }

        const bool voiced = best > 0.62f;
        // niveau des phrases : suit les trames chantées / parlées, redescend lentement (en ~4 s)
        if (voiced && rms > voiceLevel) voiceLevel = rms;
        else voiceLevel = std::max (1.0e-4f, voiceLevel * 0.99375f);

        const float upper = voiceLevel * dbToGain (-14.0f + 8.0f * sens), lower = voiceLevel * dbToGain (-55.0f);
        const bool breathy = best < 0.45f + 0.1f * sens && rms < upper && rms > lower && hfShare < 0.55f;

        const int f = frameIdx & (kFrames - 1);
        flags[(size_t) f] = breathy ? 1 : 0;
        marks[(size_t) f] = 0;
        if (breathy) { run += 1 + gapRun; gapRun = 0; }
        else if (run > 0 && ++gapRun > 1) { run = 0; gapRun = 0; }
        // une suite assez longue : toute la suite (et la trame qui la précède) est une respiration
        if (run >= 14)
            for (int b = 0; b <= std::min (run + 1, 19); ++b) marks[(size_t) ((frameIdx - b) & (kFrames - 1))] = 1;
        if (run >= 14 && ! counted) { counted = true; ++breathCount; breathLevelSum += gainToDb (std::max (rms, 1.0e-6f) / voiceLevel); }
        if (run == 0) counted = false;
        ++frameIdx;
        if (frameIdx > (1 << 30)) frameIdx -= (1 << 29);
    }

    double fs = 48000.0;
    int hopLen = 240, look = 4800, decim = 6;
    DelayLine delay[2];
    Biquad aa[2], hf, lc;
    std::vector<float> dsRing;
    std::vector<char> flags, marks;
    int dsPos = 0, dsPhase = 0, hopCount = 0, frameIdx = 0, run = 0, gapRun = 0, outCount = 0;
    double eAll = 0.0, eHf = 0.0;
    float voiceLevel = 1.0e-3f, gNow = 1.0f, attC = 0.99f, relC = 0.999f, target = 0.25f, sens = 0.5f;
    bool listen = false, active = false, counted = false;
    int breathCount = 0;
    float breathLevelSum = 0.0f;
    std::atomic<float> meter { 0.0f };
};
} // namespace onirique
