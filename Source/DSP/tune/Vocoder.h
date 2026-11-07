#pragma once

#include <array>
#include <cstdint>

namespace onirique::tune
{

/** Vocodeur à banc de filtres, sans latence.

    La voix (le "modulateur") est découpée en 32 bandes dont on suit le
    niveau ; un synthétiseur interne (la "porteuse") passe par les mêmes
    bandes, et chacune est ramenée au niveau de la bande correspondante de la
    voix. Le synthé prend ainsi l'articulation et le timbre de la voix, tout
    en jouant exactement les notes qu'on lui donne.

    Ici ces notes ne viennent pas d'un clavier : le moteur les déduit de la
    voix elle-même (voir TuneEngine). Pour que les consonnes restent
    compréhensibles, la porteuse devient du bruit dès que la voix n'est plus
    une note. */
class Vocoder
{
public:
    static constexpr int numBands = 32;
    static constexpr int maxNotes = 3;
    static constexpr int numVoicings = 4;
    static constexpr const char* voicingNames[numVoicings] = { "Mono", "Octave", "Fifth", "Chord" };

    void prepare (double sampleRate);
    void reset() noexcept;

    /** Ce que joue la porteuse autour de la note chantée. `harmonyMask` dit
        quelles notes appartiennent à la tonalité (bit 0 = do) : l'accord en
        tient compte pour choisir sa tierce et sa quinte. */
    void setVoicing (int voicing, std::uint16_t harmonyMask) noexcept;

    /** Temps de glissement d'une note à la suivante, en secondes. */
    void setGlide (double seconds) noexcept;

    /** Note à jouer (MIDI, la4 = `a4` Hz). `jump` : sans glissement. */
    void setNote (double midi, double a4, bool jump) noexcept;

    /** false = consonne ou souffle : la porteuse passe au bruit. */
    void setVoiced (bool isVoiced) noexcept { voiced = isVoiced; }

    /** Hauteur réellement chantée, en Hz : elle fixe l'écart entre les
        partiels de la voix, donc la finesse du relevé de son timbre. */
    void setVoiceFrequency (double hz) noexcept { voiceHz = hz < 40.0 ? 40.0 : (hz > 2000.0 ? 2000.0 : hz); }

    /** Un échantillon. En mono, `left` et `right` reçoivent la même valeur. */
    void process (float modulator, bool stereo, float& left, float& right) noexcept;

private:
    struct Bank
    {
        std::array<float, numBands> z1a {}, z2a {}, z1b {}, z2b {};
    };

    void runBank (Bank&, float input, float* output) noexcept;
    void updateChord() noexcept;
    void updatePitch() noexcept;
    void updateReach (double lowestCarrierHz) noexcept;
    void updateGains() noexcept;
    float nextNoise (std::uint32_t& state) noexcept;

    double fs = 48000.0;

    // Banc de filtres : passe-bande d'ordre 4 (deux étages identiques)
    std::array<float, numBands> b0 {}, a1 {}, a2 {};
    std::array<float, numBands> attack {}, release {};
    std::array<float, numBands> envMod {}, envCar {};
    Bank modBank, leftBank, rightBank;
    float carrierCoef = 0.001f;

    // Gain appliqué à chaque bande de la porteuse (signe compris)
    std::array<float, numBands> bandGain {}, bandGainStep {};
    int gainInterval = 4, gainCountdown = 0;
    bool wide = false;

    // Garde-fou de niveau : le vocodeur ne sort jamais plus fort que la voix
    float voicePower = 0.0f, outPower = 0.0f, voicePeak = 0.0f, ceiling = 1.0f;
    float powerCoef = 0.01f, ceilingDown = 0.01f, ceilingUp = 0.001f, peakRelease = 0.001f;

    // Lissage des deux relevés de niveau le long des fréquences (voir updateGains)
    std::array<int, numBands> reach {};
    int voiceFloorBand = 0, carrierFloorBand = 0;
    double bandStep = 0.15, voiceHz = 200.0;

    // Porteuse : deux dents de scie légèrement désaccordées par note
    int voicing = 0, noteCount = 1;
    std::uint16_t harmony = 0x0fff;
    std::array<int, maxNotes> intervals { 0, 0, 0 };
    std::array<float, maxNotes> levels { 1.0f, 0.0f, 0.0f };
    std::array<double, maxNotes> phaseA {}, phaseB {}, stepA {}, stepB {};
    float carrierGain = 1.0f;

    double targetMidi = 57.0, currentMidi = 57.0, tuning = 440.0;
    double glideCoef = 1.0;
    int pitchCountdown = 0;

    bool voiced = false;
    float breath = 1.0f, breathCoef = 0.01f;       // 0 = dents de scie, 1 = bruit
    std::uint32_t noiseLeft = 0x12345678u, noiseRight = 0x9e3779b9u;
};

} // namespace onirique::tune
