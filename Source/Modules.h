#pragma once

#include "Parameters.h"

namespace onirique
{
/*  Chaîne modulaire de VOCALBOX : le catalogue des modules.

    La chaîne tient dans les 16 réglages « slot1 » .. « slot16 » (0 = emplacement vide, sinon le code du module, qui ne
    change jamais : type * kInstances + exemplaire + 1). Ainsi un preset, la comparaison A/B, copier / coller et le projet
    du DAW emportent la chaîne avec eux, sans rien de plus. Chaque type de module existe en kInstances exemplaires : le
    premier garde les réglages d'origine (identifiants des versions précédentes), les suivants des réglages suffixés.
*/
namespace modules
{
enum Type
{
    gate = 0, noise, plosive, reso, deess, peq, analog, sat, fet, opto, multi, parallel,
    doubler, chorus, flanger, phone, delay, reverb, creative,
    preamp, cl1b,   // ajoutés à la fin : les codes des anciennes chaînes ne changent pas
    tuner, declick, debreath, dereverb,
    numTypes
};

// où tourne le module : à la cadence de l'hôte (analyse FFT, effets) ou dans la chaîne suréchantillonnée
enum Domain { host = 0, oversampled };

struct Info
{
    const char* shortName;   // sur le carré de la chaîne
    const char* name;        // en titre du module
    const char* desc;        // dans le + (ce que fait le module)
    int group;               // 0 NETTOYAGE, 1 TON, 2 DYNAMIQUE, 3 EFFETS
    Domain domain;
    const char* onId;        // interrupteur (bouton power)
};

inline const Info& info (int type)
{
    static const Info table[numTypes] {
        { "GATE",     "NOISE GATE",       "Coupe le bruit entre les phrases",         0, host,        ids::gateOn },
        { "NOISE",    "NOISE SUPPRESSOR", "Souffle, ventilation, pièce",               0, host,        ids::nsOn },
        { "PLOSIVES", "PLOSIVES",         "Les P et les B",                            0, host       , ids::plosOn },
        { "RESO",     "RESONANCE",        "Résonances qui sifflent ou bourdonnent",    0, host       , ids::resoOn },
        { "DE-ESS",   "DE-ESSER",         "Les S et les CH",                           0, host       , ids::deessOn },
        { "EQ",       "PARAMETRIC EQ",    "8 bandes, mode dynamique, écoute",          1, oversampled, ids::peqPower },
        { "ANALOG",   "EQ ANALOG",        "Couleur de console : LOW, MUD, HI, AIR",    1, oversampled, ids::eqOn },
        { "SAT",      "SATURATION LAB",   "Tube, analog, tape, transformer",      1, oversampled, ids::satOn },
        { "1176",     "1176 FET",         "Compresseur FET, attaque rapide",           2, oversampled, ids::fetOn },
        { "LA-2A",    "LA-2A OPTO",       "Nivellement optique, doux",                 2, oversampled, ids::optoOn },
        { "MULTI",    "MULTIBAND",        "Quatre bandes optiques",                    2, oversampled, ids::mbOn },
        { "PARALLEL", "PARALLEL",         "1176 écrasé remis dans la voix",            2, oversampled, ids::parOn },
        { "DOUBLER",  "DOUBLER",          "Une vraie double voix",                     3, host,        ids::doublerOn },
        { "CHORUS",   "CHORUS",           "Voix qui ondule, plus large",               3, host,        ids::chorusOn },
        { "FLANGER",  "FLANGER",          "Le souffle d'un avion qui passe",           3, host,        ids::flangerOn },
        { "PHONE",    "PHONE",            "Voix de téléphone, de radio",               3, host,        ids::phoneOn },
        { "DELAY",    "DELAY",            "Échos au tempo, ping-pong, ducking",        3, host,        ids::delayOn },
        { "REVERB",   "REVERB",           "Room, plates, hall, shimmer",               3, host,        ids::reverbOn },
        { "CREATIVE", "CREATIVE FX",      "Doubleur, chorus, flanger, phone",          3, host,        ids::creativeOn },
        { "PREAMP",   "PREAMP N73",       "Préampli micro ou ligne, grain de console", 1, oversampled, ids::preampOn },
        { "CL1B",     "CL1B OPTO",        "Optique à lampe : crêtes tenues en douceur", 2, oversampled, ids::cl1bOn },
        { "TUNER",    "ONIRIQUE TUNER",   "Justesse naturelle ou effet robot",         1, host,        ids::tuneOn },
        { "DE-CLICK", "DE-CLICK",         "Clics de bouche, salive, lèvres",           0, host,        ids::dclkOn },
        { "BREATH",   "DE-BREATH",        "Respirations baissées, pas coupées",        0, host,        ids::dbrOn },
        { "DE-VERB",  "DE-REVERB",        "La pièce retirée de la voix",               0, host,        ids::drvOn },
    };
    return table[juce::jlimit (0, (int) numTypes - 1, type)];
}

inline const char* groupName (int g)
{
    static const char* n[4] { "NETTOYAGE", "TON", "DYNAMIQUE", "EFFETS" };
    return n[juce::jlimit (0, 3, g)];
}

// tous les réglages d'un module (identifiants du premier exemplaire), interrupteur compris
inline const std::vector<juce::String>& paramIds (int type)
{
    static const auto table = [] {
        std::array<std::vector<juce::String>, numTypes> t;
        auto add = [&t] (int ty, std::initializer_list<const char*> l) { for (auto* s : l) t[(size_t) ty].push_back (s); };
        add (gate, { ids::gateOn, ids::gateThr, ids::gateRange, ids::gateAttack, ids::gateHold, ids::gateRelease, ids::gateHyst });
        add (noise, { ids::nsOn, ids::nsAmount });
        add (plosive, { ids::plosOn, ids::plosive, ids::plosListen });
        add (reso, { ids::resoOn, ids::resoAmt, ids::resoLow, ids::resoMid, ids::resoHigh, ids::resoLowFreq, ids::resoMidFreq,
                     ids::resoHighFreq, ids::resoLowWidth, ids::resoMidWidth, ids::resoHighWidth, ids::resoListen });
        add (deess, { ids::deessOn, ids::deessFreq, ids::deessAmt, ids::deessS, ids::deessT, ids::deessCH, ids::deessF, ids::deessClarity });
        add (peq, { ids::peqPower, ids::peqListen, ids::peqListenBand });
        for (int b = 0; b < 8; ++b)
            add (peq, { ids::peqOn[b], ids::peqType[b], ids::peqFreq[b], ids::peqGain[b], ids::peqQ[b], ids::peqSlope[b], ids::peqDyn[b], ids::peqThr[b] });
        add (analog, { ids::eqOn, ids::low, ids::mud, ids::high, ids::air, ids::lowFreq, ids::lowCut });
        add (sat, { ids::satOn, ids::drive, ids::satType, ids::satMix, ids::satOutput, ids::satTone, ids::satWarmth, ids::satAir, ids::satBias, ids::satDynamics });
        add (fet, { ids::fetOn, ids::fetIn, ids::fetOut, ids::fetAttack, ids::fetRelease, ids::fetRatio });
        add (opto, { ids::optoOn, ids::laGain, ids::laPeak, ids::laLimit });
        add (multi, { ids::mbOn, ids::mbX[0], ids::mbX[1], ids::mbX[2], ids::mbAttack, ids::mbRelease, ids::mbOutput });
        for (int b = 0; b < 4; ++b) add (multi, { ids::mbThr[b], ids::mbGain[b], ids::mbSolo[b] });
        add (parallel, { ids::parOn, ids::parAmount, ids::parAuto, ids::parThresh, ids::parAttack, ids::parRelease, ids::parDeessAmt, ids::parDeessFreq });
        add (doubler, { ids::doublerOn, ids::doublerAmount });
        add (chorus, { ids::chorusOn, ids::chorusRate, ids::chorusDepth, ids::chorusMix });
        add (flanger, { ids::flangerOn, ids::flangerRate, ids::flangerDepth, ids::flangerFeedback, ids::flangerMix });
        add (phone, { ids::phoneOn, ids::phoneAmount });
        add (delay, { ids::delayOn, ids::delaySync, ids::delayTime, ids::delayNote, ids::delayFeedback, ids::delayMix, ids::delayPingPong,
                      ids::delayLowCut, ids::delayHighCut, ids::delayDucking });
        add (reverb, { ids::reverbOn, ids::reverbMode, ids::reverbPreDelay, ids::reverbDecay, ids::reverbSize, ids::reverbDiffusion,
                       ids::reverbModRate, ids::reverbModDepth, ids::reverbMix, ids::reverbLowCut, ids::reverbHighCut, ids::reverbDucking,
                       ids::reverbWidth, ids::reverbClean });
        add (creative, { ids::creativeOn, ids::doublerOn, ids::doublerAmount, ids::chorusOn, ids::chorusRate, ids::chorusDepth, ids::chorusMix,
                         ids::flangerOn, ids::flangerRate, ids::flangerDepth, ids::flangerFeedback, ids::flangerMix, ids::phoneOn, ids::phoneAmount });
        add (preamp, { ids::preampOn, ids::preampMic, ids::preampGain, ids::preampOutput });
        add (cl1b, { ids::cl1bOn, ids::cl1bThreshold, ids::cl1bRatio, ids::cl1bAttack, ids::cl1bRelease, ids::cl1bGain, ids::cl1bMix });
        add (tuner, { ids::tuneOn, ids::tuneKey, ids::tuneScale, ids::tuneRetune, ids::tuneHumanize, ids::tuneFlex, ids::tuneVibrato,
                      ids::tuneFormant, ids::tuneVoice, ids::tuneHard, ids::tuneMix });
        add (declick, { ids::dclkOn, ids::dclkSens, ids::dclkAmount, ids::dclkListen });
        add (debreath, { ids::dbrOn, ids::dbrReduction, ids::dbrSens, ids::dbrListen });
        add (dereverb, { ids::drvOn, ids::drvAmount, ids::drvTail, ids::drvAuto, ids::drvListen });
        return t;
    }();
    return table[(size_t) juce::jlimit (0, (int) numTypes - 1, type)];
}

// code d'un emplacement : 0 = vide
inline int code (int type, int instance) noexcept { return type * kInstances + instance + 1; }
inline int typeOf (int c) noexcept { return c > 0 ? (c - 1) / kInstances : -1; }
inline int instanceOf (int c) noexcept { return c > 0 ? (c - 1) % kInstances : -1; }
inline bool valid (int c) noexcept { return c > 0 && c <= numTypes * kInstances; }

// les effets que CREATIVE FX regroupe : ils ne se proposent plus seuls dans le +, mais les anciens projets qui les ont les gardent
inline bool inCreative (int type) noexcept { return type == doubler || type == chorus || type == flanger || type == phone; }
inline bool inPicker (int type) noexcept { return ! inCreative (type); }

using Chain = std::array<int, kSlots>;

/*  Chaîne des projets et presets d'avant la refonte : elle se déduit de l'ancien ordre et des interrupteurs. Seuls les
    modules qui faisaient vraiment quelque chose y entrent (un module coupé ou à zéro ne changeait pas le son).
    get (id, valeur par défaut) lit un réglage de l'ancien état. set (id, valeur) range les nouveaux interrupteurs. */
template <typename Get, typename Set>
inline Chain legacyChain (Get&& get, Set&& set)
{
    Chain c {};
    size_t n = 0;
    auto push = [&] (int type, int inst = 0) { if (n < c.size()) c[n++] = code (type, inst); };
    auto on = [&] (const char* id, bool def) { return get (id, def ? 1.0f : 0.0f) > 0.5f; };

    const bool master = on (ids::compMaster, true);
    const int mode = juce::roundToInt (get (ids::compMode, 0.0f));
    const bool comp = on (ids::compOn, false) && master;
    const bool fetUsed = comp && (mode == 0 || mode == 2), optoUsed = comp && (mode == 1 || mode == 2);
    const bool deessOn = on (ids::deessOn, true);
    const bool plosUsed = deessOn && get (ids::plosive, 0.0f) > 0.0f;
    const bool deessUsed = deessOn && get (ids::deessAmt, 0.0f) > 0.0f;
    const bool deess2Used = deessOn && on (ids::deess2On, false) && get (ids::deess2Amt, 0.0f) > 0.0f;
    bool peqUsed = false;
    if (on (ids::peqPower, true))
        for (int b = 0; b < 8; ++b) peqUsed = peqUsed || on (ids::peqOn[b], false);

    // les nouveaux interrupteurs reprennent ce qu'on entendait
    set (ids::nsOn, get (ids::nsAmount, 0.0f) > 0.0f ? 1.0f : 0.0f);
    set (ids::plosOn, plosUsed ? 1.0f : 0.0f);
    set (ids::fetOn, fetUsed ? 1.0f : 0.0f);
    set (ids::optoOn, optoUsed ? 1.0f : 0.0f);
    set (ids::mbOn, on (ids::mbOn, false) && master ? 1.0f : 0.0f);
    if (deess2Used)   // le second de-esser devient le DE-ESSER 2
    {
        set (ids::inst (ids::deessOn, 1).toRawUTF8(), 1.0f);
        set (ids::inst (ids::deessFreq, 1).toRawUTF8(), get (ids::deess2Freq, 7500.0f));
        set (ids::inst (ids::deessAmt, 1).toRawUTF8(), get (ids::deess2Amt, 0.0f));
    }

    if (get (ids::nsAmount, 0.0f) > 0.0f) push (noise);
    if (on (ids::gateOn, false)) push (gate);

    int order = juce::roundToInt (get (ids::chainOrder, 0.0f));
    for (int m : decodeOrder (order))
    {
        switch (m)
        {
            case 0: if (peqUsed) push (peq); break;
            case 1: if (on (ids::resoOn, true) && get (ids::resoAmt, 0.0f) > 0.0f) push (reso); break;
            case 2:
                if (fetUsed && optoUsed)
                {
                    if (juce::roundToInt (get (ids::compOrder, 0.0f)) == 0) { push (fet); push (opto); }
                    else { push (opto); push (fet); }
                }
                else if (fetUsed) push (fet);
                else if (optoUsed) push (opto);
                if (on (ids::parOn, false)) push (parallel);
                break;
            case 3: if (on (ids::mbOn, false) && master) push (multi); break;
            case 4:
                if (on (ids::eqOn, true)
                    && (get (ids::low, 0.0f) != 0.0f || get (ids::mud, 0.0f) != 0.0f || get (ids::high, 0.0f) != 0.0f
                        || get (ids::air, 0.0f) != 0.0f || on (ids::lowCut, false)))
                    push (analog);
                break;
            case 5:
                if (plosUsed) push (plosive);
                if (deessUsed) push (deess);
                if (deess2Used) push (deess, 1);
                break;
            default: if (on (ids::satOn, true) && get (ids::drive, 0.0f) > 0.0f) push (sat); break;
        }
    }

    // la page FX, dans son ordre : PHONE, DOUBLER, CHORUS, FLANGER, puis DELAY et REVERB (en parallèle, voir ModularChain)
    if (on (ids::phoneOn, false)) push (phone);
    if (on (ids::doublerOn, false)) push (doubler);
    if (on (ids::chorusOn, false)) push (chorus);
    if (on (ids::flangerOn, false)) push (flanger);
    if (on (ids::delayOn, false)) push (delay);
    if (on (ids::reverbOn, false)) push (reverb);
    return c;
}
} // namespace modules
} // namespace onirique
