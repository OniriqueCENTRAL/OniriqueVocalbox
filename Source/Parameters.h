#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace onirique
{
namespace ids
{
    inline constexpr const char* input     = "input";
    inline constexpr const char* low       = "low";
    inline constexpr const char* mud       = "mud";
    inline constexpr const char* high      = "high";
    inline constexpr const char* deessFreq = "deessFreq";
    inline constexpr const char* deessAmt  = "deessAmt";
    inline constexpr const char* drive     = "drive";
    inline constexpr const char* satType   = "satType";
    inline constexpr const char* output    = "output";
    inline constexpr const char* autoGain  = "autoGain";
    inline constexpr const char* bypass    = "bypass";
    inline constexpr const char* liveMode  = "liveMode";
    inline constexpr const char* oversampling = "oversampling";
    inline constexpr const char* plosive   = "plosive";
    inline constexpr const char* resoListen = "resoListen";
    inline constexpr const char* chainOrder = "chainOrder";
    inline constexpr const char* mbSolo[4] { "mbSolo1", "mbSolo2", "mbSolo3", "mbSolo4" };
    inline constexpr const char* lowFreq   = "lowFreq";
    inline constexpr const char* highFreq  = "highFreq";
    inline constexpr const char* smartS    = "smartS";
    inline constexpr const char* lowCut    = "lowCut";
    inline constexpr const char* compOn    = "compOn";
    inline constexpr const char* compMaster = "compMaster";
    inline constexpr const char* parOn     = "parallelOn";
    inline constexpr const char* parAmount = "parallelAmount";
    inline constexpr const char* compMode  = "compMode";
    inline constexpr const char* compOrder = "compOrder";
    inline constexpr const char* fetIn     = "fetInput";
    inline constexpr const char* fetOut    = "fetOutput";
    inline constexpr const char* fetAttack = "fetAttack";
    inline constexpr const char* fetRelease = "fetRelease";
    inline constexpr const char* fetRatio  = "fetRatio";
    inline constexpr const char* laGain    = "laGain";
    inline constexpr const char* laPeak    = "laPeakReduction";
    inline constexpr const char* laLimit   = "laLimit";
    inline constexpr const char* mbOn      = "mbOn";
    inline constexpr const char* mbX[3]    { "mbXover1", "mbXover2", "mbXover3" };
    inline constexpr const char* mbThr[4]  { "mbThresh1", "mbThresh2", "mbThresh3", "mbThresh4" };
    inline constexpr const char* mbGain[4] { "mbGain1", "mbGain2", "mbGain3", "mbGain4" };
    inline constexpr const char* mbAttack  = "mbAttack";
    inline constexpr const char* mbRelease = "mbRelease";
    // EQ paramétrique : 6 bandes
    inline constexpr const char* peqOn[6]   { "peq1On", "peq2On", "peq3On", "peq4On", "peq5On", "peq6On" };
    inline constexpr const char* peqType[6] { "peq1Type", "peq2Type", "peq3Type", "peq4Type", "peq5Type", "peq6Type" };
    inline constexpr const char* peqFreq[6] { "peq1Freq", "peq2Freq", "peq3Freq", "peq4Freq", "peq5Freq", "peq6Freq" };
    inline constexpr const char* peqGain[6] { "peq1Gain", "peq2Gain", "peq3Gain", "peq4Gain", "peq5Gain", "peq6Gain" };
    inline constexpr const char* peqQ[6]    { "peq1Q", "peq2Q", "peq3Q", "peq4Q", "peq5Q", "peq6Q" };
    inline constexpr const char* resoAmt   = "resoAmount";
    inline constexpr const char* resoLow   = "resoLow";
    inline constexpr const char* resoMid   = "resoMid";
    inline constexpr const char* resoHigh  = "resoHigh";
    inline constexpr const char* resoLowFreq   = "resoLowFreq";
    inline constexpr const char* resoMidFreq   = "resoMidFreq";
    inline constexpr const char* resoHighFreq  = "resoHighFreq";
    inline constexpr const char* resoLowWidth  = "resoLowWidth";
    inline constexpr const char* resoMidWidth  = "resoMidWidth";
    inline constexpr const char* resoHighWidth = "resoHighWidth";
    inline constexpr const char* eqOn      = "eqOn";
    inline constexpr const char* resoOn    = "resoOn";
    inline constexpr const char* deessOn   = "deessOn";
    inline constexpr const char* satOn     = "satOn";
}

// ---- ordre de traitement des 6 modules (EQ, RESONANCE, COMP, MULTIBAND, DE-ESSER, SATURATION) :
//      une permutation, rangée dans un seul paramètre entier 0..719 (code de Lehmer, 0 = ordre d'origine)
constexpr int kOrderModules = 6;
inline const char* orderName (int module)
{
    static const char* n[kOrderModules] { "EQ", "RESO", "COMP", "MULTI", "DE-ESS", "SAT" };
    return n[juce::jlimit (0, kOrderModules - 1, module)];
}

inline std::array<int, kOrderModules> decodeOrder (int index)
{
    static constexpr int fact[kOrderModules] { 120, 24, 6, 2, 1, 1 };
    std::vector<int> pool { 0, 1, 2, 3, 4, 5 };
    std::array<int, kOrderModules> out {};
    index = juce::jlimit (0, 719, index);
    for (int i = 0; i < kOrderModules; ++i)
    {
        const int k = index / fact[i];
        index %= fact[i];
        out[(size_t) i] = pool[(size_t) k];
        pool.erase (pool.begin() + k);
    }
    return out;
}

inline int encodeOrder (const std::array<int, kOrderModules>& order)
{
    static constexpr int fact[kOrderModules] { 120, 24, 6, 2, 1, 1 };
    std::vector<int> pool { 0, 1, 2, 3, 4, 5 };
    int index = 0;
    for (int i = 0; i < kOrderModules; ++i)
    {
        const auto it = std::find (pool.begin(), pool.end(), order[(size_t) i]);
        const int k = (int) (it - pool.begin());
        index += k * fact[i];
        pool.erase (it);
    }
    return index;
}

inline juce::String orderText (int index)
{
    juce::StringArray s;
    for (int m : decodeOrder (index)) s.add (orderName (m));
    return s.joinIntoString (" > ");
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    using Attr = AudioParameterFloatAttributes;

    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    auto dbText = [] (float v, int)
    {
        v = std::round (v * 10.0f) / 10.0f + 0.0f;   // évite "-0.0"
        return String (v > 0.0f ? "+" : "") + String (v, 1) + " dB";
    };
    auto knobText = [] (float v, int) { return String (v, 1); };

    p.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::input, 1 }, "Input",
        NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
        Attr().withLabel ("dB").withStringFromValueFunction (dbText)));

    p.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::low, 1 }, "Low",
        NormalisableRange<float> (0.0f, 10.0f, 0.01f), 0.0f,
        Attr().withStringFromValueFunction (knobText)));

    p.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::mud, 1 }, "Mud Cleaner",
        NormalisableRange<float> (0.0f, 10.0f, 0.01f), 0.0f,
        Attr().withStringFromValueFunction (knobText)));

    p.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::high, 1 }, "High / Air",
        NormalisableRange<float> (0.0f, 10.0f, 0.01f), 0.0f,
        Attr().withStringFromValueFunction (knobText)));

    NormalisableRange<float> freqRange (3000.0f, 10000.0f, 1.0f);
    freqRange.setSkewForCentre (5500.0f);
    p.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::deessFreq, 1 }, "De-Ess Freq",
        freqRange, 4000.0f,
        Attr().withLabel ("Hz").withStringFromValueFunction ([] (float v, int)
                                                             { return String (juce::roundToInt (v)) + " Hz"; })));

    p.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::deessAmt, 1 }, "De-Ess Amount",
        NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f,
        Attr().withLabel ("%").withStringFromValueFunction ([] (float v, int)
                                                            { return String (juce::roundToInt (v)) + " %"; })));

    p.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::drive, 1 }, "Drive",
        NormalisableRange<float> (0.0f, 10.0f, 0.01f), 0.0f,
        Attr().withStringFromValueFunction (knobText)));

    // défaut TUBE : les anciens projets (sans ce paramètre) gardent le son d'origine
    p.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { ids::satType, 1 }, "Saturation Type",
        StringArray { "Tube", "Analog", "Tape" }, 0));

    p.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::output, 1 }, "Output",
        NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
        Attr().withLabel ("dB").withStringFromValueFunction (dbText)));

    p.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { ids::autoGain, 1 }, "Auto Gain", false));

    p.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { ids::bypass, 1 }, "Bypass", false));

    // MIX (qualité maximale, latence compensée) / LIVE (latence nulle, pour s'enregistrer à travers le plugin)
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::liveMode, 1 }, "Mode", StringArray { "Mix", "Live" }, 0));
    // suréchantillonnage de TOUTE la chaîne de modules (mode MIX uniquement ; forcé à OFF en LIVE)
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::oversampling, 1 }, "Oversampling",
                                                         StringArray { "Off", "2x", "4x", "8x" }, 1));   // 2x par défaut : 8x coûte ~5x plus de CPU
    p.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::plosive, 1 }, "Plosives", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f,
        Attr().withLabel ("%").withStringFromValueFunction ([] (float v, int) { return String (juce::roundToInt (v)) + " %"; })));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::resoListen, 1 }, "Resonance Listen", false));
    p.push_back (std::make_unique<AudioParameterInt> (ParameterID { ids::chainOrder, 1 }, "Processing Order", 0, 719, 0,
        AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return orderText (v); })));
    for (int b = 0; b < 4; ++b)   // écoute d'une bande du multibande (solo)
        p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::mbSolo[b], 1 }, "Multiband Listen " + String (b + 1), false));

    // ---- ajoutés après la v0.1
    p.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { ids::lowFreq, 1 }, "Low Freq",
        StringArray { "80 Hz", "100 Hz", "125 Hz" }, 2));          // 125 Hz = fréquence d'origine

    // air du HIGH + détection intelligente des S (garde du de-esser pilotée par HIGH), désactivée :
    // le HIGH seul donne un boost d'aigus parfaitement propre
    p.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { ids::highFreq, 1 }, "High Freq",
        StringArray { "8K", "10K", "20K" }, 1));
    p.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { ids::smartS, 1 }, "Smart S", false));
    p.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { ids::lowCut, 1 }, "Low Cut 85 Hz", false));

    // ---- COMPRESSION : 1176 / LA-2A / DUO (module coupé par défaut : les anciens projets ne changent pas)
    // interrupteur général de la carte COMPRESSION (1176 / LA-2A + MULTIBAND), puis un par sous-module
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::compMaster, 1 }, "Compression Section On", true));
    // compression parallèle (1176 écrasé remis dans la voix), coupée par défaut
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::parOn, 1 }, "Parallel Compression On", false));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::parAmount, 1 }, "Parallel Compression Amount",
        NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f,
        Attr().withLabel ("%").withStringFromValueFunction ([] (float v, int) { return String (juce::roundToInt (v)) + " %"; })));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::compOn, 1 }, "Compression On", false));
    p.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { ids::compMode, 1 }, "Compression Mode", StringArray { "1176", "LA-2A", "Duo" }, 0));
    p.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { ids::compOrder, 1 }, "Compression Order", StringArray { "1176 > LA-2A", "LA-2A > 1176" }, 0));

    auto timeText = [] (float s)
    {
        return s < 0.001f ? String (s * 1000.0f, s < 0.0001f ? 3 : 2) + " ms"   // attaque du 1176 en ms (0.020 .. 0.80 ms)
                          : (s < 1.0f ? String (juce::roundToInt (s * 1000.0f)) + " ms" : String (s, 2) + " s");
    };
    p.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::fetIn, 1 }, "1176 Input", NormalisableRange<float> (-12.0f, 36.0f, 0.1f), 0.0f,
        Attr().withLabel ("dB").withStringFromValueFunction (dbText)));
    p.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::fetOut, 1 }, "1176 Output", NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
        Attr().withLabel ("dB").withStringFromValueFunction (dbText)));
    p.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::fetAttack, 1 }, "1176 Attack", NormalisableRange<float> (1.0f, 7.0f, 0.01f), 3.0f,
        Attr().withStringFromValueFunction ([timeText] (float v, int)
        { return timeText (800.0e-6f * std::pow (20.0f / 800.0f, (v - 1.0f) / 6.0f)); })));
    p.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::fetRelease, 1 }, "1176 Release", NormalisableRange<float> (1.0f, 7.0f, 0.01f), 5.0f,
        Attr().withStringFromValueFunction ([timeText] (float v, int)
        { return timeText (1.1f * std::pow (0.05f / 1.1f, (v - 1.0f) / 6.0f)); })));
    p.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { ids::fetRatio, 1 }, "1176 Ratio", StringArray { "4", "8", "12", "20", "All" }, 0));

    p.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::laGain, 1 }, "LA-2A Gain", NormalisableRange<float> (-12.0f, 30.0f, 0.1f), 0.0f,
        Attr().withLabel ("dB").withStringFromValueFunction (dbText)));
    p.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::laPeak, 1 }, "LA-2A Peak Reduction", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 40.0f,
        Attr().withStringFromValueFunction ([] (float v, int) { return String (juce::roundToInt (v)); })));
    p.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { ids::laLimit, 1 }, "LA-2A Mode", StringArray { "Compress", "Limit" }, 0));

    // ---- MULTIBAND : opto 4 bandes (coupé par défaut)
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::mbOn, 1 }, "Multiband On", false));
    {
        auto hz = [] (float v, int) { return v >= 1000.0f ? String (v / 1000.0f, 2) + " kHz" : String (juce::roundToInt (v)) + " Hz"; };
        struct X { float lo, hi, centre, def; const char* name; };
        const X xs[3] { { 40.0f, 1000.0f, 200.0f, 200.0f, "Multiband Low / Lo-Mid" },
                        { 300.0f, 8000.0f, 1500.0f, 1500.0f, "Multiband Lo-Mid / Hi-Mid" },
                        { 1500.0f, 16000.0f, 6000.0f, 6000.0f, "Multiband Hi-Mid / High" } };
        for (int i = 0; i < 3; ++i)
        {
            NormalisableRange<float> r (xs[i].lo, xs[i].hi, 1.0f);
            r.setSkewForCentre (xs[i].centre);
            p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::mbX[i], 1 }, xs[i].name, r, xs[i].def,
                                                                Attr().withLabel ("Hz").withStringFromValueFunction (hz)));
        }
        const char* bandNames[4] { "Low", "Lo-Mid", "Hi-Mid", "High" };
        for (int b = 0; b < 4; ++b)
        {
            p.push_back (std::make_unique<AudioParameterFloat> (
                ParameterID { ids::mbThr[b], 1 }, String ("Multiband ") + bandNames[b] + " Threshold",
                NormalisableRange<float> (-60.0f, 0.0f, 0.1f), -20.0f, Attr().withLabel ("dB").withStringFromValueFunction (dbText)));
            p.push_back (std::make_unique<AudioParameterFloat> (
                ParameterID { ids::mbGain[b], 1 }, String ("Multiband ") + bandNames[b] + " Gain",
                NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, Attr().withLabel ("dB").withStringFromValueFunction (dbText)));
        }
        NormalisableRange<float> atk (1.0f, 200.0f, 0.1f), rel (20.0f, 2000.0f, 1.0f);
        atk.setSkewForCentre (20.0f);
        rel.setSkewForCentre (250.0f);
        auto ms = [] (float v, int) { return v >= 1000.0f ? String (v / 1000.0f, 2) + " s" : String (v, v < 10.0f ? 1 : 0) + " ms"; };
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::mbAttack, 1 }, "Multiband Attack", atk, 15.0f,
                                                            Attr().withLabel ("ms").withStringFromValueFunction (ms)));
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::mbRelease, 1 }, "Multiband Release", rel, 200.0f,
                                                            Attr().withLabel ("ms").withStringFromValueFunction (ms)));
    }

    // ---- EQ PARAMÉTRIQUE : 6 bandes, toutes coupées par défaut (positions de départ utiles pour la voix)
    {
        auto hz = [] (float v, int) { return v >= 1000.0f ? String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : String (juce::roundToInt (v)) + " Hz"; };
        auto qText = [] (float v, int) { return String (v, 2); };
        const int defType[6] { 3, 1, 0, 0, 0, 2 };                         // LOW CUT, LOW SHELF, BELL x3, HIGH SHELF
        const float defFreq[6] { 80.0f, 150.0f, 400.0f, 2000.0f, 5000.0f, 10000.0f };
        const float defQ[6] { 0.71f, 0.71f, 1.0f, 1.0f, 1.0f, 0.71f };
        for (int b = 0; b < 6; ++b)
        {
            const String n = "Param EQ " + String (b + 1) + " ";
            p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::peqOn[b], 1 }, n + "On", false));
            p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::peqType[b], 1 }, n + "Type",
                StringArray { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut" }, defType[b]));
            NormalisableRange<float> fr (20.0f, 20000.0f, 0.1f);
            fr.setSkewForCentre (1000.0f);
            p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::peqFreq[b], 1 }, n + "Freq", fr, defFreq[b],
                                                                Attr().withLabel ("Hz").withStringFromValueFunction (hz)));
            p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::peqGain[b], 1 }, n + "Gain",
                NormalisableRange<float> (-18.0f, 18.0f, 0.1f), 0.0f, Attr().withLabel ("dB").withStringFromValueFunction (dbText)));
            NormalisableRange<float> qr (0.1f, 10.0f, 0.01f);
            qr.setSkewForCentre (1.0f);
            p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::peqQ[b], 1 }, n + "Q", qr, defQ[b],
                                                                Attr().withStringFromValueFunction (qText)));
        }
    }

    auto pctText = [] (float v, int) { return String (juce::roundToInt (v)) + " %"; };
    p.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::resoAmt, 1 }, "Resonance Amount",
        NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f,
        Attr().withLabel ("%").withStringFromValueFunction (pctText)));
    p.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::resoLow, 1 }, "Resonance Low",
        NormalisableRange<float> (0.0f, 100.0f, 0.1f), 40.0f,
        Attr().withLabel ("%").withStringFromValueFunction (pctText)));
    p.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::resoMid, 1 }, "Resonance Mid",
        NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
        Attr().withLabel ("%").withStringFromValueFunction (pctText)));
    p.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::resoHigh, 1 }, "Resonance High",
        NormalisableRange<float> (0.0f, 100.0f, 0.1f), 80.0f,
        Attr().withLabel ("%").withStringFromValueFunction (pctText)));

    // zones ciblées par les bandes LOW / MID / HIGH du module RESONANCE (centre + largeur)
    auto hzText = [] (float v, int)
    {
        return v >= 1000.0f ? String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : String (juce::roundToInt (v)) + " Hz";
    };
    auto octText = [] (float v, int) { return String (v, 1) + " oct"; };
    struct BandDef { const char* freqId; const char* widthId; const char* name; float lo, hi, centre, def, width; };
    for (const auto& b : { BandDef { ids::resoLowFreq,  ids::resoLowWidth,  "Low",  40.0f,   600.0f,  160.0f,  180.0f,  1.5f },
                           BandDef { ids::resoMidFreq,  ids::resoMidWidth,  "Mid",  250.0f,  6000.0f, 1200.0f, 1200.0f, 2.0f },
                           BandDef { ids::resoHighFreq, ids::resoHighWidth, "High", 2000.0f, 18000.0f, 6000.0f, 6500.0f, 2.0f } })
    {
        NormalisableRange<float> fr (b.lo, b.hi, 1.0f);
        fr.setSkewForCentre (b.centre);
        p.push_back (std::make_unique<AudioParameterFloat> (
            ParameterID { b.freqId, 1 }, String ("Resonance ") + b.name + " Freq", fr, b.def,
            Attr().withLabel ("Hz").withStringFromValueFunction (hzText)));
        p.push_back (std::make_unique<AudioParameterFloat> (
            ParameterID { b.widthId, 1 }, String ("Resonance ") + b.name + " Width",
            NormalisableRange<float> (0.3f, 4.0f, 0.01f), b.width,
            Attr().withLabel ("oct").withStringFromValueFunction (octText)));
    }

    // interrupteurs de module (bouton power) : actifs par défaut
    using Switch = std::pair<const char*, const char*>;
    for (auto [id, name] : { Switch { ids::eqOn, "EQ On" }, Switch { ids::resoOn, "Resonance On" },
                             Switch { ids::deessOn, "De-Esser On" }, Switch { ids::satOn, "Saturation On" } })
        p.push_back (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, true));

    return { p.begin(), p.end() };
}
} // namespace onirique
