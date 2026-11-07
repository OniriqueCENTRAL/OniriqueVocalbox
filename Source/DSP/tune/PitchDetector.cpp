#include "PitchDetector.h"

#include <algorithm>
#include <cmath>

namespace onirique::tune
{

namespace
{
    constexpr float silenceRms = 0.001f;       // -60 dBFS : en dessous (souffle, repisse du casque), on ne touche à rien

    // Sur une voix soufflée ou rauque, tous les creux remontent et leurs
    // profondeurs fluctuent d'autant. Les comparaisons se font donc en
    // proportion du creux de référence, plus un petit plancher.
    //
    // "Aussi net" : la fondamentale l'emporte sur ses multiples dès que son
    // creux ne dépasse pas le plus profond de plus de ça.
    constexpr float equalRatio = 0.35f, equalFloor = 0.02f;

    // Une fois la note suivie, on ne change d'octave que sur une preuve qui dure.
    constexpr float shorterRatio = 0.25f, shorterFloor = 0.015f;   // période plus courte et aussi nette : on était sur un multiple
    constexpr float longerRatio = 0.5f, longerFloor = 0.15f;       // période plus longue et nettement plus nette : on était sur un harmonique
    constexpr int shorterRivalFrames = 8;
    constexpr int longerRivalFrames = 8;
    constexpr int coastFrames = 2;                // mesures douteuses tolérées au milieu d'une note

    // Les toutes premières mesures d'une note sont les moins fiables : pendant
    // ce court instant, une meilleure période s'impose en deux mesures.
    constexpr int onsetFrames = 10;
    constexpr int onsetRivalFrames = 2;

    // Part de la mesure courante dans la courbe qui sert aux décisions, le
    // reste venant des mesures précédentes de la même note. Une voix nette
    // se lit presque sur l'instant ; une voix soufflée ou bruitée demande
    // qu'on accumule plus longtemps pour y voir clair.
    constexpr float blendClean = 0.5f, blendNoisy = 0.12f;
    constexpr float noisyDip = 0.20f, cleanDip = 0.04f;

    // Voix qui a bougé : creux de l'instant au moins deux fois plus net que celui suivi.
    constexpr float movedRatio = 0.5f;

    int orderFor (int length) noexcept
    {
        int order = 1;
        while ((1 << order) < length)
            ++order;
        return order;
    }
}

void PitchDetector::prepare (double analysisRateToUse, double lowestFrequency)
{
    analysisRate = analysisRateToUse;
    worstTau = (int) std::ceil (analysisRate / lowestFrequency) + 2;

    const int worstOrder = orderFor (2 * worstTau + 2);

    ffts.clear();
    for (int order = 0; order <= worstOrder; ++order)
        ffts.push_back (order >= 6 ? std::make_unique<Fft> (order) : nullptr);

    const auto n = (size_t) 1 << worstOrder;
    re.assign (n, 0.0f);
    im.assign (n, 0.0f);
    cr.assign (n, 0.0f);
    ci.assign (n, 0.0f);
    frame.assign (n, 0.0f);
    energy.assign (n + 1, 0.0);
    cmnd.assign ((size_t) worstTau + 2, 1.0f);
    evidence.assign ((size_t) worstTau + 2, 1.0f);

    configure (85.0, 900.0);
}

void PitchDetector::configure (double minFrequency, double maxFrequency) noexcept
{
    tauMax = std::min ((int) std::ceil (analysisRate / minFrequency), worstTau);
    tauMin = std::max (2, (int) std::floor (analysisRate / maxFrequency));
    tauMin = std::min (tauMin, tauMax - 2);
    window = tauMax;

    const int order = std::max (6, orderFor (window + tauMax + 1));
    fft = ffts[(size_t) order].get();

    reset();
}

void PitchDetector::setTracking (float amount) noexcept
{
    const float t = amount < 0.0f ? 0.0f : (amount > 1.0f ? 1.0f : amount);
    onsetLimit = 0.40f - 0.30f * t;
    stayLimit = std::min (0.60f, onsetLimit + 0.20f);
    coastLimit = std::min (0.75f, onsetLimit + 0.40f);
}

void PitchDetector::reset() noexcept
{
    wasVoiced = false;
    lastTau = 0.0f;
    weakFrames = 0;
    rivalTau = 0.0f;
    rivalCount = 0;
    framesSinceOnset = 0;
    lastDip = 0.0f;
}

PitchDetector::Result PitchDetector::analyse (const float* ring, int mask, std::int64_t end) noexcept
{
    Result result;

    const int length = window + tauMax;
    const int n = fft->getSize();

    // Trame retournée dans le temps : frame[0] est l'échantillon le plus récent.
    energy[0] = 0.0;
    for (int j = 0; j < length; ++j)
    {
        const float x = ring[(size_t) ((end - 1 - j) & mask)];
        frame[(size_t) j] = x;
        energy[(size_t) j + 1] = energy[(size_t) j] + (double) x * (double) x;
    }

    const double e0 = energy[(size_t) window];
    result.rms = (float) std::sqrt (e0 / (double) window);
    result.centreLag = 0.5f * (float) window + (wasVoiced ? 0.5f * lastTau : 0.25f * (float) tauMax);

    if (! (result.rms >= silenceRms))      // écrit ainsi, un signal non fini passe aussi par ici
    {
        result.rms = 0.0f;
        reset();
        return result;
    }

    // Corrélation de a = frame[0..window) avec b = frame[0..length), en une
    // seule FFT : a dans la partie réelle, b dans la partie imaginaire.
    for (int j = 0; j < n; ++j)
    {
        re[(size_t) j] = j < window ? frame[(size_t) j] : 0.0f;
        im[(size_t) j] = j < length ? frame[(size_t) j] : 0.0f;
    }

    fft->forward (re.data(), im.data());

    for (int k = 0; k <= n / 2; ++k)
    {
        const int kn = (n - k) & (n - 1);
        const float ar = 0.5f * (re[(size_t) k] + re[(size_t) kn]);
        const float ai = 0.5f * (im[(size_t) k] - im[(size_t) kn]);
        const float br = 0.5f * (im[(size_t) k] + im[(size_t) kn]);
        const float bi = -0.5f * (re[(size_t) k] - re[(size_t) kn]);

        const float xr = ar * br + ai * bi;      // conj(A) * B
        const float xi = ar * bi - ai * br;
        cr[(size_t) k] = xr;
        ci[(size_t) k] = xi;
        cr[(size_t) kn] = xr;
        ci[(size_t) kn] = -xi;
    }

    fft->inverse (cr.data(), ci.data());

    // Différence entre la fenêtre et sa copie décalée, rapportée à leur
    // énergie : 0 = parfaitement périodique à ce décalage, 1 = sans rapport.
    // Cette normalisation traite de la même façon les périodes courtes et
    // longues (celle de YIN, cumulative, défavorise les voix aiguës dès que
    // le signal d'analyse est filtré).
    cmnd[0] = 1.0f;
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        const double shifted = energy[(size_t) (tau + window)] - energy[(size_t) tau];
        const double d = e0 + shifted - 2.0 * (double) cr[(size_t) tau];
        cmnd[(size_t) tau] = (float) std::min (2.0, std::max (0.0, d) / (e0 + shifted + 1.0e-12));
    }

    // Courbe de décision. À l'attaque c'est la mesure telle quelle ; ensuite
    // elle accumule les mesures de la note en cours, ce qui stabilise le choix
    // de l'octave quand le souffle fait danser les creux.
    const float trust = std::min (1.0f, std::max (0.0f, (noisyDip - lastDip) / (noisyDip - cleanDip)));

    if (! wasVoiced)
    {
        std::copy (cmnd.begin(), cmnd.begin() + tauMax + 1, evidence.begin());
    }
    else
    {
        const float blend = blendNoisy + (blendClean - blendNoisy) * trust;
        for (int tau = 0; tau <= tauMax; ++tau)
            evidence[(size_t) tau] += blend * (cmnd[(size_t) tau] - evidence[(size_t) tau]);
    }

    const auto isDip = [this] (int tau) noexcept
    {
        return evidence[(size_t) tau] <= evidence[(size_t) tau - 1] && evidence[(size_t) tau] <= evidence[(size_t) tau + 1];
    };

    const auto shortestDipBelow = [this, &isDip] (float level) noexcept
    {
        for (int tau = tauMin; tau < tauMax; ++tau)
            if (evidence[(size_t) tau] <= level && isDip (tau))
                return tau;

        return -1;
    };

    // Le creux le plus profond. On ne retient que de vrais creux : le bas de
    // la plage est encore sur la pente du décalage nul.
    int deepest = -1;
    float deepestValue = 2.0f;
    for (int tau = tauMin; tau < tauMax; ++tau)
    {
        if (evidence[(size_t) tau] < deepestValue && isDip (tau))
        {
            deepestValue = evidence[(size_t) tau];
            deepest = tau;
        }
    }

    result.clarity = 1.0f - std::min (1.0f, deepestValue);

    int pick = -1;

    if (wasVoiced && lastTau > 0.0f)
    {
        // On suit une note : son creux est-il toujours là ?
        const int lo = std::max (tauMin, (int) std::floor (lastTau * 0.8f));
        const int hi = std::min (tauMax - 1, (int) std::ceil (lastTau * 1.25f));

        int tracked = -1;
        float trackedValue = 2.0f;
        for (int tau = lo; tau <= hi; ++tau)
        {
            if (evidence[(size_t) tau] < trackedValue && isDip (tau))
            {
                trackedValue = evidence[(size_t) tau];
                tracked = tau;
            }
        }

        // Sur un glissé rapide, la courbe accumulée garde la note d'avant et son
        // creux se comble, alors que la mesure de l'instant en trouve un net un
        // peu plus loin : la voix a bougé, on repart de l'instant.
        int moved = -1;
        float movedValue = trackedValue * movedRatio;
        for (int tau = lo; tau <= hi; ++tau)
        {
            if (cmnd[(size_t) tau] < movedValue && cmnd[(size_t) tau] <= cmnd[(size_t) tau - 1] && cmnd[(size_t) tau] <= cmnd[(size_t) tau + 1])
            {
                movedValue = cmnd[(size_t) tau];
                moved = tau;
            }
        }

        if (moved >= 0 && movedValue < stayLimit)
        {
            std::copy (cmnd.begin(), cmnd.begin() + tauMax + 1, evidence.begin());
            tracked = moved;
            trackedValue = movedValue;
        }

        bool keep = false;
        if (trackedValue < stayLimit)
        {
            keep = true;
            weakFrames = 0;
        }
        else if (trackedValue < coastLimit && weakFrames < coastFrames)
        {
            keep = true;
            ++weakFrames;
        }

        if (keep)
        {
            pick = tracked;

            // Une autre période fait-elle mieux, durablement ?
            int rival = shortestDipBelow (trackedValue * (1.0f + shorterRatio) + shorterFloor);
            int framesNeeded = shorterRivalFrames;

            if (rival < 0 || rival >= lo)
            {
                rival = -1;
                if (deepest > hi && deepestValue * (1.0f + longerRatio) + longerFloor < trackedValue)
                {
                    rival = shortestDipBelow (deepestValue * (1.0f + equalRatio) + equalFloor);
                    framesNeeded = longerRivalFrames;
                    if (rival <= hi)
                        rival = -1;
                }
            }

            if (rival >= 0)
            {
                if (rivalCount > 0 && std::abs (std::log2 ((float) rival / rivalTau)) < 0.08f)
                    ++rivalCount;
                else
                    rivalCount = 1;

                rivalTau = (float) rival;

                if (framesSinceOnset < onsetFrames)
                    framesNeeded = onsetRivalFrames;

                if (rivalCount >= framesNeeded)
                {
                    pick = rival;
                    rivalCount = 0;
                }
            }
            else
            {
                rivalCount = 0;
            }
        }
    }

    if (pick < 0)
    {
        // Attaque (ou note perdue) : il faut un creux franc.
        if (deepest < 0 || deepestValue > onsetLimit)
        {
            reset();
            return result;
        }

        pick = shortestDipBelow (deepestValue * (1.0f + equalRatio) + equalFloor);
        if (pick < 0)
            pick = deepest;

        weakFrames = 0;
        rivalCount = 0;
        framesSinceOnset = 0;
    }

    ++framesSinceOnset;

    lastDip = evidence[(size_t) pick];

    // La décision est prise ; reste à lire la valeur précise de la période
    // autour du creux retenu. Sur une voix nette on la prend sur la mesure
    // instantanée (aucun retard) ; plus la voix est bruitée, plus on s'appuie
    // sur la courbe accumulée, bien moins agitée.
    const float instant = wasVoiced ? 0.25f + 0.75f * trust : 1.0f;
    const auto sharp = [this, instant] (int tau) noexcept
    {
        return instant * cmnd[(size_t) tau] + (1.0f - instant) * evidence[(size_t) tau];
    };

    {
        const int reach = std::max (2, pick / 16);
        const int from = std::max (tauMin, pick - reach);
        const int to = std::min (tauMax - 1, pick + reach);

        int sharpest = pick;
        for (int tau = from; tau <= to; ++tau)
            if (sharp (tau) < sharp (sharpest))
                sharpest = tau;

        pick = sharpest;
    }

    float tau = (float) pick;
    {
        const float a = sharp (pick - 1);
        const float b = sharp (pick);
        const float c = sharp (pick + 1);
        const float curve = a - 2.0f * b + c;
        if (curve > 1.0e-9f)
            tau += std::min (0.5f, std::max (-0.5f, 0.5f * (a - c) / curve));
    }

    wasVoiced = true;
    lastTau = tau;

    result.voiced = true;
    result.period = tau;
    result.centreLag = 0.5f * ((float) window + tau);
    return result;
}

} // namespace onirique::tune
