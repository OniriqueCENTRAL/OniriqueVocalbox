#pragma once

#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <functional>
#include <map>
#include "../Parameters.h"
#include "../DSP/VocalChain.h"

namespace onirique::assist
{
/*  ASSISTANT DE MIXAGE : écoute la voix (le signal d'entrée du plugin pendant que le morceau joue), l'analyse,
    puis règle tous les modules du VocalBox. Tout est calculé en local, sans réseau.

    1) ANALYSE de la voix captée (uniquement les passages où la voix est présente)
       - niveaux : RMS, crêtes, plage dynamique (P95 - P10 des blocs de 20 ms), bruit de fond ;
       - tonalité : spectre moyen par 1/6 d'octave comparé à une courbe cible de voix mixée professionnelle
         (adaptée au style), écarts par zone (graves, corps, boue, son "carton", présence, dureté, air) ;
       - sifflantes : trames riches en aigus -> fréquence du "S" (et d'un second S éventuel), sévérité par rapport
         au corps de la voix ; plosives (bouffées de graves) ; résonances étroites ;
       - hauteur (YIN) : registre, part de voix chantée, longueur des notes, détection de l'auto-tune
         (hauteurs collées à la grille des demi-tons et quasi immobiles) -> style deviné (rap, chant, auto-tune).
    2) DÉCISIONS
       - EQ paramétrique (nettoyage) : coupe-bas sous la voix, shelf contre le "boom", cloches de correction
         calculées sur l'écart restant, coupes DYNAMIQUES pour la dureté et les résonances (seuil mesuré) ;
       - EQ ANALOG (couleur) : LOW / MID / HIGH ajustés par moindres carrés sur la correction voulue, avec les
         courbes exactes du module ;
       - garde-fous : jamais de voix trop fine (le corps 140-280 Hz est vérifié après correction), jamais trop
         "boomy" (graves mesurés après correction).
    3) OPTIMISATION par simulation : la chaîne complète tourne hors temps réel sur la voix captée et les
       réglages sont cherchés par dichotomie pour atteindre des objectifs mesurés (réduction moyenne du 1176 et du
       LA-2A, réduction des sifflantes, niveau de sortie identique au niveau d'entrée).
*/

enum Style { styleAuto = 0, styleRap, styleSinging, styleAutotune, stylePop, styleRnb, numStyles };
enum Intensity { subtle = 0, balanced, bold };

inline const char* styleName (int s)
{
    static const char* n[numStyles] { "AUTO", "RAP", "SINGING", "AUTOTUNE", "POP", "R&B" };
    return n[juce::jlimit (0, numStyles - 1, s)];
}

constexpr int kZones = 7;
inline const char* zoneName (int z)
{
    static const char* n[kZones] { "LOW END", "BODY", "MUD", "BOX", "PRESENCE", "HARSH", "AIR" };
    return n[juce::jlimit (0, kZones - 1, z)];
}

struct Analysis
{
    bool valid = false;
    juce::String error;
    double fs = 48000.0;
    float totalSec = 0.0f, activeSec = 0.0f;
    float rmsDb = -100.0f, peakDb = -100.0f, crestDb = 0.0f, dynRangeDb = 0.0f, noiseDb = -100.0f;
    float f0Median = 0.0f, f0Low = 0.0f, f0High = 0.0f, voicedRatio = 0.0f, avgRunSec = 0.0f, rangeSt = 0.0f;
    float autotune = 0.0f;                    // 0..1
    int detected = styleRap;
    std::array<float, kZones> zoneDev {};     // dB, + = trop, - = manque (par rapport à la cible du style)
    float sibFreq = 0.0f, sibFreq2 = 0.0f, sibDb = -40.0f, sib2Db = -40.0f, sibShare = 0.0f;
    float plosivesPerMin = 0.0f;
    std::vector<std::pair<float, float>> resonances;   // fréquence, excès (dB)
    std::vector<float> spectrum;              // spectre moyen (1/6 d'octave, 50 Hz..16 kHz) pour l'affichage
    std::vector<float> target;                // cible du style sur la même grille (alignée)
};

struct Proposal
{
    std::map<juce::String, float> values;     // réglages (unités réelles)
    juce::StringArray lines;                  // résumé lisible, une ligne par module
    int style = styleRap;
};

class Engine
{
public:
    using Progress = std::function<void (float, const juce::String&)>;

    // mono : voix captée (entrée brute du plugin) ; defaults : valeur par défaut de chaque paramètre (unités réelles)
    Engine (std::vector<float> mono, double sampleRate, std::map<juce::String, float> defaultValues)
        : raw (std::move (mono)), fs (sampleRate), defaults (std::move (defaultValues)) {}

    Analysis analyse (const Progress& progress, const std::atomic<bool>& cancel)
    {
        Analysis a;
        a.fs = fs;
        a.totalSec = (float) ((double) raw.size() / fs);
        progress (0.02f, "Measuring levels");
        if (! measureLevels (a)) return a;
        if (cancel) return a;

        progress (0.10f, "Reading the tone");
        spectralAnalysis (a);
        if (cancel) return a;

        progress (0.22f, "Tracking the pitch");
        pitchAnalysis (a);
        if (cancel) return a;

        progress (0.30f, "Finding plosives");
        plosiveAnalysis (a);

        a.detected = a.autotune > 0.55f && a.voicedRatio > 0.3f ? styleAutotune
                   : (a.avgRunSec > 0.2f && a.voicedRatio > 0.45f && a.rangeSt > 5.0f) ? styleSinging : styleRap;
        a.valid = true;
        last = a;
        return a;
    }

    Proposal propose (Analysis& a, int styleChoice, int intensity, const Progress& progress, const std::atomic<bool>& cancel)
    {
        Proposal pr;
        const int style = styleChoice == styleAuto ? a.detected : styleChoice;
        pr.style = style;
        const auto spec = specFor (style);
        const float iMul = intensity == subtle ? 0.7f : (intensity == bold ? 1.35f : 1.0f);
        auto& v = pr.values;

        // ---- gain d'entrée : la voix arrive à -20 dB RMS dans la chaîne (les compresseurs travaillent toujours pareil)
        const float inDb = juce::jlimit (-18.0f, 18.0f, std::round ((-20.0f - a.rmsDb) * 2.0f) * 0.5f);
        v[ids::input] = inDb;
        pr.lines.add ("INPUT  " + signedDb (inDb) + "  (voice brought to -20 dB RMS)");

        // ---- EQ : paramétrique (nettoyage) + EQ ANALOG (couleur)
        progress (0.36f, "Designing the parametric EQ");
        designEq (a, style, spec, intensity, inDb, pr);
        if (cancel) return pr;

        // ---- modules fixes selon le style
        v[ids::resoAmt] = juce::jlimit (12.0f, 45.0f, (spec.resoBase + 2.5f * (a.resonances.empty() ? 0.0f : a.resonances.front().second - 4.5f)) * std::sqrt (iMul));
        v[ids::resoOn] = 1.0f;
        v[ids::plosive] = a.plosivesPerMin > 1.5f ? juce::jlimit (20.0f, 70.0f, 20.0f + 4.0f * a.plosivesPerMin) : 15.0f;
        v[ids::compMaster] = 1.0f;
        v[ids::compOn] = 1.0f;
        v[ids::compMode] = 0.0f;                         // 1176 seul pour régler son entrée
        v[ids::compOrder] = (float) spec.compOrder;
        v[ids::fetRatio] = (float) spec.ratio;
        v[ids::fetAttack] = spec.attack;
        v[ids::fetRelease] = spec.release;
        v[ids::laLimit] = 0.0f;
        v[ids::satType] = (float) spec.satType;
        v[ids::drive] = juce::jlimit (0.0f, 6.0f, spec.drive * iMul * (a.zoneDev[5] > 2.0f && spec.satType == 0 ? 0.7f : 1.0f));
        v[ids::satOn] = 1.0f;
        v[ids::parOn] = spec.parallel > 0.0f ? 1.0f : 0.0f;
        v[ids::parAuto] = 1.0f;
        v[ids::parAmount] = juce::jlimit (0.0f, 60.0f, spec.parallel * iMul);
        v[ids::deessOn] = 1.0f;
        v[ids::chainOrder] = 0.0f;
        v[ids::autoGain] = 0.0f;
        v[ids::mbOn] = 0.0f;
        v[ids::deessAmt] = 0.0f;
        v[ids::deess2On] = 0.0f;

        // ---- compression : objectifs mesurés, adaptés à la plage dynamique de la voix
        const float dynMul = juce::jlimit (0.6f, 1.3f, a.dynRangeDb / 14.0f);
        const float grFet = juce::jlimit (1.5f, 10.0f, spec.gr1176 * iMul * dynMul);
        const float grOpto = juce::jlimit (1.0f, 7.0f, spec.grLA * iMul * dynMul);
        prepareMasks();

        progress (0.45f, "Optimising the 1176");
        float fetIn = 0.0f;
        {
            fetIn = solve (-12.0f, 36.0f, grFet, 9, cancel, [&] (float x)
            {
                v[ids::fetIn] = x;
                return mean (simulate (v, toComp).fet, activeMask);
            });
            v[ids::fetIn] = std::round (fetIn * 2.0f) * 0.5f;
            v[ids::fetOut] = std::round (grFet * 0.7f * 2.0f) * 0.5f;
        }
        if (cancel) return pr;

        progress (0.58f, "Optimising the LA-2A");
        v[ids::compMode] = 2.0f;
        {
            const float peak = solve (0.0f, 100.0f, grOpto, 8, cancel, [&] (float x)
            {
                v[ids::laPeak] = x;
                return mean (simulate (v, toComp).la, activeMask);
            });
            v[ids::laPeak] = (float) juce::roundToInt (peak);
            v[ids::laGain] = std::round (grOpto * 0.7f * 2.0f) * 0.5f;
        }
        pr.lines.add ("COMPRESSION  1176 IN " + signedDb (v[ids::fetIn]) + " (" + juce::String (grFet, 1) + " dB GR) > LA-2A PEAK "
                      + juce::String (juce::roundToInt (v[ids::laPeak])) + " (" + juce::String (grOpto, 1) + " dB GR)"
                      + (spec.compOrder == 1 ? "  [LA-2A first]" : ""));
        if (v[ids::parOn] > 0.5f) pr.lines.add ("PARALLEL  " + juce::String (juce::roundToInt (v[ids::parAmount])) + " % (AUTO)");
        if (cancel) return pr;

        // ---- multibande : seuils posés sur les niveaux mesurés de chaque bande
        if (spec.multiband)
        {
            progress (0.66f, "Measuring the multiband");
            v[ids::mbOn] = 1.0f;
            for (int b = 0; b < 4; ++b) v[ids::mbThr[b]] = 0.0f;
            const auto s = simulate (v, toMultiband);
            juce::StringArray what;
            const char* names[4] { "low", "lo-mid", "hi-mid", "high" };
            for (int b = 0; b < 4; ++b)
            {
                std::vector<float> lv;
                for (size_t k = 0; k < s.mb.size(); ++k) if (activeMask[k]) lv.push_back (s.mb[k][(size_t) b]);
                const float p50 = percentile (lv, 0.5f), p90 = percentile (lv, 0.9f), p97 = percentile (lv, 0.97f);
                // LOW : seulement si les graves bondissent (effet de proximité) ; HI-MID : dureté sur les notes fortes
                float thr = p97 + 1.0f;
                if (b == 0 && p90 - p50 > 7.0f) { thr = p90 - 2.0f; what.add (names[b]); }
                if (b == 2)                    { thr = p90 - 1.5f * iMul; what.add (names[b]); }
                if (b == 3 && a.zoneDev[6] > 1.0f) { thr = p90; what.add (names[b]); }
                v[ids::mbThr[b]] = juce::jlimit (-60.0f, 0.0f, std::round (thr * 2.0f) * 0.5f);
            }
            v[ids::mbAttack] = style == styleRap ? 10.0f : 15.0f;
            v[ids::mbRelease] = style == styleRap ? 150.0f : 200.0f;
            pr.lines.add ("MULTIBAND  " + what.joinIntoString (" + ") + " controlled on the loudest moments");
        }
        if (cancel) return pr;

        // ---- de-esser : intensité cherchée pour ramener les S au niveau visé
        progress (0.74f, "Taming the sibilance");
        juce::String deLine = "DE-ESSER  ";
        if (a.sibShare > 0.004f)
        {
            v[ids::deessFreq] = juce::jlimit (3000.0f, 10000.0f, a.sibFreq / 1.12f);
            const float boost = eqGainAt (v, a.sibFreq);
            const float grTarget = juce::jlimit (2.5f, 9.0f, (a.sibDb + boost + 9.0f + spec.deessExtra) * std::sqrt (iMul));
            const float amt = solve (0.0f, 100.0f, grTarget, 8, cancel, [&] (float x)
            {
                v[ids::deessAmt] = x;
                return mean (simulate (v, toDeEsser).de1, sibMask);
            });
            v[ids::deessAmt] = (float) juce::roundToInt (amt);
            deLine << juce::String (v[ids::deessFreq] / 1000.0f, 1) << " kHz " << juce::roundToInt (amt) << " %";

            if (a.sibFreq2 > 0.0f && a.sib2Db > a.sibDb - 5.0f)
            {
                v[ids::deess2On] = 1.0f;
                v[ids::deess2Freq] = juce::jlimit (3000.0f, 10000.0f, a.sibFreq2 / 1.12f);
                const float amt2 = solve (0.0f, 100.0f, 0.7f * grTarget, 7, cancel, [&] (float x)
                {
                    v[ids::deess2Amt] = x;
                    return mean (simulate (v, toDeEsser).de2, sibMask);
                });
                v[ids::deess2Amt] = (float) juce::roundToInt (amt2);
                deLine << "  +  " << juce::String (v[ids::deess2Freq] / 1000.0f, 1) << " kHz " << juce::roundToInt (amt2) << " %";
            }
        }
        else
        {
            v[ids::deessAmt] = 15.0f;
            deLine << "light safety (very few S detected)";
        }
        pr.lines.add (deLine);
        pr.lines.add ("PLOSIVES  " + juce::String (juce::roundToInt (v[ids::plosive])) + " %" + sep() + "RESONANCE "
                      + juce::String (juce::roundToInt (v[ids::resoAmt])) + " %");
        static const char* satNames[] { "TUBE", "ANALOG", "TAPE" };
        pr.lines.add ("SATURATION  " + juce::String (satNames[juce::jlimit (0, 2, juce::roundToInt (v[ids::satType]))]) + " "
                      + juce::String (v[ids::drive], 1));
        if (cancel) return pr;

        // ---- sortie : même niveau perçu qu'à l'entrée (on compare à l'oreille, pas au volume)
        progress (0.9f, "Matching the output level");
        {
            const auto s = simulate (v, full, true);
            const float outRms = rmsDbOf (s.out, activeMask);
            v[ids::output] = juce::jlimit (-24.0f, 24.0f, std::round ((a.rmsDb - outRms) * 2.0f) * 0.5f);
            pr.lines.add ("OUTPUT  " + signedDb (v[ids::output]) + "  (same loudness as the raw voice)");
        }
        progress (1.0f, "Done");
        return pr;
    }

private:
    // ---------------------------------------------------------------- styles
    struct StyleSpec
    {
        float gr1176, grLA; int ratio; float attack, release; int compOrder; float parallel;
        int satType; float drive; float airOff, presOff, bodyOff; bool multiband; float deessExtra; int highFreq; float resoBase;
    };

    static StyleSpec specFor (int style)
    {
        switch (style)
        {
            case styleAutotune: return { 6.0f, 3.0f, 0, 6.0f, 6.0f, 0, 25.0f, 1, 2.0f,  1.5f, 0.5f, 0.0f, true,  1.5f, 2, 25.0f };
            case stylePop:      return { 4.0f, 3.0f, 0, 3.0f, 5.0f, 0, 20.0f, 2, 1.5f,  1.0f, 0.5f, 0.0f, true,  0.5f, 2, 22.0f };
            case styleRnb:      return { 3.0f, 4.0f, 0, 3.0f, 4.0f, 1, 15.0f, 2, 2.5f,  0.5f, 0.0f, 1.0f, false, 0.0f, 2, 18.0f };
            case styleSinging:  return { 2.5f, 4.0f, 0, 3.0f, 4.0f, 1, 12.0f, 0, 1.5f,  0.5f, 0.0f, 0.5f, false, 0.0f, 2, 18.0f };
            default:            return { 5.5f, 2.5f, 1, 5.0f, 6.0f, 0, 30.0f, 0, 2.5f, -1.0f, 1.0f, 0.5f, true,  0.5f, 1, 22.0f };   // RAP / KICKAGE
        }
    }

    // ---------------------------------------------------------------- outils
    static juce::String signedDb (float db) { return (db > 0.0f ? "+" : "") + juce::String (db, 1) + " dB"; }
    static juce::String sep() { return juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  ")); }   // point médian entouré d'espaces

    static float percentile (std::vector<float> v, float p)
    {
        if (v.empty()) return -100.0f;
        const size_t k = (size_t) juce::jlimit (0, (int) v.size() - 1, (int) std::floor (p * (float) (v.size() - 1)));
        std::nth_element (v.begin(), v.begin() + (std::ptrdiff_t) k, v.end());
        return v[k];
    }

    static float mean (const std::vector<float>& x, const std::vector<bool>& mask)
    {
        double s = 0.0; int n = 0;
        for (size_t i = 0; i < x.size() && i < mask.size(); ++i) if (mask[i]) { s += x[i]; ++n; }
        return n > 0 ? (float) (s / n) : 0.0f;
    }

    // dichotomie sur une mesure croissante : renvoie x tel que measure(x) ~ target (une borne si la cible est hors d'atteinte)
    template <typename F>
    static float solve (float lo, float hi, float target, int iters, const std::atomic<bool>& cancel, F&& measure)
    {
        for (int i = 0; i < iters && ! cancel; ++i)
        {
            const float mid = 0.5f * (lo + hi);
            (measure (mid) < target ? lo : hi) = mid;
        }
        return 0.5f * (lo + hi);
    }

    float value (const std::map<juce::String, float>& v, const char* id) const
    {
        if (auto it = v.find (id); it != v.end()) return it->second;
        if (auto it = defaults.find (id); it != defaults.end()) return it->second;
        return 0.0f;
    }

    // réglages -> paramètres de la chaîne (même lecture que le processeur)
    VocalChain::Params paramsFrom (const std::map<juce::String, float>& v) const
    {
        auto g = [&] (const char* id) { return value (v, id); };
        auto b = [&] (const char* id) { return value (v, id) > 0.5f; };
        auto i = [&] (const char* id) { return juce::roundToInt (value (v, id)); };
        VocalChain::Params p;
        p.inputDb = g (ids::input); p.low = g (ids::low); p.mud = g (ids::mud); p.high = g (ids::high);
        p.deessFreq = g (ids::deessFreq); p.deessAmt = g (ids::deessAmt); p.drive = g (ids::drive); p.satType = i (ids::satType);
        static constexpr float lowFreqs[] { 80.0f, 100.0f, 125.0f }, highFreqs[] { 8000.0f, 10000.0f, 20000.0f };
        p.lowFreqHz = lowFreqs[juce::jlimit (0, 2, i (ids::lowFreq))];
        p.highFreqHz = highFreqs[juce::jlimit (0, 2, i (ids::highFreq))];
        p.smartS = b (ids::smartS); p.lowCut = b (ids::lowCut); p.mbOn = b (ids::mbOn); p.compMaster = b (ids::compMaster);
        p.parOn = b (ids::parOn); p.parAmount = g (ids::parAmount); p.parAuto = b (ids::parAuto); p.parThresh = g (ids::parThresh);
        p.parAttack = g (ids::parAttack); p.parRelease = g (ids::parRelease);
        p.deess2On = b (ids::deess2On); p.deess2Freq = g (ids::deess2Freq); p.deess2Amt = g (ids::deess2Amt);
        p.mb.x1 = g (ids::mbX[0]); p.mb.x2 = g (ids::mbX[1]); p.mb.x3 = g (ids::mbX[2]);
        for (int k = 0; k < 4; ++k) { p.mb.thr[(size_t) k] = g (ids::mbThr[k]); p.mb.gain[(size_t) k] = g (ids::mbGain[k]); }
        p.mb.attackMs = g (ids::mbAttack); p.mb.releaseMs = g (ids::mbRelease);
        for (int k = 0; k < ParametricEQ::kBands; ++k)
        {
            auto& bd = p.peq[(size_t) k];
            bd.on = b (ids::peqOn[k]); bd.type = i (ids::peqType[k]); bd.freq = g (ids::peqFreq[k]); bd.gainDb = g (ids::peqGain[k]);
            bd.q = g (ids::peqQ[k]); bd.slope = i (ids::peqSlope[k]); bd.dyn = b (ids::peqDyn[k]); bd.thrDb = g (ids::peqThr[k]);
        }
        p.peqOn = b (ids::peqPower);
        p.compOn = b (ids::compOn); p.compMode = i (ids::compMode); p.compOrder = i (ids::compOrder);
        p.fetIn = g (ids::fetIn); p.fetOut = g (ids::fetOut); p.fetAttack = g (ids::fetAttack); p.fetRelease = g (ids::fetRelease);
        p.fetRatio = i (ids::fetRatio); p.laGain = g (ids::laGain); p.laPeak = g (ids::laPeak); p.laLimit = b (ids::laLimit);
        p.resoAmt = g (ids::resoAmt); p.resoLow = g (ids::resoLow); p.resoMid = g (ids::resoMid); p.resoHigh = g (ids::resoHigh);
        const char* rf[3] { ids::resoLowFreq, ids::resoMidFreq, ids::resoHighFreq }, *rw[3] { ids::resoLowWidth, ids::resoMidWidth, ids::resoHighWidth };
        for (int k = 0; k < 3; ++k) { p.resoFreq[k] = g (rf[k]); p.resoWidth[k] = g (rw[k]); }
        p.eqOn = b (ids::eqOn); p.resoOn = b (ids::resoOn); p.deessOn = b (ids::deessOn); p.satOn = b (ids::satOn);
        p.outputDb = g (ids::output); p.autoGain = false; p.bypass = false;
        p.plosive = g (ids::plosive); p.resoListen = false; p.chainOrder = i (ids::chainOrder);
        return p;
    }

    // ---------------------------------------------------------------- 1) niveaux
    bool measureLevels (Analysis& a)
    {
        blockLen = std::max (64, (int) std::lround (0.02 * fs));
        const int nb = (int) (raw.size() / (size_t) blockLen);
        if (nb < 20) { a.error = "Not enough audio: play at least a few seconds of vocal."; return false; }
        std::vector<float> blockDb ((size_t) nb);
        float pk = 0.0f;
        for (int k = 0; k < nb; ++k)
        {
            double e = 0.0;
            for (int i = 0; i < blockLen; ++i)
            {
                const float x = raw[(size_t) (k * blockLen + i)];
                e += (double) x * x;
                pk = std::max (pk, std::abs (x));
            }
            blockDb[(size_t) k] = (float) (10.0 * std::log10 (e / blockLen + 1.0e-12));
        }
        const float loud = percentile (blockDb, 0.95f);
        gateDb = std::max (-62.0f, loud - 36.0f);
        std::vector<float> act;
        double pw = 0.0;
        for (float d : blockDb) if (d > gateDb) { act.push_back (d); pw += std::pow (10.0, d / 10.0); }
        a.activeSec = (float) (act.size() * (size_t) blockLen / fs);
        if (a.activeSec < 2.5f || loud < -70.0f)
        {
            a.error = "Not enough voice detected: press play in your DAW and let the vocal run.";
            return false;
        }
        a.rmsDb = (float) (10.0 * std::log10 (pw / (double) act.size()));
        a.peakDb = pk > 1.0e-6f ? 20.0f * std::log10 (pk) : -100.0f;
        a.crestDb = a.peakDb - a.rmsDb;
        a.dynRangeDb = percentile (act, 0.95f) - percentile (act, 0.10f);
        std::vector<float> quiet;
        for (float d : blockDb) if (d <= gateDb) quiet.push_back (d);
        a.noiseDb = quiet.size() > 10 ? percentile (quiet, 0.5f) : -100.0f;

        // voix condensée : les passages actifs (+ un bloc de marge), raccordés par de courts fondus ; 12 s max
        audio.clear();
        const size_t maxLen = (size_t) (12.0 * fs);
        const int fade = std::min (blockLen / 2, 96);
        int k = 0;
        while (k < nb && audio.size() < maxLen)
        {
            if (blockDb[(size_t) k] <= gateDb) { ++k; continue; }
            const int start = std::max (0, k - 1);
            int end = k;
            while (end < nb && (blockDb[(size_t) end] > gateDb || (end + 1 < nb && blockDb[(size_t) end + 1] > gateDb))) ++end;
            end = std::min (nb, end + 1);
            const size_t s0 = (size_t) (start * blockLen), s1 = (size_t) (end * blockLen);
            const size_t base = audio.size();
            audio.insert (audio.end(), raw.begin() + (std::ptrdiff_t) s0, raw.begin() + (std::ptrdiff_t) s1);
            const size_t len = s1 - s0;
            for (int i = 0; i < fade && (size_t) i < len / 2; ++i)
            {
                const float g = (float) i / (float) fade;
                audio[base + (size_t) i] *= g;
                audio[base + len - 1 - (size_t) i] *= g;
            }
            k = end + 1;
        }
        if (audio.size() > maxLen) audio.resize (maxLen);
        return true;
    }

    // ---------------------------------------------------------------- 2) spectre
    static constexpr int kGrid = 50;                     // 1/6 d'octave de 50 Hz à ~14 kHz (+ 1 point à 16 kHz)
    static float gridHz (int k) { return 50.0f * std::pow (2.0f, (float) k / 6.0f); }

    // cible (énergie par 1/6 d'octave, 0 dB à 1 kHz) d'une voix mixée : le spectre moyen de la voix humaine
    // (LTASS, Byrne et al. 1994) "poli" comme le fait un mixage pro : -2 dB de boue (250-500 Hz), +2 dB de présence
    // (2-5 kHz), +3 dB d'air (8-16 kHz), graves sous 120 Hz allégés ; sous la fondamentale la plus grave, tout s'efface
    static float targetDb (float hz, int style, const StyleSpec& sp, float f0Low)
    {
        static const float tf[] { 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1000, 1250, 1600, 2000,
                                  2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000 };
        static const float tv[] { -22, -17, -12, -3, 1.5f, 2.5f, 4.5f, 4.5f, 3.5f, 6, 6.2f, 5.5f, 2.5f, 0, -0.3f, -1, -3.5f,
                                  -4, -5, -6, -7, -7.3f, -7.5f, -7.5f, -9.5f, -12 };
        constexpr int n = (int) (sizeof (tf) / sizeof (tf[0]));
        float t = tv[n - 1];
        if (hz <= tf[0]) t = tv[0];
        else
            for (int i = 0; i < n - 1; ++i)
                if (hz <= tf[i + 1])
                {
                    const float w = std::log (hz / tf[i]) / std::log (tf[i + 1] / tf[i]);
                    t = tv[i] + w * (tv[i + 1] - tv[i]);
                    break;
                }
        if (hz >= 8000.0f)                  t += sp.airOff;
        else if (hz >= 6000.0f)             t += 0.5f * sp.airOff;
        if (hz >= 1500.0f && hz < 5000.0f)  t += sp.presOff;
        if (hz >= 120.0f && hz < 300.0f)    t += sp.bodyOff;
        if (f0Low > 0.0f && hz < 0.85f * f0Low) t -= 12.0f * std::log2 (0.85f * f0Low / hz);
        juce::ignoreUnused (style);
        return t;
    }

    void spectralAnalysis (Analysis& a)
    {
        fftOrder = fs > 60000.0 ? 13 : 12;
        const int N = 1 << fftOrder, hop = N / 2;
        juce::dsp::FFT fft (fftOrder);
        std::vector<float> win ((size_t) N), buf ((size_t) (2 * N));
        for (int i = 0; i < N; ++i) win[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / N));
        const int bins = N / 2 + 1;
        ltas.assign ((size_t) bins, 0.0);
        std::vector<double> ltasA ((size_t) bins, 0.0), ltasB ((size_t) bins, 0.0);   // première / seconde moitié de la prise
        std::vector<double> sib ((size_t) bins, 0.0), vow ((size_t) bins, 0.0);
        int nAll = 0, nSib = 0, nVow = 0;
        const double binHz = fs / N;
        auto binOf = [&] (double hz) { return juce::jlimit (1, bins - 1, (int) std::lround (hz / binHz)); };
        const int b80 = binOf (80), b4k5 = binOf (4500), b11k = binOf (std::min (11000.0, 0.45 * fs));

        for (size_t pos = 0; pos + (size_t) N <= audio.size(); pos += (size_t) hop)
        {
            double e = 0.0;
            for (int i = 0; i < N; ++i) { const float x = audio[pos + (size_t) i]; e += (double) x * x; buf[(size_t) i] = x * win[(size_t) i]; }
            if (10.0 * std::log10 (e / N + 1.0e-12) < gateDb) continue;
            std::fill (buf.begin() + N, buf.end(), 0.0f);
            fft.performFrequencyOnlyForwardTransform (buf.data(), true);
            double all = 0.0, hf = 0.0;
            for (int k = b80; k <= b11k; ++k) { const double p = (double) buf[(size_t) k] * buf[(size_t) k]; all += p; if (k >= b4k5) hf += p; }
            const double ratio = hf / (all + 1.0e-20);
            auto& half = pos < audio.size() / 2 ? ltasA : ltasB;
            for (int k = 0; k < bins; ++k)
            {
                const double p = (double) buf[(size_t) k] * buf[(size_t) k];
                ltas[(size_t) k] += p;
                half[(size_t) k] += p;
            }
            ++nAll;
            if (ratio > 0.45)      { for (int k = 0; k < bins; ++k) sib[(size_t) k] += (double) buf[(size_t) k] * buf[(size_t) k]; ++nSib; }
            else if (ratio < 0.12) { for (int k = 0; k < bins; ++k) vow[(size_t) k] += (double) buf[(size_t) k] * buf[(size_t) k]; ++nVow; }
        }
        if (nAll == 0) return;
        for (auto& x : ltas) x /= nAll;

        // grille 1/6 d'octave : énergie dans la bande (somme des bins)
        auto bandE = [&] (const std::vector<double>& P, double lo, double hi)
        {
            double s = 0.0;
            const int k0 = std::max (1, (int) std::ceil (lo / binHz)), k1 = std::min (bins - 1, (int) std::floor (hi / binHz));
            if (k1 < k0) { const int k = binOf (0.5 * (lo + hi)); s = P[(size_t) k] * (hi - lo) / binHz; }
            else for (int k = k0; k <= k1; ++k) s += P[(size_t) k];
            return 10.0 * std::log10 (s + 1.0e-20);
        };
        grid.assign (kGrid + 1, 0.0f);
        for (int k = 0; k <= kGrid; ++k)
        {
            const double f = k == kGrid ? 16000.0 : gridHz (k);
            grid[(size_t) k] = (float) bandE (ltas, f * std::pow (2.0, -1.0 / 12.0), std::min (0.48 * fs, f * std::pow (2.0, 1.0 / 12.0)));
        }

        // ---- sifflantes : part de trames sifflantes, fréquence du S (spectre lissé 1/6 d'octave), sévérité
        a.sibShare = (float) nSib / (float) nAll;
        if (nSib > 0 && nVow > 0)
        {
            for (auto& x : sib) x /= nSib;
            for (auto& x : vow) x /= nVow;
            std::vector<std::pair<float, float>> sp;   // fréquence, densité (dB par bin : un souffle plat ne penche d'aucun côté)
            for (float f = 4000.0f; f <= std::min (10500.0f, (float) (0.45 * fs)); f *= std::pow (2.0f, 1.0f / 24.0f))
            {
                const double lo = f * std::pow (2.0, -1.0 / 12.0), hi = f * std::pow (2.0, 1.0 / 12.0);
                sp.push_back ({ f, (float) (bandE (sib, lo, hi) - 10.0 * std::log10 (std::max (1.0, (hi - lo) / binHz))) });
            }
            auto best = std::max_element (sp.begin(), sp.end(), [] (auto& x, auto& y) { return x.second < y.second; });
            a.sibFreq = best->first;
            if (best == sp.begin() || best == sp.end() - 1)
            {
                // pas de vrai sommet (spectre qui ne fait que descendre ou monter) : centre de gravité des S
                double num = 0.0, den = 0.0;
                for (auto& [f, d] : sp) { const double w = std::pow (10.0, d / 10.0); num += w * std::log (f); den += w; }
                a.sibFreq = (float) std::exp (num / den);
            }
            // sévérité : énergie 4-11 kHz des trames sifflantes par rapport au corps (150 Hz - 4 kHz) des voyelles
            a.sibDb = (float) (bandE (sib, 4000.0, std::min (11000.0, 0.45 * fs)) - bandE (vow, 150.0, 4000.0));
            // second S : autre sommet à plus de 0.7 octave, pas plus de 5 dB sous le premier
            float best2 = -1000.0f;
            for (auto& [f, d] : sp)
                if (std::abs (std::log2 (f / a.sibFreq)) > 0.7f && d > best2) { best2 = d; a.sibFreq2 = f; }
            // ce doit être un vrai sommet local (pas la pente du premier)
            bool isPeak = false;
            for (size_t i = 1; i + 1 < sp.size(); ++i)
                if (sp[i].first == a.sibFreq2 && sp[i].second >= sp[i - 1].second && sp[i].second >= sp[i + 1].second) isPeak = true;
            if (! isPeak || best2 < best->second - 3.0f) a.sibFreq2 = 0.0f;
            else a.sib2Db = a.sibDb - (best->second - best2);
        }

        // ---- résonances : sommets étroits (1/24 d'octave) au-dessus du spectre lissé à 1/2 octave, présents dans les
        //      DEUX moitiés de la prise (une harmonique de note tenue bouge avec la mélodie, une résonance de pièce ou de
        //      micro reste à la même fréquence)
        {
            auto fineOf = [&] (const std::vector<double>& P)
            {
                std::vector<std::pair<float, float>> fine;
                for (float f = 700.0f; f <= 7000.0f; f *= std::pow (2.0f, 1.0f / 24.0f))
                    fine.push_back ({ f, (float) bandE (P, f * std::pow (2.0, -1.0 / 48.0), f * std::pow (2.0, 1.0 / 48.0)) });
                return fine;
            };
            const auto fine = fineOf (ltas), fa = fineOf (ltasA), fb = fineOf (ltasB);
            auto excess = [] (const std::vector<std::pair<float, float>>& v, size_t i)
            {
                float smooth = 0.0f;
                for (int j = -6; j <= 6; ++j) smooth += v[i + (size_t) j].second;
                return v[i].second - smooth / 13.0f;
            };
            std::vector<std::pair<float, float>> cand;
            for (size_t i = 6; i + 6 < fine.size(); ++i)
            {
                const float ex = excess (fine, i);
                if (ex > 5.0f && excess (fa, i) > 3.5f && excess (fb, i) > 3.5f
                    && fine[i].second >= fine[i - 1].second && fine[i].second >= fine[i + 1].second)
                    cand.push_back ({ fine[i].first, ex });
            }
            std::sort (cand.begin(), cand.end(), [] (auto& x, auto& y) { return x.second > y.second; });
            for (auto& c : cand)
            {
                bool far = true;
                for (auto& r : a.resonances) if (std::abs (std::log2 (c.first / r.first)) < 0.5f) far = false;
                if (far) a.resonances.push_back (c);
                if (a.resonances.size() >= 2 || (a.resonances.size() == 1 && c.second < 6.5f)) break;
            }
        }
    }

    // ---------------------------------------------------------------- 3) hauteur (YIN)
    void pitchAnalysis (Analysis& a)
    {
        const int decim = std::max (1, (int) std::lround (fs / 12000.0));
        const double fsd = fs / decim;
        Biquad lp1, lp2;
        lp1.setCoeffs (BiquadCoeffs::lowpass (fs, 3500.0, 0.54));
        lp2.setCoeffs (BiquadCoeffs::lowpass (fs, 3500.0, 1.31));
        std::vector<float> x;
        x.reserve (audio.size() / (size_t) decim + 1);
        for (size_t i = 0; i < audio.size(); ++i)
        {
            const float y = lp2.process (lp1.process (audio[i], 0), 0);
            if (i % (size_t) decim == 0) x.push_back (y);
        }
        const int W = (int) std::lround (0.022 * fsd), tauMin = (int) (fsd / 1000.0), tauMax = (int) (fsd / 60.0), hop = (int) std::lround (0.01 * fsd);
        std::vector<float> d ((size_t) tauMax + 2);
        std::vector<float> f0s;
        std::vector<bool> voiced;
        for (size_t pos = 0; pos + (size_t) (W + tauMax + 2) < x.size(); pos += (size_t) hop)
        {
            double e = 0.0;
            for (int i = 0; i < W; ++i) e += (double) x[pos + (size_t) i] * x[pos + (size_t) i];
            if (10.0 * std::log10 (e / W + 1.0e-12) < gateDb + 6.0f) { voiced.push_back (false); continue; }
            float run = 0.0f;
            d[0] = 1.0f;
            int found = -1;
            for (int t = 1; t <= tauMax; ++t)
            {
                double s = 0.0;
                for (int i = 0; i < W; ++i) { const float df = x[pos + (size_t) i] - x[pos + (size_t) (i + t)]; s += (double) df * df; }
                run += (float) s;
                d[(size_t) t] = run > 0.0f ? (float) s * (float) t / run : 1.0f;
            }
            for (int t = tauMin; t < tauMax; ++t)
                if (d[(size_t) t] < 0.13f)
                {
                    while (t + 1 < tauMax && d[(size_t) t + 1] < d[(size_t) t]) ++t;
                    found = t;
                    break;
                }
            if (found < 0) { voiced.push_back (false); continue; }
            float tau = (float) found;
            const float y0 = d[(size_t) found - 1], y1 = d[(size_t) found], y2 = d[(size_t) found + 1];
            const float den = y0 - 2.0f * y1 + y2;
            if (std::abs (den) > 1.0e-9f) tau += 0.5f * (y0 - y2) / den;
            f0s.push_back ((float) (fsd / tau));
            voiced.push_back (true);
        }
        if (voiced.empty()) return;
        a.voicedRatio = (float) f0s.size() / (float) voiced.size();
        if (f0s.size() < 20) return;
        a.f0Median = percentile (f0s, 0.5f);
        a.f0Low = percentile (f0s, 0.05f);
        a.f0High = percentile (f0s, 0.95f);
        a.rangeSt = 12.0f * std::log2 (a.f0High / std::max (1.0f, a.f0Low));

        // notes tenues : longueur moyenne des passages voisés consécutifs
        int runs = 0, cur = 0, total = 0;
        for (bool v : voiced)
        {
            if (v) ++cur;
            else if (cur > 0) { if (cur >= 3) { ++runs; total += cur; } cur = 0; }
        }
        if (cur >= 3) { ++runs; total += cur; }
        a.avgRunSec = runs > 0 ? (float) total / (float) runs * 0.01f : 0.0f;

        // auto-tune : hauteurs collées à une grille de demi-tons (quel que soit le diapason) et quasi immobiles
        double cs = 0.0, sn = 0.0;
        std::vector<float> deltas;
        float prev = 0.0f;
        bool hasPrev = false;
        size_t fi = 0;
        for (bool v : voiced)
        {
            if (! v) { hasPrev = false; continue; }
            const float cents = 1200.0f * std::log2 (f0s[fi++] / 440.0f);
            const double ang = 2.0 * juce::MathConstants<double>::pi * cents / 100.0;
            cs += std::cos (ang); sn += std::sin (ang);
            if (hasPrev && std::abs (cents - prev) < 80.0f) deltas.push_back (std::abs (cents - prev));
            prev = cents;
            hasPrev = true;
        }
        const float R = (float) (std::sqrt (cs * cs + sn * sn) / (double) f0s.size());
        const float med = deltas.size() > 10 ? percentile (deltas, 0.5f) : 30.0f;
        a.autotune = juce::jlimit (0.0f, 1.0f, 0.6f * juce::jlimit (0.0f, 1.0f, (R - 0.25f) / 0.45f)
                                              + 0.4f * juce::jlimit (0.0f, 1.0f, (10.0f - med) / 8.0f));
    }

    // ---------------------------------------------------------------- 4) plosives
    // plosive : bouffée de très graves (sous la fondamentale de la voix) qui domine le signal et retombe en moins de 90 ms
    void plosiveAnalysis (Analysis& a)
    {
        const double fc = juce::jlimit (60.0, 120.0, a.f0Low > 40.0f ? 0.75 * a.f0Low : 110.0);
        Biquad lp1, lp2;
        lp1.setCoeffs (BiquadCoeffs::lowpass (fs, fc, 0.54));
        lp2.setCoeffs (BiquadCoeffs::lowpass (fs, fc, 1.31));
        const int blk = std::max (32, (int) std::lround (0.01 * fs));
        std::vector<float> dl, da;
        for (size_t pos = 0; pos + (size_t) blk <= audio.size(); pos += (size_t) blk)
        {
            double el = 0.0, ea = 0.0;
            for (int i = 0; i < blk; ++i)
            {
                const float s = audio[pos + (size_t) i];
                const float l = lp2.process (lp1.process (s, 0), 0);
                el += (double) l * l; ea += (double) s * s;
            }
            dl.push_back ((float) (10.0 * std::log10 (el / blk + 1.0e-12)));
            da.push_back ((float) (10.0 * std::log10 (ea / blk + 1.0e-12)));
        }
        const float typical = percentile (dl, 0.5f);
        int count = 0;
        for (size_t k = 2; k + 9 < dl.size(); ++k)
        {
            const bool burst = dl[k] > dl[k - 2] + 12.0f && dl[k] > typical + 12.0f && dl[k] > da[k] - 5.0f && da[k] > gateDb + 10.0f;
            if (! burst) continue;
            bool falls = false;
            for (size_t j = k + 1; j <= k + 9; ++j) if (dl[j] < dl[k] - 10.0f) falls = true;
            if (falls) { ++count; k += 15; }
        }
        const float minutes = (float) (audio.size() / fs / 60.0);
        a.plosivesPerMin = minutes > 0.0f ? (float) count / minutes : 0.0f;
    }

    // ---------------------------------------------------------------- EQ
    struct Bell { float f, g, q; bool dyn; float prio; };

    void designEq (Analysis& a, int style, const StyleSpec& sp, int intensity, float inDb, Proposal& pr)
    {
        auto& v = pr.values;
        const float eqMul = intensity == subtle ? 0.8f : (intensity == bold ? 1.1f : 1.0f);
        const bool haveF0 = a.f0Low > 40.0f;
        const float lowCut = juce::jlimit (55.0f, 150.0f, haveF0 ? 0.72f * a.f0Low * (style == styleRap ? 1.05f : 1.0f) : 80.0f);

        // écart à la cible, aligné sur les médiums (400 Hz - 1.2 kHz)
        std::vector<float> dev ((size_t) kGrid + 1), tgt ((size_t) kGrid + 1);
        for (int k = 0; k <= kGrid; ++k) tgt[(size_t) k] = targetDb (k == kGrid ? 16000.0f : gridHz (k), style, sp, a.f0Low);
        double off = 0.0; int no = 0;
        for (int k = 0; k <= kGrid; ++k)
        {
            const float f = gridHz (k);
            if (f >= 400.0f && f <= 1200.0f) { off += grid[(size_t) k] - tgt[(size_t) k]; ++no; }
        }
        const float offset = no > 0 ? (float) (off / no) : 0.0f;
        for (int k = 0; k <= kGrid; ++k) dev[(size_t) k] = grid[(size_t) k] - tgt[(size_t) k] - offset;
        // lissage 1/3 d'octave
        auto smooth3 = [] (std::vector<float> x)
        {
            std::vector<float> y (x.size());
            for (size_t k = 0; k < x.size(); ++k)
                y[k] = (x[k == 0 ? 0 : k - 1] + 2.0f * x[k] + x[std::min (x.size() - 1, k + 1)]) * 0.25f;
            return y;
        };
        dev = smooth3 (dev);
        a.spectrum = grid;   // pour l'affichage : spectre et cible alignés
        {
            auto& t = a.target;
            t.resize (tgt.size());
            for (size_t k = 0; k < tgt.size(); ++k) t[k] = tgt[k] + offset;
        }
        auto zoneMean = [&] (const std::vector<float>& d, float lo, float hi)
        {
            double s = 0.0; int n = 0;
            for (int k = 0; k <= kGrid; ++k) { const float f = k == kGrid ? 16000.0f : gridHz (k); if (f >= lo && f < hi && f > lowCut * 1.2f) { s += d[(size_t) k]; ++n; } }
            return n > 0 ? (float) (s / n) : 0.0f;
        };
        static const float zl[kZones] { 60, 140, 280, 550, 1200, 2500, 8000 }, zh[kZones] { 140, 280, 550, 1100, 2500, 5500, 16001 };
        for (int z = 0; z < kZones; ++z) a.zoneDev[(size_t) z] = zoneMean (dev, zl[z], zh[z]);

        // correction voulue (dB) : l'inverse de l'écart, bornée
        std::vector<float> C ((size_t) kGrid + 1);
        // asymétrique : on retire franchement ce qui dépasse, on ne comble qu'à moitié ce qui manque (une voix chantée est
        // naturellement plus sombre que la parole ; booster large et doux, couper précis)
        for (int k = 0; k <= kGrid; ++k)
        {
            const float c = -dev[(size_t) k];
            C[(size_t) k] = c < 0.0f ? juce::jmax (-9.0f, 0.85f * c * eqMul) : juce::jmin (5.0f, 0.5f * c * eqMul);
        }
        auto fOf = [] (int k) { return k == kGrid ? 16000.0f : gridHz (k); };
        auto inRange = [&] (int k, float lo, float hi) { const float f = fOf (k); return f >= lo && f <= hi && f > lowCut * 1.25f; };

        // ---- EQ ANALOG : HIGH, LOW, MID ajustés sur la correction (courbes exactes du module)
        auto curveDb = [&] (float l, float m, float h, float lf, float hf, int k)
        {
            return VocalEQ::Curve::make (fs, l, m, h, lf, hf, 0.0f).db (std::min ((double) fOf (k), 0.45 * fs));
        };
        static constexpr float lowHz[] { 80.0f, 100.0f, 125.0f }, highHz[] { 8000.0f, 10000.0f, 20000.0f };
        const float maxHigh = intensity == subtle ? 4.5f : (intensity == bold ? 7.0f : 6.0f);
        float bestH = 0.0f; int bestHf = sp.highFreq;
        {
            double bestErr = 1.0e30;
            for (int hfI = 0; hfI < 3; ++hfI)
                for (float h = 0.0f; h <= maxHigh; h += 0.25f)
                {
                    double e = (hfI == sp.highFreq ? 0.0 : 6.0);   // préférence pour la fréquence d'air du style
                    for (int k = 0; k <= kGrid; ++k)
                        if (inRange (k, 1500.0f, 15000.0f)) { const double r = C[(size_t) k] - curveDb (0, 0, h, 125.0f, highHz[hfI], k); e += r * r; }
                    if (e < bestErr) { bestErr = e; bestH = h; bestHf = hfI; }
                }
        }
        for (int k = 0; k <= kGrid; ++k) C[(size_t) k] -= curveDb (0, 0, bestH, 125.0f, highHz[bestHf], k);

        const float maxLow = intensity == subtle ? 3.0f : (intensity == bold ? 5.0f : 4.0f);
        float bestL = 0.0f; int bestLf = 2;
        {
            double base = 0.0, need = 0.0; int nn = 0;
            for (int k = 0; k <= kGrid; ++k) if (inRange (k, 60.0f, 300.0f)) { base += (double) C[(size_t) k] * C[(size_t) k]; if (fOf (k) <= 260.0f) { need += C[(size_t) k]; ++nn; } }
            const bool thin = nn > 0 && need / nn > 1.0;   // le LOW (boost) ne sert que si le bas manque vraiment de corps
            double bestErr = base * 0.85;   // le LOW ne sert que s'il améliore vraiment
            for (int lfI = 0; lfI < 3; ++lfI)
                for (float l = 0.5f; thin && l <= maxLow; l += 0.5f)
                {
                    double e = 0.0;
                    for (int k = 0; k <= kGrid; ++k)
                        if (inRange (k, 60.0f, 300.0f)) { const double r = C[(size_t) k] - curveDb (l, 0, 0, lowHz[lfI], 10000.0f, k); e += r * r; }
                    if (e < bestErr) { bestErr = e; bestL = l; bestLf = lfI; }
                }
        }
        for (int k = 0; k <= kGrid; ++k) C[(size_t) k] -= curveDb (bestL, 0, 0, lowHz[bestLf], 10000.0f, k);

        float bestM = 0.0f;
        {
            double base = 0.0, mud = 0.0; int nm = 0;
            for (int k = 0; k <= kGrid; ++k) if (inRange (k, 150.0f, 900.0f)) base += (double) C[(size_t) k] * C[(size_t) k];
            for (int k = 0; k <= kGrid; ++k) if (inRange (k, 250.0f, 550.0f)) { mud += C[(size_t) k]; ++nm; }
            const bool muddy = nm > 0 && mud / nm < -1.0;   // le MID (nettoyeur de boue) ne sert que si 250-550 Hz déborde
            double bestErr = base * 0.9;
            for (float m = 0.5f; muddy && m <= 6.0f; m += 0.5f)
            {
                double e = 0.0;
                for (int k = 0; k <= kGrid; ++k)
                    if (inRange (k, 150.0f, 900.0f)) { const double r = C[(size_t) k] - curveDb (0, m, 0, 125.0f, 10000.0f, k); e += r * r; }
                if (e < bestErr) { bestErr = e; bestM = m; }
            }
        }
        for (int k = 0; k <= kGrid; ++k) C[(size_t) k] -= curveDb (0, bestM, 0, 125.0f, 10000.0f, k);

        v[ids::eqOn] = 1.0f;
        v[ids::low] = bestL; v[ids::lowFreq] = (float) bestLf;
        v[ids::mud] = bestM;
        v[ids::high] = bestH; v[ids::highFreq] = (float) bestHf;
        v[ids::smartS] = 0.0f;   // les S sont confiés au de-esser, réglé plus loin sur la voix réelle
        v[ids::lowCut] = 0.0f;

        // ---- EQ paramétrique sur l'écart restant
        std::vector<Bell> bells;
        // "boom" : graves en trop sous 160 Hz -> shelf bas
        float shelfG = 0.0f;
        {
            double s = 0.0; int n = 0;
            for (int k = 0; k <= kGrid; ++k) if (inRange (k, 60.0f, 160.0f)) { s += C[(size_t) k]; ++n; }
            const float m = n > 0 ? (float) (s / n) : 0.0f;
            if (m < -2.0f) shelfG = juce::jlimit (-6.0f, -1.5f, 0.8f * m);
        }
        if (shelfG < 0.0f)
            for (int k = 0; k <= kGrid; ++k)
            {
                const auto sh = ParametricEQ::design (fs, ParametricEQ::lowShelf, 170.0, shelfG, 0.7);
                C[(size_t) k] -= (float) (20.0 * std::log10 (std::abs (sh.response (fs, std::min ((double) fOf (k), 0.45 * fs))) + 1.0e-9));
            }

        // lobes : zones continues à couper (ou à remonter légèrement), cherchées sur l'écart lissé à ~2/3 d'octave
        // (les harmoniques d'une voix chantée font des bosses de 1/6 d'octave qui ne sont pas des défauts)
        {
            std::vector<float> s5 (C.size());
            for (size_t k = 0; k < C.size(); ++k)
            {
                float acc = 0.0f, w = 0.0f;
                for (int j = -2; j <= 2; ++j)
                {
                    const int i = juce::jlimit (0, (int) C.size() - 1, (int) k + j);
                    const float wj = 3.0f - (float) std::abs (j);
                    acc += wj * C[(size_t) i]; w += wj;
                }
                s5[k] = acc / w;
            }
            C = s5;
        }
        auto lobes = [&] (bool cut)
        {
            int k = 0;
            while (k <= kGrid)
            {
                const bool want = inRange (k, 160.0f, 7000.0f) && (cut ? C[(size_t) k] < -1.2f : C[(size_t) k] > 1.5f);
                if (! want) { ++k; continue; }
                int e = k, pk = k;
                while (e + 1 <= kGrid && inRange (e + 1, 160.0f, 7000.0f) && (cut ? C[(size_t) e + 1] < -1.2f : C[(size_t) e + 1] > 1.5f))
                {
                    ++e;
                    if (cut ? C[(size_t) e] < C[(size_t) pk] : C[(size_t) e] > C[(size_t) pk]) pk = e;
                }
                const float bwOct = std::max (1.0f / 3.0f, (float) (e - k + 1) / 6.0f);
                const float q = juce::jlimit (0.6f, 4.0f, 1.0f / (2.0f * std::sinh (0.5f * std::log (2.0f) * bwOct)));
                const float f = fOf (pk), val = C[(size_t) pk];
                if (cut) bells.push_back ({ f, juce::jlimit (-7.0f, -1.0f, 0.85f * val), q, f >= 1800.0f, std::abs (val) * bwOct });
                else if (f >= 1000.0f && f <= 6000.0f && bwOct >= 0.5f)   // remontée : seulement la présence
                    bells.push_back ({ f, juce::jlimit (1.0f, 2.5f, 0.8f * val), q, false, 0.5f * val * bwOct });
                k = e + 1;
            }
        };
        lobes (true);
        lobes (false);
        // une seule remontée au plus
        {
            bool seen = false;
            bells.erase (std::remove_if (bells.begin(), bells.end(), [&] (const Bell& b)
            {
                if (b.g <= 0.0f) return false;
                if (seen) return true;
                seen = true;
                return false;
            }), bells.end());
        }
        // résonances : coupes étroites et dynamiques (sauf si une coupe large tombe déjà dessus)
        for (auto& r : a.resonances)
            if (std::none_of (bells.begin(), bells.end(), [&] (const Bell& b) { return b.g < 0.0f && std::abs (std::log2 (b.f / r.first)) < 0.34f; }))
                bells.push_back ({ r.first, -juce::jlimit (1.5f, 3.0f, 0.5f * r.second) * eqMul, 6.0f, true, r.second * 0.6f });
        std::sort (bells.begin(), bells.end(), [] (const Bell& x, const Bell& y) { return x.prio > y.prio; });
        if (bells.size() > 6) bells.resize (6);

        // garde-fou "voix trop fine" : le corps (140-280 Hz) ne doit pas finir creusé
        {
            float body = 0.0f; int n = 0;
            for (int k = 0; k <= kGrid; ++k)
            {
                const float f = fOf (k);
                if (f < 140.0f || f >= 280.0f || f <= lowCut * 1.25f) continue;
                float eq = (float) curveDb (bestL, bestM, bestH, lowHz[bestLf], highHz[bestHf], k);
                for (auto& b : bells)
                    eq += (float) (20.0 * std::log10 (std::abs (ParametricEQ::design (fs, ParametricEQ::bell, b.f, b.dyn ? 0.6f * b.g : b.g, b.q)
                                                                 .response (fs, f)) + 1.0e-9));
                if (shelfG < 0.0f)
                    eq += (float) (20.0 * std::log10 (std::abs (ParametricEQ::design (fs, ParametricEQ::lowShelf, 170.0, shelfG, 0.7).response (fs, f)) + 1.0e-9));
                body += dev[(size_t) k] + eq;
                ++n;
            }
            body = n > 0 ? body / (float) n : 0.0f;
            if (body < -2.5f)
            {
                for (auto& b : bells) if (b.f < 600.0f && b.g < 0.0f) b.g *= 0.55f;
                shelfG *= 0.5f;
                v[ids::low] = std::min (6.0f, bestL + 1.0f);
            }
        }

        // bande 1 : coupe-bas 24 dB/oct ; bande 2 : shelf ; bandes 3..8 : cloches
        juce::StringArray desc;
        auto setBand = [&] (int b, int type, float f, float g, float q, int slope, bool dyn, float thr)
        {
            v[ids::peqOn[b]] = 1.0f; v[ids::peqType[b]] = (float) type; v[ids::peqFreq[b]] = f; v[ids::peqGain[b]] = std::round (g * 10.0f) / 10.0f;
            v[ids::peqQ[b]] = q; v[ids::peqSlope[b]] = (float) slope; v[ids::peqDyn[b]] = dyn ? 1.0f : 0.0f; v[ids::peqThr[b]] = thr;
        };
        for (int b = 0; b < ParametricEQ::kBands; ++b) v[ids::peqOn[b]] = 0.0f;
        v[ids::peqPower] = 1.0f;
        setBand (0, ParametricEQ::lowCut, std::round (lowCut), 0.0f, 0.71f, 1, false, -30.0f);
        desc.add ("low cut " + juce::String (juce::roundToInt (lowCut)) + " Hz 24 dB/oct");
        if (shelfG < -0.5f)
        {
            setBand (1, ParametricEQ::lowShelf, 170.0f, shelfG, 0.7f, 0, false, -30.0f);
            desc.add (juce::String (shelfG, 1) + " dB shelf (boom)");
        }
        const float inGain = dbToGain (inDb);
        int slot = 2;
        for (auto& b : bells)
        {
            float thr = -30.0f;
            float g = b.g;
            if (b.dyn)
            {
                thr = detectorThreshold (b.f, b.q, inGain);
                g = juce::jlimit (-7.5f, -1.5f, b.g * 1.3f);
            }
            setBand (slot, ParametricEQ::bell, std::round (b.f), g, std::round (b.q * 100.0f) / 100.0f, 0, b.dyn, std::round (thr * 2.0f) * 0.5f);
            desc.add ((b.dyn ? "dyn " : "") + juce::String (g, 1) + " dB @ " + hzText (b.f));
            if (++slot >= ParametricEQ::kBands) break;
        }
        pr.lines.add ("PARAMETRIC EQ  " + desc.joinIntoString (sep()));
        pr.lines.add ("EQ ANALOG  LOW " + juce::String (v[ids::low], 1) + " @ " + juce::String (juce::roundToInt (lowHz[bestLf])) + " Hz" + sep() + "MID "
                      + juce::String (bestM, 1) + sep() + "HIGH " + juce::String (bestH, 1) + " @ " + juce::String (bestHf == 0 ? "8K" : bestHf == 1 ? "10K" : "20K")
                      );
    }

    static juce::String hzText (float f)
    {
        return f >= 1000.0f ? juce::String (f / 1000.0f, 1) + " kHz" : juce::String (juce::roundToInt (f)) + " Hz";
    }

    // seuil d'une bande dynamique : niveau médian (un peu au-dessus) de sa zone, mesuré comme le fait l'EQ
    float detectorThreshold (float f, float q, float inGain) const
    {
        Biquad det;
        det.setCoeffs (BiquadCoeffs::bandpass (fs, f, std::max (0.7f, q)));
        const float atk = (float) (1.0 - std::exp (-1.0 / (0.003 * fs))), rel = (float) (1.0 - std::exp (-1.0 / (0.090 * fs)));
        float env = 0.0f;
        std::vector<float> lv;
        const int blk = std::max (32, (int) std::lround (0.01 * fs));
        for (size_t i = 0; i < audio.size(); ++i)
        {
            const float d = std::abs (det.process (audio[i] * inGain, 0));
            env += (d > env ? atk : rel) * (d - env);
            if (i % (size_t) blk == 0) lv.push_back (gainToDb (env + 1.0e-9f));
        }
        return juce::jlimit (-60.0f, -6.0f, percentile (lv, 0.6f) - 1.0f);
    }

    // gain total (EQ paramétrique statique + EQ ANALOG) à une fréquence, pour estimer ce qui arrive au de-esser
    float eqGainAt (const std::map<juce::String, float>& v, float hz) const
    {
        static constexpr float lowHz[] { 80.0f, 100.0f, 125.0f }, highHz[] { 8000.0f, 10000.0f, 20000.0f };
        double db = VocalEQ::Curve::make (fs, value (v, ids::low), value (v, ids::mud), value (v, ids::high),
                                          lowHz[juce::jlimit (0, 2, juce::roundToInt (value (v, ids::lowFreq)))],
                                          highHz[juce::jlimit (0, 2, juce::roundToInt (value (v, ids::highFreq)))], 0.0f).db (hz);
        const auto p = paramsFrom (v);
        for (auto& b : p.peq)
            if (b.on) db += 20.0 * std::log10 (std::abs (ParametricEQ::bandResponse (fs, b, hz, b.dyn ? 0.5f * b.gainDb : b.gainDb)) + 1.0e-9);
        return (float) db;
    }

    // ---------------------------------------------------------------- simulation hors temps réel
    struct Sim
    {
        std::vector<float> fet, la, de1, de2, out;
        std::vector<std::array<float, 4>> mb;
    };

    void prepareMasks()
    {
        const int chunk = VocalChain::kChunk;
        const size_t nc = audio.size() / (size_t) chunk;
        activeMask.assign (nc, false);
        sibMask.assign (nc, false);
        Biquad hp;
        hp.setCoeffs (BiquadCoeffs::highpass (fs, 4500.0, 0.707));
        for (size_t c = 0; c < nc; ++c)
        {
            double e = 0.0, h = 0.0;
            for (int i = 0; i < chunk; ++i)
            {
                const float x = audio[c * (size_t) chunk + (size_t) i];
                const float y = hp.process (x, 0);
                e += (double) x * x; h += (double) y * y;
            }
            const bool act = 10.0 * std::log10 (e / chunk + 1.0e-12) > gateDb + 3.0f;
            activeMask[c] = act;
            sibMask[c] = act && h / (e + 1.0e-20) > 0.4;
        }
        // trop peu de S francs : on prend les blocs les plus riches en aigus
        if (std::count (sibMask.begin(), sibMask.end(), true) < 4)
            for (size_t c = 0; c < nc; ++c) sibMask[c] = activeMask[c];
    }

    // jusqu'où la chaîne est utile pour la mesure : les modules placés après sont coupés (et pas calculés)
    enum Stage { toComp, toMultiband, toDeEsser, full };

    Sim simulate (const std::map<juce::String, float>& values, Stage stage, bool keepOut = false) const
    {
        auto v = values;
        if (stage != full)
        {
            v[ids::resoOn] = 0.0f; v[ids::satOn] = 0.0f; v[ids::parOn] = 0.0f;
            if (stage == toComp) { v[ids::mbOn] = 0.0f; v[ids::eqOn] = 0.0f; v[ids::deessOn] = 0.0f; }
            if (stage == toMultiband) { v[ids::eqOn] = 0.0f; v[ids::deessOn] = 0.0f; }
        }
        const bool measureMb = stage == toMultiband;
        auto chain = std::make_unique<VocalChain>();
        const auto p = paramsFrom (v);
        chain->prepare (fs, p, true, 0);   // LIVE : latence nulle, sans suréchantillonnage (estimation fidèle et rapide)
        chain->setSimulationMode (true);
        chain->setParams (p, true);
        std::vector<float> x = audio;
        Sim s;
        const int chunk = VocalChain::kChunk;
        for (size_t pos = 0; pos + (size_t) chunk <= x.size(); pos += (size_t) chunk)
        {
            float* ch[1] { x.data() + pos };
            chain->process (ch, 1, chunk);
            s.fet.push_back (chain->get1176ReductionDb());
            s.la.push_back (chain->getLA2AReductionDb());
            s.de1.push_back (chain->getDeEssReductionDb());
            s.de2.push_back (chain->getDeEss2ReductionDb());
            if (measureMb)
            {
                const auto& m = chain->getMultibandMeter();
                s.mb.push_back ({ m.levelDb[0].load(), m.levelDb[1].load(), m.levelDb[2].load(), m.levelDb[3].load() });
            }
        }
        if (keepOut) s.out = std::move (x);
        return s;
    }

    float rmsDbOf (const std::vector<float>& x, const std::vector<bool>& mask) const
    {
        const int chunk = VocalChain::kChunk;
        double e = 0.0; size_t n = 0;
        for (size_t c = 0; c < mask.size(); ++c)
            if (mask[c])
                for (int i = 0; i < chunk && c * (size_t) chunk + (size_t) i < x.size(); ++i)
                { const float s = x[c * (size_t) chunk + (size_t) i]; e += (double) s * s; ++n; }
        return n > 0 ? (float) (10.0 * std::log10 (e / (double) n + 1.0e-12)) : -100.0f;
    }

    std::vector<float> raw, audio;
    double fs;
    std::map<juce::String, float> defaults;
    int blockLen = 960, fftOrder = 12;
    float gateDb = -60.0f;
    std::vector<double> ltas;
    std::vector<float> grid;
    std::vector<bool> activeMask, sibMask;
    Analysis last;
};
} // namespace onirique::assist
