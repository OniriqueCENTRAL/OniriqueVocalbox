#pragma once

#include <array>
#include <atomic>
#include "MiniDSP.h"

namespace onirique::mini
{
// PULSE : durée d'une respiration de la queue, en noires
inline constexpr int kNumPulseRates = 7;
inline const char* const kPulseRateNames[kNumPulseRates] = { "1/2", "1/4", "1/8", "1/16", "1/4T", "1/8T", "1/8D" };
inline constexpr double kPulseRateBeats[kNumPulseRates] = { 2.0, 1.0, 0.5, 0.25, 2.0 / 3.0, 1.0 / 3.0, 0.75 };

/*  REVERB de VOCAL MINI PRO : le moteur d'ONIRIQUE VERB (modes, CLEAN, PULSE) réuni avec celui des anciennes reverbs de
    VOCAL MINI (densité, entrée stéréo, voix propre, ducking sans bond), en un seul espace entièrement réglable.

    Moteur : réseau de 16 lignes à retards modulés (FDN, matrice de Hadamard normalisée : sans perte, donc stable par
    construction ; la décroissance ne vient que de l'amortissement). Amortissement calculé pour le DECAY (méthode de
    Jot : graves et aigus décroissent chacun à leur temps, la queue s'assombrit naturellement). Un passe-tout dans chaque
    ligne, à l'intérieur de la boucle : la queue est dense dès les premières millisecondes, sans « flutter ». Diffusion
    d'entrée par 4 passe-tout, décorrélation de sortie par 2 passe-tout différents à gauche et à droite.

    Les cinq modes ont chacun leur caractère (taille, réflexions précoces, tenue des aigus, densité, couleur, modulation,
    largeur), réglé pour la voix :
      ROOM    : petite pièce, réflexions précoces marquées, aigus vite amortis : épaissit et place la voix.
      PLATE 1 : plaque brillante et dense, sans réflexions séparées : la reverb de voix lisse et présente.
      PLATE 2 : plaque plus sombre, plus large et plus modulée : douce et enveloppante.
      HALL    : grande salle, réflexions lointaines, queue profonde.
      SHIMMER : l'octave supérieure réinjectée dans la queue (décaleur de hauteur à deux têtes en fondu sin²) : un voile
                harmonique qui monte et chatoie.

    Réglages :
      MIX       : l'ENVOI (piste auxiliaire) : la voix reste entière, le bouton règle ce qui part dans la reverb ; quand on
                  le baisse, la queue déjà partie s'éteint naturellement.
      PRE-DELAY : 0 à 250 ms avant la reverb (la voix garde son attaque et son intelligibilité). Glissé : aucun clic.
      DECAY     : temps de décroissance (RT60) 0.2 à 12 s, stable quel que soit SIZE.
      SIZE      : longueur des lignes ; changer SIZE fait glisser les retards (comme une bande qui ralentit) : aucun clic.
      DIFFUSION : diffusion d'entrée et densité de la boucle : bas = échos discernables, haut = queue lisse.
      MOD RATE / MOD DEPTH : modulation des retards ; la profondeur est bornée en HAUTEUR (au plus ~12 cents, quelle que
                  soit la vitesse) : elle casse les résonances métalliques sans jamais faire chanter faux la queue.
      LOW CUT / HIGH CUT : filtres 12 dB/oct sur la queue SEULE (la voix directe n'est jamais touchée), glissés.
      WIDTH     : largeur de la queue (M/S, 0 à 150 %). Les sorties gauche / droite sont décorrélées (corrélation proche
                  de 0) mais jamais en opposition : en mono, la queue reste là, elle ne s'annule pas.
      DUCKING   : la queue s'efface sous la voix (jusqu'à -24 dB) ; quand la voix s'arrête, elle attend 200 ms puis
                  remonte moins vite qu'elle ne s'éteint : jamais de bond, jamais de pompage.
      CLEAN     : chaque nouvelle phrase repart propre. Les sifflantes ne partent pas dans la reverb, les bas-médiums
                  sont creusés dans la queue, et à chaque reprise après un silence (>180 ms) l'ancienne queue est
                  VIDÉE du réseau (amortissement renforcé pendant ~120 ms) et le retour s'efface un instant : les queues
                  ne s'empilent plus d'une phrase à l'autre.
      PULSE     : la queue respire au tempo (calée sur la mesure quand le DAW joue), de 1/2 à 1/16.
    Mesures publiées pour l'interface : historique de la voix, de la queue entendue et de ce que CLEAN / DUCKING retirent.
*/
class VoiceReverb
{
public:
    static constexpr int kLines = 16;
    static constexpr int kHistory = 512;   // une valeur par tranche de 256 échantillons (~2.7 s à 48 kHz)
    enum Mode { room = 0, plate1, plate2, hall, shimmer, numModes };

    struct Params
    {
        int mode = plate1;
        float send = 0.15f;                                            // MIX (0..1)
        float decay = 1.2f, preDelayMs = 15.0f, size = 0.5f, diffusion = 0.8f, modRate = 0.6f, modDepth = 0.35f;
        float lowCut = 200.0f, highCut = 9000.0f, width = 1.0f;
        float ducking = 0.25f, clean = 0.4f, pulse = 0.0f;              // 0..1
        int pulseRate = 2;
    };

    struct Character { float sizeMul, erGain, erSize, hfRatio, diff, density, shimmer, bright, mod, side, trim; };
    static Character character (int mode) noexcept
    {
        switch (mode)
        {
            case room:    return { 0.42f, 0.75f, 0.50f, 0.50f, 0.80f, 0.60f, 0.00f,  0.0f, 0.5f, 0.6f, 1.00f };
            case plate2:  return { 0.95f, 0.00f, 0.00f, 0.50f, 1.00f, 0.70f, 0.00f, -1.0f, 1.3f, 0.9f, 1.00f };
            case hall:    return { 1.40f, 0.30f, 1.10f, 0.55f, 0.90f, 0.58f, 0.00f,  0.0f, 1.0f, 0.7f, 1.00f };
            case shimmer: return { 1.20f, 0.00f, 0.00f, 0.65f, 1.00f, 0.65f, 0.25f,  0.3f, 1.4f, 0.8f, 1.00f };
            default:      return { 0.70f, 0.10f, 0.50f, 0.75f, 1.00f, 0.68f, 0.00f,  1.0f, 0.8f, 0.6f, 1.00f };   // PLATE 1
        }
    }

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        for (auto& l : lines) l.prepare ((int) (0.30 * fs));
        static constexpr float apMs[kLines] { 2.3f, 3.1f, 2.7f, 3.7f, 4.3f, 2.9f, 3.3f, 4.1f, 2.1f, 3.9f, 4.7f, 2.5f, 3.5f, 4.9f, 2.2f, 3.6f };
        for (int k = 0; k < kLines; ++k) loopAp[k].prepare (std::max (8, (int) (apMs[k] * 0.001f * (float) fs)));
        preMid.prepare ((int) (0.30 * fs));
        preSide.prepare ((int) (0.30 * fs));
        erLine.prepare ((int) (0.12 * fs));
        shimBuf.prepare ((int) (0.2 * fs));
        static constexpr int apLen[4] { 142, 107, 379, 277 };      // passe-tout de diffusion (à 29 761 Hz)
        for (int a = 0; a < 4; ++a) inAp[a].prepare ((int) (apLen[a] * fs / 29761.0));
        static constexpr float outApMs[4] { 3.1f, 4.7f, 3.9f, 5.3f };
        for (int a = 0; a < 4; ++a) outAp[a].prepare ((int) (outApMs[a] * 0.001 * fs));
        send.prepare (fs, 0.05, send.target());
        preS.reset (fs, 0.12);
        preS.setCurrentAndTargetValue (preSamples());
        duck.prepare (fs);
        inHp.setCoeffs (BiquadCoeffs::highpass (fs, 100.0, 0.707));
        sibHp.setCoeffs (BiquadCoeffs::highpass (fs, std::min (5000.0, 0.4 * fs), 0.707));
        erLp.setCoeffs (BiquadCoeffs::lowpass (fs, std::min (6500.0, 0.45 * fs), 0.707));
        shimLp.setCoeffs (BiquadCoeffs::lowpass (fs, std::min (7000.0, 0.45 * fs), 0.707));
        shimHp.setCoeffs (BiquadCoeffs::highpass (fs, 300.0, 0.707));
        sibAtk = coefFor (0.001, fs); sibRel = coefFor (0.060, fs);
        gainSmooth = coefFor (0.015, fs);
        pulseSmooth = coefFor (0.004, fs);
        loopSmooth = coefFor (0.010, fs);
        lenGlide = coefFor (0.12, fs);
        paramGlide = coefFor (0.02, fs);
        onAtk = coefFor (0.005, fs); onRel = coefFor (0.040, fs);
        peakFall = (float) std::exp (-1.0 / (1.5 * fs));
        clearFastRel = coefFor (0.120, fs); clearSlowRel = coefFor (0.350, fs);
        gapLen = (int) (0.18 * fs);
        shimWin = (float) (0.06 * fs);
        reset();
        update (true);
    }

    void reset() noexcept
    {
        for (auto& l : lines) l.reset();
        for (auto& a : loopAp) a.reset();
        for (auto& a : inAp) a.reset();
        for (auto& a : outAp) a.reset();
        preMid.reset(); preSide.reset(); erLine.reset(); shimBuf.reset();
        for (auto& s : lpState) s = 0.0f;
        for (auto* b : { &inHp, &sibHp, &erLp, &shimLp, &shimHp }) b->reset();
        for (auto* b : { &mud, &tilt, &lc1, &hc1 }) b->reset();
        sibEnvH = sibEnvF = sibCut = 0.0f;
        duck.reset();
        shimPhase = 0.0f;
        envFast = voicePeak = clearFast = clearSlow = 0.0f;
        quiet = gapLen;
        loopS = dipS = pulseG = 1.0f;
        tail = 0.0f;
        for (int k = 0; k < kLines; ++k) { lfo[k] = (double) k * 0.73; lenNow[k] = lineLen[k]; }
        erG = erTarget; shimG = shimTarget;
    }

    void set (const Params& p, bool snap) noexcept
    {
        Params q = p;
        q.mode = juce::jlimit (0, (int) numModes - 1, p.mode);
        q.decay = juce::jlimit (0.15f, 20.0f, p.decay);
        q.size = juce::jlimit (0.0f, 1.0f, p.size);
        q.diffusion = juce::jlimit (0.0f, 1.0f, p.diffusion);
        q.lowCut = juce::jlimit (20.0f, 2000.0f, p.lowCut);
        q.highCut = juce::jlimit (1000.0f, 20000.0f, p.highCut);
        q.width = juce::jlimit (0.0f, 1.5f, p.width);
        q.pulseRate = juce::jlimit (0, kNumPulseRates - 1, p.pulseRate);
        const bool dirty = q.mode != prm.mode || q.decay != prm.decay || q.size != prm.size || q.diffusion != prm.diffusion
                        || q.clean != prm.clean;
        prm = q;
        send.set (std::pow (juce::jlimit (0.0f, 1.0f, p.send), 1.3f), snap);
        if (snap) preS.setCurrentAndTargetValue (preSamples()); else preS.setTargetValue (preSamples());
        if (dirty || snap) update (snap);
    }

    float getTail() const noexcept { return tailMeter.load (std::memory_order_relaxed); }
    bool isActive() const noexcept { return ! send.idle() || tail > 2.0e-5f; }

    // historique pour l'interface : voix, queue entendue, et queue avant CLEAN / DUCKING (ce qui a été retiré)
    int getHistoryPos() const noexcept { return histPos.load (std::memory_order_acquire); }
    void getHistory (int i, float& dry, float& wet, float& before) const noexcept
    {
        const auto k = (size_t) ((i % kHistory + kHistory) % kHistory);
        dry = histDry[k].load (std::memory_order_relaxed);
        wet = histWet[k].load (std::memory_order_relaxed);
        before = histPre[k].load (std::memory_order_relaxed);
    }
    float getCleanAmount() const noexcept { return cleanMeter.load (std::memory_order_relaxed); }   // 0..1, ce que CLEAN retire
    float getDuckDb() const noexcept { return duckMeter.load (std::memory_order_relaxed); }

    // la voix (in) entre dans la reverb ; son retour s'ajoute dans wet. bpm, ppq : tempo et position (ppq < 0 : arrêt)
    void process (const float* const* in, float* const* wet, int numCh, int n, double bpm, double ppq) noexcept
    {
        if (! isActive())
        {
            if (! wasIdle) { reset(); wasIdle = true; }
            tailMeter.store (0.0f, std::memory_order_relaxed);
            pushHistory (0.0f, 0.0f, 0.0f, n);
            return;
        }
        wasIdle = false;
        const float apG[4] { 0.75f * diffAmt, 0.75f * diffAmt, 0.625f * diffAmt, 0.625f * diffAmt };
        const double lfoInc = 2.0 * juce::MathConstants<double>::pi * prm.modRate / fs;
        // profondeur bornée en hauteur : écart de hauteur max = 2 pi f A ; 100 % -> ~12 cents (x caractère du mode)
        const float modSec = juce::jlimit (0.00005f, 0.003f, 0.0069f * prm.modDepth * modMul / (6.2831853f * std::max (0.05f, prm.modRate)));
        const float modSamples = modSec * (float) fs;
        const double beatsPerSample = std::max (20.0, bpm) / (60.0 * fs);
        const double period = kPulseRateBeats[prm.pulseRate];
        float pk = 0.0f, dryPk = 0.0f, wetPk = 0.0f, prePk = 0.0f, cleanPk = 0.0f, duckMin = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            const float xl = in[0][i], xr = numCh > 1 ? in[1][i] : xl;
            const float s = send.next();
            float mid = 0.5f * (xl + xr), side = 0.5f * (xl - xr);
            if ((i & 31) == 0) glideFilters();
            mud.tick(); tilt.tick(); lc1.tick(); hc1.tick();

            // ---- CLEAN : une reprise après un silence vide l'ancienne queue (la voix elle-même, avant l'envoi)
            const float a = std::abs (mid);
            envFast += (a > envFast ? onAtk : onRel) * (a - envFast);
            voicePeak = std::max (envFast, voicePeak * peakFall);
            if (envFast > std::max (0.002f, 0.08f * voicePeak))
            {
                if (quiet >= gapLen && prm.clean > 0.001f) { clearFast = 1.0f; clearSlow = 1.0f; }
                quiet = 0;
            }
            else if (quiet < gapLen) ++quiet;
            clearFast -= clearFastRel * clearFast;
            clearSlow -= clearSlowRel * clearSlow;
            loopS += loopSmooth * (1.0f - 0.8f * prm.clean * clearFast - loopS);
            dipS += loopSmooth * (1.0f - 0.5f * prm.clean * clearSlow - dipS);

            // ---- voix propre : coupe-bas et sifflantes retirées avant d'entrer
            mid = inHp.process (mid, 0);
            side = inHp.process (side, 1);
            const float hi = sibHp.process (mid, 0);
            const float ah = std::abs (hi), af = std::abs (mid);
            sibEnvH += (ah > sibEnvH ? sibAtk : sibRel) * (ah - sibEnvH);
            sibEnvF += (af > sibEnvF ? sibAtk : sibRel) * (af - sibEnvF);
            const float cut = juce::jlimit (0.0f, 1.0f, (sibEnvH / (sibEnvF + 1.0e-6f) - 0.3f) * 3.0f);
            sibCut += gainSmooth * (cut - sibCut);
            mid = (mid - 0.85f * sibCut * hi) * s;
            side *= s * (1.0f - 0.85f * sibCut);

            const float pre = preS.getNextValue();
            preMid.push (mid);
            preSide.push (side);
            float x = preMid.read (pre);
            const float sd = preSide.read (pre);

            // premières réflexions
            erG += paramGlide * (erTarget - erG);
            erLine.push (x);
            float erL = 0.0f, erR = 0.0f;
            if (erG > 1.0e-4f)
            {
                for (int t = 0; t < kTaps; ++t)
                {
                    const float v = erLine.tap (erTap[t]) * erAmp[t];
                    (t & 1 ? erR : erL) += v;
                }
                erL = erLp.process (erL, 0) * erG;
                erR = erLp.process (erR, 1) * erG;
            }

            // diffusion d'entrée
            for (int k = 0; k < 4; ++k) x = inAp[k].process (x, apG[k]);

            // octave réinjectée (SHIMMER)
            shimG += paramGlide * (shimTarget - shimG);
            float shim = 0.0f;
            if (shimG > 1.0e-4f)
            {
                shimPhase += 1.0f / shimWin;
                if (shimPhase >= 1.0f) shimPhase -= 1.0f;
                const float pA = shimPhase, pB = std::fmod (shimPhase + 0.5f, 1.0f);
                const float sa = shimBuf.read (2.0f + (1.0f - pA) * shimWin), sb = shimBuf.read (2.0f + (1.0f - pB) * shimWin);
                const float wa = std::sin (pA * 3.1415927f), wb = std::sin (pB * 3.1415927f);
                shim = shimLp.process (shimHp.process (sa * wa * wa + sb * wb * wb, 0), 0) * shimG;
            }

            // réseau : lecture modulée, passe-tout de densité, amortissement, Hadamard, réinjection
            float y[kLines];
            for (int k = 0; k < kLines; ++k)
            {
                lenNow[k] += lenGlide * (lineLen[k] - lenNow[k]);
                lfo[k] += lfoInc * lfoMul[k];
                float v = lines[k].read (lenNow[k] + modSamples * (float) std::sin (lfo[k]));
                v = loopAp[k].process (v, density);
                lpState[k] = absB[k] * v + absA[k] * lpState[k];
                y[k] = lpState[k] * loopS;
            }
            float h[kLines];
            std::copy (y, y + kLines, h);
            for (int len = 1; len < kLines; len <<= 1)
                for (int st = 0; st < kLines; st += len << 1)
                    for (int j = st; j < st + len; ++j)
                    {
                        const float u = h[j], w = h[j + len];
                        h[j] = u + w; h[j + len] = u - w;
                    }
            const float inj = x + shim;
            for (int k = 0; k < kLines; ++k)
                lines[k].push (0.25f * h[k] + (inj * inSign[k] + sd * sideSign[k]) * 0.35f);

            float wl = 0.0f, wr = 0.0f;
            for (int k = 0; k < kLines; ++k) { wl += y[k] * outL[k]; wr += y[k] * outR[k]; }
            outG += paramGlide * (outTarget - outG);
            wl *= outG; wr *= outG;
            if (shimG > 1.0e-4f) shimBuf.push (0.5f * (wl + wr));

            // décorrélation, premières réflexions, couleur et filtres de la queue, largeur
            wl = outAp[1].process (outAp[0].process (wl, 0.5f), 0.5f) + erL;
            wr = outAp[3].process (outAp[2].process (wr, 0.5f), 0.5f) + erR;
            wl = hc1.process (lc1.process (tilt.process (mud.process (wl, 0), 0), 0), 0);
            wr = hc1.process (lc1.process (tilt.process (mud.process (wr, 1), 1), 1), 1);
            {
                const float m = 0.5f * (wl + wr), sdw = 0.5f * (wl - wr) * prm.width;
                wl = m + sdw; wr = m - sdw;
            }

            // gains de la queue : DUCKING (sans bond), CLEAN (retour effacé à la reprise), PULSE (au tempo)
            const float g = duck.next (0.5f * (xl + xr), prm.ducking);
            const double beats = ppq >= 0.0 ? ppq + (double) i * beatsPerSample : freePhase;
            freePhase += beatsPerSample;
            float pt = 1.0f;
            if (prm.pulse > 0.001f)
            {
                const float ph = (float) std::fmod (beats / period, 1.0);
                const float shape = std::pow (0.5f + 0.5f * std::cos (6.2831853f * ph), 1.6f);
                pt = 1.0f - prm.pulse * (1.0f - shape);
            }
            pulseG += pulseSmooth * (pt - pulseG);
            const float before = std::abs (wl) + std::abs (wr);
            const float total = g * dipS * pulseG;
            wl = sanitize (wl * total); wr = sanitize (wr * total);
            if (numCh > 1) { wet[0][i] += wl; wet[1][i] += wr; }
            else wet[0][i] += 0.5f * (wl + wr);

            const float after = std::abs (wl) + std::abs (wr);
            pk = std::max (pk, after);
            dryPk = std::max (dryPk, std::abs (0.5f * (xl + xr)));
            wetPk = std::max (wetPk, 0.5f * after);
            prePk = std::max (prePk, 0.5f * before / std::max (0.05f, loopS));   // la queue sans CLEAN ni DUCKING (estimée)
            cleanPk = std::max (cleanPk, 1.0f - loopS * dipS);
            duckMin = std::min (duckMin, gainToDb (std::max (g, 1.0e-6f)));
        }
        for (auto& l : lfo) l = std::fmod (l, 2.0 * juce::MathConstants<double>::pi);
        freePhase = std::fmod (freePhase, 64.0);
        tail = pk;
        tailMeter.store (pk, std::memory_order_relaxed);
        cleanMeter.store (cleanPk, std::memory_order_relaxed);
        duckMeter.store (duckMin, std::memory_order_relaxed);
        pushHistory (dryPk, wetPk, prePk, n);
    }

private:
    static constexpr int kTaps = 12;

    float preSamples() const noexcept { return std::max (2.0f, juce::jlimit (0.0f, 250.0f, prm.preDelayMs) * 0.001f * (float) fs); }

    void pushHistory (float dry, float wetV, float pre, int n) noexcept
    {
        hDry = std::max (hDry, dry); hWet = std::max (hWet, wetV); hPre = std::max (hPre, pre);
        hCount += n;
        while (hCount >= 256)
        {
            hCount -= 256;
            const int w = histPos.load (std::memory_order_relaxed);
            histDry[(size_t) w].store (hDry, std::memory_order_relaxed);
            histWet[(size_t) w].store (hWet, std::memory_order_relaxed);
            histPre[(size_t) w].store (hPre, std::memory_order_relaxed);
            histPos.store ((w + 1) % kHistory, std::memory_order_release);
            hDry = hWet = hPre = 0.0f;
        }
    }

    // LOW CUT / HIGH CUT de la queue : fréquences glissées (toutes les 32 échantillons), coefficients recalculés
    void glideFilters() noexcept
    {
        bool changed = false;
        for (int f = 0; f < 2; ++f)
        {
            const float target = f == 0 ? prm.lowCut : prm.highCut;
            if (std::abs (cutNow[f] - target) > 0.001f * target)
            {
                cutNow[f] *= std::pow (target / cutNow[f], 0.25f);
                changed = true;
            }
            else if (cutNow[f] != target) { cutNow[f] = target; changed = true; }
        }
        if (changed) setCutCoeffs();
    }

    void setCutCoeffs (bool snap = false) noexcept
    {
        const auto l = BiquadCoeffs::highpass (fs, cutNow[0], 0.707), h = BiquadCoeffs::lowpass (fs, std::min ((double) cutNow[1], 0.45 * fs), 0.707);
        if (snap) { lc1.setNow (l); hc1.setNow (h); } else { lc1.glideTo (l, 32); hc1.glideTo (h, 32); }
    }

    void update (bool snap) noexcept
    {
        const auto ch = character (prm.mode);
        static constexpr float baseMs[kLines] { 29.7f, 37.1f, 41.1f, 43.7f, 47.9f, 53.3f, 59.0f, 61.4f,
                                                67.3f, 71.1f, 73.9f, 79.6f, 83.5f, 89.2f, 97.3f, 101.6f };
        const float sizeScale = ch.sizeMul * (0.45f + 1.1f * prm.size);
        const float rt = prm.decay, rtHi = std::max (0.08f, rt * ch.hfRatio);
        float gAvg = 0.0f;
        for (int k = 0; k < kLines; ++k)
        {
            lineLen[k] = std::min ((float) (0.285 * fs), baseMs[k] * sizeScale * 0.001f * (float) fs);
            // le passe-tout de densité allonge le trajet : il compte dans le temps de décroissance
            const float trip = lineLen[k] + (float) loopAp[k].length();
            const float gdc = std::pow (10.0f, -3.0f * trip / (rt * (float) fs));
            const float gny = std::pow (10.0f, -3.0f * trip / (rtHi * (float) fs));
            const float aa = (gdc - gny) / (gdc + gny);
            absA[k] = aa;
            absB[k] = gdc * (1.0f - aa);
            gAvg += gdc;
            lfoMul[k] = 0.7f + 0.6f * (float) ((k * 7) % 11) / 10.0f;
            inSign[k] = (k * 5 + 3) % 4 < 2 ? 1.0f : -1.0f;
            sideSign[k] = ((k & 1) ? 1.0f : -1.0f) * ch.side;
            outL[k] = ((k & 1) ? -1.0f : 1.0f) * 0.25f;
            outR[k] = (((k >> 1) ^ (k >> 2)) & 1 ? -1.0f : 1.0f) * 0.25f;
            if (snap) lenNow[k] = lineLen[k];
        }
        gAvg /= (float) kLines;
        outTarget = 1.5f * ch.trim * std::pow (std::max (0.02f, 1.0f - gAvg * gAvg), 0.35f);
        diffAmt = juce::jlimit (0.0f, 1.0f, prm.diffusion * ch.diff);
        density = ch.density * (0.45f + 0.55f * prm.diffusion);
        erTarget = ch.erGain;
        shimTarget = ch.shimmer;
        modMul = ch.mod;
        juce::Random rnd ((juce::int64) (prm.mode * 7919 + 13));
        static constexpr float erMs[kTaps] { 7.1f, 11.3f, 13.7f, 17.2f, 19.9f, 23.4f, 29.1f, 31.3f, 37.7f, 41.2f, 43.9f, 47.8f };
        for (int t = 0; t < kTaps; ++t)
        {
            erTap[t] = juce::jlimit (1, (int) (0.115 * fs), (int) (erMs[t] * std::max (0.2f, ch.erSize * (0.6f + 0.8f * prm.size)) * 0.001f * (float) fs));
            erAmp[t] = 0.55f * std::pow (0.82f, (float) t) * (0.85f + 0.3f * rnd.nextFloat());
        }
        const auto mudC = BiquadCoeffs::peaking (fs, 350.0, 0.9, -2.0 - 4.0 * prm.clean);
        const auto tiltC = BiquadCoeffs::highShelf (fs, std::min (4000.0, 0.4 * fs), 0.7, 1.5 * ch.bright);
        if (snap) { mud.setNow (mudC); tilt.setNow (tiltC); } else { mud.glideTo (mudC, 1024); tilt.glideTo (tiltC, 1024); }
        // le retour remonte moins vite que la queue ne s'éteint (60 dB en DECAY) : jamais de bond
        duck.setMaxRise (0.7f * 60.0f / prm.decay);
        if (snap)
        {
            erG = erTarget; shimG = shimTarget; outG = outTarget;
            cutNow[0] = prm.lowCut; cutNow[1] = prm.highCut;
            setCutCoeffs (true);
        }
    }

    double fs = 48000.0, freePhase = 0.0;
    Params prm;
    ModDelay lines[kLines], preMid, preSide, erLine, shimBuf;
    Allpass loopAp[kLines], inAp[4], outAp[4];
    float lineLen[kLines] {}, lenNow[kLines] {}, absA[kLines] {}, absB[kLines] {}, lpState[kLines] {}, lfoMul[kLines] {};
    float inSign[kLines] {}, sideSign[kLines] {}, outL[kLines] {}, outR[kLines] {};
    double lfo[kLines] {};
    float outTarget = 1.0f, outG = 1.0f, diffAmt = 0.75f, density = 0.6f, erTarget = 0.0f, erG = 0.0f;
    float shimTarget = 0.0f, shimG = 0.0f, shimWin = 2880.0f, shimPhase = 0.0f, modMul = 1.0f;
    float cutNow[2] { 200.0f, 9000.0f };
    int erTap[kTaps] {};
    float erAmp[kTaps] {};
    Biquad inHp, sibHp, erLp, shimLp, shimHp;
    SmoothBiquad mud, tilt, lc1, hc1;   // réglables en jouant : coefficients glissés
    float sibAtk = 0, sibRel = 0, gainSmooth = 0, pulseSmooth = 0, loopSmooth = 0, lenGlide = 0, paramGlide = 0;
    float sibEnvH = 0, sibEnvF = 0, sibCut = 0;
    float onAtk = 0, onRel = 0, peakFall = 0.9999f, clearFastRel = 0, clearSlowRel = 0;
    float envFast = 0, voicePeak = 0, clearFast = 0, clearSlow = 0, loopS = 1.0f, dipS = 1.0f, pulseG = 1.0f;
    int gapLen = 8640, quiet = 0;
    Smooth send;
    juce::SmoothedValue<float> preS;
    Ducker duck;
    float tail = 0.0f;
    bool wasIdle = true;
    std::atomic<float> tailMeter { 0.0f }, cleanMeter { 0.0f }, duckMeter { 0.0f };

    float hDry = 0.0f, hWet = 0.0f, hPre = 0.0f;
    int hCount = 0;
    std::array<std::atomic<float>, kHistory> histDry {}, histWet {}, histPre {};
    std::atomic<int> histPos { 0 };
};
} // namespace onirique::mini
