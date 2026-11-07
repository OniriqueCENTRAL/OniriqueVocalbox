#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <vector>
#include "Biquad.h"
#include "Common.h"

namespace onirique
{
/*  DE-CLICK : les bruits de bouche (clics de langue, salive, lèvres qui se décollent, craquements) retirés sans toucher
    aux consonnes ni au grain de la voix.

    Ce qui distingue un clic de bouche :
    - il est très court (moins de 4,5 ms, filtre compris) et vit dans l'aigu (au-dessus de 3 kHz) ;
    - son énergie y dépasse nettement l'aigu qui l'entoure (puissance sur 0,25 ms comparée à celle des 20 ms autour,
      qu'un clic ne fait presque pas monter). Un S, un CH ou un souffle sont du bruit : leur puissance ne fait jamais
      un tel saut, et une salve plus longue que 4,5 ms (un T, un K) n'est pas un clic ;
    - il est isolé : la voix, elle, envoie dans l'aigu une petite impulsion à chaque période (la fermeture des cordes
      vocales). Une suite d'impulsions régulières (2,5 .. 40 ms d'écart, fry compris) est la voix, pas des clics ;
    - au milieu d'une voyelle forte (beaucoup de grave), le seuil monte de 6 dB : un clic y est masqué, la voix passe.
    SENSITIVITY règle le saut qui compte pour un clic (20 dB .. 9 dB).

    Réparation : le son est séparé en grave (sous 1,5 kHz) + aigu (la somme rend exactement le son d'origine) ; pendant le clic, seul
    l'aigu est ramené au niveau de l'aigu d'autour, avec des fondus de 0,6 ms. 8 ms d'avance : la décision est prise avant
    que le son ne sorte. AMOUNT : jusqu'où le clic descend (jusqu'à -40 dB). LISTEN : seulement ce qui est retiré. */
class DeClick
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        look = std::max (8, (int) std::lround (0.008 * fs));
        maxLen = std::max (4, (int) std::lround (0.0045 * fs));
        gapLen = std::max (2, (int) std::lround (0.0004 * fs));
        pad = std::max (2, (int) std::lround (0.0006 * fs));
        settle = std::max (8, (int) std::lround (0.002 * fs));
        ringSize = 1;
        while (ringSize < look + maxLen + 4 * pad + 64) ringSize <<= 1;
        gainRing.assign ((size_t) ringSize, 1.0f);
        for (auto& d : delay) d.prepare (look);
        detHp[0].setCoeffs (BiquadCoeffs::highpass (fs, 3000.0, 0.54));
        detHp[1].setCoeffs (BiquadCoeffs::highpass (fs, 3000.0, 1.31));
        detLp.setCoeffs (BiquadCoeffs::lowpass (fs, 900.0, 0.707));
        for (auto& s : split) s.setCoeffs (BiquadCoeffs::lowpass (fs, 1500.0, 0.5));
        cShort = (float) std::exp (-1.0 / (0.00025 * fs));
        cLong = (float) std::exp (-1.0 / (0.020 * fs));
        cLowS = (float) std::exp (-1.0 / (0.0015 * fs));
        reset();
    }

    int getLatencySamples() const noexcept { return look; }

    void reset() noexcept
    {
        std::fill (gainRing.begin(), gainRing.end(), 1.0f);
        for (auto& d : delay) d.reset();
        for (auto& b : detHp) b.reset();
        detLp.reset();
        for (auto& s : split) s.reset();
        pend = {}; pS = pL = pLow = pLowS = 0.0f; runLen = 0; gap = 0; runPeak = 0.0f; runRef = 0.0f; inIdx = 0;
        events.fill (-1000000); evPos = 0;
    }

    void setParams (float sensitivity01, float amount01, bool listenRemoved) noexcept
    {
        sens = juce::jlimit (0.0f, 1.0f, sensitivity01);
        amount = juce::jlimit (0.0f, 1.0f, amount01);
        listen = listenRemoved;
        ratioPow = std::pow (10.0f, (20.0f - 11.0f * sens) / 10.0f);
        gMin = dbToGain (-40.0f * amount);
    }

    void process (float* const* ch, int numCh, int n) noexcept
    {
        const int mask = ringSize - 1;
        for (int i = 0; i < n; ++i)
        {
            // ---- détection, sur le son qui entre (8 ms d'avance sur ce qui sort)
            const float mono = numCh > 1 ? 0.5f * (ch[0][i] + ch[1][i]) : ch[0][i];
            const float h = detHp[1].process (detHp[0].process (mono, 0), 0);
            const float l = detLp.process (mono, 0);
            const float h2 = h * h;
            pS = cShort * pS + (1.0f - cShort) * h2;
            pL = cLong * pL + (1.0f - cLong) * std::min (h2, 4.0f * pL + 1.0e-12f);   // un clic ne fait presque pas monter l'entourage
            // le grave : un clic de bouche n'en a presque pas, une impulsion de la voix (début de note) si
            pLowS = cLowS * pLowS + (1.0f - cLowS) * l * l;
            pLow = cLong * pLow + (1.0f - cLong) * std::min (l * l, 4.0f * pLow + 1.0e-12f);
            // voyelle forte : le grave domine largement l'aigu, le seuil monte de 6 dB
            const float need = ratioPow * (pLow > 30.0f * pL ? 4.0f : 1.0f);
            const float ref = std::max (pL, 1.0e-10f);   // -100 dB : un clic dans le silence compte aussi
            const bool hot = amount > 0.0005f && pS > need * ref && pS > 1.0e-9f;

            if (hot)
            {
                if (runLen == 0) { runRef = ref; runPeak = 0.0f; runLowJump = 0.0f; runLowRef = std::max (pLow, 1.0e-10f); }
                runLen += gap + 1; gap = 0;
                runPeak = std::max (runPeak, pS);
                runLowJump = std::max (runLowJump, pLowS / runLowRef);
            }
            else if (runLen > 0 && ++gap > gapLen)
            {
                const int end = inIdx - gap, start = end - runLen;
                const int centre = (start + end) / 2;
                const bool regular = periodic (centre);
                events[(size_t) evPos] = centre; evPos = (evPos + 1) % (int) events.size();
                // le grave a sauté avec l'aigu (au quart près) : la voix qui démarre, pas un clic
                const bool voiceBurst = runLowJump * 4.0f > runPeak / runRef && pLowS > 1.0e-8f;
                if (runLen <= maxLen && ! regular && ! voiceBurst)
                {
                    if (pend.active) decide (mask);   // une décision encore en attente : prise maintenant
                    // décision 2 ms plus tard : si l'aigu reste haut après la salve, c'est le début d'une consonne (T, K, S)
                    pend = { true, start, end, inIdx + settle, runRef,
                             juce::jlimit (gMin, 1.0f, std::sqrt (1.0f * runRef / std::max (runPeak, 1.0e-20f))) };
                }
                runLen = 0; gap = 0;
            }
            if (pend.active && inIdx >= pend.at) decide (mask);

            // ---- réparation, sur le son retardé : grave intact, aigu ramené pendant le clic
            const int outIdx = inIdx - look;
            float& slot = gainRing[(size_t) (outIdx & mask)];
            const float g = slot;
            slot = 1.0f;
            for (int c = 0; c < numCh; ++c)
            {
                const float x = delay[c].process (ch[c][i]);
                const float lo = split[c].process (x, c);
                const float hi = x - lo;
                ch[c][i] = listen ? (1.0f - g) * hi : lo + g * hi;
            }
            if (++inIdx >= (1 << 30))
            {
                // rester aligné sur l'anneau et sur les événements gardés
                const int shift = 1 << 29;
                inIdx -= shift;
                for (auto& e : events) e -= shift;
                pend.start -= shift; pend.end -= shift; pend.at -= shift;
            }
        }
    }

    int takeClicks() noexcept { return clicks.exchange (0, std::memory_order_relaxed); }
    float lastClickDb() const noexcept { return lastDepth.load (std::memory_order_relaxed); }

private:
    struct Pending { bool active = false; int start = 0, end = 0, at = 0; float ref = 0.0f, g = 1.0f; };

    // un clic isolé : l'aigu est retombé au niveau d'autour (sinon une consonne commence) ; l'aigu est ramené (en amplitude :
    // racine du rapport des puissances), fondus de 0,6 ms
    void decide (int mask) noexcept
    {
        pend.active = false;
        if (pS > 4.0f * pend.ref) return;
        for (int j = pend.start - pad; j <= pend.end + pad; ++j)
        {
            const float w = j < pend.start ? (float) (j - (pend.start - pad)) / (float) pad
                          : j > pend.end ? (float) ((pend.end + pad) - j) / (float) pad : 1.0f;
            const float gj = 1.0f - sCurve (juce::jlimit (0.0f, 1.0f, w)) * (1.0f - pend.g);
            float& slot = gainRing[(size_t) (j & mask)];
            slot = std::min (slot, gj);
        }
        clicks.fetch_add (1, std::memory_order_relaxed);
        lastDepth.store (gainToDb (pend.g), std::memory_order_relaxed);
    }

    Pending pend;
    int settle = 96;

    // la voix : des impulsions régulières ; deux écarts voisins (2,5 .. 40 ms) à 20 % près avant celle-ci
    bool periodic (int t) const noexcept
    {
        const int lo = (int) (0.0025 * fs), hi = (int) (0.040 * fs);   // jusqu'à la voix grave et au fry (25 Hz)
        for (int a = 0; a < (int) events.size(); ++a)
        {
            const int d1 = t - events[(size_t) a];
            if (d1 < lo || d1 > hi) continue;
            for (int b = 0; b < (int) events.size(); ++b)
            {
                const int d2 = events[(size_t) a] - events[(size_t) b];
                if (d2 >= lo && d2 <= hi && std::abs (d2 - d1) < 0.2f * (float) d1) return true;
            }
        }
        return false;
    }

    double fs = 48000.0;
    int look = 384, maxLen = 144, gapLen = 19, pad = 29, ringSize = 1024;
    std::vector<float> gainRing;
    DelayLine delay[2];
    Biquad detHp[2], detLp, split[2];
    float cShort = 0.9f, cLong = 0.999f;
    float pS = 0.0f, pL = 0.0f, pLow = 0.0f, pLowS = 0.0f, runPeak = 0.0f, runRef = 0.0f, runLowJump = 0.0f, runLowRef = 1.0f, cLowS = 0.95f;
    int runLen = 0, gap = 0, inIdx = 0;
    std::array<int, 12> events {};
    int evPos = 0;
    float sens = 0.5f, amount = 0.0f, ratioPow = 30.0f, gMin = 0.01f;
    bool listen = false;
    std::atomic<int> clicks { 0 };
    std::atomic<float> lastDepth { 0.0f };
};
} // namespace onirique
