#pragma once

#include "Parameters.h"
#include "ui/OniriquePresets.h"

namespace onirique
{
/*  Presets d'usine de VOCALBOX, rangés par usage pour le navigateur de la barre (les tags). Valeurs en unités réelles
    (celles affichées) ; un réglage non cité reprend sa valeur par défaut (« Init » : tous les modules à plat).
    low / mud / drive 0..10, high / air -10..+10, le reste en % sauf : lowFreq 0 80 Hz, 1 100 Hz, 2 125 Hz ;
    compMode 0 1176, 1 LA-2A, 2 DUO ; fetInput en dB ; fetAttack / fetRelease 1 (lent) .. 7 (rapide) ;
    fetRatio 0 4, 1 8, 2 12, 3 20, 4 ALL ; satType 0 TUBE, 1 ANALOG, 2 TAPE ;
    fxReverbMode 0 ROOM, 1 PLATE 1, 2 PLATE 2, 3 HALL, 4 SHIMMER ; fxReverbDecay en s, fxReverbPreDelay en ms ;
    fxDelayNote 0 1/2, 1 1/4, 2 1/4 D, 3 1/4 T, 4 1/8, 5 1/8 D, 6 1/8 T, 7 1/16 ; gateThreshold / gateRange en dB.
    Ce sont des points de départ : aucun ne pousse tout au maximum ; AUTO GAIN garde le volume d'un preset à l'autre. */
namespace vocalbox
{
enum Tags { lead = 1, rap = 2, pop = 4, rnb = 8, backs = 16, effects = 32, cleanup = 64, spoken = 128 };

inline juce::StringArray tagNames() { return { "Lead", "Rap", "Pop", "R&B", "Backs & Ad-libs", "Effects", "Clean-up", "Spoken" }; }

/** Les réglages de session (ce qu'on écoute, la latence) : un preset les laisse. */
inline juce::StringArray notStored()
{
    juce::StringArray s { ids::bypass, ids::oversampling, ids::resoListen, ids::peqListen, ids::peqListenBand };
    for (auto* id : ids::mbSolo) s.add (id);
    return s;
}

inline std::vector<PresetBank::Stock> stockPresets()
{
    return {
        { "Init", 0, {} },
        { "Clean Pop Vocal", lead | pop,
          { { "lowCut", 1 }, { "low", 3 }, { "mud", 4 }, { "high", 3 }, { "air", 3 }, { "deessAmt", 30 }, { "resoAmount", 35 },
            { "compOn", 1 }, { "compMode", 2 }, { "fetInput", 6 }, { "fetAttack", 3 }, { "fetRelease", 5 }, { "laPeakReduction", 35 },
            { "drive", 2 }, { "autoGain", 1 } } },
        { "Rap Lead", lead | rap,
          { { "lowCut", 1 }, { "low", 5 }, { "lowFreq", 1 }, { "mud", 5 }, { "high", 3 }, { "air", 2 }, { "deessAmt", 35 },
            { "resoAmount", 40 }, { "compOn", 1 }, { "compMode", 0 }, { "fetInput", 12 }, { "fetRatio", 1 }, { "fetAttack", 5 },
            { "fetRelease", 6 }, { "satType", 1 }, { "drive", 3.5f }, { "autoGain", 1 } } },
        { "Modern Rap Upfront", lead | rap,
          { { "lowCut", 1 }, { "plosive", 40 }, { "low", 3 }, { "mud", 5 }, { "high", 3 }, { "air", 3 }, { "deessAmt", 40 },
            { "resoAmount", 45 }, { "compOn", 1 }, { "compMode", 2 }, { "fetInput", 10 }, { "fetAttack", 5 }, { "fetRelease", 6 },
            { "laPeakReduction", 35 }, { "parallelOn", 1 }, { "parallelAmount", 35 }, { "satType", 1 }, { "drive", 3 },
            { "autoGain", 1 }, { "fxReverbOn", 1 }, { "fxReverbMode", 0 }, { "fxReverbMix", 10 }, { "fxReverbDecay", 0.6f },
            { "fxReverbPreDelay", 5 } } },
        { "Melodic Trap", rap | pop,
          { { "lowCut", 1 }, { "low", 2 }, { "mud", 4 }, { "high", 2.5f }, { "air", 4.5f }, { "deessAmt", 40 }, { "resoAmount", 40 },
            { "compOn", 1 }, { "compMode", 2 }, { "fetInput", 8 }, { "fetAttack", 4 }, { "fetRelease", 5 }, { "laPeakReduction", 40 },
            { "drive", 2 }, { "autoGain", 1 }, { "fxDoublerOn", 1 }, { "fxDoublerAmount", 25 }, { "fxDelayOn", 1 }, { "fxDelayNote", 1 },
            { "fxDelayFeedback", 35 }, { "fxDelayMix", 18 }, { "fxDelayDucking", 40 }, { "fxReverbOn", 1 }, { "fxReverbMode", 2 },
            { "fxReverbMix", 18 }, { "fxReverbDecay", 1.8f }, { "fxReverbPreDelay", 25 }, { "fxReverbClean", 60 },
            { "fxReverbDucking", 35 } } },
        { "Big Parallel Lead", lead | rap | pop,
          { { "lowCut", 1 }, { "low", 2 }, { "mud", 4 }, { "high", 2.5f }, { "air", 3 }, { "deessAmt", 40 }, { "resoAmount", 40 },
            { "compOn", 1 }, { "compMode", 0 }, { "fetInput", 8 }, { "fetAttack", 4 }, { "fetRelease", 5 }, { "parallelOn", 1 },
            { "parallelAmount", 55 }, { "parallelDeEssAmount", 40 }, { "autoGain", 1 } } },
        { "Airy Ballad", lead | pop | rnb,
          { { "lowCut", 1 }, { "low", 2 }, { "mud", 3 }, { "high", 3 }, { "air", 6 }, { "deessAmt", 25 }, { "resoAmount", 30 },
            { "compOn", 1 }, { "compMode", 1 }, { "laPeakReduction", 45 }, { "satType", 2 }, { "drive", 2 }, { "autoGain", 1 } } },
        { "Bright Pop Air", lead | pop,
          { { "lowCut", 1 }, { "low", 1 }, { "mud", 4 }, { "high", 3.5f }, { "air", 6 }, { "deessAmt", 45 }, { "deess2On", 1 },
            { "deess2Amt", 25 }, { "resoAmount", 40 }, { "compOn", 1 }, { "compMode", 2 }, { "fetInput", 6 }, { "laPeakReduction", 40 },
            { "autoGain", 1 }, { "fxReverbOn", 1 }, { "fxReverbMode", 1 }, { "fxReverbMix", 16 }, { "fxReverbDecay", 1.6f },
            { "fxReverbPreDelay", 20 } } },
        { "Smooth Multiband", lead | pop,
          { { "lowCut", 1 }, { "mud", 3 }, { "high", 2 }, { "air", 3 }, { "deessAmt", 30 }, { "resoAmount", 35 }, { "mbOn", 1 },
            { "compOn", 1 }, { "compMode", 1 }, { "laPeakReduction", 35 }, { "autoGain", 1 } } },
        { "R&B Silk", lead | rnb,
          { { "lowCut", 1 }, { "low", 3 }, { "lowFreq", 1 }, { "mud", 3 }, { "high", 1.5f }, { "air", 5 }, { "deessAmt", 35 },
            { "resoAmount", 30 }, { "compOn", 1 }, { "compMode", 1 }, { "laPeakReduction", 45 }, { "satType", 2 }, { "drive", 2 },
            { "autoGain", 1 }, { "fxChorusOn", 1 }, { "fxChorusMix", 15 }, { "fxReverbOn", 1 }, { "fxReverbMode", 2 },
            { "fxReverbMix", 22 }, { "fxReverbDecay", 2.4f }, { "fxReverbPreDelay", 30 }, { "fxReverbClean", 50 } } },
        { "Warm Tape Vocal", lead | rnb,
          { { "low", 4 }, { "lowFreq", 2 }, { "mud", 3 }, { "high", 2 }, { "deessAmt", 20 }, { "compOn", 1 }, { "compMode", 1 },
            { "laPeakReduction", 40 }, { "satType", 2 }, { "drive", 5 }, { "autoGain", 1 } } },
        { "Intimate Close Mic", lead | rnb,
          { { "plosive", 50 }, { "low", 4 }, { "lowFreq", 0 }, { "mud", 4 }, { "high", 1 }, { "air", 2 }, { "deessAmt", 25 },
            { "resoAmount", 35 }, { "compOn", 1 }, { "compMode", 1 }, { "laPeakReduction", 30 }, { "satType", 2 }, { "drive", 2 },
            { "autoGain", 1 }, { "fxReverbOn", 1 }, { "fxReverbMode", 0 }, { "fxReverbMix", 15 }, { "fxReverbDecay", 0.6f } } },
        { "Aggressive Rock", lead,
          { { "lowCut", 1 }, { "low", 3 }, { "mud", 3 }, { "high", 4 }, { "air", 2 }, { "deessAmt", 30 }, { "compOn", 1 },
            { "compMode", 2 }, { "fetInput", 14 }, { "fetRatio", 4 }, { "fetAttack", 4 }, { "fetRelease", 6 }, { "laPeakReduction", 30 },
            { "satType", 1 }, { "drive", 6 }, { "autoGain", 1 } } },
        { "Dark Vintage", lead | effects,
          { { "low", 4 }, { "high", -2 }, { "air", -3 }, { "deessAmt", 20 }, { "compOn", 1 }, { "compMode", 1 },
            { "laPeakReduction", 50 }, { "satType", 2 }, { "drive", 5 }, { "autoGain", 1 }, { "fxReverbOn", 1 }, { "fxReverbMode", 2 },
            { "fxReverbMix", 15 }, { "fxReverbHighCut", 5000 } } },
        { "Wide Atmosphere", effects | pop,
          { { "lowCut", 1 }, { "mud", 3 }, { "high", 2 }, { "air", 5 }, { "deessAmt", 40 }, { "resoAmount", 35 }, { "compOn", 1 },
            { "compMode", 1 }, { "laPeakReduction", 40 }, { "autoGain", 1 }, { "fxDoublerOn", 1 }, { "fxDoublerAmount", 45 },
            { "fxChorusOn", 1 }, { "fxChorusMix", 25 }, { "fxDelayOn", 1 }, { "fxDelayNote", 2 }, { "fxDelayFeedback", 45 },
            { "fxDelayMix", 25 }, { "fxDelayDucking", 40 }, { "fxReverbOn", 1 }, { "fxReverbMode", 3 }, { "fxReverbMix", 28 },
            { "fxReverbDecay", 3.5f }, { "fxReverbPreDelay", 30 }, { "fxReverbClean", 60 }, { "fxReverbDucking", 40 },
            { "fxReverbWidth", 130 } } },
        { "Dream Shimmer Hook", effects | pop,
          { { "air", 5 }, { "deessAmt", 35 }, { "compOn", 1 }, { "compMode", 1 }, { "laPeakReduction", 40 }, { "autoGain", 1 },
            { "fxDoublerOn", 1 }, { "fxDoublerAmount", 40 }, { "fxDelayOn", 1 }, { "fxDelayNote", 2 }, { "fxDelayFeedback", 45 },
            { "fxDelayMix", 22 }, { "fxReverbOn", 1 }, { "fxReverbMode", 4 }, { "fxReverbMix", 30 }, { "fxReverbDecay", 4.5f },
            { "fxReverbPreDelay", 25 }, { "fxReverbClean", 50 } } },
        { "Radio Phone", effects,
          { { "lowCut", 1 }, { "compOn", 1 }, { "compMode", 0 }, { "fetInput", 10 }, { "satType", 1 }, { "drive", 4 },
            { "autoGain", 1 }, { "fxPhoneOn", 1 }, { "fxPhoneAmount", 75 }, { "fxReverbOn", 1 }, { "fxReverbMode", 0 },
            { "fxReverbMix", 12 }, { "fxReverbDecay", 0.45f } } },
        { "Ad-libs Wide", backs | rap,
          { { "lowCut", 1 }, { "mud", 5 }, { "high", 3 }, { "air", 3 }, { "deessAmt", 40 }, { "compOn", 1 }, { "compMode", 0 },
            { "fetInput", 10 }, { "autoGain", 1 }, { "fxDoublerOn", 1 }, { "fxDoublerAmount", 60 }, { "fxChorusOn", 1 },
            { "fxChorusMix", 25 }, { "fxDelayOn", 1 }, { "fxDelayNote", 4 }, { "fxDelayFeedback", 35 }, { "fxDelayMix", 30 },
            { "fxReverbOn", 1 }, { "fxReverbMix", 20 } } },
        { "Ad-libs Telephone", backs | effects,
          { { "lowCut", 1 }, { "compOn", 1 }, { "fetInput", 12 }, { "satType", 1 }, { "drive", 4 }, { "autoGain", 1 },
            { "fxPhoneOn", 1 }, { "fxPhoneAmount", 85 }, { "fxDoublerOn", 1 }, { "fxDoublerAmount", 30 }, { "fxDelayOn", 1 },
            { "fxDelayNote", 4 }, { "fxDelayFeedback", 30 }, { "fxDelayMix", 22 }, { "fxReverbOn", 1 }, { "fxReverbMode", 0 },
            { "fxReverbMix", 12 }, { "fxReverbDecay", 0.5f } } },
        { "Backs & Harmonies", backs | pop | rnb,
          { { "lowCut", 1 }, { "mud", 6 }, { "high", 1 }, { "air", 4 }, { "deessAmt", 45 }, { "resoAmount", 40 }, { "compOn", 1 },
            { "compMode", 1 }, { "laPeakReduction", 50 }, { "mbOn", 1 }, { "autoGain", 1 }, { "fxDoublerOn", 1 },
            { "fxDoublerAmount", 40 }, { "fxChorusOn", 1 }, { "fxChorusMix", 30 }, { "fxReverbOn", 1 }, { "fxReverbMode", 2 },
            { "fxReverbMix", 25 }, { "fxReverbDecay", 2.2f } } },
        { "Home Recording Cleanup", cleanup | lead,
          { { "lowCut", 1 }, { "plosive", 60 }, { "nsAmount", 50 }, { "gateOn", 1 }, { "gateThreshold", -50 }, { "gateRange", 15 },
            { "mud", 5 }, { "air", 2.5f }, { "deessAmt", 40 }, { "resoAmount", 50 }, { "compOn", 1 }, { "compMode", 1 },
            { "laPeakReduction", 35 }, { "autoGain", 1 } } },
        { "Podcast Voice", spoken | cleanup,
          { { "lowCut", 1 }, { "plosive", 50 }, { "nsAmount", 40 }, { "gateOn", 1 }, { "gateThreshold", -55 }, { "gateRange", 12 },
            { "low", 2 }, { "lowFreq", 1 }, { "mud", 3 }, { "high", 1.5f }, { "air", 1 }, { "deessAmt", 35 }, { "resoAmount", 35 },
            { "compOn", 1 }, { "compMode", 2 }, { "fetInput", 6 }, { "laPeakReduction", 45 }, { "autoGain", 1 } } },
        { "Voice-over Broadcast", spoken,
          { { "lowCut", 1 }, { "plosive", 45 }, { "nsAmount", 30 }, { "gateOn", 1 }, { "gateThreshold", -55 }, { "gateRange", 10 },
            { "low", 3 }, { "lowFreq", 1 }, { "mud", 3 }, { "high", 1.5f }, { "air", 1.5f }, { "deessAmt", 35 }, { "compOn", 1 },
            { "compMode", 2 }, { "fetInput", 8 }, { "laPeakReduction", 50 }, { "mbOn", 1 }, { "satType", 1 }, { "drive", 1.5f },
            { "autoGain", 1 } } },
    };
}

/** Les presets enregistrés avec l'ancienne barre (fichiers .ovbpreset du même dossier) passent dans le navigateur,
    une fois : chacun est recopié au format de la nouvelle barre, l'original reste en place. */
inline void importOldUserPresets (const PresetBank& bank)
{
    for (const auto& file : bank.getFolder().findChildFiles (juce::File::findFiles, false, "*.ovbpreset"))
    {
        const auto target = file.withFileExtension (bank.getExtension());
        if (target.existsAsFile())
            continue;

        if (const auto xml = juce::XmlDocument::parse (file))
        {
            std::map<juce::String, float> values;
            for (auto* p : xml->getChildWithTagNameIterator ("PARAM"))
                values[p->getStringAttribute ("id")] = (float) p->getDoubleAttribute ("value");

            // presets d'avant la séparation EQ paramétrique / EQ ANALOG (ordre à 6 modules, un seul interrupteur EQ)
            if (values.count (ids::chainOrder) == 0 && values.count (ids::legacyChainOrder) != 0)
                values[ids::chainOrder] = (float) convertLegacyOrder (juce::roundToInt (values[ids::legacyChainOrder]));
            if (values.count (ids::peqPower) == 0 && values.count (ids::eqOn) != 0)
                values[ids::peqPower] = values[ids::eqOn];

            juce::ValueTree snapshot ("Snapshot");
            for (const auto& [id, value] : values)
                if (! notStored().contains (id))
                    snapshot.setProperty (id, value, nullptr);

            if (const auto out = snapshot.createXml())
                out->writeTo (target);
        }
    }
}
} // namespace vocalbox
} // namespace onirique
