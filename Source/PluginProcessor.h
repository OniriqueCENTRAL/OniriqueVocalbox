#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>

#include "License/LicenseGuard.h"
#include "Parameters.h"
#include "Modules.h"
#include "DSP/ModularChain.h"
#include "DSP/SpectrumTap.h"
#include "StockPresets.h"

class OniriqueVocalBoxAudioProcessor : public juce::AudioProcessor,
                                       private juce::AudioProcessorValueTreeState::Listener,
                                       private juce::AsyncUpdater,
                                       private juce::Timer
{
public:
    OniriqueVocalBoxAudioProcessor();
    ~OniriqueVocalBoxAudioProcessor() override;

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
    /** Les presets d'usine et ceux de l'utilisateur, la comparaison A/B : ce que montre la barre du haut. */
    onirique::PresetBank presets { apvts, "VocalBox", onirique::vocalbox::stockPresets(), onirique::vocalbox::tagNames(),
                                   onirique::vocalbox::notStored() };
    onirique::SpectrumTap inputTap, outputTap;   // analyseur : entrée et sortie du plugin (mono)

    // ---- la chaîne, côté interface (thread de l'interface)
    onirique::modules::Chain getChain() const;
    void setChain (const onirique::modules::Chain&);
    /** Ajoute un module (exemplaire libre suivant) en fin de chaîne, réglages d'usine, allumé ; rend son code (0 : plus de place). */
    int addModule (int type);
    void removeModule (int code);
    void moveModule (int fromIndex, int toIndex);
    onirique::ChainModule* module (int code) const noexcept
    {
        return chain.module (onirique::modules::typeOf (code), onirique::modules::instanceOf (code));
    }
    juce::RangedAudioParameter* param (const char* id, int instance) const { return apvts.getParameter (onirique::ids::inst (id, instance)); }

    // lus par l'éditeur (thread GUI) : crête de sortie depuis le dernier appel, correction d'Auto Gain
    float consumeOutputPeak() noexcept { return outPeak.exchange (0.0f); }
    float getAutoGainDb() const noexcept { return autoGainDbShown.load(); }
    float consumeInputLevel (int c) noexcept  { return chain.consumeInputLevel (c); }
    float consumeOutputLevel (int c) noexcept { return chain.consumeOutputLevel (c); }
    double getHostBpm() const noexcept { return hostBpm.load(); }

    // écoute d'une bande de l'EQ paramétrique : vraie seulement pendant qu'on tient une bande
    std::atomic<bool> peqListenHold { false };

    // AUTO MIX : la prise écoutée (mono, 10 s de voix ; les blocs silencieux ne comptent pas)
    void startCapture() noexcept { capPos.store (0); capturing.store (capLen > 0, std::memory_order_release); }
    void stopCapture() noexcept { capturing.store (false); }
    float captureProgress() const noexcept { return capLen > 0 ? (float) capPos.load() / (float) capLen : 0.0f; }
    bool captureDone() const noexcept { return capLen > 0 && capPos.load (std::memory_order_acquire) >= capLen; }
    // à lire seulement quand captureDone() (le thread audio n'y écrit plus)
    const std::vector<float>& capturedTake() const noexcept { return capBuf; }
    double captureRate() const noexcept { return currentRate; }

    // L'APPRENTISSAGE (seulement si l'utilisateur l'a allumé, menu ⋯) : le mix monté par AUTO MIX attend sa note
    // (👍 / 👎) ou 10 minutes de retouches, puis part au serveur, anonyme, sans audio. Thread de l'interface.
    void learnArm (juce::var payload, std::vector<std::pair<juce::String, float>> applied);
    bool learnPending() const noexcept { return learnPayload.isObject(); }
    void learnSend (int rating);   // -1, 0 (pas de note), +1 : avec les retouches, puis oublié
    // une retouche dite en mots après le mix (« moins de réverbe ») : jointe au retour, c'est ce qui apprend le plus au cerveau
    void learnRefine (const juce::String& text)
    {
        if (! learnPayload.isObject()) return;
        auto arr = learnPayload["refine"];
        if (! arr.isArray()) { learnPayload.getDynamicObject()->setProperty ("refine", juce::Array<juce::var>()); arr = learnPayload["refine"]; }
        if (auto* a = arr.getArray(); a != nullptr && a->size() < 12) a->add (text.substring (0, 200));
    }
    // AUTO MIX vient de monter la chaîne : l'interface ouvre l'écran AUTO MIX (les macros, affiner en mots)
    std::atomic<bool> showMixPage { false };

private:
    std::atomic<float>* pBypass = nullptr;
    std::atomic<float>* pOversampling = nullptr;
    std::atomic<double> hostBpm { 120.0 };

    // le suréchantillonnage change la latence : la chaîne est reconfigurée sur le thread de l'interface (audio suspendu le
    // temps du changement), puis la nouvelle latence est annoncée au DAW
    void parameterChanged (const juce::String&, float) override { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override;
    void configureChain (double sampleRate);
    // la chaîne change : sa latence est annoncée au DAW (thread de l'interface)
    void timerCallback() override;
    int osNow = 3;
    double currentRate = 0.0;

    onirique::ModularChain chain { apvts, hostBpm, peqListenHold };
    std::atomic<float> outPeak { 0.0f }, autoGainDbShown { 0.0f };
    std::vector<float> capBuf;
    juce::var learnPayload;
    std::vector<std::pair<juce::String, float>> learnApplied;   // les réglages posés par AUTO MIX
    double learnAt = 0.0;
    juce::ThreadPool learnPool { 1 };   // l'envoi, hors de l'interface ; attendu à la fermeture
    std::atomic<int> capPos { 0 };
    std::atomic<bool> capturing { false };
    int capLen = 0;

public:
    /** Licence ONIRIQUE (acheté, essai, démo) : voir License/LicenseGuard.h. */
    onirique::license::Guard license { "vocalbox" };

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OniriqueVocalBoxAudioProcessor)
};
