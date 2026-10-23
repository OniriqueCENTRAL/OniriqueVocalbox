#pragma once

#include "MiniDSP.h"

namespace onirique::mini
{
/*  EQ de VOCAL MINI : quatre boutons bipolaires (-10..+10). Chaque bouton n'est pas une simple bande : c'est une courbe
    pensée pour la voix, différente vers le haut et vers le bas, qui reste musicale sur toute sa course.
    100 % IIR (aucune latence), calculé dans la chaîne suréchantillonnée x2 : les plateaux d'aigus gardent leur forme
    jusqu'en haut du spectre (pas de compression des courbes près de Nyquist).

    BODY (LOW, MID) : avant la compression.
      LOW +  : la poitrine de la voix : plateau large à 200 Hz + cloche de poids à 120 Hz, et un léger creux à 480 Hz
               pour que le corps ajouté ne tourne jamais au carton : plus chaude, plus pleine, sans boue.
      LOW -  : plateau à 230 Hz et creux à 130 Hz : retire le boom et l'effet de proximité, la voix s'allège.
      MID +  : la voix avance : cloche large à 1.4 kHz et mordant à 3 kHz.
      MID -  : la voix se nettoie : creux à 420 Hz (son « boîte », nasal), compensé à 2.3 kHz pour ne jamais sonner creux.

    SHINE (HI, AIR) : après la compression, avec une garde de sifflantes (les aigus ajoutés reculent de moitié pendant
    un S, et seulement pendant le S).
      HI +   : présence à 3.2 kHz (+1.8 dB au plus : la zone 2.5-4 kHz, celle de la dureté et de la fatigue, n'est jamais
               poussée de plus de ~4.5 dB, même HI et AIR à fond) + plateau à 6 kHz : ouverture progressive, sans dureté.
      HI -   : retire la dureté (3.6 kHz) et adoucit le haut (6.5 kHz).
      AIR +  : la clarté qui respire. Un plateau très large posé à 12 kHz et prolongé à 17.5 kHz, plus le GRAIN :
               une excitation harmonique douce, prise dans la voix entre 2.8 et 9 kHz, mise en forme par une saturation
               asymétrique à niveau constant (même grain que la voix soit douce ou forte), dont on ne garde que ce qui
               naît au-dessus de 6.5 kHz. Ce sont de vraies harmoniques de la voix (pas seulement plus d'aigus) :
               une brillance soyeuse, détaillée, jamais sifflante.
      AIR -  : haut arrondi, plus sombre et plus vintage (plateau à 9 kHz).
    LOW CUT : passe-haut Butterworth du 3e ordre (18 dB/oct) à 90 Hz, en fondu de 30 ms à l'enclenchement.
*/

// courbe de réponse d'une cascade (pour l'interface : la courbe affichée est exactement celle appliquée)
template <int N>
inline float cascadeDb (const BiquadCoeffs (&c)[N], const bool (&on)[N], double fs, double hz) noexcept
{
    std::complex<double> r { 1.0, 0.0 };
    for (int i = 0; i < N; ++i) if (on[i]) r *= c[i].response (fs, hz);
    return (float) (20.0 * std::log10 (std::max (1.0e-9, std::abs (r))));
}

// un filtre de courbe : il se réveille sans ancien état, glisse vers ses nouveaux coefficients, et ne quitte la chaîne
// qu'après avoir glissé jusqu'à 0 dB EXACT et laissé s'éteindre son état (~85 ms) : aucune marche dans le signal quand un
// bouton revient à zéro (un filtre retiré à +0.04 dB laissait un minuscule saut dans l'onde).
struct EqSlot
{
    static constexpr int kHold = 4096;
    SmoothBiquad f;
    bool on = false, wanted = false;
    int hold = 0;

    void reset() noexcept { f.reset(); on = wanted = false; hold = 0; }
    // c : coefficients voulus (0 dB exact quand want est faux)
    void update (const BiquadCoeffs& c, bool want) noexcept
    {
        wanted = want;
        if (want && ! on) { f.reset(); f.setNow (c); on = true; }
        else if (on) f.glideTo (c, 16);
        if (want) hold = kHold;
    }
    void retire (int n) noexcept { if (on && ! wanted && (hold -= n) <= 0) on = false; }
    void tick() noexcept { if (on) f.tick(); }
    float process (float x, int c) noexcept { return on ? f.process (x, c) : x; }
};

class BodyEQ
{
public:
    static constexpr int kF = 9;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        low.prepare (fs, 0.05, low.target());
        mid.prepare (fs, 0.05, mid.target());
        lc.prepare (fs, 0.03, lc.target());
        for (auto& f : flt) f.reset();
        lcA.reset(); lcB.reset();
        lcA.setCoeffs (BiquadCoeffs::highpass (fs, 90.0, 1.0));    // Butterworth 3e ordre = biquad Q 1 + 1er ordre
        lcB.setCoeffs (BiquadCoeffs::highpass1 (fs, 90.0));
        lastL = lastM = 99.0f;
        countdown = 0;
    }

    // low, mid : -1..1 ; lowCut : passe-haut 90 Hz
    void set (float l, float m, bool lowCut, bool snap) noexcept
    {
        low.set (juce::jlimit (-1.0f, 1.0f, l), snap);
        mid.set (juce::jlimit (-1.0f, 1.0f, m), snap);
        lc.set (lowCut ? 1.0f : 0.0f, snap);
        if (snap) { lastL = lastM = 99.0f; countdown = 0; }
    }

    void process (float* const* ch, int numCh, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const float l = low.next(), m = mid.next(), lcm = lc.next();
            if (--countdown <= 0)
            {
                if (std::abs (l - lastL) > 1.0e-5f || std::abs (m - lastM) > 1.0e-5f) update (l, m);
                for (auto& f : flt) f.retire (16);
                countdown = 16;
            }
            for (auto& f : flt) f.tick();
            for (int c = 0; c < numCh; ++c)
            {
                float s = ch[c][i];
                if (lcm > 0.0f) s += lcm * (lcB.process (lcA.process (s, c), c) - s);
                for (auto& f : flt) s = f.process (s, c);
                ch[c][i] = s;
            }
        }
    }

    static void design (double fs, float l, float m, BiquadCoeffs (&c)[kF], bool (&on)[kF]) noexcept
    {
        const float lp = taper (l, 1.15f), ln = taper (-l, 1.1f), mp = taper (m, 1.15f), mn = taper (-m, 1.1f);
        c[0] = BiquadCoeffs::lowShelf (fs, 200.0, 0.6, 5.5 * lp);
        c[1] = BiquadCoeffs::peaking  (fs, 120.0, 0.9, 1.5 * lp);
        c[2] = BiquadCoeffs::peaking  (fs, 480.0, 1.0, -1.5 * lp);
        c[3] = BiquadCoeffs::lowShelf (fs, 230.0, 0.6, -7.0 * ln);
        c[4] = BiquadCoeffs::peaking  (fs, 130.0, 0.8, -2.0 * ln);
        c[5] = BiquadCoeffs::peaking  (fs, 1400.0, 0.65, 5.0 * mp);
        c[6] = BiquadCoeffs::peaking  (fs, 3000.0, 1.0, 1.2 * mp);
        c[7] = BiquadCoeffs::peaking  (fs, 420.0, 0.8, -6.5 * mn);
        c[8] = BiquadCoeffs::peaking  (fs, 2300.0, 0.9, 0.8 * mn);
        for (int k = 0; k < kF; ++k) on[k] = (k < 3 ? lp : k < 5 ? ln : k < 7 ? mp : mn) > 1.0e-4f;
    }

private:
    void update (float l, float m) noexcept
    {
        BiquadCoeffs c[kF];
        bool now[kF];
        design (fs, l, m, c, now);
        for (int k = 0; k < kF; ++k) flt[k].update (c[k], now[k]);
        lastL = l; lastM = m;
    }

    double fs = 48000.0;
    Smooth low, mid, lc;
    EqSlot flt[kF];
    Biquad lcA, lcB;
    float lastL = 99.0f, lastM = 99.0f;
    int countdown = 0;
};

class ShineEQ
{
public:
    static constexpr int kCut = 3, kBoost = 4;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        hi.prepare (fs, 0.05, hi.target());
        air.prepare (fs, 0.05, air.target());
        for (auto& f : cut) f.reset();
        for (auto& f : boost) f.reset();
        // garde de sifflantes : bande 5-10 kHz comparée à la voix entière
        sibBp.setCoeffs (BiquadCoeffs::bandpass (fs, 6800.0, 1.0));
        sibBp.reset();
        sAtk = coefFor (0.0005, fs); sRel = coefFor (0.040, fs);
        fAtk = coefFor (0.003, fs);  fRel = coefFor (0.060, fs);
        envS = envF = guard = 0.0f;
        // GRAIN : excitation harmonique
        excInHp.setCoeffs (BiquadCoeffs::highpass (fs, 2800.0, 0.707));
        // en LIVE (cadence du projet), la voix qui nourrit le grain s'arrête à 7 kHz : ses harmoniques restent sous Nyquist
        excInLp.setCoeffs (BiquadCoeffs::lowpass (fs, std::min (fs < 60000.0 ? 7000.0 : 9000.0, 0.45 * fs), 0.707));
        excHp1.setCoeffs (BiquadCoeffs::highpass (fs, 6500.0, 0.5412));   // Butterworth 4e ordre
        excHp2.setCoeffs (BiquadCoeffs::highpass (fs, 6500.0, 1.3066));
        excLp.setCoeffs (BiquadCoeffs::lowpass (fs, std::min (18500.0, 0.45 * fs), 0.707));
        for (auto* b : { &excInHp, &excInLp, &excHp1, &excHp2, &excLp }) b->reset();
        excAtk = coefFor (0.001, fs); excRel = coefFor (0.030, fs);
        excEnv[0] = excEnv[1] = 0.0f;
        lastH = lastA = 99.0f;
        countdown = 0;
    }

    // hi, air : -1..1
    void set (float h, float a, bool snap) noexcept
    {
        hi.set (juce::jlimit (-1.0f, 1.0f, h), snap);
        air.set (juce::jlimit (-1.0f, 1.0f, a), snap);
        if (snap) { lastH = lastA = 99.0f; countdown = 0; }
    }

    // réduction de la garde de sifflantes (0..1), pour l'interface
    float getGuard() const noexcept { return guardMeter.load (std::memory_order_relaxed); }

    void process (float* const* ch, int numCh, int n) noexcept
    {
        float guardPk = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float h = hi.next(), a = air.next();
            if (--countdown <= 0)
            {
                if (std::abs (h - lastH) > 1.0e-5f || std::abs (a - lastA) > 1.0e-5f) update (h, a);
                for (auto& f : cut) f.retire (16);
                for (auto& f : boost) f.retire (16);
                countdown = 16;
            }
            for (auto& f : cut) f.tick();
            for (auto& f : boost) f.tick();

            // ---- garde : quand un S passe, ce que HI / AIR ajoutent recule (jusqu'à -50 %)
            float sb = 0.0f, fb = 0.0f;
            for (int c = 0; c < numCh; ++c)
            {
                sb = std::max (sb, std::abs (sibBp.process (ch[c][i], c)));
                fb = std::max (fb, std::abs (ch[c][i]));
            }
            envS += (sb > envS ? sAtk : sRel) * (sb - envS);
            envF += (fb > envF ? fAtk : fRel) * (fb - envF);
            const float ratio = envS / (envF + 1.0e-6f);
            const float target = juce::jlimit (0.0f, 1.0f, (ratio - 0.40f) * 2.5f);
            guard += (target > guard ? sAtk * 4.0f : sRel) * (target - guard);
            guardPk = std::max (guardPk, guard);
            const float dyn = 1.0f - 0.5f * guard;

            const float grain = excAmt;   // 0 quand AIR <= 0
            for (int c = 0; c < numCh; ++c)
            {
                const float x = ch[c][i];
                float s = x;
                for (auto& f : cut) s = f.process (s, c);
                float b = s;
                for (auto& f : boost) b = f.process (b, c);
                float y = s + dyn * (b - s);

                // ---- GRAIN : la voix 2.8-9 kHz, ramenée à un niveau constant, saturée en douceur (harmoniques paires
                //      et impaires), et seules les harmoniques nées au-dessus de 6.5 kHz sont gardées
                if (grain > 0.0f)
                {
                    const float e = excInLp.process (excInHp.process (x, c), c);
                    const float ae = std::abs (e);
                    excEnv[c] += (ae > excEnv[c] ? excAtk : excRel) * (ae - excEnv[c]);
                    const float z = e / (excEnv[c] + 1.0e-5f) * 0.9f;
                    const float shaped = std::tanh (z) + 0.22f * z * z / (1.0f + 0.3f * z * z);
                    float harm = excHp2.process (excHp1.process (shaped * excEnv[c], c), c);
                    harm = excLp.process (harm, c);
                    y += grain * dyn * harm;
                }
                ch[c][i] = y;
            }
        }
        guardMeter.store (guardPk, std::memory_order_relaxed);
    }

    // courbe statique de HI / AIR (sans le grain ni la garde) : cut puis boost
    static void design (double fs, float h, float a, BiquadCoeffs (&c)[kCut], bool (&cOn)[kCut],
                        BiquadCoeffs (&b)[kBoost], bool (&bOn)[kBoost]) noexcept
    {
        const float hp = taper (h, 1.1f), hn = taper (-h, 1.1f), ap = taper (a, 1.1f), an = taper (-a, 1.1f);
        c[0] = BiquadCoeffs::peaking   (fs, 3600.0, 0.9, -4.0 * hn);
        c[1] = BiquadCoeffs::highShelf (fs, 6500.0, 0.6, -3.0 * hn);
        c[2] = BiquadCoeffs::highShelf (fs, 9000.0, 0.5, -8.0 * an);
        cOn[0] = cOn[1] = hn > 1.0e-4f;
        cOn[2] = an > 1.0e-4f;
        b[0] = BiquadCoeffs::peaking   (fs, 3200.0, 0.7, 1.8 * hp);
        b[1] = BiquadCoeffs::highShelf (fs, 6000.0, 0.55, 6.5 * hp);
        b[2] = BiquadCoeffs::highShelf (fs, std::min (12000.0, 0.40 * fs), 0.42, 7.0 * ap);
        b[3] = BiquadCoeffs::highShelf (fs, std::min (17500.0, 0.42 * fs), 0.6, 3.0 * ap);
        bOn[0] = bOn[1] = hp > 1.0e-4f;
        bOn[2] = bOn[3] = ap > 1.0e-4f;
    }

private:
    void update (float h, float a) noexcept
    {
        BiquadCoeffs c[kCut], b[kBoost];
        bool cNow[kCut], bNow[kBoost];
        design (fs, h, a, c, cNow, b, bNow);
        for (int k = 0; k < kCut; ++k) cut[k].update (c[k], cNow[k]);
        for (int k = 0; k < kBoost; ++k) boost[k].update (b[k], bNow[k]);
        const float ap = taper (a, 1.1f);
        if (ap > 1.0e-4f && excAmt <= 0.0f)
        {
            for (auto* f : { &excInHp, &excInLp, &excHp1, &excHp2, &excLp }) f->reset();
            excEnv[0] = excEnv[1] = 0.0f;
        }
        excAmt = 0.5f * ap;   // le grain arrive avec AIR et grandit avec lui
        lastH = h; lastA = a;
    }

    double fs = 48000.0;
    Smooth hi, air;
    EqSlot cut[kCut], boost[kBoost];
    Biquad sibBp, excInHp, excInLp, excHp1, excHp2, excLp;
    float sAtk = 0, sRel = 0, fAtk = 0, fRel = 0, envS = 0, envF = 0, guard = 0;
    float excAtk = 0, excRel = 0, excEnv[2] {}, excAmt = 0.0f;
    float lastH = 99.0f, lastA = 99.0f;
    int countdown = 0;
    std::atomic<float> guardMeter { 0.0f };
};
} // namespace onirique::mini
