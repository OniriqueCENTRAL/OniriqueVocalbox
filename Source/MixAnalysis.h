#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <vector>

/*  CRÉER LE MIX : ce que le plugin entend d'une voix, et la chaîne qu'il en déduit.

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
    juce::StringArray understood;   // ce qui a été compris (pour le compte rendu)
    juce::StringArray unknown;      // les mots non compris (des noms d'artistes, souvent)
    juce::StringArray artists;      // les artistes reconnus par le cerveau (leur fiche est appliquée)
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
};
std::shared_ptr<const std::vector<ArtistStyle>> brainArtists();
juce::File brainFolder();
void reloadBrain();                                   // après un téléchargement ou un nouvel artiste en cache
void applyArtist (Hints&, const ArtistStyle&);
ArtistStyle artistFromVar (const juce::var&);        // une fiche lue dans le JSON (du cerveau ou du serveur)

struct Setting
{
    int type = -1;
    int instance = 0;                                     // exemplaire (deux SATURATION : préampli et couleur)
    std::vector<std::pair<juce::String, float>> params;   // identifiants du premier exemplaire, valeurs réelles
};

struct Plan
{
    std::vector<Setting> modules;     // dans l'ordre de la chaîne
    float inputDb = 0.0f;             // gain d'entrée : la voix amenée au niveau d'une voix mixée
    juce::StringArray report;         // ce qui a été entendu et décidé, en clair
};

// source : la prise (peut être invalide : pas de lecture) ; target : la voix de référence (peut être invalide)
Plan buildPlan (const Profile& source, const Profile& target, const Hints& hints);
} // namespace onirique::mix
