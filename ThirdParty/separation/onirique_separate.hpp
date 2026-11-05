#pragma once
#include <functional>
#include <string>
#include <vector>

namespace onirique::sep
{
// Sépare la voix d'un morceau stéréo à 44,1 kHz (demucs.cpp, modèle htdemucs_ft vocals).
// progress (0..1) est appelé souvent ; s'il rend false, le calcul s'arrête au plus vite et la fonction rend false.
// vocals : la voix en mono, même longueur que l'entrée.
bool separateVocals (const std::string& modelFile, const float* left, const float* right, int numSamples, int threads,
                     std::vector<float>& vocals, std::function<bool (float)> progress);
}

#ifdef _WIN32
namespace onirique::sep
{
// le modèle intégré au plugin Windows (ressource) : sa taille (-1 s'il n'y est pas), et son écriture sur le disque
long long embeddedModelSize (void* module);
bool extractEmbeddedModel (void* module, const std::wstring& destination);
}
#endif
