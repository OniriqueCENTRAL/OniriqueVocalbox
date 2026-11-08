#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <vector>

/*  AUTO MIX : ce que le plugin entend d'une voix, et la chaîne qu'il en déduit.

    - Profile : le portrait d'une voix (timbre en tiers d'octave, dynamique, sifflantes, souffle, plosives, réverbe et
      échos autour d'elle). Il se mesure sur la prise de l'utilisateur (10 s) et sur la voix d'un morceau de référence.
    - isolateVoice : la voix d'un morceau stéréo, prise au centre du mix (ce qui est identique à gauche et à droite,
      entre 90 Hz et 16 kHz). Une séparation par IA prendra sa place à l'étape suivante.
    - Hints : ce que dit la barre de recherche (styles, couleurs, effets), compris dans le plugin, en français et en anglais.
    - buildPlan : la chaîne et ses réglages ; les écarts entre la prise et la cible donnent l'EQ, la compression, le
      de-esser... ; la référence (ou le style demandé) donne l'espace (réverbe, delay) et la texture. */
namespace onirique::mix
{
constexpr int kBands = 31;
// centres des tiers d'octave, 20 Hz .. 20 kHz
constexpr float kCentres[kBands] { 20.0f, 25.0f, 31.5f, 40.0f, 50.0f, 63.0f, 80.0f, 100.0f, 125.0f, 160.0f, 200.0f, 250.0f, 315.0f,
                                   400.0f, 500.0f, 630.0f, 800.0f, 1000.0f, 1250.0f, 1600.0f, 2000.0f, 2500.0f, 3150.0f, 4000.0f,
                                   5000.0f, 6300.0f, 8000.0f, 10000.0f, 12500.0f, 16000.0f, 20000.0f };

struct Profile
{
    bool valid = false;
    double seconds = 0.0;
    std::array<float, kBands> bands {};   // dB, la moyenne 200 Hz .. 5 kHz ramenée à 0
    float loudDb = -60.0f;                // fenêtres de 400 ms : 90e centile (les phrases fortes)
    float softDb = -60.0f;                // 10e centile des fenêtres actives
    float dynRange = 0.0f;                // loudDb - softDb
    float peak50Db = -60.0f;              // fenêtres de 50 ms : 95e centile (ce que voit un compresseur rapide)
    float noiseFloorDb = -90.0f;          // le fond entre les phrases
    float floorSpreadDb = 20.0f;          // écart-type du fond : faible = souffle stable, fort = queues de réverbe / respirations
    float sibilance = -30.0f;             // part des aigus (4,5 .. 10 kHz) dans les trames les plus sifflantes, dB
    float sibFreq = 6000.0f;              // où sifflent les S
    float plosivesPerMin = 0.0f;          // bouffées de graves (P, B)
    float rt60 = 0.0f;                    // réverbe estimée après la fin des phrases, s (0 : sèche)
    float wetDb = -60.0f;                 // niveau de la queue 120 ms après la fin d'une phrase, par rapport à la phrase
    float echoMs = 0.0f, echoAmount = 0.0f;   // écho répété après les phrases
    // voix prise au centre d'un mix (pas séparée) : la batterie et la basse y restent ; on n'en garde que le timbre au-dessus de
    // 200 Hz et l'espace (la dynamique et les sifflantes y sont faussées par la musique)
    bool fromMix = false;
    // 0 : parlé / rappé, 1 : chanté (la part du temps où la voix tient des notes : hauteur stable sur plus de 150 ms)
    float sung = 0.0f;
    // les résonances : des pics étroits qui dépassent le spectre lissé de la voix (dureté, nasalité, pièce, micro)
    int numRes = 0;
    std::array<float, 4> resFreq {}, resDb {}, resQ {};
    // les bruits de bouche et les respirations (les détecteurs mêmes de DE-CLICK et DE-BREATH, passés sur la prise)
    float clicksPerMin = 0.0f;
    float breathsPerMin = 0.0f, breathDb = -60.0f;   // leur niveau par rapport aux phrases
    // la justesse et la tonalité (notes tenues) : écart moyen à la note la plus proche, tonique 0..11 (do = 0), mineur ou majeur
    float pitchDevCents = 0.0f;
    float noteMidi = 0.0f;        // la hauteur moyenne des notes tenues (le registre : soprano, alto / ténor, baryton)
    int keyRoot = -1;
    bool keyMinor = false;
    float keyConfidence = 0.0f;   // 0 .. 1
};

// le portrait d'une voix mono
Profile analyse (const float* mono, int numSamples, double sampleRate);

// la voix d'un morceau (stéréo : le centre du mix ; mono : tel quel) ; shouldStop est appelé de temps en temps
std::vector<float> isolateVoice (const juce::AudioBuffer<float>& song, std::function<bool()> shouldStop = {});

// la voix d'un morceau séparée par l'IA (demucs.cpp, dans le plugin, hors ligne) : un extrait de maxSeconds pris à 30 % du
// morceau, rendu en mono à 44,1 kHz ; false si le modèle manque ou si shouldContinue rend false (progression 0..1)
bool separateVoice (const juce::AudioBuffer<float>& song, double sampleRate, std::vector<float>& voice, double& voiceRate,
                    std::function<bool (float)> shouldContinue, double maxSeconds = 40.0);
juce::File modelFile();   // le modèle de séparation livré avec le plugin (invalide s'il manque)

// lit un morceau (au plus maxSeconds), rend false si le format n'est pas lisible
bool readSong (const juce::File&, juce::AudioBuffer<float>& out, double& sampleRate, double maxSeconds = 360.0);
bool canRead (const juce::File&);

struct Hints
{
    float bright = 0.0f, air = 0.0f, warm = 0.0f, dark = 0.0f;   // 0 .. 1
    float sung = 0.0f;                    // la demande parle de chant (0 .. 1), ajouté à ce que la prise montre
    float comp = 0.0f;           // -1 (naturel) .. +1 (très en avant)
    float reverb = -1.0f;        // -1 : non dit ; 0 .. 1
    bool reverbExplicit = false; // l'espace a été demandé en toutes lettres (sinon la référence décide)
    float reverbSize = -1.0f;    // -1 : non dit ; 0 (pièce) .. 1 (grand espace)
    bool dreamy = false;
    float delay = -1.0f;         // -1 : non dit ; 0 .. 1
    float sat = 0.0f;            // 0 .. 1
    int satType = -1;            // -1 : au choix ; 0 tube, 1 analog, 2 tape, 3 transformer
    float phone = 0.0f, doubler = 0.0f, chorus = 0.0f, flanger = 0.0f;
    bool controlled = false;     // « bien contrôlé » : MULTIBAND
    bool parallel = false;       // densité, punch : COMPRESSION PARALLÈLE
    float preamp = 0.0f;         // PREAMP N73 en tête (0 : aucun ; 0..1 : son gain) : les artistes qui en ont un à l'enregistrement
    bool preampMic = false;      // entrée MIC (grain plus marqué) plutôt que LINE
    juce::String preampOf;       // l'artiste d'où vient le préampli (compte rendu)
    int peakComp = -1;           // le compresseur des crêtes : -1 au choix, 0 le 1176, 1 le CL1B
    bool fast = false;           // rap moderne : attaques et relâchements plus rapides
    bool rapStyle = false;       // rap, drill, trap (demandé, ou le genre de l'artiste) : le 1176 tient les crêtes, même sur une voix mélodique
    float tune = 0.0f;           // TUNER : -1 refusé (« sans autotune »), 0 au choix, 0..1 de la justesse naturelle (0,3) à l'effet robot (1)
    juce::String tuneOf;         // l'artiste d'où vient l'accordage (compte rendu)
    bool cleanClicks = false, cleanBreath = false, cleanRoom = false;   // demandés en toutes lettres
    int role = 0;                // LEAD, BACKS, ADLIBS, DOUBLES (le rôle choisi dans VOCALBOX)
    int sharedKey = -1;          // la tonalité trouvée par une autre instance de VOCALBOX du projet (0..23 : majeur, +12 mineur)
    juce::StringArray understood;   // ce qui a été compris (pour le compte rendu)
    juce::StringArray unknown;      // les mots non compris (des noms d'artistes, souvent)
    juce::StringArray artists;      // les artistes reconnus par le cerveau (leur fiche est appliquée)
    juce::StringArray unsure;       // ceux dont la fiche est estimée ou trouvée par le serveur (AUTO MIX les connaît moins bien)
    // la voix mesurée du premier artiste demandé qui en a une (cerveau : « voice ») : sert de cible quand aucune référence n'est glissée
    float reverbBias = 0.0f, delayBias = 0.0f;   // appris sur toutes les demandes (ONR Neural Learning)
    juce::String voiceOf;
    std::array<float, kBands> voiceBands {};   // dB, comme Profile::bands ; -100 : pas mesuré (hors 160 Hz .. 8 kHz)
    float voiceDyn = 0.0f, voiceRt60 = 0.0f, voiceWet = -60.0f, voiceEcho = 0.0f;
};

Hints parseHints (const juce::String& text);

/*  LE CERVEAU : la façon de mixer la voix de chaque artiste. Il est livré dans le plugin (Resources/vocalbox-brain.json) ;
    une version plus récente téléchargée (AppData/Onirique/VocalBox/brain.json) prend sa place, et les artistes trouvés par le
    serveur ONIRIQUE s'y ajoutent (artists-cache.json). Un nom d'artiste reconnu dans la demande applique sa fiche, sur les
    mêmes échelles que les mots de la barre de recherche. */
struct ArtistStyle
{
    juce::String name, genre;
    juce::StringArray aliases;   // en minuscules, sans accents
    juce::var hints;             // bright, air, warm, dark, comp, reverb, reverbSize, delay, sat, satType, doubler, chorus, flanger, phone, controlled, parallel, dreamy
    juce::var voice;             // facultatif : la voix mesurée sur une référence (bands, dyn, rt60, wet, echo)
    bool estimate = false;       // fiche estimée (pas mesurée, pas confirmée par un ingé son)
    bool fromServer = false;     // fiche trouvée par le serveur (artists-cache.json) ou ajoutée par ONR Neural Learning
};
std::shared_ptr<const std::vector<ArtistStyle>> brainArtists();
juce::File brainFolder();
void reloadBrain();
juce::var brainCorrections();                     // « corrections » du cerveau en service
bool installBrainIfNewer (const juce::var& brain);  // un cerveau téléchargé : gardé (brain.json) et chargé s'il est plus récent                                   // après un téléchargement ou un nouvel artiste en cache
void applyArtist (Hints&, const ArtistStyle&);
ArtistStyle artistFromVar (const juce::var&);        // une fiche lue dans le JSON (du cerveau ou du serveur)

struct Setting
{
    int type = -1;
    int instance = 0;                                     // exemplaire (deux SATURATION : préampli et couleur)
    std::vector<std::pair<juce::String, float>> params;   // identifiants du premier exemplaire, valeurs réelles
    juce::String why;                                     // pourquoi AUTO MIX l'a mis (sa ligne du compte rendu)
    int repAt = -1;                                       // (buildPlan) la taille du compte rendu quand il a été ajouté
};

struct Plan
{
    std::vector<Setting> modules;     // dans l'ordre de la chaîne
    float inputDb = 0.0f;             // gain d'entrée : la voix amenée au niveau d'une voix mixée
    std::vector<std::pair<juce::String, float>> globals;   // réglages généraux : macros remises à zéro, OUTPUT et panoramique du rôle
    int keyFound = -1;                // la tonalité retenue pour le TUNER (0..11 majeur, 12..23 mineur), partagée aux autres VOCALBOX
    float keyConfidence = 0.0f;
    juce::StringArray report;         // ce qui a été entendu et décidé, en clair
};

// source : la prise (peut être invalide : pas de lecture) ; target : la voix de référence (peut être invalide)
Plan buildPlan (const Profile& source, const Profile& target, const Hints& hints);

/*  AFFINER EN MOTS, après le mix : « un peu moins de réverbe », « plus en avant », « plus sombre », « moins d'autotune ».
    Rien n'est reconstruit : les macros bougent (PRÉSENCE, CHALEUR, TENUE, ESPACE, en points de -100 .. +100), et deux réglages
    précis (le retour du DELAY, la vitesse du TUNER) sont multipliés. « un peu » divise par deux, « beaucoup » / « très » x 1,5 ;
    « moins », « trop » ou « pas assez » donnent le sens. */
struct Refine
{
    float presence = 0.0f, warmth = 0.0f, hold = 0.0f, space = 0.0f;
    float delayMul = 1.0f, retuneMul = 1.0f, breathDb = 0.0f;
    juce::StringArray understood;
    bool any() const noexcept { return ! understood.isEmpty(); }
};
Refine parseRefine (const juce::String& text);

/*  ONR NEURAL LEARNING, les compléments (ils s'ajoutent à ce qui précède, sans rien remplacer) :
    - nearestVoice : la voix mesurée du cerveau la plus proche de la prise (timbre 160 Hz .. 8 kHz et tenue) ;
    - communityChain : la chaîne de la communauté validée par les fondateurs pour un des artistes demandés (corrections.chains) ;
    - applyLearnedModel : le petit modèle appris sur les mix gardés (corrections.model), validé par les fondateurs ; il ne fait
      que de petites retouches (±0,15 au plus) des couleurs, après le cerveau ; sans modèle publié, rien ne change ;
    - modelFeatures : les mesures que le modèle lit (les mêmes que le serveur, brainlearn.js). */
struct VoiceMatch { juce::String artist; float distance = 99.0f; };
VoiceMatch nearestVoice (const Profile& source);
juce::var communityChain (const juce::StringArray& artists, const juce::String& request = {});   // ou un style de la communauté nommé dans la demande
juce::StringArray styleWords (const juce::String& request);   // les mots de style compris (sans les artistes)
std::array<float, 8> modelFeatures (const Profile&);
bool applyLearnedModel (Hints&, const Profile&);
} // namespace onirique::mix
