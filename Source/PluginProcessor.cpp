#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Learning.h"
#include "MixAnalysis.h"
#include "Registry.h"
#include "Habits.h"

using namespace onirique;

OniriqueVocalBoxAudioProcessor::OniriqueVocalBoxAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    pBypass = apvts.getRawParameterValue (ids::bypass);
    pOversampling = apvts.getRawParameterValue (ids::oversampling);
    apvts.addParameterListener (ids::oversampling, this);

    // presets d'avant la chaîne modulaire : leur chaîne se déduit de leurs anciens réglages
    presets.onLoad = [] (juce::ValueTree& s) { vocalbox::upgradeSnapshot (s); };
    // ONR Neural Learning : le cerveau publié, s'il est plus récent que celui du plugin (une fois par jour, hors de l'audio)
    learnPool.addJob ([] { onirique::mix::installBrainIfNewer (onirique::learning::fetchBrain()); });
    learnPool.addJob ([this] { sendPendingFromDisk(); });
    apvts.state.setProperty ("preset", "Init", nullptr);   // l'état de départ : les valeurs par défaut
    vocalbox::importOldUserPresets (presets);
    startTimerHz (10);
}

bool OniriqueVocalBoxAudioProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto& in = l.getMainInputChannelSet();
    const auto& out = l.getMainOutputChannelSet();
    if (in != out) return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

OniriqueVocalBoxAudioProcessor::~OniriqueVocalBoxAudioProcessor()
{
    stopTimer();
    cancelPendingUpdate();
    apvts.removeParameterListener (onirique::ids::oversampling, this);
    onirique::registry::remove (this);
    habitTick (true);   // les dernières retouches de la séance
    // un mix sans note ni 10 minutes de retouches : son retour est gardé et partira à la prochaine ouverture
    if (learnPending() && onirique::learning::enabled())
    {
        const auto f = onirique::learning::file().getSiblingFile ("learn-pending.json");
        f.getParentDirectory().createDirectory();
        f.replaceWithText (juce::JSON::toString (learnFinal(), true));
    }
}

//==============================================================================
modules::Chain OniriqueVocalBoxAudioProcessor::getChain() const
{
    modules::Chain c {};
    for (int k = 0; k < kSlots; ++k)
        c[(size_t) k] = juce::roundToInt (apvts.getRawParameterValue (ids::slot[k])->load());
    // tassée à gauche, sans emplacement vide ni doublon
    modules::Chain out {};
    size_t n = 0;
    for (int code : c)
        if (modules::valid (code) && std::find (out.begin(), out.begin() + (long) n, code) == out.begin() + (long) n)
            out[n++] = code;
    return out;
}

void OniriqueVocalBoxAudioProcessor::setChain (const modules::Chain& c)
{
    for (int k = 0; k < kSlots; ++k)
        if (auto* p = apvts.getParameter (ids::slot[k]))
        {
            const float v = p->convertTo0to1 ((float) c[(size_t) k]);
            if (std::abs (p->getValue() - v) > 1.0e-6f) p->setValueNotifyingHost (v);
        }
    setLatencySamples (chain.latencyFor (c));
}

int OniriqueVocalBoxAudioProcessor::addModule (int type)
{
    auto c = getChain();
    const auto used = (size_t) std::count_if (c.begin(), c.end(), [] (int x) { return x != 0; });
    if (used >= c.size()) return 0;
    for (int k = 0; k < kInstances; ++k)
    {
        const int code = modules::code (type, k);
        if (std::find (c.begin(), c.end(), code) != c.end()) continue;
        // CREATIVE FX fait tourner les DOUBLER / CHORUS / FLANGER / PHONE de son exemplaire : jamais les deux à la fois
        auto inChain = [&c] (int ty, int kk) { return std::find (c.begin(), c.end(), modules::code (ty, kk)) != c.end(); };
        if (type == modules::creative && (inChain (modules::doubler, k) || inChain (modules::chorus, k) || inChain (modules::flanger, k) || inChain (modules::phone, k))) continue;
        if (modules::inCreative (type) && inChain (modules::creative, k)) continue;
        // un module ajouté part de ses réglages d'usine, allumé
        for (const auto& id : modules::paramIds (type))
            if (auto* p = apvts.getParameter (ids::inst (id.toRawUTF8(), k)))
                p->setValueNotifyingHost (p->getDefaultValue());
        if (auto* on = apvts.getParameter (ids::inst (modules::info (type).onId, k)))
            on->setValueNotifyingHost (1.0f);
        // CREATIVE FX arrive avec le doubleur allumé : on entend tout de suite quelque chose
        if (type == modules::creative)
            if (auto* d = apvts.getParameter (ids::inst (ids::doublerOn, k))) d->setValueNotifyingHost (1.0f);
        c[used] = code;
        setChain (c);
        return code;
    }
    return 0;
}

void OniriqueVocalBoxAudioProcessor::removeModule (int code)
{
    auto c = getChain();
    modules::Chain out {};
    size_t n = 0;
    for (int x : c) if (x != 0 && x != code) out[n++] = x;
    setChain (out);
}

void OniriqueVocalBoxAudioProcessor::moveModule (int from, int to)
{
    auto c = getChain();
    const int used = (int) std::count_if (c.begin(), c.end(), [] (int x) { return x != 0; });
    if (from < 0 || from >= used) return;
    to = juce::jlimit (0, used - 1, to);
    if (from == to) return;
    const int m = c[(size_t) from];
    std::vector<int> v (c.begin(), c.begin() + used);
    v.erase (v.begin() + from);
    v.insert (v.begin() + to, m);
    modules::Chain out {};
    for (size_t k = 0; k < v.size(); ++k) out[k] = v[k];
    setChain (out);
}

//==============================================================================
// (re)construit la chaîne selon le suréchantillonnage, et annonce la latence au DAW
void OniriqueVocalBoxAudioProcessor::configureChain (double sampleRate)
{
    osNow = juce::jlimit (0, 3, juce::roundToInt (pOversampling->load()));
    chain.prepare (sampleRate, osNow);
    setLatencySamples (chain.getLatencySamples());
}

void OniriqueVocalBoxAudioProcessor::handleAsyncUpdate()
{
    const int os = juce::jlimit (0, 3, juce::roundToInt (pOversampling->load()));
    if (currentRate <= 0.0 || os == osNow)
    {
        osNow = os;
        return;   // rien qui change le traitement
    }
    suspendProcessing (true);    // le thread audio attend la fin de la reconfiguration (allocation hors temps réel)
    configureChain (currentRate);
    suspendProcessing (false);
}

void OniriqueVocalBoxAudioProcessor::learnArm (juce::var payload, std::vector<std::pair<juce::String, float>> applied)
{
    if (learnPending()) learnSend (0);   // le mix d'avant part tel quel
    learnPayload = std::move (payload);
    learnApplied = std::move (applied);
    learnAt = juce::Time::getMillisecondCounterHiRes() * 0.001;
    learnChain = getChain();
    learnCompares = compareCount.load();
}

juce::var OniriqueVocalBoxAudioProcessor::learnFinal()
{
    auto payload = learnPayload;
    // les retouches : ce que l'utilisateur a changé depuis (la valeur décidée, puis celle d'aujourd'hui)
    auto* fin = new juce::DynamicObject();
    int changed = 0;
    for (const auto& [id, was] : learnApplied)
        if (auto* p = apvts.getParameter (id))
        {
            const float now = p->convertFrom0to1 (p->getValue());
            if (std::abs (now - was) > 1.0e-3f * juce::jmax (1.0f, std::abs (was))) { fin->setProperty (id, now); ++changed; }
        }
    // ce que l'utilisateur a fait de la chaîne (les « oui » et « non » silencieux) : gardée telle quelle, retouchée, ou démontée
    const auto now = getChain();
    int total = 0, kept = 0, added = 0;
    for (int c : learnChain) if (modules::valid (c)) { ++total; if (std::find (now.begin(), now.end(), c) != now.end()) ++kept; }
    for (int c : now) if (modules::valid (c) && std::find (learnChain.begin(), learnChain.end(), c) == learnChain.end()) ++added;
    auto* out = new juce::DynamicObject();
    out->setProperty ("total", total);
    out->setProperty ("kept", kept);
    out->setProperty ("added", added);
    out->setProperty ("changed", changed);
    out->setProperty ("compares", compareCount.load() - learnCompares);
    juce::Array<juce::var> removed, addedNames;
    for (int c : learnChain) if (modules::valid (c) && std::find (now.begin(), now.end(), c) == now.end()) removed.add (modules::info (modules::typeOf (c)).name);
    for (int c : now) if (modules::valid (c) && std::find (learnChain.begin(), learnChain.end(), c) == learnChain.end()) addedNames.add (modules::info (modules::typeOf (c)).name);
    out->setProperty ("removedModules", removed);
    out->setProperty ("addedModules", addedNames);
    out->setProperty ("result", total > 0 && kept * 2 < total ? "rebuilt"
                              : kept == total && added == 0 && changed == 0 ? "kept" : "tweaked");
    if (auto* o = payload.getDynamicObject())
    {
        o->setProperty ("final", juce::var (fin));
        o->setProperty ("outcome", juce::var (out));
        o->setProperty ("minutes", juce::roundToInt ((juce::Time::getMillisecondCounterHiRes() * 0.001 - learnAt) / 60.0));
    }
    return payload;
}

void OniriqueVocalBoxAudioProcessor::learnSend (int rating)
{
    if (! learnPending()) return;
    auto payload = learnFinal();
    learnPayload = juce::var();
    if (! onirique::learning::enabled()) return;   // éteint entre-temps : rien ne part
    if (auto* o = payload.getDynamicObject()) o->setProperty ("rating", juce::jlimit (-1, 1, rating));
    learnPool.addJob ([payload] { onirique::learning::postFeedback (payload); });
}

// un retour gardé à la fermeture du projet (le mix n'avait ni note ni 10 minutes) : il part à la prochaine ouverture
void OniriqueVocalBoxAudioProcessor::sendPendingFromDisk()
{
    const auto f = onirique::learning::file().getSiblingFile ("learn-pending.json");
    if (! f.existsAsFile()) return;
    const auto v = juce::JSON::parse (f);
    f.deleteFile();
    if (v.isObject() && onirique::learning::enabled()) onirique::learning::postFeedback (v);
}

void OniriqueVocalBoxAudioProcessor::setWhy (const std::map<int, juce::String>& w)
{
    auto* o = new juce::DynamicObject();
    for (const auto& [code, text] : w) if (text.isNotEmpty()) o->setProperty (juce::Identifier (juce::String (code)), text);
    apvts.state.setProperty ("mixWhy", juce::JSON::toString (juce::var (o), true), nullptr);
}

juce::String OniriqueVocalBoxAudioProcessor::whyOf (int code) const
{
    const auto v = juce::JSON::parse (apvts.state.getProperty ("mixWhy").toString());
    return v.isObject() ? v.getProperty (juce::Identifier (juce::String (code)), {}).toString() : juce::String();
}


//==============================================================================
// LES HABITUDES : les réglages d'un module (sans l'interrupteur, ni LISTEN / SOLO : ce sont des écoutes, pas des réglages)
std::map<juce::String, float> OniriqueVocalBoxAudioProcessor::habitSnapshot (int type, int k) const
{
    std::map<juce::String, float> out;
    const juce::String on (modules::info (type).onId);
    for (const auto& id : modules::paramIds (type))
    {
        if (id == on || id.containsIgnoreCase ("listen") || id.containsIgnoreCase ("solo")) continue;
        if (auto* p = apvts.getRawParameterValue (ids::inst (id.toRawUTF8(), k))) out[id] = p->load();
    }
    return out;
}

void OniriqueVocalBoxAudioProcessor::habitArm()
{
    habitTick (true);   // le mix d'avant garde sa dernière retouche
    habitMixId = juce::Time::currentTimeMillis();
    // la chaîne d'AUTO MIX arrive telle quelle : son état sert de départ à la retouche, et ce n'est pas encore une habitude
    habitArrival.clear();
    habitChain = getChain();
    for (int c : habitChain) if (modules::valid (c)) habitArrival[c] = habitSnapshot (modules::typeOf (c), modules::instanceOf (c));
    habitMixBase = habitArrival;
    habitRole = juce::roundToInt (apvts.getRawParameterValue (ids::voiceRole)->load());
    habitCompares = compareCount.load();
    habitOffer.store (habits::mixHabit (habitRole).any());
}

void OniriqueVocalBoxAudioProcessor::habitTick (bool now)
{
    // un module qui arrive (ajouté, preset, AUTO MIX) : son état de départ est noté ; un module retiré est oublié
    const auto c = getChain();
    if (c != habitChain)
    {
        for (int x : c) if (modules::valid (x) && habitArrival.find (x) == habitArrival.end()) habitArrival[x] = habitSnapshot (modules::typeOf (x), modules::instanceOf (x));
        for (auto it = habitArrival.begin(); it != habitArrival.end();)
            it = std::find (c.begin(), c.end(), it->first) == c.end() ? habitArrival.erase (it) : std::next (it);
        habitChain = c;
        // le pourquoi d'AUTO MIX : oublié pour un module qui quitte la chaîne (un autre pourrait reprendre sa place)
        const auto w = juce::JSON::parse (apvts.state.getProperty ("mixWhy").toString());
        if (auto* o = w.getDynamicObject())
        {
            std::map<int, juce::String> keep;
            for (const auto& p : o->getProperties())
                if (std::find (c.begin(), c.end(), p.name.toString().getIntValue()) != c.end()) keep[p.name.toString().getIntValue()] = p.value.toString();
            if ((int) keep.size() != o->getProperties().size()) setWhy (keep);
        }
    }
    if (! habitStarted) { habitStarted = true; habitStartChain = c; }
    if (! now && ++habitTicks < 300) return;   // l'écriture : toutes les 30 s, et à la fermeture
    habitTicks = 0;
    try
    {
        // la chaîne construite à la main dans cette séance (pas celle du projet rouvert, pas celle d'AUTO MIX telle quelle)
        {
            std::vector<int> types;
            for (int x : c) if (modules::valid (x)) types.push_back (modules::typeOf (x));
            std::vector<int> autoTypes;
            for (const auto& [code, base] : habitMixBase) autoTypes.push_back (modules::typeOf (code));
            auto sorted = [] (std::vector<int> v) { std::sort (v.begin(), v.end()); return v; };
            const bool isAuto = habitMixId != 0 && sorted (types) == sorted (autoTypes);
            if (types.size() >= 2 && c != habitStartChain && ! isAuto && types != habitChainWritten)
            {
                habits::recordChain (juce::String (habitSession) + "-chain", types);
                habitChainWritten = types;
            }
        }
        // un module que l'utilisateur a touché : une empreinte par séance
        for (const auto& [code, start] : habitArrival)
        {
            const auto cur = habitSnapshot (modules::typeOf (code), modules::instanceOf (code));
            bool touched = false;
            for (const auto& [id, v] : cur) { auto s = start.find (id); touched = touched || (s != start.end() && std::abs (s->second - v) > 1.0e-4f); }
            if (! touched || habitWritten[code] == cur) continue;
            habits::recordModule (modules::typeOf (code), juce::String (habitSession) + "-" + juce::String (code), cur);
            habitWritten[code] = cur;
        }
        // après AUTO MIX : la retouche de l'utilisateur, module par module, ramenée aux macros et aux grandeurs des modules
        if (habitMixId != 0)
        {
            auto macro = [this] (const char* id) { auto* p = apvts.getRawParameterValue (id); return p != nullptr ? p->load() : 0.0f; };
            habits::MixSample x;
            float holdD = 0.0f, spaceD = 0.0f; int nh = 0, ns = 0;
            for (const auto& [code, base] : habitMixBase)
            {
                if (std::find (c.begin(), c.end(), code) == c.end()) continue;
                const int t = modules::typeOf (code);
                const auto cur = habitSnapshot (t, modules::instanceOf (code));
                auto b = [&base] (const char* id) { auto it = base.find (id); return it != base.end() ? it->second : 0.0f; };
                auto n = [&cur] (const char* id) { auto it = cur.find (id); return it != cur.end() ? it->second : 0.0f; };
                auto d = [&] (const char* id) { return n (id) - b (id); };
                switch (t)
                {
                    case modules::fet:  holdD += d (ids::fetIn) / 6.0f * 100.0f; ++nh; break;
                    case modules::opto: holdD += d (ids::laPeak) / 20.0f * 100.0f; ++nh; break;
                    case modules::cl1b: holdD += -d (ids::cl1bThreshold) / 8.0f * 100.0f; ++nh; break;
                    case modules::reverb: spaceD += b (ids::reverbMix) > 0.5f ? 100.0f * std::log2 (std::max (n (ids::reverbMix), 0.5f) / b (ids::reverbMix)) : 0.0f; ++ns; break;
                    case modules::delay:  spaceD += b (ids::delayMix) > 0.5f ? 100.0f * std::log2 (std::max (n (ids::delayMix), 0.5f) / b (ids::delayMix)) : 0.0f; ++ns; break;
                    case modules::analog: x.a += d (ids::air) + 0.5f * d (ids::high); x.m += d (ids::mud); x.w += 15.0f * d (ids::low); break;
                    case modules::peq:
                        for (int k = 0; k < 8; ++k)
                        {
                            if (n (ids::peqOn[k]) < 0.5f) continue;
                            const float f = n (ids::peqFreq[k]), g = d (ids::peqGain[k]);
                            if (f >= 5000.0f) x.a += 0.5f * g;
                            else if (f >= 200.0f && f <= 500.0f) x.m -= 0.5f * g;
                        }
                        break;
                    case modules::deess:    x.d += d (ids::deessAmt); break;
                    case modules::tuner:    x.t += std::log2 ((b (ids::tuneRetune) + 1.0f) / (n (ids::tuneRetune) + 1.0f)); break;
                    case modules::preamp:   x.g += d (ids::preampGain) / 6.0f; break;
                    case modules::sat:      x.g += d (ids::drive); break;
                    case modules::debreath: x.r += d (ids::dbrReduction); break;
                    default: break;
                }
            }
            x.p = juce::jlimit (-100.0f, 100.0f, macro (ids::macroPresence));
            x.w = juce::jlimit (-100.0f, 100.0f, macro (ids::macroWarmth) + x.w);
            x.h = juce::jlimit (-100.0f, 100.0f, macro (ids::macroHold) + (nh > 0 ? holdD / (float) nh : 0.0f));
            x.s = juce::jlimit (-100.0f, 100.0f, macro (ids::macroSpace) + (ns > 0 ? spaceD / (float) ns : 0.0f));
            x.role = habitRole;
            x.c = compareCount.load() - habitCompares;
            habits::recordMix (habitMixId, x);
        }
    }
    catch (...) {}   // un fichier illisible ne doit jamais gêner le plugin
}

void OniriqueVocalBoxAudioProcessor::timerCallback()
{
    habitTick (false);
    // l'apprentissage : sans note au bout de 10 minutes, le mix part avec ses seules retouches
    if (learnPending() && juce::Time::getMillisecondCounterHiRes() * 0.001 - learnAt > 600.0) learnSend (0);
    // les autres VOCALBOX du projet voient le rôle de celle-ci
    if (auto* r = apvts.getRawParameterValue (onirique::ids::voiceRole)) onirique::registry::publishRole (this, juce::roundToInt (r->load()));
    // chaîne changée par un preset, A/B, l'automation du DAW... : sa latence suit
    if (currentRate > 0.0)
    {
        const int l = chain.latencyFor (getChain());
        if (l != getLatencySamples()) setLatencySamples (l);
    }
}

void OniriqueVocalBoxAudioProcessor::prepareToPlay (double sr, int)
{
    license.refresh();   // beaucoup de DAW rappellent prepareToPlay juste avant un export

    // Toute l'allocation se fait ici ; processBlock n'alloue rien.
    currentRate = sr;
    configureChain (sr);
    capturing.store (false);
    capLen = juce::jmax (1, (int) (sr * 10.0));
    capBuf.assign ((size_t) capLen, 0.0f);
    capPos.store (0);
}

void OniriqueVocalBoxAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int total = buffer.getNumSamples();
    const int numCh = juce::jmin (buffer.getNumChannels(), 2);

    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, total);

    if (numCh < 1 || total < 1)
        return;

    // tempo de l'hôte (DELAY calé sur le tempo) ; sans transport, on garde le dernier connu
    if (auto* ph = getPlayHead())
        if (const auto pos = ph->getPosition())
            if (const auto bpm = pos->getBpm(); bpm.hasValue() && *bpm > 1.0)
                hostBpm.store (*bpm, std::memory_order_relaxed);

    float* ch[2] { buffer.getWritePointer (0), numCh > 1 ? buffer.getWritePointer (1) : nullptr };
    inputTap.push (ch[0], ch[1], total);    // analyseur : signal d'entrée
    if (capturing.load (std::memory_order_acquire))
    {
        // AUTO MIX : la prise en mono, sans les blocs silencieux (transport arrêté, blancs)
        double e = 0.0;
        for (int i = 0; i < total; ++i) { const float m = numCh > 1 ? 0.5f * (ch[0][i] + ch[1][i]) : ch[0][i]; e += (double) m * m; }
        if (std::sqrt (e / total) > 1.0e-4)   // -80 dB : le transport arrêté ne compte pas, le fond de la pièce si
        {
            int p = capPos.load (std::memory_order_relaxed);
            const int n = juce::jmin (total, capLen - p);
            for (int i = 0; i < n; ++i) capBuf[(size_t) (p + i)] = numCh > 1 ? 0.5f * (ch[0][i] + ch[1][i]) : ch[0][i];
            p += n;
            capPos.store (p, std::memory_order_release);
            if (p >= capLen) capturing.store (false, std::memory_order_release);
        }
    }
    chain.process (ch, numCh, total);
    outputTap.push (ch[0], ch[1], total);   // analyseur : signal de sortie
    license.process (buffer, getSampleRate(), isNonRealtime());

    // crête de sortie + correction d'Auto Gain pour l'interface
    float pk = 0.0f;
    for (int c = 0; c < numCh; ++c)
        pk = juce::jmax (pk, buffer.getMagnitude (c, 0, total));
    if (pk > outPeak.load (std::memory_order_relaxed))
        outPeak.store (pk, std::memory_order_relaxed);
    autoGainDbShown.store (pBypass->load() > 0.5f ? 0.0f : chain.getAutoGainDb(), std::memory_order_relaxed);
}

//==============================================================================
void OniriqueVocalBoxAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    presets.writeState (state);   // le côté A/B écouté, et l'autre
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void OniriqueVocalBoxAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto findParam = [&xml] (const juce::String& id) -> juce::XmlElement*
            {
                for (auto* e : xml->getChildWithTagNameIterator ("PARAM"))
                    if (e->getStringAttribute ("id") == id) return e;
                return nullptr;
            };
            auto setParam = [&] (const juce::String& id, double v)
            {
                auto* e = findParam (id);
                if (e == nullptr) { e = xml->createNewChildElement ("PARAM"); e->setAttribute ("id", id); }
                e->setAttribute ("value", v);
            };

            // projets d'avant la séparation EQ paramétrique / EQ ANALOG : l'ordre à 6 modules est converti
            // (l'EQ ANALOG se place juste après le MULTIBAND) et l'EQ paramétrique suit l'ancien interrupteur EQ
            if (findParam (ids::chainOrder) == nullptr)
            {
                const auto* old = findParam (ids::legacyChainOrder);
                setParam (ids::chainOrder, old != nullptr ? convertLegacyOrder (old->getIntAttribute ("value")) : 0);
            }
            if (findParam (ids::peqPower) == nullptr)
                if (const auto* eq = findParam (ids::eqOn))
                    setParam (ids::peqPower, eq->getDoubleAttribute ("value"));

            // projets d'avant la chaîne modulaire : la chaîne se déduit de l'ancien ordre et des interrupteurs
            if (findParam (ids::slot[0]) == nullptr)
            {
                const auto c = modules::legacyChain (
                    [&] (const char* id, float def) { const auto* e = findParam (id); return e != nullptr ? (float) e->getDoubleAttribute ("value") : def; },
                    [&] (const char* id, float v) { setParam (id, v); });
                for (int k = 0; k < kSlots; ++k) setParam (ids::slot[k], c[(size_t) k]);
            }

            auto state = juce::ValueTree::fromXml (*xml);
            // un projet enregistré avec l'ancienne barre : le nom de son preset suit
            if (! state.hasProperty ("preset") && state.hasProperty ("presetName"))
                state.setProperty ("preset", state["presetName"], nullptr);
            presets.readState (state);
            apvts.replaceState (state);
            if (currentRate > 0.0) setLatencySamples (chain.latencyFor (getChain()));
            habitStarted = true;             // la chaîne du projet rouvert : pas une construction de cette séance
            habitStartChain = getChain();
        }
}

juce::AudioProcessorEditor* OniriqueVocalBoxAudioProcessor::createEditor()
{
    return new OniriqueVocalBoxAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OniriqueVocalBoxAudioProcessor();
}

// la chaîne telle qu'elle est : chaque module (son nom, son exemplaire, ses réglages, sans les écoutes) ; pour la communauté
juce::var OniriqueVocalBoxAudioProcessor::chainSnapshot() const
{
    juce::Array<juce::var> mods;
    for (int c : getChain())
    {
        if (! modules::valid (c)) continue;
        auto* m = new juce::DynamicObject();
        m->setProperty ("type", modules::info (modules::typeOf (c)).name);
        m->setProperty ("instance", modules::instanceOf (c));
        m->setProperty ("on", apvts.getRawParameterValue (ids::inst (modules::info (modules::typeOf (c)).onId, modules::instanceOf (c)))->load() > 0.5f);
        auto* p = new juce::DynamicObject();
        for (const auto& [id, v] : habitSnapshot (modules::typeOf (c), modules::instanceOf (c))) p->setProperty (juce::Identifier (id), std::round (v * 1000.0f) / 1000.0f);
        m->setProperty ("params", juce::var (p));
        mods.add (juce::var (m));
    }
    return mods;
}

bool OniriqueVocalBoxAudioProcessor::buildUsualChain()
{
    const auto h = habits::chainHabit();
    if (! h.any()) return false;
    learnFlush ("usual");   // le retour d'AUTO MIX en cours part avant que la chaîne ne change
    auto set = [this] (const juce::String& id, float v)
    {
        if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
    };
    modules::Chain c {};
    size_t n = 0;
    for (int type : h.usual)
    {
        if (n >= c.size() || type < 0 || type >= modules::numTypes) continue;
        for (const auto& id : modules::paramIds (type))
            if (auto* p = apvts.getParameter (ids::inst (id.toRawUTF8(), 0))) p->setValueNotifyingHost (p->getDefaultValue());
        set (ids::inst (modules::info (type).onId, 0), 1.0f);
        for (const auto& [id, v] : habits::moduleHabit (type)) set (ids::inst (id.toRawUTF8(), 0), v);
        c[n++] = modules::code (type, 0);
    }
    if (n == 0) return false;
    setWhy ({});
    setChain (c);
    return true;
}
