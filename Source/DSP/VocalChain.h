#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "Common.h"
#include "Parameters.h"   // ordre de traitement (decodeOrder)
#include "VocalEQ.h"
#include "ParametricEQ.h"
#include "ResoMultirate.h"
#include "DeEsser.h"
#include "DePlosive.h"
#include "CompressorModule.h"
#include "ParallelComp.h"
#include "MultibandOpto.h"
#include "SaturationLab.h"
#include "AutoGain.h"
#include "NoiseGate.h"
#include "NoiseSuppressor.h"
#include "VocalFX.h"

namespace onirique
{
/*  Interrupteur de module (bouton power) : fondu de 20 ms entre le signal traité et le signal
    d'entrée du module retardé de sa latence. Le module tourne en permanence (ses états restent
    à jour) : la latence totale du plugin ne change jamais et la remise en route est instantanée.
*/
class ModuleSwitch
{
public:
    void prepare (double fs, int latencySamples, int maxBlock)
    {
        latency = latencySamples;
        for (int c = 0; c < 2; ++c)
        {
            delay[c].prepare (latencySamples);
            dry[c].assign ((size_t) maxBlock, 0.0f);
        }
        mix.reset (fs, 0.02);
        asleep = false;
        warm = 0;
    }

    void set (bool on, bool snap) noexcept
    {
        if (snap) mix.setCurrentAndTargetValue (on ? 1.0f : 0.0f);
        else      mix.setTargetValue (on ? 1.0f : 0.0f);
    }

    template <typename Fn>
    void process (float* const* ch, int numCh, int n, Fn&& runModule) noexcept
    {
        process (ch, numCh, n, std::forward<Fn> (runModule), [] {});
    }

    // wake : remise à zéro du module quand il est rallumé après avoir dormi
    template <typename Fn, typename Wake>
    void process (float* const* ch, int numCh, int n, Fn&& runModule, Wake&& wake) noexcept
    {
        // module coupé (fondu terminé) : il n'est pas calculé du tout (avec le suréchantillonnage, un module coupé coûtait
        // autant qu'un module allumé). Seule sa ligne à retard tourne : la latence et l'alignement ne changent pas.
        if (! mix.isSmoothing() && mix.getTargetValue() <= 0.0f)
        {
            asleep = true;
            for (int c = 0; c < numCh; ++c)
                for (int i = 0; i < n; ++i)
                    ch[c][i] = dry[c][(size_t) i] = delay[c].process (ch[c][i]);
            return;
        }

        // rallumé : détecteurs remis à zéro, puis le module se remplit du signal actuel (deux fois sa latence) pendant
        // qu'on garde le signal sec aligné ; le fondu de 20 ms ne commence qu'ensuite (rien de l'ancien signal ne ressort)
        if (asleep)
        {
            asleep = false;
            wake();
            warm = 2 * latency + 64;
            const float t = mix.getTargetValue();
            mix.setCurrentAndTargetValue (0.0f);
            mix.setTargetValue (t);
        }

        for (int c = 0; c < numCh; ++c)
            for (int i = 0; i < n; ++i)
                dry[c][(size_t) i] = delay[c].process (ch[c][i]);

        runModule (ch, numCh, n);

        if (warm <= 0 && ! mix.isSmoothing() && mix.getTargetValue() >= 1.0f)
            return;

        for (int i = 0; i < n; ++i)
        {
            if (warm > 0)
            {
                --warm;
                for (int c = 0; c < numCh; ++c) ch[c][i] = dry[c][(size_t) i];
                continue;
            }
            const float m = sCurve (mix.getNextValue());
            for (int c = 0; c < numCh; ++c)
                ch[c][i] = dry[c][(size_t) i] + m * (ch[c][i] - dry[c][(size_t) i]);
        }
    }

private:
    DelayLine delay[2];
public:
    const float* getDry (int c) const noexcept { return dry[c].data(); }   // entrée du module, alignée sur sa sortie
private:
    std::vector<float> dry[2];
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mix;
    int latency = 0, warm = 0;
    bool asleep = false;
};

/*  Chaîne complète : INPUT -> NOISE GATE -> EQ PARAMÉTRIQUE -> RESONANCE -> COMPRESSION -> MULTIBAND -> EQ ANALOG -> DE-ESSER
    -> SATURATION LAB -> AUTO GAIN -> OUTPUT (ordre des 7 modules réglable)
    + interrupteur par module (EQ, RESONANCE, COMPRESSION, MULTIBAND, EQ ANALOG, DE-ESSER, SATURATION LAB)
    + BYPASS global (fondu 20 ms, signal sec retardé de la latence totale du plugin).

    Garanties temps réel : aucune allocation dans process() (tout est dans prepare()),
    blocs découpés en tranches de 512 échantillons, NaN/Inf neutralisés à l'entrée et à
    la sortie, sortie bornée à +/-32 (+30 dBFS) uniquement comme garde-fou.
*/
class VocalChain
{
public:
    static constexpr int kChunk = 512;

    struct Params
    {
        float inputDb = 0.0f, low = 0.0f, mud = 0.0f;   // LOW, MUD : 0..10
        float high = 0.0f, air = 0.0f;                   // HI, AIR (VOCAL MINI) : -10..+10
        float lowFreqHz = 125.0f;
        bool lowCut = false;                            // passe-haut 85 Hz de l'EQ
        bool compOn = false, compMaster = true;          // compMaster : interrupteur général de la carte COMPRESSION
        bool parOn = false;                             // compression parallèle
        float parAmount = 50.0f;                        // %
        bool parAuto = true;                            // AUTO : réglage d'usine ; sinon seuil / attaque / relâchement
        float parThresh = -37.0f, parAttack = 6.0f, parRelease = 6.5f;
        int compMode = 0, compOrder = 0;                // 0 1176, 1 LA-2A, 2 Duo ; 0 1176 > LA-2A, 1 LA-2A > 1176
        float fetIn = 0.0f, fetOut = 0.0f, fetAttack = 3.0f, fetRelease = 5.0f;
        int fetRatio = 0;                               // 4, 8, 12, 20, All
        float laGain = 0.0f, laPeak = 40.0f;
        bool laLimit = false;
        float plosive = 0.0f;                           // %
        bool resoListen = false;
        int chainOrder = 0;                             // permutation des 7 modules (voir decodeOrder)
        std::array<bool, 4> mbSolo {};                  // écoute par bande du multibande
        bool mbOn = false;
        MultibandOpto::Params mb;
        std::array<ParametricEQ::Band, ParametricEQ::kBands> peq {};   // EQ paramétrique (premier module)
        bool peqOn = true;                              // interrupteur de l'EQ paramétrique (eqOn = EQ ANALOG)
        bool gateOn = false;                            // NOISE GATE (premier module, cadence de l'hôte)
        float gateThr = -48.0f, gateRange = 24.0f, gateAttack = 1.5f, gateHold = 120.0f, gateRelease = 220.0f, gateHyst = 5.0f;
        float nsAmount = 0.0f;                          // NOISE SUPPRESSOR (%), avant le gate
        float mbOutDb = 0.0f;                           // sortie du MULTIBAND
        float parDeessAmt = 0.0f, parDeessFreq = 6000.0f; // de-esser du bus de compression parallèle (%, Hz)
        bool peqListen = false; int peqListenBand = 2;  // écoute (casque) d'une bande de l'EQ paramétrique
        fx::FxRack::Params fx;                          // page FX (fin de chaîne)
        float resoAmt = 0.0f, resoLow = 40.0f, resoMid = 100.0f, resoHigh = 80.0f;   // en %
        float resoFreq[3] { 180.0f, 1200.0f, 6500.0f }, resoWidth[3] { 1.5f, 2.0f, 2.0f };   // Hz, octaves
        float deessFreq = 4000.0f, deessAmt = 0.0f;     // amt en %
        bool deess2On = false;                          // second de-esser (bouton +)
        float deess2Freq = 7500.0f, deess2Amt = 0.0f;
        float drive = 0.0f, outputDb = 0.0f;
        int satType = 0;                                // 0 Tube, 1 Analog, 2 Tape
        bool eqOn = true, resoOn = true, deessOn = true, satOn = true;
        bool autoGain = false, bypass = false;
    };

    // oversamplingOrder : 0 = OFF, 1..3 = x2..x8. Toujours en qualité maximale (FFT de RESONANCE, anticipations) :
    // VOCALBOX n'a plus de mode LIVE à latence nulle.
    void prepare (double sampleRate, const Params& initial, int oversamplingOrder = 1)
    {
        fs = sampleRate;
        constexpr bool live = false;

        // ---- suréchantillonnage de TOUTE la chaîne de modules (EQ -> ... -> SATURATION) : 1 seul aller-retour
        //      (FIR demi-bande à phase linéaire, latence entière)
        osOrder = juce::jlimit (0, 3, oversamplingOrder);
        osFactor = 1 << osOrder;
        //      Filtres sur mesure, -110 dB (ceux de JUCE en « qualité standard » ne descendaient qu'à -60 / -70 dB : leur
        //      ondulation laissait une trace à -60 dB dans la bande audible) : le premier étage, raide, garde toute la bande
        //      jusqu'à 0.45 x la cadence de l'hôte ; les suivants n'ont qu'à rejeter les images lointaines.
        os = std::make_unique<juce::dsp::Oversampling<float>> (4);   // 2 canaux audio + 2 d'écoute
        if (osOrder == 0) os->addDummyOversamplingStage();
        for (int s = 0; s < osOrder; ++s)
            os->addOversamplingStage (juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple,
                                      s == 0 ? 0.05f : 0.20f, -110.0f, s == 0 ? 0.05f : 0.20f, -110.0f);
        // latence entière EXACTE : celle des filtres, comptée à la cadence suréchantillonnée (toujours un nombre entier),
        // complétée par un retard de quelques échantillons à cette cadence jusqu'au multiple du facteur suivant (la
        // compensation « entière » de JUCE passait par un retard fractionnaire approché : un décalage de phase à -70 dB).
        os->setUsingIntegerLatency (false);
        os->initProcessing ((size_t) kChunk);
        const int osLatencyOS = juce::roundToInt (os->getLatencyInSamples() * osFactor);
        osPad = (osFactor - osLatencyOS % osFactor) % osFactor;
        for (auto& d : osPadDelay) d.prepare (osPad);
        osLatencyHost = (osLatencyOS + osPad) / osFactor;
        const double fsOS = fs * osFactor;
        const int blockOS = kChunk * osFactor;

        eq.prepare (fsOS);
        peq.prepare (fsOS);
        reso.prepare (fsOS, osFactor, blockOS);
        deEsser.prepare (fsOS, live, osFactor);
        deEsser2.prepare (fsOS, live, osFactor);   // toujours dans la chaîne : la latence ne change pas avec le bouton +
        plosive.prepare (fsOS, live, osFactor);   // anticipation en MIX (multiple du facteur : la latence reste entière)
        comp.prepare (fsOS, blockOS);
        parallel.prepare (fsOS, blockOS);
        multiband.prepare (fsOS);
        sat.prepare (fsOS, blockOS, 0);   // la saturation tourne déjà à la cadence suréchantillonnée de la chaîne
        autoGain.prepare (fs);
        gate.prepare (fs, live);
        suppressor.prepare (fs, live);
        mbOutGain.reset (fsOS, 0.03);
        fxRack.prepare (fs);
        peqListenMix.reset (fs, 0.03);
        listenF = -1.0f;
        listenA.reset(); listenB.reset();

        eqSw.prepare (fsOS, 0, blockOS);
        peqSw.prepare (fsOS, 0, blockOS);
        resoSw.prepare (fsOS, reso.getLatencySamples(), blockOS);
        compSw.prepare (fsOS, 0, blockOS);
        parSw.prepare (fsOS, 0, blockOS);
        mbSw.prepare (fsOS, 0, blockOS);
        deessSw.prepare (fsOS, deessLatency(), blockOS);
        satSw.prepare (fsOS, sat.getLatencySamples(), blockOS);

        // latence totale (à la cadence de l'hôte) : filtres de suréchantillonnage + modules (multiples du facteur)
        const int modulesOS = reso.getLatencySamples() + deessLatency() + sat.getLatencySamples();
        jassert (modulesOS % osFactor == 0);
        latency = suppressor.getLatencySamples() + gate.getLatencySamples() + osLatencyHost + modulesOS / osFactor;
        for (auto& d : dryDelay) d.prepare (latency);
        // écoute RESONANCE : ce qui est retiré est retardé comme le reste de la chaîne (DE-ESSER + SATURATION)
        for (auto& d : listenDelay) d.prepare (deessLatency() + sat.getLatencySamples());
        listenMix.reset (fs, 0.03);
        orderStep = 1.0f / (float) std::max (1, juce::roundToInt (0.015 * fs));

        inGain.reset (fs, 0.02);
        outGain.reset (fs, 0.02);
        wetMix.reset (fs, 0.02);
        setParams (initial, true);
    }

    int getLatencySamples() const noexcept { return latency; }

    void setParams (const Params& p, bool snap = false) noexcept
    {
        const float gi = dbToGain (juce::jlimit (-24.0f, 24.0f, p.inputDb));
        const float go = dbToGain (juce::jlimit (-24.0f, 24.0f, p.outputDb));
        const float wm = p.bypass ? 0.0f : 1.0f;

        if (snap)
        {
            inGain.setCurrentAndTargetValue (gi);
            outGain.setCurrentAndTargetValue (go);
            wetMix.setCurrentAndTargetValue (wm);
        }
        else
        {
            inGain.setTargetValue (gi);
            outGain.setTargetValue (go);
            wetMix.setTargetValue (wm);
        }

        eq.setTargets (p.low, p.mud, snap);
        eq.setShine (p.high, p.air, snap);
        eq.setLowFreq (p.lowFreqHz, snap);
        eq.setLowCut (p.lowCut, snap);
        peq.setBands (p.peq, snap);
        comp.setMode (p.compMode, p.compOrder, snap);
        comp.set1176 (p.fetIn, p.fetOut, p.fetAttack, p.fetRelease, p.fetRatio, snap);
        comp.setLA2A (p.laGain, p.laPeak, p.laLimit, snap);
        multiband.setParams (p.mb, snap);
        const float sens[3] { p.resoLow, p.resoMid, p.resoHigh };
        std::array<ResonanceSuppressor::Band, ResonanceSuppressor::kNumBands> bands;
        for (int b = 0; b < 3; ++b)
            bands[(size_t) b] = { p.resoFreq[b], p.resoWidth[b], sens[b] * 0.01f };
        reso.setParams (p.resoAmt * 0.01f, bands);
        pendingOrder = decodeOrder (p.chainOrder);
        if (snap) { applyOrder (pendingOrder); orderGain = 1.0f; orderJustSwapped = false; }
        multiband.setSolo (p.mbSolo, snap);
        listenMix.setTargetValue (p.resoListen && p.resoOn ? 1.0f : 0.0f);
        if (snap) listenMix.setCurrentAndTargetValue (p.resoListen && p.resoOn ? 1.0f : 0.0f);
        plosive.set (p.deessOn, p.plosive * 0.01f);
        // (la garde de sifflantes des aigus est dans l'EQ ANALOG : celle de HI / AIR, comme dans VOCAL MINI)
        deEsser.setParams (p.deessFreq, p.deessOn ? p.deessAmt * 0.01f : 0.0f, snap);
        deEsser2.setParams (p.deess2Freq, p.deessOn && p.deess2On ? p.deess2Amt * 0.01f : 0.0f, snap);
        sat.setDrive (p.drive, snap);
        sat.setMode (p.satType, snap);
        gate.setParams (p.gateOn, p.gateThr, p.gateRange, p.gateAttack, p.gateHold, p.gateRelease, p.gateHyst);
        suppressor.setAmount (p.nsAmount * 0.01f);
        fxRack.set (p.fx);
        setListenBand (p, snap);

        eqSw.set (p.eqOn, snap);
        peqSw.set (p.peqOn, snap);
        resoSw.set (p.resoOn, snap);
        compSw.set (p.compOn && p.compMaster, snap);
        parSw.set (p.parOn, snap);
        parallel.setAmount (p.parAmount * 0.01f, snap);
        parallel.setControl (p.parAuto, p.parThresh, p.parAttack, p.parRelease, snap);
        parallel.setDeEsser (p.parDeessFreq, p.parDeessAmt * 0.01f, snap);
        if (snap) mbOutGain.setCurrentAndTargetValue (dbToGain (p.mbOutDb)); else mbOutGain.setTargetValue (dbToGain (p.mbOutDb));
        mbSw.set (p.mbOn && p.compMaster, snap);
        deessSw.set (p.deessOn, snap);   // les plosives suivent l'interrupteur DE-ESSER / PLOSIVES
        satSw.set (p.satOn, snap);
        autoOn = p.autoGain;
    }

    void process (float* const* ch, int numCh, int n) noexcept
    {
        for (int pos = 0; pos < n; pos += kChunk)
        {
            const int m = std::min (kChunk, n - pos);
            float* c[2] { ch[0] + pos, numCh > 1 ? ch[1] + pos : nullptr };
            processChunk (c, numCh, m);
        }
    }

    float getAutoGainDb() const noexcept { return autoGain.getGainDb(); }
    float getDeEssReductionDb() const noexcept { return deEsser.getReductionDb(); }
    float getDeEss2ReductionDb() const noexcept { return deEsser2.getReductionDb(); }
    float getPlosiveReductionDb() const noexcept { return plosive.getReductionDb(); }
    float getParallelReductionDb() const noexcept { return parallel.getGainReductionDb(); }
    float getPeqBandGainDb (int b) const noexcept { return peq.getBandGainDb (b); }
    const NoiseGate::Meter& getGateMeter() const noexcept { return gate.getMeter(); }
    float getSuppressorReductionDb() const noexcept { return suppressor.getReductionDb(); }
    float getSuppressorFloorDb() const noexcept { return suppressor.getNoiseFloorDb(); }
    float getReverbTail() const noexcept { return fxRack.getReverbTail(); }
    float getParallelDeEssDb() const noexcept { return parallel.getDeEssReductionDb(); }

    // mètres (crête linéaire par canal depuis la dernière lecture) : lus et remis à zéro par l'interface
    float consumeInputLevel (int c) noexcept  { return inLevel[c & 1].exchange (0.0f, std::memory_order_relaxed); }
    float consumeOutputLevel (int c) noexcept { return outLevel[c & 1].exchange (0.0f, std::memory_order_relaxed); }
    const ResonanceSuppressor::Meter& getResonanceMeter() const noexcept { return reso.getMeter(); }
    float get1176ReductionDb() const noexcept { return comp.get1176ReductionDb(); }
    float getLA2AReductionDb() const noexcept { return comp.getLA2AReductionDb(); }
    const MultibandOpto::Meter& getMultibandMeter() const noexcept { return multiband.getMeter(); }

private:
    void processChunk (float* const* ch, int numCh, int n) noexcept
    {
        orderJustSwapped = false;

        // 1) entrée saine + copie du signal sec, retardé de la latence totale
        for (int c = 0; c < numCh; ++c)
            for (int i = 0; i < n; ++i)
            {
                const float x = sanitize (ch[c][i]);
                ch[c][i] = x;
                dry[c][i] = dryDelay[c].process (x);
            }

        // 2) INPUT
        for (int i = 0; i < n; ++i)
        {
            const float g = inGain.getNextValue();
            for (int c = 0; c < numCh; ++c) ch[c][i] *= g;
        }

        storePeaks (inLevel, ch, numCh, n);   // mètre INPUT : ce qui entre dans la chaîne (après le gain INPUT)

        // NOISE SUPPRESSOR puis NOISE GATE : premiers modules, à la cadence de l'hôte
        //   (suppressor : une trame de latence en MIX, rien en LIVE ; gate : anticipation de 1.5 ms en MIX, rien en LIVE)
        suppressor.process (ch, numCh, n);
        gate.process (ch, numCh, n);
        autoGain.analyseReference (ch, numCh, n);

        // 3) montée en cadence. Les 2 canaux de l'écoute RESONANCE (ce qu'elle retire) ne montent et ne redescendent
        //    qu'en écoute ; une piste mono ne fait monter qu'un canal (le suréchantillonnage coûte par canal).
        const bool listening = listenMix.isSmoothing() || listenMix.getTargetValue() > 0.0f;
        for (int c = 0; c < 2; ++c) std::fill (removed[c], removed[c] + n, 0.0f);
        if (numCh < 2 && listening) std::copy (ch[0], ch[0] + n, monoSpare);
        float* chans[4] { ch[0], numCh > 1 ? ch[1] : monoSpare, removed[0], removed[1] };
        juce::dsp::AudioBlock<float> block (chans, (size_t) (listening ? 4 : numCh), (size_t) n);
        auto up = os->processSamplesUp (block);
        const int nOS = n * osFactor;
        float* x[2] { up.getChannelPointer (0), numCh > 1 || listening ? up.getChannelPointer (1) : nullptr };
        float* rem[2] { listening ? up.getChannelPointer (2) : nullptr, listening ? up.getChannelPointer (3) : nullptr };

        // 4) les 7 modules dans l'ordre choisi (cadence suréchantillonnée)
        for (int k = 0; k < kOrderModules; ++k)
        {
            switch (order[(size_t) k])
            {
                case 0: peqSw.process (x, numCh, nOS, [this] (float* const* s, int nc, int q) { peq.process (s, nc, q); }); break;
                case 1:
                    resoSw.process (x, numCh, nOS, [this] (float* const* s, int nc, int q) { reso.process (s, nc, q); }, [this] { reso.reset(); });
                    // LISTEN : ce que RESONANCE retire = son entrée (alignée) - sa sortie
                    if (listening)
                        for (int c = 0; c < numCh; ++c)
                    {
                        const float* in = resoSw.getDry (c);
                        for (int i = 0; i < nOS; ++i) rem[c][i] = listenDelay[c].process (in[i] - x[c][i]);
                    }
                    break;
                case 2:   // COMPRESSION puis PARALLEL (la compression parallèle suit la compression dans l'ordre)
                    compSw.process (x, numCh, nOS, [this] (float* const* s, int nc, int q) { comp.process (s, nc, q); }, [this] { comp.reset(); });
                    parSw.process (x, numCh, nOS, [this] (float* const* s, int nc, int q) { parallel.process (s, nc, q); }, [this] { parallel.reset(); });
                    break;
                case 3:   // MULTIBAND puis son OUTPUT
                    mbSw.process (x, numCh, nOS, [this] (float* const* s, int nc, int q)
                    {
                        multiband.process (s, nc, q);
                        for (int i = 0; i < q; ++i) { const float g = mbOutGain.getNextValue(); for (int c = 0; c < nc; ++c) s[c][i] *= g; }
                    }, [this] { multiband.reset(); });
                    break;
                case 4: eqSw.process (x, numCh, nOS, [this] (float* const* s, int nc, int q) { eq.process (s, nc, q); }); break;   // EQ ANALOG
                case 5: deessSw.process (x, numCh, nOS, [this] (float* const* s, int nc, int q) { plosive.process (s, nc, q); deEsser.process (s, nc, q); deEsser2.process (s, nc, q); }, [this] { plosive.reset(); }); break;
                default: satSw.process (x, numCh, nOS, [this] (float* const* s, int nc, int q) { sat.process (s, nc, q); }); break;
            }
        }

        // 5) retour à la cadence de l'hôte
        if (osPad > 0)
            for (int c = 0; c < (int) up.getNumChannels(); ++c)
            {
                float* s = up.getChannelPointer ((size_t) c);
                for (int i = 0; i < nOS; ++i) s[i] = osPadDelay[c].process (s[i]);
            }
        os->processSamplesDown (block);

        autoGain.analyseProcessed (ch, numCh, n);
        autoGain.update (autoOn, n);

        // 6) AUTO GAIN (sur la voix traitée), puis la page FX : les effets ne font pas varier la correction d'AUTO GAIN
        for (int i = 0; i < n; ++i)
        {
            const float ag = autoGain.nextGain();
            for (int c = 0; c < numCh; ++c) ch[c][i] *= ag;
        }
        fxRack.process (ch, numCh, n);

        // 7) OUTPUT + écoutes (RESONANCE, bande de l'EQ) + fondu BYPASS (signal sec et traité alignés)
        //    changement d'ordre : on glisse vers le signal sec aligné (15 ms), on réordonne, on revient (15 ms)
        const bool listenBandActive = peqListenMix.isSmoothing() || peqListenMix.getTargetValue() > 0.0f;
        for (int i = 0; i < n; ++i)
        {
            if (pendingOrder != order)
            {
                orderGain -= orderStep;
                if (orderGain <= 0.0f) { orderGain = 0.0f; applyOrder (pendingOrder); }   // pris en compte au bloc suivant
            }
            else if (orderGain < 1.0f && ! orderJustSwapped)
                orderGain = std::min (1.0f, orderGain + orderStep);

            const float g = outGain.getNextValue();
            const float m = sCurve (wetMix.getNextValue()) * sCurve (orderGain);
            const float lm = sCurve (listenMix.getNextValue());   // écoute RESONANCE : on n'entend plus que ce qui est retiré
            const float pl = sCurve (peqListenMix.getNextValue()); // écoute d'une bande de l'EQ : on n'entend plus qu'elle
            for (int c = 0; c < numCh; ++c)
            {
                float wet = ch[c][i] * g * (1.0f - lm) + removed[c][i] * lm;
                if (listenBandActive)
                    wet += pl * (listenGain * listenB.process (listenA.process (dry[c][i], c), c) - wet);
                ch[c][i] = sanitize (dry[c][i] * (1.0f - m) + wet * m);
            }
        }
        storePeaks (outLevel, ch, numCh, n);  // mètre OUTPUT : niveau final
    }

    double fs = 44100.0;
    int latency = 0;
    bool autoOn = false;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> inGain, outGain, wetMix;
    VocalEQ eq;
    ParametricEQ peq;
    ResoMultirate reso;   // RESONANCE : analyse à la cadence de l'hôte, même dans la chaîne suréchantillonnée
    CompressorModule comp;
    ParallelComp parallel;
    MultibandOpto multiband;
    DeEsser deEsser, deEsser2;   // deEsser2 : second de-esser (bouton + de la carte)
    int deessLatency() const noexcept { return plosive.getLatencySamples() + deEsser.getLatencySamples() + deEsser2.getLatencySamples(); }
    SaturationLab sat;
    AutoGain autoGain;
    NoiseGate gate;
    NoiseSuppressor suppressor;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> mbOutGain { 1.0f };
    fx::FxRack fxRack;
    // écoute d'une bande de l'EQ paramétrique : le signal d'entrée (aligné) passé dans le filtre de la bande
    Biquad listenA, listenB;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> peqListenMix;
    float listenF = -1.0f, listenQ = -1.0f, listenGain = 1.0f; int listenType = -1;

    void setListenBand (const Params& p, bool snap) noexcept
    {
        const float target = p.peqListen ? 1.0f : 0.0f;
        if (snap) peqListenMix.setCurrentAndTargetValue (target); else peqListenMix.setTargetValue (target);
        const auto& b = p.peq[(size_t) juce::jlimit (0, ParametricEQ::kBands - 1, p.peqListenBand)];
        if (b.freq == listenF && b.q == listenQ && b.type == listenType) return;
        listenF = b.freq; listenQ = b.q; listenType = b.type;
        const double f = juce::jlimit (25.0, 0.45 * fs, (double) b.freq);
        // cloche / notch : la zone autour de la bande ; shelf ou coupe : la partie du spectre concernée
        listenGain = (b.type == ParametricEQ::bell || b.type == ParametricEQ::notch) ? 1.6f : 1.0f;   // une bande étroite paraît plus faible
        if (b.type == ParametricEQ::lowShelf || b.type == ParametricEQ::lowCut)
        { listenA.setCoeffs (BiquadCoeffs::lowpass (fs, f, 0.707)); listenB.setCoeffs (BiquadCoeffs::lowpass (fs, f, 0.707)); }
        else if (b.type == ParametricEQ::highShelf || b.type == ParametricEQ::highCut)
        { listenA.setCoeffs (BiquadCoeffs::highpass (fs, f, 0.707)); listenB.setCoeffs (BiquadCoeffs::highpass (fs, f, 0.707)); }
        else
        {
            const double q = juce::jlimit (0.5, 12.0, (double) b.q);
            listenA.setCoeffs (BiquadCoeffs::bandpass (fs, f, q)); listenB.setCoeffs (BiquadCoeffs::bandpass (fs, f, q));
        }
    }
    ModuleSwitch eqSw, peqSw, resoSw, compSw, parSw, mbSw, deessSw, satSw;
    DelayLine dryDelay[2];
    float dry[2][kChunk] {}, removed[2][kChunk] {}, monoSpare[kChunk] {};
    DelayLine listenDelay[2];
    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    int osOrder = 0, osFactor = 1, osPad = 0, osLatencyHost = 0;
    DelayLine osPadDelay[4];   // complète la latence des filtres jusqu'à un nombre entier d'échantillons de l'hôte
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> listenMix;

    // ordre de traitement : appliqué quand la sortie a glissé vers le signal sec (aucun clic)
    void applyOrder (const std::array<int, kOrderModules>& o) noexcept
    {
        order = o;
        orderJustSwapped = true;
        // l'écoute RESONANCE est retardée de la latence des modules placés APRÈS elle
        int after = 0;
        bool seen = false;
        for (int m : order)
        {
            if (seen && m == 5) after += deessLatency();
            if (seen && m == 6) after += sat.getLatencySamples();
            if (m == 1) seen = true;
        }
        for (auto& d : listenDelay) d.setLength (after);
    }

    std::array<int, kOrderModules> order { 0, 1, 2, 3, 4, 5, 6 }, pendingOrder { 0, 1, 2, 3, 4, 5, 6 };
    float orderGain = 1.0f, orderStep = 0.001f;
    bool orderJustSwapped = false;
    mini::DePlosive plosive;   // les plosives (carte DE-ESSER / PLOSIVES), le moteur de VOCAL MINI

    static void storePeaks (std::atomic<float> (&dest)[2], float* const* ch, int numCh, int n) noexcept
    {
        for (int c = 0; c < 2; ++c)
        {
            const float* s = ch[std::min (c, numCh - 1)];
            float pk = 0.0f;
            for (int i = 0; i < n; ++i) pk = std::max (pk, std::abs (s[i]));
            if (pk > dest[c].load (std::memory_order_relaxed)) dest[c].store (pk, std::memory_order_relaxed);
        }
    }
    std::atomic<float> inLevel[2] {}, outLevel[2] {};
};
} // namespace onirique
