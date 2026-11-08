#include "MixAnalysis.h"
#include "Modules.h"
#include "onirique_separate.hpp"
#include "DSP/DeClick.h"    // les détecteurs de DE-CLICK et DE-BREATH, passés sur la prise
#include "DSP/DeBreath.h"
#include <memory>
#if __has_include (<BinaryData.h>)
 #include <BinaryData.h>   // le cerveau livré dans le plugin
#endif

namespace onirique::mix
{
namespace
{
float dbOf (double power) { return power > 1.0e-12 ? (float) (10.0 * std::log10 (power)) : -120.0f; }

float pct (std::vector<float> v, float q)
{
    if (v.empty()) return -120.0f;
    const auto k = (size_t) juce::jlimit (0, (int) v.size() - 1, (int) std::lround (q * (float) (v.size() - 1)));
    std::nth_element (v.begin(), v.begin() + (long) k, v.end());
    return v[k];
}

float median (std::vector<float> v) { return pct (std::move (v), 0.5f); }

juce::String hzText (float f) { return f >= 1000.0f ? juce::String (f / 1000.0f, f >= 10000.0f ? 0 : 1) + " kHz" : juce::String (juce::roundToInt (f)) + " Hz"; }
juce::String dbText (float d) { return (d > 0.0f ? "+" : "") + juce::String (d, 1) + " dB"; }
juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }
} // namespace

//==============================================================================
Profile analyse (const float* x, int n, double sr)
{
    Profile p;
    if (x == nullptr || sr < 8000.0 || n < (int) (sr * 2.0)) return p;
    p.seconds = (double) n / sr;

    // ---- enveloppe de 10 ms (dB)
    const int h10 = juce::jmax (1, (int) (sr * 0.01));
    std::vector<float> env;
    env.reserve ((size_t) (n / h10 + 1));
    for (int i = 0; i + h10 <= n; i += h10)
    {
        double e = 0.0;
        for (int j = 0; j < h10; ++j) e += (double) x[i + j] * x[i + j];
        env.push_back (dbOf (e / h10));
    }
    auto windows = [&env] (int len, int hop)
    {
        std::vector<float> w;
        for (size_t s = 0; s + (size_t) len <= env.size(); s += (size_t) hop)
        {
            double e = 0.0;
            for (int j = 0; j < len; ++j) e += std::pow (10.0, env[s + (size_t) j] / 10.0);
            w.push_back (dbOf (e / len));
        }
        return w;
    };
    const auto w400 = windows (40, 10), w50 = windows (5, 5);
    if (w400.size() < 10) return p;

    p.noiseFloorDb = juce::jmax (-100.0f, pct (env, 0.08f));
    const float thr = juce::jmax (p.noiseFloorDb + 12.0f, pct (w400, 0.95f) - 30.0f);
    {
        // le fond : les trames sous le seuil (hors silence numérique) ; stable = souffle, variable = réverbe, respirations
        std::vector<float> gaps;
        for (float v : env) if (v < thr && v > -100.0f) gaps.push_back (v);
        if (gaps.size() > 20)
        {
            const float lo = pct (gaps, 0.1f), hi = pct (gaps, 0.6f);
            p.floorSpreadDb = hi - lo;
        }
    }
    std::vector<float> act400, act50;
    for (float v : w400) if (v >= thr) act400.push_back (v);
    for (float v : w50) if (v >= thr) act50.push_back (v);
    if (act400.size() < 5) return p;
    p.loudDb = pct (act400, 0.9f);
    p.softDb = pct (act400, 0.1f);
    p.dynRange = p.loudDb - p.softDb;
    p.peak50Db = pct (act50, 0.95f);

    // ---- spectre : FFT de 4096 (8192 au-delà de 50 kHz), recouvrement de 75 %
    const int order = sr > 50000.0 ? 13 : 12, N = 1 << order, hop = N / 4;
    juce::dsp::FFT fft (order);
    juce::dsp::WindowingFunction<float> win ((size_t) N, juce::dsp::WindowingFunction<float>::hann, false);
    std::vector<float> buf ((size_t) (2 * N));
    std::vector<int> bandOf ((size_t) (N / 2), -1);
    for (int k = 1; k < N / 2; ++k)
    {
        const float f = (float) (k * sr / N);
        for (int b = 0; b < kBands; ++b)
            if (f >= kCentres[b] * 0.8909f && f < kCentres[b] * 1.1225f) { bandOf[(size_t) k] = b; break; }
    }
    std::array<double, kBands> acc {};
    std::vector<double> fine ((size_t) (N / 2), 0.0);   // le spectre fin moyen, pour trouver les résonances
    int frames = 0;
    std::vector<float> sibRatio, sibLevel, frameLevel;
    std::vector<std::vector<float>> sibSpectra;   // spectre 4 .. 10 kHz des trames, pour trouver où sifflent les S
    const int k4 = (int) (4000.0 * N / sr), k10 = juce::jmin (N / 2 - 1, (int) (10000.0 * N / sr));
    bool inPlosive = false;
    int plosives = 0;
    for (int s = 0; s + N <= n; s += hop)
    {
        double e = 0.0;
        for (int i = 0; i < N; ++i) e += (double) x[s + i] * x[s + i];
        if (dbOf (e / N) < thr) { inPlosive = false; continue; }
        std::fill (buf.begin(), buf.end(), 0.0f);
        std::copy (x + s, x + s + N, buf.begin());
        win.multiplyWithWindowingTable (buf.data(), (size_t) N);
        fft.performFrequencyOnlyForwardTransform (buf.data());
        double eSib = 0.0, eAll = 0.0, eLow = 0.0;
        for (int k = 1; k < N / 2; ++k)
        {
            const double pw = (double) buf[(size_t) k] * buf[(size_t) k];
            const float f = (float) (k * sr / N);
            if (bandOf[(size_t) k] >= 0) acc[(size_t) bandOf[(size_t) k]] += pw;
            fine[(size_t) k] += pw;
            if (f >= 100.0f && f < 16000.0f) eAll += pw;
            if (f >= 4500.0f && f < 10000.0f) eSib += pw;
            if (f < 150.0f) eLow += pw;
        }
        ++frames;
        if (eAll > 0.0)
        {
            sibRatio.push_back (dbOf (eSib / eAll));
            sibLevel.push_back (dbOf (eSib));
            frameLevel.push_back (dbOf (eAll));
            std::vector<float> sp ((size_t) juce::jmax (0, k10 - k4));
            for (int k = k4; k < k10; ++k) sp[(size_t) (k - k4)] = buf[(size_t) k] * buf[(size_t) k];
            sibSpectra.push_back (std::move (sp));
        }
        const bool plo = eLow > 0.5 * (eAll + eLow) && dbOf (e / N) > p.loudDb - 18.0f;
        if (plo && ! inPlosive) ++plosives;
        inPlosive = plo;
    }
    if (frames < 8) return p;
    p.plosivesPerMin = (float) plosives / (float) (p.seconds / 60.0);

    // ---- chant ou rap : la hauteur suivie toutes les 10 ms (autocorrélation normalisée sur la voix ramenée vers 8 kHz) ; une
    //      note tenue = au moins 150 ms où la hauteur bouge de moins d'un demi-ton. Le rap en a peu, le chant beaucoup.
    {
        const int d = juce::jmax (1, (int) std::round (sr / 8000.0));
        const double fs = sr / d;
        std::vector<float> y ((size_t) (n / d));
        for (size_t i = 0; i < y.size(); ++i) { float s = 0.0f; for (int j = 0; j < d; ++j) s += x[(int) i * d + j]; y[i] = s / (float) d; }
        const int W = (int) (fs * 0.04), step = juce::jmax (1, (int) (fs * 0.01));
        const int lagMin = juce::jmax (2, (int) (fs / 900.0)), lagMax = (int) (fs / 70.0);
        std::vector<float> corr ((size_t) lagMax + 2, 0.0f);
        int active = 0, held = 0, run = 0;
        float prevSt = 0.0f;
        // les notes tenues donnent aussi la justesse (écart à la note la plus proche) et la tonalité (le poids de chaque note)
        std::vector<float> runSt;
        std::array<double, 12> pcWeight {};
        double devSum = 0.0, devW = 0.0, midiSum = 0.0;
        auto closeRun = [&]
        {
            if (run >= 15 && ! runSt.empty())
            {
                held += run;
                const float mid = median (runSt);           // demi-tons au-dessus de la2 (110 Hz)
                const float midi = 45.0f + mid;
                const float frac = midi - std::round (midi);
                devSum += std::abs (frac) * 100.0 * run; devW += run;
                midiSum += (double) midi * run;
                pcWeight[(size_t) (((int) std::round (midi) % 12 + 12) % 12)] += run;
            }
            run = 0;
            runSt.clear();
        };
        for (int s = 0; s + W + lagMax < (int) y.size(); s += step)
        {
            const size_t e10 = (size_t) ((double) s * d / h10);
            if (e10 >= env.size() || env[e10] < thr) { closeRun(); continue; }
            ++active;
            float best = 0.0f; int bestLag = 0;
            for (int lag = lagMin; lag <= lagMax; ++lag)
            {
                double xy = 0.0, xx = 0.0, yy = 0.0;
                for (int i = 0; i < W; ++i) { const float a = y[(size_t) (s + i)], b = y[(size_t) (s + i + lag)]; xy += a * b; xx += a * a; yy += b * b; }
                const float r = xx + yy > 0.0 ? (float) (2.0 * xy / (xx + yy)) : 0.0f;
                corr[(size_t) lag] = r;
                if (r > best) best = r;
            }
            if (best < 0.8f) { closeRun(); continue; }
            // la période : le premier sommet presque aussi haut que le meilleur (évite de sauter à l'octave en dessous)
            for (int lag = lagMin + 1; lag < lagMax; ++lag)
                if (corr[(size_t) lag] >= 0.92f * best && corr[(size_t) lag] >= corr[(size_t) lag - 1] && corr[(size_t) lag] >= corr[(size_t) lag + 1]) { bestLag = lag; break; }
            if (bestLag == 0) { closeRun(); continue; }
            const float st = 12.0f * std::log2 ((float) (fs / bestLag) / 110.0f);
            if (run > 0 && std::abs (st - prevSt) > 0.5f) closeRun();
            ++run; prevSt = st;
            runSt.push_back (st);
        }
        closeRun();
        if (active > 50) p.sung = juce::jlimit (0.0f, 1.0f, ((float) held / (float) active - 0.15f) / 0.3f);
        if (devW > 0.0) { p.pitchDevCents = (float) (devSum / devW); p.noteMidi = (float) (midiSum / devW); }
        // la tonalité : les profils de Krumhansl-Kessler (poids des degrés majeurs et mineurs), corrélés aux notes tenues
        double total = 0.0;
        for (double w : pcWeight) total += w;
        if (total >= 60.0)   // au moins ~0,6 s de notes tenues
        {
            static constexpr double maj[12] { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
            static constexpr double mnr[12] { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
            auto corrOf = [&] (const double* prof, int root)
            {
                double mx = 0, my = 0;
                for (int i = 0; i < 12; ++i) { mx += pcWeight[(size_t) ((i + root) % 12)]; my += prof[i]; }
                mx /= 12.0; my /= 12.0;
                double sxy = 0, sxx = 0, syy = 0;
                for (int i = 0; i < 12; ++i)
                {
                    const double a = pcWeight[(size_t) ((i + root) % 12)] - mx, b = prof[i] - my;
                    sxy += a * b; sxx += a * a; syy += b * b;
                }
                return sxx > 0.0 && syy > 0.0 ? sxy / std::sqrt (sxx * syy) : 0.0;
            };
            double best = -2.0, second = -2.0;
            for (int root = 0; root < 12; ++root)
                for (int mode = 0; mode < 2; ++mode)
                {
                    const double c = corrOf (mode == 0 ? maj : mnr, root);
                    if (c > best) { second = best; best = c; p.keyRoot = root; p.keyMinor = mode == 1; }
                    else if (c > second) second = c;
                }
            // confiance : la corrélation, et l'avance sur la deuxième tonalité (souvent la relative)
            p.keyConfidence = (float) juce::jlimit (0.0, 1.0, (best - 0.5) * 2.0) * (float) juce::jlimit (0.3, 1.0, (best - second) * 8.0 + 0.3);
        }
    }

    // ---- bruits de bouche et respirations : les détecteurs mêmes de DE-CLICK et DE-BREATH, sur toute la prise
    {
        std::vector<float> a (x, x + n), b (x, x + n);
        DeClick dc; dc.prepare (sr); dc.setParams (0.5f, 0.7f, false);
        DeBreath db; db.prepare (sr); db.setParams (12.0f, 0.5f, false);
        for (int s = 0; s < n; s += 512)
        {
            const int m = juce::jmin (512, n - s);
            float* ca[1] { a.data() + s };
            float* cb[1] { b.data() + s };
            dc.process (ca, 1, m);
            db.process (cb, 1, m);
        }
        const float minutes = (float) (p.seconds / 60.0);
        p.clicksPerMin = (float) dc.takeClicks() / minutes;
        p.breathsPerMin = (float) db.breathsFound() / minutes;
        p.breathDb = db.breathLevelDb();
    }

    // timbre : moyenne par tiers d'octave, la zone 200 Hz .. 5 kHz ramenée à 0
    for (int b = 0; b < kBands; ++b) p.bands[(size_t) b] = acc[(size_t) b] > 0.0 ? dbOf (acc[(size_t) b] / frames) : -120.0f;
    float ref = 0.0f;
    for (int b = 10; b <= 24; ++b) ref += p.bands[(size_t) b];
    ref /= 15.0f;
    for (auto& v : p.bands) v = juce::jmax (-60.0f, v - ref);

    // résonances : le spectre fin moyen, lissé sur 1/12 d'octave (les harmoniques, qui bougent avec la note, s'y fondent),
    // comparé au même spectre lissé sur 2/3 d'octave ; un pic qui dépasse de plus de 3 dB entre 250 Hz et 9 kHz est retenu
    {
        const int half = N / 2;
        std::vector<float> fdb ((size_t) half, -120.0f), narrow ((size_t) half, -120.0f), wide ((size_t) half, -120.0f);
        for (int k = 1; k < half; ++k) fdb[(size_t) k] = dbOf (fine[(size_t) k] / frames);
        auto smooth = [&] (std::vector<float>& out, float octaves)
        {
            const float r = std::pow (2.0f, octaves / 2.0f);
            std::vector<double> cum ((size_t) half + 1, 0.0);
            for (int k = 0; k < half; ++k) cum[(size_t) k + 1] = cum[(size_t) k] + fdb[(size_t) k];
            for (int k = 2; k < half; ++k)
            {
                const int a = juce::jmax (1, (int) std::floor (k / r)), b = juce::jmin (half - 1, juce::jmax (a + 1, (int) std::ceil (k * r)));
                out[(size_t) k] = (float) ((cum[(size_t) b + 1] - cum[(size_t) a]) / (b - a + 1));
            }
        };
        smooth (narrow, 1.0f / 12.0f);
        smooth (wide, 2.0f / 3.0f);
        // sous 900 Hz, les pics sont surtout les harmoniques des notes tenues : la boue et le bas-médium sont traités en bandes larges
        const int k250 = (int) (900.0 * N / sr), k9k = juce::jmin (half - 2, (int) (9000.0 * N / sr));
        struct Peak { float f, db, q; };
        std::vector<Peak> peaks;
        for (int k = juce::jmax (2, k250); k < k9k; ++k)
        {
            const float ex = narrow[(size_t) k] - wide[(size_t) k];
            if (ex < 3.0f || narrow[(size_t) k] < narrow[(size_t) k - 1] || narrow[(size_t) k] < narrow[(size_t) k + 1]) continue;
            // la largeur : là où le pic retombe à la moitié de son excès
            int lo = k, hi = k;
            while (lo > 2 && narrow[(size_t) lo] - wide[(size_t) lo] > 0.5f * ex) --lo;
            while (hi < half - 2 && narrow[(size_t) hi] - wide[(size_t) hi] > 0.5f * ex) ++hi;
            const float f = (float) (k * sr / N), bw = juce::jmax ((float) (sr / N), (float) ((hi - lo) * sr / N));
            peaks.push_back ({ f, ex, juce::jlimit (2.0f, 6.0f, f / bw) });
        }
        std::sort (peaks.begin(), peaks.end(), [] (const Peak& a, const Peak& b) { return a.db > b.db; });
        for (const auto& pk : peaks)
        {
            if (p.numRes >= 4) break;
            bool near = false;   // deux pics à moins d'un tiers d'octave : le plus fort suffit
            for (int i = 0; i < p.numRes; ++i) near = near || std::abs (std::log2 (pk.f / p.resFreq[(size_t) i])) < 1.0f / 3.0f;
            if (near) continue;
            p.resFreq[(size_t) p.numRes] = pk.f; p.resDb[(size_t) p.numRes] = pk.db; p.resQ[(size_t) p.numRes] = pk.q;
            ++p.numRes;
        }
    }

    // sifflantes : les 10 % de trames les plus riches en aigus ; leur pic entre 4 et 10 kHz
    if (! sibRatio.empty())
    {
        // les S : niveau de leurs aigus (trames les plus riches en aigus) par rapport aux trames fortes de la voix
        const float cut = pct (sibRatio, 0.9f);
        std::vector<float> sl;
        for (size_t i = 0; i < sibRatio.size(); ++i) if (sibRatio[i] >= cut) sl.push_back (sibLevel[i]);
        p.sibilance = median (sl) - pct (frameLevel, 0.9f);
        std::vector<double> sum ((size_t) juce::jmax (0, k10 - k4), 0.0);
        for (size_t i = 0; i < sibRatio.size(); ++i)
            if (sibRatio[i] >= cut)
                for (size_t k = 0; k < sum.size(); ++k) sum[k] += sibSpectra[i][k];
        size_t best = 0;
        double bestV = -1.0;
        const int smooth = juce::jmax (1, (int) (250.0 * N / sr));   // pic lissé sur 250 Hz
        for (size_t k = 0; k < sum.size(); ++k)
        {
            double v = 0.0;
            for (int j = -smooth; j <= smooth; ++j) { const long q = (long) k + j; if (q >= 0 && q < (long) sum.size()) v += sum[(size_t) q]; }
            if (v > bestV) { bestV = v; best = k; }
        }
        p.sibFreq = juce::jlimit (4000.0f, 9000.0f, (float) ((double) (k4 + (int) best) * sr / N));
    }

    // ---- espace : après la fin des phrases (l'enveloppe lissée repasse sous les phrases fortes - 15 dB après y avoir été
    //      300 ms), la pente de la queue donne la réverbe, son niveau à 120 ms le mélange, une bosse répétée l'écho
    std::vector<float> sm (env.size());
    for (size_t i = 0; i < env.size(); ++i)
    {
        double e = 0.0; int c = 0;
        for (int j = -2; j <= 2; ++j) { const long q = (long) i + j; if (q >= 0 && q < (long) env.size()) { e += std::pow (10.0, env[(size_t) q] / 10.0); ++c; } }
        sm[i] = dbOf (e / c);
    }
    std::vector<float> rts, wets;
    std::vector<int> echoHist (100, 0);
    int offsets = 0, above = 0;
    for (size_t i = 1; i + 90 < sm.size(); ++i)
    {
        if (sm[i] >= p.loudDb - 8.0f) { ++above; continue; }
        // fin de phrase : on repasse sous les phrases fortes - 8 dB après y avoir été 200 ms
        if (above >= 20)
        {
            ++offsets;
            float phrase = -120.0f;
            for (int j = 1; j <= 20; ++j) phrase = juce::jmax (phrase, sm[i - (size_t) j]);
            // la queue : régression sur 400 ms tant qu'elle reste au-dessus du fond et qu'aucune phrase ne repart
            double sx = 0, sy = 0, sxx = 0, sxy = 0; int m = 0;
            for (int j = 0; j < 40; ++j)
            {
                const float v = sm[i + (size_t) j];
                if (v < p.noiseFloorDb + 4.0f || (j > 3 && v > sm[i + (size_t) j - 1] + 3.0f)) break;
                sx += j; sy += v; sxx += (double) j * j; sxy += (double) j * v; ++m;
            }
            if (m >= 5)
            {
                const double slope = (m * sxy - sx * sy) / juce::jmax (1.0e-9, m * sxx - sx * sx);   // dB par 10 ms
                if (slope < -0.05) rts.push_back ((float) juce::jlimit (0.1, 8.0, -60.0 / (slope * 100.0)));
            }
            wets.push_back (sm[i + 12] - phrase);
            // l'écho : une bosse de 4 dB au même retard après la plupart des phrases
            for (int lag = 12; lag < 90; ++lag)
            {
                float lo = 1.0e9f;
                for (int j = 1; j <= 6; ++j) lo = juce::jmin (lo, sm[i + (size_t) (lag - j)]);
                if (sm[i + (size_t) lag] - lo > 4.0f && sm[i + (size_t) lag] > p.noiseFloorDb + 6.0f && sm[i + (size_t) lag] < phrase - 6.0f)
                    for (int d = -1; d <= 1; ++d) ++echoHist[(size_t) (lag + d)];
            }
        }
        above = 0;
    }
    if (rts.size() >= 3) p.rt60 = median (rts);
    if (wets.size() >= 3) p.wetDb = median (wets);
    if (offsets >= 4)
    {
        int best = 0;
        for (int lag = 12; lag < 90; ++lag) if (echoHist[(size_t) lag] > echoHist[(size_t) best]) best = lag;
        const float share = juce::jmin (1.0f, (float) echoHist[(size_t) best] / (2.0f * (float) offsets));
        if (share >= 0.5f) { p.echoMs = (float) best * 10.0f; p.echoAmount = juce::jmin (1.0f, share); }
    }
    p.valid = true;
    return p;
}

//==============================================================================
std::vector<float> isolateVoice (const juce::AudioBuffer<float>& song, std::function<bool()> shouldStop)
{
    const int n = song.getNumSamples();
    std::vector<float> out ((size_t) n, 0.0f);
    if (song.getNumChannels() < 2)
    {
        if (n > 0) std::copy (song.getReadPointer (0), song.getReadPointer (0) + n, out.begin());
        return out;
    }
    // le centre du mix : par fréquence et par trame, ce qui est pareil à gauche et à droite (même phase, même niveau)
    constexpr int order = 11, N = 1 << order, hop = N / 4;
    juce::dsp::FFT fft (order);
    std::vector<float> w ((size_t) N);
    for (int i = 0; i < N; ++i) w[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) N);
    std::vector<float> L ((size_t) (2 * N)), R ((size_t) (2 * N)), C ((size_t) (2 * N));
    const float* l = song.getReadPointer (0);
    const float* r = song.getReadPointer (1);
    const double sr = 44100.0;   // seules les bornes 90 Hz / 16 kHz en dépendent : une approximation suffit
    int count = 0;
    for (int s = 0; s + N <= n; s += hop)
    {
        if (shouldStop && (++count % 256) == 0 && shouldStop()) break;
        for (int i = 0; i < N; ++i) { L[(size_t) i] = l[s + i] * w[(size_t) i]; R[(size_t) i] = r[s + i] * w[(size_t) i]; }
        std::fill (L.begin() + N, L.end(), 0.0f);
        std::fill (R.begin() + N, R.end(), 0.0f);
        fft.performRealOnlyForwardTransform (L.data(), true);
        fft.performRealOnlyForwardTransform (R.data(), true);
        for (int k = 0; k <= N / 2; ++k)
        {
            const float lr = L[(size_t) (2 * k)], li = L[(size_t) (2 * k + 1)], rr = R[(size_t) (2 * k)], ri = R[(size_t) (2 * k + 1)];
            const float pl = lr * lr + li * li, pr = rr * rr + ri * ri, cross = lr * rr + li * ri;
            const float sim = 2.0f * cross / (pl + pr + 1.0e-20f);        // 1 : identiques
            const float bal = std::abs (pl - pr) / (pl + pr + 1.0e-20f);  // 0 : au centre
            const float f = (float) (k * sr / N);
            float m = juce::jlimit (0.0f, 1.0f, (sim - 0.6f) / 0.35f);
            m = m * m * juce::jlimit (0.0f, 1.0f, 1.0f - 2.0f * bal);
            if (f < 90.0f || f > 16000.0f) m = 0.0f;
            C[(size_t) (2 * k)] = 0.5f * m * (lr + rr);
            C[(size_t) (2 * k + 1)] = 0.5f * m * (li + ri);
        }
        fft.performRealOnlyInverseTransform (C.data());
        for (int i = 0; i < N; ++i) out[(size_t) (s + i)] += C[(size_t) i] * w[(size_t) i] / 1.5f;
    }
    return out;
}

//==============================================================================
bool canRead (const juce::File& f)
{
    juce::AudioFormatManager m;
    m.registerBasicFormats();
    return m.findFormatForFileExtension (f.getFileExtension()) != nullptr;
}

bool readSong (const juce::File& file, juce::AudioBuffer<float>& out, double& sampleRate, double maxSeconds)
{
    juce::AudioFormatManager m;
    m.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> rd (m.createReaderFor (file));
    if (rd == nullptr || rd->sampleRate <= 0.0) return false;
    sampleRate = rd->sampleRate;
    const auto len = (int) juce::jmin<juce::int64> (rd->lengthInSamples, (juce::int64) (maxSeconds * rd->sampleRate));
    if (len < (int) (rd->sampleRate * 5.0)) return false;
    out.setSize ((int) juce::jmin (2u, rd->numChannels), len);
    rd->read (&out, 0, len, 0, true, out.getNumChannels() > 1);
    return true;
}

juce::File modelFile()
{
    const juce::String name ("ggml-model-htdemucs_ft_vocals-4s-f16.bin");
    const auto bin = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
    for (const auto& f : { bin.getSiblingFile ("Resources").getChildFile (name), bin.getChildFile (name), bin.getChildFile ("Resources").getChildFile (name),
                           juce::File::getSpecialLocation (juce::File::commonApplicationDataDirectory).getChildFile ("Onirique/models").getChildFile (name),
                           juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Onirique/models").getChildFile (name) })
        if (f.existsAsFile() && f.getSize() > 1000000) return f;
   #if JUCE_WINDOWS
    // le modèle livré dans le plugin : écrit une fois dans AppDataOniriquemodels (même taille = déjà fait)
    void* module = juce::Process::getCurrentModuleInstanceHandle();
    const auto size = sep::embeddedModelSize (module);
    if (size > 1000000)
    {
        const auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Onirique/models");
        const auto f = dir.getChildFile (name);
        if (f.existsAsFile() && f.getSize() == size) return f;
        if (dir.createDirectory() && sep::extractEmbeddedModel (module, f.getFullPathName().toWideCharPointer()) && f.getSize() == size) return f;
    }
   #endif
    return {};
}

bool separateVoice (const juce::AudioBuffer<float>& song, double sr, std::vector<float>& voice, double& voiceRate,
                    std::function<bool (float)> shouldContinue, double maxSeconds)
{
    const auto model = modelFile();
    if (! model.existsAsFile() || sr <= 0.0 || song.getNumSamples() < (int) (sr * 5.0)) return false;
    // l'extrait : maxSeconds à partir de 30 % du morceau (couplets et refrains, pas l'intro)
    const int total = song.getNumSamples(), len = juce::jmin (total, (int) (maxSeconds * sr));
    const int start = juce::jmin (total - len, (int) (0.3 * total));
    // en 44,1 kHz stéréo
    const double ratio = sr / 44100.0;
    const int out = (int) std::floor ((double) len / ratio) - 4;
    if (out < 44100 * 5) return false;
    std::vector<float> lr[2];
    for (int c = 0; c < 2; ++c)
    {
        lr[c].resize ((size_t) out);
        const float* src = song.getReadPointer (juce::jmin (c, song.getNumChannels() - 1), start);
        if (std::abs (ratio - 1.0) < 1.0e-6) std::copy (src, src + out, lr[c].begin());
        else { juce::LagrangeInterpolator li; li.process (ratio, src, lr[c].data(), out); }
    }
    const int threads = juce::jlimit (1, 8, juce::SystemStats::getNumCpus() - 1);
    if (! sep::separateVocals (model.getFullPathName().toStdString(), lr[0].data(), lr[1].data(), out, threads, voice, shouldContinue))
        return false;
    voiceRate = 44100.0;
    return true;
}

//==============================================================================
//==============================================================================
// LE CERVEAU : la façon de mixer des artistes
namespace
{
juce::String plainText (juce::String t)
{
    // minuscules sans accents : comme la barre de recherche
    t = t.toLowerCase();
    const char* from[] { "é", "è", "ê", "ë", "à", "â", "î", "ï", "ô", "ù", "û", "ç", "’" };
    const char* to[] { "e", "e", "e", "e", "a", "a", "i", "i", "o", "u", "u", "c", "'" };
    for (int i = 0; i < 13; ++i) t = t.replace (juce::String::fromUTF8 (from[i]), to[i]);
    return t.trim();
}

juce::SpinLock brainLock;
std::shared_ptr<const std::vector<ArtistStyle>> brainNow;
juce::var correctionsNow;   // « corrections » du cerveau en service (global : décalages appris sur toutes les demandes)
// un cerveau est plus récent : version (le fichier du dépôt) plus haute, ou même version et révision (ce qu'ONR Neural Learning
// y a ajouté sur le serveur) plus haute
bool newerBrain (const juce::var& a, const juce::var& b)
{
    const int va = (int) a["version"], vb = (int) b["version"];
    return va != vb ? va > vb : (int) a.getProperty ("revision", 0) > (int) b.getProperty ("revision", 0);
}

std::shared_ptr<const std::vector<ArtistStyle>> loadBrain()
{
    auto list = std::make_shared<std::vector<ArtistStyle>>();
    auto addFrom = [&list] (const juce::var& root, bool replace)
    {
        if (auto* a = root["artists"].getArray())
            for (const auto& v : *a)
            {
                auto s = artistFromVar (v);
                if (s.name.isEmpty() || s.aliases.isEmpty()) continue;
                auto same = std::find_if (list->begin(), list->end(), [&s] (const ArtistStyle& o) { return o.name.equalsIgnoreCase (s.name); });
                if (same == list->end()) list->push_back (s);
                else if (replace) *same = s;
            }
    };
    // 1. le cerveau livré dans le plugin ; 2. une version plus récente téléchargée ; 3. les artistes trouvés par le serveur
    juce::var shipped;
   #if __has_include (<BinaryData.h>)
    shipped = juce::JSON::parse (juce::String::fromUTF8 (BinaryData::vocalboxbrain_json, BinaryData::vocalboxbrain_jsonSize));
   #endif
    if (const auto env = juce::SystemStats::getEnvironmentVariable ("VOCALBOX_BRAIN", {}); env.isNotEmpty())   // le banc d'essai
        shipped = juce::JSON::parse (juce::File (env));
    const auto newer = juce::JSON::parse (brainFolder().getChildFile ("brain.json"));
    const bool useNewer = newer.isObject() && newerBrain (newer, shipped);
    const auto& inUse = useNewer ? newer : shipped;
    addFrom (inUse, true);
    {
        const juce::SpinLock::ScopedLockType l (brainLock);
        correctionsNow = inUse["corrections"];
    }
    addFrom (juce::JSON::parse (brainFolder().getChildFile ("artists-cache.json")), false);
    return list;
}
} // namespace

juce::File brainFolder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Onirique").getChildFile ("VocalBox");
}

std::shared_ptr<const std::vector<ArtistStyle>> brainArtists()
{
    {
        const juce::SpinLock::ScopedLockType l (brainLock);
        if (brainNow != nullptr) return brainNow;
    }
    auto fresh = loadBrain();
    const juce::SpinLock::ScopedLockType l (brainLock);
    if (brainNow == nullptr) brainNow = fresh;
    return brainNow;
}

juce::var brainCorrections()
{
    brainArtists();
    const juce::SpinLock::ScopedLockType l (brainLock);
    return correctionsNow;
}

bool installBrainIfNewer (const juce::var& candidate)
{
    if (! candidate.isObject() || candidate["artists"].getArray() == nullptr) return false;
    juce::var shipped;
   #if __has_include (<BinaryData.h>)
    shipped = juce::JSON::parse (juce::String::fromUTF8 (BinaryData::vocalboxbrain_json, BinaryData::vocalboxbrain_jsonSize));
   #endif
    const auto f = brainFolder().getChildFile ("brain.json");
    const auto have = juce::JSON::parse (f);
    if (! newerBrain (candidate, shipped) || (have.isObject() && ! newerBrain (candidate, have))) return false;
    f.getParentDirectory().createDirectory();
    if (! f.replaceWithText (juce::JSON::toString (candidate))) return false;
    reloadBrain();
    return true;
}

void reloadBrain()
{
    auto fresh = loadBrain();
    const juce::SpinLock::ScopedLockType l (brainLock);
    brainNow = fresh;
}

ArtistStyle artistFromVar (const juce::var& v)
{
    ArtistStyle s;
    s.name = v["name"].toString().trim();
    s.genre = v["genre"].toString();
    if (auto* a = v["aliases"].getArray())
        for (const auto& x : *a)
            if (const auto t = plainText (x.toString()); t.length() >= 2) s.aliases.addIfNotAlreadyThere (t);
    if (s.name.isNotEmpty()) s.aliases.addIfNotAlreadyThere (plainText (s.name));
    s.hints = v["hints"];
    s.voice = v["voice"];
    return s;
}

void applyArtist (Hints& h, const ArtistStyle& a)
{
    const auto& x = a.hints;
    auto has = [&x] (const char* k) { return x.hasProperty (juce::Identifier (k)); };
    auto num = [&x] (const char* k) { return (float) (double) x.getProperty (juce::Identifier (k), 0.0); };
    // la couleur et la tenue de l'artiste s'ajoutent à ce qui est dit ; l'espace et les effets prennent le plus marqué
    h.bright += 0.8f * num ("bright"); h.air += 0.8f * num ("air"); h.warm += 0.8f * num ("warm"); h.dark += 0.8f * num ("dark");
    h.comp += 0.8f * num ("comp"); h.sat += 0.8f * num ("sat");
    if (has ("reverb") && ! h.reverbExplicit) h.reverb = juce::jmax (h.reverb, num ("reverb"));
    if (has ("reverbSize")) h.reverbSize = juce::jmax (h.reverbSize, num ("reverbSize"));
    if (has ("delay")) h.delay = juce::jmax (h.delay, num ("delay"));
    if (has ("satType") && h.satType < 0) h.satType = juce::jlimit (0, 3, (int) num ("satType"));
    h.doubler = juce::jmax (h.doubler, num ("doubler"));
    if (a.genre.containsIgnoreCase ("rnb") || a.genre.containsIgnoreCase ("pop")) h.sung = juce::jmax (h.sung, 0.3f);   // un artiste qui chante
    h.chorus = juce::jmax (h.chorus, num ("chorus"));
    h.flanger = juce::jmax (h.flanger, num ("flanger"));
    h.phone = juce::jmax (h.phone, num ("phone"));
    if ((bool) x.getProperty ("controlled", false)) h.controlled = true;
    if ((bool) x.getProperty ("parallel", false)) h.parallel = true;
    if ((bool) x.getProperty ("dreamy", false)) h.dreamy = true;
    // sa chaîne d'enregistrement : un préampli (MIC ou LINE), le compresseur des crêtes (1176 ou CL1B), la vitesse du rap moderne
    if (has ("preamp") && num ("preamp") > h.preamp)
    {
        h.preamp = juce::jlimit (0.0f, 1.0f, num ("preamp"));
        h.preampMic = x.getProperty ("preampType", "line").toString().equalsIgnoreCase ("mic");
        h.preampOf = a.name;
    }
    if (h.peakComp < 0 && has ("peakComp")) h.peakComp = x["peakComp"].toString().equalsIgnoreCase ("cl1b") ? 1 : 0;
    if ((bool) x.getProperty ("fast", false)) h.fast = true;
    // son accordage : 0,3 justesse discrète .. 1 l'effet robot (« sans autotune » dans la demande garde le dernier mot)
    if (has ("tune") && h.tune >= 0.0f && num ("tune") > h.tune) { h.tune = juce::jlimit (0.0f, 1.0f, num ("tune")); h.tuneOf = a.name; }
    if (a.genre.containsIgnoreCase ("rap") || a.genre.containsIgnoreCase ("drill") || a.genre.containsIgnoreCase ("trap")) h.rapStyle = true;
    // sa voix mesurée (une référence, souvent séparée par IA) : la cible du timbre, de la tenue et de l'espace
    if (h.voiceOf.isEmpty() && a.voice.isObject())
    {
        const auto& v = a.voice;
        h.voiceBands.fill (-100.0f);
        int n = 0;
        if (auto* o = v["bands"].getDynamicObject())
            for (int b = 0; b < kBands; ++b)
            {
                const auto key = juce::Identifier (juce::String (juce::roundToInt (kCentres[b])));
                if (kCentres[b] >= 160.0f && kCentres[b] <= 8000.0f && o->hasProperty (key)) { h.voiceBands[(size_t) b] = (float) (double) o->getProperty (key); ++n; }
            }
        if (n >= 8)
        {
            h.voiceOf = a.name;
            h.voiceDyn = (float) (double) v.getProperty ("dyn", 0.0);
            h.voiceRt60 = (float) (double) v.getProperty ("rt60", 0.0);
            h.voiceWet = (float) (double) v.getProperty ("wet", -60.0);
            h.voiceEcho = (float) (double) v.getProperty ("echo", 0.0);
        }
    }
    h.artists.addIfNotAlreadyThere (a.name);
    h.understood.addIfNotAlreadyThere (juce::String::fromUTF8 ("façon ") + a.name);
}


Hints parseHints (const juce::String& text)
{
    Hints h;
    // minuscules sans accents
    auto t = text.toLowerCase();
    const char* from[] { "é", "è", "ê", "ë", "à", "â", "î", "ï", "ô", "ù", "û", "ç", "’" };
    const char* to[] { "e", "e", "e", "e", "a", "a", "i", "i", "o", "u", "u", "c", "'" };
    for (int i = 0; i < 13; ++i) t = t.replace (u8 (from[i]), to[i]);
    juce::StringArray words;
    words.addTokens (t, " ,;:.!?/()+-\"'·", "");
    words.removeEmptyStrings();
    const juce::String joined = " " + words.joinIntoString (" ") + " ";
    juce::StringArray used;
    auto has = [&] (std::initializer_list<const char*> keys)
    {
        bool any = false;
        for (auto* k : keys)
            if (joined.contains (" " + juce::String (k) + " ") || (juce::String (k).length() > 5 && joined.contains (juce::String (k))))
            {
                any = true;
                juce::StringArray kw; kw.addTokens (k, " ", ""); used.addArray (kw);
            }
        return any;
    };
    auto note = [&h] (const char* s) { h.understood.addIfNotAlreadyThere (juce::String::fromUTF8 (s)); };

    // styles
    if (has ({ "chant", "chante", "chantee", "chanteur", "chanteuse", "sung", "singing", "singer", "melodique", "melodic", "ballade", "ballad", "toplines" })) { h.sung = 0.6f; note ("chant"); }
    // la chaîne d'enregistrement demandée : un préampli, le compresseur des crêtes, la vitesse
    if (has ({ "preampli", "preamp", "neve", "1073", "console" })) { h.preamp = juce::jmax (h.preamp, 0.5f); h.preampMic = true; note ("préampli"); }
    if (has ({ "cl1b", "tubetech", "tube-tech" })) { h.peakComp = 1; note ("CL1B"); }
    if (has ({ "1176", "fet" })) { h.peakComp = 0; note ("1176"); }
    if (has ({ "punchy", "agressif", "agressive", "nerveux", "nerveuse", "percutant", "percutante" })) { h.fast = true; note ("compression rapide"); }
    if (has ({ "rap", "hip hop", "hiphop", "drill", "trap", "rap fr", "boom bap" })) { h.rapStyle = true; h.comp += 0.5f; h.bright += 0.3f; h.air += 0.2f; if (h.reverb < 0) h.reverb = 0.12f; note ("rap"); }
    if (has ({ "rnb", "r&b", "r b", "soul", "neo soul" })) { h.warm += 0.4f; h.air += 0.3f; h.comp += 0.2f; h.reverb = juce::jmax (h.reverb, 0.3f); h.doubler += 0.3f; note ("R&B"); }
    if (has ({ "pop" })) { h.bright += 0.4f; h.air += 0.5f; h.comp += 0.5f; h.reverb = juce::jmax (h.reverb, 0.22f); note ("pop"); }
    if (has ({ "rock", "punk", "metal", "grunge" })) { h.sat += 0.4f; h.comp += 0.5f; h.satType = 3; note ("rock"); }
    if (has ({ "jazz", "acoustique", "acoustic", "folk" })) { h.comp -= 0.4f; h.warm += 0.4f; h.reverb = juce::jmax (h.reverb, 0.2f); h.reverbSize = 0.3f; note ("acoustique"); }
    if (has ({ "afro", "afrobeat", "dancehall", "reggaeton", "latin" })) { h.bright += 0.3f; h.delay = juce::jmax (h.delay, 0.3f); h.comp += 0.3f; note ("afro / latin"); }
    if (has ({ "ballade", "ballad", "piano voix" })) { h.reverb = juce::jmax (h.reverb, 0.4f); h.reverbSize = 0.8f; h.comp -= 0.1f; note ("ballade"); }
    if (has ({ "edm", "house", "techno", "dance", "electro" })) { h.bright += 0.4f; h.air += 0.4f; h.delay = juce::jmax (h.delay, 0.35f); h.comp += 0.4f; note ("électro"); }
    // l'accordage (TUNER) : refusé, naturel, ou l'effet robot
    if (has ({ "sans autotune", "pas d autotune", "sans tune", "sans auto tune", "no autotune", "sans accordage" })) { h.tune = -1.0f; note ("sans autotune"); }
    else if (has ({ "hard tune", "hardtune", "robot", "robotique", "t pain", "tpain", "autotune fort", "autotune a fond", "gros autotune", "full autotune",
                    "effet autotune", "autotune marque", "autotune prononce" })) { h.tune = 1.0f; note ("autotune marqué"); }
    else if (has ({ "autotune", "auto tune", "tune", "tuner", "accorde", "accordee", "justesse", "pitch", "pitch correction" }))
    { h.tune = juce::jmax (h.tune, 0.45f); note ("autotune"); }
    // les nettoyages demandés en toutes lettres
    if (has ({ "clic", "clics", "click", "clicks", "bruits de bouche", "bruit de bouche", "bouche", "salive", "mouth", "declick", "de click" })) { h.cleanClicks = true; note ("bruits de bouche"); }
    if (has ({ "respiration", "respirations", "breath", "breaths", "debreath", "de breath", "inspirations" })) { h.cleanBreath = true; note ("respirations"); }
    if (has ({ "dereverb", "de reverb", "enlever la reverb", "retirer la reverb", "sans la piece", "chambre", "salle de bain", "cabine", "home studio", "piece qui sonne", "ca resonne" }))
    { h.cleanRoom = true; note ("la pièce retirée"); }

    // couleurs
    if (has ({ "aere", "aeree", "aerien", "aerienne", "air", "airy", "brillant", "brillante", "bright", "clair", "claire", "cristal", "cristalline", "crisp" })) { h.air += 0.6f; h.bright += 0.4f; note ("aérée"); }
    if (has ({ "sombre", "dark", "fonce", "grave", "lourd", "lourde" })) { h.dark += 0.6f; note ("sombre"); }
    if (has ({ "chaud", "chaude", "warm", "doux", "douce", "mellow", "velours", "smooth", "rond", "ronde" })) { h.warm += 0.6f; note ("chaude"); }
    if (has ({ "agressif", "agressive", "aggressive", "punch", "punchy", "en avant", "upfront", "devant", "serre", "serree", "compresse", "compressee", "tight", "dense" })) { h.comp += 0.7f; note ("en avant"); }
    if (has ({ "naturel", "naturelle", "natural", "leger", "legere", "light", "dynamique", "ouvert", "ouverte" })) { h.comp -= 0.6f; note ("naturelle"); }
    // espace
    if (has ({ "reverb", "reverbe", "reverberation", "espace", "spacieux", "spacieuse", "hall", "cathedrale", "cathedral", "atmospherique", "atmosphere", "ambiance", "ambient" })) { h.reverb = juce::jmax (h.reverb, 0.45f); h.reverbExplicit = true; h.reverbSize = juce::jmax (h.reverbSize, 0.6f); note ("réverbe"); }
    if (has ({ "planant", "planante", "dreamy", "reve", "reveur", "onirique", "ethere", "etheree", "ethereal", "nuage", "cloud" })) { h.reverb = juce::jmax (h.reverb, 0.55f); h.reverbExplicit = true; h.reverbSize = 0.9f; h.delay = juce::jmax (h.delay, 0.3f); note ("planante"); }
    if (has ({ "shimmer", "celeste", "angelique", "angelic" })) { h.reverb = juce::jmax (h.reverb, 0.5f); h.reverbSize = 0.9f; h.dreamy = true; note ("shimmer"); }
    if (has ({ "sec", "seche", "dry", "close", "intime", "intimate", "proche" })) { h.reverb = 0.0f; h.delay = 0.0f; h.reverbExplicit = true; note ("sèche"); }
    if (has ({ "delay", "echo", "echos", "ping pong" })) { h.delay = juce::jmax (h.delay, 0.5f); note ("delay"); }
    if (has ({ "longue", "long", "grande", "large reverb", "big" })) h.reverbSize = juce::jmax (h.reverbSize, 0.8f);
    if (has ({ "courte", "short", "petite", "room", "piece" })) h.reverbSize = 0.15f;
    // texture
    if (has ({ "sature", "saturee", "saturation", "grain", "granuleux", "distordu", "distorsion", "distortion", "crunch", "gras", "grasse", "sale", "dirty" })) { h.sat += 0.6f; note ("saturée"); }
    if (has ({ "tape", "bande", "cassette", "lofi", "lo fi", "vintage", "retro", "old school" })) { h.sat += 0.4f; h.satType = 2; h.dark += 0.2f; note ("bande / vintage"); }
    if (has ({ "tube", "lampe", "lampes", "valve" })) { h.sat += 0.4f; h.satType = 0; note ("lampe"); }
    if (has ({ "analog", "analogique", "console" })) { h.sat += 0.3f; h.satType = 1; note ("analogique"); }
    // tenue
    if (has ({ "multiband", "multibande", "multi band", "multi bande", "multibandes" })) { h.controlled = true; note ("multiband"); }
    if (has ({ "controle", "controlee", "maitrise", "maitrisee", "carre", "carree", "propre", "pro", "professionnel", "radio", "commercial", "commerciale", "stable", "controlled", "polished", "clean" })) { h.controlled = true; h.comp += 0.2f; note ("bien contrôlée"); }
    if (has ({ "parallele", "parallel", "densite", "epais", "epaisse", "punch", "punchy", "gros", "grosse", "massive", "massif", "fat" })) { h.parallel = true; note ("dense"); }
    // effets
    if (has ({ "telephone", "phone", "radio", "megaphone", "talkie" })) { h.phone = 0.7f; note ("téléphone"); }
    if (has ({ "double", "doublee", "doubled", "doubleur", "doubler", "stereo", "large", "wide", "epaisse", "thick" })) { h.doubler = juce::jmax (h.doubler, 0.6f); note ("doublée"); }
    if (has ({ "chorus" })) { h.chorus = 0.6f; note ("chorus"); }
    if (has ({ "flanger", "flange", "avion", "jet" })) { h.flanger = 0.6f; note ("flanger"); }

    // les artistes que le cerveau connaît : leur façon de mixer la voix
    if (const auto brain = brainArtists())
        for (const auto& a : *brain)
            for (const auto& alias : a.aliases)
                if (joined.contains (" " + alias + " "))
                {
                    applyArtist (h, a);
                    juce::StringArray kw; kw.addTokens (alias, " ", ""); used.addArray (kw);
                    break;
                }

    // les mots non compris : souvent des noms d'artistes
    static const juce::StringArray stop { "de", "la", "le", "les", "du", "des", "voix", "voice", "vocal", "vocals", "type", "facon", "style", "comme", "like",
        "un", "une", "et", "and", "avec", "with", "mix", "mixe", "mixee", "very", "tres", "plus", "more", "a", "en", "au", "aux", "sur", "pour", "the", "of",
        "sound", "son", "ma", "mon", "ta", "ton", "sa", "ses", "mes", "tes", "je", "veux", "want", "qui", "que", "dans", "in", "on", "my", "your", "un", "peu",
        "bit", "little", "fa", "ex", "fr", "us", "uk", "bien", "well", "compression", "compresseur", "voix", "chaine", "chain", "bon", "bonne", "beaucoup", "lot", "pas", "trop", "too", "not", "no", "sans", "without" };
    for (auto& w : words)
        if (w.length() >= 3 && ! stop.contains (w) && ! used.contains (w) && ! w.containsOnly ("0123456789"))
            h.unknown.addIfNotAlreadyThere (w);

    h.bright = juce::jlimit (0.0f, 1.0f, h.bright);
    h.air = juce::jlimit (0.0f, 1.0f, h.air);
    h.warm = juce::jlimit (0.0f, 1.0f, h.warm);
    h.dark = juce::jlimit (0.0f, 1.0f, h.dark);
    h.comp = juce::jlimit (-1.0f, 1.0f, h.comp);
    h.sat = juce::jlimit (0.0f, 1.0f, h.sat);
    // ONR Neural Learning : ce que les utilisateurs retouchent sur toutes les demandes (décalages bornés, appris sur le serveur)
    if (const auto c = brainCorrections()["global"]; c.isObject())
    {
        auto num = [&c] (const char* k) { return juce::jlimit (-0.25f, 0.25f, (float) (double) c.getProperty (juce::Identifier (k), 0.0)); };
        h.bright = juce::jmax (0.0f, h.bright + num ("bright")); h.air = juce::jmax (0.0f, h.air + num ("air"));
        h.warm = juce::jmax (0.0f, h.warm + num ("warm")); h.dark = juce::jmax (0.0f, h.dark + num ("dark"));
        h.comp = juce::jlimit (-1.0f, 1.0f, h.comp + num ("comp")); h.sat = juce::jmax (0.0f, h.sat + num ("sat"));
        h.reverbBias = num ("reverb"); h.delayBias = num ("delay");
    }
    return h;
}

//==============================================================================
namespace
{
/*  La cible par défaut : une voix bien mixée (la « VOIX MIX » de référence de Nov, lissée pour ne garder que l'équilibre,
    pas les formants propres à cette voix) : graves coupés sous 125 Hz, corps 160 .. 500 Hz, médiums neutres, présence 2 .. 5 kHz
    tenue, beaucoup d'air 6 .. 10 kHz. Même normalisation que Profile::bands (moyenne 200 Hz .. 5 kHz à 0). */
constexpr float kDefaultTarget[kBands] { -40, -36, -32, -28, -24, -21, -18, -11, -5, 1.5f, 3, 3, 3, 3, 2.5f, 1.5f, 1, 0.5f, 0, -1,
                                         -2, -2.5f, -3, -4, -4, -3, -2.5f, -3.5f, -6, -12, -20 };
// ce que mesure la même voix : très tenue (2,8 dB de dynamique), phrases fortes à -18,6 dBFS, réverbe plate ~1,9 s présente
constexpr float kDefaultDyn = 3.0f, kDefaultLoud = -18.5f, kDefaultSib = -6.5f;

void put (Setting& s, const char* id, float v) { s.params.emplace_back (juce::String (id), v); }
}

Plan buildPlan (const Profile& src, const Profile& tgtIn, const Hints& hIn)
{
    Plan plan;
    auto& rep = plan.report;

    /*  LE RÔLE de la voix (choisi dans VOCALBOX) : la même demande, mais une autre place dans le morceau.
        - BACKS : derrière le lead, plus sombres, plus tenus, plus loin (réverbe), doublés, 4 dB plus bas, accordés serré.
        - ADLIBS : à côté (panoramique), filtrés comme un téléphone léger, plus d'écho, très tenus, 3 dB plus bas.
        - DOUBLES : collés au lead, très compressés, moins d'espace (l'espace est celui du lead), 6 dB plus bas, accordés serré. */
    Hints h = hIn;
    float roleOut = 0.0f, rolePan = 0.0f;
    static const char* roleName[4] { "LEAD", "BACKS", "ADLIBS", "DOUBLES" };
    switch (h.role)
    {
        case 1: h.dark += 0.2f; h.comp += 0.3f; h.reverb = juce::jmax (h.reverb, 0.0f) + 0.15f; h.doubler = juce::jmax (h.doubler, 0.55f);
                h.preamp = 0.0f; if (h.tune >= 0.0f && h.tune < 0.5f) h.tune = 0.5f; roleOut = -4.0f; break;
        case 2: h.phone = juce::jmax (h.phone, 0.3f); h.delay = juce::jmax (h.delay, 0.0f) + 0.35f; h.reverb = juce::jmax (h.reverb, 0.0f) + 0.1f;
                h.comp += 0.4f; h.preamp = 0.0f; roleOut = -3.0f; rolePan = 30.0f; break;
        case 3: h.comp += 0.5f; h.dark += 0.1f; h.reverb = h.reverb > 0.0f ? h.reverb * 0.5f : 0.05f; h.delay = h.delay > 0.0f ? h.delay * 0.4f : 0.0f;
                h.doubler = 0.0f; h.preamp = 0.0f; if (h.tune >= 0.0f && h.tune < 0.6f) h.tune = 0.6f; roleOut = -6.0f; break;
        default: break;
    }
    h.dark = juce::jlimit (0.0f, 1.0f, h.dark); h.comp = juce::jlimit (-1.0f, 1.0f, h.comp);
    if (h.role > 0)
        rep.add (u8 ("Rôle : ") + roleName[juce::jlimit (0, 3, h.role)]
                 + (h.role == 1 ? u8 (" (derrière le lead : plus sombres, plus loin, doublés, −4 dB)")
                    : h.role == 2 ? u8 (" (à côté : filtrés, plus d'écho, panoramique à droite, −3 dB)")
                                  : u8 (" (collés au lead : très tenus, peu d'espace, −6 dB)")));
    // les macros repartent de zéro : la chaîne montée est le point de départ, PRÉSENCE, CHALEUR, TENUE et ESPACE l'affinent
    for (auto* id : { ids::macroPresence, ids::macroWarmth, ids::macroHold, ids::macroSpace }) plan.globals.push_back ({ id, 0.0f });
    plan.globals.push_back ({ ids::output, roleOut });
    plan.globals.push_back ({ ids::outPan, rolePan });

    // sans référence glissée, la voix mesurée de l'artiste demandé (le cerveau) sert de référence : son timbre entre 160 Hz
    // et 8 kHz, à moitié (une part de l'écart tient au timbre naturel du chanteur, pas au mix ; et hors de cette zone, la
    // séparation par IA a pu couper des fréquences), sa tenue et son espace. Son écho donne la quantité de delay, pas sa
    // durée (elle dépend du tempo de son morceau, le delay reste calé sur celui du projet).
    Profile voiceRef;
    if (! tgtIn.valid && h.voiceOf.isNotEmpty())
    {
        voiceRef.valid = true;
        for (int b = 0; b < kBands; ++b)
            voiceRef.bands[(size_t) b] = h.voiceBands[(size_t) b] > -90.0f ? 0.5f * h.voiceBands[(size_t) b] + 0.5f * kDefaultTarget[b] : kDefaultTarget[b];
        voiceRef.dynRange = h.voiceDyn > 0.0f ? juce::jlimit (2.5f, 9.0f, h.voiceDyn) : kDefaultDyn;
        voiceRef.loudDb = kDefaultLoud;
        voiceRef.sibilance = kDefaultSib;
        voiceRef.rt60 = h.voiceRt60;
        voiceRef.wetDb = h.voiceWet - 5.0f;   // la séparation laisse un peu de la réverbe de la prod autour de la voix
        voiceRef.echoAmount = h.voiceEcho;
        rep.add (u8 ("Voix de ") + h.voiceOf + u8 (" mesurée (cerveau) : son timbre, sa tenue et son espace servent de cible"));
    }
    const Profile& tgt = tgtIn.valid ? tgtIn : voiceRef;
    const bool tgtDyn = tgt.valid && ! tgt.fromMix;   // dynamique, niveau et sifflantes de la référence : voix vraiment seule

    // ---- la cible : le timbre de la référence, sinon celui d'une voix bien mixée ; puis les couleurs demandées
    std::array<float, kBands> target {};
    for (int b = 0; b < kBands; ++b)
    {
        float v = kDefaultTarget[b];
        if (tgt.valid) v = ! tgt.fromMix ? 0.8f * tgt.bands[(size_t) b] + 0.2f * kDefaultTarget[b]
                                         : (b < 10 ? kDefaultTarget[b] : 0.7f * tgt.bands[(size_t) b] + 0.3f * kDefaultTarget[b]);
        const float f = kCentres[b];
        const float oct2k = juce::jmax (0.0f, std::log2 (f / 2000.0f)), oct8k = juce::jmax (0.0f, std::log2 (f / 8000.0f) + 1.0f);
        v += h.bright * 1.2f * juce::jmin (2.0f, oct2k) + h.air * 2.0f * juce::jmin (1.5f, oct8k) - h.dark * 1.5f * juce::jmin (2.0f, oct2k);
        if (f >= 150.0f && f <= 450.0f) v += 1.5f * h.warm;
        target[(size_t) b] = v;
    }

    // ---- le niveau : la voix entre dans la chaîne au niveau d'une voix mixée (tous les seuils en dépendent), chaque module
    //      compense sa réduction : elle sort « prête », en avant, quel que soit le niveau d'enregistrement
    const float tgtLoud = tgtDyn ? juce::jlimit (-24.0f, -12.0f, tgt.loudDb) : kDefaultLoud;
    plan.inputDb = src.valid ? juce::jlimit (-24.0f, 24.0f, std::round (tgtLoud - src.loudDb)) : 0.0f;
    const float L = src.valid ? src.loudDb + plan.inputDb : tgtLoud;          // phrases fortes, à l'entrée de la chaîne
    const float P = src.valid ? src.peak50Db + plan.inputDb : tgtLoud + 2.0f; // crêtes de 50 ms
    if (src.valid && std::abs (plan.inputDb) >= 1.0f)
        rep.add (u8 ("Niveau : ") + dbText (plan.inputDb) + u8 (" à l'entrée, la voix travaille et sort au niveau d'une voix mixée"));

    // ---- 1. NETTOYAGE en tête : souffle, bruit entre les phrases, plosives
    if (src.valid || h.cleanClicks || h.cleanBreath || h.cleanRoom)
    {
        const float floorIn = src.noiseFloorDb + plan.inputDb, gap = src.loudDb - src.noiseFloorDb;
        const bool steady = src.valid && src.floorSpreadDb < 6.0f;   // un vrai souffle, pas une queue de réverbe
        // DE-CLICK en tout premier (un clic fausse tout ce qui suit : compresseurs, de-esser) : seulement s'il en a entendu
        // (6 par minute au moins, les détecteurs mêmes du module) ou si c'est demandé
        if (h.cleanClicks || (src.valid && src.clicksPerMin >= 6.0f))
        {
            Setting s { modules::declick, 0, {} };
            const float sens = src.clicksPerMin >= 25.0f ? 60.0f : 50.0f;
            put (s, ids::dclkSens, sens);
            put (s, ids::dclkAmount, src.clicksPerMin >= 25.0f ? 80.0f : 70.0f);
            plan.modules.push_back (s);
            rep.add (src.valid && src.clicksPerMin >= 1.0f
                       ? u8 ("Bruits de bouche : ") + juce::String (juce::roundToInt (src.clicksPerMin)) + u8 (" clics par minute entendus, DE-CLICK (les consonnes ne sont pas touchées)")
                       : u8 ("DE-CLICK demandé : les clics de bouche retirés"));
        }
        if (steady && gap < 62.0f && src.noiseFloorDb > -78.0f)
        {
            Setting s { modules::noise, 0, {} };
            const float amt = juce::jlimit (15.0f, 70.0f, 15.0f + (62.0f - gap) * 2.2f);
            put (s, ids::nsAmount, std::round (amt));
            plan.modules.push_back (s);
            rep.add (u8 ("Fond à ") + juce::String (juce::roundToInt (gap)) + u8 (" dB sous la voix : noise suppressor ") + juce::String (juce::roundToInt (amt)) + " %");
        }
        // DE-REVERB : la pièce de l'enregistrement (chambre, salon) entendue après les phrases, ou demandé. Pas sur une voix sèche
        // (une note relâchée doucement ressemble à une petite pièce) : une vraie queue de 0,5 à 2,5 s (au-delà, c'est un effet
        // voulu, pas une pièce), qui a déjà baissé de 6 dB 120 ms après la phrase mais reste au-dessus de −20 dB, pas un souffle stable
        const bool roomHeard = src.valid && ! steady && src.rt60 >= 0.5f && src.rt60 <= 2.5f && src.wetDb > -20.0f && src.wetDb < -6.0f;
        if (h.cleanRoom || roomHeard)
        {
            Setting s { modules::dereverb, 0, {} };
            const float amt = roomHeard ? juce::jlimit (30.0f, 75.0f, 35.0f + (src.rt60 - 0.35f) * 50.0f + (src.wetDb + 22.0f) * 1.5f) : 50.0f;
            put (s, ids::drvAmount, std::round (amt));
            put (s, ids::drvAuto, 1.0f);
            if (src.valid && src.rt60 > 0.1f) put (s, ids::drvTail, juce::jlimit (0.1f, 3.0f, src.rt60));
            plan.modules.push_back (s);
            rep.add (roomHeard ? u8 ("La pièce s'entend autour de ta voix (queue de ") + juce::String (src.rt60, 2) + u8 (" s) : DE-REVERB ")
                                   + juce::String (juce::roundToInt (amt)) + u8 (" %, l'espace viendra de la réverbe du mix")
                               : u8 ("DE-REVERB demandé : la pièce retirée"));
        }
        // DE-BREATH : des respirations fortes (au-dessus de −32 dB sous les phrases), au moins 3 par minute, ou demandé. Baissées,
        // pas coupées : moins sur une voix chantée (elles font partie du phrasé), plus sur le rap moderne
        const float sungNow = juce::jlimit (0.0f, 1.0f, (src.valid ? src.sung : 0.0f) + h.sung);
        if (h.cleanBreath || (src.valid && src.breathsPerMin >= 3.0f && src.breathDb > -32.0f))
        {
            Setting s { modules::debreath, 0, {} };
            float red = src.valid && src.breathDb > -60.0f ? juce::jlimit (6.0f, 15.0f, (src.breathDb + 38.0f) * 0.7f) : 10.0f;
            red *= 1.0f - 0.35f * sungNow;
            if (h.rapStyle || h.fast) red += 2.0f;
            red = std::round (juce::jlimit (5.0f, 16.0f, red));
            put (s, ids::dbrReduction, red);
            plan.modules.push_back (s);
            rep.add (src.valid && src.breathsPerMin >= 1.0f
                       ? u8 ("Respirations : ") + juce::String (juce::roundToInt (src.breathsPerMin)) + u8 (" par minute, à ") + dbText (src.breathDb)
                             + u8 (" sous les phrases : DE-BREATH −") + juce::String (juce::roundToInt (red)) + u8 (" dB (baissées, pas coupées)")
                       : u8 ("DE-BREATH demandé : respirations baissées de ") + juce::String (juce::roundToInt (red)) + " dB");
        }
        if (steady && gap < 52.0f && gap > 16.0f)
        {
            Setting s { modules::gate, 0, {} };
            put (s, ids::gateThr, juce::jlimit (-75.0f, -25.0f, floorIn + 0.45f * gap));
            put (s, ids::gateRange, gap < 35.0f ? 20.0f : 14.0f);
            plan.modules.push_back (s);
            rep.add (u8 ("Bruit entre les phrases : gate (−") + juce::String (gap < 35.0f ? 20 : 14) + " dB)");
        }
        if (src.plosivesPerMin > 1.5f)
        {
            Setting s { modules::plosive, 0, {} };
            put (s, ids::plosive, juce::jlimit (30.0f, 70.0f, 30.0f + src.plosivesPerMin * 2.5f));
            plan.modules.push_back (s);
            rep.add (u8 ("Des P et des B qui claquent : plosives"));
        }
    }

    // ---- 2. PREAMP N73 en tête de chaîne : pas pour tout le monde. Pour les artistes qui en ont un dans leur chaîne
    //      d'enregistrement (le cerveau : « preamp »), quand il est demandé (« préampli », « Neve », « 1073 »), ou quand il est
    //      utile : voix fine, ou chaleur demandée. MIC (le grain d'une prise micro, plus marqué) ou LINE (plus discret).
    const bool thin = src.valid && (src.bands[11] + src.bands[12] + src.bands[13]) / 3.0f < (target[11] + target[12] + target[13]) / 3.0f - 3.0f;
    const bool usePre = h.preamp > 0.0f || thin || h.warm > 0.4f;
    int satInstance = 0;
    if (usePre)
    {
        const float a = juce::jlimit (0.2f, 1.0f, h.preamp > 0.0f ? h.preamp : (thin ? 0.45f : 0.35f) + 0.3f * h.warm);
        const bool mic = h.preampMic;
        const float gdb = mic ? 22.0f + 8.0f * a : 9.0f * a;   // MIC +22 .. +30 dB ; LINE 0 .. +9 dB
        Setting s { modules::preamp, 0, {} };
        put (s, ids::preampMic, mic ? 1.0f : 0.0f);
        put (s, ids::preampGain, std::round (gdb * 10.0f) / 10.0f);
        plan.modules.insert (plan.modules.begin(), s);   // le tout premier de la chaîne, comme à l'enregistrement
        const int db = juce::roundToInt (gdb);
        rep.add (u8 ("Préampli N73 en tête (") + (mic ? "MIC +" : "LINE ") + juce::String (db) + u8 (" dB) : ")
                 + (h.preamp > 0.0f ? (h.preampOf.isNotEmpty() ? u8 ("comme dans la chaîne d'enregistrement de ") + h.preampOf : u8 ("demandé"))
                                    : thin ? u8 ("de la densité pour une voix un peu fine") : u8 ("de la chaleur")));
    }

    /*  ---- 2b. TUNER, avant l'EQ et les compresseurs (on accorde la voix telle qu'elle a été chantée). Seulement :
        - quand c'est demandé (« autotune », « robot »), ou que l'artiste demandé en a un dans son son (le cerveau : « tune ») ;
        - sur une voix chantée qui chante à côté (plus de 18 cents d'écart moyen sur ses notes tenues) : une justesse discrète.
        Jamais si « sans autotune ». La tonalité : celle des notes tenues de la prise si elle est sûre, sinon celle qu'une autre
        VOCALBOX du projet a trouvée (le LEAD), sinon chromatique (toutes les notes : aucune fausse note possible).
        Le réglage suit la quantité : 0,3 justesse naturelle (Retune lent, Humanize, Flex-Tune), 0,6..0,85 l'autotune moderne
        (Retune 5 .. 15 ms), au-delà l'effet robot (HARD TUNE). */
    {
        const float sungT = juce::jlimit (0.0f, 1.0f, (src.valid ? src.sung : 0.0f) + h.sung);
        float a = h.tune;
        const bool offPitch = src.valid && sungT >= 0.5f && src.pitchDevCents > 18.0f;
        if (a == 0.0f && offPitch) a = 0.3f;
        if (a > 0.0f)
        {
            Setting s { modules::tuner, 0, {} };
            int key = -1;
            juce::String keyWhy;
            if (src.valid && src.keyRoot >= 0 && src.keyConfidence >= 0.35f)
            {
                key = src.keyRoot + (src.keyMinor ? 12 : 0);
                keyWhy = u8 ("trouvée sur tes notes tenues");
            }
            else if (h.sharedKey >= 0)
            {
                key = h.sharedKey;
                keyWhy = u8 ("celle du lead (une autre VOCALBOX du projet)");
            }
            static const char* names[12] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            if (key >= 0)
            {
                put (s, ids::tuneKey, (float) (key % 12));
                put (s, ids::tuneScale, key >= 12 ? 2.0f : 1.0f);   // MINOR ou MAJOR (l'ordre des gammes d'ONIRIQUE TUNER)
                plan.keyFound = key;
                plan.keyConfidence = src.valid ? src.keyConfidence : 0.0f;
            }
            else put (s, ids::tuneScale, 0.0f);
            // le registre : la hauteur moyenne des notes tenues
            if (src.valid && src.noteMidi > 0.0f) put (s, ids::tuneVoice, src.noteMidi >= 64.0f ? 0.0f : src.noteMidi < 50.0f ? 2.0f : 1.0f);
            juce::String how;
            if (a >= 0.9f)
            {
                put (s, ids::tuneHard, 1.0f); put (s, ids::tuneRetune, 0.0f); put (s, ids::tuneHumanize, 0.0f);
                how = u8 ("l'effet robot (HARD TUNE)");
            }
            else if (a >= 0.6f)
            {
                const float rt = std::round (15.0f - 25.0f * (a - 0.6f));   // 15 .. 8 ms
                put (s, ids::tuneRetune, rt); put (s, ids::tuneHumanize, 15.0f);
                how = u8 ("l'autotune moderne (Retune ") + juce::String (juce::roundToInt (rt)) + " ms)";
            }
            else
            {
                const float rt = std::round (60.0f - 60.0f * a);              // 0,3 : 42 ms ; 0,45 : 33 ms
                put (s, ids::tuneRetune, rt); put (s, ids::tuneHumanize, 50.0f); put (s, ids::tuneFlex, a < 0.4f ? 30.0f : 15.0f);
                how = u8 ("une justesse naturelle (Retune ") + juce::String (juce::roundToInt (rt)) + u8 (" ms, Humanize, Flex-Tune : les inflexions restent)");
            }
            plan.modules.push_back (s);
            const juce::String keyText = key >= 0 ? juce::String (names[key % 12]) + (key >= 12 ? u8 (" mineur") : u8 (" majeur")) + " (" + keyWhy + ")"
                                                  : u8 ("chromatique (tonalité pas assez sûre : toutes les notes)");
            rep.add (u8 ("TUNER en ") + keyText + " : " + how
                     + (h.tuneOf.isNotEmpty() ? u8 (", comme ") + h.tuneOf
                        : offPitch && h.tune == 0.0f ? u8 (" ; ta voix s'écarte de ") + juce::String (juce::roundToInt (src.pitchDevCents)) + u8 (" cents en moyenne")
                                                     : juce::String()));
        }
        else if (src.valid && src.keyRoot >= 0 && src.keyConfidence >= 0.35f)
        {
            plan.keyFound = src.keyRoot + (src.keyMinor ? 12 : 0);   // partagée quand même (les BACKS pourront s'accorder dessus)
            plan.keyConfidence = src.keyConfidence;
        }
    }

    // ---- 3. EQ : l'écart entre la prise et la cible (±4 dB lissés), présence pour la mettre en avant
    float highBoost = 0.0f, mudCut = 0.0f, mudExcess = 0.0f, compGr = 0.0f;
    bool multiUsed = false;
    // chant ou rap : d'après la prise (notes tenues), et ce que la demande en dit
    const float sung = juce::jlimit (0.0f, 1.0f, (src.valid ? src.sung : 0.0f) + h.sung);
    {
        Setting s { modules::peq, 0, {} };
        put (s, ids::peqPower, 1.0f);
        std::array<float, kBands> diff {};
        if (src.valid)
        {
            std::array<float, kBands> raw {};
            for (int b = 7; b <= 29; ++b) raw[(size_t) b] = target[(size_t) b] - src.bands[(size_t) b];
            for (int b = 8; b <= 28; ++b) diff[(size_t) b] = juce::jlimit (-4.0f, 4.0f, 0.65f * (0.25f * raw[(size_t) b - 1] + 0.5f * raw[(size_t) b] + 0.25f * raw[(size_t) b + 1]));
        }
        else
            for (int b = 8; b <= 28; ++b) diff[(size_t) b] = juce::jlimit (-4.0f, 4.0f, target[(size_t) b] - kDefaultTarget[b]);
        auto avg = [&diff] (int a, int b) { float v = 0.0f; for (int i = a; i <= b; ++i) v += diff[(size_t) i]; return v / (float) (b - a + 1); };
        float cut = 90.0f;
        if (src.valid && src.bands[7] > target[7] + 3.0f) cut = 110.0f;
        put (s, ids::peqOn[0], 1.0f); put (s, ids::peqType[0], 3.0f); put (s, ids::peqFreq[0], cut); put (s, ids::peqSlope[0], 1.0f); put (s, ids::peqQ[0], 0.71f);
        const int centre[6] { 9, 12, 15, 18, 21, 24 };
        juce::StringArray moves;
        // six bandes larges : l'écart de timbre lissé
        struct Band { float f, g, q; bool used; };
        std::array<Band, 6> bands {};
        for (int i = 0; i < 6; ++i)
        {
            float g = avg (centre[i] - 1, centre[i] + 1);
            if (i == 4 || i == 5) g += 0.6f + 0.8f * juce::jmax (0.0f, h.comp);   // présence : la voix passe devant la prod
            g = juce::jlimit (-4.0f, 4.0f, g);
            bands[(size_t) i] = { kCentres[centre[i]], g, 1.1f, std::abs (g) >= 0.7f };
        }
        auto octs = [] (float a, float b) { return std::abs (std::log2 (a / b)); };

        // LA BOUE (200 .. 500 Hz) : mesurée sur la prise ; coupée là où elle est la plus forte, selon le style : franche sur une
        // voix rap qui doit passer devant, plus douce quand de la chaleur est demandée ou sur une voix chantée qui doit garder son corps
        float mudFreq = 0.0f;
        if (src.valid)
        {
            float ex = 0.0f; int at = 11; float worst = -99.0f;
            for (int b = 10; b <= 14; ++b)
            {
                const float d = src.bands[(size_t) b] - target[(size_t) b];
                ex += d / 5.0f;
                if (d > worst) { worst = d; at = b; }
            }
            mudExcess = juce::jmax (0.0f, ex);
            if (ex > 1.5f)
            {
                float k = 1.0f;
                if (h.warm > 0.3f) k *= 0.6f;
                k *= 1.0f - 0.25f * sung;
                if (h.comp > 0.3f) k *= 1.15f;
                const float g = -juce::jlimit (1.5f, 6.0f, 0.8f * ex * k);
                mudFreq = kCentres[at]; mudCut = g;
                // à la place de la bande large la plus proche (pas de coupe empilée sur la même zone)
                int slot = 0;
                for (int i = 1; i < 3; ++i) if (octs (bands[(size_t) i].f, mudFreq) < octs (bands[(size_t) slot].f, mudFreq)) slot = i;
                bands[(size_t) slot] = { mudFreq, g, 1.4f, true };
                // beaucoup de boue : on ne remonte pas le bas juste en dessous
                if (g < -3.0f) for (auto& b : bands) if (b.f < mudFreq && b.g > 0.0f) b.g = 0.0f, b.used = false;
                for (int i = 0; i < 3; ++i) if (i != slot && octs (bands[(size_t) i].f, mudFreq) < 0.7f && bands[(size_t) i].g < 0.0f) bands[(size_t) i].used = false;
                rep.add (u8 ("Mud mesuré (+") + juce::String (ex, 1) + u8 (" dB) : coupe ") + dbText (g) + u8 (" à ") + hzText (mudFreq));
            }
        }

        // LES RÉSONANCES : un pic étroit qui dépasse de la voix (dureté vers 2 .. 5 kHz, nasalité, pièce) est coupé à sa fréquence,
        // serré, et sans excès : environ 70 % du dépassement, 5 dB au plus dans la zone dure, 4 ailleurs, 3 au-dessus de 6 kHz
        // (le de-esser s'occupe des S). Deux au plus, dans les bandes restées libres.
        juce::StringArray notches;
        if (src.valid)
            for (int r = 0, done = 0; r < src.numRes && done < 2; ++r)
            {
                const float f = src.resFreq[(size_t) r], ex = src.resDb[(size_t) r];
                const bool harsh = f >= 1500.0f && f <= 6000.0f;
                if (ex < (harsh ? 3.0f : 4.0f) || (mudFreq > 0.0f && octs (f, mudFreq) < 0.4f)) continue;
                int slot = -1;
                for (int i = 0; i < 6; ++i) if (! bands[(size_t) i].used) { slot = i; break; }
                if (slot < 0)   // plus de bande libre : la bande large la plus discrète (moins de 1,5 dB) laisse sa place
                    for (int i = 0; i < 6; ++i)
                        if (bands[(size_t) i].q < 2.0f && bands[(size_t) i].f != mudFreq && std::abs (bands[(size_t) i].g) < 1.5f
                            && (slot < 0 || std::abs (bands[(size_t) i].g) < std::abs (bands[(size_t) slot].g))) slot = i;
                if (slot < 0) break;
                const float cap = f > 6000.0f ? 3.0f : harsh ? 5.0f : 4.0f;
                const float g = -juce::jlimit (1.5f, cap, 0.7f * ex);
                // une bande large qui coupe déjà autour : moitié moins, pour ne pas creuser la voix
                for (auto& b : bands) if (b.used && b.q < 2.0f && octs (b.f, f) < 0.4f) b.g *= 0.5f;   // et une présence poussée juste là : moitié moins
                bands[(size_t) slot] = { f, g, juce::jlimit (3.0f, 6.0f, src.resQ[(size_t) r]), true };
                notches.add (hzText (f) + " " + dbText (g));
                ++done;
            }
        if (! notches.isEmpty())
            rep.add (u8 ("Résonances coupées proprement : ") + notches.joinIntoString (u8 (" · ")));

        for (int i = 0; i < 6; ++i)
        {
            const auto& b = bands[(size_t) i];
            const int bnd = i + 1;
            put (s, ids::peqOn[bnd], b.used ? 1.0f : 0.0f); put (s, ids::peqType[bnd], 0.0f); put (s, ids::peqFreq[bnd], std::round (b.f));
            put (s, ids::peqGain[bnd], std::round (b.g * 10.0f) / 10.0f); put (s, ids::peqQ[bnd], std::round (b.q * 100.0f) / 100.0f);
            if (b.used && b.q < 2.0f && b.f != mudFreq) moves.add (dbText (b.g) + " " + hzText (b.f));
            if (b.used && b.g > 0.0f && b.f >= 2000.0f) highBoost = juce::jmax (highBoost, b.g);
        }
        const float shelf = juce::jlimit (-4.0f, 4.0f, avg (26, 28) + 0.8f);   // un peu d'air en plus : une voix mixée brille
        put (s, ids::peqOn[7], std::abs (shelf) >= 0.7f ? 1.0f : 0.0f);
        put (s, ids::peqType[7], 2.0f); put (s, ids::peqFreq[7], 10000.0f); put (s, ids::peqGain[7], std::round (shelf * 10.0f) / 10.0f); put (s, ids::peqQ[7], 0.71f);
        if (std::abs (shelf) >= 0.7f) moves.add ("air " + dbText (shelf));
        highBoost = juce::jmax (highBoost, shelf);
        plan.modules.push_back (s);
        rep.add ("EQ : coupe-bas " + hzText (cut) + (moves.isEmpty() ? juce::String() : u8 (" · ") + moves.joinIntoString (u8 (" · "))));
    }

    // ---- 4. 1176 puis 5. LA-2A : la dynamique de la prise ramenée vers celle d'une voix mixée (très tenue)
    const float srcDr = src.valid ? src.dynRange : 12.0f;
    const float tgtDr = juce::jlimit (2.0f, 10.0f, (tgtDyn ? tgt.dynRange : kDefaultDyn) - 0.8f * h.comp);
    const float need = juce::jlimit (0.0f, 18.0f, srcDr - tgtDr);
    {
        const float grFet = juce::jlimit (3.0f, 10.0f, 3.0f + 0.5f * need + 1.5f * juce::jmax (0.0f, h.comp));
        // les crêtes : 1176 (FET, mordant, rap) ou CL1B (optique à lampe, crêtes tenues en douceur : voix chantées, R&B, pop).
        // Le cerveau peut l'imposer (« peakComp ») ; sinon une voix surtout chantée prend le CL1B.
        const bool useCl1b = h.peakComp == 1 || (h.peakComp < 0 && ! h.rapStyle && sung >= 0.5f);
        // rap moderne (SCH, Niro, Damso...) : attaque et relâchement plus rapides encore, la voix reste collée et en avant
        const float fast = h.fast ? 1.0f : 0.0f;
        const float soft = juce::jlimit (0.0f, 1.0f, sung + (h.comp < -0.2f ? 0.4f : 0.0f)) * (1.0f - 0.7f * fast);
        if (useCl1b)
        {
            // le seuil du CL1B : le niveau (en dB du 0 VU, -18 dBFS) où commence le premier dB ; au-dessus, ratio R
            const float ratio = sung >= 0.5f ? 3.0f : 4.0f;
            const float peakVu = P + 18.0f;
            const float thr = juce::jlimit (-40.0f, 20.0f, peakVu - (grFet - 1.0f) / (1.0f - 1.0f / ratio));
            Setting c { modules::cl1b, 0, {} };
            put (c, ids::cl1bThreshold, std::round (thr * 10.0f) / 10.0f);
            put (c, ids::cl1bRatio, ratio);
            put (c, ids::cl1bAttack, std::round (10.0f * (fast > 0.5f ? 1.0f : 3.0f + 9.0f * soft)) / 10.0f);
            put (c, ids::cl1bRelease, std::round (fast > 0.5f ? 90.0f : 150.0f + 350.0f * soft));
            put (c, ids::cl1bGain, std::round (0.8f * grFet * 10.0f) / 10.0f);
            plan.modules.push_back (c);
        }
        else
        {
            const float in = juce::jlimit (-12.0f, 36.0f, grFet / 0.75f - 20.0f - P);   // 1176 ratio 4 : seuil -20 dBFS
            Setting fet { modules::fet, 0, {} };
            put (fet, ids::fetIn, std::round (in * 10.0f) / 10.0f);
            put (fet, ids::fetOut, juce::jlimit (-24.0f, 24.0f, -in + 0.8f * grFet));
            // voix moderne : attaque et release rapides (7 = le plus rapide) ; un peu plus lent pour un rendu naturel
            //               une voix chantée garde ses attaques et ses fins de notes : attaque plus lente, release plus long
            const float atk = fast > 0.5f ? 6.3f : (h.comp > 0.3f ? 5.5f : 5.0f) - 2.5f * soft;
            const float rel = fast > 0.5f ? 6.9f : 6.5f - 2.5f * soft;
            put (fet, ids::fetAttack, std::round (10.0f * atk) / 10.0f);
            put (fet, ids::fetRelease, std::round (10.0f * rel) / 10.0f);
            put (fet, ids::fetRatio, h.comp > 0.6f ? 1.0f : 0.0f);
            plan.modules.push_back (fet);
        }
        const float grOpto = juce::jlimit (2.0f, 7.0f, 2.0f + 0.35f * need);
        compGr = grFet + grOpto;
        const float level = L + 0.4f * grFet;
        Setting opto { modules::opto, 0, {} };
        put (opto, ids::laPeak, std::round (juce::jlimit (0.0f, 100.0f, -(level - 1.5f * grOpto) / 0.45f)));   // seuil -0,45 × PEAK
        put (opto, ids::laGain, juce::jlimit (-12.0f, 30.0f, 0.8f * grOpto));
        plan.modules.push_back (opto);
        rep.add (u8 ("Dynamique ") + juce::String (srcDr, 1) + u8 (" dB → ") + juce::String (tgtDr, 1) + u8 (" dB : ")
                 + (useCl1b ? u8 ("CL1B (≈ −") : u8 ("1176 (≈ −")) + juce::String (juce::roundToInt (grFet))
                 + u8 (" dB sur les crêtes") + (fast > 0.5f ? u8 (", attaque et relâchement rapides du rap moderne") : juce::String())
                 + u8 (") puis LA-2A (≈ −") + juce::String (juce::roundToInt (grOpto)) + " dB)");
    }

    // ---- 6. MULTIBAND : chaque zone de la voix tenue à part, 4 à 5 dB de réduction ; ses gains finissent de la façonner.
    //      Quand un mix « bien contrôlé » est demandé, quand la voix est très inégale, ou quand elle doit être très en avant.
    multiUsed = h.controlled || need >= 6.0f || h.comp > 0.5f;
    if (multiUsed)
    {
        Setting s { modules::multi, 0, {} };
        // la réduction visée : 4 dB, jusqu'à 5 pour une voix très maîtrisée, un peu moins pour un rendu naturel
        const float gr = juce::jlimit (3.0f, 5.0f, 4.0f + (h.controlled ? 0.6f : 0.0f) + 0.08f * juce::jmax (0.0f, need - 6.0f)
                                                    + 0.5f * juce::jmax (0.0f, h.comp) - 0.8f * juce::jmax (0.0f, -h.comp));
        // l'énergie de la voix dans chaque bande (coupures 200 Hz, 1,5 kHz, 6 kHz), par rapport à la voix entière
        const float edges[5] { 20.0f, 200.0f, 1500.0f, 6000.0f, 20000.0f };
        double all = 0.0, part[4] {};
        for (int b = 0; b < kBands; ++b)
        {
            const double pw = std::pow (10.0, (src.valid ? src.bands[(size_t) b] : kDefaultTarget[b]) / 10.0);
            all += pw;
            for (int k = 0; k < 4; ++k) if (kCentres[b] >= edges[k] && kCentres[b] < edges[k + 1]) part[k] += pw;
        }
        // l'écart à la cible dans chaque bande : ce que les gains peuvent encore façonner, au-delà de l'EQ
        auto gap = [&] (float lo, float hi)
        {
            float v = 0.0f; int cnt = 0;
            for (int b = 0; b < kBands; ++b)
                if (kCentres[b] >= lo && kCentres[b] < hi) { v += src.valid ? target[(size_t) b] - src.bands[(size_t) b] : 0.0f; ++cnt; }
            return cnt > 0 ? v / (float) cnt : 0.0f;
        };
        // 3:1 et un genou large : environ 2/3 du dépassement est retiré ; les graves et les aigus un peu moins pressés
        const float bandGr[4] { 0.8f * gr, gr, gr, 0.8f * gr };
        const float muddy = src.valid ? juce::jmax (0.0f, (src.bands[11] + src.bands[12] + src.bands[13]) / 3.0f - (target[11] + target[12] + target[13]) / 3.0f) : 0.0f;
        const float shape[4] {
            juce::jlimit (-2.0f, 0.5f, 0.25f * gap (100.0f, 200.0f) - 0.5f),                                  // LOW : des graves tenus (rien à gagner sous 100 Hz)
            juce::jlimit (-2.0f, 1.5f, 0.35f * gap (200.0f, 1500.0f) - 0.4f * muddy + (thin ? 0.8f : 0.0f)),   // LO-MID : le corps, sans la boue
            juce::jlimit (-1.5f, 1.0f, 0.2f * gap (1500.0f, 6000.0f) + 0.3f + 0.3f * juce::jmax (0.0f, h.comp) + 0.4f * h.bright - 0.6f * h.dark),   // HI-MID : la présence (l'EQ en a déjà donné)
            juce::jlimit (-1.5f, 1.0f, 0.2f * gap (6000.0f, 16000.0f) + 0.6f * h.air - 0.8f * h.dark) };      // HIGH : l'air (l'EQ et l'EQ ANALOG en donnent déjà)
        static const char* names[4] { "LOW", "LO-MID", "HI-MID", "HIGH" };
        juce::StringArray gains;
        for (int k = 0; k < 4; ++k)
        {
            const float bandLevel = L + (float) (10.0 * std::log10 (juce::jmax (1.0e-9, part[k] / juce::jmax (1.0e-9, all))));
            put (s, ids::mbThr[k], std::round (juce::jlimit (-60.0f, 0.0f, bandLevel - bandGr[k] / 0.667f) * 10.0f) / 10.0f);
            const float g = juce::jlimit (-6.0f, 9.0f, 0.8f * bandGr[k] + shape[k]);   // la réduction compensée, et le façonnage
            put (s, ids::mbGain[k], std::round (g * 10.0f) / 10.0f);
            gains.add (juce::String (names[k]) + " " + dbText (g));
        }
        // rapide sur le rap (la zone est tenue dès l'attaque de la syllabe), plus souple sur le chant
        const float soft = juce::jlimit (0.0f, 1.0f, sung + (h.comp < -0.2f ? 0.4f : 0.0f));
        put (s, ids::mbAttack, h.fast ? 3.0f : std::round (6.0f + 14.0f * soft));
        put (s, ids::mbRelease, h.fast ? 60.0f : std::round (90.0f + 130.0f * soft));
        plan.modules.push_back (s);
        rep.add (u8 ("Multiband : environ −") + juce::String (gr, 1) + u8 (" dB par zone pour une voix maîtrisée · gains ") + gains.joinIntoString (u8 (" · ")));
    }

    // ---- 7. DE-ESSER : après les compressions et l'EQ, qui font ressortir les S
    {
        const float tgtSib = tgtDyn ? tgt.sibilance : kDefaultSib;
        const float excess = src.valid ? src.sibilance - tgtSib : 0.0f;
        const float amt = juce::jlimit (25.0f, 75.0f, 35.0f + 4.0f * excess + 4.0f * juce::jmax (0.0f, highBoost) + 2.0f * need + 8.0f * h.air);
        Setting s { modules::deess, 0, {} };
        put (s, ids::deessFreq, src.valid ? src.sibFreq : 6500.0f);
        put (s, ids::deessAmt, std::round (amt));
        put (s, ids::deessS, 1.0f);
        put (s, ids::deessCH, amt > 50.0f ? 1.0f : 0.0f);
        put (s, ids::deessClarity, h.air > 0.3f || h.bright > 0.3f ? 35.0f : 20.0f);
        plan.modules.push_back (s);
        rep.add (u8 ("Sifflantes vers ") + hzText (src.valid ? src.sibFreq : 6500.0f) + " : de-esser " + juce::String (juce::roundToInt (amt)) + " %");
    }

    // ---- 8. SATURATION de couleur : seulement si elle est demandée
    if (h.sat > 0.05f || h.satType >= 0)
    {
        Setting s { modules::sat, satInstance, {} };
        const int type = h.satType >= 0 ? h.satType : 1;
        put (s, ids::satType, (float) type);
        put (s, ids::drive, 2.0f + 5.0f * h.sat);
        put (s, ids::satMix, juce::jlimit (25.0f, 90.0f, 30.0f + 55.0f * h.sat));
        put (s, ids::satWarmth, 20.0f + 40.0f * h.warm);
        plan.modules.push_back (s);
        static const char* names[] { "lampe", "analogique", "bande", "transformateur" };
        rep.add (u8 ("Couleur : saturation ") + u8 (names[type]) + (h.sat > 0.4f ? u8 (" marquée") : u8 (" légère")));
    }

    // ---- 9. EQ ANALOG après les compressions : la série de compressions remonte la boue et ternit le haut ; on renettoie
    //      légèrement (MUD, coupe-bas 85 Hz) et on rend la clarté (HI, AIR), sans empiler sur l'air déjà ajouté par l'EQ
    {
        Setting s { modules::analog, 0, {} };
        // SECONDE COUCHE DE DEMUD : ce que la voix récupère de boue après les compresseurs. Elle est estimée : la boue que l'EQ
        // du début a laissée (mesurée sur la prise, moins sa coupe), plus ce que la compression remonte (elle écrase les crêtes,
        // le bas-médium tenu des voyelles ressort d'autant plus que la réduction est forte), moins ce que le multiband tient
        // déjà dans le bas-médium. Chaleur demandée ou voix chantée : on en garde davantage. Rien à reprendre : MUD reste à 0.
        const float residual = juce::jmax (0.0f, mudExcess - std::abs (mudCut) / 0.8f);
        float regained = 0.6f * residual + 0.09f * compGr + (h.parallel || h.comp > 0.4f ? 0.4f : 0.0f)
                         - (multiUsed ? 0.5f : 0.0f) - (h.warm > 0.3f ? 0.8f : 0.0f) - 0.4f * sung;
        if (src.valid && mudExcess <= 0.0f) regained *= 0.5f;   // prise sans boue : très peu à reprendre
        const float mud = regained < 0.5f ? 0.0f : juce::jlimit (1.0f, 5.0f, 0.6f + 1.4f * regained);
        const float hi = juce::jlimit (0.0f, 4.0f, 1.5f + 1.5f * h.bright - 1.5f * h.dark);
        const float air = juce::jlimit (0.0f, 5.0f, 2.0f + 3.0f * h.air - 0.6f * juce::jmax (0.0f, highBoost - 2.0f) - 2.0f * h.dark);
        put (s, ids::mud, std::round (mud * 10.0f) / 10.0f);
        put (s, ids::high, std::round (hi * 10.0f) / 10.0f);
        put (s, ids::air, std::round (air * 10.0f) / 10.0f);
        put (s, ids::lowCut, 1.0f);
        plan.modules.push_back (s);
        rep.add (u8 ("EQ analog après les compressions : ")
                 + (mud > 0.0f ? u8 ("seconde couche de DeMud (MUD ") + juce::String (mud, 1) + u8 (", le mud que les compresseurs font remonter), ")
                               : u8 ("pas de mud à reprendre, "))
                 + u8 ("clarté HI +") + juce::String (hi, 1) + " et AIR +" + juce::String (air, 1));
    }

    // ---- 9 bis. COMPRESSION PARALLÈLE en fin de traitement : de la densité et du punch quand la voix doit passer devant
    if (h.parallel || h.comp > 0.4f)
    {
        Setting s { modules::parallel, 0, {} };
        put (s, ids::parAmount, juce::jlimit (20.0f, 55.0f, 25.0f + 25.0f * juce::jmax (0.0f, h.comp)));
        put (s, ids::parThresh, juce::jlimit (-57.0f, -9.0f, L - 14.0f));
        put (s, ids::parAttack, h.fast ? 6.6f : std::round (10.0f * (6.0f - 2.0f * sung)) / 10.0f);
        put (s, ids::parRelease, h.fast ? 7.0f : std::round (10.0f * (6.5f - 2.0f * sung)) / 10.0f);
        plan.modules.push_back (s);
        rep.add (u8 ("Compression parallèle : la voix gagne en densité et passe devant la prod"));
    }

    // ---- 10. REVERB : la queue de la référence, sinon l'espace demandé, sinon celui d'une voix mixée (plate ~1,8 s)
    {
        float send = h.reverb >= 0.0f ? h.reverb : 0.26f;
        send = juce::jmax (0.0f, send + h.reverbBias);   // ce qu'ONR Neural Learning a appris
        float decay = h.reverbSize >= 0.0f ? 0.8f + 2.7f * h.reverbSize : 1.8f;
        if (tgt.valid && tgt.rt60 > 0.0f && ! h.reverbExplicit)
        {
            decay = juce::jlimit (0.5f, 3.5f, tgt.rt60);   // au-delà, ce sont des restes d'instruments
            const float wet = juce::jlimit (0.0f, 1.0f, (tgt.wetDb + 25.0f) / 20.0f);   // -25 dB : sèche, -5 dB : très présente
            send = 0.05f + 0.4f * wet;
        }
        if (send > 0.02f)
        {
            Setting s { modules::reverb, 0, {} };
            const int mode = h.dreamy ? 4 : decay < 1.0f ? 0 : decay < 2.4f ? 1 : 3;
            const float mix = juce::jlimit (3.0f, 60.0f, 100.0f * send * 0.7f);
            put (s, ids::reverbMode, (float) mode);
            put (s, ids::reverbDecay, decay);
            put (s, ids::reverbMix, mix);
            put (s, ids::reverbPreDelay, decay > 2.0f ? 35.0f : 22.0f);
            put (s, ids::reverbDucking, 8.0f);    // un léger effacement sous la voix (8 %), la queue s'ouvre dans les blancs
            put (s, ids::reverbClean, 35.0f);
            plan.modules.push_back (s);
            static const char* modes[] { "room", "plate", "plate 2", "hall", "shimmer" };
            rep.add (u8 ("Réverbe ") + modes[mode] + " " + juce::String (decay, 1) + " s, envoi " + juce::String (juce::roundToInt (mix)) + " %"
                     + (tgt.valid && tgt.rt60 > 0.0f && ! h.reverbExplicit ? (tgtIn.valid ? u8 (" (d'après la référence)") : u8 (" (d'après la voix de ") + h.voiceOf + ")") : juce::String()));
        }
    }

    // ---- 11. DELAY : écho repéré dans la référence, ou demandé
    {
        float amount = h.delay;
        if (tgt.valid && tgt.echoAmount > 0.0f) amount = juce::jmax (amount, 0.3f + 0.4f * tgt.echoAmount);
        if (amount > 0.05f) amount = juce::jmax (0.06f, amount + h.delayBias);
        if (amount > 0.05f)
        {
            Setting s { modules::delay, 0, {} };
            if (tgt.valid && tgt.echoMs > 0.0f) { put (s, ids::delaySync, 0.0f); put (s, ids::delayTime, tgt.echoMs); }
            put (s, ids::delayFeedback, 20.0f + 25.0f * amount);
            put (s, ids::delayMix, 10.0f + 18.0f * amount);
            put (s, ids::delayDucking, 10.0f);    // léger : 10 % au plus
            plan.modules.push_back (s);
            rep.add (tgt.valid && tgt.echoMs > 0.0f ? u8 ("Écho entendu à ") + juce::String (juce::roundToInt (tgt.echoMs)) + u8 (" ms dans la référence : delay")
                                                    : juce::String ("Delay au tempo, en retrait sous la voix"));
        }
    }

    // ---- 12. CREATIVE FX : effets demandés
    if (h.phone > 0.0f || h.doubler > 0.0f || h.chorus > 0.0f || h.flanger > 0.0f)
    {
        Setting s { modules::creative, 0, {} };
        put (s, ids::doublerOn, h.doubler > 0.0f ? 1.0f : 0.0f); put (s, ids::doublerAmount, 30.0f + 40.0f * h.doubler);
        put (s, ids::chorusOn, h.chorus > 0.0f ? 1.0f : 0.0f); put (s, ids::chorusMix, 20.0f + 25.0f * h.chorus);
        put (s, ids::flangerOn, h.flanger > 0.0f ? 1.0f : 0.0f); put (s, ids::flangerMix, 25.0f + 25.0f * h.flanger);
        put (s, ids::phoneOn, h.phone > 0.0f ? 1.0f : 0.0f); put (s, ids::phoneAmount, 40.0f + 40.0f * h.phone);
        plan.modules.push_back (s);
        juce::StringArray fx;
        if (h.doubler > 0.0f) fx.add ("doubleur");
        if (h.chorus > 0.0f) fx.add ("chorus");
        if (h.flanger > 0.0f) fx.add ("flanger");
        if (h.phone > 0.0f) fx.add (u8 ("téléphone"));
        rep.add ("Creative FX : " + fx.joinIntoString (", "));
    }
    return plan;
}

//==============================================================================
Refine parseRefine (const juce::String& text)
{
    Refine r;
    auto t = " " + text.toLowerCase() + " ";
    const char* from[] { "é", "è", "ê", "ë", "à", "â", "î", "ï", "ô", "ù", "û", "ç", "’", "'" };
    const char* to[] { "e", "e", "e", "e", "a", "a", "i", "i", "o", "u", "u", "c", " ", " " };
    for (int i = 0; i < 14; ++i) t = t.replace (u8 (from[i]), to[i]);
    // une demande, plusieurs retouches : « moins de réverbe et plus en avant, un peu plus sombre »
    juce::StringArray parts;
    parts.addTokens (t.replace (" et ", ",").replace (" mais ", ",").replace (" puis ", ","), ",;.", "");
    for (auto part : parts)
    {
        part = " " + part.trim() + " ";
        if (part.trim().isEmpty()) continue;
        auto has = [&part] (std::initializer_list<const char*> keys) { for (auto* k : keys) if (part.contains (juce::String (k))) return true; return false; };
        // le sens : « moins », « trop » baissent ; « plus », « pas assez », « encore » montent ; l'ampleur : « un peu », « beaucoup »
        float sign = 1.0f;
        if (has ({ " moins ", " trop ", " less ", " baisse ", " enleve ", " retire ", " reduis " })) sign = -1.0f;
        if (has ({ " pas assez ", " encore plus " })) sign = 1.0f;
        float amt = 1.0f;
        if (has ({ " un peu ", " legerement ", " un poil ", " a bit ", " slightly ", " un chouia " })) amt = 0.5f;
        if (has ({ " beaucoup ", " tres ", " vraiment ", " bien plus ", " much ", " way ", " carrement " })) amt = 1.5f;
        const float k = sign * amt;
        auto said = [&r] (const char* s) { r.understood.add (juce::String::fromUTF8 (s)); };
        bool hit = false;
        // l'espace
        if (has ({ "reverb", "espace", "air autour", "ambiance" })) { r.space += 30.0f * k; said (k > 0 ? "plus d'espace" : "moins d'espace"); hit = true; }
        if (has ({ " sec ", " seche ", " dry ", " proche " })) { r.space -= 35.0f * amt; said ("plus sèche"); hit = true; }
        if (has ({ "delay", "echo" })) { r.delayMul *= k > 0 ? 1.0f + 0.4f * amt : 1.0f / (1.0f + 0.4f * amt); said (k > 0 ? "plus de delay" : "moins de delay"); hit = true; }
        // la place
        if (has ({ "en avant", "devant", "present", "presente", "upfront", "ressorte", "ressortir" })) { r.presence += 22.0f * k; r.hold += 18.0f * k; said (k > 0 ? "plus en avant" : "moins en avant"); hit = true; }
        if (has ({ "en arriere", "recule", "fondue", "fondu dans" })) { r.presence -= 22.0f * amt; r.hold -= 12.0f * amt; r.space += 12.0f * amt; said ("plus en arrière"); hit = true; }
        // la couleur
        if (has ({ "sombre", "dark", "doux", "douce", "moins brillant", "moins agressi", "smooth" })) { r.presence -= 28.0f * k; said (k > 0 ? "plus sombre" : "moins sombre"); hit = true; }
        else if (has ({ "brillant", "brillante", "clair", "claire", "aere", "aeree", " air ", "bright", "cristal" })) { r.presence += 28.0f * k; said (k > 0 ? "plus brillante" : "moins brillante"); hit = true; }
        if (has ({ "chaud", "chaude", "rond", "ronde", "epais", "epaisse", "corps", "warm", "grave" })) { r.warmth += 28.0f * k; said (k > 0 ? "plus chaude" : "moins chaude"); hit = true; }
        if (has ({ " fine ", " fin ", "maigre", "moins de basses", "boueu", "boue", " mud " })) { r.warmth -= 25.0f * amt; said ("plus légère dans le bas"); hit = true; }
        // la tenue
        if (has ({ "compress", "tenu", "tenue", "serre", "serree", "punch", "dense", "constant" })) { r.hold += 28.0f * k; said (k > 0 ? "plus tenue" : "moins tenue"); hit = true; }
        if (has ({ "naturel", "naturelle", "dynamique", "respire" }) && ! has ({ "respiration" })) { r.hold -= 25.0f * amt; said ("plus naturelle"); hit = true; }
        // l'accordage, les respirations
        if (has ({ "autotune", "auto tune", "tune", "robot", "accord" })) { r.retuneMul *= k > 0 ? 1.0f / (1.0f + amt) : 1.0f + amt; said (k > 0 ? "plus d'autotune" : "moins d'autotune"); hit = true; }
        if (has ({ "respiration", "breath", "souffle entre" })) { r.breathDb += (k > 0 ? -4.0f : 4.0f) * amt; said (k > 0 ? "respirations plus présentes" : "respirations plus baissées"); hit = true; }
        juce::ignoreUnused (hit);
    }
    r.presence = juce::jlimit (-100.0f, 100.0f, r.presence); r.warmth = juce::jlimit (-100.0f, 100.0f, r.warmth);
    r.hold = juce::jlimit (-100.0f, 100.0f, r.hold); r.space = juce::jlimit (-100.0f, 100.0f, r.space);
    return r;
}
} // namespace onirique::mix
