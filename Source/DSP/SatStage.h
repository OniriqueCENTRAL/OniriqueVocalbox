#pragma once

#include <juce_dsp/juce_dsp.h>
#include <vector>
#include "Biquad.h"
#include "Common.h"

namespace onirique
{
// Détecteur de transitoires rapide/lent : 0 sur un signal tenu, -> 1 pendant les attaques.
struct TransientFollower
{
    void prepare (double fs) noexcept
    {
        auto k = [fs] (double ms) { return (float) (1.0 - std::exp (-1.0 / (ms * 0.001 * fs))); };
        fAtk = k (0.05); fRel = k (2.0);
        sAtk = k (3.0);  sRel = k (30.0);
        tAtk = k (0.04); tRel = k (12.0);
    }

    void reset() noexcept { envF = envS = t = 0.0f; }

    float process (float mx) noexcept
    {
        envF += (mx > envF ? fAtk : fRel) * (mx - envF);
        envS += (mx > envS ? sAtk : sRel) * (mx - envS);
        const float tr = juce::jlimit (0.0f, 1.0f, envF / (envS + 1.0e-4f) - 1.2f);
        t += (tr > t ? tAtk : tRel) * (tr - t);
        return t;
    }

    float fAtk = 0, fRel = 0, sAtk = 0, sRel = 0, tAtk = 0, tRel = 0;
    float envF = 0, envS = 0, t = 0;
};

/*  Base commune des étages de saturation (TUBE / ANALOG / TAPE).

    - Suréchantillonnage réglable OFF / x2 / x4 / x8 (FIR demi-bande, latence entière ; OFF en mode LIVE),
      identique pour tous les étages :
      même latence partout, le passage d'un type à l'autre ne décale rien. x8 garde les drives
      forts propres (pas de repliement audible).
    - DRIVE (0..10) est lissé ici et converti en gain d'attaque du waveshaper :
      0 dB -> +30 dB (k = 1 -> 31.6). À mi-course on retrouve l'ancien maximum, la moitié haute
      va beaucoup plus loin.
    - Compensation de niveau de la saturation ("drive auto-gain") : la puissance RMS (fenêtre
      ~50 ms) de l'entrée de l'étage, retardée de la latence, est comparée à celle de la sortie,
      et la sortie est corrigée en continu. Monter DRIVE ne change pas le volume : on n'entend
      que les harmoniques (et la couleur) arriver. Correction bornée à -30 / +12 dB, gelée sous
      -90 dBFS (silences), absente à DRIVE 0 (l'étage est alors transparent).
      Chaque étage applique en plus une normalisation statique, calée sur le niveau mesuré, pour
      que la correction dynamique n'ait qu'un ajustement fin à faire quand on tourne le bouton.
    - resetState() remet l'étage à zéro avant qu'il n'entre en fondu (un étage inactif ne
      calcule rien) ; la compensation converge alors en accéléré pendant 30 ms.
*/
class SatStage
{
public:
    int osOrder = 3, osFactor = 8;                    // facteur de suréchantillonnage : 2^osOrder (0 = OFF)
    static constexpr float maxDriveDb = 30.0f;
    static constexpr float nominal = 0.25f;           // niveau de départ de la normalisation statique (-12 dBFS)

    SatStage()
        : os (std::make_unique<juce::dsp::Oversampling<float>> (2, (size_t) 3, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, false, true)) {}
    virtual ~SatStage() = default;

    void prepare (double sampleRate, int maxBlock, int oversamplingOrder = 3)
    {
        fs = sampleRate;
        osOrder = juce::jlimit (0, 3, oversamplingOrder);
        osFactor = 1 << osOrder;
        os = std::make_unique<juce::dsp::Oversampling<float>> (2, (size_t) osOrder, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, false, true);
        os->initProcessing ((size_t) maxBlock);
        driveS.reset (fs, 0.08);
        powBuf.assign ((size_t) maxBlock, 0.0f);
        powDelay.prepare (getLatencySamples());
        detCoef  = coef (50.0);
        refCoef  = coef (300.0);
        fastCoef = coef (6.0);
        gainCoef = coef (15.0);
        warmLen  = juce::roundToInt (0.03 * fs);
        prepareStage (maxBlock);
        resetState();
    }

    void resetState() noexcept
    {
        os->reset();
        driveS.setCurrentAndTargetValue (driveS.getTargetValue());
        powDelay.reset();
        envIn = envOut = refEnv = 0.0f;
        compGain = 1.0f;
        refLevel = nominal;
        warm = warmLen;
        resetStage();
    }

    int getLatencySamples() const noexcept { return juce::roundToInt (os->getLatencyInSamples()); }

    void setDrive (float drive0to10, bool snap) noexcept
    {
        const float d = juce::jlimit (0.0f, 10.0f, drive0to10) * 0.1f;
        if (snap) driveS.setCurrentAndTargetValue (d);
        else      driveS.setTargetValue (d);
    }

    void process (float* const* ch, int numCh, int n) noexcept
    {
        const float invCh = 1.0f / (float) numCh;

        // puissance d'entrée, retardée de la latence pour rester alignée avec la sortie
        for (int i = 0; i < n; ++i)
        {
            float p = 0.0f;
            for (int c = 0; c < numCh; ++c) p += ch[c][i] * ch[c][i];
            powBuf[(size_t) i] = powDelay.process (p * invCh);
        }

        const float dBlock = driveS.getCurrentValue();
        processStage (ch, numCh, n);
        const float amt = std::min (1.0f, std::max (dBlock, driveS.getCurrentValue()) * 10.0f);

        const float gMin = dbToGain (-30.0f), gMax = dbToGain (12.0f);
        for (int i = 0; i < n; ++i)
        {
            float p = 0.0f;
            for (int c = 0; c < numCh; ++c) p += ch[c][i] * ch[c][i];

            const bool warming = warm > 0;
            const float k = warming ? fastCoef : detCoef;
            if (warm > 0) --warm;
            envIn  += k * (powBuf[(size_t) i] - envIn);
            envOut += k * (p * invCh - envOut);
            refEnv += (warming ? fastCoef : refCoef) * (powBuf[(size_t) i] - refEnv);

            if (envIn > 1.0e-9f)   // ~ -90 dBFS : en dessous, on garde la dernière correction
            {
                const float target = juce::jlimit (gMin, gMax, std::sqrt (envIn / (envOut + 1.0e-12f)));
                compGain += (warming ? fastCoef : gainCoef) * (target - compGain);
            }

            const float g = 1.0f + amt * (compGain - 1.0f);
            for (int c = 0; c < numCh; ++c) ch[c][i] *= g;
        }

        // niveau crête estimé de l'entrée, utilisé par la normalisation statique du bloc suivant
        if (refEnv > 1.0e-9f)
            refLevel = juce::jlimit (0.02f, 1.0f, std::sqrt (2.0f * refEnv));
    }

protected:
    virtual void processStage (float* const* ch, int numCh, int n) noexcept = 0;
    virtual void prepareStage (int maxBlock) = 0;
    virtual void resetStage() noexcept = 0;

    float coef (double ms) const noexcept
    {
        return (float) (1.0 - std::exp (-1.0 / (ms * 0.001 * fs)));
    }

    // gain d'attaque du waveshaper pour d = DRIVE / 10
    static float driveGain (float d) noexcept { return dbToGain (maxDriveDb * d); }

    double fs = 44100.0;
    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> driveS;
    float refLevel = nominal;                         // crête estimée de l'entrée (suit le signal)

private:
    std::vector<float> powBuf;
    DelayLine powDelay;
    float detCoef = 0, fastCoef = 0, gainCoef = 0, refCoef = 0;
    float envIn = 0, envOut = 0, refEnv = 0, compGain = 1.0f;
    int warm = 0, warmLen = 0;
};
} // namespace onirique
