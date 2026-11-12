#pragma once
#include <juce_core/juce_core.h>
#include <map>

// ESSAIS DE PERFORMANCE (version de test seulement, ONIRIQUE_UI_PROFILE=fichier) : le temps passé dans chaque dessin, écrit
// dans le fichier à la fermeture de l'interface. Dans la version livrée, rien n'est mesuré.
namespace onirique::prof
{
struct Totals
{
    std::map<std::string, std::pair<double, int>> t;
    ~Totals()
    {
        const auto path = juce::SystemStats::getEnvironmentVariable ("ONIRIQUE_UI_PROFILE", {});
        if (path.isEmpty() || t.empty()) return;
        juce::String s;
        for (const auto& [k, v] : t) s << k << " : " << juce::String (v.first, 1) << " ms en " << v.second << " dessins (" << juce::String (v.first / juce::jmax (1, v.second), 3) << " ms chacun)\n";
        juce::File (path).replaceWithText (s);
    }
};
inline Totals& totals() { static Totals t; return t; }
struct Scope
{
    const char* name; double t0;
    explicit Scope (const char* n) : name (n), t0 (juce::Time::getMillisecondCounterHiRes()) {}
    ~Scope() { auto& e = totals().t[name]; e.first += juce::Time::getMillisecondCounterHiRes() - t0; ++e.second; }
};
} // namespace onirique::prof
#if ONIRIQUE_UI_TEST
 #define ONIRIQUE_PAINT_SCOPE(n) onirique::prof::Scope onirique_prof_scope_ (n)
#else
 #define ONIRIQUE_PAINT_SCOPE(n)
#endif
