#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include "Comp1176.h"
#include "CompLA2A.h"

namespace onirique
{
/*  COMPRESSION : 1176 et LA-2A, en solo ou en duo (série), ordre réglable.

    - Les deux compresseurs tournent en permanence (états toujours à jour) ; chacun a son propre
      mélange traité/direct lissé sur 25 ms : passer de SOLO à DUO, ou d'un solo à l'autre, ne
      clique pas. Aucune latence.
    - Changement d'ordre (glisser-déposer dans l'interface) : la paire de compresseurs est clonée
      avec son état exact (réductions en cours, mémoire optique...) et la copie tourne dans le
      nouvel ordre en parallèle ; fondu de 30 ms de l'ancien ordre vers le nouveau, puis la copie
      devient la paire active. Ni clic ni saut de volume.
*/
class CompressorModule
{
public:
    enum Mode { fet = 0, opto, duo };

    void prepare (double sampleRate, int maxBlock)
    {
        for (auto& p : pairs) { p.fet.prepare (sampleRate); p.opto.prepare (sampleRate); }
        for (int c = 0; c < 2; ++c)
        {
            tmp[c].assign ((size_t) maxBlock, 0.0f);
            alt[c].assign ((size_t) maxBlock, 0.0f);
        }
        fetM.assign ((size_t) maxBlock, 0.0f);
        optoM.assign ((size_t) maxBlock, 0.0f);
        fetMix.reset (sampleRate, 0.025);
        optoMix.reset (sampleRate, 0.025);
        fadeLen = std::max (1, juce::roundToInt (0.03 * sampleRate));
        fading = false;
    }

    void setMode (int mode, int optoFirst, bool snap) noexcept
    {
        mode = juce::jlimit (0, 2, mode);
        const float f = (mode == fet || mode == duo) ? 1.0f : 0.0f;
        const float o = (mode == opto || mode == duo) ? 1.0f : 0.0f;
        if (snap) { fetMix.setCurrentAndTargetValue (f); optoMix.setCurrentAndTargetValue (o); }
        else      { fetMix.setTargetValue (f); optoMix.setTargetValue (o); }

        pendingOptoFirst = optoFirst != 0;
        if (snap) { isOptoFirst = pendingOptoFirst; fading = false; }
    }

    void set1176 (float inDb, float outDb, float attack, float release, int ratio, bool snap) noexcept
    {
        for (auto& p : pairs) p.fet.setParams (inDb, outDb, attack, release, ratio, snap);
    }

    void setLA2A (float gainDb, float peak, bool limit, bool snap) noexcept
    {
        for (auto& p : pairs) p.opto.setParams (gainDb, peak, limit, snap);
    }

    float get1176ReductionDb() const noexcept { return pairs[(size_t) active].fet.getGainReductionDb(); }
    float getLA2AReductionDb() const noexcept { return pairs[(size_t) active].opto.getGainReductionDb(); }

    void process (float* const* ch, int numCh, int n) noexcept
    {
        // mélanges solo/duo calculés une fois par bloc (partagés par les deux paires pendant un fondu)
        for (int i = 0; i < n; ++i)
        {
            fetM[(size_t) i] = fetMix.getNextValue();
            optoM[(size_t) i] = optoMix.getNextValue();
        }

        if (! fading && pendingOptoFirst != isOptoFirst)
        {
            auto& from = pairs[(size_t) active];
            auto& to = pairs[(size_t) (1 - active)];
            to.fet.copyStateFrom (from.fet);
            to.opto.copyStateFrom (from.opto);
            newOptoFirst = pendingOptoFirst;
            fadePos = 0;
            fading = true;
        }

        if (! fading)
        {
            runChain (pairs[(size_t) active], isOptoFirst, ch, numCh, n);
            return;
        }

        float* b[2] { alt[0].data(), alt[1].data() };
        for (int c = 0; c < numCh; ++c) std::copy (ch[c], ch[c] + n, b[c]);
        runChain (pairs[(size_t) active], isOptoFirst, ch, numCh, n);
        runChain (pairs[(size_t) (1 - active)], newOptoFirst, b, numCh, n);

        const float inc = 1.0f / (float) fadeLen;
        for (int i = 0; i < n; ++i)
        {
            const float t = std::min (1.0f, (float) (fadePos + i + 1) * inc);
            for (int c = 0; c < numCh; ++c) ch[c][i] += t * (b[c][i] - ch[c][i]);
        }
        fadePos += n;
        if (fadePos >= fadeLen)
        {
            active = 1 - active;
            isOptoFirst = newOptoFirst;
            fading = false;
        }
    }

private:
    struct Pair { FetCompressor fet; OptoCompressor opto; };

    void runChain (Pair& p, bool optoFirst, float* const* ch, int numCh, int n) noexcept
    {
        if (optoFirst) { runStage (p.opto, optoM, ch, numCh, n); runStage (p.fet, fetM, ch, numCh, n); }
        else           { runStage (p.fet, fetM, ch, numCh, n);   runStage (p.opto, optoM, ch, numCh, n); }
    }

    // chaque compresseur traite une copie ; son mélange (0..1) décide de sa présence (solo / duo)
    template <typename Comp>
    void runStage (Comp& comp, const std::vector<float>& mix, float* const* ch, int numCh, int n) noexcept
    {
        float* t[2] { tmp[0].data(), tmp[1].data() };
        for (int c = 0; c < numCh; ++c) std::copy (ch[c], ch[c] + n, t[c]);
        comp.process (t, numCh, n);
        for (int i = 0; i < n; ++i)
        {
            const float m = mix[(size_t) i];
            for (int c = 0; c < numCh; ++c) ch[c][i] += m * (t[c][i] - ch[c][i]);
        }
    }

    Pair pairs[2];
    int active = 0;
    std::vector<float> tmp[2], alt[2], fetM, optoM;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> fetMix, optoMix;
    bool isOptoFirst = false, pendingOptoFirst = false, newOptoFirst = false, fading = false;
    int fadeLen = 1, fadePos = 0;
};
} // namespace onirique
