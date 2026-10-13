#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include "Biquad.h"
#include "Common.h"

namespace onirique
{
/*  NOISE GATE "ultra transparent" (repris d'ONIRIQUE BROADCAST, sans la garde d'écho) : premier module du VocalBox,
    juste après INPUT, à la cadence de l'hôte.

    Ce qui le rend inaudible :
    - Détecteur sur une chaîne latérale FILTRÉE (passe-haut 90 Hz + passe-bas 7 kHz) : le grondement, les
      chocs de bureau, la ventilation et le souffle n'ouvrent pas le gate ; la voix, si.
    - Détection RMS courte (3 ms) combinée à la crête : réagit au corps de la voix sans se laisser tromper
      par un clic isolé.
    - Anticipation de 1.5 ms (mode MIX) : l'audio est retardé, le détecteur non. Le gate est déjà ouvert quand la
      première consonne arrive : aucune attaque mangée, aucun "clic" d'ouverture. En LIVE : pas d'anticipation (latence nulle).
    - Hystérésis : ouverture au seuil, fermeture HYST dB plus bas. Pas de battement autour du seuil.
    - HOLD : le gate reste ouvert entre les mots (évite le hachage des fins de phrases).
    - Gain lissé dans le domaine dB (attaque / relâchement réglables), puis un second lissage de 1 ms sur
      le gain linéaire : courbe en S, aucun angle, donc aucune distorsion de modulation.
    - RANGE : le gate n'éteint pas le son, il l'abaisse de RANGE dB (24 dB par défaut). Le fond de la pièce
      reste naturel derrière la voix ; à 80 dB il devient un vrai mute.
    - Désactivé : la latence (anticipation) est conservée et le gain revient en douceur à 0 dB : allumer ou
      couper le module ne fait jamais sauter le son.

    Mesures publiées pour l'interface : niveau détecté, gain appliqué, état, et une estimation du bruit de
    fond (minimum glissant du niveau RMS) qui sert au bouton LEARN.
*/
class NoiseGate
{
public:
    static constexpr double kLookaheadSec = 0.0015;

    struct Meter
    {
        std::atomic<float> levelDb { -120.0f }, gainDb { 0.0f }, floorDb { -120.0f };
        std::atomic<bool> open { false };
    };

    void prepare (double sampleRate, bool live = false)
    {
        fs = sampleRate;
        lookahead = live ? 0 : std::max (1, (int) std::lround (kLookaheadSec * fs));
        for (auto& d : delay) d.prepare (lookahead);
        scHp.setCoeffs (BiquadCoeffs::highpass (fs, 90.0, 0.707));
        scLp.setCoeffs (BiquadCoeffs::lowpass (fs, 7000.0, 0.707));
        rmsC = coef (3.0);
        peakRel = coef (40.0);
        linC = coef (1.0);
        floorWin = coef (60.0);
        floorRise = (float) (3.0 / fs);            // le plancher remonte de 3 dB/s quand le bruit monte
        reset();
        updateTimes();
    }

    void reset() noexcept
    {
        for (auto& d : delay) d.reset();
        scHp.reset(); scLp.reset();
        ms = peak = 0.0f;
        gDb = targetDb = 0.0f;
        gLin = 1.0f;
        holdLeft = 0;
        isOpen = true;
        floorMs = 0.0f;
        floorDb = -90.0f;
    }

    int getLatencySamples() const noexcept { return lookahead; }

    // thresholdDb -80..0, rangeDb 0..80 (atténuation), attackMs 0.1..50, holdMs 0..500, releaseMs 5..2000, hystDb 0..12
    void setParams (bool enabled, float thresholdDb, float rangeDb, float attackMs, float holdMs, float releaseMs, float hystDb) noexcept
    {
        on = enabled;
        thr = juce::jlimit (-90.0f, 0.0f, thresholdDb);
        range = juce::jlimit (0.0f, 90.0f, rangeDb);
        hyst = juce::jlimit (0.0f, 20.0f, hystDb);
        holdSamples = (int) (juce::jlimit (0.0f, 2000.0f, holdMs) * 0.001 * fs);
        if (attackMs != atkMs || releaseMs != relMs)
        {
            atkMs = juce::jlimit (0.05f, 200.0f, attackMs);
            relMs = juce::jlimit (5.0f, 5000.0f, releaseMs);
            updateTimes();
        }
    }

    void process (float* const* ch, int numCh, int n) noexcept
    {
        float maxLvl = -120.0f, minGain = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            // ---- chaîne latérale filtrée (mono : max des canaux)
            float sc = 0.0f;
            for (int c = 0; c < numCh; ++c)
                sc = std::max (sc, std::abs (scLp.process (scHp.process (ch[c][i], c), c)));

            ms += rmsC * (sc * sc - ms);
            peak = sc > peak ? sc : peak + peakRel * (sc - peak);
            // niveau : RMS ramené à la crête d'un sinus, la crête ne compte qu'à moitié (pas d'ouverture sur un clic)
            const float rmsLvl = std::sqrt (2.0f * ms);
            const float det = std::max (rmsLvl, 0.5f * peak);
            const float lvl = det > 1.0e-6f ? gainToDb (det) : -120.0f;
            maxLvl = std::max (maxLvl, lvl);

            // ---- bruit de fond : minimum glissant du RMS (fenêtre 60 ms), remonte lentement
            floorMs += floorWin * (sc * sc - floorMs);
            const float fl = floorMs > 1.0e-12f ? 10.0f * std::log10 (2.0f * floorMs) : -120.0f;
            floorDb = fl < floorDb ? fl : floorDb + floorRise;

            // ---- état : ouverture au seuil, fermeture (après HOLD) HYST dB plus bas
            if (! on)
            {
                isOpen = true;
                holdLeft = holdSamples;
            }
            else if (lvl >= thr)
            {
                isOpen = true;
                holdLeft = holdSamples;
            }
            else if (isOpen && lvl < thr - hyst)
            {
                if (holdLeft > 0) --holdLeft;
                else isOpen = false;
            }
            else if (isOpen && holdLeft < holdSamples)
                holdLeft = std::min (holdSamples, holdLeft + 1);   // repassé entre les deux seuils : on recharge doucement

            targetDb = isOpen ? 0.0f : -range;

            // ---- gain : lissage en dB (attaque / relâchement), puis lissage linéaire 1 ms (courbe en S)
            gDb += (targetDb > gDb ? atkC : relC) * (targetDb - gDb);
            const float gt = gDb > -89.5f ? dbToGain (gDb) : 0.0f;
            gLin += linC * (gt - gLin);
            minGain = std::min (minGain, gDb);

            for (int c = 0; c < numCh; ++c)
                ch[c][i] = delay[c].process (ch[c][i]) * gLin;
        }

        meter.levelDb.store (maxLvl, std::memory_order_relaxed);
        meter.gainDb.store (minGain, std::memory_order_relaxed);
        meter.floorDb.store (floorDb, std::memory_order_relaxed);
        meter.open.store (isOpen, std::memory_order_relaxed);
    }

    const Meter& getMeter() const noexcept { return meter; }

private:
    float coef (double timeMs) const noexcept { return (float) (1.0 - std::exp (-1.0 / (timeMs * 0.001 * fs))); }

    // en dB, une constante de temps T fait parcourir ~63 % de l'écart : on la cale pour que l'attaque
    // / le relâchement réglés correspondent à ~90 % du trajet (ressenti naturel des temps affichés)
    void updateTimes() noexcept
    {
        atkC = coef (atkMs / 2.3f);
        relC = coef (relMs / 2.3f);
    }

    double fs = 48000.0;
    int lookahead = 72;
    DelayLine delay[2];
    Biquad scHp, scLp;

    bool on = false, isOpen = true;
    float thr = -50.0f, range = 24.0f, hyst = 4.0f, atkMs = 2.0f, relMs = 180.0f;
    int holdSamples = 0, holdLeft = 0;

    float rmsC = 0, peakRel = 0, linC = 0, atkC = 0, relC = 0;
    float ms = 0, peak = 0, gDb = 0, targetDb = 0, gLin = 1.0f;
    float floorMs = 0, floorDb = -90.0f, floorWin = 0, floorRise = 0;
    Meter meter;
};
} // namespace onirique
