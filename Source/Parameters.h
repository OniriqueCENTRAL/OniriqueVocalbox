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
    inline constexpr const char* chainOrder = "processOrder";      // 7 modules (EQ paramétrique et EQ ANALOG séparés)
    inline constexpr const char* legacyChainOrder = "chainOrder";  // ancien ordre à 6 modules (projets < 1.1), converti au chargement
    inline constexpr const char* mbSolo[4] { "mbSolo1", "mbSolo2", "mbSolo3", "mbSolo4" };
    inline constexpr const char* lowFreq   = "lowFreq";
    inline constexpr const char* highFreq  = "highFreq";
    inline constexpr const char* smartS    = "smartS";
    inline constexpr const char* lowCut    = "lowCut";
    inline constexpr const char* compOn    = "compOn";
    inline constexpr const char* compMaster = "compMaster";
    inline constexpr const char* parOn     = "parallelOn";
    inline constexpr const char* parAmount = "parallelAmount";
    inline constexpr const char* parAuto    = "parallelAuto";       // AUTO : réglage d'usine du 1176 écrasé
    inline constexpr const char* parThresh  = "parallelThreshold";  // AUTO coupé : seuil, attaque, relâchement
    inline constexpr const char* parAttack  = "parallelAttack";
    inline constexpr const char* parRelease = "parallelRelease";
    inline constexpr const char* deess2On   = "deess2On";           // second de-esser (bouton + de la carte)
    inline constexpr const char* deess2Freq = "deess2Freq";
    inline constexpr const char* deess2Amt  = "deess2Amt";
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
    // NOISE SUPPRESSOR (un seul curseur, avant le NOISE GATE)
    inline constexpr const char* nsAmount    = "nsAmount";
    // écoute d'une bande de l'EQ paramétrique (casque) : réglages de session, hors presets
    inline constexpr const char* peqListen     = "peqListen";
    inline constexpr const char* peqListenBand = "peqListenBand";
    // FX (page FX) : CHORUS, FLANGER, DELAY, REVERB
    inline constexpr const char* chorusOn = "fxChorusOn", *chorusRate = "fxChorusRate", *chorusDepth = "fxChorusDepth", *chorusMix = "fxChorusMix";
    inline constexpr const char* flangerOn = "fxFlangerOn", *flangerRate = "fxFlangerRate", *flangerDepth = "fxFlangerDepth",
                                 *flangerFeedback = "fxFlangerFeedback", *flangerMix = "fxFlangerMix";
    inline constexpr const char* delayOn = "fxDelayOn", *delaySync = "fxDelaySync", *delayTime = "fxDelayTime", *delayNote = "fxDelayNote",
                                 *delayFeedback = "fxDelayFeedback", *delayMix = "fxDelayMix", *delayPingPong = "fxDelayPingPong",
                                 *delayLowCut = "fxDelayLowCut", *delayHighCut = "fxDelayHighCut", *delayDucking = "fxDelayDucking";
    inline constexpr const char* reverbOn = "fxReverbOn", *reverbMode = "fxReverbMode", *reverbPreDelay = "fxReverbPreDelay",
                                 *reverbDecay = "fxReverbDecay", *reverbSize = "fxReverbSize", *reverbDiffusion = "fxReverbDiffusion",
                                 *reverbModRate = "fxReverbModRate", *reverbModDepth = "fxReverbModDepth", *reverbMix = "fxReverbMix",
                                 *reverbLowCut = "fxReverbLowCut", *reverbHighCut = "fxReverbHighCut", *reverbDucking = "fxReverbDucking";
    // NOISE GATE (premier module, comme celui d'ONIRIQUE BROADCAST)
    inline constexpr const char* gateOn      = "gateOn";
    inline constexpr const char* gateThr     = "gateThreshold";
    inline constexpr const char* gateRange   = "gateRange";
    inline constexpr const char* gateAttack  = "gateAttack";
    inline constexpr const char* gateHold    = "gateHold";
    inline constexpr const char* gateRelease = "gateRelease";
    inline constexpr const char* gateHyst    = "gateHysteresis";
    // EQ paramétrique : 8 bandes (pente des coupe-bas / coupe-haut, mode dynamique par bande) + interrupteur du module
    inline constexpr const char* peqPower = "peqPower";
    inline constexpr const char* peqOn[8]    { "peq1On", "peq2On", "peq3On", "peq4On", "peq5On", "peq6On", "peq7On", "peq8On" };
    inline constexpr const char* peqType[8]  { "peq1Type", "peq2Type", "peq3Type", "peq4Type", "peq5Type", "peq6Type", "peq7Type", "peq8Type" };
    inline constexpr const char* peqFreq[8]  { "peq1Freq", "peq2Freq", "peq3Freq", "peq4Freq", "peq5Freq", "peq6Freq", "peq7Freq", "peq8Freq" };
    inline constexpr const char* peqGain[8]  { "peq1Gain", "peq2Gain", "peq3Gain", "peq4Gain", "peq5Gain", "peq6Gain", "peq7Gain", "peq8Gain" };
    inline constexpr const char* peqQ[8]     { "peq1Q", "peq2Q", "peq3Q", "peq4Q", "peq5Q", "peq6Q", "peq7Q", "peq8Q" };
    inline constexpr const char* peqSlope[8] { "peq1Slope", "peq2Slope", "peq3Slope", "peq4Slope", "peq5Slope", "peq6Slope", "peq7Slope", "peq8Slope" };
    inline constexpr const char* peqDyn[8]   { "peq1Dyn", "peq2Dyn", "peq3Dyn", "peq4Dyn", "peq5Dyn", "peq6Dyn", "peq7Dyn", "peq8Dyn" };
    inline constexpr const char* peqThr[8]   { "peq1Thresh", "peq2Thresh", "peq3Thresh", "peq4Thresh", "peq5Thresh", "peq6Thresh", "peq7Thresh", "peq8Thresh" };
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

// ---- ordre de traitement des 7 modules : EQ (paramétrique), RESONANCE, COMP, MULTIBAND, EQ ANALOG, DE-ESSER, SATURATION.
//      Une permutation, rangée dans un seul paramètre entier 0..5039 (code de Lehmer, 0 = ordre d'origine :
//      l'EQ paramétrique nettoie en premier, l'EQ ANALOG colore juste après la compression multibande).
constexpr int kOrderModules = 7;
constexpr int kMaxOrderIndex = 5039;
inline const char* orderName (int module)
{
    static const char* n[kOrderModules] { "EQ", "RESO", "COMP", "MULTI", "ANALOG", "DE-ESS", "SAT" };
    return n[juce::jlimit (0, kOrderModules - 1, module)];
}

template <int N>
inline std::array<int, N> factorials()
{
    std::array<int, N> fact {};
    fact[(size_t) N - 1] = 1;
    for (int i = N - 2; i >= 0; --i) fact[(size_t) i] = fact[(size_t) i + 1] * (N - 1 - i);
    return fact;
}

template <int N>
inline std::array<int, N> decodePermutation (int index)
{
    const auto fact = factorials<N>();
    std::vector<int> pool;
    for (int i = 0; i < N; ++i) pool.push_back (i);
    std::array<int, N> out {};
    index = juce::jlimit (0, fact[0] * N - 1, index);
    for (int i = 0; i < N; ++i)
    {
        const int k = index / fact[(size_t) i];
        index %= fact[(size_t) i];
        out[(size_t) i] = pool[(size_t) k];
        pool.erase (pool.begin() + k);
    }
    return out;
}

template <int N>
inline int encodePermutation (const std::array<int, N>& order)
{
    const auto fact = factorials<N>();
    std::vector<int> pool;
    for (int i = 0; i < N; ++i) pool.push_back (i);
    int index = 0;
    for (int i = 0; i < N; ++i)
    {
        const auto it = std::find (pool.begin(), pool.end(), order[(size_t) i]);
        if (it == pool.end()) return 0;
        index += (int) (it - pool.begin()) * fact[(size_t) i];
        pool.erase (it);
    }
    return index;
}

inline std::array<int, kOrderModules> decodeOrder (int index) { return decodePermutation<kOrderModules> (index); }
inline int encodeOrder (const std::array<int, kOrderModules>& order) { return encodePermutation<kOrderModules> (order); }

// ancien ordre (6 modules : EQ, RESO, COMP, MULTI, DE-ESS, SAT) -> nouvel ordre : l'ancien EQ devient l'EQ paramétrique
// (même place) et l'EQ ANALOG s'insère juste après le MULTIBAND
inline int convertLegacyOrder (int legacyIndex)
{
    static constexpr int remap[6] { 0, 1, 2, 3, 5, 6 };
    std::array<int, kOrderModules> out {};
    size_t k = 0;
    for (int m : decodePermutation<6> (legacyIndex))
    {
        out[k++] = remap[m];
        if (m == 3) out[k++] = 4;
    }
    return encodeOrder (out);
}

// DELAY calé sur le tempo : valeurs de note (en noires) du sélecteur
inline const juce::StringArray& delayNoteNames()
{
    static const juce::StringArray n { "1/2", "1/4", "1/4 D", "1/4 T", "1/8", "1/8 D", "1/8 T", "1/16", "1/16 D", "1/16 T" };
    return n;
}
inline float delayNoteBeats (int index)
{
    static constexpr float b[] { 2.0f, 1.0f, 1.5f, 2.0f / 3.0f, 0.5f, 0.75f, 1.0f / 3.0f, 0.25f, 0.375f, 1.0f / 6.0f };
    return b[juce::jlimit (0, 9, index)];
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

    // second de-esser : ajouté par le bouton + de la carte, plus haut dans les aigus par défaut
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::deess2On, 1 }, "De-Esser 2 On", false));
    p.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::deess2Freq, 1 }, "De-Ess 2 Freq",
        freqRange, 7500.0f,
        Attr().withLabel ("Hz").withStringFromValueFunction ([] (float v, int)
                                                             { return String (juce::roundToInt (v)) + " Hz"; })));
    p.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::deess2Amt, 1 }, "De-Ess 2 Amount",
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
    p.push_back (std::make_unique<AudioParameterInt> (ParameterID { ids::chainOrder, 1 }, "Processing Order", 0, kMaxOrderIndex, 0,
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

    // compression parallèle : AUTO = réglage d'usine (1176 ALL, INPUT +16 dB, attaque ~37 us, relâchement ~65 ms) ;
    // AUTO coupé, ces trois réglages reprennent la main. Leurs valeurs par défaut sont exactement celles d'AUTO.
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::parAuto, 1 }, "Parallel Compression Auto", true));
    p.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::parThresh, 1 }, "Parallel Compression Threshold", NormalisableRange<float> (-57.0f, -9.0f, 0.1f), -37.0f,
        Attr().withLabel ("dB").withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })));
    p.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::parAttack, 1 }, "Parallel Compression Attack", NormalisableRange<float> (1.0f, 7.0f, 0.01f), 6.0f,
        Attr().withStringFromValueFunction ([timeText] (float v, int)
        { return timeText (800.0e-6f * std::pow (20.0f / 800.0f, (v - 1.0f) / 6.0f)); })));
    p.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::parRelease, 1 }, "Parallel Compression Release", NormalisableRange<float> (1.0f, 7.0f, 0.01f), 6.5f,
        Attr().withStringFromValueFunction ([timeText] (float v, int)
        { return timeText (1.1f * std::pow (0.05f / 1.1f, (v - 1.0f) / 6.0f)); })));

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

    // ---- NOISE SUPPRESSOR : un seul curseur, 0 = coupé
    {
        auto pct = [] (float v, int) { return String (juce::roundToInt (v)) + " %"; };
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::nsAmount, 1 }, "Noise Suppressor",
            NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, Attr().withLabel ("%").withStringFromValueFunction (pct)));

        // écoute d'une bande de l'EQ paramétrique (session : ni presets, ni automation)
        p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::peqListen, 1 }, "Param EQ Listen", false,
                                                           AudioParameterBoolAttributes().withAutomatable (false)));
        p.push_back (std::make_unique<AudioParameterInt> (ParameterID { ids::peqListenBand, 1 }, "Param EQ Listen Band", 0, 7, 2,
                                                          AudioParameterIntAttributes().withAutomatable (false)));

        // ---- FX : tous coupés par défaut
        auto hzT = [] (float v, int) { return v >= 1000.0f ? String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : String (juce::roundToInt (v)) + " Hz"; };
        auto msT = [] (float v, int) { return v >= 1000.0f ? String (v / 1000.0f, 2) + " s" : String (juce::roundToInt (v)) + " ms"; };
        auto addB = [&] (const char* id, const char* name, bool def) { p.push_back (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def)); };
        auto addF = [&] (const char* id, const char* name, NormalisableRange<float> r, float def, const char* label,
                         std::function<String (float, int)> txt)
        {
            p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def,
                                                                Attr().withLabel (label).withStringFromValueFunction (std::move (txt))));
        };
        auto skewed = [] (float lo, float hi, float step, float centre) { NormalisableRange<float> r (lo, hi, step); r.setSkewForCentre (centre); return r; };
        auto rateT = [] (float v, int) { return String (v, v < 1.0f ? 2 : 1) + " Hz"; };

        addB (ids::chorusOn, "Chorus On", false);
        addF (ids::chorusRate, "Chorus Rate", skewed (0.05f, 4.0f, 0.01f, 0.8f), 0.6f, "Hz", rateT);
        addF (ids::chorusDepth, "Chorus Depth", { 0.0f, 100.0f, 0.1f }, 50.0f, "%", pct);
        addF (ids::chorusMix, "Chorus Mix", { 0.0f, 100.0f, 0.1f }, 35.0f, "%", pct);

        addB (ids::flangerOn, "Flanger On", false);
        addF (ids::flangerRate, "Flanger Rate", skewed (0.02f, 5.0f, 0.01f, 0.5f), 0.25f, "Hz", rateT);
        addF (ids::flangerDepth, "Flanger Depth", { 0.0f, 100.0f, 0.1f }, 60.0f, "%", pct);
        addF (ids::flangerFeedback, "Flanger Feedback", { -95.0f, 95.0f, 0.1f }, 40.0f, "%", pct);
        addF (ids::flangerMix, "Flanger Mix", { 0.0f, 100.0f, 0.1f }, 50.0f, "%", pct);

        addB (ids::delayOn, "Delay On", false);
        addB (ids::delaySync, "Delay Sync", true);
        addF (ids::delayTime, "Delay Time", skewed (10.0f, 2000.0f, 1.0f, 300.0f), 375.0f, "ms", msT);
        p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::delayNote, 1 }, "Delay Note", delayNoteNames(), 4));
        addF (ids::delayFeedback, "Delay Feedback", { 0.0f, 95.0f, 0.1f }, 35.0f, "%", pct);
        addF (ids::delayMix, "Delay Mix", { 0.0f, 100.0f, 0.1f }, 25.0f, "%", pct);
        addB (ids::delayPingPong, "Delay Ping-Pong", true);
        addF (ids::delayLowCut, "Delay Low Cut", skewed (20.0f, 2000.0f, 1.0f, 250.0f), 250.0f, "Hz", hzT);
        addF (ids::delayHighCut, "Delay High Cut", skewed (1000.0f, 20000.0f, 1.0f, 6000.0f), 6000.0f, "Hz", hzT);
        addF (ids::delayDucking, "Delay Ducking", { 0.0f, 100.0f, 0.1f }, 0.0f, "%", pct);

        addB (ids::reverbOn, "Reverb On", false);
        p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::reverbMode, 1 }, "Reverb Mode",
                                                             StringArray { "Room", "Plate 1", "Plate 2", "Hall", "Shimmer" }, 1));
        addF (ids::reverbPreDelay, "Reverb Pre-Delay", skewed (0.0f, 250.0f, 0.1f, 40.0f), 20.0f, "ms", msT);
        addF (ids::reverbDecay, "Reverb Decay", skewed (0.2f, 20.0f, 0.01f, 2.5f), 2.2f, "s",
              [] (float v, int) { return String (v, v < 10.0f ? 2 : 1) + " s"; });
        addF (ids::reverbSize, "Reverb Size", { 0.0f, 100.0f, 0.1f }, 60.0f, "%", pct);
        addF (ids::reverbDiffusion, "Reverb Diffusion", { 0.0f, 100.0f, 0.1f }, 75.0f, "%", pct);
        addF (ids::reverbModRate, "Reverb Mod Rate", skewed (0.05f, 5.0f, 0.01f, 0.8f), 0.8f, "Hz", rateT);
        addF (ids::reverbModDepth, "Reverb Mod Depth", { 0.0f, 100.0f, 0.1f }, 35.0f, "%", pct);
        addF (ids::reverbMix, "Reverb Mix", { 0.0f, 100.0f, 0.1f }, 20.0f, "%", pct);
        addF (ids::reverbLowCut, "Reverb Low Cut", skewed (20.0f, 1000.0f, 1.0f, 200.0f), 180.0f, "Hz", hzT);
        addF (ids::reverbHighCut, "Reverb High Cut", skewed (1000.0f, 20000.0f, 1.0f, 7000.0f), 9000.0f, "Hz", hzT);
        addF (ids::reverbDucking, "Reverb Ducking", { 0.0f, 100.0f, 0.1f }, 0.0f, "%", pct);
    }

    // ---- NOISE GATE : coupé par défaut (les anciens projets ne changent pas) ; réglages d'usine du BROADCAST
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::gateOn, 1 }, "Gate On", false));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::gateThr, 1 }, "Gate Threshold",
        NormalisableRange<float> (-80.0f, 0.0f, 0.1f), -48.0f, Attr().withLabel ("dB").withStringFromValueFunction (dbText)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::gateRange, 1 }, "Gate Range",
        NormalisableRange<float> (0.0f, 80.0f, 0.1f), 24.0f,
        Attr().withLabel ("dB").withStringFromValueFunction ([] (float v, int) { return v >= 79.5f ? String ("MUTE") : "-" + String (v, 1) + " dB"; })));
    {
        auto msText = [] (float v, int) { return v >= 1000.0f ? String (v / 1000.0f, 2) + " s" : String (v, v < 10.0f ? 1 : 0) + " ms"; };
        NormalisableRange<float> atk (0.1f, 50.0f, 0.01f), hold (0.0f, 500.0f, 1.0f), rel (10.0f, 2000.0f, 1.0f);
        atk.setSkewForCentre (3.0f);
        hold.setSkewForCentre (100.0f);
        rel.setSkewForCentre (200.0f);
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::gateAttack, 1 }, "Gate Attack", atk, 1.5f,
                                                            Attr().withLabel ("ms").withStringFromValueFunction (msText)));
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::gateHold, 1 }, "Gate Hold", hold, 120.0f,
                                                            Attr().withLabel ("ms").withStringFromValueFunction (msText)));
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::gateRelease, 1 }, "Gate Release", rel, 220.0f,
                                                            Attr().withLabel ("ms").withStringFromValueFunction (msText)));
    }
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::gateHyst, 1 }, "Gate Hysteresis",
        NormalisableRange<float> (0.0f, 12.0f, 0.1f), 5.0f, Attr().withLabel ("dB").withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })));

    // ---- EQ PARAMÉTRIQUE : 8 bandes, toutes coupées par défaut (positions de départ utiles pour la voix)
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::peqPower, 1 }, "Parametric EQ On", true));
    {
        auto hz = [] (float v, int) { return v >= 1000.0f ? String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : String (juce::roundToInt (v)) + " Hz"; };
        auto qText = [] (float v, int) { return String (v, 2); };
        const int defType[8] { 3, 1, 0, 0, 0, 2, 0, 0 };                   // LOW CUT, LOW SHELF, BELL x3, HIGH SHELF, BELL x2
        const float defFreq[8] { 80.0f, 150.0f, 400.0f, 2000.0f, 5000.0f, 10000.0f, 250.0f, 1000.0f };
        const float defQ[8] { 0.71f, 0.71f, 1.0f, 1.0f, 1.0f, 0.71f, 1.0f, 1.0f };
        for (int b = 0; b < 8; ++b)
        {
            const String n = "Param EQ " + String (b + 1) + " ";
            p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::peqOn[b], 1 }, n + "On", false));
            p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::peqType[b], 1 }, n + "Type",
                StringArray { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch" }, defType[b]));
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
            // pente des coupe-bas / coupe-haut ; mode dynamique (cloche, shelfs, notch) et son seuil
            p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::peqSlope[b], 1 }, n + "Slope",
                StringArray { "12 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct" }, 0));
            p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::peqDyn[b], 1 }, n + "Dynamic", false));
            p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::peqThr[b], 1 }, n + "Threshold",
                NormalisableRange<float> (-60.0f, 0.0f, 0.1f), -30.0f,
                Attr().withLabel ("dB").withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })));
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
    for (auto [id, name] : { Switch { ids::eqOn, "EQ Analog On" }, Switch { ids::resoOn, "Resonance On" },
                             Switch { ids::deessOn, "De-Esser On" }, Switch { ids::satOn, "Saturation On" } })
        p.push_back (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, true));

    return { p.begin(), p.end() };
}
} // namespace onirique
