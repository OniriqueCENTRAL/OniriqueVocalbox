#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "../Modules.h"
#include "VocalChain.h"   // ModuleSwitch, et tous les moteurs des modules
#include "SpectrumTap.h"
#include "PreampN73.h"     // PREAMP : l'étage préampli du N73
#include "cl1b/Opto.h"      // CL1B : la cellule optique et la lampe de l'OptoBox
#include "tune/TuneEngine.h"   // TUNER : le moteur d'ONIRIQUE TUNER
#include "tune/Scales.h"
#include "DeClick.h"
#include "DeBreath.h"
#include "DeReverb.h"


namespace onirique
{
/*  Ce que l'interface dessine d'un module : l'enveloppe de son entrée et de sa sortie, colonne par colonne (une colonne =
    25 ms de son). La partie « retirée » (gate, de-esser, plosives, souffle...) est l'écart entre les deux. Écrit par le
    thread audio, lu par l'interface : de simples atomiques, aucun verrou. */
struct ModuleScope
{
    static constexpr int kCols = 256;
    std::atomic<float> in[kCols] {}, out[kCols] {}, gr[kCols] {};   // gr : la plus forte réduction (dB) de la colonne
    void noteReduction (float db) noexcept { grMax = std::max (grMax, std::abs (db)); }
    std::atomic<int> write { 0 };

    void prepare (double fs) noexcept { colLen = std::max (1, juce::roundToInt (0.025 * fs)); count = 0; pkIn = pkOut = 0.0f; }
    void push (const float* const* dry, const float* const* wet, int numCh, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < numCh; ++c)
            {
                pkIn = std::max (pkIn, std::abs (dry[c][i]));
                pkOut = std::max (pkOut, std::abs (wet[c][i]));
            }
            if (++count >= colLen)
            {
                const int w = write.load (std::memory_order_relaxed);
                in[w].store (pkIn, std::memory_order_relaxed);
                out[w].store (pkOut, std::memory_order_relaxed);
                gr[w].store (grMax, std::memory_order_relaxed);
                grMax = 0.0f;
                write.store ((w + 1) % kCols, std::memory_order_release);
                count = 0; pkIn = pkOut = 0.0f;
            }
        }
    }

private:
    int colLen = 1200, count = 0;
    float pkIn = 0.0f, pkOut = 0.0f, grMax = 0.0f;
};

//==============================================================================
/*  Un module de la chaîne : son moteur, ses réglages (exemplaire k), son interrupteur en fondu (ModuleSwitch : la latence
    ne change pas quand on le coupe) et son oscilloscope pour l'interface. */
class ChainModule
{
public:
    ChainModule (juce::AudioProcessorValueTreeState& s, int moduleType, int instance) : apvts (s), type (moduleType), inst (instance)
    {
        pOn = raw (modules::info (type).onId);
        mHold = s.getRawParameterValue (ids::macroHold); mSpace = s.getRawParameterValue (ids::macroSpace); mWarm = s.getRawParameterValue (ids::macroWarmth);
        if (type == modules::gate || type == modules::noise || type == modules::plosive || type == modules::deess
            || type == modules::declick || type == modules::debreath || type == modules::dereverb)
        { tapIn = std::make_unique<SpectrumTap>(); tapOut = std::make_unique<SpectrumTap>(); }
    }
    virtual ~ChainModule() = default;

    // rate : la cadence à laquelle il tourne (celle de l'hôte, ou suréchantillonnée) ; factor : le suréchantillonnage
    void prepare (double rate, int factor, int maxBlock)
    {
        fs = rate;
        prepareEngine (rate, factor, maxBlock);
        sw.prepare (rate, getLatencySamples(), maxBlock, getMaxLatencySamples());
        scope.prepare (rate);
        update (true);
    }

    // réglages lus à chaque bloc (snap : sans glissement, à l'arrivée dans la chaîne)
    void update (bool snap) noexcept
    {
        const bool on = isOn();
        sw.set (selfSwitch() || on, snap);
        updateEngine (on, snap);
    }

    void process (float* const* ch, int numCh, int n) noexcept
    {
        if (variableLatency) sw.setLatency (getLatencySamples());
        sw.process (ch, numCh, n, [this] (float* const* s, int nc, int q) { processEngine (s, nc, q); }, [this] { resetEngine(); });
        scope.noteReduction (getReductionDb());
        const float* dry[2] { sw.getDry (0), sw.getDry (numCh > 1 ? 1 : 0) };
        scope.push (dry, ch, numCh, n);
        if (tapIn != nullptr) { tapIn->push (dry[0], numCh > 1 ? dry[1] : nullptr, n); tapOut->push (ch[0], numCh > 1 ? ch[1] : nullptr, n); }
    }

    // entrée dans la chaîne : rien de l'ancien passage ne ressort
    void arrive() noexcept { resetEngine(); update (true); }

    bool isOn() const noexcept { return pOn != nullptr && pOn->load (std::memory_order_relaxed) > 0.5f; }
    virtual int getLatencySamples() const noexcept { return 0; }   // à la cadence du module
    virtual int getMaxLatencySamples() const noexcept { return getLatencySamples(); }   // la plus grande qu'il pourra prendre
    // les effets qui gardent leur queue quand on les coupe (reverb, delay...) gèrent eux-mêmes leur interrupteur
    virtual bool selfSwitch() const noexcept { return false; }
    // retour d'effet (DELAY, REVERB) : ajoute son retour dans wet à partir de la voix (in) au lieu de traiter en place
    virtual bool isSend() const noexcept { return false; }
    virtual void processSendInto (const float* const*, float* const*, int, int) noexcept {}
    virtual float getReductionDb() const noexcept { return 0.0f; }   // pour les mètres de l'interface
    // le mini-mètre de son carré dans la chaîne : ce qu'il retire en ce moment (dB) ; la réduction, sauf pour ceux qui la
    // mesurent autrement (RESONANCE, MULTIBAND : la bande la plus réduite ; DE-CLICK : le dernier clic retiré)
    virtual float activityDb() const noexcept { return std::abs (getReductionDb()); }

    juce::AudioProcessorValueTreeState& apvts;
    const int type, inst;
    ModuleScope scope;
    // spectres avant / après (nettoyages : gate, noise, plosives, de-esser), à la cadence du module (getRate)
    std::unique_ptr<SpectrumTap> tapIn, tapOut;
    double getRate() const noexcept { return fs; }
    // retours d'effet (DELAY, REVERB) : la voix qui entre et le retour, pour l'interface
    void recordSend (const float* const* in, const float* const* wet, int numCh, int n) noexcept { scope.push (in, wet, numCh, n); }

protected:
    virtual void prepareEngine (double rate, int factor, int maxBlock) = 0;
    virtual void updateEngine (bool on, bool snap) noexcept = 0;
    virtual void processEngine (float* const* ch, int numCh, int n) noexcept = 0;
    virtual void resetEngine() noexcept {}

    std::atomic<float>* raw (const char* id) const
    {
        auto* p = apvts.getRawParameterValue (ids::inst (id, inst));
        jassert (p != nullptr);
        return p;
    }
    static float f (std::atomic<float>* p) noexcept { return p->load (std::memory_order_relaxed); }
    static bool b (std::atomic<float>* p) noexcept { return f (p) > 0.5f; }
    static int i (std::atomic<float>* p) noexcept { return juce::roundToInt (f (p)); }

    double fs = 48000.0;
    ModuleSwitch sw;
    std::atomic<float>* pOn = nullptr;
    // les macros (-1 .. +1) : TENUE pousse les compresseurs, ESPACE les retours, CHALEUR le préampli
    std::atomic<float> *mHold = nullptr, *mSpace = nullptr, *mWarm = nullptr;
    bool variableLatency = false;   // TUNER : sa latence suit le registre
    static float macro (std::atomic<float>* p) noexcept { return p != nullptr ? juce::jlimit (-1.0f, 1.0f, p->load (std::memory_order_relaxed) * 0.01f) : 0.0f; }

    friend class ModularChain;
};

namespace chainmods
{
//------------------------------------------------------------------------------ NETTOYAGE
class Gate final : public ChainModule
{
public:
    Gate (juce::AudioProcessorValueTreeState& s, int k) : ChainModule (s, modules::gate, k)
    {
        const char* id[6] { ids::gateThr, ids::gateRange, ids::gateAttack, ids::gateHold, ids::gateRelease, ids::gateHyst };
        for (int j = 0; j < 6; ++j) p[j] = raw (id[j]);
    }
    bool selfSwitch() const noexcept override { return true; }   // le gate s'ouvre en fondu quand on le coupe
    int getLatencySamples() const noexcept override { return engine.getLatencySamples(); }
    const NoiseGate::Meter& getMeter() const noexcept { return engine.getMeter(); }
    float getReductionDb() const noexcept override { return engine.getMeter().gainDb.load(); }

private:
    void prepareEngine (double rate, int, int) override { engine.prepare (rate, false); }
    void updateEngine (bool on, bool) noexcept override { engine.setParams (on, f (p[0]), f (p[1]), f (p[2]), f (p[3]), f (p[4]), f (p[5])); }
    void processEngine (float* const* ch, int numCh, int n) noexcept override { engine.process (ch, numCh, n); }
    void resetEngine() noexcept override { engine.reset(); }
    NoiseGate engine;
    std::atomic<float>* p[6] {};
};

class Noise final : public ChainModule
{
public:
    Noise (juce::AudioProcessorValueTreeState& s, int k) : ChainModule (s, modules::noise, k) { amt = raw (ids::nsAmount); }
    bool selfSwitch() const noexcept override { return true; }
    int getLatencySamples() const noexcept override { return engine.getLatencySamples(); }
    float getReductionDb() const noexcept override { return engine.getReductionDb(); }
    float getNoiseFloorDb() const noexcept { return engine.getNoiseFloorDb(); }

private:
    void prepareEngine (double rate, int, int) override { engine.prepare (rate, false); }
    void updateEngine (bool on, bool) noexcept override { engine.setAmount (on ? f (amt) * 0.01f : 0.0f); }
    void processEngine (float* const* ch, int numCh, int n) noexcept override { engine.process (ch, numCh, n); }
    NoiseSuppressor engine;
    std::atomic<float>* amt = nullptr;
};

class Plosive final : public ChainModule
{
public:
    Plosive (juce::AudioProcessorValueTreeState& s, int k) : ChainModule (s, modules::plosive, k) { amt = raw (ids::plosive); listen = raw (ids::plosListen); }
    int getLatencySamples() const noexcept override { return engine.getLatencySamples(); }
    float getReductionDb() const noexcept override { return engine.getReductionDb(); }

private:
    void prepareEngine (double rate, int factor, int) override
    {
        engine.prepare (rate, false, factor);
        for (auto& d : dry) d.prepare (engine.getLatencySamples());
    }
    void updateEngine (bool, bool) noexcept override { engine.set (true, f (amt) * 0.01f); }
    // LISTEN : seulement ce qui est retiré (le son d'origine, aligné sur la latence, moins le son traité)
    void processEngine (float* const* ch, int numCh, int n) noexcept override
    {
        // le son d'origine passe toujours dans sa ligne à retard : LISTEN s'allume sans saut
        if (! b (listen)) { for (int c = 0; c < numCh; ++c) for (int i = 0; i < n; ++i) dry[c].process (ch[c][i]); engine.process (ch, numCh, n); return; }
        for (int pos = 0; pos < n; pos += 512)
        {
            const int m = std::min (512, n - pos);
            for (int c = 0; c < numCh; ++c)
                for (int i = 0; i < m; ++i) tmp[(size_t) c][(size_t) i] = dry[c].process (ch[c][pos + i]);
            float* part[2] { ch[0] + pos, numCh > 1 ? ch[1] + pos : nullptr };
            engine.process (part, numCh, m);
            for (int c = 0; c < numCh; ++c)
                for (int i = 0; i < m; ++i) ch[c][pos + i] = tmp[(size_t) c][(size_t) i] - ch[c][pos + i];
        }
    }
    void resetEngine() noexcept override { engine.reset(); for (auto& d : dry) d.reset(); }
    mini::DePlosive engine;
    DelayLine dry[2];
    std::array<std::array<float, 512>, 2> tmp {};
    std::atomic<float>* amt = nullptr, *listen = nullptr;
};

class Reso final : public ChainModule
{
public:
    Reso (juce::AudioProcessorValueTreeState& s, int k) : ChainModule (s, modules::reso, k)
    {
        amt = raw (ids::resoAmt); sens[0] = raw (ids::resoLow); sens[1] = raw (ids::resoMid); sens[2] = raw (ids::resoHigh);
        fr[0] = raw (ids::resoLowFreq); fr[1] = raw (ids::resoMidFreq); fr[2] = raw (ids::resoHighFreq);
        wd[0] = raw (ids::resoLowWidth); wd[1] = raw (ids::resoMidWidth); wd[2] = raw (ids::resoHighWidth);
        listen = raw (ids::resoListen);
    }
    int getLatencySamples() const noexcept override { return engine.getLatencySamples(); }
    const ResonanceSuppressor::Meter& getMeter() const noexcept { return engine.getMeter(); }
    float activityDb() const noexcept override { float m = 0.0f; for (auto& r : engine.getMeter().reductionDb) m = std::max (m, std::abs (r.load (std::memory_order_relaxed))); return m; }

private:
    void prepareEngine (double rate, int factor, int maxBlock) override
    {
        engine.prepare (rate, factor, maxBlock);
        listenMix.reset (rate, 0.03);
    }
    void updateEngine (bool on, bool snap) noexcept override
    {
        std::array<ResonanceSuppressor::Band, ResonanceSuppressor::kNumBands> bands;
        for (int k = 0; k < 3; ++k) bands[(size_t) k] = { f (fr[k]), f (wd[k]), f (sens[k]) * 0.01f };
        engine.setParams (f (amt) * 0.01f, bands);
        const float t = b (listen) && on ? 1.0f : 0.0f;
        if (snap) listenMix.setCurrentAndTargetValue (t); else listenMix.setTargetValue (t);
    }
    void processEngine (float* const* ch, int numCh, int n) noexcept override
    {
        engine.process (ch, numCh, n);
        // LISTEN : on n'entend plus que ce que RESONANCE retire (son entrée alignée moins sa sortie)
        if (! listenMix.isSmoothing() && listenMix.getTargetValue() <= 0.0f) return;
        for (int i2 = 0; i2 < n; ++i2)
        {
            const float m = sCurve (listenMix.getNextValue());
            for (int c = 0; c < numCh; ++c) { const float rem = sw.getDry (c)[i2] - ch[c][i2]; ch[c][i2] += m * (rem - ch[c][i2]); }
        }
    }
    void resetEngine() noexcept override { engine.reset(); }
    ResoMultirate engine;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> listenMix;
    std::atomic<float> *amt = nullptr, *sens[3] {}, *fr[3] {}, *wd[3] {}, *listen = nullptr;
};

/*  DE-ESSER : on coche ce qu'on veut traiter. S (à la fréquence réglée, 5.5 kHz d'origine), T (attaques vers 4.2 kHz), CH (vers
    3.2 kHz), F (le souffle aigu vers 9 kHz) : un détecteur par son, en série, tous à la même dose (AMOUNT). CLARITY rend un peu
    d'air et de présence après coup (étagère douce dès 3 kHz), mais se retire tant qu'un son est en train d'être traité : la
    clarté revient sans faire revenir les S. Par défaut : S seul, sans CLARITY (le son d'origine). */
class DeEss final : public ChainModule
{
public:
    DeEss (juce::AudioProcessorValueTreeState& s, int k) : ChainModule (s, modules::deess, k)
    {
        freq = raw (ids::deessFreq); amt = raw (ids::deessAmt); clarity = raw (ids::deessClarity);
        target[0] = raw (ids::deessS); target[1] = raw (ids::deessT); target[2] = raw (ids::deessCH); target[3] = raw (ids::deessF);
    }
    int getLatencySamples() const noexcept override { int l = 0; for (auto& e : engines) l += e.getLatencySamples(); return l; }
    float getReductionDb() const noexcept override { float r = 0.0f; for (auto& e : engines) r = std::max (r, std::abs (e.getReductionDb())); return r; }

private:
    void prepareEngine (double rate, int factor, int) override
    {
        for (auto& e : engines) e.prepare (rate, false, factor);
        clar.reset (rate, 0.05);
        lastGain = 1.0e9f;
    }
    void updateEngine (bool, bool snap) noexcept override
    {
        const float a = f (amt) * 0.01f;
        const float freqs[4] { f (freq), 4200.0f, 3200.0f, 9000.0f };
        for (int j = 0; j < 4; ++j) engines[j].setParams (freqs[j], b (target[j]) ? a : 0.0f, snap);
        const float c = f (clarity) * 0.01f;
        if (snap) clar.setCurrentAndTargetValue (c); else clar.setTargetValue (c);
    }
    void processEngine (float* const* ch, int numCh, int n) noexcept override
    {
        for (auto& e : engines) e.process (ch, numCh, n);
        const float c = clar.getNextValue();
        clar.skip (std::max (0, n - 1));
        if (c <= 0.0005f && lastGain == 0.0f) return;
        // l'étagère suit la réduction : à 6 dB de réduction ou plus, elle s'efface
        const float duck = 1.0f - juce::jlimit (0.0f, 1.0f, getReductionDb() / 6.0f);
        duckS += 0.2f * (duck - duckS);
        const float gainDb = 4.0f * c * duckS;
        if (std::abs (gainDb - lastGain) > 0.02f)
        {
            lastGain = gainDb < 0.01f ? 0.0f : gainDb;
            shelf.setCoeffs (BiquadCoeffs::highShelf (fs, std::min (3000.0, 0.45 * fs), 0.6, lastGain));
        }
        if (lastGain == 0.0f) return;
        for (int cc = 0; cc < numCh; ++cc) for (int s = 0; s < n; ++s) ch[cc][s] = shelf.process (ch[cc][s], cc);
    }
    void resetEngine() noexcept override { shelf.reset(); duckS = 1.0f; }
    DeEsser engines[4];
    Biquad shelf;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> clar;
    float lastGain = 0.0f, duckS = 1.0f;
    std::atomic<float> *freq = nullptr, *amt = nullptr, *clarity = nullptr, *target[4] {};
};

//------------------------------------------------------------------------------ TON
class Peq final : public ChainModule
{
public:
    Peq (juce::AudioProcessorValueTreeState& s, int k, std::atomic<bool>& hold) : ChainModule (s, modules::peq, k), listenHold (hold)
    {
        for (int bb = 0; bb < 8; ++bb)
        {
            const char* id[8] { ids::peqOn[bb], ids::peqType[bb], ids::peqFreq[bb], ids::peqGain[bb], ids::peqQ[bb], ids::peqSlope[bb], ids::peqDyn[bb], ids::peqThr[bb] };
            for (int j = 0; j < 8; ++j) p[bb][j] = raw (id[j]);
        }
        listen = raw (ids::peqListen); listenBand = raw (ids::peqListenBand);
    }
    float getBandGainDb (int band) const noexcept { return engine.getBandGainDb (band); }

private:
    void prepareEngine (double rate, int, int) override { engine.prepare (rate); listenMix.reset (rate, 0.03); lastF = -1.0f; }
    void updateEngine (bool on, bool snap) noexcept override
    {
        std::array<ParametricEQ::Band, ParametricEQ::kBands> bands;
        for (int bb = 0; bb < 8; ++bb)
        {
            auto& x = bands[(size_t) bb];
            x.on = b (p[bb][0]); x.type = i (p[bb][1]); x.freq = f (p[bb][2]); x.gainDb = f (p[bb][3]); x.q = f (p[bb][4]);
            x.slope = i (p[bb][5]); x.dyn = b (p[bb][6]); x.thrDb = f (p[bb][7]);
        }
        engine.setBands (bands, snap);
        // écoute d'une bande : seulement pendant qu'on la tient (casque armé, clic maintenu sur la bande)
        const float t = on && b (listen) && listenHold.load (std::memory_order_relaxed) ? 1.0f : 0.0f;
        if (snap) listenMix.setCurrentAndTargetValue (t); else listenMix.setTargetValue (t);
        const auto& lb = bands[(size_t) juce::jlimit (0, 7, i (listenBand))];
        if (lb.freq != lastF || lb.q != lastQ || lb.type != lastType) setListenFilter (lb);
    }
    void processEngine (float* const* ch, int numCh, int n) noexcept override
    {
        engine.process (ch, numCh, n);
        if (! listenMix.isSmoothing() && listenMix.getTargetValue() <= 0.0f) return;
        for (int s = 0; s < n; ++s)
        {
            const float m = sCurve (listenMix.getNextValue());
            for (int c = 0; c < numCh; ++c)
            {
                const float band = listenGain * fb.process (fa.process (sw.getDry (c)[s], c), c);
                ch[c][s] += m * (band - ch[c][s]);
            }
        }
    }
    void setListenFilter (const ParametricEQ::Band& lb) noexcept
    {
        lastF = lb.freq; lastQ = lb.q; lastType = lb.type;
        const double fq = juce::jlimit (25.0, 0.45 * fs, (double) lb.freq);
        listenGain = (lb.type == ParametricEQ::bell || lb.type == ParametricEQ::notch) ? 1.6f : 1.0f;
        if (lb.type == ParametricEQ::lowShelf || lb.type == ParametricEQ::lowCut)
        { fa.setCoeffs (BiquadCoeffs::lowpass (fs, fq, 0.707)); fb.setCoeffs (BiquadCoeffs::lowpass (fs, fq, 0.707)); }
        else if (lb.type == ParametricEQ::highShelf || lb.type == ParametricEQ::highCut)
        { fa.setCoeffs (BiquadCoeffs::highpass (fs, fq, 0.707)); fb.setCoeffs (BiquadCoeffs::highpass (fs, fq, 0.707)); }
        else
        {
            const double qq = juce::jlimit (0.5, 12.0, (double) lb.q);
            fa.setCoeffs (BiquadCoeffs::bandpass (fs, fq, qq)); fb.setCoeffs (BiquadCoeffs::bandpass (fs, fq, qq));
        }
    }
    ParametricEQ engine;
    std::atomic<bool>& listenHold;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> listenMix;
    Biquad fa, fb;
    float lastF = -1.0f, lastQ = -1.0f, listenGain = 1.0f; int lastType = -1;
    std::atomic<float>* p[8][8] {};
    std::atomic<float> *listen = nullptr, *listenBand = nullptr;
};

class Analog final : public ChainModule
{
public:
    Analog (juce::AudioProcessorValueTreeState& s, int k) : ChainModule (s, modules::analog, k)
    {
        low = raw (ids::low); mud = raw (ids::mud); hi = raw (ids::high); air = raw (ids::air); lowFreq = raw (ids::lowFreq); lowCut = raw (ids::lowCut);
    }
    float getGuard() const noexcept { return engine.getGuard(); }

private:
    void prepareEngine (double rate, int, int) override { engine.prepare (rate); }
    void updateEngine (bool, bool snap) noexcept override
    {
        static constexpr float freqs[] { 80.0f, 100.0f, 125.0f };
        engine.setTargets (f (low), f (mud), snap);
        engine.setShine (f (hi), f (air), snap);
        engine.setLowFreq (freqs[juce::jlimit (0, 2, i (lowFreq))], snap);
        engine.setLowCut (b (lowCut), snap);
    }
    void processEngine (float* const* ch, int numCh, int n) noexcept override { engine.process (ch, numCh, n); }
    VocalEQ engine;
    std::atomic<float> *low = nullptr, *mud = nullptr, *hi = nullptr, *air = nullptr, *lowFreq = nullptr, *lowCut = nullptr;
};

/*  SATURATION LAB : quatre émulations (TUBE, ANALOG et TAPE d'origine ; TRANSFORMER : l'ANALOG nourri d'un bas renforcé que
    l'on retire à la sortie, comme un noyau de transformateur qui sature d'abord dans le grave), et autour d'elles :
    DRIVE ; BIAS (un décalage avant la saturation, retiré après : harmoniques paires, son plus « tube ») ; WARMTH (le
    bas-médium poussé dans la saturation puis rendu à la sortie : plus de densité sans plus de grave) ; DYNAMICS (les passages
    forts poussés plus fort dans la saturation, rendus à leur niveau ensuite) ; TONE (bascule grave / aigu après) ; AIR
    (aigus après) ; MIX (avec le signal direct aligné) ; OUTPUT. Tous neutres par défaut : le son d'origine ne change pas. */
class Sat final : public ChainModule
{
public:
    Sat (juce::AudioProcessorValueTreeState& s, int k) : ChainModule (s, modules::sat, k)
    {
        drive = raw (ids::drive); mode = raw (ids::satType); mix = raw (ids::satMix); out = raw (ids::satOutput);
        tone = raw (ids::satTone); warmth = raw (ids::satWarmth); air = raw (ids::satAir); bias = raw (ids::satBias); dyn = raw (ids::satDynamics);
    }
    int getLatencySamples() const noexcept override { return engine.getLatencySamples(); }

private:
    void prepareEngine (double rate, int, int maxBlock) override
    {
        engine.prepare (rate, maxBlock, 0);   // la chaîne est déjà suréchantillonnée
        for (auto* s : { &mixS, &outS, &biasS, &dynS }) s->reset (rate, 0.03);
        for (auto& d : gainDelay) d.prepare (std::max (1, engine.getLatencySamples()));
        gainDelay[0].setLength (engine.getLatencySamples());
        envA = (float) (1.0 - std::exp (-1.0 / (0.005 * rate)));
        envR = (float) (1.0 - std::exp (-1.0 / (0.120 * rate)));
        dcR = (float) std::exp (-2.0 * juce::MathConstants<double>::pi * 12.0 / rate);
        for (auto& l : last) l = 1.0e9f;
    }
    void updateEngine (bool, bool snap) noexcept override
    {
        const int m4 = juce::jlimit (0, 3, i (mode));
        engine.setDrive (f (drive), snap);
        engine.setMode (m4 == 3 ? 1 : m4, snap);
        const float m = f (mix) * 0.01f, g = dbToGain (f (out)), b = f (bias) * 0.01f, dy = f (dyn) * 0.01f;
        if (snap) { mixS.setCurrentAndTargetValue (m); outS.setCurrentAndTargetValue (g); biasS.setCurrentAndTargetValue (b); dynS.setCurrentAndTargetValue (dy); }
        else      { mixS.setTargetValue (m); outS.setTargetValue (g); biasS.setTargetValue (b); dynS.setTargetValue (dy); }
        const float w = f (warmth) * 0.01f, t = f (tone) * 0.01f, a = f (air) * 0.01f, tr = m4 == 3 ? 1.0f : 0.0f;
        if (w != last[0] || t != last[1] || a != last[2] || tr != last[3])
        {
            last[0] = w; last[1] = t; last[2] = a; last[3] = tr;
            const double fq = std::min (0.45 * fs, 11000.0);
            pre.setCoeffs (BiquadCoeffs::peaking (fs, 280.0, 0.7, 7.0 * w));
            post.setCoeffs (BiquadCoeffs::peaking (fs, 280.0, 0.7, -7.0 * w));
            preT.setCoeffs (BiquadCoeffs::lowShelf (fs, 110.0, 0.7, 6.0 * tr));
            postT.setCoeffs (BiquadCoeffs::lowShelf (fs, 110.0, 0.7, -6.0 * tr));
            tiltLo.setCoeffs (BiquadCoeffs::lowShelf (fs, 700.0, 0.6, -4.0 * t));
            tiltHi.setCoeffs (BiquadCoeffs::highShelf (fs, 1800.0, 0.6, 4.0 * t));
            airF.setCoeffs (BiquadCoeffs::highShelf (fs, fq, 0.7, 6.0 * a));
            colour = w > 0.0005f || std::abs (t) > 0.0005f || a > 0.0005f || tr > 0.5f;
        }
    }
    void processEngine (float* const* ch, int numCh, int n) noexcept override
    {
        // avant : BIAS et DYNAMICS (gain qui suit l'enveloppe, rendu ensuite, aligné sur la latence), couleurs d'entrée
        const bool shaping = biasS.isSmoothing() || dynS.isSmoothing() || biasS.getTargetValue() != 0.0f || dynS.getTargetValue() > 0.0f;
        if (shaping)
            for (int s = 0; s < n; ++s)
            {
                float pk = 0.0f;
                for (int c = 0; c < numCh; ++c) pk = std::max (pk, std::abs (ch[c][s]));
                env += (pk > env ? envA : envR) * (pk - env);
                const float gIn = 1.0f + dynS.getNextValue() * 2.0f * std::min (1.0f, env * 2.0f);
                const float b = biasS.getNextValue() * 0.18f;
                gains[(size_t) std::min (s, (int) gains.size() - 1)] = gainDelay[0].process (1.0f / gIn);
                for (int c = 0; c < numCh; ++c) ch[c][s] = ch[c][s] * gIn + b;
            }
        if (colour)
            for (int c = 0; c < numCh; ++c) for (int s = 0; s < n; ++s) ch[c][s] = preT.process (pre.process (ch[c][s], c), c);
        engine.process (ch, numCh, n);
        if (shaping)
            for (int c = 0; c < numCh; ++c)
                for (int s = 0; s < n; ++s)
                {
                    // le décalage retiré (passe-haut de 12 Hz), le gain de DYNAMICS rendu
                    const float x = ch[c][s] * gains[(size_t) std::min (s, (int) gains.size() - 1)];
                    const float y = x - dcX[c] + dcR * dcY[c];
                    dcX[c] = x; dcY[c] = y;
                    ch[c][s] = y;
                }
        if (colour)
            for (int c = 0; c < numCh; ++c)
                for (int s = 0; s < n; ++s)
                    ch[c][s] = airF.process (tiltHi.process (tiltLo.process (post.process (postT.process (ch[c][s], c), c), c), c), c);
        if (! mixS.isSmoothing() && mixS.getTargetValue() >= 1.0f && ! outS.isSmoothing() && outS.getTargetValue() == 1.0f) return;
        for (int s = 0; s < n; ++s)
        {
            const float m = mixS.getNextValue(), g = outS.getNextValue();
            for (int c = 0; c < numCh; ++c) { const float d = sw.getDry (c)[s]; ch[c][s] = (d + m * (ch[c][s] - d)) * g; }
        }
    }
    void resetEngine() noexcept override
    {
        for (auto* q : { &pre, &post, &preT, &postT, &tiltLo, &tiltHi, &airF }) q->reset();
        env = 0.0f; dcX[0] = dcX[1] = dcY[0] = dcY[1] = 0.0f;
    }
    SaturationLab engine;
    Biquad pre, post, preT, postT, tiltLo, tiltHi, airF;
    bool colour = false;
    float last[4] {}, env = 0.0f, envA = 0.1f, envR = 0.01f, dcR = 0.999f, dcX[2] {}, dcY[2] {};
    std::array<float, 8192> gains {};
    DelayLine gainDelay[1];
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixS { 1.0f }, outS { 1.0f }, biasS, dynS;
    std::atomic<float> *drive = nullptr, *mode = nullptr, *mix = nullptr, *out = nullptr, *tone = nullptr, *warmth = nullptr, *air = nullptr,
                       *bias = nullptr, *dyn = nullptr;
};

//------------------------------------------------------------------------------ PREAMP et CL1B
/*  PREAMP : en tête de chaîne, le grain d'un préampli de console (voir PreampN73). MIC / LINE, GAIN, OUTPUT. */
class Preamp final : public ChainModule
{
public:
    Preamp (juce::AudioProcessorValueTreeState& s, int k) : ChainModule (s, modules::preamp, k)
    {
        mic = raw (ids::preampMic); gain = raw (ids::preampGain); out = raw (ids::preampOutput);
    }
    int getLatencySamples() const noexcept override { return engine.getLatencySamples(); }
    float driveNow() const noexcept { return engine.drive(); }

private:
    void prepareEngine (double rate, int, int maxBlock) override { engine.prepare (rate, maxBlock); }
    void updateEngine (bool, bool snap) noexcept override { engine.set (i (mic) == 1, f (gain) + 8.0f * macro (mWarm), f (out), snap); }
    void processEngine (float* const* ch, int numCh, int n) noexcept override { engine.process (ch, numCh, n); }
    void resetEngine() noexcept override { engine.reset(); }
    PreampN73 engine;
    std::atomic<float> *mic = nullptr, *gain = nullptr, *out = nullptr;
};

/*  CL1B : compresseur optique à lampe (le moteur de l'OptoBox, génération moderne, attaque et relâchement manuels). Ses
    crêtes sont tenues en douceur, sans le mordant du 1176 : la cellule suit avec un léger retard et se souvient. */
class Cl1b final : public ChainModule
{
public:
    Cl1b (juce::AudioProcessorValueTreeState& s, int k) : ChainModule (s, modules::cl1b, k)
    {
        thr = raw (ids::cl1bThreshold); ratio = raw (ids::cl1bRatio); atk = raw (ids::cl1bAttack); rel = raw (ids::cl1bRelease);
        gain = raw (ids::cl1bGain); mix = raw (ids::cl1bMix);
    }
    float getReductionDb() const noexcept override { return -std::abs (engine.reductionDb()); }
    float needle() const noexcept { return engine.needle(); }

private:
    void prepareEngine (double rate, int, int) override { engine.setSettings (settings()); engine.prepare (rate, 2); }
    void updateEngine (bool, bool) noexcept override { engine.setSettings (settings()); }
    void processEngine (float* const* ch, int numCh, int n) noexcept override
    {
        if (numCh == 1) { const float* in[1] { ch[0] }; engine.process (in, ch, n, nullptr, 0); return; }
        const float* in[2] { ch[0], ch[1] };
        engine.process (in, ch, n, nullptr, 0);
    }
    void resetEngine() noexcept override { engine.reset(); }
    obx::Settings settings() const noexcept
    {
        obx::Settings s;
        s.thresholdDb = f (thr) - 8.0f * macro (mHold); s.ratio = f (ratio); s.attackMs = f (atk); s.releaseMs = f (rel);
        s.outputDb = f (gain); s.parallel = f (mix) * 0.01f;
        s.timing = obx::Timing::manual; s.generation = obx::Generation::modern;
        return s;
    }
    obx::Compressor engine;
    std::atomic<float> *thr = nullptr, *ratio = nullptr, *atk = nullptr, *rel = nullptr, *gain = nullptr, *mix = nullptr;
};

//------------------------------------------------------------------------------ TUNER
/*  TUNER : le moteur d'ONIRIQUE TUNER (détection de hauteur, correction façon Auto-Tune, PSOLA), toujours en HQ : la voix
    est retardée et la hauteur lissée avec de l'avance, le plus transparent. La latence suit le registre : la chaîne se reconstruit (10 ms de fondu) et le DAW est prévenu. */
class Tuner final : public ChainModule
{
public:
    Tuner (juce::AudioProcessorValueTreeState& s, int k) : ChainModule (s, modules::tuner, k)
    {
        key = raw (ids::tuneKey); scale = raw (ids::tuneScale); retune = raw (ids::tuneRetune); hum = raw (ids::tuneHumanize);
        flex = raw (ids::tuneFlex); vib = raw (ids::tuneVibrato); formant = raw (ids::tuneFormant); voice = raw (ids::tuneVoice);
        hard = raw (ids::tuneHard); mix = raw (ids::tuneMix);
        variableLatency = true;
    }
    // la latence que donnent les réglages (le moteur la prend à son prochain passage, en fondu)
    int getLatencySamples() const noexcept override { return tune::TuneEngine::latencyFor (fs, true, juce::jlimit (0, 2, i (voice))); }
    int getMaxLatencySamples() const noexcept override { return tune::TuneEngine::latencyFor (fs, true, 2); }
    bool popFrame (tune::DisplayFrame& f) noexcept { return engine.popDisplayFrame (f); }

private:
    tune::EngineParams params() const noexcept
    {
        tune::EngineParams p;
        p.hq = true;   // toujours HQ : le plus transparent (la latence est compensée par le DAW)
        p.inputType = juce::jlimit (0, 2, i (voice));
        const int sc = juce::jlimit (0, tune::numScales - 1, i (scale));
        p.noteMask = tune::pitchClassMask (tune::scales[sc].mask, juce::jlimit (0, 11, i (key)));
        p.retuneMs = f (retune);
        p.hardTune = b (hard);
        p.humanize = f (hum) * 0.01f;
        p.flex = f (flex) * 0.01f;
        p.vibrato = f (vib) * 0.01f;
        p.formant = b (formant);
        p.mix = f (mix) * 0.01f;
        return p;
    }
    void prepareEngine (double rate, int, int) override { fs = rate; engine.setParams (params()); engine.prepare (rate); }
    void updateEngine (bool, bool) noexcept override { engine.setParams (params()); }
    void processEngine (float* const* ch, int numCh, int n) noexcept override
    {
        // une voix mono sur une piste stéréo : un seul canal accordé, recopié (moitié moins de calcul) ; si la stéréo revient,
        // le moteur repart à zéro (il n'a pas suivi le canal droit)
        bool same = numCh == 2;
        for (int i = 0; i < n && same; ++i) same = ch[0][i] == ch[1][i];
        if (! same && monoRun) engine.reset();
        monoRun = same;
        engine.process (ch, same ? 1 : numCh, n);
        if (same) std::copy (ch[0], ch[0] + n, ch[1]);
    }
    void resetEngine() noexcept override { engine.reset(); }
    bool monoRun = false;
    tune::TuneEngine engine;
    std::atomic<float> *key = nullptr, *scale = nullptr, *retune = nullptr, *hum = nullptr, *flex = nullptr, *vib = nullptr,
                       *formant = nullptr, *voice = nullptr, *hard = nullptr, *mix = nullptr;
};

//------------------------------------------------------------------------------ PITCHER (le moteur du TUNER, sans correction)
/*  PITCHER : ta vraie voix décalée, comme une pédale de pitch posée sur la voix. Le moteur du TUNER (PSOLA, HQ) sans
    correction : la voix garde sa justesse et ses inflexions, seulement plus haut ou plus bas. FORMANT déplace le timbre
    (gorge plus courte ou plus longue) sans toucher à la hauteur. SNAP accroche au demi-ton, ROBOT fige chaque phrase sur
    sa première note. DRIVE sature la voix décalée seule ; MIX la remet sur la voix d'origine (alignée). Latence fixe. */
class PitcherM final : public ChainModule
{
public:
    PitcherM (juce::AudioProcessorValueTreeState& s, int k) : ChainModule (s, modules::pitcher, k)
    {
        pitch = raw (ids::pchPitch); formant = raw (ids::pchFormant); fine = raw (ids::pchDetune); drive = raw (ids::pchDrive);
        robot = raw (ids::pchRobot); snap = raw (ids::pchSnap); mix = raw (ids::pchMix);
    }
    int getLatencySamples() const noexcept override { return tune::TuneEngine::latencyFor (fs, true, 1); }
    bool popFrame (tune::DisplayFrame& fr) noexcept { return engine.popDisplayFrame (fr); }

private:
    tune::EngineParams params() const noexcept
    {
        tune::EngineParams p;
        p.hq = true;
        p.inputType = 1;
        p.transpose = f (pitch) + f (fine) * 0.01f;
        // FORMANT en demi-tons : + = gorge plus courte (plus clair), - = plus longue (plus sombre)
        p.formant = true;
        p.throat = std::pow (2.0f, -f (formant) / 12.0f);
        if (b (snap) && ! b (robot))
        {
            p.correct = true; p.hardTune = true; p.noteMask = 0x0fff; p.retuneMs = 0.0f;   // au demi-ton, sauts francs
        }
        else
        {
            p.correct = false; p.monotone = b (robot);
        }
        p.mix = 1.0f;   // le mélange se fait ici, après le DRIVE
        return p;
    }
    void prepareEngine (double rate, int, int maxBlock) override
    {
        fs = rate;
        engine.setParams (params());
        engine.prepare (rate);
        lat = getLatencySamples();
        for (auto& d : dry) { d.assign ((size_t) (lat + 1), 0.0f); }
        wpos = 0;
        tmp.setSize (2, std::max (1, maxBlock));
        lastMix = f (mix) * 0.01f; lastDrive = f (drive) * 0.01f;
    }
    void updateEngine (bool, bool) noexcept override { engine.setParams (params()); }
    void processEngine (float* const* ch, int numCh, int n) noexcept override
    {
        numCh = std::min (numCh, 2);
        if (n > tmp.getNumSamples()) return;   // ne doit pas arriver (bloc plus grand que prévu)
        // la voix d'origine, retardée de la latence du moteur, pour le MIX
        for (int c = 0; c < numCh; ++c) tmp.copyFrom (c, 0, ch[c], n);
        bool same = numCh == 2;
        for (int s = 0; s < n && same; ++s) same = ch[0][s] == ch[1][s];
        if (! same && monoRun) engine.reset();
        monoRun = same;
        engine.process (ch, same ? 1 : numCh, n);
        if (same) std::copy (ch[0], ch[0] + n, ch[1]);

        const float mixT = f (mix) * 0.01f, driveT = f (drive) * 0.01f;
        const int len = lat + 1;
        // DRIVE en gain match automatique : on compare l'énergie de la voix décalée avant et après saturation (~80 ms) et on
        // remet la saturée au même niveau (lissé ~30 ms) ; tourner DRIVE change le grain, pas le volume
        const float aE = 1.0f - std::exp (-1.0f / (0.08f * (float) fs)), aG = 1.0f - std::exp (-1.0f / (0.03f * (float) fs));
        for (int s = 0; s < n; ++s)
        {
            const float a = (float) s / (float) n;
            const float m = lastMix + (mixT - lastMix) * a, d = lastDrive + (driveT - lastDrive) * a;
            const float k = 1.0f + 11.0f * d * d;
            const int rpos = (wpos + 1) % len;
            float pre = 0.0f, post = 0.0f, wet[2] {};
            for (int c = 0; c < numCh; ++c)
            {
                const float w = ch[c][s];
                const float y = d > 0.0005f ? (1.0f - d) * w + d * std::tanh (k * w) : w;
                pre += w * w; post += y * y; wet[c] = y;
            }
            ePre += aE * (pre - ePre); ePost += aE * (post - ePost);
            const float target = d > 0.0005f ? juce::jlimit (0.05f, 2.0f, std::sqrt ((ePre + 1.0e-10f) / (ePost + 1.0e-10f))) : 1.0f;
            match += aG * (target - match);
            for (int c = 0; c < numCh; ++c)
            {
                auto& line = dry[(size_t) c];
                line[(size_t) wpos] = tmp.getSample (c, s);
                const float dl = line[(size_t) rpos];
                ch[c][s] = dl + m * (wet[c] * match - dl);
            }
            wpos = rpos;
        }
        lastMix = mixT; lastDrive = driveT;
    }
    void resetEngine() noexcept override { engine.reset(); for (auto& d : dry) std::fill (d.begin(), d.end(), 0.0f); ePre = ePost = 0.0f; match = 1.0f; }

    tune::TuneEngine engine;
    std::array<std::vector<float>, 2> dry;
    juce::AudioBuffer<float> tmp;
    int lat = 0, wpos = 0;
    bool monoRun = false;
    float lastMix = 1.0f, lastDrive = 0.0f, ePre = 0.0f, ePost = 0.0f, match = 1.0f;   // gain match du DRIVE
public:
    std::atomic<float> *pitch = nullptr, *formant = nullptr, *fine = nullptr, *drive = nullptr, *robot = nullptr, *snap = nullptr, *mix = nullptr;
};

//------------------------------------------------------------------------------ DE-CLICK, DE-BREATH, DE-REVERB (cadence de l'hôte)
class DeClickM final : public ChainModule
{
public:
    DeClickM (juce::AudioProcessorValueTreeState& s, int k) : ChainModule (s, modules::declick, k)
    { sens = raw (ids::dclkSens); amt = raw (ids::dclkAmount); listen = raw (ids::dclkListen); }
    int getLatencySamples() const noexcept override { return engine.getLatencySamples(); }
    int takeClicks() noexcept { return engine.takeClicks(); }
    float lastClickDb() const noexcept { return engine.lastClickDb(); }
    float activityDb() const noexcept override { return engine.recentClickDb(); }
private:
    void prepareEngine (double rate, int, int) override { engine.prepare (rate); }
    void updateEngine (bool, bool) noexcept override { engine.setParams (f (sens) * 0.01f, f (amt) * 0.01f, b (listen)); }
    void processEngine (float* const* ch, int numCh, int n) noexcept override { engine.process (ch, numCh, n); }
    void resetEngine() noexcept override { engine.reset(); }
    DeClick engine;
    std::atomic<float> *sens = nullptr, *amt = nullptr, *listen = nullptr;
};

class DeBreathM final : public ChainModule
{
public:
    DeBreathM (juce::AudioProcessorValueTreeState& s, int k) : ChainModule (s, modules::debreath, k)
    { red = raw (ids::dbrReduction); sens = raw (ids::dbrSens); listen = raw (ids::dbrListen); }
    int getLatencySamples() const noexcept override { return engine.getLatencySamples(); }
    float getReductionDb() const noexcept override { return engine.getReductionDb(); }
private:
    void prepareEngine (double rate, int, int) override { engine.prepare (rate); }
    void updateEngine (bool, bool) noexcept override { engine.setParams (f (red), f (sens) * 0.01f, b (listen)); }
    void processEngine (float* const* ch, int numCh, int n) noexcept override { engine.process (ch, numCh, n); }
    void resetEngine() noexcept override { engine.reset(); }
    DeBreath engine;
    std::atomic<float> *red = nullptr, *sens = nullptr, *listen = nullptr;
};

class DeReverbM final : public ChainModule
{
public:
    DeReverbM (juce::AudioProcessorValueTreeState& s, int k) : ChainModule (s, modules::dereverb, k)
    { amt = raw (ids::drvAmount); tail = raw (ids::drvTail); autoT = raw (ids::drvAuto); listen = raw (ids::drvListen); }
    int getLatencySamples() const noexcept override { return engine.getLatencySamples(); }
    float getReductionDb() const noexcept override { return -engine.getReductionDb(); }
    float tailSeconds() const noexcept { return engine.getTailSeconds(); }
    bool tailMeasured() const noexcept { return engine.tailMeasured(); }
private:
    void prepareEngine (double rate, int, int) override { engine.prepare (rate); }
    void updateEngine (bool, bool) noexcept override { engine.setParams (f (amt) * 0.01f, f (tail), b (autoT), b (listen)); }
    void processEngine (float* const* ch, int numCh, int n) noexcept override { engine.process (ch, numCh, n); }
    void resetEngine() noexcept override { engine.reset(); }
    DeReverb engine;
    std::atomic<float> *amt = nullptr, *tail = nullptr, *autoT = nullptr, *listen = nullptr;
};

//------------------------------------------------------------------------------ DYNAMIQUE
class Fet final : public ChainModule
{
public:
    Fet (juce::AudioProcessorValueTreeState& s, int k) : ChainModule (s, modules::fet, k)
    {
        in = raw (ids::fetIn); out = raw (ids::fetOut); atk = raw (ids::fetAttack); rel = raw (ids::fetRelease); ratio = raw (ids::fetRatio);
    }
    float getReductionDb() const noexcept override { return engine.getGainReductionDb(); }

private:
    void prepareEngine (double rate, int, int) override { engine.prepare (rate); }
    void updateEngine (bool, bool snap) noexcept override { engine.setParams (juce::jlimit (-12.0f, 36.0f, f (in) + 6.0f * macro (mHold)), f (out), f (atk), f (rel), i (ratio), snap); }
    void processEngine (float* const* ch, int numCh, int n) noexcept override { engine.process (ch, numCh, n); }
    void resetEngine() noexcept override { engine.reset(); }
    FetCompressor engine;
    std::atomic<float> *in = nullptr, *out = nullptr, *atk = nullptr, *rel = nullptr, *ratio = nullptr;
};

class Opto final : public ChainModule
{
public:
    Opto (juce::AudioProcessorValueTreeState& s, int k) : ChainModule (s, modules::opto, k) { gain = raw (ids::laGain); peak = raw (ids::laPeak); limit = raw (ids::laLimit); }
    float getReductionDb() const noexcept override { return engine.getGainReductionDb(); }

private:
    void prepareEngine (double rate, int, int) override { engine.prepare (rate); }
    void updateEngine (bool, bool snap) noexcept override { engine.setParams (f (gain), juce::jlimit (0.0f, 100.0f, f (peak) + 20.0f * macro (mHold)), b (limit), snap); }
    void processEngine (float* const* ch, int numCh, int n) noexcept override { engine.process (ch, numCh, n); }
    void resetEngine() noexcept override { engine.reset(); }
    OptoCompressor engine;
    std::atomic<float> *gain = nullptr, *peak = nullptr, *limit = nullptr;
};

class Multi final : public ChainModule
{
public:
    Multi (juce::AudioProcessorValueTreeState& s, int k) : ChainModule (s, modules::multi, k)
    {
        for (int j = 0; j < 3; ++j) x[j] = raw (ids::mbX[j]);
        for (int bb = 0; bb < 4; ++bb) { thr[bb] = raw (ids::mbThr[bb]); gain[bb] = raw (ids::mbGain[bb]); solo[bb] = raw (ids::mbSolo[bb]); }
        atk = raw (ids::mbAttack); rel = raw (ids::mbRelease); out = raw (ids::mbOutput);
    }
    const MultibandOpto::Meter& getMeter() const noexcept { return engine.getMeter(); }
    float activityDb() const noexcept override { float m = 0.0f; for (auto& r : engine.getMeter().grDb) m = std::max (m, std::abs (r.load (std::memory_order_relaxed))); return m; }

private:
    void prepareEngine (double rate, int, int) override { engine.prepare (rate); outGain.reset (rate, 0.03); }
    void updateEngine (bool, bool snap) noexcept override
    {
        MultibandOpto::Params q;
        q.x1 = f (x[0]); q.x2 = f (x[1]); q.x3 = f (x[2]);
        std::array<bool, 4> so {};
        for (int bb = 0; bb < 4; ++bb) { q.thr[(size_t) bb] = f (thr[bb]); q.gain[(size_t) bb] = f (gain[bb]); so[(size_t) bb] = b (solo[bb]); }
        q.attackMs = f (atk); q.releaseMs = f (rel);
        engine.setParams (q, snap);
        engine.setSolo (so, snap);
        if (snap) outGain.setCurrentAndTargetValue (dbToGain (f (out))); else outGain.setTargetValue (dbToGain (f (out)));
    }
    void processEngine (float* const* ch, int numCh, int n) noexcept override
    {
        engine.process (ch, numCh, n);
        for (int s = 0; s < n; ++s) { const float g = outGain.getNextValue(); for (int c = 0; c < numCh; ++c) ch[c][s] *= g; }
    }
    void resetEngine() noexcept override { engine.reset(); }
    MultibandOpto engine;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> outGain { 1.0f };
    std::atomic<float> *x[3] {}, *thr[4] {}, *gain[4] {}, *solo[4] {}, *atk = nullptr, *rel = nullptr, *out = nullptr;
};

class Parallel final : public ChainModule
{
public:
    Parallel (juce::AudioProcessorValueTreeState& s, int k) : ChainModule (s, modules::parallel, k)
    {
        amount = raw (ids::parAmount); autoM = raw (ids::parAuto); thr = raw (ids::parThresh); atk = raw (ids::parAttack); rel = raw (ids::parRelease);
        dsAmt = raw (ids::parDeessAmt); dsFreq = raw (ids::parDeessFreq);
    }
    float getReductionDb() const noexcept override { return engine.getGainReductionDb(); }
    float getDeEssDb() const noexcept { return engine.getDeEssReductionDb(); }

private:
    void prepareEngine (double rate, int, int maxBlock) override { engine.prepare (rate, maxBlock); }
    void updateEngine (bool, bool snap) noexcept override
    {
        engine.setAmount (juce::jlimit (0.0f, 1.0f, f (amount) * 0.01f * (1.0f + macro (mHold))), snap);
        engine.setControl (b (autoM), f (thr), f (atk), f (rel), snap);
        engine.setDeEsser (f (dsFreq), f (dsAmt) * 0.01f, snap);
    }
    void processEngine (float* const* ch, int numCh, int n) noexcept override { engine.process (ch, numCh, n); }
    void resetEngine() noexcept override { engine.reset(); }
    ParallelComp engine;
    std::atomic<float> *amount = nullptr, *autoM = nullptr, *thr = nullptr, *atk = nullptr, *rel = nullptr, *dsAmt = nullptr, *dsFreq = nullptr;
};

//------------------------------------------------------------------------------ EFFETS (cadence de l'hôte)
template <typename Engine, int Type>
class AmountFx final : public ChainModule   // PHONE, DOUBLER : un bouton, fondu à zéro quand on coupe
{
public:
    AmountFx (juce::AudioProcessorValueTreeState& s, int k, const char* amountId) : ChainModule (s, Type, k) { amt = raw (amountId); }
    bool selfSwitch() const noexcept override { return true; }

private:
    void prepareEngine (double rate, int, int) override { engine.prepare (rate); }
    void updateEngine (bool on, bool) noexcept override { engine.set (on ? f (amt) * 0.01f : 0.0f, false); }
    void processEngine (float* const* ch, int numCh, int n) noexcept override { engine.process (ch, numCh, n); }
    Engine engine;
    std::atomic<float>* amt = nullptr;
};

class Chorus final : public ChainModule
{
public:
    Chorus (juce::AudioProcessorValueTreeState& s, int k) : ChainModule (s, modules::chorus, k) { rate = raw (ids::chorusRate); depth = raw (ids::chorusDepth); mix = raw (ids::chorusMix); }
    bool selfSwitch() const noexcept override { return true; }

private:
    void prepareEngine (double r, int, int) override { engine.prepare (r); }
    void updateEngine (bool on, bool) noexcept override { engine.set (on, f (rate), f (depth) * 0.01f, f (mix) * 0.01f); }
    void processEngine (float* const* ch, int numCh, int n) noexcept override { engine.process (ch, numCh, n); }
    fx::Chorus engine;
    std::atomic<float> *rate = nullptr, *depth = nullptr, *mix = nullptr;
};

class Flanger final : public ChainModule
{
public:
    Flanger (juce::AudioProcessorValueTreeState& s, int k) : ChainModule (s, modules::flanger, k)
    {
        rate = raw (ids::flangerRate); depth = raw (ids::flangerDepth); fb = raw (ids::flangerFeedback); mix = raw (ids::flangerMix);
    }
    bool selfSwitch() const noexcept override { return true; }

private:
    void prepareEngine (double r, int, int) override { engine.prepare (r); }
    void updateEngine (bool on, bool) noexcept override { engine.set (on, f (rate), f (depth) * 0.01f, f (fb) * 0.01f, f (mix) * 0.01f); }
    void processEngine (float* const* ch, int numCh, int n) noexcept override { engine.process (ch, numCh, n); }
    fx::Flanger engine;
    std::atomic<float> *rate = nullptr, *depth = nullptr, *fb = nullptr, *mix = nullptr;
};

// CREATIVE FX : DOUBLER, CHORUS, FLANGER et PHONE du même exemplaire, en série ; chacun suit son interrupteur et ses réglages.
// Il fait tourner les modules eux-mêmes (leurs mètres et leurs écrans restent valables) : un exemplaire de ces quatre effets
// n'est donc jamais à la fois seul dans la chaîne et dans un CREATIVE FX (le processeur l'évite à l'ajout).
class CreativeFx final : public ChainModule
{
public:
    CreativeFx (juce::AudioProcessorValueTreeState& s, int k) : ChainModule (s, modules::creative, k) {}
    void setParts (ChainModule* d, ChainModule* c, ChainModule* f, ChainModule* p) noexcept { parts = { d, c, f, p }; }
    ChainModule* part (int i) const noexcept { return parts[(size_t) juce::jlimit (0, 3, i)]; }

private:
    void prepareEngine (double, int, int) override {}   // les quatre modules sont préparés avec les autres
    void updateEngine (bool, bool snap) noexcept override { for (auto* m : parts) if (m != nullptr) m->update (snap); }
    void processEngine (float* const* ch, int numCh, int n) noexcept override { for (auto* m : parts) if (m != nullptr) m->process (ch, numCh, n); }
    void resetEngine() noexcept override { for (auto* m : parts) if (m != nullptr) m->arrive(); }
    std::array<ChainModule*, 4> parts {};
};

// DELAY et REVERB : des pistes auxiliaires (la voix reste à 100 %, leur retour s'ajoute)
class Delay final : public ChainModule
{
public:
    Delay (juce::AudioProcessorValueTreeState& s, int k, const std::atomic<double>& tempo) : ChainModule (s, modules::delay, k), bpm (tempo)
    {
        sync = raw (ids::delaySync); time = raw (ids::delayTime); note = raw (ids::delayNote); fbk = raw (ids::delayFeedback); mix = raw (ids::delayMix);
        pp = raw (ids::delayPingPong); lc = raw (ids::delayLowCut); hc = raw (ids::delayHighCut); duck = raw (ids::delayDucking);
    }
    bool selfSwitch() const noexcept override { return true; }
    bool isSend() const noexcept override { return true; }
    void processSendInto (const float* const* in, float* const* wet, int numCh, int n) noexcept override { engine.processSend (in, wet, numCh, n); }

private:
    void prepareEngine (double r, int, int) override { engine.prepare (r); }
    void updateEngine (bool on, bool) noexcept override
    {
        const double t = juce::jlimit (30.0, 300.0, bpm.load (std::memory_order_relaxed));
        const float sec = b (sync) ? (float) (60.0 / t * delayNoteBeats (i (note))) : f (time) * 0.001f;
        engine.set (on, sec, f (fbk) * 0.01f, juce::jlimit (0.0f, 1.0f, f (mix) * 0.01f * dbToGain (6.0f * macro (mSpace))), b (pp), f (lc), f (hc), f (duck) * 0.01f);
    }
    void processEngine (float* const*, int, int) noexcept override {}
    fx::StereoDelay engine;
    const std::atomic<double>& bpm;
    std::atomic<float> *sync = nullptr, *time = nullptr, *note = nullptr, *fbk = nullptr, *mix = nullptr, *pp = nullptr, *lc = nullptr, *hc = nullptr, *duck = nullptr;
};

class Reverb final : public ChainModule
{
public:
    Reverb (juce::AudioProcessorValueTreeState& s, int k) : ChainModule (s, modules::reverb, k)
    {
        const char* id[13] { ids::reverbMode, ids::reverbPreDelay, ids::reverbDecay, ids::reverbSize, ids::reverbDiffusion, ids::reverbModRate,
                             ids::reverbModDepth, ids::reverbMix, ids::reverbLowCut, ids::reverbHighCut, ids::reverbDucking, ids::reverbWidth, ids::reverbClean };
        for (int j = 0; j < 13; ++j) p[j] = raw (id[j]);
    }
    bool selfSwitch() const noexcept override { return true; }
    bool isSend() const noexcept override { return true; }
    void processSendInto (const float* const* in, float* const* wet, int numCh, int n) noexcept override { engine.processSend (in, wet, numCh, n); }
    float getTail() const noexcept { return engine.getTailLevel(); }
    const mini::VoiceReverb& getEngine() const noexcept { return engine.getEngine(); }

private:
    void prepareEngine (double r, int, int) override { engine.prepare (r); }
    void updateEngine (bool on, bool) noexcept override
    {
        fx::VocalReverb::Params q;
        q.on = on; q.mode = i (p[0]); q.preDelayMs = f (p[1]); q.decay = f (p[2]); q.size = f (p[3]) * 0.01f; q.diffusion = f (p[4]) * 0.01f;
        q.modRate = f (p[5]); q.modDepth = f (p[6]) * 0.01f; q.mix = juce::jlimit (0.0f, 1.0f, f (p[7]) * 0.01f * dbToGain (6.0f * macro (mSpace))); q.lowCut = f (p[8]); q.highCut = f (p[9]);
        q.ducking = f (p[10]) * 0.01f; q.width = f (p[11]) * 0.01f; q.clean = f (p[12]) * 0.01f;
        engine.set (q);
    }
    void processEngine (float* const*, int, int) noexcept override {}
    fx::VocalReverb engine;
    std::atomic<float>* p[13] {};
};
} // namespace chainmods

//==============================================================================
/*  La chaîne complète : INPUT -> les modules dans l'ordre des emplacements -> AUTO GAIN -> OUTPUT, + BYPASS global.

    - Les modules d'une même cadence qui se suivent partagent un seul aller-retour de suréchantillonnage (le NOISE GATE et le
      NOISE SUPPRESSOR, puis les effets, tournent à la cadence de l'hôte ; tout le reste est suréchantillonné).
    - Ajouter, retirer ou déplacer un module : la sortie descend en 10 ms, la chaîne se reconstruit (rien n'est alloué : tous
      les exemplaires sont prêts depuis prepare()), elle remonte en 10 ms. La latence annoncée au DAW suit la chaîne.
    - AUTO GAIN compare la voix à l'entrée (après le NOISE GATE / NOISE SUPPRESSOR de tête) et après le dernier module qui
      n'est pas un effet : les effets ne font pas varier sa correction (comme avant).
    - Un DELAY suivi d'une REVERB : deux pistes auxiliaires nourries par la même voix (comme la page FX d'avant).
    - Aucune allocation dans process(), blocs de 512 échantillons, NaN / Inf neutralisés, sortie bornée (garde-fou). */
class ModularChain
{
public:
    static constexpr int kChunk = 512;
    static constexpr int kModules = modules::numTypes * kInstances;
    static constexpr int kMaxSegments = kSlots / 2 + 1;

    ModularChain (juce::AudioProcessorValueTreeState& s, const std::atomic<double>& tempo, std::atomic<bool>& peqHold) : apvts (s)
    {
        using namespace chainmods;
        for (int k = 0; k < kInstances; ++k)
        {
            auto put = [&] (int type, ChainModule* m) { mods[(size_t) (type * kInstances + k)].reset (m); };
            put (modules::gate, new Gate (s, k));
            put (modules::noise, new Noise (s, k));
            put (modules::plosive, new Plosive (s, k));
            put (modules::reso, new Reso (s, k));
            put (modules::deess, new DeEss (s, k));
            put (modules::peq, new Peq (s, k, peqHold));
            put (modules::analog, new Analog (s, k));
            put (modules::sat, new Sat (s, k));
            put (modules::fet, new Fet (s, k));
            put (modules::opto, new Opto (s, k));
            put (modules::multi, new Multi (s, k));
            put (modules::parallel, new Parallel (s, k));
            put (modules::doubler, new AmountFx<mini::Doubler, modules::doubler> (s, k, ids::doublerAmount));
            put (modules::chorus, new Chorus (s, k));
            put (modules::flanger, new Flanger (s, k));
            put (modules::phone, new AmountFx<mini::Phone, modules::phone> (s, k, ids::phoneAmount));
            put (modules::delay, new Delay (s, k, tempo));
            put (modules::reverb, new Reverb (s, k));
            put (modules::creative, new CreativeFx (s, k));
            put (modules::preamp, new Preamp (s, k));
            put (modules::cl1b, new Cl1b (s, k));
            put (modules::tuner, new Tuner (s, k));
            put (modules::declick, new DeClickM (s, k));
            put (modules::debreath, new DeBreathM (s, k));
            put (modules::dereverb, new DeReverbM (s, k));
            put (modules::pitcher, new PitcherM (s, k));
            static_cast<CreativeFx*> (module (modules::creative, k))->setParts (module (modules::doubler, k), module (modules::chorus, k),
                                                                              module (modules::flanger, k), module (modules::phone, k));
        }
        for (int j = 0; j < kSlots; ++j) pSlot[j] = s.getRawParameterValue (ids::slot[j]);
        pInput = s.getRawParameterValue (ids::input);
        pOutput = s.getRawParameterValue (ids::output);
        pAutoGain = s.getRawParameterValue (ids::autoGain);
        pBypass = s.getRawParameterValue (ids::bypass);
        pPres = s.getRawParameterValue (ids::macroPresence);
        pWarm = s.getRawParameterValue (ids::macroWarmth);
        pPan = s.getRawParameterValue (ids::outPan);
    }

    ChainModule* module (int type, int instance) const noexcept
    {
        if (type < 0 || type >= modules::numTypes || instance < 0 || instance >= kInstances) return nullptr;
        return mods[(size_t) (type * kInstances + instance)].get();
    }

    // oversamplingOrder : 0 = OFF, 1..3 = x2..x8 (allocation : thread de l'interface, audio suspendu)
    void prepare (double sampleRate, int oversamplingOrder)
    {
        fs = sampleRate;
        osOrder = juce::jlimit (0, 3, oversamplingOrder);
        osFactor = 1 << osOrder;
        for (auto& o : os)
        {
            o = std::make_unique<juce::dsp::Oversampling<float>> (2);
            if (osOrder == 0) o->addDummyOversamplingStage();
            for (int st = 0; st < osOrder; ++st)
                o->addOversamplingStage (juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple,
                                         st == 0 ? 0.05f : 0.20f, -110.0f, st == 0 ? 0.05f : 0.20f, -110.0f);
            o->setUsingIntegerLatency (false);
            o->initProcessing ((size_t) kChunk);
        }
        const int osLatencyOS = juce::roundToInt (os[0]->getLatencyInSamples() * osFactor);
        osPad = (osFactor - osLatencyOS % osFactor) % osFactor;
        osLatencyHost = (osLatencyOS + osPad) / osFactor;
        const int blockOS = kChunk * osFactor;
        for (auto& seg : segPad) for (auto& d : seg) d.prepare (4 * osFactor + osPad);

        for (auto& m : mods)
        {
            const bool over = modules::info (m->type).domain == modules::oversampled;
            m->prepare (over ? fs * osFactor : fs, over ? osFactor : 1, over ? blockOS : kChunk);
        }
        autoGain.prepare (fs);
        maxLatency = juce::roundToInt (fs * 2.0);
        for (auto& d : dryDelay) d.prepare (maxLatency);
        inGain.reset (fs, 0.02); outGain.reset (fs, 0.02); wetMix.reset (fs, 0.02);
        cmpMix.reset (fs, 0.03);
        eCoef = (float) std::exp (-1.0 / (2.0 * fs));
        eDry = eWet = 0.0f; matchGain = 1.0f;
        planStep = 1.0f / (float) std::max (1, juce::roundToInt (0.010 * fs));
        for (auto& w : wetBuf) w.assign ((size_t) kChunk, 0.0f);

        readSlots (pending);
        applyPlan (pending);
        planGain = 1.0f;
        updateParams (true);
    }

    // la latence de la chaîne actuelle (à la cadence de l'hôte) ; le processeur l'annonce au DAW quand elle change
    int getLatencySamples() const noexcept { return latency.load (std::memory_order_relaxed); }
    // latence qu'aurait une chaîne donnée (thread de l'interface, pour l'annoncer dès le changement)
    int latencyFor (const modules::Chain& c) const noexcept { Plan p; build (c, p); return p.latency; }

    void process (float* const* ch, int numCh, int n) noexcept
    {
        modules::Chain now;
        readSlots (now);
        if (now != pending) pending = now;
        updateParams (false);
        // un module dont la latence a changé (TUNER : le registre) : la chaîne se reconstruit pour rester alignée
        if (planVariable && ! relatch)
        {
            Plan probe;
            build (plan.codes, probe);
            relatch = probe.latency != plan.latency;
        }
        for (int pos = 0; pos < n; pos += kChunk)
        {
            const int m = std::min (kChunk, n - pos);
            float* c[2] { ch[0] + pos, numCh > 1 ? ch[1] + pos : nullptr };
            processChunk (c, numCh, m);
        }
    }

    // COMPARER : la prise brute, remontée au volume du mix (une écoute de l'interface, ni réglage ni preset)
    std::atomic<bool> compare { false };
    float getAutoGainDb() const noexcept { return autoGain.getGainDb(); }
    float consumeInputLevel (int c) noexcept  { return inLevel[c & 1].exchange (0.0f, std::memory_order_relaxed); }
    float consumeOutputLevel (int c) noexcept { return outLevel[c & 1].exchange (0.0f, std::memory_order_relaxed); }

private:
    struct Step { ChainModule* m = nullptr; bool over = false; };
    struct Plan
    {
        std::array<Step, kSlots> steps {};
        int count = 0, refPoint = 0, agPoint = 0, latency = 0;
        modules::Chain codes {};
    };

    void readSlots (modules::Chain& c) const noexcept
    {
        for (int j = 0; j < kSlots; ++j) c[(size_t) j] = juce::roundToInt (pSlot[j]->load (std::memory_order_relaxed));
    }

    void build (const modules::Chain& c, Plan& p) const noexcept
    {
        p = Plan {};
        p.codes = c;
        for (int code : c)
        {
            if (! modules::valid (code)) continue;
            // un même exemplaire n'est traité qu'une fois (emplacements en double : le second est ignoré)
            auto* m = module (modules::typeOf (code), modules::instanceOf (code));
            bool dup = false;
            for (int j = 0; j < p.count; ++j) dup = dup || p.steps[(size_t) j].m == m;
            if (dup || m == nullptr) continue;
            p.steps[(size_t) p.count++] = { m, modules::info (m->type).domain == modules::oversampled };
        }
        while (p.refPoint < p.count && (p.steps[(size_t) p.refPoint].m->type == modules::gate || p.steps[(size_t) p.refPoint].m->type == modules::noise))
            ++p.refPoint;
        p.agPoint = 0;
        for (int j = 0; j < p.count; ++j)
            if (modules::info (p.steps[(size_t) j].m->type).group != 3) p.agPoint = j + 1;
        p.agPoint = std::max (p.agPoint, p.refPoint);

        // latence : modules à la cadence de l'hôte + chaque aller-retour suréchantillonné (filtres + modules, au multiple)
        int lat = 0;
        for (int j = 0; j < p.count;)
        {
            if (! p.steps[(size_t) j].over) { lat += p.steps[(size_t) j].m->getLatencySamples(); ++j; continue; }
            int sum = 0;
            const int end = segmentEnd (p, j);
            for (; j < end; ++j) sum += p.steps[(size_t) j].m->getLatencySamples();
            lat += osLatencyHost + (sum + padFor (sum)) / osFactor;
        }
        p.latency = std::min (lat, maxLatency);
    }

    int padFor (int sumOS) const noexcept { return (osFactor - sumOS % osFactor) % osFactor; }
    // fin d'un passage suréchantillonné commencé en j : il s'arrête avant un module de l'hôte, et aux points d'AUTO GAIN
    static int segmentEnd (const Plan& p, int j) noexcept
    {
        int e = j;
        while (e < p.count && p.steps[(size_t) e].over && (e == j || (e != p.refPoint && e != p.agPoint))) ++e;
        return e;
    }

    void applyPlan (const modules::Chain& c) noexcept
    {
        Plan next;
        build (c, next);
        for (int j = 0; j < next.count; ++j)
        {
            bool was = false;
            for (int q = 0; q < plan.count; ++q) was = was || plan.steps[(size_t) q].m == next.steps[(size_t) j].m;
            if (! was) next.steps[(size_t) j].m->arrive();
        }
        plan = next;
        planVariable = false;
        for (int j = 0; j < plan.count; ++j) planVariable = planVariable || plan.steps[(size_t) j].m->variableLatency;
        for (auto& d : dryDelay) d.setLength (plan.latency);
        int seg = 0;
        for (int j = 0; j < plan.count;)
        {
            if (! plan.steps[(size_t) j].over) { ++j; continue; }
            int sum = 0;
            const int end = segmentEnd (plan, j);
            for (; j < end; ++j) sum += plan.steps[(size_t) j].m->getLatencySamples();
            if (seg < kMaxSegments) { os[(size_t) seg]->reset(); for (auto& d : segPad[seg]) d.setLength (osPad + padFor (sum)); }
            ++seg;
        }
        latency.store (plan.latency, std::memory_order_relaxed);
    }

    void updateParams (bool snap) noexcept
    {
        for (int j = 0; j < plan.count; ++j) plan.steps[(size_t) j].m->update (snap);
        const float gi = dbToGain (juce::jlimit (-24.0f, 24.0f, pInput->load()));
        const float go = dbToGain (juce::jlimit (-24.0f, 24.0f, pOutput->load()));
        const float wm = pBypass->load() > 0.5f ? 0.0f : 1.0f;
        if (snap) { inGain.setCurrentAndTargetValue (gi); outGain.setCurrentAndTargetValue (go); wetMix.setCurrentAndTargetValue (wm); }
        else      { inGain.setTargetValue (gi); outGain.setTargetValue (go); wetMix.setTargetValue (wm); }
        autoOn = pAutoGain->load() > 0.5f;
    }

    void processChunk (float* const* ch, int numCh, int n) noexcept
    {
        for (int c = 0; c < numCh; ++c)
            for (int s = 0; s < n; ++s)
            {
                const float x = sanitize (ch[c][s]);
                ch[c][s] = x;
                dry[c][s] = dryDelay[c].process (x);
            }
        for (int s = 0; s < n; ++s) { const float g = inGain.getNextValue(); for (int c = 0; c < numCh; ++c) ch[c][s] *= g; }
        storePeaks (inLevel, ch, numCh, n);

        int seg = 0;
        for (int j = 0; j <= plan.count;)
        {
            if (j == plan.refPoint) autoGain.analyseReference (ch, numCh, n);
            if (j == plan.agPoint)
            {
                autoGain.analyseProcessed (ch, numCh, n);
                autoGain.update (autoOn, n);
                for (int s = 0; s < n; ++s) { const float ag = autoGain.nextGain(); for (int c = 0; c < numCh; ++c) ch[c][s] *= ag; }
            }
            if (j == plan.count) break;

            const auto& st = plan.steps[(size_t) j];
            if (! st.over)
            {
                // DELAY suivi d'une REVERB : deux retours nourris par la même voix
                if (st.m->isSend())
                {
                    int e = j;
                    while (e < plan.count && plan.steps[(size_t) e].m->isSend() && e - j < 2
                           && (e == j || (plan.steps[(size_t) (e - 1)].m->type == modules::delay && plan.steps[(size_t) e].m->type == modules::reverb))) ++e;
                    float* wet[2] { wetBuf[0].data(), wetBuf[1].data() };
                    std::fill (wet[0], wet[0] + n, 0.0f); std::fill (wet[1], wet[1] + n, 0.0f);
                    const float* in[2] { ch[0], numCh > 1 ? ch[1] : ch[0] };
                    for (int q = j; q < e; ++q) plan.steps[(size_t) q].m->processSendInto (in, wet, numCh, n);
                    for (int q = j; q < e; ++q) plan.steps[(size_t) q].m->recordSend (in, wet, numCh, n);
                    for (int c = 0; c < numCh; ++c) for (int s = 0; s < n; ++s) ch[c][s] += wet[c][s];
                    j = e;
                    continue;
                }
                st.m->process (ch, numCh, n);
                ++j;
                continue;
            }

            // passage suréchantillonné
            const int end = segmentEnd (plan, j);
            if (seg >= kMaxSegments) { j = end; continue; }
            auto& o = *os[(size_t) seg];
            juce::dsp::AudioBlock<float> block (ch, (size_t) numCh, (size_t) n);
            auto up = o.processSamplesUp (block);
            const int nOS = n * osFactor;
            float* x[2] { up.getChannelPointer (0), numCh > 1 ? up.getChannelPointer (1) : nullptr };
            for (; j < end; ++j) plan.steps[(size_t) j].m->process (x, numCh, nOS);
            for (int c = 0; c < numCh; ++c) for (int s = 0; s < nOS; ++s) x[c][s] = segPad[seg][c].process (x[c][s]);
            o.processSamplesDown (block);
            ++seg;
        }

        macroStage (ch, numCh, n);

        // COMPARER : la prise brute, remontée au volume du mix. Les deux volumes sont suivis en continu (énergie moyennée sur
        // ~2 s, seulement quand la voix est là) ; le gain d'égalisation glisse d'un bloc à l'autre
        const float matchTarget = eDry > 1.0e-12f ? juce::jlimit (0.1f, 10.0f, std::sqrt (eWet / eDry)) : 1.0f;
        const float matchFrom = matchGain, matchStep = (matchTarget - matchGain) * 0.15f / (float) std::max (1, n);
        cmpMix.setTargetValue (compare.load (std::memory_order_relaxed) ? 1.0f : 0.0f);

        // OUTPUT + BYPASS (signal sec aligné) ; chaîne modifiée : on descend, on reconstruit, on remonte (10 ms)
        for (int s = 0; s < n; ++s)
        {
            if (pending != plan.codes || relatch)
            {
                planGain -= planStep;
                if (planGain <= 0.0f) { planGain = 0.0f; applyPlan (pending); relatch = false; }
            }
            else if (planGain < 1.0f) planGain = std::min (1.0f, planGain + planStep);

            const float g = outGain.getNextValue(), m = sCurve (wetMix.getNextValue()), pg = sCurve (planGain);
            const float cm = sCurve (cmpMix.getNextValue()), mg = matchFrom + matchStep * (float) s;
            float ed = 0.0f, ew = 0.0f;
            for (int c = 0; c < numCh; ++c)
            {
                const float d = dry[c][s], w = ch[c][s] * g;
                ed += d * d; ew += w * w;
                const float normal = (d * (1.0f - m) + w * m) * pg;
                ch[c][s] = sanitize (normal + cm * (d * mg * pg - normal));
            }
            if (ed > 1.0e-9f * (float) numCh) { eDry = eCoef * eDry + (1.0f - eCoef) * ed; eWet = eCoef * eWet + (1.0f - eCoef) * ew; }
        }
        matchGain = matchFrom + matchStep * (float) n;
        storePeaks (outLevel, ch, numCh, n);
    }

    /*  PRÉSENCE et CHALEUR (les macros), puis le panoramique (rôles BACKS, ADLIBS) : une courbe douce en fin de chaîne.
        PRÉSENCE : cloche large à 3,8 kHz (±3,5 dB) et plateau d'air à 11 kHz (±2,5 dB). CHALEUR : plateau à 200 Hz (±3 dB) et
        un léger creux de dureté à 2,8 kHz. Rien n'est calculé tant que les deux sont à zéro. */
    void macroStage (float* const* ch, int numCh, int n) noexcept
    {
        const float tp = juce::jlimit (-1.0f, 1.0f, pPres->load (std::memory_order_relaxed) * 0.01f);
        const float tw = juce::jlimit (-1.0f, 1.0f, pWarm->load (std::memory_order_relaxed) * 0.01f);
        const float pres = std::abs (tp - mPres) < 0.002f ? tp : mPres + 0.35f * (tp - mPres);
        const float warm = std::abs (tw - mWarm) < 0.002f ? tw : mWarm + 0.35f * (tw - mWarm);
        const bool eqOn = std::abs (pres) > 0.001f || std::abs (warm) > 0.001f;
        if (eqOn && (! eqWasOn || pres != mPres || warm != mWarm))
        {
            if (! eqWasOn) for (auto& b : mEq) b.reset();
            mEq[0].setCoeffs (BiquadCoeffs::lowShelf (fs, 200.0, 0.7, 3.0 * warm));
            mEq[1].setCoeffs (BiquadCoeffs::peaking (fs, 2800.0, 1.1, -1.2 * std::max (0.0f, warm)));
            mEq[2].setCoeffs (BiquadCoeffs::peaking (fs, 3800.0, 0.8, 3.5 * pres));
            mEq[3].setCoeffs (BiquadCoeffs::highShelf (fs, std::min (11000.0, 0.4 * fs), 0.7, 2.5 * pres));
        }
        mPres = pres; mWarm = warm; eqWasOn = eqOn;
        if (eqOn)
            for (int c = 0; c < numCh; ++c)
                for (int s = 0; s < n; ++s)
                {
                    float x = ch[c][s];
                    for (auto& b : mEq) x = b.process (x, c);
                    ch[c][s] = x;
                }

        const float tpan = juce::jlimit (-1.0f, 1.0f, pPan->load (std::memory_order_relaxed) * 0.01f);
        if (numCh < 2 || (std::abs (tpan) < 0.001f && std::abs (pan) < 0.001f)) { pan = numCh < 2 ? 0.0f : tpan; return; }
        const float step = (tpan - pan) / (float) n;
        for (int s = 0; s < n; ++s)
        {
            pan += step;
            const float a = (pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
            ch[0][s] *= std::cos (a) * juce::MathConstants<float>::sqrt2;
            ch[1][s] *= std::sin (a) * juce::MathConstants<float>::sqrt2;
        }
        pan = tpan;
    }

    static void storePeaks (std::atomic<float> (&dest)[2], float* const* ch, int numCh, int n) noexcept
    {
        for (int c = 0; c < 2; ++c)
        {
            const float* s = ch[std::min (c, numCh - 1)];
            float pk = 0.0f;
            for (int q = 0; q < n; ++q) pk = std::max (pk, std::abs (s[q]));
            if (pk > dest[c].load (std::memory_order_relaxed)) dest[c].store (pk, std::memory_order_relaxed);
        }
    }

    juce::AudioProcessorValueTreeState& apvts;
    std::array<std::unique_ptr<ChainModule>, kModules> mods;
    std::atomic<float>* pSlot[kSlots] {};
    std::atomic<float> *pInput = nullptr, *pOutput = nullptr, *pAutoGain = nullptr, *pBypass = nullptr;

    double fs = 48000.0;
    int osOrder = 1, osFactor = 2, osPad = 0, osLatencyHost = 0, maxLatency = 1;
    std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, kMaxSegments> os;
    DelayLine segPad[kMaxSegments][2];
    Plan plan;
    bool planVariable = false, relatch = false;
    std::atomic<float> *pPres = nullptr, *pWarm = nullptr, *pPan = nullptr;
    Biquad mEq[4];
    float mPres = 0.0f, mWarm = 0.0f, pan = 0.0f;
    bool eqWasOn = false;
    modules::Chain pending {};
    float planGain = 1.0f, planStep = 0.001f;
    std::atomic<int> latency { 0 };
    AutoGain autoGain;
    bool autoOn = false;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> inGain, outGain, wetMix, cmpMix;
    float eDry = 0.0f, eWet = 0.0f, eCoef = 0.9999f, matchGain = 1.0f;   // COMPARER : volumes suivis et gain d'égalisation
    DelayLine dryDelay[2];
    float dry[2][kChunk] {};
    std::vector<float> wetBuf[2];
    std::atomic<float> inLevel[2] {}, outLevel[2] {};
};
} // namespace onirique
