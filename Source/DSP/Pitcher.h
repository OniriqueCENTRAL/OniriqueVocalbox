#pragma once

#include <juce_dsp/juce_dsp.h>
#include "tune/PitchDetector.h"
#include "tune/Fft.h"
#include <array>
#include <cmath>
#include <vector>

namespace onirique::fx
{
/*  PITCHER : la voix rejouée par une banque d'harmoniques, qu'on peut décaler, étirer et moduler.

    Analyse (toutes les 5 ms environ) : la hauteur de la voix (le détecteur YIN d'ONIRIQUE TUNER) et l'enveloppe de son
    spectre (les sommets des harmoniques, lissés : les formants, ce qui fait le timbre de la voix).
    Synthèse : jusqu'à 48 harmoniques (et 24 pour la couche d'octave), chacune à la fréquence voulue et à l'amplitude que
    l'enveloppe donne à cette fréquence. Les réglages :
      PITCH       décale la hauteur (demi-tons), les formants restent (la voix ne devient pas « chipmunk ») ;
      FORMANT     décale les formants seuls (demi-tons) : voix plus petite (+) ou plus grande (-) ;
      HARMONICS   penche le spectre : - vers une voix ronde, presque sinus ; + vers une voix riche, bourdonnante ;
      RATIO       l'écart entre les harmoniques : 1 = la voix, en dessous et au-dessus des timbres métalliques, de cloche ;
      FM          une modulation de fréquence commune aux harmoniques (à l'octave de la note) : du grain, du growl ;
      ALTERNATOR  décale une harmonique sur deux (demi-tons) : des accords, des voix doubles et étranges ;
      OCTAVE      ajoute une couche une octave en dessous (-) ou au-dessus (+) ;
      GLIDE       le temps que met la hauteur pour rejoindre la note suivante (portamento) ;
      SMEAR       étale le timbre dans le temps (les formants glissent, la voix devient nappe) ;
      STEREO      écarte les harmoniques à gauche et à droite ;
      DETUNE      désaccorde légèrement les harmoniques entre elles (épaisseur, chœur) ;
      SNAP        accroche la hauteur au demi-ton le plus proche (l'effet robot) ;
      MIX         la voix d'origine et la voix rejouée.
    Les consonnes et les respirations (pas de hauteur) passent par la voix d'origine : le texte reste compréhensible.
    Aucune latence ; aucune allocation dans process(). */
class Pitcher
{
public:
    struct Params
    {
        float pitch = 0.0f, formant = 0.0f, harmonics = 0.0f, ratio = 1.0f, fm = 0.0f, alternator = 0.0f, octave = 0.0f;
        float glideMs = 0.0f, smear = 0.0f, stereo = 0.0f, detune = 0.0f, mix = 1.0f;
        bool snap = false, on = false;
    };

    void prepare (double sampleRate, int /*maxBlock*/)
    {
        fs = sampleRate;
        decim = juce::jmax (1, juce::roundToInt (fs / 24000.0));
        fa = fs / (double) decim;
        detector.prepare (fa, 60.0);
        detector.configure (65.0, 1100.0);
        detector.setTracking (0.45f);
        aRing.assign (8192, 0.0f);
        aMask = 8191;

        // l'analyse du timbre : 2048 points à 48 kHz (la même durée aux autres fréquences)
        int order = 11;
        while ((1 << order) < (int) std::round (2048.0 * fs / 48000.0)) ++order;
        fft = std::make_unique<tune::Fft> (order);
        N = 1 << order;
        hop = N / 8;
        ring.assign ((size_t) N, 0.0f);
        re.assign ((size_t) N, 0.0f);
        im.assign ((size_t) N, 0.0f);
        mag.assign ((size_t) N / 2 + 1, 0.0f);
        envB.assign ((size_t) N / 2 + 1, 0.0f);
        window.resize ((size_t) N);
        for (int i = 0; i < N; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) N);
        for (int i = 0; i < kTable; ++i) sinTable[(size_t) i] = std::sin (juce::MathConstants<float>::twoPi * (float) i / (float) kTable);
        sinTable[kTable] = sinTable[0];

        lpA.reset(); lpB.reset();
        const double cut = 0.4 * fa;
        lpA.setCoefficients (juce::IIRCoefficients::makeLowPass (fs, cut, 0.54));
        lpB.setCoefficients (juce::IIRCoefficients::makeLowPass (fs, cut, 1.31));
        for (auto& h : hp) { h.reset(); h.setCoefficients (juce::IIRCoefficients::makeHighPass (fs, 3500.0, 0.7)); }
        reset();
    }

    void reset() noexcept
    {
        std::fill (aRing.begin(), aRing.end(), 0.0f);
        std::fill (ring.begin(), ring.end(), 0.0f);
        std::fill (envB.begin(), envB.end(), 0.0f);
        detector.reset();
        aCount = 0; ringPos = 0; sinceHop = 0; decimPhase = 0;
        f0In = 0.0f; f0Out = 0.0f; voiced = 0.0f; voicedTarget = 0.0f;
        for (auto& v : voices) v = Partial {};
        modPhase = 0.0;
        wet = 0.0f;
        noise = 0x12345678u;
    }

    void set (const Params& p) noexcept { params = p; }
    // pour l'écran : la hauteur entendue et celle rejouée (Hz, 0 = pas de note)
    float shownIn() const noexcept { return inHz.load (std::memory_order_relaxed); }
    float shownOut() const noexcept { return outHz.load (std::memory_order_relaxed); }

    // ch : 1 ou 2 canaux, traités en place
    void process (float* const* ch, int numCh, int n) noexcept
    {
        const float mixTarget = params.on ? juce::jlimit (0.0f, 1.0f, params.mix) : 0.0f;
        if (mixTarget <= 0.0f && wet < 1.0e-4f) { wet = 0.0f; feedOnly (ch, numCh, n); return; }

        const float wetCoef = 1.0f - std::exp (-1.0f / (0.02f * (float) fs));
        const float vCoef = 1.0f - std::exp (-1.0f / (0.012f * (float) fs));
        for (int i = 0; i < n; ++i)
        {
            const float mono = numCh > 1 ? 0.5f * (ch[0][i] + ch[1][i]) : ch[0][i];
            push (mono);
            if (++sinceHop >= hop) { sinceHop = 0; analyse(); }

            voiced += vCoef * (voicedTarget - voiced);
            float l = 0.0f, r = 0.0f;
            synth (l, r);
            wet += wetCoef * (mixTarget - wet);

            for (int c = 0; c < numCh; ++c)
            {
                const float dry = ch[c][i];
                const float syn = numCh > 1 ? (c == 0 ? l : r) : 0.5f * (l + r);
                // pas de hauteur (consonnes, souffles) : la voix d'origine ; avec une note : la voix rejouée
                const float w = voiced * syn + (1.0f - voiced) * dry;
                ch[c][i] = dry + wet * (w - dry);
            }
        }
    }

private:
    static constexpr int kMain = 48, kOct = 24, kTable = 4096;
    struct Partial { double phase = 0.0; float amp = 0.0f, target = 0.0f, inc = 0.0f, incTarget = 0.0f, panL = 0.7071f, panR = 0.7071f; };

    // coupé : l'analyse continue (la voix reprend sans à-coup), le son passe tel quel
    void feedOnly (float* const* ch, int numCh, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            push (numCh > 1 ? 0.5f * (ch[0][i] + ch[1][i]) : ch[0][i]);
            if (++sinceHop >= hop) { sinceHop = 0; analyse(); }
        }
    }

    void push (float x) noexcept
    {
        ring[(size_t) ringPos] = x;
        ringPos = (ringPos + 1) & (N - 1);
        // la voie d'analyse de hauteur : passe-bas puis décimation
        const float y = (float) lpB.processSingleSampleRaw ((float) lpA.processSingleSampleRaw (x));
        if (++decimPhase >= decim)
        {
            decimPhase = 0;
            aRing[(size_t) (aCount & aMask)] = y;
            ++aCount;
        }
    }

    void analyse() noexcept
    {
        // 1. la hauteur
        if (aCount > 2048)
        {
            const auto m = detector.analyse (aRing.data(), aMask, aCount);
            if (m.voiced && m.period > 1.0f)
            {
                const float f = (float) (fa / (double) m.period);
                f0In = f0In > 0.0f ? f0In + 0.5f * (f - f0In) : f;
                voicedTarget = 1.0f;
            }
            else voicedTarget = 0.0f;
        }
        if (f0In <= 0.0f) return;

        // 2. l'enveloppe du timbre : spectre fenêtré des N derniers échantillons, sommets des harmoniques lissés
        for (int i = 0; i < N; ++i)
        {
            re[(size_t) i] = ring[(size_t) ((ringPos + i) & (N - 1))] * window[(size_t) i];
            im[(size_t) i] = 0.0f;
        }
        fft->forward (re.data(), im.data());
        const int half = N / 2;
        for (int b = 0; b <= half; ++b) mag[(size_t) b] = std::sqrt (re[(size_t) b] * re[(size_t) b] + im[(size_t) b] * im[(size_t) b]);
        const float binHz = (float) fs / (float) N;
        const int w = juce::jlimit (1, 64, (int) (0.5f * f0In / binHz));
        for (int b = 0; b <= half; ++b)
        {
            float mx = 0.0f;
            for (int j = juce::jmax (0, b - w); j <= juce::jmin (half, b + w); ++j) mx = std::max (mx, mag[(size_t) j]);
            envB[(size_t) b] = mx * (4.0f / (float) N);   // amplitude d'une sinusoïde (fenêtre de Hann)
        }
        retarget();
    }

    float envAt (float hz) const noexcept
    {
        const float binHz = (float) fs / (float) N;
        const float x = hz / binHz;
        const int half = N / 2;
        if (x <= 0.0f || x >= (float) half) return 0.0f;
        const int b = (int) x;
        const float t = x - (float) b;
        return envB[(size_t) b] + t * (envB[(size_t) b + 1] - envB[(size_t) b]);
    }

    // les fréquences et les amplitudes visées de chaque harmonique (une fois par trame)
    void retarget() noexcept
    {
        const auto& p = params;
        float target = f0In * std::pow (2.0f, p.pitch / 12.0f);
        if (p.snap)
        {
            const float midi = 69.0f + 12.0f * std::log2 (target / 440.0f);
            target = 440.0f * std::pow (2.0f, (std::round (midi) - 69.0f) / 12.0f);
        }
        // GLIDE : la hauteur rejoint la note en glissant (en demi-tons, trame après trame)
        if (f0Out <= 0.0f) f0Out = target;
        else
        {
            const float frameSec = (float) hop / (float) fs;
            const float k = p.glideMs <= 1.0f ? 1.0f : 1.0f - std::exp (-frameSec / (p.glideMs * 0.001f));
            f0Out *= std::pow (target / f0Out, k);
        }
        inHz.store (voicedTarget > 0.5f ? f0In : 0.0f, std::memory_order_relaxed);
        outHz.store (voicedTarget > 0.5f ? f0Out : 0.0f, std::memory_order_relaxed);
        const float formantRatio = std::pow (2.0f, p.formant / 12.0f);
        const float tilt = p.harmonics >= 0.0f ? 0.8f * p.harmonics : 1.6f * p.harmonics;
        const float altRatio = std::pow (2.0f, p.alternator / 12.0f);
        const float nyq = 0.45f * (float) fs;
        const float twoPiOverFs = 1.0f / (float) fs;   // incrément en tours par échantillon
        const float stereo = juce::jlimit (0.0f, 1.0f, p.stereo);
        auto setPartial = [&] (Partial& v, float hz, float amp, int k)
        {
            if (hz <= 20.0f || hz >= nyq) { v.target = 0.0f; return; }
            v.incTarget = hz * twoPiOverFs;
            if (v.amp <= 1.0e-6f) v.inc = v.incTarget;   // une harmonique qui naît part directement à sa fréquence
            v.target = amp;
            const float pan = (k % 2 == 1 ? -1.0f : 1.0f) * stereo * 0.85f;   // -1 gauche .. +1 droite
            v.panL = std::sqrt (0.5f * (1.0f - pan));
            v.panR = std::sqrt (0.5f * (1.0f + pan));
        };
        for (int k = 1; k <= kMain; ++k)
        {
            auto& v = voices[(size_t) k - 1];
            float hz = f0Out * (1.0f + (float) (k - 1) * p.ratio);
            if (k % 2 == 0) hz *= altRatio;
            const float cents = (k % 2 == 1 ? 0.5f : -0.5f) * p.detune;
            hz *= std::pow (2.0f, cents / 1200.0f);
            const float amp = envAt (hz / formantRatio) * std::pow ((float) k, tilt);
            setPartial (v, hz, amp, k);
        }
        // OCTAVE : une couche à l'octave du dessous ou du dessus
        const float oct = juce::jlimit (-1.0f, 1.0f, p.octave);
        const float octF = f0Out * (oct < 0.0f ? 0.5f : 2.0f);
        for (int k = 1; k <= kOct; ++k)
        {
            auto& v = voices[(size_t) (kMain + k - 1)];
            const float hz = octF * (1.0f + (float) (k - 1) * p.ratio);
            const float amp = std::abs (oct) * envAt (hz / formantRatio) * std::pow ((float) k, tilt);
            setPartial (v, hz, amp, k + 1);
        }
        // le plus fort ne dépasse pas la voix d'origine (HARMONICS + et OCTAVE peuvent beaucoup ajouter)
        float sum = 0.0f;
        for (const auto& v : voices) sum += v.target * v.target;
        const float ref = std::max (1.0e-9f, [&] { float s = 0.0f; for (int k = 1; k <= kMain; ++k) { const float a = envAt (f0In * (float) k); s += a * a; } return s; }());
        const float g = sum > ref ? std::sqrt (ref / sum) : 1.0f;
        for (auto& v : voices) v.target *= g;
    }

    inline float sinTurn (double phase) const noexcept
    {
        const double x = (phase - std::floor (phase)) * kTable;
        const int i = (int) x;
        const float t = (float) (x - (double) i);
        return sinTable[(size_t) i] + t * (sinTable[(size_t) i + 1] - sinTable[(size_t) i]);
    }

    void synth (float& l, float& r) noexcept
    {
        // SMEAR : le niveau de chaque harmonique suit le timbre en 6 ms, jusqu'à 400 ms (la voix devient nappe)
        const float ampCoef = 1.0f - std::exp (-1.0f / ((0.006f + 0.4f * juce::jlimit (0.0f, 1.0f, params.smear)) * (float) fs));
        const float incCoef = 1.0f - std::exp (-1.0f / (0.004f * (float) fs));
        // FM : une modulation commune à l'octave de la note
        const float fmIndex = 2.5f * juce::jlimit (0.0f, 1.0f, params.fm);
        modPhase += (double) (2.0f * f0Out / (float) fs);
        if (modPhase >= 1.0) modPhase -= std::floor (modPhase);
        const float pm = fmIndex > 0.0f ? fmIndex * sinTurn (modPhase) / juce::MathConstants<float>::twoPi : 0.0f;
        for (auto& v : voices)
        {
            v.amp += ampCoef * (v.target - v.amp);
            if (v.amp < 1.0e-6f) continue;
            v.inc += incCoef * (v.incTarget - v.inc);
            v.phase += (double) v.inc;
            if (v.phase >= 1.0) v.phase -= std::floor (v.phase);
            const float s = v.amp * sinTurn (v.phase + (double) pm);
            l += s * v.panL;
            r += s * v.panR;
        }
        l *= 1.4142f; r *= 1.4142f;   // les panoramiques à puissance constante : au centre, le niveau d'origine
    }

    Params params;
    double fs = 48000.0, fa = 24000.0;
    int decim = 2, decimPhase = 0;
    tune::PitchDetector detector;
    std::vector<float> aRing;
    int aMask = 8191;
    std::int64_t aCount = 0;
    std::unique_ptr<tune::Fft> fft;
    int N = 2048, hop = 256, ringPos = 0, sinceHop = 0;
    std::vector<float> ring, re, im, mag, envB, window;
    std::array<float, kTable + 1> sinTable {};
    juce::IIRFilter lpA, lpB;
    std::array<juce::IIRFilter, 2> hp;
    float f0In = 0.0f, f0Out = 0.0f, voiced = 0.0f, voicedTarget = 0.0f, wet = 0.0f;
    std::array<Partial, kMain + kOct> voices {};
    double modPhase = 0.0;
    std::uint32_t noise = 0x12345678u;
    std::atomic<float> inHz { 0.0f }, outHz { 0.0f };
};
} // namespace onirique::fx
