#pragma once
#include <juce_dsp/juce_dsp.h>
#include "n73/OutputStage.h"   // n73 : AmpStage, Transformer, OutputStage (copiés de ONIRIQUE N73)

namespace onirique
{
/*  PREAMP : l'étage préampli du N73 seul, sans son EQ, rendu plus lisse.

    Le chemin du N73 : transformateur d'entrée (micro 10468 en 1:2, ou ligne), ampli BA283 NV, étage de sortie classe A et
    son transformateur, sous la même convention de niveau (0 dBFS = +18 dBu). Plus lisse que le N73 : moins de troisième
    harmonique et moins d'accentuation des aigus dans la distorsion de l'ampli, et un passe-bas doux à 18 kHz en sortie
    (le grain de console, sans la dureté du haut).

    GAIN : le gain du préampli en dB (-10 .. +50 ; vers 0 en LINE, +20 à +40 en MIC). Le gain nominal est rendu
    en sortie : le bouton pousse les transformateurs et les étages (le grain), pas le volume. OUTPUT ensuite.
    Tourne au-dessus de 80 kHz (le modèle du N73 en a besoin) : suréchantillonné ×2 en interne si la chaîne est plus lente. */
class PreampN73
{
public:
    void prepare (double rate, int maxBlock)
    {
        factor = rate < 80000.0 ? 2 : 1;
        if (factor > 1)
        {
            os = std::make_unique<juce::dsp::Oversampling<float>> (2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
            os->initProcessing ((size_t) std::max (1, maxBlock));
        }
        else os.reset();
        work = rate * factor;
        for (auto& c : ch) c.prepare (work);
        configured = false;
        comp.reset (work, 0.03); outS.reset (rate, 0.03); fade.reset (rate, 0.012);
        fade.setCurrentAndTargetValue (1.0f);
    }

    int getLatencySamples() const noexcept { return os != nullptr ? (int) std::lround (os->getLatencyInSamples()) : 0; }

    void reset() noexcept
    {
        if (os != nullptr) os->reset();
        for (auto& c : ch) c.reset();
    }

    void set (bool mic, float gainDbIn, float outDb, bool snap) noexcept
    {
        const double gainDb = gainDbIn;
        if (! configured || snap) { applyMode (mic); configured = true; }
        else if (mic != isMic) pendingMic = mic, switching = true;   // l'entrée change : fondu à zéro, bascule, fondu retour
        for (auto& c : ch) c.setGainDb (gainDb, snap);
        const float og = juce::Decibels::decibelsToGain (outDb);
        if (snap) outS.setCurrentAndTargetValue (og); else outS.setTargetValue (og);
    }

    void process (float* const* data, int numCh, int n) noexcept
    {
        numCh = std::min (numCh, 2);
        if (switching && ! fade.isSmoothing() && fade.getTargetValue() == 1.0f) fade.setTargetValue (0.0f);
        if (switching && ! fade.isSmoothing() && fade.getCurrentValue() == 0.0f)
        {
            applyMode (pendingMic);
            for (auto& c : ch) c.reset();
            switching = false;
            fade.setTargetValue (1.0f);
        }
        juce::dsp::AudioBlock<float> block (data, (size_t) numCh, (size_t) n);
        auto up = os != nullptr ? os->processSamplesUp (block) : block;
        const int un = (int) up.getNumSamples();
        float pk = 0.0f;
        // une voix mono sur une piste stéréo (gauche = droite) : un seul canal calculé, recopié (moitié moins de calcul). Quand
        // la stéréo revient, le canal droit repart de l'état du gauche (le même, puisqu'ils recevaient la même voix)
        bool same = numCh == 2;
        if (same)
        {
            const float* a = up.getChannelPointer (0); const float* b = up.getChannelPointer (1);
            for (int i = 0; i < un && same; ++i) same = a[i] == b[i];
        }
        if (! same && monoRun) ch[1] = ch[0];
        monoRun = same;
        const int work = same ? 1 : numCh;
        for (int c = 0; c < work; ++c)
        {
            auto* s = up.getChannelPointer ((size_t) c);
            for (int i = 0; i < un; ++i)
            {
                double y = ch[c].process ((double) s[i]);
                if (! std::isfinite (y)) { y = 0.0; ch[c].reset(); }
                s[i] = (float) y;
            }
            pk = std::max (pk, (float) ch[c].drive());
        }
        if (same) std::copy (up.getChannelPointer (0), up.getChannelPointer (0) + un, up.getChannelPointer (1));
        if (os != nullptr) os->processSamplesDown (block);
        for (int i = 0; i < n; ++i)
        {
            const float g = outS.getNextValue() * fade.getNextValue();
            for (int c = 0; c < numCh; ++c) data[c][i] *= g;
        }
        lastDrive.store (pk);
    }

    float drive() const noexcept { return lastDrive.load(); }   // 0 = repos, 1 = butée de l'étage de sortie

private:
    void applyMode (bool mic) noexcept { isMic = mic; for (auto& c : ch) c.configure (mic); }

    // un canal, à la cadence de travail
    struct Channel
    {
        void prepare (double fs)
        {
            rate = fs;
            t1.prepare (fs); t2.prepare (fs); nv.prepare (fs); out.prepare (fs);
            dc.setHighPass (1.0, fs);
            g.reset (fs, 0.03);
            configure (mic);
        }
        void reset() { t1.reset(); t2.reset(); nv.reset(); out.reset(); dc.reset(); t1Hf.reset(); t2Hf.reset(); outHf.reset(); bump.reset(); smooth.reset(); }

        void configure (bool isMic)
        {
            mic = isMic;
            const double fs = rate;
            if (mic)
            {
                n73::Transformer::Params tp;                       // 10468 en 1:2, impédance HI (1k2), micro de 150 Ω
                tp.Rs = 150.0; tp.RL = 1200.0; tp.Lm = 10.0; tp.lambdaS = 0.06; tp.lambdaDC = 0.0; tp.ratio = 2.0;
                t1.setParams (tp);
                frontGain = tp.ratio * tp.RL / (tp.RL + tp.Rs);
                t1Hf.setLowPass (60000.0, 1.08, fs, 20000.0);
            }
            else
            {
                n73::Transformer::Params tp;                       // transformateur ligne
                tp.Rs = 100.0; tp.RL = 10000.0; tp.Lm = 20.0; tp.lambdaS = 0.12; tp.lambdaDC = 0.0; tp.ratio = 1.0;
                t2.setParams (tp);
                frontGain = tp.ratio * tp.RL / (tp.RL + tp.Rs);
                t2Hf.setLowPass (100000.0, 0.75, fs, 20000.0);
            }
            // BA283 NV, plus lisse que dans le N73 : troisième harmonique ×0,6, accentuation des aigus ×1,8 au lieu de ×3
            n73::AmpStage::Params a;
            a.gain = nvGain; a.hd2 = 1.0e-4; a.hd3 = 1.2e-4; a.vRef = 10.0; a.vPos = 10.5; a.vNeg = 7.5;
            a.fEmph = 7000.0; a.emph = 1.8; a.fCouple = 1.0;
            nv.setParams (a);
            n73::OutputStage::Params op;
            op.gain = kOutGain; op.load = kLoad;
            op.xf.Rs = kOutZ; op.xf.RL = kLoad; op.xf.Lm = 15.0; op.xf.lambdaS = 0.2; op.xf.lambdaDC = 0.07; op.xf.ratio = 1.0;
            out.setParams (op);
            bump.setHighPass (7.2, 0.9, fs, 15.0);
            outHf.setLowPass (120000.0, 0.7, fs, 20000.0);
            smooth.setLowPass (18000.0, 0.6, fs, 18000.0);   // le haut adouci
            setGainDb (gainDb, true);
        }

        void setGainDb (double db, bool snap)
        {
            gainDb = db;
            const double rest = nvGain * kHpfFlat * kOutGain * kLoad / (kLoad + kOutZ);
            const double wanted = std::pow (10.0, db / 20.0);
            pre = wanted / (frontGain * rest);   // l'atténuateur (ou le gain) avant la BA283, comme le commutateur du N73
            if (snap) g.setCurrentAndTargetValue (pre); else g.setTargetValue (pre);
            makeup = 1.0 / wanted;               // le gain nominal rendu : le bouton pousse le grain, pas le volume
        }

        inline double process (double x) noexcept
        {
            const double p = g.getNextValue();
            double v = dc.process (x * kVfs);
            v = mic ? t1Hf.process (t1.process (v)) : t2Hf.process (t2.process (v));
            v = nv.process (v * p) * kHpfFlat;
            v = out.process (v);
            v = smooth.process (outHf.process (bump.process (v)));
            return v / kVfs * (pre / p) * makeup;   // pendant un glissement du gain : niveau tenu
        }
        double drive() const noexcept { return out.lastDrive(); }

        static constexpr double kVfs = 8.7180;     // 0 dBFS = +18 dBu crête
        static constexpr double kOutGain = 9.87, kOutZ = 75.0, kLoad = 600.0, kHpfFlat = 5100.0 / (1200.0 + 5100.0);
        static constexpr double nvGain = 1.0 + 2200.0 / (390.0 * 1800.0 / (390.0 + 1800.0));
        double rate = 96000.0, frontGain = 1.0, pre = 1.0, makeup = 1.0, gainDb = 0.0;
        bool mic = false;
        n73::Transformer t1, t2;
        n73::AmpStage nv;
        n73::OutputStage out;
        n73::OnePole dc;
        n73::Biquad t1Hf, t2Hf, outHf, bump, smooth;
        juce::SmoothedValue<double> g { 1.0 };
    };

    std::array<Channel, 2> ch;
    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    double work = 96000.0;
    int factor = 1;
    bool configured = false, isMic = false, pendingMic = false, switching = false, monoRun = false;
    juce::SmoothedValue<double> comp;
    juce::SmoothedValue<float> outS { 1.0f }, fade { 1.0f };
    std::atomic<float> lastDrive { 0.0f };
};
} // namespace onirique
