#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"

namespace onirique
{
/*  Presets : usine (définis ici) + utilisateur (fichiers .ovbpreset dans
    %APPDATA%/Onirique/VocalBox/Presets). Thread de l'interface uniquement.

    - Un preset contient tous les paramètres SAUF Bypass, le mode LIVE / MIX, le suréchantillonnage et l'écoute
      RESONANCE (réglages de session : charger un preset ne change jamais la latence ni ce qu'on entend).
    - Un paramètre absent d'un fichier (preset d'une version plus ancienne) reprend sa valeur par défaut.
    - Le nom du preset courant est rangé dans l'état du plugin : il revient avec le projet.
    - "Modifié" : comparaison des valeurs actuelles avec celles du chargement.
*/
class PresetManager
{
public:
    struct Entry { juce::String name; bool factory = false; juce::File file; int factoryIndex = -1; };

    explicit PresetManager (juce::AudioProcessorValueTreeState& s) : apvts (s)
    {
        folder().createDirectory();
        takeSnapshot();
    }

    static juce::File folder()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Onirique").getChildFile ("VocalBox").getChildFile ("Presets");
    }

    std::vector<Entry> list() const
    {
        std::vector<Entry> v;
        for (int i = 0; i < (int) factory().size(); ++i)
            v.push_back ({ factory()[(size_t) i].name, true, {}, i });
        auto files = folder().findChildFiles (juce::File::findFiles, false, "*.ovbpreset");
        files.sort();
        for (auto& f : files) v.push_back ({ f.getFileNameWithoutExtension(), false, f, -1 });
        return v;
    }

    juce::String getCurrentName() const { return apvts.state.getProperty ("presetName", "Init").toString(); }

    bool isModified() const
    {
        if (snapshot.empty()) return false;
        size_t i = 0;
        for (auto* p : params())
            if (i < snapshot.size() && std::abs (p->getValue() - snapshot[i++]) > 1.0e-4f) return true;
        return false;
    }

    void load (const Entry& e)
    {
        std::map<juce::String, float> values;
        if (e.factory)
            values = factory()[(size_t) e.factoryIndex].values;
        else if (auto xml = juce::XmlDocument::parse (e.file))
            for (auto* p : xml->getChildWithTagNameIterator ("PARAM"))
                values[p->getStringAttribute ("id")] = (float) p->getDoubleAttribute ("value");

        // presets d'avant la séparation EQ paramétrique / EQ ANALOG (ordre à 6 modules, un seul interrupteur EQ)
        if (values.count (ids::chainOrder) == 0 && values.count (ids::legacyChainOrder) != 0)
            values[ids::chainOrder] = (float) convertLegacyOrder (juce::roundToInt (values[ids::legacyChainOrder]));
        if (values.count (ids::peqPower) == 0 && values.count (ids::eqOn) != 0)
            values[ids::peqPower] = values[ids::eqOn];

        applyValues (values, e.name);
    }

    // tous les réglages d'un coup (valeurs en unités réelles) ; un paramètre absent reprend sa valeur par défaut.
    // Sert aux presets et à l'ASSISTANT.
    void applyValues (const std::map<juce::String, float>& values, const juce::String& name)
    {
        for (auto* p : params())
        {
            const auto it = values.find (p->getParameterID());
            const float norm = it != values.end() ? p->convertTo0to1 (it->second) : p->getDefaultValue();
            p->beginChangeGesture();
            p->setValueNotifyingHost (norm);
            p->endChangeGesture();
        }
        apvts.state.setProperty ("presetName", name, nullptr);
        takeSnapshot();
    }

    // valeurs actuelles de tous les réglages (unités réelles) : sert à annuler une application de l'ASSISTANT
    std::map<juce::String, float> currentValues() const
    {
        std::map<juce::String, float> v;
        for (auto* p : params()) v[p->getParameterID()] = p->convertFrom0to1 (p->getValue());
        return v;
    }

    // renvoie false si le nom est vide ; un nom d'usine est suffixé pour ne jamais l'écraser
    bool save (juce::String name)
    {
        name = juce::File::createLegalFileName (name.trim());
        if (name.isEmpty()) return false;
        for (const auto& f : factory()) if (f.name.equalsIgnoreCase (name)) name << " (user)";

        juce::XmlElement xml ("OniriquePreset");
        xml.setAttribute ("name", name);
        xml.setAttribute ("version", 1);
        for (auto* p : params())
        {
            auto* e = xml.createNewChildElement ("PARAM");
            e->setAttribute ("id", p->getParameterID());
            e->setAttribute ("value", (double) p->convertFrom0to1 (p->getValue()));
        }
        if (! xml.writeTo (folder().getChildFile (name + ".ovbpreset"))) return false;
        apvts.state.setProperty ("presetName", name, nullptr);
        takeSnapshot();
        return true;
    }

    void remove (const Entry& e)
    {
        if (! e.factory) e.file.deleteFile();
    }

    // charge le preset précédent / suivant dans la liste (boucle)
    void step (int delta)
    {
        const auto v = list();
        if (v.empty()) return;
        int idx = 0;
        for (int i = 0; i < (int) v.size(); ++i) if (v[(size_t) i].name == getCurrentName()) idx = i;
        load (v[(size_t) ((idx + delta + (int) v.size()) % (int) v.size())]);
    }

    void takeSnapshot()
    {
        snapshot.clear();
        for (auto* p : params()) snapshot.push_back (p->getValue());
    }

private:
    struct Factory { juce::String name; std::map<juce::String, float> values; };

    // tous les paramètres sauf Bypass
    std::vector<juce::RangedAudioParameter*> params() const
    {
        std::vector<juce::RangedAudioParameter*> v;
        for (auto* p : apvts.processor.getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
                if (const auto id = r->getParameterID(); id != ids::bypass && id != ids::oversampling && id != ids::resoListen
                                                         && id != ids::peqListen && id != ids::peqListenBand
                                                         && ! id.startsWith ("mbSolo"))
                    v.push_back (r);   // réglages de session exclus : bypass, mode LIVE/MIX, suréchantillonnage, écoute
        return v;
    }

    // valeurs en unités réelles (dB, %, 0..10, index de choix, 0/1)
    static const std::vector<Factory>& factory()
    {
        static const std::vector<Factory> f {
            { "Init", {} },
            { "Clean Pop Vocal", { { ids::lowCut, 1.0f }, { ids::low, 3.0f }, { ids::mud, 4.0f }, { ids::high, 3.0f }, { ids::air, 3.0f },
                                   { ids::deessAmt, 30.0f }, { ids::resoAmt, 35.0f }, { ids::compOn, 1.0f },
                                   { ids::compMode, 2.0f }, { ids::fetIn, 6.0f }, { ids::fetAttack, 3.0f }, { ids::fetRelease, 5.0f },
                                   { ids::laPeak, 35.0f }, { ids::drive, 2.0f }, { ids::autoGain, 1.0f } } },
            { "Rap Lead",        { { ids::lowCut, 1.0f }, { ids::low, 5.0f }, { ids::lowFreq, 1.0f }, { ids::mud, 5.0f }, { ids::high, 3.0f }, { ids::air, 2.0f },
                                   { ids::deessAmt, 35.0f }, { ids::resoAmt, 40.0f }, { ids::compOn, 1.0f },
                                   { ids::compMode, 0.0f }, { ids::fetIn, 12.0f }, { ids::fetRatio, 1.0f }, { ids::fetAttack, 5.0f },
                                   { ids::fetRelease, 6.0f }, { ids::satType, 1.0f }, { ids::drive, 3.5f }, { ids::autoGain, 1.0f } } },
            { "Airy Ballad",     { { ids::lowCut, 1.0f }, { ids::low, 2.0f }, { ids::mud, 3.0f }, { ids::high, 3.0f }, { ids::air, 6.0f },
                                   { ids::deessAmt, 25.0f }, { ids::resoAmt, 30.0f }, { ids::compOn, 1.0f },
                                   { ids::compMode, 1.0f }, { ids::laPeak, 45.0f }, { ids::satType, 2.0f }, { ids::drive, 2.0f },
                                   { ids::autoGain, 1.0f } } },
            { "Warm Tape Vocal", { { ids::low, 4.0f }, { ids::lowFreq, 2.0f }, { ids::mud, 3.0f }, { ids::high, 2.0f }, { ids::deessAmt, 20.0f },
                                   { ids::compOn, 1.0f }, { ids::compMode, 1.0f }, { ids::laPeak, 40.0f }, { ids::satType, 2.0f },
                                   { ids::drive, 5.0f }, { ids::autoGain, 1.0f } } },
            { "Aggressive Rock", { { ids::lowCut, 1.0f }, { ids::low, 3.0f }, { ids::mud, 3.0f }, { ids::high, 4.0f }, { ids::air, 2.0f },
                                   { ids::deessAmt, 30.0f }, { ids::compOn, 1.0f }, { ids::compMode, 2.0f }, { ids::fetIn, 14.0f },
                                   { ids::fetRatio, 4.0f }, { ids::fetAttack, 4.0f }, { ids::fetRelease, 6.0f }, { ids::laPeak, 30.0f },
                                   { ids::satType, 1.0f }, { ids::drive, 6.0f }, { ids::autoGain, 1.0f } } },
        };
        return f;
    }

    juce::AudioProcessorValueTreeState& apvts;
    std::vector<float> snapshot;
};
} // namespace onirique
