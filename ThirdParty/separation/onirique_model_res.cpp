// Windows : le modèle de séparation est livré DANS le plugin (ressource RCDATA « DEMUCS_MODEL », voir model.rc), puis écrit
// une fois sur le disque au premier usage (demucs.cpp lit un fichier). Mac : il est dans Contents/Resources du bundle.
#include "onirique_separate.hpp"

#ifdef _WIN32
 #define WIN32_LEAN_AND_MEAN
 #define NOMINMAX
 #include <windows.h>
 #include <cstdio>

namespace onirique::sep
{
bool extractEmbeddedModel (void* module, const std::wstring& destination)
{
    auto h = (HMODULE) module;
    HRSRC res = FindResourceW (h, L"DEMUCS_MODEL", MAKEINTRESOURCEW (10));   // 10 = RT_RCDATA
    if (res == nullptr) return false;
    const DWORD size = SizeofResource (h, res);
    HGLOBAL data = LoadResource (h, res);
    const void* bytes = data != nullptr ? LockResource (data) : nullptr;
    if (bytes == nullptr || size < 1000000) return false;
    // écrit à côté puis renommé : jamais de fichier à moitié écrit si le DAW se ferme au milieu
    const std::wstring tmp = destination + L".part";
    FILE* f = _wfopen (tmp.c_str(), L"wb");
    if (f == nullptr) return false;
    const bool ok = fwrite (bytes, 1, size, f) == size;
    fclose (f);
    if (! ok) { DeleteFileW (tmp.c_str()); return false; }
    return MoveFileExW (tmp.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

long long embeddedModelSize (void* module)
{
    auto h = (HMODULE) module;
    HRSRC res = FindResourceW (h, L"DEMUCS_MODEL", MAKEINTRESOURCEW (10));
    return res != nullptr ? (long long) SizeofResource (h, res) : -1;
}
} // namespace onirique::sep
#endif
