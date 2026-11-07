#include "PitchCorrector.h"

#include <algorithm>
#include <cmath>

namespace onirique::tune
{

namespace
{
    constexpr double hangSeconds = 0.06;        // trou de voisement toléré sans repartir de zéro
    constexpr double twoPi = 6.283185307179586;

    constexpr double quickHz = 25.0;            // lissage léger de la hauteur mesurée (gigue du détecteur)
    constexpr double centreSeconds = 0.18;      // une période de vibrato à 5,5 Hz : la moyenne l'efface
    constexpr double centreJump = 0.85;         // demi-tons d'écart au centre : au-delà, la voix a changé de note
    // Seuil de vibrato étroit (comme le réglage « Narrow » d'Auto-Tune) : seul un vibrato discret compte pour du
    // vibrato ; au-delà, c'est un mouvement de la voix, que la sortie suit tel quel (glissés et attaques par en
    // dessous gardent leur forme).
    constexpr double narrowSwing = 0.2;         // demi-ton : le plus large vibrato reconnu
    // ... ou bien elle s'est posée ailleurs : immobile, loin du centre, plus
    // longtemps qu'un vibrato ne s'arrête à son sommet.
    constexpr double settledFrom = 0.15;        // demi-tons
    constexpr double settledSpeed = 2.0;        // demi-tons par seconde
    constexpr double settledFor = 0.02;         // s (un vibrato reste moins de 10 ms à son sommet)

    // Choix de la note : la plus proche de la voix, avec juste assez de marge
    // pour qu'une voix posée au milieu de deux notes ne les fasse pas papillonner.
    constexpr double naturalHysteresis = 0.10;
    constexpr double hardHysteresis = 0.04;

    // Transitions. La voix quitte sa note dès qu'elle s'en écarte plus que son
    // propre vibrato, vite, et en s'éloignant : la correction se fige alors à
    // celle de la note quittée, et la sortie glisse exactement comme la voix.
    // Se tromper ici est sans danger (la voix passe telle quelle un instant).
    constexpr double departFrom = 0.12;         // demi-tons d'écart au centre, au minimum
    constexpr double departOverSwing = 1.25;    // ... ou un peu plus que le vibrato de la voix
    constexpr double departSpeed = 4.0;         // demi-tons par seconde
    constexpr double swingRelease = 0.5;        // s : mémoire de l'amplitude du vibrato
    constexpr double releaseMs = 5.0;           // la correction rejoint sa valeur figée
    // Arrivée : depuis le départ, la voix est entrée dans la zone d'une note et
    // y reste ; et elle a nettement ralenti par rapport à son propre glissé, ou
    // bien elle a fait demi-tour (un dépassement, ou le vibrato de la nouvelle note).
    constexpr double arriveAfter = 0.03;        // s dans la zone de la même note
    constexpr double calmRatio = 0.25;          // vitesse résiduelle, en part de la vitesse du glissé
    constexpr double calmSpeed = 3.0;           // ... et au plus ça (demi-tons par seconde)
    constexpr double calmFor = 0.012;           // s
    constexpr double stillRange = 0.15;         // demi-ton : la voix qui ne sort plus de cette plage...
    constexpr double stillFor = 0.025;          // ... pendant ça s'est posée, même entre deux notes
    constexpr double longestGlide = 0.6;        // s : au-delà, on reprend quoi qu'il arrive
    constexpr double shortestMove = 0.6;        // demi-ton : en deçà (sans changer de note), ce n'était qu'un vibrato
    constexpr double resumeSeconds = 0.03;      // après l'arrivée, la reprise ne se fait jamais d'un coup
    constexpr double resumeMs = 4.0;

    // Humanize : sur la partie tenue d'une note longue, la correction peut
    // devenir jusqu'à 7 fois plus lente.
    constexpr double sustainFrom = 0.15, sustainTo = 0.40;   // s : la note devient "tenue"
    constexpr double humanizeSlowdown = 6.0;

    double smoothStep (double lo, double hi, double x) noexcept
    {
        if (x <= lo) return 0.0;
        if (x >= hi) return 1.0;
        const double t = (x - lo) / (hi - lo);
        return t * t * (3.0 - 2.0 * t);
    }

    /** Coefficient d'un filtre à un pôle, par constante de temps en ms. */
    double follow (double dt, double tauMs) noexcept
    {
        return tauMs < 0.05 ? 1.0 : 1.0 - std::exp (-dt * 1000.0 / tauMs);
    }

    /** Coefficient d'un filtre à un pôle, par fréquence de coupure. */
    double followHz (double dt, double hz) noexcept
    {
        return 1.0 - std::exp (-twoPi * hz * dt);
    }

    bool inMask (int note, std::uint16_t mask) noexcept
    {
        return (mask & (1u << (((note % 12) + 12) % 12))) != 0;
    }

    bool allowed (int note, std::uint16_t mask) noexcept
    {
        return (mask & 0x0fff) == 0 || inMask (note, mask);
    }

    /** Note ignorée : la plus proche de la voix, au demi-ton près, est marquée « laisser passer ». */
    bool skipped (double midi, std::uint16_t skipMask) noexcept
    {
        return skipMask != 0 && inMask ((int) std::floor (midi + 0.5), skipMask);
    }

    /** Moitié de l'écart entre une note et sa voisine dans la gamme, du côté
        où se trouve la voix : c'est là que la voix est à mi-chemin. */
    double halfGap (int note, double voice, std::uint16_t mask) noexcept
    {
        const int direction = voice >= (double) note ? 1 : -1;
        for (int step = 1; step <= 12; ++step)
            if (allowed (note + direction * step, mask))
                return 0.5 * (double) step;

        return 6.0;
    }

    /** Flex-Tune : la correction n'agit que dans une zone autour de la note,
        d'autant plus étroite que Flex-Tune est haut ; au-delà, la voix reste libre. */
    double flexWeight (double distance, float flex) noexcept
    {
        if (flex <= 0.001f)
            return 1.0;

        const double f = flex > 1.0f ? 1.0 : (double) flex;
        const double inner = std::pow (1.0 - f, 1.5) * 0.5 + 0.04;
        const double outer = inner * 1.6 + 0.08;
        return 1.0 - smoothStep (inner, outer, std::abs (distance));
    }

    /** Forme du vibrato créé, phase 0..1, de -1 à 1. */
    double vibratoShape (int shape, double phase) noexcept
    {
        switch (shape)
        {
            case 1:  return phase < 0.5 ? 4.0 * phase - 1.0 : 3.0 - 4.0 * phase;   // triangle
            case 2:  return phase < 0.5 ? 1.0 : -1.0;                               // carré
            case 3:  return 2.0 * phase - 1.0;                                      // dent de scie montante
            default: return std::sin (twoPi * phase);
        }
    }
}

void PitchCorrector::reset() noexcept
{
    active = false;
    silentFor = 1.0e9;
    target = 0;
    held = 0.0;
    quick = 0.0;
    recentIndex = recentCount = 0;
    resetCentre (0.0);
    shiftA = shift = 0.0;
    vibPhase = vibTime = vibShapeNow = 0.0;
    vibRateScale = vibDepthScale = 1.0;
    velocity = 0.0;
    gliding = false;
    glideHold = glideTime = swing = excursion = bandTime = 0.0;
    resumeTime = 1.0;
}

int PitchCorrector::nearestNote (double midi, std::uint16_t mask) noexcept
{
    const int base = (int) std::floor (midi + 0.5);
    if ((mask & 0x0fff) == 0)
        return base;

    int best = base;
    double bestDistance = 1.0e9;
    for (int note = base - 6; note <= base + 6; ++note)
    {
        if (! inMask (note, mask))
            continue;

        const double distance = std::abs (midi - (double) note);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = note;
        }
    }

    return best;
}

/** La note visée change quand une autre note de la gamme est nettement plus
    proche de la voix ; chaque nouvelle note relance le vibrato créé. */
void PitchCorrector::chooseNote (double judged, double hysteresis, std::uint16_t mask) noexcept
{
    const int candidate = nearestNote (judged, mask);
    if (candidate == target)
        return;

    if (! allowed (target, mask) || std::abs (judged - (double) candidate) + hysteresis < std::abs (judged - (double) target))
    {
        target = candidate;
        held = 0.0;
        vibTime = 0.0;
        vibPhase = 0.0;
        varyVibrato();
    }
}

/** Variation du vibrato créé : chaque cycle a sa durée et son ampleur, un peu différentes. */
void PitchCorrector::varyVibrato() noexcept
{
    seed = seed * 1664525u + 1013904223u;
    const double r1 = (double) (seed >> 8) / 16777216.0 * 2.0 - 1.0;
    seed = seed * 1664525u + 1013904223u;
    const double r2 = (double) (seed >> 8) / 16777216.0 * 2.0 - 1.0;
    vibRateScale = 1.0 + 0.25 * r1;
    vibDepthScale = 1.0 + 0.35 * r2;
}

PitchCorrector::Output PitchCorrector::process (bool voiced, double frequencyHz, double dt, const CorrectorParams& p) noexcept
{
    if (! voiced || frequencyHz <= 0.0)
    {
        silentFor += dt;
        if (silentFor > hangSeconds)
            active = false;

        return { false, lastIn, lastOut, 0.0f, lastNote };
    }

    const double pitch = 69.0 + 12.0 * std::log2 (frequencyHz / p.a4);
    const double wanted = pitch + (double) p.transpose;

    if (! active)
    {
        // Nouvelle phrase : on repart de la voix telle qu'elle arrive. La
        // correction part de zéro : à Retune Speed lent, l'attaque glisse vers
        // la note, comme chez Auto-Tune ; à 0, la note est juste d'emblée.
        active = true;
        target = nearestNote (wanted, p.noteMask);
        held = 0.0;
        quick = wanted;
        recentIndex = recentCount = 0;
        resetCentre (wanted);
        shiftA = shift = 0.0;
        vibTime = vibPhase = 0.0;
        velocity = 0.0;
        gliding = false;
        swing = excursion = 0.0;
        resumeTime = 1.0;
        bandNote = target;
        bandTime = 0.0;
        anchor = wanted;
    }

    silentFor = 0.0;
    held += dt;

    quick += followHz (dt, quickHz) * (wanted - quick);
    updateCentre (quick, dt);

    const double vibrato = createdVibrato (dt, p.created);

    // Note ignorée : la voix passe telle quelle tant que c'est d'elle qu'elle est le plus près (le vibrato créé
    // s'ajoute toujours). La correction repart de zéro en sortant : aucun saut.
    if (p.hard && skipped (quick, p.skipMask))
    {
        shiftA = shift = 0.0;
        return finish (pitch, wanted + vibrato, 0.0f);
    }

    if (p.hard)
    {
        // La note se juge sur la voix à peine lissée, et la sortie est la note
        // elle-même : plate, et le saut se fait d'un coup.
        chooseNote (quick, hardHysteresis, p.noteMask);

        // Garde la correction prête : repasser en accordage classique se fait sans saut.
        shiftA = shift = (double) target - wanted;
        return finish (pitch, (double) target + vibrato, 1.0f);
    }

    // Note de la gamme dont la voix est la plus proche en ce moment, et depuis
    // quand : c'est ainsi qu'on sait qu'elle est arrivée quelque part.
    const int band = nearestNote (quick, p.noteMask);
    bandTime = band == bandNote ? bandTime + dt : 0.0;
    bandNote = band;

    const double away = quick - centre;
    resumeTime += dt;

    // Le plus large vibrato reconnu comme tel : celui de la voix, ou moins avec le seuil étroit.
    const double vibratoSwing = p.narrowVibrato ? std::min (swing, narrowSwing) : swing;

    if (! gliding)
    {
        // Dernier endroit où la voix était encore posée sur sa note : c'est de là
        // qu'elle part (le centre, une moyenne, a déjà un peu suivi le départ).
        if (std::abs (away) < 0.5 * std::max (departFrom, departOverSwing * vibratoSwing))
            anchor = quick;

        // La voix part : plus loin que son vibrato, vite, en s'éloignant.
        if (std::abs (away) > std::max (departFrom, departOverSwing * vibratoSwing)
            && std::abs (velocity) > departSpeed && velocity * away > 0.0)
        {
            gliding = true;
            glideTime = 0.0;
            bandTime = 0.0;
            calmTime = 0.0;
            glideSpeed = std::abs (velocity);
            glideDirection = velocity > 0.0 ? 1 : -1;
            turned = false;
            departNote = target;
            stillFrom = quick;
            stillTime = 0.0;
            riseCount = 0;
            rememberRise (glideSpeed, std::max (0.0, (double) glideDirection * (velocityAt - anchor)));

            // On garde la correction de la note quittée, telle qu'elle était
            // avant le départ (vibrato et départ ôtés).
            const double left = (double) target - anchor;
            glideHold = flexWeight (left, p.flex) * left;
        }
        else
        {
            // Amplitude du vibrato : le plus loin où la voix est allée depuis
            // qu'elle a passé le centre, retenu seulement une fois qu'elle en
            // revient (un vibrato revient, un départ non).
            if (away * excursion < 0.0)
                excursion = 0.0;
            if (std::abs (away) > std::abs (excursion))
                excursion = away;

            swing -= dt / swingRelease * swing;
            if (velocity * away <= 0.0)
                swing = std::max (swing, std::abs (excursion));
        }
    }
    else
    {
        glideTime += dt;
        if (std::abs (velocity) > glideSpeed)
        {
            glideSpeed = std::abs (velocity);
            rememberRise (glideSpeed, std::max (0.0, (double) glideDirection * (velocityAt - anchor)));
        }
        turned = turned || velocity * (double) glideDirection < 0.0;
        calmTime = std::abs (velocity) < std::min (calmSpeed, calmRatio * glideSpeed) ? calmTime + dt : 0.0;

        if (std::abs (quick - stillFrom) > stillRange)
        {
            stillFrom = quick;
            stillTime = 0.0;
        }
        stillTime += dt;

        if ((bandTime >= arriveAfter && (calmTime >= calmFor || turned)) || stillTime >= stillFor || glideTime > longestGlide)
        {
            // Arrivée : la nouvelle note repart d'un centre neuf, et la
            // correction rejoint la note depuis sa valeur figée, en douceur.
            // Si la voix n'a pas vraiment changé de note, c'était le premier
            // aller d'un vibrato : rien n'est recalé, et son amplitude est
            // retenue pour que les suivants ne passent plus pour des départs.
            gliding = false;
            resumeTime = 0.0;

            if (band != departNote && std::abs (quick - anchor) > shortestMove)
                resetCentre (quick);
            else
                swing = std::max (swing, std::abs (quick - centre));
        }
    }

    // Natural Vibrato : le vibrato de la voix (ce qui bouge autour du centre de
    // la note), agrandi ou réduit avant la correction ; jamais le glissé.
    const double voice = gliding ? wanted : wanted + (double) p.vibrato * (wanted - centre);

    // La note se choisit sur le centre de la voix (vibrato ôté) : un vibrato
    // large sur une note un peu fausse ne fait pas sauter la correction d'une
    // note à l'autre. Pendant un glissé, sur la voix elle-même.
    chooseNote (gliding ? quick : centre, naturalHysteresis, p.noteMask);

    const double toNote = (double) target - voice;
    const bool ignored = skipped (gliding ? quick : centre, p.skipMask);
    double aim = ignored ? 0.0 : flexWeight (toNote, p.flex) * toNote;

    if (gliding)
    {
        // Tant que la voix accélère, elle garde la correction de la note
        // quittée : la sortie glisse exactement comme elle. Un glissé chanté
        // freine à peu près comme il a accéléré : à chaque vitesse en freinant,
        // il lui reste à peu près le chemin qu'elle avait fait en accélérant
        // jusqu'à cette vitesse. On sait donc où elle va se poser, et la
        // correction passe peu à peu à celle de cette note, au fil du chemin :
        // la sortie y arrive en même temps que la voix. Quand la voix s'arrête,
        // c'est là où elle est qui compte. Près du milieu de deux notes la
        // correction visée vaut zéro : jamais d'à-coup d'une note à l'autre.
        const auto towards = [&p] (double at)
        {
            if (skipped (at, p.skipMask))
                return 0.0;
            const int note = nearestNote (at, p.noteMask);
            const double to = (double) note - at;
            const double half = halfGap (note, at, p.noteMask);
            return (1.0 - smoothStep (0.7 * half, half, std::abs (to))) * flexWeight (to, p.flex) * to;
        };

        const double peakTravel = riseTravel[(size_t) riseCount - 1];
        const double remaining = travelAtRise (std::abs (velocity));
        const double braking = std::clamp (1.0 - std::abs (velocity) / glideSpeed, 0.0, 1.0);
        const double stopped = braking * braking;
        const double landing = (1.0 - stopped) * towards (velocityAt + (double) glideDirection * remaining) + stopped * towards (quick);
        const double progress = std::max (stopped, peakTravel > 0.02 ? std::clamp (1.0 - remaining / peakTravel, 0.0, 1.0) : 0.0);
        aim = glideHold + progress * (landing - glideHold);
    }

    // Retune Speed : temps pour atteindre la note (90 % du chemin), par deux
    // pôles en cascade. Humanize l'allonge sur la partie tenue des notes longues.
    // Pendant un glissé, la correction rejoint vite sa valeur figée ; juste
    // après l'arrivée, elle ne reprend jamais d'un coup.
    const double sustain = smoothStep (sustainFrom, sustainTo, held);
    const double retuneMs = (double) p.retuneMs * (1.0 + humanizeSlowdown * (double) p.humanize * sustain);
    const double tauMs = gliding ? releaseMs : std::max (0.26 * retuneMs, resumeTime < resumeSeconds ? resumeMs : 0.0);
    const double alpha = follow (dt, tauMs);
    shiftA += alpha * (aim - shiftA);
    shift += alpha * (shiftA - shift);

    // À Retune 0 la note est imposée exactement (sans la gigue de la mesure) ;
    // dès que la correction prend du temps, la voix garde ses micro-variations.
    // Pendant un glissé, et le temps de la reprise, la voix garde les siennes.
    const double resumed = gliding || ignored ? 0.0 : smoothStep (0.0, resumeSeconds, resumeTime);
    const float pin = (float) (resumed * (1.0 - smoothStep (0.5, 4.0, retuneMs)) * smoothStep (0.9, 0.999, flexWeight (toNote, p.flex)));
    return finish (pitch, voice + shift + vibrato, pin);
}

void PitchCorrector::rememberRise (double speed, double travel) noexcept
{
    riseCount = std::min (riseCount, (int) riseSpeed.size() - 1);   // pleine : la dernière case suit le pic
    riseSpeed[(size_t) riseCount] = speed;
    riseTravel[(size_t) riseCount] = travel;
    ++riseCount;
}

/** Chemin qu'avait fait la voix, depuis la note quittée, quand en accélérant
    elle allait à cette vitesse (les vitesses retenues vont croissant). */
double PitchCorrector::travelAtRise (double speed) const noexcept
{
    if (speed <= riseSpeed[0])
        return riseTravel[0] * speed / riseSpeed[0];

    for (int i = 1; i < riseCount; ++i)
    {
        if (speed <= riseSpeed[(size_t) i])
        {
            const double u = (speed - riseSpeed[(size_t) i - 1]) / (riseSpeed[(size_t) i] - riseSpeed[(size_t) i - 1]);
            return riseTravel[(size_t) i - 1] + u * (riseTravel[(size_t) i] - riseTravel[(size_t) i - 1]);
        }
    }

    return riseTravel[(size_t) riseCount - 1];
}

void PitchCorrector::resetCentre (double pitch) noexcept
{
    centreRing.fill (pitch);
    centreWrite = 0;
    centreCount = 1;
    centreSum = pitch;
    centre = pitch;
    settled = 0.0;
}

void PitchCorrector::updateCentre (double pitch, double dt) noexcept
{
    // Vitesse de la voix sur une vingtaine de millisecondes.
    const double past = recent[(size_t) recentIndex];
    recent[(size_t) recentIndex] = pitch;
    recentIndex = (recentIndex + 1) % (int) recent.size();
    recentCount = std::min (recentCount + 1, (int) recent.size());
    const double speed = recentCount == (int) recent.size() ? std::abs (pitch - past) / (dt * (double) recent.size()) : 1.0e9;
    velocity = recentCount == (int) recent.size() ? (pitch - past) / (dt * (double) recent.size()) : 0.0;
    velocityAt = recentCount == (int) recent.size() ? 0.5 * (pitch + past) : pitch;

    if (std::abs (pitch - centre) > settledFrom && speed < settledSpeed)
        settled += dt;
    else
        settled = 0.0;

    if (std::abs (pitch - centre) > centreJump || settled >= settledFor)
    {
        resetCentre (pitch);
        return;
    }

    const int length = std::clamp ((int) std::lround (centreSeconds / std::max (dt, 1.0e-4)), 1, centreSize);
    if (centreCount >= length)
    {
        // fenêtre pleine : la plus ancienne valeur sort de la moyenne
        const int oldest = (centreWrite - centreCount + centreSize) % centreSize;
        centreSum -= centreRing[(size_t) oldest];
        --centreCount;
    }

    centreRing[(size_t) centreWrite] = pitch;
    centreWrite = (centreWrite + 1) % centreSize;
    centreSum += pitch;
    ++centreCount;
    centre = centreSum / (double) centreCount;
}

/** Vibrato créé : nul pendant le délai, puis monte jusqu'à l'amplitude choisie
    en `onsetMs`, relancé à chaque note. Renvoie un écart en demi-tons. */
double PitchCorrector::createdVibrato (double dt, const VibratoParams& v) noexcept
{
    vibTime += dt;
    vibShapeNow = 0.0;
    if (! v.on)
        return 0.0;

    const double variation = std::clamp ((double) v.variation, 0.0, 1.0);
    const double rate = std::max (0.05, (double) v.rateHz * (1.0 + variation * (vibRateScale - 1.0)));
    vibPhase += rate * dt;
    if (vibPhase >= 1.0)
    {
        vibPhase -= std::floor (vibPhase);
        varyVibrato();      // un nouveau cycle, un peu différent du précédent
    }

    const double since = vibTime * 1000.0 - (double) v.delayMs;
    if (since <= 0.0)
        return 0.0;

    const double envelope = v.onsetMs <= 1.0f ? 1.0 : std::min (1.0, since / (double) v.onsetMs);
    vibShapeNow = envelope * (1.0 + variation * (vibDepthScale - 1.0)) * vibratoShape (v.shape, vibPhase);
    return (double) v.depthCents / 100.0 * vibShapeNow;
}

PitchCorrector::Output PitchCorrector::finish (double pitch, double outMidi, float pin) noexcept
{
    lastIn = (float) pitch;
    lastOut = (float) outMidi;
    lastNote = (float) target;
    return { true, lastIn, lastOut, pin, lastNote, (float) vibShapeNow };
}

} // namespace onirique::tune
