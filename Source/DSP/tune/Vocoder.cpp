#include "Vocoder.h"

#include <algorithm>
#include <cmath>

namespace onirique::tune
{

namespace
{
   #if defined (_MSC_VER)
    #define ONIRIQUE_RESTRICT __restrict
   #else
    #define ONIRIQUE_RESTRICT __restrict__
   #endif

    constexpr double pi = 3.14159265358979323846;
    constexpr double lowestBand = 100.0;
    constexpr double highestBand = 11000.0;
    constexpr int pitchInterval = 16;            // la hauteur de la porteuse est recalculée tous les 16 échantillons
    constexpr double detuneSemitones = 0.07;     // écart entre les deux dents de scie d'une même note
    constexpr float crossPan = 0.35f;            // part de chaque oscillateur envoyée de l'autre côté

    // Une bande où la porteuse n'a presque rien à offrir (sous sa
    // fondamentale, par exemple) ne doit pas être amplifiée à l'infini.
    constexpr float carrierFloor = 0.015f;
    constexpr float maxBandGain = 24.0f;

    constexpr float voicedBreath = 0.2f;         // un peu de bruit en permanence : il donne l'air des aigus

    // Réglage de niveau, pour que le vocodeur sorte aussi fort que la voix
    // qui le pilote. Le bruit des consonnes s'additionne moins bien d'une
    // bande à l'autre qu'une note : il reçoit un supplément.
    constexpr float outputTrim = 1.08f;
    constexpr float consonantBoost = 0.6f;

    // En stéréo chaque côté porte sa propre porteuse à plein niveau ; en mono
    // les deux sont réunies et se renforcent moins. Même niveau perçu au final.
    constexpr float stereoTrim = 0.76f;

    // Puissance que le vocodeur peut atteindre par rapport à la voix (+1 dB)
    constexpr float loudnessMargin = 1.26f;

    // Crête que le vocodeur peut atteindre par rapport à celle de la voix (+1 dB)
    constexpr float peakMargin = 1.12f;

    /** Résidu qui rend une dent de scie numérique propre (polyBLEP). */
    inline double blep (double t, double dt) noexcept
    {
        if (t < dt)
        {
            const double x = t / dt;
            return x + x - x * x - 1.0;
        }

        if (t > 1.0 - dt)
        {
            const double x = (t - 1.0) / dt;
            return x * x + x + x + 1.0;
        }

        return 0.0;
    }

    inline bool inScale (std::uint16_t mask, int note) noexcept
    {
        return (mask & (1u << (((note % 12) + 12) % 12))) != 0;
    }
}

void Vocoder::prepare (double sampleRate)
{
    fs = sampleRate;

    const double top = std::min (highestBand, 0.42 * fs);
    const double ratio = std::pow (top / lowestBand, 1.0 / (double) (numBands - 1));

    // Deux bandes voisines se croisent à -3 dB ; deux étages en série
    // resserrent chaque filtre, d'où le facteur 0,644.
    const double q = 0.6436 / (std::sqrt (ratio) - 1.0 / std::sqrt (ratio));
    bandStep = std::log (ratio);

    for (int b = 0; b < numBands; ++b)
    {
        const double frequency = lowestBand * std::pow (ratio, (double) b);
        const double w0 = 2.0 * pi * frequency / fs;
        const double alpha = std::sin (w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;

        b0[(size_t) b] = (float) (alpha / a0);
        a1[(size_t) b] = (float) (-2.0 * std::cos (w0) / a0);
        a2[(size_t) b] = (float) ((1.0 - alpha) / a0);

        // Suivi du niveau : montée rapide, descente sur environ trois
        // périodes de la bande (assez lent pour ne pas suivre chaque cycle
        // de la voix, assez vif pour garder l'articulation).
        const double releaseSeconds = std::min (0.03, std::max (0.008, 3.0 / frequency));
        attack[(size_t) b] = (float) (1.0 - std::exp (-1.0 / (0.0015 * fs)));
        release[(size_t) b] = (float) (1.0 - std::exp (-1.0 / (releaseSeconds * fs)));
    }

    carrierCoef = (float) (1.0 - std::exp (-1.0 / (0.02 * fs)));
    gainInterval = std::max (1, (int) std::lround (fs / 12000.0));
    powerCoef = (float) (1.0 - std::exp (-1.0 / (0.004 * fs)));
    ceilingDown = (float) (1.0 - std::exp (-1.0 / (0.001 * fs)));
    ceilingUp = (float) (1.0 - std::exp (-1.0 / (0.05 * fs)));
    peakRelease = (float) (1.0 - std::exp (-1.0 / (0.08 * fs)));
    breathCoef = (float) (1.0 - std::exp (-1.0 / (0.004 * fs)));

    setGlide (0.01);
    updateChord();
    reset();
}

void Vocoder::reset() noexcept
{
    modBank = {};
    leftBank = {};
    rightBank = {};
    envMod.fill (0.0f);
    envCar.fill (1.0f);         // la porteuse est d'abord supposée forte : le vocodeur entre en douceur
    bandGain.fill (0.0f);
    bandGainStep.fill (0.0f);
    gainCountdown = 0;
    voicePower = 0.0f;
    outPower = 0.0f;
    voicePeak = 0.0f;
    ceiling = 1.0f;
    phaseA.fill (0.0);
    phaseB.fill (0.37);
    currentMidi = targetMidi;
    pitchCountdown = 0;
    breath = 1.0f;
    voiced = false;
}

void Vocoder::setVoicing (int newVoicing, std::uint16_t harmonyMask) noexcept
{
    newVoicing = std::max (0, std::min (numVoicings - 1, newVoicing));
    if (newVoicing == voicing && harmonyMask == harmony)
        return;

    voicing = newVoicing;
    harmony = harmonyMask;
    updateChord();
}

void Vocoder::setGlide (double seconds) noexcept
{
    const double tau = std::max (0.0015, seconds);
    glideCoef = 1.0 - std::exp (-(double) pitchInterval / (tau * fs));
}

void Vocoder::setNote (double midi, double a4, bool jump) noexcept
{
    const bool rootChanged = (int) std::lround (midi) != (int) std::lround (targetMidi);

    targetMidi = midi;
    tuning = a4;
    if (jump)
        currentMidi = midi;

    // La tierce et la quinte de l'accord dépendent de la note de départ.
    if (rootChanged && voicing == 3)
        updateChord();
}

void Vocoder::updateChord() noexcept
{
    const int root = (int) std::lround (targetMidi);

    intervals = { 0, 0, 0 };
    levels = { 1.0f, 0.0f, 0.0f };
    noteCount = 1;

    if (voicing == 1)            // la note, l'octave du dessous et celle du dessus
    {
        intervals = { 0, -12, 12 };
        levels = { 1.0f, 0.7f, 0.45f };
        noteCount = 3;
    }
    else if (voicing == 2)       // la note et sa quinte
    {
        intervals = { 0, 7, 0 };
        levels = { 1.0f, 0.75f, 0.0f };
        noteCount = 2;
    }
    else if (voicing == 3)       // accord de trois sons pris dans la tonalité
    {
        const bool minorThird = inScale (harmony, root + 3);
        const bool majorThird = inScale (harmony, root + 4);

        int third = 0;
        if (majorThird)
            third = 4;
        else if (minorThird)
            third = 3;
        else if (inScale (harmony, root + 5))
            third = 5;           // pas de tierce dans la gamme : quarte suspendue
        else if (inScale (harmony, root + 2))
            third = 2;

        int fifth = 7;
        if (! inScale (harmony, root + 7))
        {
            if (inScale (harmony, root + 6))
                fifth = 6;
            else if (inScale (harmony, root + 8))
                fifth = 8;
        }

        if (third != 0)
        {
            intervals = { 0, third, fifth };
            levels = { 1.0f, 0.8f, 0.8f };
            noteCount = 3;
        }
        else
        {
            intervals = { 0, fifth, 0 };
            levels = { 1.0f, 0.8f, 0.0f };
            noteCount = 2;
        }
    }

    float power = 0.0f;
    for (int i = 0; i < noteCount; ++i)
        power += levels[(size_t) i] * levels[(size_t) i];

    // Même niveau d'ensemble quel que soit le nombre de notes.
    carrierGain = 0.6f / std::sqrt (std::max (0.01f, power) * (1.0f + crossPan * crossPan));
    pitchCountdown = 0;
}

void Vocoder::updatePitch() noexcept
{
    currentMidi += glideCoef * (targetMidi - currentMidi);

    const double up = std::pow (2.0, detuneSemitones / 12.0);
    for (int i = 0; i < noteCount; ++i)
    {
        const double frequency = tuning * std::pow (2.0, (currentMidi + (double) intervals[(size_t) i] - 69.0) / 12.0);
        const double step = std::min (0.45, frequency / fs);
        stepA[(size_t) i] = std::min (0.45, step * up);
        stepB[(size_t) i] = step / up;
    }

    double lowest = 1.0e9;
    for (int i = 0; i < noteCount; ++i)
        lowest = std::min (lowest, tuning * std::pow (2.0, (currentMidi + (double) intervals[(size_t) i] - 69.0) / 12.0));

    updateReach (lowest);
}

/** Dans le grave, les bandes sont plus étroites que l'écart entre deux
    partiels : relevé tel quel, le niveau de la voix aurait des trous entre
    ses partiels, et une note de la porteuse tombant dans un trou (une tierce
    d'accord, une voix transposée) serait étouffée. On relève donc le timbre
    comme une courbe qui passe par les sommets : chaque bande prend le plus
    fort niveau de ses voisines, sur la largeur d'un écart entre partiels. */
void Vocoder::updateReach (double lowestCarrierHz) noexcept
{
    const double spacing = std::max (voiceHz, lowestCarrierHz);

    for (int b = 0; b < numBands; ++b)
    {
        const double centre = lowestBand * std::exp (bandStep * (double) b);
        const double bandsPerPartial = std::log (1.0 + spacing / centre) / bandStep;
        reach[(size_t) b] = std::max (0, std::min (4, (int) std::lround (0.5 * bandsPerPartial)));
    }

    const auto bandOf = [this] (double hz)
    {
        return std::max (0, std::min (numBands - 1, (int) std::lround (std::log (hz / lowestBand) / bandStep)));
    };

    voiceFloorBand = bandOf (voiceHz);
    carrierFloorBand = bandOf (lowestCarrierHz);
}

float Vocoder::nextNoise (std::uint32_t& state) noexcept
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return (float) (std::int32_t) state * (1.0f / 2147483648.0f);
}

void Vocoder::runBank (Bank& bank, float input, float* output) noexcept
{
    // Les 32 bandes sont indépendantes : écrit ainsi (tableaux garantis
    // distincts), le compilateur les calcule quatre par quatre.
    float* ONIRIQUE_RESTRICT z1a = bank.z1a.data();
    float* ONIRIQUE_RESTRICT z2a = bank.z2a.data();
    float* ONIRIQUE_RESTRICT z1b = bank.z1b.data();
    float* ONIRIQUE_RESTRICT z2b = bank.z2b.data();
    const float* ONIRIQUE_RESTRICT c0 = b0.data();
    const float* ONIRIQUE_RESTRICT c1 = a1.data();
    const float* ONIRIQUE_RESTRICT c2 = a2.data();
    float* ONIRIQUE_RESTRICT out = output;

    for (int b = 0; b < numBands; ++b)
    {
        const float k1 = c0[b] * input;
        const float y1 = k1 + z1a[b];
        z1a[b] = z2a[b] - c1[b] * y1;
        z2a[b] = -k1 - c2[b] * y1;

        const float k2 = c0[b] * y1;
        const float y2 = k2 + z1b[b];
        z1b[b] = z2b[b] - c1[b] * y2;
        z2b[b] = -k2 - c2[b] * y2;

        out[b] = y2;
    }
}

void Vocoder::process (float modulator, bool stereo, float& left, float& right) noexcept
{
    wide = stereo;

    if (--pitchCountdown <= 0)
    {
        pitchCountdown = pitchInterval;
        updatePitch();
    }

    // --- porteuse
    double sawLeft = 0.0, sawRight = 0.0;
    for (int i = 0; i < noteCount; ++i)
    {
        const auto n = (size_t) i;
        const double a = 2.0 * phaseA[n] - 1.0 - blep (phaseA[n], stepA[n]);
        const double b = 2.0 * phaseB[n] - 1.0 - blep (phaseB[n], stepB[n]);

        sawLeft += (double) levels[n] * (a + (double) crossPan * b);
        sawRight += (double) levels[n] * (b + (double) crossPan * a);

        phaseA[n] += stepA[n];
        if (phaseA[n] >= 1.0)
            phaseA[n] -= 1.0;

        phaseB[n] += stepB[n];
        if (phaseB[n] >= 1.0)
            phaseB[n] -= 1.0;
    }

    breath += breathCoef * ((voiced ? 0.0f : 1.0f) - breath);
    const float tone = carrierGain * (1.0f - breath);
    const float hiss = voicedBreath + (1.0f - voicedBreath) * breath;

    const float carrierLeft = tone * (float) sawLeft + hiss * nextNoise (noiseLeft);
    const float carrierRight = tone * (float) sawRight + hiss * nextNoise (noiseRight);

    // --- les signaux passent par le même banc de filtres (en mono, les deux
    //     côtés de la porteuse sont réunis avant : un banc de moins à calculer)
    float mod[numBands], l[numBands], r[numBands];
    runBank (modBank, modulator, mod);

    if (stereo)
    {
        runBank (leftBank, carrierLeft, l);
        runBank (rightBank, carrierRight, r);
    }
    else
    {
        runBank (leftBank, 0.5f * (carrierLeft + carrierRight), l);
    }

    // --- niveau de chaque bande, pour la voix et pour la porteuse
    for (int b = 0; b < numBands; ++b)
    {
        const auto i = (size_t) b;

        const float voiceLevel = std::abs (mod[b]);
        envMod[i] += (voiceLevel > envMod[i] ? attack[i] : release[i]) * (voiceLevel - envMod[i]);

        const float carrierLevel = stereo ? 0.5f * (std::abs (l[b]) + std::abs (r[b])) : std::abs (l[b]);
        envCar[i] += (carrierLevel > envCar[i] ? attack[i] : carrierCoef) * (carrierLevel - envCar[i]);
    }

    // --- les gains par bande. Les niveaux bougent lentement : on recalcule les
    //     gains quelques milliers de fois par seconde et on glisse de l'un à
    //     l'autre, ce qui allège nettement le calcul sans rien changer au son.
    if (--gainCountdown <= 0)
    {
        gainCountdown = gainInterval;
        updateGains();
    }

    float outLeft = 0.0f, outRight = 0.0f;
    if (stereo)
    {
        for (int b = 0; b < numBands; ++b)
        {
            const auto i = (size_t) b;
            bandGain[i] += bandGainStep[i];
            outLeft += bandGain[i] * l[b];
            outRight += bandGain[i] * r[b];
        }
    }
    else
    {
        for (int b = 0; b < numBands; ++b)
        {
            const auto i = (size_t) b;
            bandGain[i] += bandGainStep[i];
            outLeft += bandGain[i] * l[b];
        }

        outRight = outLeft;
    }

    // Garde-fou : bande par bande les niveaux sont justes, mais à une attaque
    // ou à un changement de note la somme peut dépasser un instant. On
    // compare donc la puissance d'ensemble à celle de la voix, et on retient
    // le vocodeur dès qu'il passe au-dessus.
    voicePower += powerCoef * (modulator * modulator - voicePower);
    outPower += powerCoef * (0.5f * (outLeft * outLeft + outRight * outRight) - outPower);

    const float allowed = outPower > loudnessMargin * voicePower + 1.0e-12f
                            ? std::sqrt ((loudnessMargin * voicePower + 1.0e-12f) / outPower)
                            : 1.0f;
    ceiling += (allowed < ceiling ? ceilingDown : ceilingUp) * (allowed - ceiling);

    // Et, sans délai, aucune crête au-dessus de celles de la voix : ce qui
    // sort du vocodeur ne peut pas saturer si la voix ne saturait pas.
    voicePeak = std::max (std::abs (modulator), voicePeak - peakRelease * voicePeak);
    const float peak = std::max (std::abs (outLeft), std::abs (outRight));
    const float highest = peakMargin * voicePeak + 1.0e-9f;
    if (ceiling * peak > highest)
        ceiling = highest / peak;

    left = ceiling * outLeft;
    right = ceiling * outRight;
}

/** La porteuse prend le timbre de la voix : dans chaque bande,
    (porteuse / timbre de la porteuse) x timbre de la voix. */
void Vocoder::updateGains() noexcept
{
    // Les deux timbres, vus comme des courbes passant par les sommets
    float voiceShape[numBands], carrierShape[numBands];
    for (int b = 0; b < numBands; ++b)
    {
        const int from = std::max (0, b - reach[(size_t) b]);
        const int to = std::min (numBands - 1, b + reach[(size_t) b]);

        float v = 0.0f, c = 0.0f;
        for (int j = from; j <= to; ++j)
        {
            v = std::max (v, envMod[(size_t) j]);
            c = std::max (c, envCar[(size_t) j]);
        }

        voiceShape[b] = v;
        carrierShape[b] = c;
    }

    // Sous sa note la plus grave, un son n'a pas de timbre mesurable : on y
    // prolonge le niveau de cette note.
    for (int b = 0; b < voiceFloorBand; ++b)
        voiceShape[b] = voiceShape[voiceFloorBand];
    for (int b = 0; b < carrierFloorBand; ++b)
        carrierShape[b] = carrierShape[carrierFloorBand];

    const float trim = outputTrim * (wide ? stereoTrim : 1.0f) * (1.0f + consonantBoost * breath);
    const float perSample = 1.0f / (float) gainInterval;

    for (int b = 0; b < numBands; ++b)
    {
        // Deux bandes voisines se recouvrent et tournent la phase en sens
        // inverse : additionnées telles quelles elles s'annulent en partie.
        // Une bande sur deux est donc inversée, et la somme redevient plate.
        const float gain = trim * std::min (maxBandGain, voiceShape[b] / std::max (carrierFloor, carrierShape[b]));
        const float target = (b & 1) != 0 ? -gain : gain;
        bandGainStep[(size_t) b] = (target - bandGain[(size_t) b]) * perSample;
    }
}

} // namespace onirique::tune
