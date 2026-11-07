#pragma once

#include <array>
#include <cstdint>

namespace onirique::tune
{

/** Vibrato créé (indépendant de celui du chanteur). */
struct VibratoParams
{
    bool on = false;
    int shape = 0;               // 0 sinus, 1 triangle, 2 carré, 3 dent de scie
    float rateHz = 5.5f;
    float depthCents = 30.0f;    // écart de chaque côté de la note
    float delayMs = 150.0f;      // début de la note -> début du vibrato
    float onsetMs = 250.0f;      // début du vibrato -> pleine amplitude
    float variation = 0.15f;     // 0..1 : la durée et l'ampleur de chaque cycle changent un peu, comme chez un chanteur
    float amplitudeDb = 0.0f;    // le volume suit le vibrato (dB de part et d'autre)
    float timbreDb = 0.0f;       // la voix s'éclaircit vers 1,5 kHz quand le vibrato monte (dB de part et d'autre)
};

struct CorrectorParams
{
    float retuneMs = 20.0f;      // temps pour amener la voix sur la note (0 = immédiat)
    float flex = 0.0f;           // 0..1 : ne corrige qu'à l'approche de la note
    float humanize = 0.0f;       // 0..1 : correction plus lente sur la partie tenue des notes longues
    float vibrato = 0.0f;        // -1..1 : atténue ou accentue le vibrato naturel
    float transpose = 0.0f;      // demi-tons
    double a4 = 440.0;           // diapason
    std::uint16_t noteMask = 0x0fff;
    std::uint16_t skipMask = 0;  // notes ignorées : une voix dont c'est la note la plus proche passe sans correction
    bool hard = false;           // Hard Tune : notes plates et sauts instantanés, l'effet robot
    bool narrowVibrato = false;  // seuil de vibrato étroit : moins de mouvement compte pour du vibrato, les glissés gardent leur forme
    VibratoParams created;
};

/** Décide, trame après trame, de la hauteur à faire entendre, à la manière
    d'Auto-Tune (mode Auto).

    - La note visée est, en continu, la note de la gamme la plus proche de la
      voix.
    - La correction amène la voix sur cette note en Retune Speed millisecondes
      (deux pôles en cascade : départ et arrivée arrondis). Rapide, elle
      aplatit tout, vibrato compris ; lente, elle ne recentre que la note et
      laisse passer le vibrato et les inflexions. À 0, la note est imposée
      telle quelle.
    - Humanize allonge ce temps sur la partie tenue des notes longues.
    - Flex-Tune ne corrige que lorsque la voix s'approche de la note.
    - Natural Vibrato agrandit ou réduit le vibrato de la voix avant correction.

    Hard Tune : la sortie est exactement la note de la gamme, plate, et passe
    d'une note à l'autre d'un coup, sans glissé, dès que la voix franchit le
    milieu de deux notes (Auto-Tune, Retune Speed à 0).

    Comme le clavier d'Auto-Tune, une note peut être ignorée : tant que la voix
    en est le plus près, elle passe sans correction. Le seuil de vibrato (large
    ou étroit) règle ce qui compte pour du vibrato plutôt que pour un mouvement
    de la voix.

    Le vibrato créé s'ajoute ensuite, note par note, dans les deux modes ; chaque
    cycle varie un peu en durée et en ampleur. */
class PitchCorrector
{
public:
    struct Output
    {
        bool voiced = false;
        float inMidi = 0.0f;
        float outMidi = 0.0f;

        /** 1 = la sortie doit valoir exactement outMidi (note imposée) ; 0 = la
            correction est une simple transposition et la voix garde ses
            micro-variations. */
        float pin = 0.0f;

        /** Note visée dans la gamme, transposition comprise. */
        float noteMidi = 0.0f;

        /** Le vibrato créé à cet instant, de -1 à 1 (enveloppe comprise) : le volume et le timbre le suivent. */
        float vibratoShape = 0.0f;
    };

    void reset() noexcept;
    Output process (bool voiced, double frequencyHz, double dt, const CorrectorParams&) noexcept;

    static int nearestNote (double midi, std::uint16_t mask) noexcept;


private:
    void chooseNote (double judged, double hysteresis, std::uint16_t mask) noexcept;
    double createdVibrato (double dt, const VibratoParams&) noexcept;
    Output finish (double pitch, double outMidi, float pin) noexcept;

    bool active = false;
    double silentFor = 1.0e9;
    int target = 0;
    double held = 0.0;           // depuis quand la note visée n'a pas changé

    void resetCentre (double pitch) noexcept;
    void updateCentre (double pitch, double dt) noexcept;
    void varyVibrato() noexcept;

    double quick = 0.0;          // hauteur à peine lissée (gigue du détecteur ôtée)

    // Centre de la note (vibrato ôté) : moyenne de la voix sur une période de
    // vibrato, repartie de zéro dès que la voix s'en écarte trop pour que ce
    // soit du vibrato (un changement de note) : il ne traîne jamais derrière elle.
    static constexpr int centreSize = 256;
    std::array<double, centreSize> centreRing {};
    int centreWrite = 0, centreCount = 0;
    double centreSum = 0.0, centre = 0.0;
    std::array<double, 8> recent {};     // dernières valeurs de `quick` : vitesse de la voix
    int recentIndex = 0, recentCount = 0;
    double velocity = 0.0;       // vitesse signée de la voix (demi-tons par seconde)
    double velocityAt = 0.0;     // ... mesurée là (milieu de sa fenêtre)
    std::array<double, 48> riseSpeed {}, riseTravel {};   // glissé en cours, en accélérant : vitesse -> chemin fait
    int riseCount = 0;
    void rememberRise (double speed, double travel) noexcept;
    double travelAtRise (double speed) const noexcept;

    // Transitions : quand la voix quitte sa note pour une autre, la correction
    // se fige (la sortie glisse comme la voix) puis reprend à l'arrivée.
    bool gliding = false;
    double glideHold = 0.0, glideTime = 0.0, swing = 0.0, excursion = 0.0, resumeTime = 1.0, anchor = 0.0;
    int bandNote = 0, glideDirection = 1, departNote = 0;
    double bandTime = 0.0, calmTime = 0.0, glideSpeed = 0.0, stillTime = 0.0, stillFrom = 0.0;
    bool turned = false;
    double settled = 0.0;        // depuis quand la voix est posée loin du centre
    double shiftA = 0.0, shift = 0.0;     // correction en cours (demi-tons), deux pôles

    // vibrato créé
    double vibPhase = 0.0, vibTime = 0.0, vibRateScale = 1.0, vibDepthScale = 1.0, vibShapeNow = 0.0;
    std::uint32_t seed = 0x9e3779b9u;

    float lastIn = 0.0f;
    float lastOut = 0.0f;
    float lastNote = 0.0f;
};

} // namespace onirique::tune
