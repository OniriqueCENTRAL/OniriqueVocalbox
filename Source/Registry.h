#pragma once
#include <juce_core/juce_core.h>
#include <vector>

/*  Les VOCALBOX d'un même projet se reconnaissent : chaque instance (dans le même processus que le DAW, la même DLL) se
    déclare avec son rôle (LEAD, BACKS, ADLIBS, DOUBLES) et la tonalité qu'AUTO MIX a trouvée sur sa prise. Les BACKS, ADLIBS et
    DOUBLES reprennent la tonalité du LEAD pour leur TUNER ; l'écran AUTO MIX montre qui est là. Rien ne sort de l'ordinateur. */
namespace onirique::registry
{
struct Entry
{
    const void* who = nullptr;
    int role = 0;      // 0 LEAD, 1 BACKS, 2 ADLIBS, 3 DOUBLES
    int key = -1;      // -1 inconnue ; 0..11 majeur, 12..23 mineur
    float keyConfidence = 0.0f;
};

inline juce::CriticalSection& lock() { static juce::CriticalSection l; return l; }
inline std::vector<Entry>& entries() { static std::vector<Entry> e; return e; }

inline void publishRole (const void* who, int role)
{
    const juce::ScopedLock sl (lock());
    for (auto& e : entries()) if (e.who == who) { e.role = role; return; }
    entries().push_back ({ who, role, -1, 0.0f });
}

inline void publishKey (const void* who, int key, float confidence)
{
    const juce::ScopedLock sl (lock());
    for (auto& e : entries()) if (e.who == who) { e.key = key; e.keyConfidence = confidence; return; }
    entries().push_back ({ who, 0, key, confidence });
}

inline void remove (const void* who)
{
    const juce::ScopedLock sl (lock());
    auto& v = entries();
    v.erase (std::remove_if (v.begin(), v.end(), [who] (const Entry& e) { return e.who == who; }), v.end());
}

// la tonalité d'une autre instance : celle du LEAD d'abord, sinon la plus sûre ; -1 si personne ne la connaît
inline int sharedKey (const void* who, float* confidence = nullptr)
{
    const juce::ScopedLock sl (lock());
    const Entry* best = nullptr;
    for (const auto& e : entries())
    {
        if (e.who == who || e.key < 0) continue;
        if (best == nullptr || (e.role == 0 && best->role != 0) || (e.role == best->role && e.keyConfidence > best->keyConfidence)) best = &e;
    }
    if (best != nullptr && confidence != nullptr) *confidence = best->keyConfidence;
    return best != nullptr ? best->key : -1;
}

// les autres instances du projet (leurs rôles et tonalités), pour l'écran
inline std::vector<Entry> others (const void* who)
{
    const juce::ScopedLock sl (lock());
    std::vector<Entry> r;
    for (const auto& e : entries()) if (e.who != who) r.push_back (e);
    return r;
}
} // namespace onirique::registry
