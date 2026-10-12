#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>

#include "License/LicenseGuard.h"
#include "Parameters.h"
#include "DSP/VocalChain.h"
#include "DSP/SpectrumTap.h"
#include "PresetManager.h"

class OniriqueVocalBoxAudioProcessor : public juce::AudioProcessor,
                                       private juce::AudioProcessorValueTreeState::Listener,
                                       private juce::AsyncUpdater
{
public:
    OniriqueVocalBoxAudioProcessor();
    ~OniriqueVocalBoxAudioProcessor() override;

    bool isLiveMode() const noexcept { return liveNow; }
    int getOversamplingOrder() const noexcept { return osNow; }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    // Le bypass du DAW pilote directement le paramètre "Bypass" (fondu propre + latence alignée)
    juce::AudioProcessorParameter* getBypassParameter() const override { return apvts.getParameter (onirique::ids::bypass); }

    const juce::String getName() const override { return "ONIRIQUE VOCALBOX"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    onirique::PresetManager presets { apvts };
    onirique::SpectrumTap inputTap, outputTap;   // analyseur : entrée et sortie du plugin (mono)

    // lus par l'éditeur (thread GUI) : crête de sortie depuis le dernier appel, correction d'Auto Gain
    float consumeOutputPeak() noexcept { return outPeak.exchange (0.0f); }
    float getAutoGainDb() const noexcept { return autoGainDbShown.load(); }
    const onirique::ResonanceSuppressor::Meter& getResonanceMeter() const noexcept { return chain.getResonanceMeter(); }
    float get1176ReductionDb() const noexcept { return chain.get1176ReductionDb(); }
    float getLA2AReductionDb() const noexcept { return chain.getLA2AReductionDb(); }
    float getPlosiveReductionDb() const noexcept { return chain.getPlosiveReductionDb(); }
    float getParallelReductionDb() const noexcept { return chain.getParallelReductionDb(); }
    float getDeEssReductionDb() const noexcept { return chain.getDeEssReductionDb(); }
    float getDeEss2ReductionDb() const noexcept { return chain.getDeEss2ReductionDb(); }
    float consumeInputLevel (int c) noexcept  { return chain.consumeInputLevel (c); }
    float consumeOutputLevel (int c) noexcept { return chain.consumeOutputLevel (c); }
    const onirique::MultibandOpto::Meter& getMultibandMeter() const noexcept { return chain.getMultibandMeter(); }

private:
    onirique::VocalChain::Params readParams() const noexcept;

    std::atomic<float>* pInput = nullptr;
    std::atomic<float>* pLow = nullptr;
    std::atomic<float>* pMud = nullptr;
    std::atomic<float>* pHigh = nullptr;
    std::atomic<float>* pDeessFreq = nullptr;
    std::atomic<float>* pDeessAmt = nullptr;
    std::atomic<float>* pDrive = nullptr;
    std::atomic<float>* pSatType = nullptr;
    std::atomic<float>* pLowFreq = nullptr;
    std::atomic<float>* pHighFreq = nullptr;
    std::atomic<float>* pSmartS = nullptr;
    std::atomic<float>* pLowCut = nullptr;
    std::atomic<float>* pMbOn = nullptr;
    std::atomic<float>* pCompMaster = nullptr;
    std::atomic<float>* pParOn = nullptr;
    std::atomic<float>* pParAmount = nullptr;
    std::atomic<float> *pParAuto = nullptr, *pParThresh = nullptr, *pParAttack = nullptr, *pParRelease = nullptr;
    std::atomic<float> *pDeess2On = nullptr, *pDeess2Freq = nullptr, *pDeess2Amt = nullptr;
    std::atomic<float>* pMbX[3] {};
    std::atomic<float>* pMbThr[4] {};
    std::atomic<float>* pMbGain[4] {};
    std::atomic<float>* pMbAttack = nullptr;
    std::atomic<float>* pMbRelease = nullptr;
    std::atomic<float>* pPeq[6][5] {};   // on, type, freq, gain, q
    std::atomic<float>* pComp[11] {};   // compOn, mode, order, 1176 in/out/attack/release/ratio, LA-2A gain/peak/limit
    std::atomic<float>* pResoAmt = nullptr;
    std::atomic<float>* pResoLow = nullptr;
    std::atomic<float>* pResoMid = nullptr;
    std::atomic<float>* pResoHigh = nullptr;
    std::atomic<float>* pResoFreq[3] {};
    std::atomic<float>* pResoWidth[3] {};
    std::atomic<float>* pEqOn = nullptr;
    std::atomic<float>* pResoOn = nullptr;
    std::atomic<float>* pDeessOn = nullptr;
    std::atomic<float>* pSatOn = nullptr;
    std::atomic<float>* pOutput = nullptr;
    std::atomic<float>* pAutoGain = nullptr;
    std::atomic<float>* pBypass = nullptr;
    std::atomic<float>* pLive = nullptr;
    std::atomic<float>* pOversampling = nullptr;
    std::atomic<float>* pPlosive = nullptr;
    std::atomic<float>* pResoListen = nullptr;
    std::atomic<float>* pChainOrder = nullptr;
    std::atomic<float>* pMbSolo[4] {};

    // MIX / LIVE et suréchantillonnage changent la latence : la chaîne est reconfigurée sur le thread
    // de l'interface (audio suspendu le temps du changement), puis la nouvelle latence est annoncée au DAW
    void parameterChanged (const juce::String&, float) override { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override;
    void configureChain (double sampleRate);
    bool liveNow = false;
    int osNow = 3;
    double currentRate = 0.0;

    onirique::VocalChain chain;
    std::atomic<float> outPeak { 0.0f }, autoGainDbShown { 0.0f };

public:
    /** Licence ONIRIQUE (acheté, essai, démo) : voir License/LicenseGuard.h. */
    onirique::license::Guard license { "vocalbox" };

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OniriqueVocalBoxAudioProcessor)
};
