#pragma once

#include "PitchCorrector.h"
#include "PitchDetector.h"
#include "SincTable.h"
#include "Vocoder.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace onirique::tune
{

struct EngineParams
{
    bool hq = false;                     // false = LIVE (sans latence), true = HQ
    int inputType = 1;                   // voir TuneEngine::inputTypeNames
    std::uint16_t noteMask = 0x0fff;     // classes de hauteur autorisées (bit 0 = do)
    std::uint16_t skipMask = 0;          // notes ignorées : la voix qui en est le plus près passe sans correction
    float retuneMs = 20.0f;
    bool hardTune = false;               // effet robot : notes plates, sauts francs
    bool narrowVibrato = false;          // seuil de vibrato étroit (voir CorrectorParams)
    float flex = 0.0f;                   // 0..1
    float humanize = 0.0f;               // 0..1
    float vibrato = 0.0f;                // -1..1
    VibratoParams createdVibrato;        // vibrato ajouté (bouton VIBRATO)
    bool formant = false;                // true = formants préservés, false = son classique
    float throat = 1.0f;                 // longueur de gorge relative (1 = neutre)
    float transpose = 0.0f;              // demi-tons
    float detuneCents = 0.0f;
    float tracking = 0.5f;               // 0..1
    float mix = 1.0f;                    // 0..1
    bool bypass = false;

    bool vocoder = false;
    int vocoderVoicing = 0;              // voir Vocoder::voicingNames
    float vocoderBlend = 1.0f;           // 0 = voix accordée seule, 1 = vocodeur seul
    std::uint16_t harmonyMask = 0x0ab5;  // tonalité dans laquelle le vocodeur construit ses accords
};

/** Une mesure destinée à l'affichage (hauteur entendue / hauteur corrigée). */
struct DisplayFrame
{
    double time = 0.0;        // instant décrit par la mesure, en secondes depuis le démarrage
    float inMidi = 0.0f;
    float outMidi = 0.0f;
    float noteMidi = 0.0f;    // note visée dans la gamme (celle que joue le vocodeur)
    float clarity = 0.0f;     // 1 = voix parfaitement périodique
    float level = 0.0f;       // niveau efficace de la voie d'analyse
    bool voiced = false;
};

/** Note déduite de la voix, à transmettre telle quelle en MIDI. */
struct NoteEvent
{
    int sampleOffset = 0;     // position dans le bloc en cours de traitement
    int note = 60;
    int velocity = 100;
    bool on = false;
};

/** Moteur d'accordage : détection de hauteur, choix de la note, puis
    transposition par grains synchrones à la période (PSOLA).

    Deux façons de placer les grains, donc deux modes :
    - LIVE : la position de chaque grain est prédite à partir des périodes déjà
      entendues ; aucun échantillon d'avance, donc aucune latence déclarée.
    - HQ : la voix est retardée, les périodes sont mesurées de part et d'autre
      de chaque grain et la courbe de hauteur est lissée avec de l'avance.

    Les notes choisies servent aussi à un vocodeur intégré : la voix est
    convertie en notes, qui pilotent un synthétiseur auquel la voix donne
    ensuite son articulation.

    Indépendant de JUCE pour pouvoir être testé seul (voir Tests/). */
class TuneEngine
{
public:
    static constexpr int maxChannels = 2;
    static constexpr int numInputTypes = 5;
    static constexpr const char* inputTypeNames[numInputTypes] = { "Soprano", "Alto / Tenor", "Baritone / Bass", "Instrument", "Bass Inst" };
    static constexpr int maxNoteEvents = 256;

    void prepare (double sampleRate);
    void reset() noexcept;
    void setParams (const EngineParams&) noexcept;

    int getLatencySamples() const noexcept { return latency; }
    static int latencyFor (double sampleRate, bool hq, int inputType) noexcept;

    void process (float* const* channels, int numChannels, int numSamples) noexcept;

    /** Notes produites pendant le dernier appel à process() (vocodeur actif). */
    const NoteEvent* getNoteEvents() const noexcept { return noteEvents.data(); }
    int getNumNoteEvents() const noexcept { return numNoteEvents; }

    /** À appeler depuis l'interface : dépile une mesure, false s'il n'y en a plus. */
    bool popDisplayFrame (DisplayFrame&) noexcept;

private:
    struct Biquad
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0, z1 = 0.0, z2 = 0.0;

        double process (double x) noexcept
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }

        void setLowpass (double sampleRate, double frequency, double q) noexcept;
        void setHighpass (double sampleRate, double frequency, double q) noexcept;
        void setPeak (double sampleRate, double frequency, double q, double gainDb) noexcept;
        void clear() noexcept { z1 = z2 = 0.0; }
    };

    /** Mesure brute du détecteur, datée au centre de sa fenêtre. */
    struct Raw
    {
        double time = 0.0;
        float period = 0.0f;
        float clarity = 0.0f;
        float level = 0.0f;
        bool voiced = false;
    };

    /** Consigne de hauteur : période entendue et période à produire. */
    struct Control
    {
        double time = 0.0;
        float period = 0.0f;
        float outPeriod = 0.0f;
        float slope = 0.0f;       // pente de log2(période) par échantillon (mode LIVE)
        float pin = 0.0f;         // 1 = sortie imposée, 0 = simple transposition
        float noteMidi = 0.0f;    // note visée dans la gamme
        float level = 0.0f;
        float vibrato = 0.0f;     // vibrato créé, de -1 à 1 : le volume et le timbre le suivent
        bool voiced = false;
    };

    /** Période réelle de la voix là où un grain va lire. */
    struct Local
    {
        double period = 0.0;
        std::int64_t markIndex = -1;
        bool aligned = false;
    };

    /** Repère de période dans la voix d'entrée (mode HQ). */
    struct Mark
    {
        double pos = 0.0;
        float period = 0.0f;
        bool voiced = false;
    };

    struct Grain
    {
        bool active = false;
        bool chained = false;     // fenêtres complémentaires avec les grains voisins
        bool integer = true;      // relu tel quel, sans interpolation
        bool steered = false;     // LIVE classique : vitesse réajustée en cours de route
        double centre = 0.0;
        double start = 0.0;
        double fadeIn = 1.0;
        double fadeOut = 1.0;
        double source = 0.0;      // position dans l'entrée lue au centre du grain
        double position = 0.0;    // position lue pour l'échantillon en cours
        double speed = 1.0;       // vitesse de relecture
        std::int64_t offset = 0;
        float gain = 1.0f;
        const SincTable* table = nullptr;
    };

    static constexpr int rawSize = 64;
    static constexpr int controlSize = 256;
    static constexpr int markSize = 1024;
    static constexpr int maxGrains = 24;
    static constexpr int displaySize = 1024;
    static constexpr int hannSize = 2048;

    void configure() noexcept;
    void restart() noexcept;

    void analyseFrame() noexcept;
    Raw smoothedRaw (std::int64_t index) const noexcept;
    void emitControl (const Raw&) noexcept;

    void advanceMarks() noexcept;
    void launchGrains() noexcept;
    void startGrain (double centre, bool voiced, bool preserve, const Local&, double tout, double half) noexcept;
    void steerLiveGrains() noexcept;
    Local resolveLocal (const Control&, double tin, double centre) noexcept;
    double predictedPeriod (const Control&, double guess, double when) const noexcept;
    static double outputPeriod (const Control&, double tin, double pinned, double local) noexcept;
    void renderGrains (float* wet, int numCh) noexcept;

    void followNotes (int sampleOffset) noexcept;
    void applyNote (const Control&, int sampleOffset) noexcept;
    void releaseNote (int sampleOffset) noexcept;
    void pushNoteEvent (int sampleOffset, int note, int velocity, bool on) noexcept;

    Control lookupControl (double time) noexcept;
    Control newestControl() const noexcept;
    std::int64_t nearestRawIndex (double time) noexcept;
    std::int64_t nearestMark (double time) noexcept;

    bool measureRecentPeriod (double guess, double& period, double& centreTime) const noexcept;
    float periodSlope() const noexcept;
    bool measureForwardPeriod (double position, double guess, double& period) const noexcept;
    double energyPeak (std::int64_t from, int span, int box) const noexcept;
    double centreOnPulse (double source, double period, double limit, bool reseed) const noexcept;

    double timeOfAnalysisIndex (double index) const noexcept { return index * (double) decim + (double) (decim - 1) - analysisDelay; }
    double analysisIndexOfTime (double time) const noexcept { return (time - (double) (decim - 1) + analysisDelay) / (double) decim; }

    const Raw& rawAt (std::int64_t i) const noexcept { return rawRing[(size_t) (i & (rawSize - 1))]; }
    const Control& controlAt (std::int64_t i) const noexcept { return controlRing[(size_t) (i & (controlSize - 1))]; }
    const Mark& markAt (std::int64_t i) const noexcept { return markRing[(size_t) (i & (markSize - 1))]; }
    float analysisAt (std::int64_t i) const noexcept { return analysisRing[(size_t) (i & analysisMask)]; }
    float hann (double x) const noexcept;

    // --- configuration
    EngineParams params;               // ce qui est demandé
    bool hq = false;                   // ce qui tourne (le changement de mode se fait en fondu)
    int inputType = 1;
    CorrectorParams correctorParams;
    double fs = 44100.0, fa = 22050.0;
    int decim = 2, hopFs = 128, latency = 0;
    double minPeriod = 16.0, maxPeriod = 1024.0, unvoicedPeriod = 220.0, analysisDelay = 0.0;
    double formantSpeed = 1.0;
    bool prepared = false;

    // --- signal
    std::array<std::vector<float>, maxChannels> inRing;
    std::int64_t inMask = 0, now = 0;
    std::vector<float> analysisRing;
    std::int64_t analysisMask = 0, analysisCount = 0;
    Biquad highpass, lowpassA, lowpassB;
    int decimPhase = 0, hopCounter = 0;
    float mixNow = 1.0f, mixCoef = 0.01f;
    float fade = 1.0f, fadeStep = 0.01f;       // fondu de sortie autour d'un changement de mode
    bool reconfigurePending = false;

    // --- hauteur
    PitchDetector detector;
    PitchCorrector corrector;
    std::array<Raw, rawSize> rawRing;
    std::int64_t rawCount = 0, rawCursor = 0;
    std::array<Control, controlSize> controlRing;
    std::int64_t controlCount = 0, controlCursor = 0;
    float loudest = 0.0f, loudestDecay = 0.999f;   // niveau récent de la voix, référence du seuil

    // --- repères et grains
    std::array<Mark, markSize> markRing;
    std::int64_t markCount = 0, markCursor = 0;
    double markWake = 0.0;
    std::array<Grain, maxGrains> grains;
    Grain* lastGrain = nullptr;
    double lastCentre = 0.0, lastTout = 220.0, lastSource = 0.0;
    std::int64_t lastMarkIndex = -1;
    bool sourceValid = false;

    SincBank sincLive, sincHq;
    const SincBank* sinc = nullptr;
    std::vector<float> hannTable;

    // --- le volume et le timbre qui suivent le vibrato créé
    std::array<Biquad, maxChannels> vibratoTone;
    float vibratoNow = 0.0f, vibratoShown = 0.0f;
    int vibratoToneLeft = 0;

    // --- vocodeur et notes
    Vocoder vocoder;
    bool vocoderRunning = false;
    float vocoderNow = 0.0f;
    std::int64_t noteCursor = 0;
    int heldNote = -1, candidateNote = -1, candidateFrames = 0, silentFrames = 0, noteHangFrames = 40;
    int pendingNoteOff = -1;
    std::array<NoteEvent, maxNoteEvents> noteEvents;
    int numNoteEvents = 0;

    // --- affichage (un producteur : l'audio ; un consommateur : l'interface)
    std::array<DisplayFrame, displaySize> displayRing;
    std::atomic<int> displayWrite { 0 }, displayRead { 0 };
};

} // namespace onirique::tune
