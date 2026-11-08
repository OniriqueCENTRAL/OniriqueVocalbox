#pragma once
#include <juce_core/juce_core.h>

/*  L'APPRENTISSAGE d’AUTO MIX : l'utilisateur choisit (menu ⋯, case désactivée au départ) d'aider VOCALBOX à progresser.
    Accepté, le plugin envoie au serveur ONIRIQUE, sans compte ni audio : le texte de la demande, le portrait de la voix
    (des mesures), la chaîne décidée, les retouches faites ensuite et la note 👍 / 👎 ; et il demande au serveur la fiche d'un
    artiste que son cerveau ne connaît pas. Refusé, rien ne part : AUTO MIX fonctionne seul, comme avant.
    Le choix est gardé pour l'ordinateur : AppData/Onirique/VocalBox/learning.txt (« 1 » ou « 0 »). */
namespace onirique::learning
{
inline juce::File file()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Onirique/VocalBox/learning.txt");
}
inline bool enabled() { return file().loadFileAsString().trim() == "1"; }
inline void setEnabled (bool on)
{
    file().getParentDirectory().createDirectory();
    file().replaceWithText (on ? "1" : "0");
}
// un identifiant d'installation tiré au hasard (pas un compte, pas une machine) : il regroupe les retours d'un même poste
inline juce::String installId()
{
    const auto f = file().getSiblingFile ("install-id.txt");
    auto id = f.loadFileAsString().trim();
    if (id.length() < 16)
    {
        id = juce::Uuid().toString();
        f.getParentDirectory().createDirectory();
        f.replaceWithText (id);
    }
    return id;
}
} // namespace onirique::learning

// Le réseau : jamais sur le fil audio ni sur celui de l'interface (8 s au plus). Les envois (artiste inconnu, retours) seulement quand
// l'apprentissage est allumé ; la lecture du cerveau publié (fetchBrain), pour tous.
namespace onirique::learning
{
inline const char* kServer = "https://onirique.store";

/** La fiche d'un artiste que le cerveau livré ne connaît pas : {"artist": {...}} ou {"unknown": true} ; vide si pas de réponse. */
inline juce::var fetchArtist (const juce::String& query)
{
    const auto url = juce::URL (juce::String (kServer) + "/api/vocalbox/artist").withParameter ("q", query);
    const auto opts = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress).withConnectionTimeoutMs (8000);
    if (auto in = url.createInputStream (opts)) return juce::JSON::parse (in->readEntireStreamAsString());
    return {};
}

/** Les retours d'un mix (anonymes, sans audio). */
inline bool postFeedback (const juce::var& payload)
{
    const auto url = juce::URL (juce::String (kServer) + "/api/vocalbox/feedback").withPOSTData (juce::JSON::toString (payload, true));
    const auto opts = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                          .withConnectionTimeoutMs (8000).withExtraHeaders ("Content-Type: application/json");
    auto in = url.createInputStream (opts);
    return in != nullptr;
}

/** Une chaîne partagée à la communauté (PARTAGER, seulement si l'apprentissage est allumé) : la demande, les artistes et
 *  les modules avec leurs réglages ; rien d'autre. Rend true si le serveur l'a reçue. */
inline bool postChain (const juce::var& payload)
{
    const auto url = juce::URL (juce::String (kServer) + "/api/vocalbox/chains").withPOSTData (juce::JSON::toString (payload, true));
    const auto opts = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                          .withConnectionTimeoutMs (8000).withExtraHeaders ("Content-Type: application/json");
    if (auto in = url.createInputStream (opts)) return juce::JSON::parse (in->readEntireStreamAsString())["ok"];
    return false;
}

/** Le dernier cerveau publié (ONR Neural Learning : le fichier livré enrichi par les retours des utilisateurs). Demandé une
 *  fois par jour au plus, à tous (rien n'est envoyé : une simple lecture, comme une mise à jour) ; vide si pas de réponse. */
inline juce::var fetchBrain()
{
    const auto stamp = file().getSiblingFile ("brain-check.txt");
    const auto now = juce::Time::currentTimeMillis();
    if (now - stamp.loadFileAsString().getLargeIntValue() < 20LL * 3600 * 1000) return {};
    stamp.getParentDirectory().createDirectory();
    stamp.replaceWithText (juce::String (now));
    const auto opts = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress).withConnectionTimeoutMs (5000);
    if (auto in = juce::URL (juce::String (kServer) + "/api/vocalbox/brain").createInputStream (opts))
        return juce::JSON::parse (in->readEntireStreamAsString());
    return {};
}

/** Un artiste trouvé par le serveur, gardé sur ce poste (artists-cache.json, lu par le cerveau) : plus besoin de le redemander. */
inline void cacheArtist (const juce::var& artist)
{
    const auto f = file().getSiblingFile ("artists-cache.json");
    auto root = juce::JSON::parse (f);
    if (! root.isObject()) root = juce::var (new juce::DynamicObject());
    juce::Array<juce::var> list;
    if (auto* a = root["artists"].getArray()) list = *a;
    for (const auto& x : list) if (x["name"].toString().equalsIgnoreCase (artist["name"].toString())) return;
    list.add (artist);
    root.getDynamicObject()->setProperty ("artists", list);
    f.getParentDirectory().createDirectory();
    f.replaceWithText (juce::JSON::toString (root));
}
} // namespace onirique::learning
