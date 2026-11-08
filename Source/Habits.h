#pragma once
#include <juce_core/juce_core.h>
#include <map>
#include <vector>
#include <algorithm>

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

constexpr int kKeep = 20, kKeepMix = 40, kMin = 3;   // AUTO MIX : 40 séances (assez pour chaque rôle)

/*  ---- AUTO MIX : la correction de l'utilisateur, une séance = un échantillon.
    Les 4 macros en points (-100 .. +100) : PRÉSENCE, CHALEUR (avec le LOW de l'EQ ANALOG), TENUE (avec les compresseurs),
    ESPACE (avec les retours de réverbe et de delay). Puis, en unités des modules : l'air (AIR et HI de l'EQ ANALOG, bandes de
    l'EQ au-dessus de 5 kHz), le mud (MUD de l'EQ ANALOG, bandes de l'EQ entre 200 et 500 Hz : + = il en retire plus), les S
    (DE-ESSER, %), l'autotune (vitesse du TUNER, en octaves : + = plus marqué), le grain (préampli, saturation) et les
    respirations (DE-BREATH, dB). Une habitude : 3 séances au moins, 70 % dans le même sens, une moyenne au-delà d'un seuil. */
// role : LEAD, BACKS, ADLIBS, DOUBLES (-1 : pas noté, les séances d'avant) ; c : COMPARER utilisé pendant la retouche
struct MixSample { float p = 0, w = 0, h = 0, s = 0, a = 0, m = 0, d = 0, t = 0, g = 0, r = 0; int role = -1, c = 0; };

struct MixHabit
{
    float presence = 0.0f, warmth = 0.0f, hold = 0.0f, space = 0.0f;
    float air = 0.0f, mud = 0.0f, ess = 0.0f, tune = 0.0f, grain = 0.0f, breath = 0.0f;
    int sessions = 0;
    int role = -1;   // l'habitude d'un rôle (assez de séances pour lui), sinon -1 : l'habitude générale
    bool any() const noexcept
    {
        return presence != 0.0f || warmth != 0.0f || hold != 0.0f || space != 0.0f || air != 0.0f || mud != 0.0f || ess != 0.0f
            || tune != 0.0f || grain != 0.0f || breath != 0.0f;
    }

    juce::String describe() const
    {
        auto u8 = [] (const char* t) { return juce::String::fromUTF8 (t); };
        // les plus marquées d'abord (rapportées à leur seuil), quatre au plus : la phrase reste lisible
        std::vector<std::pair<float, juce::String>> s;
        auto add = [&] (float v, float thr, const char* more, const char* less) { if (v != 0.0f) s.push_back ({ std::abs (v) / thr, u8 (v > 0 ? more : less) }); };
        add (hold, 15.0f, "tu compresses plus tes voix", "tu compresses moins tes voix");
        add (space, 15.0f, "tu leur donnes plus d'espace", "tu leur donnes moins d'espace");
        add (presence, 15.0f, "tu les veux plus présentes", "tu les veux plus douces");
        add (warmth, 15.0f, "tu leur donnes plus de chaleur", "tu leur donnes moins de chaleur");
        add (air, 1.0f, "tu leur donnes plus d'air", "tu leur donnes moins d'air");
        add (mud, 1.0f, "tu enlèves plus de mud", "tu gardes plus de mud");
        add (ess, 8.0f, "tu enlèves plus de S", "tu gardes plus de S");
        add (tune, 0.4f, "tu veux l'autotune plus marqué", "tu veux l'autotune plus naturel");
        add (grain, 1.0f, "tu leur donnes plus de grain", "tu leur donnes moins de grain");
        add (breath, 2.0f, "tu baisses plus les respirations", "tu baisses moins les respirations");
        if (s.empty()) return {};
        std::sort (s.begin(), s.end(), [] (const auto& x, const auto& y) { return x.first > y.first; });
        juce::StringArray parts;
        for (size_t i = 0; i < std::min<size_t> (4, s.size()); ++i) parts.add (s[i].second);
        const auto last = parts[parts.size() - 1];
        parts.remove (parts.size() - 1);
        static const char* roleText[4] { "sur tes voix lead, ", "sur tes backs, ", "sur tes adlibs, ", "sur tes doubles, " };
        return u8 ("D'habitude, ") + (role >= 0 && role < 4 ? u8 (roleText[role]) : juce::String())
             + (parts.isEmpty() ? last : parts.joinIntoString (", ") + " et " + last) + ".";
    }
};

inline void recordMix (juce::int64 session, const MixSample& x)
{
    const juce::ScopedLock sl (lock());
    auto v = load();
    auto arr = v["mix"];
    juce::Array<juce::var> list;
    if (auto* a = arr.getArray()) list = *a;
    for (int i = list.size(); --i >= 0;) if ((juce::int64) list[i]["id"] == session) list.remove (i);
    auto* o = new juce::DynamicObject();
    o->setProperty ("id", session);
    o->setProperty ("p", x.p); o->setProperty ("w", x.w); o->setProperty ("h", x.h); o->setProperty ("s", x.s); o->setProperty ("a", x.a);
    o->setProperty ("m", x.m); o->setProperty ("d", x.d); o->setProperty ("t", x.t); o->setProperty ("g", x.g); o->setProperty ("r", x.r);
    o->setProperty ("role", x.role); o->setProperty ("c", x.c);
    list.add (juce::var (o));
    while (list.size() > kKeepMix) list.remove (0);
    v.getDynamicObject()->setProperty ("mix", list);
    save (v);
}

inline MixHabit mixHabit (int role = -1)
{
    const juce::ScopedLock sl (lock());
    MixHabit h;
    auto v = load();
    auto* all = v["mix"].getArray();
    if (all == nullptr) return h;
    // le rôle d'abord (LEAD, BACKS...) : ses séances seules quand il en a assez ; sinon toutes, comme avant
    juce::Array<juce::var> own;
    if (role >= 0) for (const auto& e : *all) if ((int) e.getProperty ("role", -1) == role) own.add (e);
    const bool byRole = own.size() >= kMin;
    const juce::Array<juce::var>* a = byRole ? &own : all;
    if (a->size() < kMin) return h;
    h.sessions = a->size();
    h.role = byRole ? role : -1;
    // une retouche faite en comparant avant / après (COMPARER, 2 fois au moins) pèse une fois et demie
    auto weight = [] (const juce::var& e) { return (int) e.getProperty ("c", 0) >= 2 ? 1.5f : 1.0f; };
    // thr : le seuil de la moyenne ; step : l'arrondi ; lo, hi : les bornes
    auto dim = [a, &weight] (const char* k, float thr, float step, float lo, float hi)
    {
        float sum = 0.0f, wsum = 0.0f, pos = 0.0f, neg = 0.0f;
        for (const auto& e : *a)
        {
            const float x = (float) (double) e[k], w = weight (e);
            sum += w * x; wsum += w;
            if (x > thr / 3.0f) pos += w; else if (x < -thr / 3.0f) neg += w;
        }
        const float mean = sum / wsum;
        const float agree = std::max (pos, neg) / wsum;
        if (std::abs (mean) < thr || agree < 0.7f) return 0.0f;
        return juce::jlimit (lo, hi, std::round (mean / step) * step);
    };
    h.presence = dim ("p", 15.0f, 5.0f, -100.0f, 100.0f); h.warmth = dim ("w", 15.0f, 5.0f, -100.0f, 100.0f);
    h.hold = dim ("h", 15.0f, 5.0f, -100.0f, 100.0f);     h.space = dim ("s", 15.0f, 5.0f, -100.0f, 100.0f);
    h.air = dim ("a", 1.0f, 0.5f, -10.0f, 10.0f);          h.mud = dim ("m", 1.0f, 0.5f, -10.0f, 10.0f);
    h.ess = dim ("d", 8.0f, 1.0f, -100.0f, 100.0f);        h.tune = dim ("t", 0.4f, 0.1f, -3.0f, 3.0f);
    h.grain = dim ("g", 1.0f, 0.5f, -10.0f, 10.0f);        h.breath = dim ("r", 2.0f, 1.0f, -30.0f, 30.0f);
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

// ---- LES CHAÎNES CONSTRUITES À LA MAIN (sans AUTO MIX, ou la sienne remaniée) : une séance = la suite de ses modules (types).
//      Après 3 chaînes : sa chaîne habituelle (les modules présents dans au moins la moitié de ses chaînes, rangés à leur place
//      moyenne) et les modules qu'il ajoute le plus.
inline void recordChain (const juce::String& sessionKey, const std::vector<int>& types)
{
    const juce::ScopedLock sl (lock());
    auto v = load();
    juce::Array<juce::var> list;
    if (auto* a = v["chains"].getArray()) list = *a;
    for (int i = list.size(); --i >= 0;) if (list[i]["s"].toString() == sessionKey) list.remove (i);
    juce::Array<juce::var> t;
    for (int x : types) t.add (x);
    auto* o = new juce::DynamicObject();
    o->setProperty ("s", sessionKey);
    o->setProperty ("t", t);
    list.add (juce::var (o));
    while (list.size() > kKeep) list.remove (0);
    v.getDynamicObject()->setProperty ("chains", list);
    save (v);
}

struct ChainHabit
{
    std::vector<int> usual;                    // sa chaîne habituelle, dans l'ordre
    std::vector<std::pair<int, float>> often;  // les modules qu'il met le plus (part de ses chaînes)
    int chains = 0;
    bool any() const noexcept { return usual.size() >= 2; }
};

inline ChainHabit chainHabit()
{
    const juce::ScopedLock sl (lock());
    ChainHabit h;
    auto v = load();
    auto* a = v["chains"].getArray();
    if (a == nullptr || a->size() < kMin) return h;
    h.chains = a->size();
    std::map<int, int> count;
    std::map<int, float> place;   // la place moyenne (0 : en tête, 1 : en bout de chaîne)
    for (const auto& e : *a)
    {
        auto* t = e["t"].getArray();
        if (t == nullptr || t->isEmpty()) continue;
        std::vector<int> seen;
        for (int i = 0; i < t->size(); ++i)
        {
            const int type = (int) (*t)[i];
            if (std::find (seen.begin(), seen.end(), type) != seen.end()) continue;
            seen.push_back (type);
            ++count[type];
            place[type] += t->size() > 1 ? (float) i / (float) (t->size() - 1) : 0.5f;
        }
    }
    std::vector<std::pair<float, int>> order;
    for (const auto& [type, n] : count)
    {
        const float share = (float) n / (float) h.chains;
        h.often.push_back ({ type, share });
        if (share >= 0.5f) order.push_back ({ place[type] / (float) n, type });
    }
    std::sort (order.begin(), order.end());
    for (const auto& o : order) h.usual.push_back (o.second);
    std::sort (h.often.begin(), h.often.end(), [] (const auto& x, const auto& y) { return x.second > y.second; });
    if (h.often.size() > 5) h.often.resize (5);
    return h;
}

// ---- MON PROFIL : le nombre de séances retenues par module (ceux qui ont une habitude : 3 séances au moins), et l'effacement
inline std::map<int, int> moduleSessions()
{
    const juce::ScopedLock sl (lock());
    std::map<int, int> out;
    auto v = load();
    if (auto* o = v["modules"].getDynamicObject())
        for (const auto& p : o->getProperties())
            if (auto* a = p.value.getArray()) out[p.name.toString().getIntValue()] = a->size();
    return out;
}
inline int mixSessions (int role = -1)
{
    const juce::ScopedLock sl (lock());
    auto v = load();
    auto* a = v["mix"].getArray();
    if (a == nullptr) return 0;
    if (role < 0) return a->size();
    int n = 0;
    for (const auto& e : *a) if ((int) e.getProperty ("role", -1) == role) ++n;
    return n;
}
inline void clearAll()
{
    const juce::ScopedLock sl (lock());
    file().deleteFile();
}
} // namespace onirique::habits
