#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "../Modules.h"
#include "VocalChain.h"   // ModuleSwitch, et tous les moteurs des modules
#include "SpectrumTap.h"

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
        if (type == modules::gate || type == modules::noise || type == modules::plosive || type == modules::deess)
        { tapIn = std::make_unique<SpectrumTap>(); tapOut = std::make_unique<SpectrumTap>(); }
    }
    virtual ~ChainModule() = default;

    // rate : la cadence à laquelle il tourne (celle de l'hôte, ou suréchantillonnée) ; factor : le suréchantillonnage
    void prepare (double rate, int factor, int maxBlock)
    {
        fs = rate;
        prepareEngine (rate, factor, maxBlock);
        sw.prepare (rate, getLatencySamples(), maxBlock);
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
    // les effets qui gardent leur queue quand on les coupe (reverb, delay...) gèrent eux-mêmes leur interrupteur
    virtual bool selfSwitch() const noexcept { return false; }
    // retour d'effet (DELAY, REVERB) : ajoute son retour dans wet à partir de la voix (in) au lieu de traiter en place
    virtual bool isSend() const noexcept { return false; }
    virtual void processSendInto (const float* const*, float* const*, int, int) noexcept {}
    virtual float getReductionDb() const noexcept { return 0.0f; }   // pour les mètres de l'interface

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
    Plosive (juce::AudioProcessorValueTreeState& s, int k) : ChainModule (s, modules::plosive, k) { amt = raw (ids::plosive); }
    int getLatencySamples() const noexcept override { return engine.getLatencySamples(); }
    float getReductionDb() const noexcept override { return engine.getReductionDb(); }

private:
    void prepareEngine (double rate, int factor, int) override { engine.prepare (rate, false, factor); }
    void updateEngine (bool, bool) noexcept override { engine.set (true, f (amt) * 0.01f); }
    void processEngine (float* const* ch, int numCh, int n) noexcept override { engine.process (ch, numCh, n); }
    void resetEngine() noexcept override { engine.reset(); }
    mini::DePlosive engine;
    std::atomic<float>* amt = nullptr;
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
// l'instrumentale reçue sur l'entrée SIDECHAIN (mono, à la cadence de l'hôte), pour le morceau de bloc en cours
struct KeyBus { const float* data = nullptr; int n = 0; };

class Peq final : public ChainModule
{
public:
    Peq (juce::AudioProcessorValueTreeState& s, int k, std::atomic<bool>& hold, const KeyBus& kb)
        : ChainModule (s, modules::peq, k), listenHold (hold), keyBus (kb)
    {
        for (int bb = 0; bb < 8; ++bb)
        {
            const char* id[8] { ids::peqOn[bb], ids::peqType[bb], ids::peqFreq[bb], ids::peqGain[bb], ids::peqQ[bb], ids::peqSlope[bb], ids::peqDyn[bb], ids::peqThr[bb] };
            for (int j = 0; j < 8; ++j) p[bb][j] = raw (id[j]);
            pKey[bb] = raw (ids::peqKey[bb]);
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
            x.slope = i (p[bb][5]); x.dyn = b (p[bb][6]); x.thrDb = f (p[bb][7]); x.key = b (pKey[bb]);
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
        engine.process (ch, numCh, n, keyBus.data, keyBus.n);
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
    const KeyBus& keyBus;
    std::atomic<float>* pKey[8] {};
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
    void updateEngine (bool, bool snap) noexcept override { engine.setParams (f (in), f (out), f (atk), f (rel), i (ratio), snap); }
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
    void updateEngine (bool, bool snap) noexcept override { engine.setParams (f (gain), f (peak), b (limit), snap); }
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
        engine.setAmount (f (amount) * 0.01f, snap);
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
        engine.set (on, sec, f (fbk) * 0.01f, f (mix) * 0.01f, b (pp), f (lc), f (hc), f (duck) * 0.01f);
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
        q.modRate = f (p[5]); q.modDepth = f (p[6]) * 0.01f; q.mix = f (p[7]) * 0.01f; q.lowCut = f (p[8]); q.highCut = f (p[9]);
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
            put (modules::peq, new Peq (s, k, peqHold, keyBus));
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
            static_cast<CreativeFx*> (module (modules::creative, k))->setParts (module (modules::doubler, k), module (modules::chorus, k),
                                                                              module (modules::flanger, k), module (modules::phone, k));
        }
        for (int j = 0; j < kSlots; ++j) pSlot[j] = s.getRawParameterValue (ids::slot[j]);
        pInput = s.getRawParameterValue (ids::input);
        pOutput = s.getRawParameterValue (ids::output);
        pAutoGain = s.getRawParameterValue (ids::autoGain);
        pBypass = s.getRawParameterValue (ids::bypass);
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

    /** L'instrumentale de l'entrée SIDECHAIN pour le prochain process() (mono, n échantillons), ou nullptr. */
    void setKey (const float* key) noexcept { scKey = key; }

    void process (float* const* ch, int numCh, int n) noexcept
    {
        modules::Chain now;
        readSlots (now);
        if (now != pending) pending = now;
        updateParams (false);
        for (int pos = 0; pos < n; pos += kChunk)
        {
            const int m = std::min (kChunk, n - pos);
            float* c[2] { ch[0] + pos, numCh > 1 ? ch[1] + pos : nullptr };
            keyBus.data = scKey != nullptr ? scKey + pos : nullptr;
            keyBus.n = m;
            processChunk (c, numCh, m);
        }
    }

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

        // OUTPUT + BYPASS (signal sec aligné) ; chaîne modifiée : on descend, on reconstruit, on remonte (10 ms)
        for (int s = 0; s < n; ++s)
        {
            if (pending != plan.codes)
            {
                planGain -= planStep;
                if (planGain <= 0.0f) { planGain = 0.0f; applyPlan (pending); }
            }
            else if (planGain < 1.0f) planGain = std::min (1.0f, planGain + planStep);

            const float g = outGain.getNextValue(), m = sCurve (wetMix.getNextValue()), pg = sCurve (planGain);
            for (int c = 0; c < numCh; ++c)
                ch[c][s] = sanitize ((dry[c][s] * (1.0f - m) + ch[c][s] * g * m) * pg);
        }
        storePeaks (outLevel, ch, numCh, n);
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
    chainmods::KeyBus keyBus;            // l'instrumentale du morceau de bloc en cours, lue par les EQ (bandes « SC »)
    const float* scKey = nullptr;        // l'instrumentale du bloc entier (process)
    std::array<std::unique_ptr<ChainModule>, kModules> mods;
    std::atomic<float>* pSlot[kSlots] {};
    std::atomic<float> *pInput = nullptr, *pOutput = nullptr, *pAutoGain = nullptr, *pBypass = nullptr;

    double fs = 48000.0;
    int osOrder = 1, osFactor = 2, osPad = 0, osLatencyHost = 0, maxLatency = 1;
    std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, kMaxSegments> os;
    DelayLine segPad[kMaxSegments][2];
    Plan plan;
    modules::Chain pending {};
    float planGain = 1.0f, planStep = 0.001f;
    std::atomic<int> latency { 0 };
    AutoGain autoGain;
    bool autoOn = false;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> inGain, outGain, wetMix;
    DelayLine dryDelay[2];
    float dry[2][kChunk] {};
    std::vector<float> wetBuf[2];
    std::atomic<float> inLevel[2] {}, outLevel[2] {};
};
} // namespace onirique
