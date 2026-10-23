#pragma once

#include <juce_dsp/juce_dsp.h>
#include <vector>
#include "Biquad.h"
#include "ResonanceSuppressor.h"

namespace onirique
{
/*  RESONANCE dans la chaîne suréchantillonnée (x2..x8) : son analyse spectrale ne sert que sous 20 kHz, elle tourne donc à
    la cadence de l'hôte. Le signal redescend par des demi-bandes (FIR équiondulation à phase linéaire, -110 dB, bande
    passante jusqu'à 0.45 x la cadence de l'hôte), passe dans RESONANCE (mêmes FFT, même résolution en Hz, mêmes temps
    qu'avant), puis remonte par les mêmes filtres. En x8, RESONANCE coûtait la moitié de toute la VOCALBOX.

    Latence (à la cadence suréchantillonnée) : celle de RESONANCE x le facteur + celle des filtres, arrondie à un multiple
    du facteur (le reste de la chaîne compense toute latence entière de la cadence de l'hôte).
    Sans suréchantillonnage : RESONANCE seule, rien ne change.
*/
class ResoMultirate
{
public:
    using Band = ResonanceSuppressor::Band;
    static constexpr int kNumBands = ResonanceSuppressor::kNumBands;

    void prepare (double sampleRate, int factor, int maxBlock)
    {
        k = juce::jmax (1, factor);
        stages = 0;
        while ((1 << stages) < k) ++stages;
        core.prepare (sampleRate / k);
        if (k == 1) { latency = core.getLatencySamples(); return; }

        // une demi-bande par étage ; la dernière (vers la cadence de l'hôte) est la plus raide
        for (int s = 0; s < stages; ++s)
        {
            const bool last = s == stages - 1;
            auto co = juce::dsp::FilterDesign<float>::designFIRLowpassHalfBandEquirippleMethod (last ? 0.05f : 0.20f, -110.0f);
            auto& st = stage[(size_t) s];
            st.h.assign (co->getRawCoefficients(), co->getRawCoefficients() + co->getFilterOrder() + 1);
            st.L = (int) st.h.size();
            jassert (st.L % 4 == 3);   // demi-bande : centre impair, coefficients non nuls aux indices pairs
            st.c = (st.L - 1) / 2;
            st.hc = st.h[(size_t) st.c];
            st.H = (st.L + 1) / 2;
            st.hEven.resize ((size_t) st.H);
            for (int q = 0; q < st.H; ++q) st.hEven[(size_t) q] = st.h[(size_t) (2 * q)];
            for (int c = 0; c < 2; ++c)
            {
                st.dn[c].assign ((size_t) (2 * st.L), 0.0f);
                st.upHist[c].assign ((size_t) (2 * st.H), 0.0f);
                st.dnPos[c] = st.upPos[c] = 0;
            }
        }
        // latence des filtres : (L - 1) / 2 à la descente + (L - 1) / 2 à la remontée, à la cadence de chaque étage
        int filt = 0;
        for (int s = 0; s < stages; ++s) filt += (stage[(size_t) s].L - 1) * (1 << s);
        filt -= k - 1;   // chaque descente garde le 2e échantillon de chaque paire : 1 échantillon d'avance par étage, à sa cadence
        const int raw = core.getLatencySamples() * k + filt;
        const int pad = (k - raw % k) % k;
        for (auto& d : padDelay) d.prepare (pad);
        latency = raw + pad;
        for (int c = 0; c < 2; ++c)
            for (auto& b : work[c]) b.assign ((size_t) maxBlock + 8, 0.0f);
    }

    // module rallumé après avoir dormi : rien de l'ancien signal ne reste dans les tampons
    void reset() noexcept
    {
        core.reset();
        for (auto& st : stage)
            for (int c = 0; c < 2; ++c)
            {
                std::fill (st.dn[c].begin(), st.dn[c].end(), 0.0f);
                std::fill (st.upHist[c].begin(), st.upHist[c].end(), 0.0f);
                st.phase[c] = 0;
            }
        for (auto& d : padDelay) d.reset();
    }

    int getLatencySamples() const noexcept { return latency; }
    void setParams (float amount01, const std::array<Band, kNumBands>& b) noexcept { core.setParams (amount01, b); }
    const ResonanceSuppressor::Meter& getMeter() const noexcept { return core.getMeter(); }

    void process (float* const* ch, int numCh, int n) noexcept
    {
        if (k == 1) { core.process (ch, numCh, n); return; }

        // descente : n -> n / k (n est un multiple du facteur : blocs suréchantillonnés)
        int len = n;
        for (int c = 0; c < numCh; ++c)
        {
            const float* src = ch[c];
            len = n;
            for (int s = 0; s < stages; ++s)
            {
                float* dst = work[c][(size_t) (s & 1)].data();
                len = decimate (stage[(size_t) s], c, src, dst, len);
                src = dst;
            }
            base[c] = const_cast<float*> (src);
        }
        core.process (base, numCh, len);

        // remontée : n / k -> n
        for (int c = 0; c < numCh; ++c)
        {
            const float* src = base[c];
            int m = len;
            for (int s = stages - 1; s >= 0; --s)
            {
                float* dst = s == 0 ? ch[c] : work[c][(size_t) ((s + 1) & 1)].data();
                if (dst == src) dst = work[c][2].data();
                m = interpolate (stage[(size_t) s], c, src, dst, m);
                src = dst;
            }
            if (src != ch[c]) std::copy (src, src + n, ch[c]);
            for (int i = 0; i < n; ++i) ch[c][i] = padDelay[c].process (ch[c][i]);
        }
    }

private:
    // demi-bande de longueur L = 4m + 3 : centre c = (L - 1) / 2 impair ; seuls le centre et les indices PAIRS sont non nuls
    struct Stage
    {
        std::vector<float> h, hEven, dn[2], upHist[2];
        float hc = 0.5f;
        int L = 1, c = 0, H = 1, dnPos[2] {}, upPos[2] {};
        int phase[2] {};   // descente : échantillon pair / impair
    };

    // descente par 2 : une sortie pour deux entrées ; seuls les coefficients non nuls sont calculés (historique doublé :
    // lecture contiguë)
    static int decimate (Stage& st, int ch, const float* in, float* out, int n) noexcept
    {
        auto& buf = st.dn[ch];
        int& p = st.dnPos[ch];
        int o = 0;
        for (int i = 0; i < n; ++i)
        {
            p = (p == 0 ? st.L : p) - 1;
            buf[(size_t) p] = buf[(size_t) p + (size_t) st.L] = in[i];
            if ((st.phase[ch] ^= 1) == 0)
            {
                const float* x = buf.data() + p;
                float acc = st.hc * x[st.c];
                for (int q = 0; q < st.H; ++q) acc += st.hEven[(size_t) q] * x[2 * q];
                out[o++] = acc;
            }
        }
        return o;
    }

    // remontée par 2 (zéros intercalés, gain 2), en polyphase : la sortie paire ne voit que les coefficients pairs, la
    // sortie impaire que le centre (un échantillon d'entrée retardé)
    static int interpolate (Stage& st, int ch, const float* in, float* out, int n) noexcept
    {
        auto& buf = st.upHist[ch];
        int& p = st.upPos[ch];
        int o = 0;
        const int dc = (st.c - 1) / 2;
        for (int i = 0; i < n; ++i)
        {
            p = (p == 0 ? st.H : p) - 1;
            buf[(size_t) p] = buf[(size_t) p + (size_t) st.H] = in[i];
            const float* x = buf.data() + p;
            float acc = 0.0f;
            for (int q = 0; q < st.H; ++q) acc += st.hEven[(size_t) q] * x[q];
            out[o++] = 2.0f * acc;
            out[o++] = 2.0f * st.hc * x[dc];
        }
        return o;
    }

    ResonanceSuppressor core;
    std::array<Stage, 3> stage;
    std::vector<float> work[2][3];
    float* base[2] {};
    DelayLine padDelay[2];
    int k = 1, stages = 0, latency = 0;
};
} // namespace onirique
