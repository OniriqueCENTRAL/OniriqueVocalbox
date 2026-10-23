#pragma once

#include <atomic>
#include "MiniDSP.h"

namespace onirique::mini
{
/*  DE-PLOSIVE : les « P », les « B » et le souffle dans la capsule disparaissent, la voix garde son corps. Dans VOCALBOX, il
    tourne dans la carte DE-ESSER / PLOSIVES (bouton PLOSIVES), dans la chaîne suréchantillonnée.

    Une plosive n'est pas « beaucoup de grave » : c'est une BOUFFÉE de pression sous ~120 Hz, brutale (elle monte en
    quelques millisecondes), qui dépasse nettement le grave habituel de la voix et qui ÉCRASE le médium (une voyelle
    grave ou une voix chaude ont toujours du médium en même temps ; une plosive presque pas).

    Ce qui le rend précis (il remplace l'ancien suppresseur de VOCALBOX, un shelf fixe à 180 Hz piloté par une seule détection) :
    - TROIS INDICES au lieu de deux : la bouffée au-dessus du grave habituel, la domination du grave sur le médium
      (300 Hz-3 kHz) COMPARÉE À L'ÉQUILIBRE HABITUEL de cette voix (un P fait sauter ce rapport d'un coup, même sous une
      voix qui chante déjà ; une note grave le garde), et la MONTÉE (le grave sur 0.3 ms contre le grave sur 5 ms) : une note grave tenue, un
      bourdonnement ou une voix de basse ne déclenchent rien, seul un choc le fait.
    - RÉFÉRENCE PROTÉGÉE : le grave habituel n'apprend que pendant le chant hors plosive (et très lentement sinon) :
      des P répétés ne relèvent pas la référence, le dixième est traité comme le premier.
    - DEUX ÉTAGES : le « pop » profond est retiré franchement (deux shelves identiques en cascade à 140 Hz : une pente
      raide, sans bosse, jusqu'à -30 dB sous 80 Hz) ; le corps de la voix
      (shelf entre 160 et 260 Hz) ne descend que si la plosive y déborde vraiment, d'au plus 9 dB, et sa fréquence suit
      l'étendue réelle de la bouffée. La voix qui suit le P garde sa chaleur.
    - MAINTIEN : la réduction tient tant que la bouffée dure (10 ms au moins), puis se relâche en 45 ms : le pop ne
      « repasse » pas en fin de souffle, le corps revient tout de suite derrière.
    - ANTICIPATION en MIX (2 ms) : la réduction est en place quand la première onde du pop arrive. En LIVE : latence
      nulle, détection en 0.3 ms.
    - Filtres à coefficients glissés (forme directe I) : aucune marche dans le signal ; hors plosive, les deux shelves
      sont à 0 dB EXACT : le signal sort strictement intact. Coupé : rien n'est calculé (seule l'anticipation reste en
      MIX, pour que la latence annoncée au DAW ne change jamais).
*/
class DePlosive
{
public:
    // multiple : l'anticipation est arrondie à un multiple de ce nombre (le facteur de suréchantillonnage de la chaîne : la
    // latence totale reste un nombre entier d'échantillons à la cadence de l'hôte)
    void prepare (double sampleRate, bool live, int multiple = 1)
    {
        fs = sampleRate;
        multiple = std::max (1, multiple);
        lookahead = live ? 0 : multiple * std::max (1, (int) std::lround (0.002 * fs / multiple));
        for (auto& d : delay) d.prepare (lookahead);
        constexpr double q = 0.7071067811865476;
        deepA.setCoeffs (BiquadCoeffs::lowpass (fs, 120.0, q));      // grave profond : Linkwitz-Riley 24 dB/oct
        deepB.setCoeffs (BiquadCoeffs::lowpass (fs, 120.0, q));
        bodyHp.setCoeffs (BiquadCoeffs::highpass (fs, 120.0, q));    // corps : 120-280 Hz
        bodyLp.setCoeffs (BiquadCoeffs::lowpass (fs, 280.0, q));
        midHp.setCoeffs (BiquadCoeffs::highpass (fs, 300.0, q));     // la voix : 300 Hz-3 kHz
        midLp.setCoeffs (BiquadCoeffs::lowpass (fs, std::min (3000.0, 0.45 * fs), q));
        fastAtk = coefFor (0.0003, fs); fastRel = coefFor (0.020, fs);
        slowAtk = coefFor (0.005, fs);  slowRel = coefFor (0.020, fs);
        midAtk = coefFor (0.001, fs);   midRel = coefFor (0.030, fs);
        habitC = coefFor (0.6, fs);     habitSlowC = coefFor (4.0, fs);  habitFastC = coefFor (0.1, fs);
        gAtk = coefFor (live ? 0.0003 : 0.001, fs);
        gRel = coefFor (0.045, fs);
        freqC = coefFor (0.005, fs);
        holdLen = (int) (0.010 * fs);
        reset();
    }

    void reset() noexcept
    {
        for (auto& d : delay) d.reset();
        for (auto* b : { &deepA, &deepB, &bodyHp, &bodyLp, &midHp, &midLp }) b->reset();
        deepShelf.reset(); deepShelf2.reset(); bodyShelf.reset();
        deepShelf.setNow (BiquadCoeffs::lowShelf (fs, kDeepHz, 0.707, 0.0));
        deepShelf2.setNow (BiquadCoeffs::lowShelf (fs, kDeepHz, 0.707, 0.0));
        bodyShelf.setNow (BiquadCoeffs::lowShelf (fs, 200.0, 0.7, 0.0));
        deepF = deepS = bodyF = midF = 0.0f;
        deepHabit = bodyHabit = 0.003f;   // ~ -50 dBFS : une référence prudente au départ
        domHabitDb = -6.0f;
        learned = 0.0f;
        redDeep = redBody = 0.0f;
        bodyHz = 200.0f;
        hold = countdown = 0;
        lastDeep = lastBody = 0.0f;
        meter.store (0.0f, std::memory_order_relaxed);
    }

    int getLatencySamples() const noexcept { return lookahead; }

    void set (bool enabled, float amount01) noexcept
    {
        on = enabled;
        amount = juce::jlimit (0.0f, 1.0f, amount01);
    }

    // réduction du grave profond (dB, positive), pour l'interface
    float getReductionDb() const noexcept { return meter.load (std::memory_order_relaxed); }

    void process (float* const* ch, int numCh, int n) noexcept
    {
        const bool active = on && amount > 0.0005f;
        if (! active && redDeep <= 0.0f && redBody <= 0.0f)
        {
            if (! wasIdle) { reset(); wasIdle = true; }
            if (lookahead > 0)
                for (int c = 0; c < numCh; ++c)
                    for (int i = 0; i < n; ++i) ch[c][i] = delay[c].process (ch[c][i]);
            return;
        }
        wasIdle = false;

        float peak = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            // ---- chaîne latérale (signal non retardé : en MIX, elle a 2 ms d'avance sur le son)
            float dp = 0.0f, bp = 0.0f, mp = 0.0f;
            for (int c = 0; c < numCh; ++c)
            {
                const float x = ch[c][i];
                dp = std::max (dp, std::abs (deepB.process (deepA.process (x, c), c)));
                bp = std::max (bp, std::abs (bodyLp.process (bodyHp.process (x, c), c)));
                mp = std::max (mp, std::abs (midLp.process (midHp.process (x, c), c)));
            }
            deepF += (dp > deepF ? fastAtk : fastRel) * (dp - deepF);
            deepS += (dp > deepS ? slowAtk : slowRel) * (dp - deepS);
            bodyF += (bp > bodyF ? fastAtk : fastRel) * (bp - bodyF);
            midF += (mp > midF ? midAtk : midRel) * (mp - midF);

            float tDeep = 0.0f, tBody = 0.0f;
            bool popping = false;
            if (active && deepF > 0.003f)   // ~ -50 dBFS : en dessous, rien à faire
            {
                const float burstDb = gainToDb ((deepF + 1.0e-6f) / (1.4f * deepHabit + 1.0e-5f));   // au-dessus du grave habituel
                const float domDb = gainToDb ((deepF + 1.0e-6f) / (midF + 1.0e-5f));                // le grave écrase la voix
                const float riseDb = gainToDb ((deepF + 1.0e-6f) / (deepS + 1.0e-6f));              // un choc, pas une note
                const float s1 = juce::jlimit (0.0f, 1.0f, (burstDb - 2.0f) / 8.0f);
                // domination comparée à l'équilibre grave / médium HABITUEL de cette voix (une note grave le garde, un P le
                // fait sauter), et jamais quand le grave reste nettement sous le médium
                const float s2 = juce::jlimit (0.0f, 1.0f, (domDb - domHabitDb - 8.0f) / 8.0f) * juce::jlimit (0.0f, 1.0f, (domDb + 3.0f) / 4.0f);
                lastDom = domDb;
                const float s3 = hold > 0 ? 1.0f : juce::jlimit (0.0f, 1.0f, (riseDb - 0.5f) / 2.5f);   // une fois saisi, on tient
                const float score = s1 * s2 * s3;
                if (score > 0.05f)
                {
                    popping = true;
                    tDeep = std::pow (amount, 0.6f) * std::min (30.0f, 6.0f + 1.3f * std::max (0.0f, burstDb)) * score;
                    // le corps ne descend que si la bouffée y déborde (et au plus de 9 dB)
                    const float bodyBurstDb = gainToDb ((bodyF + 1.0e-6f) / (1.4f * bodyHabit + 1.0e-5f));
                    const float spill = juce::jlimit (0.0f, 1.0f, (bodyBurstDb - 3.0f) / 9.0f);
                    tBody = std::min (9.0f, 0.45f * tDeep) * spill;
                    // fréquence du second étage : suit l'étendue de la bouffée (corps fort face au profond = plus haut)
                    const float reach = juce::jlimit (0.0f, 1.0f, (gainToDb ((bodyF + 1.0e-6f) / (deepF + 1.0e-6f)) + 12.0f) / 12.0f);
                    bodyHz += freqC * (160.0f + 100.0f * reach - bodyHz);
                }
            }
            if (popping) hold = holdLen; else if (hold > 0) --hold;
            // le grave habituel : appris pendant le chant hors plosive, très lentement sinon (un bourdonnement finit
            // par devenir l'habitude : il ne sera jamais traité comme une plosive)
            if (midF > 0.002f || deepF > 0.003f)
            {
                // la première demi-seconde de voix : la référence se cale vite (100 ms) sur CETTE voix
                const bool learning = learned < 0.5f;
                if (learning) learned += 1.0f / (float) fs;
                const float hc = learning ? habitFastC : (popping || hold > 0 ? habitSlowC : habitC);
                deepHabit += hc * (deepF - deepHabit);
                bodyHabit += hc * (bodyF - bodyHabit);
                if (midF > 0.002f) domHabitDb += hc * (juce::jlimit (-40.0f, 20.0f, gainToDb ((deepF + 1.0e-6f) / (midF + 1.0e-5f))) - domHabitDb);
            }

            const bool holding = hold > 0;
            redDeep += (tDeep > redDeep ? gAtk : (holding ? 0.0f : gRel)) * (tDeep - redDeep);
            redBody += (tBody > redBody ? gAtk : (holding ? 0.0f : gRel)) * (tBody - redBody);
            if (redDeep < 0.01f && ! holding) redDeep = 0.0f;
            if (redBody < 0.01f && ! holding) redBody = 0.0f;
            peak = std::max (peak, redDeep);

            // shelves dynamiques : nouveaux coefficients toutes les 4 échantillons, glissés entre deux
            if (--countdown <= 0)
            {
                countdown = 4;
                if (std::abs (redDeep - lastDeep) > 0.02f || (redDeep == 0.0f && lastDeep != 0.0f))
                {
                    const auto half = BiquadCoeffs::lowShelf (fs, kDeepHz, 0.707, -0.5 * redDeep);
                    deepShelf.glideTo (half, 4);
                    deepShelf2.glideTo (half, 4);
                    lastDeep = redDeep;
                }
                if (std::abs (redBody - lastBody) > 0.02f || (redBody == 0.0f && lastBody != 0.0f) || redBody > 0.0f)
                {
                    bodyShelf.glideTo (BiquadCoeffs::lowShelf (fs, bodyHz, 0.7, -redBody), 4);
                    lastBody = redBody;
                }
            }
            deepShelf.tick();
            deepShelf2.tick();
            bodyShelf.tick();
            for (int c = 0; c < numCh; ++c)
                ch[c][i] = bodyShelf.process (deepShelf2.process (deepShelf.process (delay[c].process (ch[c][i]), c), c), c);
        }
        meter.store (peak, std::memory_order_relaxed);
    }

private:
    double fs = 48000.0;
    int lookahead = 0, hold = 0, holdLen = 480, countdown = 0;
    bool on = false, wasIdle = true;
    float amount = 0.5f;
    DelayLine delay[2];
    Biquad deepA, deepB, bodyHp, bodyLp, midHp, midLp;
    static constexpr double kDeepHz = 140.0;
    SmoothBiquad deepShelf, deepShelf2, bodyShelf;   // deux shelves identiques en cascade : pente plus raide, sans bosse
    float fastAtk = 0, fastRel = 0, slowAtk = 0, slowRel = 0, midAtk = 0, midRel = 0, habitC = 0, habitSlowC = 0, habitFastC = 0;
    float learned = 0.0f;   // secondes de voix déjà entendues
    float gAtk = 0, gRel = 0, freqC = 0;
    float deepF = 0, deepS = 0, bodyF = 0, midF = 0, deepHabit = 0.003f, bodyHabit = 0.003f;
    float redDeep = 0, redBody = 0, bodyHz = 200.0f, lastDeep = 0, lastBody = 0, domHabitDb = -6.0f, lastDom = 0.0f;
    std::atomic<float> meter { 0.0f };
};
} // namespace onirique::mini
