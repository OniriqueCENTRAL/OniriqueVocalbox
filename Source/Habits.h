#pragma once
#include <juce_core/juce_core.h>
#include <map>
#include <vector>

/*  LES HABITUDES de l'utilisateur, gardées sur son ordinateur (AppData/Onirique/VocalBox/habits.json), jamais envoyées.

    - Après AUTO MIX : ce que l'utilisateur change à la chaîne montée, ramené aux quatre macros (PRÉSENCE, CHALEUR, TENUE,
      ESPACE). Une habitude existe quand au moins 3 mix vont dans le même sens (70 % des mix, ±15 en moyenne). Au mix suivant,
      la page AUTO MIX la propose : « D'habitude, tu compresses plus tes voix… appliquer ? ».
    - Sans AUTO MIX : les réglages d'un module que l'utilisateur a vraiment touchés (une séance = une empreinte par module).
      Après 3 séances, ajouter ce module propose ses réglages habituels (la médiane de ses dernières séances).
    Les 20 dernières séances sont gardées, par module et pour AUTO MIX. */
namespace onirique::habits
{
inline juce::File file()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Onirique").getChildFile ("VocalBox").getChildFile ("habits.json");
}

inline juce::CriticalSection& lock() { static juce::CriticalSection l; return l; }

inline juce::var load()
{
    auto v = juce::JSON::parse (file());
    if (! v.isObject()) v = juce::var (new juce::DynamicObject());
    return v;
}

inline void save (const juce::var& v)
{
    file().getParentDirectory().createDirectory();
    file().replaceWithText (juce::JSON::toString (v, true));
}

constexpr int kKeep = 20, kMin = 3;

// ---- AUTO MIX : la correction de l'utilisateur, en points de macros (-100 .. +100)
struct MixHabit
{
    float presence = 0.0f, warmth = 0.0f, hold = 0.0f, space = 0.0f;
    int sessions = 0;
    bool any() const noexcept { return presence != 0.0f || warmth != 0.0f || hold != 0.0f || space != 0.0f; }

    juce::String describe() const
    {
        juce::StringArray s;
        auto u8 = [] (const char* t) { return juce::String::fromUTF8 (t); };
        if (hold > 0) s.add (u8 ("tu compresses plus tes voix")); else if (hold < 0) s.add (u8 ("tu les compresses moins"));
        if (space > 0) s.add (u8 ("tu leur donnes plus d'espace")); else if (space < 0) s.add (u8 ("tu leur donnes moins d'espace"));
        if (presence > 0) s.add (u8 ("tu les veux plus présentes")); else if (presence < 0) s.add (u8 ("tu les veux plus douces"));
        if (warmth > 0) s.add (u8 ("plus chaudes")); else if (warmth < 0) s.add (u8 ("plus légères dans le bas"));
        if (s.isEmpty()) return {};
        const auto last = s[s.size() - 1];
        s.remove (s.size() - 1);
        return u8 ("D'habitude, ") + (s.isEmpty() ? last : s.joinIntoString (", ") + " et " + last) + ".";
    }
};

inline void recordMix (juce::int64 session, float presence, float warmth, float hold, float space)
{
    const juce::ScopedLock sl (lock());
    auto v = load();
    auto arr = v["mix"];
    juce::Array<juce::var> list;
    if (auto* a = arr.getArray()) list = *a;
    for (int i = list.size(); --i >= 0;) if ((juce::int64) list[i]["id"] == session) list.remove (i);
    auto* o = new juce::DynamicObject();
    o->setProperty ("id", session); o->setProperty ("p", presence); o->setProperty ("w", warmth); o->setProperty ("h", hold); o->setProperty ("s", space);
    list.add (juce::var (o));
    while (list.size() > kKeep) list.remove (0);
    v.getDynamicObject()->setProperty ("mix", list);
    save (v);
}

inline MixHabit mixHabit()
{
    const juce::ScopedLock sl (lock());
    MixHabit h;
    auto v = load();
    auto* a = v["mix"].getArray();
    if (a == nullptr || a->size() < kMin) return h;
    h.sessions = a->size();
    auto dim = [a] (const char* k)
    {
        float sum = 0.0f; int pos = 0, neg = 0;
        for (const auto& e : *a) { const float x = (float) (double) e[k]; sum += x; if (x > 5.0f) ++pos; else if (x < -5.0f) ++neg; }
        const float mean = sum / (float) a->size();
        const float agree = (float) std::max (pos, neg) / (float) a->size();
        if (std::abs (mean) < 15.0f || agree < 0.7f) return 0.0f;
        return juce::jlimit (-100.0f, 100.0f, std::round (mean / 5.0f) * 5.0f);
    };
    h.presence = dim ("p"); h.warmth = dim ("w"); h.hold = dim ("h"); h.space = dim ("s");
    return h;
}

// ---- un module : les réglages d'une séance (identifiants du premier exemplaire)
inline void recordModule (int type, const juce::String& sessionKey, const std::map<juce::String, float>& values)
{
    const juce::ScopedLock sl (lock());
    auto v = load();
    auto mods = v["modules"];
    if (! mods.isObject()) { mods = juce::var (new juce::DynamicObject()); v.getDynamicObject()->setProperty ("modules", mods); }
    const juce::Identifier key { juce::String (type) };
    juce::Array<juce::var> list;
    if (auto* a = mods[key].getArray()) list = *a;
    for (int i = list.size(); --i >= 0;) if (list[i]["s"].toString() == sessionKey) list.remove (i);
    auto* vals = new juce::DynamicObject();
    for (const auto& [id, x] : values) vals->setProperty (juce::Identifier (id), x);
    auto* o = new juce::DynamicObject();
    o->setProperty ("s", sessionKey);
    o->setProperty ("v", juce::var (vals));
    list.add (juce::var (o));
    while (list.size() > kKeep) list.remove (0);
    mods.getDynamicObject()->setProperty (key, list);
    save (v);
}

// les réglages habituels d'un module : la médiane de chaque réglage sur ses dernières séances (vide avant 3 séances)
inline std::map<juce::String, float> moduleHabit (int type)
{
    const juce::ScopedLock sl (lock());
    std::map<juce::String, float> out;
    auto v = load();
    auto* a = v["modules"][juce::Identifier (juce::String (type))].getArray();
    if (a == nullptr || a->size() < kMin) return out;
    std::map<juce::String, std::vector<float>> all;
    for (const auto& e : *a)
        if (auto* o = e["v"].getDynamicObject())
            for (const auto& p : o->getProperties()) all[p.name.toString()].push_back ((float) (double) p.value);
    for (auto& [id, xs] : all)
    {
        if ((int) xs.size() < kMin) continue;
        std::nth_element (xs.begin(), xs.begin() + (long) (xs.size() / 2), xs.end());
        out[id] = xs[xs.size() / 2];
    }
    return out;
}
} // namespace onirique::habits
