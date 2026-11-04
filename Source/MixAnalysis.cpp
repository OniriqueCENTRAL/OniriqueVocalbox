#include "MixAnalysis.h"
#include "Modules.h"

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

    // timbre : moyenne par tiers d'octave, la zone 200 Hz .. 5 kHz ramenée à 0
    for (int b = 0; b < kBands; ++b) p.bands[(size_t) b] = acc[(size_t) b] > 0.0 ? dbOf (acc[(size_t) b] / frames) : -120.0f;
    float ref = 0.0f;
    for (int b = 10; b <= 24; ++b) ref += p.bands[(size_t) b];
    ref /= 15.0f;
    for (auto& v : p.bands) v = juce::jmax (-60.0f, v - ref);

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
        if (above >= 30 && sm[i] < p.loudDb - 15.0f && sm[i - 1] >= p.loudDb - 15.0f)
        {
            ++offsets;
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
            wets.push_back (sm[i + 12] - p.loudDb);
            // l'écho : une bosse de 4 dB au même retard après la plupart des phrases
            for (int lag = 12; lag < 90; ++lag)
            {
                float lo = 1.0e9f;
                for (int j = 1; j <= 6; ++j) lo = juce::jmin (lo, sm[i + (size_t) (lag - j)]);
                if (sm[i + (size_t) lag] - lo > 4.0f && sm[i + (size_t) lag] > p.noiseFloorDb + 6.0f)
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
        if (share >= 0.4f) { p.echoMs = (float) best * 10.0f; p.echoAmount = juce::jmin (1.0f, share); }
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

//==============================================================================
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
    if (has ({ "rap", "hip hop", "hiphop", "drill", "trap", "rap fr", "boom bap" })) { h.comp += 0.5f; h.bright += 0.3f; h.air += 0.2f; if (h.reverb < 0) h.reverb = 0.12f; note ("rap"); }
    if (has ({ "rnb", "r&b", "r b", "soul", "neo soul" })) { h.warm += 0.4f; h.air += 0.3f; h.comp += 0.2f; h.reverb = juce::jmax (h.reverb, 0.3f); h.doubler += 0.3f; note ("R&B"); }
    if (has ({ "pop" })) { h.bright += 0.4f; h.air += 0.5f; h.comp += 0.5f; h.reverb = juce::jmax (h.reverb, 0.22f); note ("pop"); }
    if (has ({ "rock", "punk", "metal", "grunge" })) { h.sat += 0.4f; h.comp += 0.5f; h.satType = 3; note ("rock"); }
    if (has ({ "jazz", "acoustique", "acoustic", "folk" })) { h.comp -= 0.4f; h.warm += 0.4f; h.reverb = juce::jmax (h.reverb, 0.2f); h.reverbSize = 0.3f; note ("acoustique"); }
    if (has ({ "afro", "afrobeat", "dancehall", "reggaeton", "latin" })) { h.bright += 0.3f; h.delay = juce::jmax (h.delay, 0.3f); h.comp += 0.3f; note ("afro / latin"); }
    if (has ({ "ballade", "ballad", "piano voix" })) { h.reverb = juce::jmax (h.reverb, 0.4f); h.reverbSize = 0.8f; h.comp -= 0.1f; note ("ballade"); }
    if (has ({ "edm", "house", "techno", "dance", "electro" })) { h.bright += 0.4f; h.air += 0.4f; h.delay = juce::jmax (h.delay, 0.35f); h.comp += 0.4f; note ("électro"); }
    if (has ({ "autotune", "auto tune", "tune" })) note ("(l'accordage n'est pas dans VOCALBOX)");

    // couleurs
    if (has ({ "aere", "aeree", "aerien", "aerienne", "air", "airy", "brillant", "brillante", "bright", "clair", "claire", "cristal", "cristalline", "crisp" })) { h.air += 0.6f; h.bright += 0.4f; note ("aérée"); }
    if (has ({ "sombre", "dark", "fonce", "grave", "lourd", "lourde" })) { h.dark += 0.6f; note ("sombre"); }
    if (has ({ "chaud", "chaude", "warm", "doux", "douce", "mellow", "velours", "smooth", "rond", "ronde" })) { h.warm += 0.6f; note ("chaude"); }
    if (has ({ "agressif", "agressive", "aggressive", "punch", "punchy", "en avant", "upfront", "devant", "serre", "serree", "compresse", "compressee", "tight", "dense" })) { h.comp += 0.7f; note ("en avant"); }
    if (has ({ "naturel", "naturelle", "natural", "leger", "legere", "light", "dynamique", "ouvert", "ouverte" })) { h.comp -= 0.6f; note ("naturelle"); }
    // espace
    if (has ({ "reverb", "reverbe", "reverberation", "espace", "spacieux", "spacieuse", "hall", "cathedrale", "cathedral", "atmospherique", "atmosphere", "ambiance", "ambient" })) { h.reverb = juce::jmax (h.reverb, 0.45f); h.reverbSize = juce::jmax (h.reverbSize, 0.6f); note ("réverbe"); }
    if (has ({ "planant", "planante", "dreamy", "reve", "reveur", "onirique", "ethere", "etheree", "ethereal", "nuage", "cloud" })) { h.reverb = juce::jmax (h.reverb, 0.55f); h.reverbSize = 0.9f; h.delay = juce::jmax (h.delay, 0.3f); note ("planante"); }
    if (has ({ "shimmer", "celeste", "angelique", "angelic" })) { h.reverb = juce::jmax (h.reverb, 0.5f); h.reverbSize = 0.9f; h.dreamy = true; note ("shimmer"); }
    if (has ({ "sec", "seche", "dry", "close", "intime", "intimate", "proche" })) { h.reverb = 0.0f; h.delay = 0.0f; note ("sèche"); }
    if (has ({ "delay", "echo", "echos", "ping pong" })) { h.delay = juce::jmax (h.delay, 0.5f); note ("delay"); }
    if (has ({ "longue", "long", "grande", "large reverb", "big" })) h.reverbSize = juce::jmax (h.reverbSize, 0.8f);
    if (has ({ "courte", "short", "petite", "room", "piece" })) h.reverbSize = 0.15f;
    // texture
    if (has ({ "sature", "saturee", "saturation", "grain", "granuleux", "distordu", "distorsion", "distortion", "crunch", "gras", "grasse", "sale", "dirty" })) { h.sat += 0.6f; note ("saturée"); }
    if (has ({ "tape", "bande", "cassette", "lofi", "lo fi", "vintage", "retro", "old school" })) { h.sat += 0.4f; h.satType = 2; h.dark += 0.2f; note ("bande / vintage"); }
    if (has ({ "tube", "lampe", "lampes", "valve" })) { h.sat += 0.4f; h.satType = 0; note ("lampe"); }
    if (has ({ "analog", "analogique", "console" })) { h.sat += 0.3f; h.satType = 1; note ("analogique"); }
    // effets
    if (has ({ "telephone", "phone", "radio", "megaphone", "talkie" })) { h.phone = 0.7f; note ("téléphone"); }
    if (has ({ "double", "doublee", "doubled", "doubleur", "doubler", "stereo", "large", "wide", "epaisse", "thick" })) { h.doubler = juce::jmax (h.doubler, 0.6f); note ("doublée"); }
    if (has ({ "chorus" })) { h.chorus = 0.6f; note ("chorus"); }
    if (has ({ "flanger", "flange", "avion", "jet" })) { h.flanger = 0.6f; note ("flanger"); }

    // les mots non compris : souvent des noms d'artistes
    static const juce::StringArray stop { "de", "la", "le", "les", "du", "des", "voix", "voice", "vocal", "vocals", "type", "facon", "style", "comme", "like",
        "un", "une", "et", "and", "avec", "with", "mix", "mixe", "mixee", "very", "tres", "plus", "more", "a", "en", "au", "aux", "sur", "pour", "the", "of",
        "sound", "son", "ma", "mon", "ta", "ton", "sa", "ses", "mes", "tes", "je", "veux", "want", "qui", "que", "dans", "in", "on", "my", "your", "un", "peu",
        "bit", "little", "fa", "ex", "fr", "us", "uk", "beaucoup", "lot", "pas", "trop", "too", "not", "no", "sans", "without" };
    for (auto& w : words)
        if (w.length() >= 3 && ! stop.contains (w) && ! used.contains (w) && ! w.containsOnly ("0123456789"))
            h.unknown.addIfNotAlreadyThere (w);

    h.bright = juce::jlimit (0.0f, 1.0f, h.bright);
    h.air = juce::jlimit (0.0f, 1.0f, h.air);
    h.warm = juce::jlimit (0.0f, 1.0f, h.warm);
    h.dark = juce::jlimit (0.0f, 1.0f, h.dark);
    h.comp = juce::jlimit (-1.0f, 1.0f, h.comp);
    h.sat = juce::jlimit (0.0f, 1.0f, h.sat);
    return h;
}

//==============================================================================
namespace
{
// la courbe d'une voix mixée « commerciale » (moyenne de voix pop / rap / R&B mixées), même normalisation que Profile::bands
constexpr float kDefaultTarget[kBands] { -40, -36, -32, -26, -20, -14, -9, -5, -2, 0, 1, 1.5f, 1.5f, 1, 0.5f, 0, -0.5f, -1, -1.5f, -2,
                                         -2.5f, -2.8f, -3.2f, -4.2f, -5.2f, -6.5f, -8, -9.5f, -11.5f, -14.5f, -20 };

void put (Setting& s, const char* id, float v) { s.params.emplace_back (juce::String (id), v); }
}

Plan buildPlan (const Profile& src, const Profile& tgt, const Hints& h)
{
    Plan plan;
    auto& rep = plan.report;

    // ---- la cible : le timbre de la référence, sinon celui d'une voix mixée ; puis les couleurs demandées
    std::array<float, kBands> target {};
    for (int b = 0; b < kBands; ++b)
    {
        float v = kDefaultTarget[b];
        if (tgt.valid) v = ! tgt.fromMix ? tgt.bands[(size_t) b] : (b < 10 ? kDefaultTarget[b] : 0.7f * tgt.bands[(size_t) b] + 0.3f * kDefaultTarget[b]);
        target[(size_t) b] = v;
    }
    const bool tgtDyn = tgt.valid && ! tgt.fromMix;   // dynamique et sifflantes de la référence : seulement si la voix est vraiment seule
    for (int b = 0; b < kBands; ++b)
    {
        const float f = kCentres[b];
        const float oct2k = juce::jmax (0.0f, std::log2 (f / 2000.0f)), oct8k = juce::jmax (0.0f, std::log2 (f / 8000.0f) + 1.0f);
        target[(size_t) b] += h.bright * 1.2f * juce::jmin (2.0f, oct2k) + h.air * 2.5f * juce::jmin (1.5f, oct8k)
                              - h.dark * 1.5f * juce::jmin (2.0f, oct2k);
        if (f >= 150.0f && f <= 450.0f) target[(size_t) b] += 1.5f * h.warm;
    }

    // ---- 1. NOISE / GATE : le fond entre les phrases
    if (src.valid)
    {
        const float gap = src.loudDb - src.noiseFloorDb;
        if (src.noiseFloorDb > -64.0f && gap < 50.0f)
        {
            Setting s { modules::noise, {} };
            const float amt = juce::jlimit (20.0f, 75.0f, 20.0f + (src.noiseFloorDb + 64.0f) * 2.8f);
            put (s, ids::nsAmount, amt);
            plan.modules.push_back (s);
            rep.add (u8 ("Souffle de fond à ") + juce::String (juce::roundToInt (src.noiseFloorDb)) + " dB : noise suppressor " + juce::String (juce::roundToInt (amt)) + " %");
        }
        if (src.noiseFloorDb > -56.0f && gap > 18.0f)
        {
            Setting s { modules::gate, {} };
            put (s, ids::gateThr, juce::jlimit (-70.0f, -20.0f, src.noiseFloorDb + 8.0f));
            put (s, ids::gateRange, 18.0f);
            plan.modules.push_back (s);
            rep.add (u8 ("Bruit entre les phrases : gate léger (−18 dB)"));
        }
        if (src.plosivesPerMin > 3.0f)
        {
            Setting s { modules::plosive, {} };
            put (s, ids::plosive, juce::jlimit (30.0f, 70.0f, 25.0f + src.plosivesPerMin * 2.0f));
            plan.modules.push_back (s);
            rep.add (u8 ("Des P et des B qui claquent : plosives"));
        }
    }

    // ---- 2. EQ : l'écart entre la prise et la cible (au plus ±6 dB, lissé), sinon les couleurs demandées
    {
        Setting s { modules::peq, {} };
        put (s, ids::peqPower, 1.0f);
        std::array<float, kBands> diff {};
        if (src.valid)
        {
            std::array<float, kBands> raw {};
            for (int b = 7; b <= 29; ++b) raw[(size_t) b] = target[(size_t) b] - src.bands[(size_t) b];
            for (int b = 8; b <= 28; ++b) diff[(size_t) b] = juce::jlimit (-4.0f, 4.0f, 0.6f * (0.25f * raw[(size_t) b - 1] + 0.5f * raw[(size_t) b] + 0.25f * raw[(size_t) b + 1]));
        }
        else
        {
            for (int b = 8; b <= 28; ++b) diff[(size_t) b] = juce::jlimit (-4.0f, 4.0f, target[(size_t) b] - kDefaultTarget[b]);
        }
        auto avg = [&diff] (int a, int b) { float v = 0.0f; for (int i = a; i <= b; ++i) v += diff[(size_t) i]; return v / (float) (b - a + 1); };
        // bande 1 : coupe-bas (là où la prise n'a plus que du grondement)
        float cut = 90.0f;
        if (src.valid && src.bands[7] > target[7] + 3.0f) cut = 110.0f;
        put (s, ids::peqOn[0], 1.0f); put (s, ids::peqType[0], 3.0f); put (s, ids::peqFreq[0], cut); put (s, ids::peqSlope[0], 1.0f); put (s, ids::peqQ[0], 0.71f);
        // bandes 2 .. 7 : cloches à 160, 315, 630, 1250, 2500, 5000 Hz ; bande 8 : étagère d'air à 10 kHz
        const int centre[6] { 9, 12, 15, 18, 21, 24 };
        juce::StringArray moves;
        for (int i = 0; i < 6; ++i)
        {
            const float g = juce::jlimit (-4.0f, 4.0f, avg (centre[i] - 1, centre[i] + 1));
            const int bnd = i + 1;
            const bool on = std::abs (g) >= 0.7f;
            put (s, ids::peqOn[bnd], on ? 1.0f : 0.0f);
            put (s, ids::peqType[bnd], 0.0f);
            put (s, ids::peqFreq[bnd], kCentres[centre[i]]);
            put (s, ids::peqGain[bnd], std::round (g * 10.0f) / 10.0f);
            put (s, ids::peqQ[bnd], 1.1f);
            if (on) moves.add (dbText (g) + u8 (" à ") + hzText (kCentres[centre[i]]));
        }
        const float shelf = juce::jlimit (-4.0f, 4.0f, avg (27, 28));
        put (s, ids::peqOn[7], std::abs (shelf) >= 0.7f ? 1.0f : 0.0f);
        put (s, ids::peqType[7], 2.0f); put (s, ids::peqFreq[7], 10000.0f); put (s, ids::peqGain[7], std::round (shelf * 10.0f) / 10.0f); put (s, ids::peqQ[7], 0.71f);
        if (std::abs (shelf) >= 0.7f) moves.add (dbText (shelf) + u8 (" d'air au-dessus de 10 kHz"));
        plan.modules.push_back (s);
        rep.add ("EQ : coupe-bas " + hzText (cut) + (moves.isEmpty() ? juce::String() : ", " + moves.joinIntoString (", ")));
    }

    // ---- 3. COMPRESSION : la dynamique de la prise ramenée vers celle de la cible (1176 pour les crêtes, LA-2A pour lisser)
    {
        const float srcDr = src.valid ? src.dynRange : 12.0f;
        const float tgtDr = juce::jlimit (2.5f, 12.0f, tgtDyn ? tgt.dynRange : 5.5f - 1.8f * h.comp);
        const float need = juce::jlimit (0.0f, 16.0f, srcDr - tgtDr);
        const float peak = src.valid ? src.peak50Db : -18.0f;
        const float loud = src.valid ? src.loudDb : -22.0f;
        const float grFet = juce::jlimit (2.0f, 9.0f, 2.0f + 0.45f * need + 1.5f * juce::jmax (0.0f, h.comp));
        // 1176 (ratio 4) : seuil à -20 dBFS, réduction ≈ 3/4 du dépassement
        const float in = juce::jlimit (-12.0f, 36.0f, grFet / 0.75f - 20.0f - peak);
        Setting fet { modules::fet, {} };
        put (fet, ids::fetIn, std::round (in * 10.0f) / 10.0f);
        put (fet, ids::fetOut, juce::jlimit (-24.0f, 24.0f, -in + 0.8f * grFet));
        put (fet, ids::fetAttack, h.comp > 0.3f ? 3.5f : 3.0f);
        put (fet, ids::fetRelease, 5.0f);
        put (fet, ids::fetRatio, h.comp > 0.6f ? 1.0f : 0.0f);
        // LA-2A : seuil à -0,45 × PEAK REDUCTION dBFS, réduction ≈ 2/3 du dépassement
        const float grOpto = juce::jlimit (1.5f, 6.0f, 1.5f + 0.3f * need);
        const float level = loud + 0.4f * grFet;
        const float pr = juce::jlimit (0.0f, 100.0f, -(level - 1.5f * grOpto) / 0.45f);
        Setting opto { modules::opto, {} };
        put (opto, ids::laPeak, std::round (pr));
        put (opto, ids::laGain, juce::jlimit (-12.0f, 30.0f, 0.8f * grOpto));
        const bool useFet = need >= 2.0f || h.comp > 0.2f;
        if (useFet) plan.modules.push_back (fet);
        plan.modules.push_back (opto);
        rep.add (u8 ("Dynamique ") + juce::String (srcDr, 1) + u8 (" dB → cible ") + juce::String (tgtDr, 1) + " dB : "
                 + (useFet ? u8 ("1176 (≈ −") + juce::String (juce::roundToInt (grFet)) + " dB sur les crêtes) puis " : juce::String())
                 + u8 ("LA-2A (≈ −") + juce::String (juce::roundToInt (grOpto)) + " dB)");
    }

    // ---- 4. DE-ESSER : après la compression (elle fait ressortir les S)
    {
        const float tgtSib = tgtDyn ? tgt.sibilance : -12.0f;
        const float excess = src.valid ? src.sibilance - tgtSib : 2.0f;
        const float amt = juce::jlimit (20.0f, 70.0f, 32.0f + 5.0f * excess + 8.0f * h.air);
        Setting s { modules::deess, {} };
        put (s, ids::deessFreq, src.valid ? src.sibFreq : 6000.0f);
        put (s, ids::deessAmt, std::round (amt));
        put (s, ids::deessS, 1.0f);
        put (s, ids::deessCH, excess > 3.0f ? 1.0f : 0.0f);
        put (s, ids::deessClarity, h.air > 0.3f || h.bright > 0.3f ? 35.0f : 15.0f);
        plan.modules.push_back (s);
        rep.add (u8 ("Sifflantes vers ") + hzText (src.valid ? src.sibFreq : 6000.0f) + " : de-esser " + juce::String (juce::roundToInt (amt)) + " %");
    }

    // ---- 5. TEXTURE : saturation demandée, sinon un grain discret (les voix mixées en ont presque toujours)
    {
        Setting s { modules::sat, {} };
        const int type = h.satType >= 0 ? h.satType : (h.warm > 0.3f ? 0 : 1);
        const float drive = 1.5f + 5.0f * h.sat;
        put (s, ids::satType, (float) type);
        put (s, ids::drive, drive);
        put (s, ids::satMix, juce::jlimit (25.0f, 90.0f, 30.0f + 55.0f * h.sat));
        put (s, ids::satWarmth, 20.0f + 40.0f * h.warm);
        plan.modules.push_back (s);
        static const char* names[] { "lampe", "analogique", "bande", "transformateur" };
        rep.add (u8 ("Texture : saturation ") + u8 (names[type]) + (h.sat > 0.1f ? juce::String (" marquée") : u8 (" discrète")));
    }

    // ---- 6. CREATIVE FX : effets demandés
    if (h.phone > 0.0f || h.doubler > 0.0f || h.chorus > 0.0f || h.flanger > 0.0f)
    {
        Setting s { modules::creative, {} };
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

    // ---- 7. DELAY : écho repéré dans la référence, ou demandé
    {
        float amount = h.delay;
        if (tgt.valid && tgt.echoAmount > 0.0f) amount = juce::jmax (amount, 0.3f + 0.4f * tgt.echoAmount);
        if (amount > 0.05f)
        {
            Setting s { modules::delay, {} };
            if (tgt.valid && tgt.echoMs > 0.0f) { put (s, ids::delaySync, 0.0f); put (s, ids::delayTime, tgt.echoMs); }
            put (s, ids::delayFeedback, 20.0f + 25.0f * amount);
            put (s, ids::delayMix, 10.0f + 20.0f * amount);
            put (s, ids::delayDucking, 55.0f);
            plan.modules.push_back (s);
            rep.add (tgt.valid && tgt.echoMs > 0.0f ? u8 ("Écho entendu à ") + juce::String (juce::roundToInt (tgt.echoMs)) + " ms dans la référence : delay"
                                                    : juce::String ("Delay au tempo, en retrait sous la voix"));
        }
    }

    // ---- 8. REVERB : la queue de la référence, sinon l'espace demandé
    {
        float send = h.reverb >= 0.0f ? h.reverb : 0.18f;
        float decay = h.reverbSize >= 0.0f ? 0.8f + 3.0f * h.reverbSize : 1.8f;
        if (tgt.valid && tgt.rt60 > 0.0f)
        {
            decay = juce::jlimit (0.5f, 5.0f, tgt.rt60);
            // la queue (mesurée au centre du mix, donc sous-estimée) : -45 dB → presque sèche, -20 dB → très présente
            const float wet = juce::jlimit (0.0f, 1.0f, (tgt.wetDb + 6.0f + 45.0f) / 25.0f);
            if (h.reverb < 0.0f) send = 0.05f + 0.45f * wet;
        }
        if (send > 0.02f)
        {
            Setting s { modules::reverb, {} };
            const int mode = h.dreamy ? 4 : decay < 1.0f ? 0 : decay < 2.4f ? 1 : 3;
            put (s, ids::reverbMode, (float) mode);
            put (s, ids::reverbDecay, decay);
            put (s, ids::reverbMix, juce::jlimit (3.0f, 60.0f, 100.0f * send * 0.7f));
            put (s, ids::reverbPreDelay, decay > 2.0f ? 35.0f : 20.0f);
            put (s, ids::reverbDucking, 35.0f);
            put (s, ids::reverbClean, 30.0f);
            plan.modules.push_back (s);
            static const char* modes[] { "room", "plate", "plate 2", "hall", "shimmer" };
            rep.add (u8 ("Réverbe ") + modes[mode] + " " + juce::String (decay, 1) + " s, envoi " + juce::String (juce::roundToInt (100.0f * send * 0.7f)) + " %"
                     + (tgt.valid && tgt.rt60 > 0.0f ? u8 (" (d'après la référence)") : juce::String()));
        }
    }
    return plan;
}
} // namespace onirique::mix
