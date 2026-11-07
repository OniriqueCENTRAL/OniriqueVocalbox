#include "TuneEngine.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace onirique::tune
{

namespace
{
    struct Range
    {
        double minFrequency, maxFrequency;
    };

    // Les plages vocales d'Auto-Tune (Vocal Range) : une voix aiguë qui descend sous 150 Hz reste suivie
    constexpr Range ranges[TuneEngine::numInputTypes] = {
        { 100.0, 1200.0 },   // soprano
        {  85.0,  800.0 },   // alto / ténor
        {  53.0,  600.0 },   // baryton / basse
        {  52.0, 2000.0 },   // instrument
        {  24.5,  400.0 },   // instrument basse
    };

    constexpr int hopAnalysis = 64;            // une mesure de hauteur toutes les ~2,7 ms
    constexpr int smoothFrames = 2;            // mesures d'avance utilisées par le lissage HQ
    constexpr double lowestFrequency = 24.5;
    constexpr double pi = 3.14159265358979323846;
    constexpr int maxLags = 256;
    constexpr float farBelowVoice = 0.0126f;   // 38 dB sous le niveau récent de la voix : repisse, pas la voix
    constexpr int noteConfirmFrames = 2;
    constexpr float maxInput = 8.0f;           // +18 dB pleine échelle : au-delà, ce n'est plus du son
    constexpr double minReliableMatch = 0.5;

    int decimationFor (double sampleRate) noexcept
    {
        return std::max (1, (int) std::lround (sampleRate / 24000.0));
    }

    template <typename T>
    T limit (T v, T lo, T hi) noexcept
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    int clampType (int type) noexcept
    {
        return limit (type, 0, TuneEngine::numInputTypes - 1);
    }

    double saneRate (double sampleRate) noexcept
    {
        return sampleRate >= 8000.0 && sampleRate <= 384000.0 ? sampleRate : 44100.0;
    }

    bool isFinite (float x) noexcept
    {
        // Test sur les bits : il reste vrai quelles que soient les
        // optimisations flottantes du compilateur.
        std::uint32_t bits;
        std::memcpy (&bits, &x, sizeof (bits));
        return (bits & 0x7f800000u) != 0x7f800000u;
    }

    /** Écarte ce qu'un hôte ne devrait jamais envoyer (valeurs non finies ou
        démesurées) : un seul NaN suffirait à empoisonner tous les filtres. */
    float sanitize (float x) noexcept
    {
        return isFinite (x) ? limit (x, -maxInput, maxInput) : 0.0f;
    }

    float clean (float value, float lo, float hi, float fallback) noexcept
    {
        return isFinite (value) ? limit (value, lo, hi) : fallback;
    }

    /** Ramène un écart dans [-période/2, +période/2]. */
    double wrapHalf (double x, double period) noexcept
    {
        x = std::fmod (x, period);
        if (x > 0.5 * period)
            x -= period;
        if (x < -0.5 * period)
            x += period;
        return x;
    }

    double parabolicPeak (float a, float b, float c) noexcept
    {
        const double curve = (double) a - 2.0 * (double) b + (double) c;
        if (curve > -1.0e-12)
            return 0.0;
        return limit (0.5 * ((double) a - (double) c) / curve, -0.5, 0.5);
    }
}

//==============================================================================
void TuneEngine::Biquad::setLowpass (double sampleRate, double frequency, double q) noexcept
{
    const double w0 = 2.0 * pi * frequency / sampleRate;
    const double c = std::cos (w0);
    const double alpha = std::sin (w0) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    b0 = 0.5 * (1.0 - c) / a0;
    b1 = (1.0 - c) / a0;
    b2 = b0;
    a1 = -2.0 * c / a0;
    a2 = (1.0 - alpha) / a0;
}

void TuneEngine::Biquad::setHighpass (double sampleRate, double frequency, double q) noexcept
{
    const double w0 = 2.0 * pi * frequency / sampleRate;
    const double c = std::cos (w0);
    const double alpha = std::sin (w0) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    b0 = 0.5 * (1.0 + c) / a0;
    b1 = -(1.0 + c) / a0;
    b2 = b0;
    a1 = -2.0 * c / a0;
    a2 = (1.0 - alpha) / a0;
}

void TuneEngine::Biquad::setPeak (double sampleRate, double frequency, double q, double gainDb) noexcept
{
    const double w0 = 2.0 * pi * frequency / sampleRate;
    const double a = std::pow (10.0, gainDb / 40.0);
    const double alpha = std::sin (w0) / (2.0 * q);
    const double a0 = 1.0 + alpha / a;
    b0 = (1.0 + alpha * a) / a0;
    b1 = -2.0 * std::cos (w0) / a0;
    b2 = (1.0 - alpha * a) / a0;
    a1 = b1;
    a2 = (1.0 - alpha / a) / a0;
}

//==============================================================================
int TuneEngine::latencyFor (double sampleRate, bool hq, int inputType) noexcept
{
    if (! hq)
        return 0;

    // Il faut voir venir un peu plus de deux périodes de la note la plus
    // grave, plus les mesures d'avance du lissage de hauteur.
    const double rate = saneRate (sampleRate);
    const double longestPeriod = rate / ranges[clampType (inputType)].minFrequency;
    const int hop = hopAnalysis * decimationFor (rate);
    return (int) std::ceil (2.3 * longestPeriod) + (smoothFrames + 1) * hop + 16;
}

void TuneEngine::prepare (double sampleRate)
{
    fs = saneRate (sampleRate);
    decim = decimationFor (fs);
    fa = fs / (double) decim;
    hopFs = hopAnalysis * decim;

    const double worstPeriod = fs / lowestFrequency;
    const int worstLatency = latencyFor (fs, true, numInputTypes - 1);
    const int needed = worstLatency + (int) (5.0 * worstPeriod) + 8192;

    int size = 1024;
    while (size < needed)
        size <<= 1;

    for (auto& ring : inRing)
        ring.assign ((size_t) size, 0.0f);
    inMask = size - 1;

    int analysisSize = 4096;
    while (analysisSize < needed / decim + 4096)
        analysisSize <<= 1;

    analysisRing.assign ((size_t) analysisSize, 0.0f);
    analysisMask = analysisSize - 1;

    detector.prepare (fa, lowestFrequency);
    sincLive.build (32, 256);
    sincHq.build (48, 256);
    vocoder.prepare (fs);

    hannTable.resize ((size_t) hannSize + 2);
    for (int i = 0; i < hannSize + 2; ++i)
    {
        const double x = std::min (1.0, (double) i / (double) hannSize);
        hannTable[(size_t) i] = (float) (0.5 - 0.5 * std::cos (pi * x));
    }

    mixCoef = (float) (1.0 - std::exp (-1.0 / (0.005 * fs)));
    fadeStep = (float) (1.0 / (0.004 * fs));
    loudestDecay = (float) std::pow (0.5, ((double) hopFs / fs) / 2.0);       // -6 dB toutes les deux secondes
    noteHangFrames = (int) std::ceil (0.12 * fs / (double) hopFs);

    now = 0;
    analysisCount = 0;
    decimPhase = 0;
    highpass.clear();
    lowpassA.clear();
    lowpassB.clear();
    loudest = 0.0f;
    fade = 1.0f;
    reconfigurePending = false;
    vocoderNow = 0.0f;
    vocoderRunning = false;
    heldNote = -1;
    pendingNoteOff = -1;
    numNoteEvents = 0;

    hq = params.hq;
    inputType = params.inputType;
    prepared = true;
    configure();

    mixNow = params.bypass ? 0.0f : params.mix;
}

/** Règle le moteur pour le mode et le registre en cours, puis repart. */
void TuneEngine::configure() noexcept
{
    const auto& range = ranges[clampType (inputType)];
    maxPeriod = fs / range.minFrequency;
    minPeriod = fs / range.maxFrequency;
    unvoicedPeriod = std::floor (fs * 0.005);
    latency = latencyFor (fs, hq, inputType);

    // Voie d'analyse : passe-haut sous la note la plus grave, puis passe-bas
    // de Butterworth d'ordre 4 avant décimation.
    const double cutoff = 0.36 * fa;
    highpass.setHighpass (fs, 0.6 * range.minFrequency, 0.70710678);
    lowpassA.setLowpass (fs, cutoff, 0.54119610);
    lowpassB.setLowpass (fs, cutoff, 1.30656296);
    analysisDelay = (1.0 / 0.54119610 + 1.0 / 1.30656296) / (2.0 * pi * cutoff / fs);

    detector.configure (range.minFrequency, range.maxFrequency);
    detector.setTracking (params.tracking);
    sinc = hq ? &sincHq : &sincLive;

    restart();
}

/** Oublie tout ce qui a été déduit de la voix (hauteur, repères, grains,
    notes) mais garde le son déjà reçu et l'horloge : le moteur peut repartir
    au milieu d'un morceau sans trou ni saut dans le temps. */
void TuneEngine::restart() noexcept
{
    const double nowD = (double) now;

    hopCounter = 0;
    detector.reset();
    corrector.reset();
    rawCount = rawCursor = 0;
    controlCount = controlCursor = 0;
    for (auto& tone : vibratoTone)
        tone.clear();
    vibratoNow = vibratoShown = 0.0f;
    vibratoToneLeft = 0;
    noteCursor = 0;

    markRing[0] = { nowD - (double) latency, (float) unvoicedPeriod, false };
    markCount = 1;
    markCursor = 0;
    markWake = 0.0;

    for (auto& g : grains)
        g.active = false;

    // Un premier grain déjà à plein niveau : la sortie est transparente dès
    // le tout premier échantillon, sans fondu d'entrée.
    Grain& first = grains[0];
    first = {};
    first.active = true;
    first.chained = true;
    first.centre = nowD;
    first.start = nowD - unvoicedPeriod;
    first.fadeIn = unvoicedPeriod;
    first.fadeOut = unvoicedPeriod;
    first.source = nowD - (double) latency;
    first.offset = -(std::int64_t) latency;

    lastGrain = &first;
    lastCentre = nowD;
    lastTout = unvoicedPeriod;
    lastSource = first.source;
    lastMarkIndex = -1;
    sourceValid = false;

    // Une note en cours ne doit jamais rester bloquée.
    if (heldNote >= 0)
        pendingNoteOff = heldNote;

    heldNote = -1;
    candidateNote = -1;
    candidateFrames = 0;
    silentFrames = 0;
    vocoderRunning = false;
}

void TuneEngine::reset() noexcept
{
    if (! prepared)
        return;

    for (auto& ring : inRing)
        std::fill (ring.begin(), ring.end(), 0.0f);
    std::fill (analysisRing.begin(), analysisRing.end(), 0.0f);

    highpass.clear();
    lowpassA.clear();
    lowpassB.clear();
    loudest = 0.0f;
    fade = 1.0f;
    reconfigurePending = false;
    hq = params.hq;
    inputType = params.inputType;
    configure();

    mixNow = params.bypass ? 0.0f : params.mix;
    vocoderNow = 0.0f;
}

void TuneEngine::setParams (const EngineParams& p) noexcept
{
    params = p;
    params.inputType = clampType (p.inputType);
    params.retuneMs = clean (p.retuneMs, 0.0f, 2000.0f, 20.0f);
    params.flex = clean (p.flex, 0.0f, 1.0f, 0.0f);
    params.humanize = clean (p.humanize, 0.0f, 1.0f, 0.0f);
    params.vibrato = clean (p.vibrato, -1.0f, 1.0f, 0.0f);
    params.throat = clean (p.throat, 0.5f, 2.0f, 1.0f);
    params.transpose = clean (p.transpose, -24.0f, 24.0f, 0.0f);
    params.detuneCents = clean (p.detuneCents, -200.0f, 200.0f, 0.0f);
    params.tracking = clean (p.tracking, 0.0f, 1.0f, 0.5f);
    params.mix = clean (p.mix, 0.0f, 1.0f, 1.0f);
    params.vocoderBlend = clean (p.vocoderBlend, 0.0f, 1.0f, 1.0f);
    formantSpeed = 1.0 / (double) params.throat;

    correctorParams.retuneMs = params.retuneMs;
    correctorParams.hard = params.hardTune;
    correctorParams.flex = params.flex;
    correctorParams.humanize = params.humanize;
    correctorParams.vibrato = params.vibrato;
    correctorParams.transpose = params.transpose;
    correctorParams.a4 = 440.0 * std::pow (2.0, (double) params.detuneCents / 1200.0);
    correctorParams.noteMask = p.noteMask;
    correctorParams.skipMask = p.skipMask;
    correctorParams.narrowVibrato = p.narrowVibrato;
    correctorParams.created = p.createdVibrato;
    correctorParams.created.amplitudeDb = clean (p.createdVibrato.amplitudeDb, 0.0f, 12.0f, 0.0f);
    correctorParams.created.timbreDb = clean (p.createdVibrato.timbreDb, 0.0f, 12.0f, 0.0f);
    correctorParams.created.shape = std::clamp (p.createdVibrato.shape, 0, 3);
    correctorParams.created.rateHz = clean (p.createdVibrato.rateHz, 0.1f, 12.0f, 5.5f);
    correctorParams.created.depthCents = clean (p.createdVibrato.depthCents, 0.0f, 200.0f, 30.0f);
    correctorParams.created.delayMs = clean (p.createdVibrato.delayMs, 0.0f, 3000.0f, 150.0f);
    correctorParams.created.onsetMs = clean (p.createdVibrato.onsetMs, 0.0f, 3000.0f, 250.0f);
    correctorParams.created.variation = clean (p.createdVibrato.variation, 0.0f, 1.0f, 0.15f);

    detector.setTracking (params.tracking);
    vocoder.setVoicing (params.vocoderVoicing, params.harmonyMask);
    vocoder.setGlide ((double) params.retuneMs / 2500.0);

    if (! prepared)
    {
        hq = params.hq;
        inputType = params.inputType;
        return;
    }

    // Changer de mode ou de registre reconfigure tout le moteur : on le fait
    // au creux d'un court fondu de sortie (voir process), jamais à vif.
    reconfigurePending = params.hq != hq || params.inputType != inputType;
}

bool TuneEngine::popDisplayFrame (DisplayFrame& frame) noexcept
{
    const int r = displayRead.load (std::memory_order_relaxed);
    if (r == displayWrite.load (std::memory_order_acquire))
        return false;

    frame = displayRing[(size_t) r];
    displayRead.store ((r + 1) & (displaySize - 1), std::memory_order_release);
    return true;
}

float TuneEngine::hann (double x) const noexcept
{
    const double scaled = limit (x, 0.0, 1.0) * (double) hannSize;
    const int i = (int) scaled;
    const float t = (float) (scaled - (double) i);
    return hannTable[(size_t) i] + t * (hannTable[(size_t) i + 1] - hannTable[(size_t) i]);
}

//==============================================================================
void TuneEngine::process (float* const* channels, int numCh, int numSamples) noexcept
{
    numNoteEvents = 0;

    if (! prepared || channels == nullptr || numSamples <= 0)
        return;

    numCh = limit (numCh, 1, maxChannels);

    if (pendingNoteOff >= 0)
    {
        pushNoteEvent (0, pendingNoteOff, 0, false);
        pendingNoteOff = -1;
    }

    const float mixTarget = params.bypass ? 0.0f : params.mix;
    const float vocoderTarget = params.vocoder && ! params.bypass ? params.vocoderBlend : 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        // Changement de mode : la sortie descend à zéro, le moteur se
        // reconfigure, puis la sortie remonte.
        if (reconfigurePending)
        {
            fade -= fadeStep;
            if (fade <= 0.0f)
            {
                fade = 0.0f;
                reconfigurePending = false;
                hq = params.hq;
                inputType = params.inputType;
                configure();
            }
        }
        else if (fade < 1.0f)
        {
            fade = std::min (1.0f, fade + fadeStep);
        }

        float mono = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
        {
            const float x = sanitize (channels[ch][i]);
            inRing[(size_t) ch][(size_t) (now & inMask)] = x;
            mono += x;
        }

        if (numCh > 1)
            mono *= 0.5f;

        const double filtered = lowpassB.process (lowpassA.process (highpass.process ((double) mono)));
        if (++decimPhase >= decim)
        {
            decimPhase = 0;
            analysisRing[(size_t) (analysisCount & analysisMask)] = (float) filtered;
            ++analysisCount;

            if (++hopCounter >= hopAnalysis)
            {
                hopCounter = 0;
                analyseFrame();
            }
        }

        if (hq)
            advanceMarks();

        launchGrains();

        float wet[maxChannels] = { 0.0f, 0.0f };
        renderGrains (wet, numCh);

        // Vibrato créé, comme le Vibrato FX d'Auto-Tune : le volume et l'éclat (une cloche vers 1,5 kHz) suivent
        // le vibrato de la voix accordée, celui qui s'entend à cet instant.
        const auto& created = correctorParams.created;
        if (created.on && (created.amplitudeDb > 0.01f || created.timbreDb > 0.01f))
        {
            if (--vibratoToneLeft <= 0)
            {
                vibratoToneLeft = 16;
                const Control heard = hq ? lookupControl ((double) now - (double) latency) : newestControl();
                vibratoNow = heard.voiced ? heard.vibrato : 0.0f;
                for (auto& tone : vibratoTone)
                    tone.setPeak (fs, 1500.0, 0.9, (double) created.timbreDb * (double) vibratoShown);
            }

            vibratoShown += mixCoef * (vibratoNow - vibratoShown);
            const float gain = (float) std::pow (10.0, (double) created.amplitudeDb * (double) vibratoShown / 20.0);
            for (int ch = 0; ch < numCh; ++ch)
                wet[ch] = (float) vibratoTone[(size_t) ch].process ((double) wet[ch]) * gain;
        }

        float dry[maxChannels] = { 0.0f, 0.0f };
        for (int ch = 0; ch < numCh; ++ch)
            dry[ch] = inRing[(size_t) ch][(size_t) ((now - latency) & inMask)];

        // Vocodeur : il écoute la voix d'origine (retardée comme le reste en
        // mode HQ) et remplace tout ou partie de la voix accordée.
        vocoderNow += mixCoef * (vocoderTarget - vocoderNow);
        if (vocoderTarget > 0.0f || vocoderNow > 1.0e-4f)
        {
            if (! vocoderRunning)
            {
                vocoderRunning = true;
                vocoder.reset();
                noteCursor = controlCount;      // on ne rejoue pas le passé
            }

            followNotes (i);

            float left = 0.0f, right = 0.0f;
            vocoder.process (numCh > 1 ? 0.5f * (dry[0] + dry[1]) : dry[0], numCh > 1, left, right);

            wet[0] += vocoderNow * (left - wet[0]);
            if (numCh > 1)
                wet[1] += vocoderNow * (right - wet[1]);
        }
        else if (vocoderRunning)
        {
            vocoderRunning = false;
            vocoderNow = 0.0f;
            releaseNote (i);
        }

        mixNow += mixCoef * (mixTarget - mixNow);
        for (int ch = 0; ch < numCh; ++ch)
            channels[ch][i] = fade * (dry[ch] + mixNow * (wet[ch] - dry[ch]));

        ++now;
    }
}

//==============================================================================
//  Hauteur
//==============================================================================
void TuneEngine::analyseFrame() noexcept
{
    auto measured = detector.analyse (analysisRing.data(), (int) analysisMask, analysisCount);

    // Une prise de voix contient aussi la repisse du casque et les queues de
    // réverbération : des notes justes, mais très faibles. Tout ce qui est
    // loin sous le niveau récent de la voix est laissé tel quel.
    loudest = std::max (measured.rms, loudest * loudestDecay);
    if (measured.rms < loudest * farBelowVoice)
        measured.voiced = false;

    Raw raw;
    raw.time = timeOfAnalysisIndex ((double) (analysisCount - 1) - (double) measured.centreLag);
    if (rawCount > 0)
        raw.time = std::max (raw.time, rawAt (rawCount - 1).time + 1.0);

    raw.voiced = measured.voiced;
    raw.period = measured.voiced ? measured.period * (float) decim : 0.0f;
    raw.clarity = measured.clarity;
    raw.level = measured.rms;

    rawRing[(size_t) (rawCount & (rawSize - 1))] = raw;
    ++rawCount;
    markWake = 0.0;

    if (! hq)
    {
        emitControl (raw);
        steerLiveGrains();
    }
    else if (rawCount > smoothFrames)
    {
        emitControl (smoothedRaw (rawCount - 1 - smoothFrames));
    }
}

/** Mode HQ : une mesure n'est validée qu'une fois ses voisines connues, ce
    qui permet de boucher un trou isolé et d'écarter un saut d'octave fugitif. */
TuneEngine::Raw TuneEngine::smoothedRaw (std::int64_t index) const noexcept
{
    Raw out = rawAt (index);

    const std::int64_t oldest = std::max<std::int64_t> (0, rawCount - rawSize);
    const std::int64_t lo = std::max (oldest, index - smoothFrames);
    const std::int64_t hi = std::min (rawCount - 1, index + smoothFrames);

    const bool before = index - 1 >= oldest && rawAt (index - 1).voiced;
    const bool after = index + 1 <= rawCount - 1 && rawAt (index + 1).voiced;

    if (! out.voiced && before && after)
    {
        out.voiced = true;
        out.period = std::sqrt (rawAt (index - 1).period * rawAt (index + 1).period);
    }
    else if (out.voiced && ! before && ! after)
    {
        out.voiced = false;
        out.period = 0.0f;
    }

    if (out.voiced)
    {
        float logs[2 * smoothFrames + 1];
        int count = 0;
        for (std::int64_t i = lo; i <= hi; ++i)
            if (rawAt (i).voiced)
                logs[count++] = std::log2 (rawAt (i).period);

        if (count >= 3)
        {
            std::sort (logs, logs + count);
            const float median = logs[count / 2];
            if (std::abs (std::log2 (out.period) - median) > 0.25f)
                out.period = std::exp2 (median);
        }
    }

    return out;
}

void TuneEngine::emitControl (const Raw& raw) noexcept
{
    const double dt = (double) hopFs / fs;
    const double frequency = raw.voiced && raw.period > 0.0f ? fs / (double) raw.period : 0.0;
    const auto out = corrector.process (raw.voiced, frequency, dt, correctorParams);

    Control control;
    control.time = raw.time;
    control.voiced = out.voiced;
    control.period = raw.period;
    control.pin = out.pin;
    control.noteMidi = out.noteMidi;
    control.level = raw.level;
    control.vibrato = out.vibratoShape;
    if (out.voiced)
        control.outPeriod = (float) (fs / (correctorParams.a4 * std::pow (2.0, ((double) out.outMidi - 69.0) / 12.0)));

    controlRing[(size_t) (controlCount & (controlSize - 1))] = control;
    ++controlCount;

    if (! hq && control.voiced)
        controlRing[(size_t) ((controlCount - 1) & (controlSize - 1))].slope = periodSlope();

    const int w = displayWrite.load (std::memory_order_relaxed);
    const int next = (w + 1) & (displaySize - 1);
    if (next != displayRead.load (std::memory_order_acquire))
    {
        DisplayFrame& frame = displayRing[(size_t) w];
        frame.time = raw.time / fs;
        frame.inMidi = out.inMidi;
        frame.outMidi = out.outMidi;
        frame.noteMidi = out.noteMidi;
        frame.clarity = raw.clarity;
        frame.level = raw.level;
        frame.voiced = out.voiced;
        displayWrite.store (next, std::memory_order_release);
    }
}

/** Pente récente de la hauteur (log2 de la période, par échantillon), par
    régression sur les dernières mesures. Le mode LIVE s'en sert pour
    anticiper : il ne connaît que le passé alors que le grain lira la suite. */
float TuneEngine::periodSlope() const noexcept
{
    constexpr int points = 5;
    if (controlCount < points)
        return 0.0f;

    const double origin = controlAt (controlCount - 1).time;
    double st = 0.0, sy = 0.0, stt = 0.0, sty = 0.0;

    for (int i = 0; i < points; ++i)
    {
        const Control& c = controlAt (controlCount - 1 - i);
        if (! c.voiced || c.period <= 0.0f)
            return 0.0f;

        const double t = c.time - origin;
        const double y = std::log2 ((double) c.period);
        st += t;
        sy += y;
        stt += t * t;
        sty += t * y;
    }

    const double spread = (double) points * stt - st * st;
    return spread < 1.0 ? 0.0f : (float) (((double) points * sty - st * sy) / spread);
}

TuneEngine::Control TuneEngine::newestControl() const noexcept
{
    return controlCount > 0 ? controlAt (controlCount - 1) : Control {};
}

TuneEngine::Control TuneEngine::lookupControl (double time) noexcept
{
    if (controlCount == 0)
        return {};

    const std::int64_t newest = controlCount - 1;
    const std::int64_t oldest = std::max<std::int64_t> (0, controlCount - controlSize);

    std::int64_t i = limit (controlCursor, oldest, newest);
    while (i < newest && controlAt (i + 1).time <= time)
        ++i;
    while (i > oldest && controlAt (i).time > time)
        --i;
    controlCursor = i;

    const Control& a = controlAt (i);
    if (i == newest || a.time > time)
        return a;

    const Control& b = controlAt (i + 1);
    const double x = (time - a.time) / std::max (1.0, b.time - a.time);
    if (a.voiced && b.voiced)
    {
        Control mixed = a;
        mixed.time = time;
        mixed.period = (float) ((double) a.period + x * ((double) b.period - (double) a.period));
        mixed.outPeriod = (float) ((double) a.outPeriod + x * ((double) b.outPeriod - (double) a.outPeriod));
        return mixed;
    }

    return x < 0.5 ? a : b;
}

/** Index de la mesure brute la plus proche d'un instant donné. */
std::int64_t TuneEngine::nearestRawIndex (double time) noexcept
{
    if (rawCount == 0)
        return -1;

    const std::int64_t newest = rawCount - 1;
    const std::int64_t oldest = std::max<std::int64_t> (0, rawCount - rawSize);

    std::int64_t i = limit (rawCursor, oldest, newest);
    while (i < newest && rawAt (i + 1).time <= time)
        ++i;
    while (i > oldest && rawAt (i).time > time)
        --i;
    rawCursor = i;

    if (i < newest && rawAt (i + 1).time - time < time - rawAt (i).time)
        return i + 1;

    return i;
}

//==============================================================================
//  Notes : la voix convertie en notes, pour le vocodeur et la sortie MIDI
//==============================================================================

/** Applique les consignes de hauteur arrivées à échéance. En mode HQ elles
    sont connues en avance : on attend que le son correspondant sorte. */
void TuneEngine::followNotes (int sampleOffset) noexcept
{
    const double due = (double) (now - latency);
    noteCursor = std::max (noteCursor, std::max<std::int64_t> (0, controlCount - controlSize));

    while (noteCursor < controlCount && controlAt (noteCursor).time <= due)
    {
        applyNote (controlAt (noteCursor), sampleOffset);
        ++noteCursor;
    }
}

void TuneEngine::applyNote (const Control& control, int sampleOffset) noexcept
{
    if (! control.voiced)
    {
        // Consonne, souffle ou silence : la note tient encore un instant
        // (elle reprend le plus souvent juste après), puis s'arrête.
        vocoder.setVoiced (false);
        if (heldNote >= 0 && ++silentFrames > noteHangFrames)
            releaseNote (sampleOffset);

        return;
    }

    const int note = limit ((int) std::lround ((double) control.noteMidi), 0, 127);
    const double decibels = 20.0 * std::log10 ((double) control.level + 1.0e-9);
    const int velocity = limit ((int) std::lround (127.0 + 2.2 * (decibels + 6.0)), 1, 127);

    if (heldNote < 0)
    {
        heldNote = note;
        pushNoteEvent (sampleOffset, note, velocity, true);
        vocoder.setNote ((double) note, correctorParams.a4, true);
    }
    else if (note != heldNote)
    {
        // Une nouvelle note doit se confirmer sur deux mesures : la voix
        // traverse souvent une note voisine sans s'y arrêter.
        candidateFrames = note == candidateNote ? candidateFrames + 1 : 1;
        candidateNote = note;

        if (candidateFrames >= noteConfirmFrames)
        {
            pushNoteEvent (sampleOffset, heldNote, 0, false);
            pushNoteEvent (sampleOffset, note, velocity, true);
            heldNote = note;
            candidateNote = -1;
            candidateFrames = 0;
        }
    }
    else
    {
        candidateNote = -1;
        candidateFrames = 0;
    }

    silentFrames = 0;
    vocoder.setVoiced (true);
    vocoder.setVoiceFrequency (fs / (double) control.period);
    vocoder.setNote ((double) heldNote, correctorParams.a4, false);
}

void TuneEngine::releaseNote (int sampleOffset) noexcept
{
    if (heldNote >= 0)
        pushNoteEvent (sampleOffset, heldNote, 0, false);

    heldNote = -1;
    candidateNote = -1;
    candidateFrames = 0;
    silentFrames = 0;
}

void TuneEngine::pushNoteEvent (int sampleOffset, int note, int velocity, bool on) noexcept
{
    if (numNoteEvents < maxNoteEvents)
        noteEvents[(size_t) numNoteEvents++] = { sampleOffset, note, velocity, on };
    else if (! on)
        noteEvents[(size_t) maxNoteEvents - 1] = { sampleOffset, note, velocity, on };   // une fin de note ne se perd jamais
}

//==============================================================================
//  Mesures locales sur le signal d'analyse
//==============================================================================

/** Période des deux derniers cycles reçus : on cherche le décalage qui
    superpose le mieux le dernier cycle au précédent. Aucune avance requise. */
bool TuneEngine::measureRecentPeriod (double guessFs, double& periodFs, double& centreTime) const noexcept
{
    const double guess = guessFs / (double) decim;
    const int w = std::max ((int) std::lround (guess), 48);
    const int spread = std::max (2, (int) std::ceil (guess * 0.1));
    const int lo = std::max (2, (int) std::floor (guess) - spread);
    const int hi = (int) std::ceil (guess) + spread;
    const int count = hi - lo + 1;

    if (count > maxLags || analysisCount < (std::int64_t) (w + hi + 2))
        return false;

    const std::int64_t newest = analysisCount - 1;

    double xx = 0.0;
    for (int j = 0; j < w; ++j)
    {
        const double a = (double) analysisAt (newest - j);
        xx += a * a;
    }

    if (xx < 1.0e-10)
        return false;

    std::array<float, maxLags> score;
    int best = 0;
    for (int i = 0; i < count; ++i)
    {
        const int lag = lo + i;
        double xy = 0.0, yy = 0.0;
        for (int j = 0; j < w; ++j)
        {
            const double a = (double) analysisAt (newest - j);
            const double b = (double) analysisAt (newest - j - lag);
            xy += a * b;
            yy += b * b;
        }

        score[(size_t) i] = (float) (xy / std::sqrt (xx * yy + 1.0e-20));
        if (score[(size_t) i] > score[(size_t) best])
            best = i;
    }

    if ((double) score[(size_t) best] < minReliableMatch)
        return false;

    double lag = (double) (lo + best);
    if (best > 0 && best < count - 1)
        lag += parabolicPeak (score[(size_t) best - 1], score[(size_t) best], score[(size_t) best + 1]);

    periodFs = lag * (double) decim;
    centreTime = timeOfAnalysisIndex ((double) newest - 0.5 * ((double) w + lag));
    return true;
}

/** Période mesurée autour d'une position passée : décalage qui superpose le
    cycle centré sur `position` au cycle suivant (mode HQ). */
bool TuneEngine::measureForwardPeriod (double position, double guessFs, double& periodFs) const noexcept
{
    const double guess = guessFs / (double) decim;
    const int w = std::max ((int) std::lround (guess), 48);
    const int spread = std::max (2, (int) std::ceil (guess * 0.1));
    const int lo = std::max (2, (int) std::floor (guess) - spread);
    const int hi = (int) std::ceil (guess) + spread;
    const int count = hi - lo + 1;

    const std::int64_t start = (std::int64_t) std::llround (analysisIndexOfTime (position)) - w / 2;
    if (count > maxLags || start + w + hi >= analysisCount)
        return false;

    double xx = 0.0;
    for (int j = 0; j < w; ++j)
    {
        const double a = (double) analysisAt (start + j);
        xx += a * a;
    }

    if (xx < 1.0e-10)
        return false;

    std::array<float, maxLags> score;
    int best = 0;
    for (int i = 0; i < count; ++i)
    {
        const int lag = lo + i;
        double xy = 0.0, yy = 0.0;
        for (int j = 0; j < w; ++j)
        {
            const double a = (double) analysisAt (start + j);
            const double b = (double) analysisAt (start + j + lag);
            xy += a * b;
            yy += b * b;
        }

        score[(size_t) i] = (float) (xy / std::sqrt (xx * yy + 1.0e-20));
        if (score[(size_t) i] > score[(size_t) best])
            best = i;
    }

    if ((double) score[(size_t) best] < minReliableMatch)
        return false;

    double lag = (double) (lo + best);
    if (best > 0 && best < count - 1)
        lag += parabolicPeak (score[(size_t) best - 1], score[(size_t) best], score[(size_t) best + 1]);

    periodFs = lag * (double) decim;
    return true;
}

/** Instant (en échantillons plein débit) où l'énergie à court terme est
    maximale parmi les `span` positions d'analyse à partir de `from`. C'est là
    que tombe l'impulsion glottique : un grain centré dessus garde le timbre. */
double TuneEngine::energyPeak (std::int64_t from, int span, int box) const noexcept
{
    const int halfBox = box / 2;

    double sum = 0.0;
    for (int j = -halfBox; j <= halfBox; ++j)
    {
        const double v = (double) analysisAt (from + j);
        sum += v * v;
    }

    double best = sum;
    std::int64_t bestIndex = from;
    for (int i = 1; i < span; ++i)
    {
        const double in = (double) analysisAt (from + i + halfBox);
        const double out = (double) analysisAt (from + i - 1 - halfBox);
        sum += in * in - out * out;
        if (sum > best)
        {
            best = sum;
            bestIndex = from + i;
        }
    }

    return timeOfAnalysisIndex ((double) bestIndex);
}

/** Mode LIVE avec formants préservés : cale la chaîne de positions sources
    sur l'impulsion du dernier cycle reçu (d'un coup à l'attaque, puis par
    retouches imperceptibles pour ne pas dériver). */
double TuneEngine::centreOnPulse (double source, double period, double sourceLimit, bool reseed) const noexcept
{
    const int span = std::max (4, (int) std::lround (period / (double) decim));
    const int box = std::max (3, span / 4) | 1;
    const std::int64_t from = analysisCount - span - box / 2 - 1;
    if (from - box / 2 < 0)
        return source;

    const double offset = wrapHalf (energyPeak (from, span, box) - source, period);

    if (reseed)
    {
        double s = source + offset;
        while (s > sourceLimit)
            s -= period;
        return s;
    }

    if (std::abs (offset) > 0.12 * period)
    {
        const double step = limit (offset * 0.05, -0.004 * period, 0.004 * period);
        if (source + step <= sourceLimit)
            return source + step;
    }

    return source;
}

//==============================================================================
//  Repères de période (mode HQ)
//==============================================================================
void TuneEngine::advanceMarks() noexcept
{
    const double nowD = (double) now;
    if (nowD < markWake)
        return;

    for (int guard = 0; guard < 4; ++guard)
    {
        Mark last = markAt (markCount - 1);

        // Filet de sécurité : une chaîne de repères trop en retard ferait lire
        // les grains hors de la mémoire disponible. On la replace.
        if (last.pos < nowD - ((double) latency + 6.0 * maxPeriod + 4096.0))
        {
            last = { nowD - (double) latency, (float) unvoicedPeriod, false };
            markRing[(size_t) (markCount & (markSize - 1))] = last;
            ++markCount;
        }

        // On n'avance qu'une fois la hauteur connue à cet endroit.
        if (rawCount == 0 || rawAt (rawCount - 1).time < last.pos - (double) hopFs)
        {
            markWake = 1.0e300;      // réveillé par la prochaine mesure
            return;
        }

        const Raw info = rawAt (nearestRawIndex (last.pos));
        const bool voiced = info.voiced && info.period > 0.0f;
        const double guess = voiced ? limit ((double) info.period, minPeriod * 0.5, maxPeriod * 1.2) : unvoicedPeriod;

        // La mesure compare le cycle courant au suivant : il faut les avoir reçus.
        const double guessA = guess / (double) decim;
        const double reachA = 0.5 * std::max (guessA, 48.0) + guessA + std::max (2.0, 0.1 * guessA) + 4.0;
        const double needed = last.pos + std::max (1.8 * guess, reachA * (double) decim) + 6.0 * (double) decim + 8.0;
        if (needed > nowD)
        {
            markWake = needed;
            return;
        }

        double lag = guess;
        if (voiced)
        {
            double measured = guess;
            if (measureForwardPeriod (last.pos, guess, measured))
                lag = measured;
        }

        Mark next { last.pos + lag, (float) lag, voiced };

        if (voiced)
        {
            const int span = std::max (4, (int) std::lround (guess / (double) decim));
            const int box = std::max (3, span / 4) | 1;
            const auto from = (std::int64_t) std::llround (analysisIndexOfTime (next.pos - 0.5 * guess));
            const double offset = energyPeak (from, span, box) - next.pos;

            if (! last.voiced)
                next.pos += offset;                                           // attaque : droit sur l'impulsion
            else if (std::abs (offset) > 0.12 * guess)
                next.pos += limit (offset * 0.05, -0.004 * guess, 0.004 * guess);   // dérive lente
        }

        markRing[(size_t) (markCount & (markSize - 1))] = next;
        ++markCount;
    }
}

std::int64_t TuneEngine::nearestMark (double time) noexcept
{
    if (markCount == 0)
        return -1;

    const std::int64_t newest = markCount - 1;
    const std::int64_t oldest = std::max<std::int64_t> (0, markCount - markSize);

    std::int64_t i = limit (markCursor, oldest, newest);
    while (i < newest && markAt (i + 1).pos <= time)
        ++i;
    while (i > oldest && markAt (i).pos > time)
        --i;
    markCursor = i;

    if (i < newest && markAt (i + 1).pos - time < time - markAt (i).pos)
        return i + 1;

    return i;
}

//==============================================================================
//  Grains
//==============================================================================

/** Période réelle de la voix à l'endroit que le prochain grain va lire. */
TuneEngine::Local TuneEngine::resolveLocal (const Control& control, double tin, double centre) noexcept
{
    Local local;
    local.period = tin;

    if (hq)
    {
        // Mesurée : c'est l'écart entre les repères qui encadrent la source.
        local.markIndex = nearestMark (centre - (double) latency);
        if (local.markIndex >= 0 && markAt (local.markIndex).voiced)
        {
            local.period = (double) markAt (local.markIndex).period;
            local.aligned = true;
        }
    }
    else
    {
        // Prédite. En chaîne classique le grain relit ce qui vient d'arriver ;
        // avec formants préservés, son centre est encore à venir.
        const double when = params.formant ? centre - 0.5 * tin : (double) now - 0.5 * tin;
        local.period = predictedPeriod (control, tin, when);
        local.aligned = true;
    }

    return local;
}

/** Mode LIVE : période des derniers cycles reçus, prolongée jusqu'à l'instant
    `when` selon la pente récente de la hauteur. */
double TuneEngine::predictedPeriod (const Control& control, double guess, double when) const noexcept
{
    double period = guess, measuredAt = control.time;
    measureRecentPeriod (guess, period, measuredAt);
    return period * limit (std::exp2 ((double) control.slope * (when - measuredAt)), 0.98, 1.02);
}

/** Période à produire. Épinglée, c'est exactement celle de la note visée :
    la sortie est parfaitement plate. Sinon c'est la période réelle de la voix
    divisée par le rapport de correction : la voix est transposée en douceur
    et garde ses micro-variations naturelles. */
double TuneEngine::outputPeriod (const Control& control, double tin, double pinned, double local) noexcept
{
    const double transposed = local * pinned / tin;
    return transposed * std::pow (pinned / transposed, (double) control.pin);
}

void TuneEngine::launchGrains() noexcept
{
    const double nowD = (double) now;

    for (int guard = 0; guard < 8; ++guard)
    {
        const Control control = hq ? lookupControl (lastCentre + lastTout - (double) latency)
                                          : newestControl();

        const bool voiced = control.voiced && control.period > 0.0f && control.outPeriod > 0.0f;
        const double tin = voiced ? (double) control.period : unvoicedPeriod;
        const double pinned = voiced ? limit ((double) control.outPeriod, std::max (12.0, 0.25 * tin), 4.0 * tin)
                                     : unvoicedPeriod;
        const bool preserve = voiced && params.formant;

        // Premier tri, bon marché, avec les chiffres du détecteur.
        if (lastCentre + pinned - (preserve ? std::min (tin / formantSpeed, 1.3 * tin) : pinned) > nowD)
            return;

        Local local;
        local.period = tin;
        double tout = pinned;

        if (voiced)
        {
            local = resolveLocal (control, tin, lastCentre + pinned);
            tout = limit (outputPeriod (control, tin, pinned, local.period), std::max (12.0, 0.25 * local.period), 4.0 * local.period);
        }

        // Formants préservés : chaque grain couvre deux périodes de la voix
        // d'origine, quel que soit l'écart entre grains. Sinon les fenêtres
        // se complètent exactement (grain = deux périodes de sortie).
        const double half = preserve ? std::min (local.period / formantSpeed, 1.3 * local.period) : tout;
        const double centre = std::max (lastCentre + tout, nowD + 2.0);

        startGrain (centre, voiced, preserve, local, tout, half);
        lastCentre = centre;
        lastTout = tout;
    }
}

void TuneEngine::startGrain (double centre, bool voiced, bool preserve, const Local& local, double tout, double half) noexcept
{
    Grain* g = nullptr;
    for (auto& candidate : grains)
    {
        if (! candidate.active)
        {
            g = &candidate;
            break;
        }
    }

    if (g == nullptr)
        return;

    const double nowD = (double) now;
    const bool chained = ! preserve;

    // Chaîne à fenêtres complémentaires : le fondu de sortie du grain
    // précédent couvre exactement l'intervalle jusqu'à ce nouveau centre.
    const bool follows = chained && lastGrain != nullptr && lastGrain->active && lastGrain->chained
                         && lastGrain->centre > nowD - 1.5 && lastGrain->centre <= nowD;

    if (follows)
        lastGrain->fadeOut = std::max (1.0, centre - lastGrain->centre);

    g->chained = chained;
    g->centre = centre;
    g->start = follows ? lastGrain->centre : nowD;
    g->fadeIn = std::max (1.0, centre - g->start);
    g->fadeOut = half;
    g->speed = 1.0;
    g->integer = true;
    g->steered = false;
    g->gain = 1.0f;
    g->table = nullptr;

    double source = hq ? centre - (double) latency : centre;
    bool aligned = false;
    std::int64_t markIndex = local.markIndex;

    if (voiced)
    {
        aligned = local.aligned;

        const double period = local.period;
        double speed = limit (preserve ? formantSpeed : period / tout, 0.25, 4.0);
        const double margin = (double) (sinc->halfLength() + 1);
        const double fadeOutGuess = chained ? 1.5 * half + 2.0 : half;

        if (! hq && chained)
        {
            // LIVE classique : le grain reprend la lecture là où en est le
            // précédent, à un nombre entier de périodes près (zéro : lecture
            // continue ; moins un : un cycle répété ; plus un : un cycle
            // sauté), en restant au plus près de l'instant présent.
            const double headroom = margin + (speed > 1.0 ? 1.25 * (speed - 1.0) * (g->fadeIn + fadeOutGuess) : 0.0);
            double position = nowD - headroom;

            if (sourceValid && lastGrain != nullptr && lastGrain->active && lastGrain->steered)
            {
                const double steps = std::floor ((position - lastGrain->position) / period);
                if (steps >= -3.0 && steps <= 8.0)
                    position = lastGrain->position + steps * period;
            }

            g->position = position;
            g->steered = true;
            source = position + speed * (centre - nowD);
        }
        else
        {
            // Le grain ne peut pas lire au-delà de ce qui est déjà arrivé.
            const double reach = speed <= 1.0 ? g->fadeIn * (1.0 - speed) : fadeOutGuess * (speed - 1.0);
            const double sourceLimit = centre - margin - reach;

            if (hq)
            {
                if (aligned)
                {
                    const std::int64_t oldest = std::max<std::int64_t> (0, markCount - markSize);
                    while (markIndex > oldest && markAt (markIndex).pos > sourceLimit)
                        --markIndex;

                    source = markAt (markIndex).pos;

                    // Deux repères consécutifs : relecture continue, sans raccord.
                    if (chained && sourceValid && markIndex == lastMarkIndex + 1)
                        speed = limit ((source - lastSource) / tout, 0.25, 4.0);
                }

                source = std::min (source, sourceLimit);
            }
            else
            {
                // LIVE, formants préservés : on avance d'un nombre entier de
                // périodes prédites, calé sur l'impulsion du dernier cycle.
                bool reseed = ! sourceValid;
                if (sourceValid)
                {
                    const double steps = std::floor ((sourceLimit - lastSource) / period);
                    reseed = steps < -3.0 || steps > 8.0;
                    source = reseed ? sourceLimit : lastSource + steps * period;
                }
                else
                {
                    source = sourceLimit;
                }

                source = centreOnPulse (source, period, sourceLimit, reseed);
            }

            g->position = source - speed * (centre - nowD);
        }

        g->speed = speed;
        g->integer = false;
        g->table = &sinc->forRatio (speed);

        // Plus de grains par seconde = plus d'énergie : on compense pour
        // que le niveau perçu ne suive pas la transposition.
        if (preserve)
            g->gain = (float) std::sqrt (limit (tout / (0.5 * (g->fadeIn + g->fadeOut)), 0.25, 2.25));
    }

    g->source = source;
    g->offset = std::min<std::int64_t> (0, (std::int64_t) std::llround (source - centre));
    g->active = true;

    lastGrain = g;
    lastSource = source;
    lastMarkIndex = markIndex;
    sourceValid = voiced && aligned;
}

/** Mode LIVE classique : à chaque nouvelle mesure de hauteur, la vitesse de
    lecture des grains en cours est réajustée. La correction suit ainsi la
    voix à quelques millisecondes près au lieu d'attendre le grain suivant. */
void TuneEngine::steerLiveGrains() noexcept
{
    if (hq || params.formant || lastGrain == nullptr || ! lastGrain->active || ! lastGrain->steered)
        return;

    const Control control = newestControl();
    if (! control.voiced || control.period <= 0.0f || control.outPeriod <= 0.0f)
        return;

    const double tin = (double) control.period;
    const double pinned = limit ((double) control.outPeriod, std::max (12.0, 0.25 * tin), 4.0 * tin);
    const double local = control.pin > 0.001f ? predictedPeriod (control, tin, lastGrain->position) : tin;
    const double speed = limit (local / outputPeriod (control, tin, pinned, local), 0.25, 4.0);

    for (auto& g : grains)
    {
        if (g.active && g.steered)
        {
            g.speed = speed;
            g.table = &sinc->forRatio (speed);
        }
    }
}

void TuneEngine::renderGrains (float* wet, int numCh) noexcept
{
    const double u = (double) now;

    for (auto& g : grains)
    {
        if (! g.active)
            continue;

        float w;
        if (u <= g.centre)
        {
            w = hann ((u - g.start) / g.fadeIn);
        }
        else
        {
            const double x = 1.0 - (u - g.centre) / g.fadeOut;
            if (x <= 0.0)
            {
                g.active = false;
                continue;
            }

            w = hann (x);
        }

        w *= g.gain;

        if (g.integer)
        {
            const std::int64_t index = now + g.offset;
            for (int ch = 0; ch < numCh; ++ch)
                wet[ch] += w * inRing[(size_t) ch][(size_t) (index & inMask)];
        }
        else
        {
            auto index = (std::int64_t) std::floor (g.position);
            auto frac = (float) (g.position - (double) index);

            // Garde-fous : ne jamais lire ce qui n'est pas encore arrivé, ni
            // ce que la mémoire circulaire a déjà recouvert.
            const std::int64_t newestReadable = now - g.table->halfLength();
            const std::int64_t oldestReadable = now - inMask + 2 * g.table->halfLength();
            if (index > newestReadable || index < oldestReadable)
            {
                index = index > newestReadable ? newestReadable : oldestReadable;
                frac = 0.0f;
                g.position = (double) index;
            }

            for (int ch = 0; ch < numCh; ++ch)
                wet[ch] += w * g.table->read (inRing[(size_t) ch].data(), inMask, index, frac);

            g.position += g.speed;
        }
    }
}

} // namespace onirique::tune
