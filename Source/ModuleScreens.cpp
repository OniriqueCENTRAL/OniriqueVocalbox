#include "ModulePanels.h"

using namespace onirique;

/*  Les écrans des modules, recopiés de la démo validée (Main.dc.html) : mêmes mesures, mêmes couleurs, mêmes textes.
    Les visuels se dessinent dans le repère de la démo (1400 x 360, mis à l'échelle comme un SVG « meet ») ; ils montrent le
    vrai signal : la voix qui entre et ce qui sort de chaque module, les spectres avant / après, les réductions de gain. */

namespace
{
constexpr float kPi = juce::MathConstants<float>::pi;
const juce::Colour cEmber { 0xffE5384A }, cBone { 0xffF4ECEC }, cPink { 0xffff8e9a }, cMist { 0xfff4ecec };

juce::String utf8 (const char* s) { return juce::String::fromUTF8 (s); }
float dbOf (float a) { return a > 1.0e-6f ? 20.0f * std::log10 (a) : -120.0f; }
juce::String hz (float f) { return f >= 1000.0f ? juce::String (f / 1000.0f, 1) + " kHz" : juce::String (juce::roundToInt (f)) + " Hz"; }

// trait pointillé
void dashed (juce::Graphics& g, const juce::Path& p, float w, float on, float off)
{
    juce::Path d;
    const float dash[] { on, off };
    juce::PathStrokeType (w).createDashedStroke (d, p, dash, 2);
    g.fillPath (d);
}

// lueur douce autour d'une forme (drop-shadow de la démo), en quelques passes
void glowPath (juce::Graphics& g, const juce::Path& p, juce::Colour c, float radius, bool fill)
{
    for (int k = 3; k >= 1; --k)
    {
        g.setColour (c.withMultipliedAlpha (0.22f / (float) k));
        if (fill) g.strokePath (p, juce::PathStrokeType (radius * (float) k * 0.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        else      g.strokePath (p, juce::PathStrokeType (radius * (float) k * 0.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

// la forme d'onde en miroir (comme mir() de la démo)
juce::Path mirror (const std::vector<float>& v, float x0, float x1, float cy, float h)
{
    juce::Path p;
    const int n = (int) v.size();
    if (n < 2) return p;
    for (int i = 0; i < n; ++i) { const float x = x0 + (x1 - x0) * (float) i / (float) (n - 1); if (i == 0) p.startNewSubPath (x, cy - h * v[(size_t) i]); else p.lineTo (x, cy - h * v[(size_t) i]); }
    for (int i = n - 1; i >= 0; --i) p.lineTo (x0 + (x1 - x0) * (float) i / (float) (n - 1), cy + h * v[(size_t) i]);
    p.closeSubPath();
    return p;
}

juce::Path poly (const std::vector<juce::Point<float>>& pts)
{
    juce::Path p;
    for (size_t i = 0; i < pts.size(); ++i) { if (i == 0) p.startNewSubPath (pts[i]); else p.lineTo (pts[i]); }
    return p;
}
juce::Path between (const std::vector<juce::Point<float>>& a, const std::vector<juce::Point<float>>& b)
{
    auto p = poly (a);
    for (size_t i = b.size(); i-- > 0;) p.lineTo (b[i]);
    p.closeSubPath();
    return p;
}

// les dernières colonnes de l'oscilloscope d'un module (la plus ancienne à gauche)
void readScope (const ModuleScope& s, std::vector<float>& in, std::vector<float>& out)
{
    const int n = ModuleScope::kCols, w = s.write.load (std::memory_order_acquire);
    in.resize ((size_t) n); out.resize ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        const int idx = (w + i) % n;
        in[(size_t) i] = s.in[idx].load (std::memory_order_relaxed);
        out[(size_t) i] = s.out[idx].load (std::memory_order_relaxed);
    }
}

//==============================================================================
// Le spectre d'une prise (FFT 4096, fenêtre de Hann), ramené sur nb points logarithmiques entre fa et fb, lissé
class MiniSpectrum
{
public:
    void setRange (float a, float b, int points) { fa = a; fb = b; db.assign ((size_t) points, -120.0f); }
    void update (SpectrumTap& tap, double rate)
    {
        float tmp[2048];
        int got;
        while ((got = tap.pull (tmp, 2048)) > 0)
            for (int i = 0; i < got; ++i) { ring[(size_t) pos] = tmp[i]; pos = (pos + 1) % kN; }
        for (int i = 0; i < kN; ++i)
        {
            const float w = 0.5f - 0.5f * std::cos (2.0f * kPi * (float) i / (float) kN);
            work[(size_t) i] = ring[(size_t) ((pos + i) % kN)] * w;
        }
        std::fill (work.begin() + kN, work.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (work.data());
        const int n = (int) db.size();
        for (int k = 0; k < n; ++k)
        {
            const float f0 = fa * std::pow (fb / fa, (float) k / (float) n), f1 = fa * std::pow (fb / fa, (float) (k + 1) / (float) n);
            int b0 = (int) (f0 / (float) rate * kN), b1 = std::max (b0 + 1, (int) (f1 / (float) rate * kN));
            b0 = juce::jlimit (1, kN / 2 - 1, b0); b1 = juce::jlimit (b0 + 1, kN / 2, b1);
            float m = 0.0f;
            for (int b = b0; b < b1; ++b) m = std::max (m, work[(size_t) b]);
            const float v = dbOf (m * 4.0f / (float) kN);
            auto& d = db[(size_t) k];
            d += (v > d ? 0.5f : 0.12f) * (v - d);
        }
    }
    float fa = 20.0f, fb = 20000.0f;
    std::vector<float> db;

private:
    static constexpr int kOrder = 12, kN = 1 << kOrder;
    juce::dsp::FFT fft { kOrder };
    std::array<float, (size_t) kN> ring {};
    std::array<float, (size_t) (2 * kN)> work {};
    int pos = 0;
};

//==============================================================================
/*  Le cadre d'un visuel (.viz de la démo) : rayon 22, fond radial (#1d1013 -> #0e0a0c -> #070506), liseré, ombre ; le
    titre en haut à gauche, la légende en haut à droite, la phrase d'état en bas à gauche. draw() dessine dans le repère
    1400 x 360 de la démo. */
class Viz : public juce::Component, public Animated
{
public:
    juce::String title, stat;
    std::vector<std::pair<juce::String, juce::Colour>> legend;

    void paint (juce::Graphics& g) override
    {
        const float u = ui::unitOf (*this);
        const auto r = getLocalBounds().toFloat();
        const float rad = 22.0f * u;
        for (int k = 3; k >= 1; --k)
        {
            g.setColour (juce::Colours::black.withAlpha (0.12f / (float) k));
            g.fillRoundedRectangle (r.translated (0.0f, 14.0f * u).expanded (8.0f * u * (float) k), rad + 8.0f * u * (float) k);
        }
        juce::ColourGradient bg (juce::Colour (0xff1d1013), r.getX() + r.getWidth() * 0.15f, r.getY(), juce::Colour (0xff070506),
                                 r.getX() + r.getWidth() * 0.15f + r.getWidth() * 1.2f, r.getY(), true);
        bg.addColour (0.55, juce::Colour (0xff0e0a0c));
        g.setGradientFill (bg);
        g.fillRoundedRectangle (r, rad);
        {
            juce::Graphics::ScopedSaveState s (g);
            juce::Path clip; clip.addRoundedRectangle (r, rad);
            g.reduceClipRegion (clip);
            const float sc = std::min (r.getWidth() / 1400.0f, r.getHeight() / 360.0f);
            g.addTransform (juce::AffineTransform::scale (sc).translated (r.getX() + (r.getWidth() - 1400.0f * sc) * 0.5f, r.getY() + (r.getHeight() - 360.0f * sc) * 0.5f));
            // la grille de fond
            g.setColour (juce::Colours::white.withAlpha (0.035f));
            for (int y = 60; y <= 300; y += 60) g.drawHorizontalLine (y, 0.0f, 1400.0f);
            draw (g);
        }
        g.setColour (juce::Colour (255, 220, 225).withAlpha (0.10f));
        g.drawRoundedRectangle (r.reduced (0.5f), rad, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillRect (r.withHeight (1.0f).reduced (rad, 0.0f).translated (0.0f, 1.0f));

        // titre, légende, état
        g.setColour (juce::Colour (0xffd9c6c9));
        g.setFont (ui::css (ui::Face::mono, 10.0f * u, 0.12f));
        g.drawText (title, juce::Rectangle<float> (r.getX() + 18.0f * u, r.getY() + 14.0f * u, r.getWidth() * 0.6f, 14.0f * u), juce::Justification::centredLeft, false);
        float x = r.getRight() - 18.0f * u;
        for (auto it = legend.rbegin(); it != legend.rend(); ++it)
        {
            const auto lf = ui::css (ui::Face::mono, 9.0f * u, 0.12f);
            const float tw = ui::textWidth (lf, it->first);
            x -= tw;
            g.setColour (juce::Colour (0xffbba7aa));
            g.setFont (lf);
            g.drawText (it->first, juce::Rectangle<float> (x, r.getY() + 14.0f * u, tw + 4.0f, 14.0f * u), juce::Justification::centredLeft, false);
            x -= 7.0f * u + 14.0f * u;
            const auto chip = juce::Rectangle<float> (x, r.getY() + 19.0f * u, 14.0f * u, 4.0f * u);
            g.setColour (it->second.withAlpha (0.35f));
            g.fillRoundedRectangle (chip.expanded (2.0f * u), 3.0f * u);
            g.setColour (it->second);
            g.fillRoundedRectangle (chip, 2.0f * u);
            x -= 18.0f * u;
        }
        g.setColour (juce::Colours::white);
        g.setFont (ui::css (ui::Face::mono, 10.0f * u, 0.12f));
        g.drawText (stat, juce::Rectangle<float> (r.getX() + 18.0f * u, r.getBottom() - 26.0f * u, r.getWidth() - 36.0f * u, 14.0f * u), juce::Justification::centredLeft, false);
    }

protected:
    virtual void draw (juce::Graphics&) = 0;
    // une position de la souris -> le repère de la démo
    juce::Point<float> toDemo (juce::Point<float> p) const
    {
        const auto r = getLocalBounds().toFloat();
        const float sc = std::min (r.getWidth() / 1400.0f, r.getHeight() / 360.0f);
        return { (p.x - (r.getWidth() - 1400.0f * sc) * 0.5f) / sc, (p.y - (r.getHeight() - 360.0f * sc) * 0.5f) / sc };
    }
    float time() const { return (float) (juce::Time::getMillisecondCounterHiRes() * 0.001); }
};

// le dégradé braise des parties retirées (#ff6b7a -> #7a0d1c, du haut vers le bas)
juce::ColourGradient emberFill (float y0, float y1) { return { juce::Colour (0xffff6b7a), 0.0f, y0, juce::Colour (0xff7a0d1c), 0.0f, y1, false }; }

//==============================================================================
// GATE, DE-ESSER, NOISE, PLOSIVES : la voix qui défile (ce qui est gardé en blanc, ce qui est retiré en braise) ; à droite,
// le spectre avant (braise, pointillé) et après (blanc), l'écart rempli en braise, le repère de la fréquence réglée
class WaveViz : public Viz
{
public:
    WaveViz (OniriqueVocalBoxAudioProcessor& p, int code) : processor (p), module (p.module (code)), type (modules::typeOf (code)), k (modules::instanceOf (code))
    {
        const float fa = type == modules::deess ? 1000.0f : 20.0f, fb = type == modules::deess ? 16000.0f : (type == modules::plosive ? 1000.0f : 20000.0f);
        specIn.setRange (fa, fb, 70);
        specOut.setRange (fa, fb, 70);
        if (type == modules::gate) setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    }

    void tick() override
    {
        if (module == nullptr) return;
        readScope (module->scope, in, out);
        if (++frame % 2 == 0 && module->tapIn != nullptr)
        {
            specIn.update (*module->tapIn, module->getRate());
            specOut.update (*module->tapOut, module->getRate());
        }
        // la réduction : la plus forte récente
        red = std::max (red * 0.97f, std::abs (module->getReductionDb()));
        updateText();
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override { mouseDrag (e); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (type != modules::gate) return;
        const auto q = toDemo (e.position);
        const float t = juce::jlimit (0.0f, 1.0f, std::abs (q.y - 170.0f) / 150.0f);
        if (auto* prm = processor.param (ids::gateThr, k)) prm->setValueNotifyingHost (prm->convertTo0to1 (t * 70.0f - 70.0f));
    }

private:
    float get (const char* id) const { return processor.apvts.getRawParameterValue (ids::inst (id, k))->load(); }
    float norm (float a) const
    {
        if (type == modules::gate) return juce::jlimit (0.0f, 1.0f, (dbOf (a) + 70.0f) / 70.0f);
        return juce::jlimit (0.0f, 1.0f, a * 1.5f);
    }

    void updateText()
    {
        switch (type)
        {
            case modules::gate:
            {
                int closed = 0;
                for (size_t i = 0; i < in.size(); ++i) if (in[i] > 1.0e-5f && out[i] < in[i] * 0.5f) ++closed;
                title = utf8 ("NOISE GATE · CE QUI EST COUPÉ ENTRE LES PHRASES");
                legend = { { "GARDE", cMist }, { utf8 ("COUPÉ"), cEmber } };
                stat = "SEUIL " + juce::String (juce::roundToInt (get (ids::gateThr))) + " dB · " + juce::String (juce::roundToInt (100.0f * (float) closed / (float) std::max<size_t> (1, in.size())))
                     + utf8 (" % DU TEMPS FERMÉ · −") + juce::String (juce::roundToInt (get (ids::gateRange))) + " dB";
                break;
            }
            case modules::deess:
                title = utf8 ("DE-ESSER · LES S RETIRÉS EN BRAISE");
                legend = { { utf8 ("GARDÉ"), cMist }, { utf8 ("RETIRÉ"), cEmber } };
                stat = hz (get (ids::deessFreq)) + utf8 (" · −") + juce::String (red, 1) + " dB SUR LES S";
                break;
            case modules::noise:
                title = utf8 ("NOISE SUPPRESSOR · LE SOUFFLE RETIRÉ EN BRAISE");
                legend = { { utf8 ("GARDÉ"), cMist }, { utf8 ("RETIRÉ"), cEmber } };
                stat = utf8 ("BRUIT DE FOND −") + juce::String (red, 1) + " dB · AMOUNT " + juce::String (juce::roundToInt (get (ids::nsAmount))) + " %";
                break;
            default:
                title = utf8 ("PLOSIVES · LES P ET LES B RETIRÉS EN BRAISE");
                legend = { { utf8 ("GARDÉ"), cMist }, { utf8 ("RETIRÉ"), cEmber } };
                stat = utf8 ("P / B −") + juce::String (red, 1) + " dB · AMOUNT " + juce::String (juce::roundToInt (get (ids::plosive))) + " %";
                break;
        }
    }

    void draw (juce::Graphics& g) override
    {
        if (module == nullptr || in.empty()) return;
        std::vector<float> a (in.size()), b (out.size());
        for (size_t i = 0; i < in.size(); ++i) { a[i] = norm (in[i]); b[i] = norm (out[i]); }
        const auto removed = mirror (a, 0.0f, 1000.0f, 170.0f, 150.0f), kept = mirror (b, 0.0f, 1000.0f, 170.0f, 150.0f);
        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (0, 0, 1000, 360);
            glowPath (g, removed, cEmber, 8.0f, true);
            g.setGradientFill (emberFill (20.0f, 320.0f));
            g.setOpacity (0.9f);
            g.fillPath (removed);
            g.setOpacity (1.0f);
            g.setColour (juce::Colour (244, 236, 236).withAlpha (0.92f));
            g.fillPath (kept);
            if (type == modules::gate)
            {
                // le seuil (deux lignes pointillées) et les moments où le gate est ouvert (barres braise en bas)
                const float t = juce::jlimit (0.0f, 1.0f, (get (ids::gateThr) + 70.0f) / 70.0f) * 150.0f;
                g.setColour (cPink);
                for (float y : { 170.0f - t, 170.0f + t })
                {
                    juce::Path l; l.startNewSubPath (0.0f, y); l.lineTo (1000.0f, y);
                    dashed (g, l, 1.5f, 6.0f, 6.0f);
                }
                const int n = (int) in.size();
                for (int i = 0; i < n;)
                {
                    const bool open = out[(size_t) i] > in[(size_t) i] * 0.5f && in[(size_t) i] > 1.0e-4f;
                    if (! open) { ++i; continue; }
                    int e = i;
                    while (e + 1 < n && out[(size_t) e + 1] > in[(size_t) e + 1] * 0.5f && in[(size_t) e + 1] > 1.0e-4f) ++e;
                    const auto bar = juce::Rectangle<float> (1000.0f * (float) i / (float) (n - 1), 338.0f, std::max (2.0f, 1000.0f * (float) (e - i + 1) / (float) (n - 1)), 8.0f);
                    g.setColour (cEmber.withAlpha (0.35f));
                    g.fillRoundedRectangle (bar.expanded (3.0f), 6.0f);
                    g.setColour (cEmber);
                    g.fillRoundedRectangle (bar, 4.0f);
                    i = e + 1;
                }
            }
        }
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillRect (1000.0f, 0.0f, 1.0f, 360.0f);

        // le spectre de droite (1030 -> 1380)
        if (specIn.db.empty()) return;
        auto X = [&] (int i) { return 1030.0f + 350.0f * (float) i / (float) (specIn.db.size() - 1); };
        auto Y = [] (float d) { return juce::jlimit (40.0f, 330.0f, 40.0f + (-24.0f - d) / 66.0f * 290.0f); };
        std::vector<juce::Point<float>> pin, pout;
        for (size_t i = 0; i < specIn.db.size(); ++i) { pin.push_back ({ X ((int) i), Y (specIn.db[i]) }); pout.push_back ({ X ((int) i), Y (specOut.db[i]) }); }
        g.setColour (cEmber.withAlpha (0.28f));
        g.fillPath (between (pin, pout));
        g.setColour (cEmber);
        dashed (g, poly (pin), 2.0f, 5.0f, 5.0f);
        const auto po = poly (pout);
        glowPath (g, po, juce::Colour (255, 236, 238).withAlpha (0.5f), 6.0f, false);
        g.setColour (cBone);
        g.strokePath (po, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved));
        // le repère de la fréquence réglée (DE-ESSER)
        if (type == modules::deess)
        {
            const float f = get (ids::deessFreq);
            const float x = 1030.0f + 350.0f * std::log (f / specIn.fa) / std::log (specIn.fb / specIn.fa);
            g.setColour (cPink);
            g.drawLine (x, 40.0f, x, 330.0f, 1.5f);
            const int idx = juce::jlimit (0, (int) specOut.db.size() - 1, juce::roundToInt ((x - 1030.0f) / 350.0f * (float) (specOut.db.size() - 1)));
            const auto dot = juce::Rectangle<float> (14.0f, 14.0f).withCentre ({ x, Y (specOut.db[(size_t) idx]) });
            g.setColour (cEmber.withAlpha (0.4f));
            g.fillEllipse (dot.expanded (6.0f));
            g.setColour (cEmber);
            g.fillEllipse (dot);
            g.setColour (juce::Colours::white);
            g.drawEllipse (dot, 2.0f);
        }
    }

    OniriqueVocalBoxAudioProcessor& processor;
    ChainModule* module;
    int type, k, frame = 0;
    float red = 0.0f;
    std::vector<float> in, out;
    MiniSpectrum specIn, specOut;
};

//==============================================================================
// RESONANCE : le spectre en grand (20 Hz - 20 kHz), ce qui est creusé rempli en braise, les zones LOW / MID / HIGH, et leurs
// poignées (glisser : fréquence à l'horizontale, sensibilité à la verticale)
class ResoViz : public Viz
{
public:
    ResoViz (OniriqueVocalBoxAudioProcessor& p, int code) : processor (p), module (dynamic_cast<chainmods::Reso*> (p.module (code))), k (modules::instanceOf (code))
    {
        setMouseCursor (juce::MouseCursor::CrosshairCursor);
        legend = { { utf8 ("GARDÉ"), cMist }, { utf8 ("CREUSÉ"), cEmber } };
        title = utf8 ("RESONANCE · LES PICS QUI SIFFLENT OU BOURDONNENT, CREUSÉS EN BRAISE");
    }
    void tick() override
    {
        if (module != nullptr)
        {
            const auto& m = module->getMeter();
            for (int b = 0; b < ResonanceSuppressor::kBands; ++b)
            {
                spec[(size_t) b] += 0.3f * (m.spectrumDb[(size_t) b].load() - spec[(size_t) b]);
                const float rd = m.reductionDb[(size_t) b].load();
                red[(size_t) b] += (rd > red[(size_t) b] ? 0.5f : 0.1f) * (rd - red[(size_t) b]);
            }
        }
        stat = "AMOUNT " + juce::String (juce::roundToInt (get (ids::resoAmt))) + " % · LOW " + juce::String (juce::roundToInt (get (ids::resoLow)))
             + " % · MID " + juce::String (juce::roundToInt (get (ids::resoMid))) + " % · HIGH " + juce::String (juce::roundToInt (get (ids::resoHigh))) + " %";
        repaint();
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto q = toDemo (e.position);
        drag = -1;
        float best = 40.0f;
        for (int b = 0; b < 3; ++b) { const float d = handle (b).getDistanceFrom (q); if (d < best) { best = d; drag = b; } }
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (drag < 0) return;
        const auto q = toDemo (e.position);
        const char* fid[] { ids::resoLowFreq, ids::resoMidFreq, ids::resoHighFreq };
        const char* sid[] { ids::resoLow, ids::resoMid, ids::resoHigh };
        if (auto* p = processor.param (fid[drag], k)) p->setValueNotifyingHost (p->convertTo0to1 (fOf (q.x)));
        if (auto* p = processor.param (sid[drag], k)) p->setValueNotifyingHost (p->convertTo0to1 (juce::jlimit (0.0f, 100.0f, (330.0f - q.y) / 270.0f * 100.0f)));
    }
    void mouseUp (const juce::MouseEvent&) override { drag = -1; }

private:
    float get (const char* id) const { return processor.apvts.getRawParameterValue (ids::inst (id, k))->load(); }
    static float X (float f) { return 40.0f + 1320.0f * std::log (f / 20.0f) / std::log (1000.0f); }
    static float fOf (float x) { return 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, (x - 40.0f) / 1320.0f)); }
    static float Y (float d) { return juce::jlimit (40.0f, 340.0f, 50.0f + (-14.0f - d) / 80.0f * 290.0f); }
    juce::Point<float> handle (int b) const
    {
        const char* fid[] { ids::resoLowFreq, ids::resoMidFreq, ids::resoHighFreq };
        const char* sid[] { ids::resoLow, ids::resoMid, ids::resoHigh };
        return { X (get (fid[b])), 330.0f - get (sid[b]) * 0.01f * 270.0f };
    }
    void draw (juce::Graphics& g) override
    {
        const char* fid[] { ids::resoLowFreq, ids::resoMidFreq, ids::resoHighFreq };
        const char* wid[] { ids::resoLowWidth, ids::resoMidWidth, ids::resoHighWidth };
        const char* names[] { "LOW", "MID", "HIGH" };
        for (int b = 0; b < 3; ++b)
        {
            const float f = get (fid[b]), w = get (wid[b]);
            const float x0 = X (f / std::pow (2.0f, w * 0.5f)), x1 = X (f * std::pow (2.0f, w * 0.5f));
            g.setColour (juce::Colours::white.withAlpha (b % 2 ? 0.035f : 0.02f));
            g.fillRect (juce::Rectangle<float> (x0, 0.0f, x1 - x0, 360.0f));
            g.setColour (juce::Colour (0xff7d6a6e));
            g.setFont (ui::css (ui::Face::mono, 11.0f, 0.12f));
            g.drawText (names[b], juce::Rectangle<float> (x0, 318.0f, x1 - x0, 20.0f), juce::Justification::centred, false);
        }
        std::vector<juce::Point<float>> pin, pout;
        for (int b = 0; b < ResonanceSuppressor::kBands; ++b)
        {
            const float f = 20.0f * std::pow (1000.0f, ((float) b + 0.5f) / (float) ResonanceSuppressor::kBands);
            pin.push_back ({ X (f), Y (spec[(size_t) b]) });
            pout.push_back ({ X (f), Y (spec[(size_t) b] - red[(size_t) b]) });
        }
        const auto fill = between (pin, pout);
        glowPath (g, fill, cEmber, 10.0f, true);
        g.setGradientFill (emberFill (40.0f, 330.0f));
        g.setOpacity (0.55f);
        g.fillPath (fill);
        g.setOpacity (1.0f);
        g.setColour (cEmber);
        dashed (g, poly (pin), 2.0f, 5.0f, 5.0f);
        const auto po = poly (pout);
        glowPath (g, po, juce::Colour (255, 236, 238).withAlpha (0.5f), 8.0f, false);
        g.setColour (cBone.withAlpha (0.85f + 0.15f * std::sin (time() * 2.2f)));
        g.strokePath (po, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved));
        for (int b = 0; b < 3; ++b)
        {
            const auto dot = juce::Rectangle<float> (12.0f, 12.0f).withCentre (handle (b));
            g.setColour (cEmber.withAlpha (0.4f));
            g.fillEllipse (dot.expanded (6.0f));
            g.setColour (cEmber);
            g.fillEllipse (dot);
            g.setColour (juce::Colours::white);
            g.drawEllipse (dot, 1.5f);
        }
    }

    OniriqueVocalBoxAudioProcessor& processor;
    chainmods::Reso* module;
    int k, drag = -1;
    std::array<float, ResonanceSuppressor::kBands> spec {}, red {};
};

//==============================================================================
// MULTIBAND : quatre colonnes ; le niveau de chaque bande, son seuil (glisser dans la bande), la réduction en rideau depuis
// le haut, les coupures (glisser les poignées blanches)
class MultiViz : public Viz
{
public:
    MultiViz (OniriqueVocalBoxAudioProcessor& p, int code) : processor (p), module (dynamic_cast<chainmods::Multi*> (p.module (code))), k (modules::instanceOf (code))
    {
        spec.setRange (20.0f, 20000.0f, 140);
        title = "MULTIBAND · GLISSE LES COUPURES, MONTE OU DESCENDS LE SEUIL DANS UNE BANDE";
        legend = { { "NIVEAU", cMist }, { utf8 ("RÉDUCTION"), cEmber } };
    }
    std::function<void (int)> onBand;
    int band = 1;

    void tick() override
    {
        if (module != nullptr)
            for (int b = 0; b < 4; ++b)
            {
                const float l = module->getMeter().levelDb[(size_t) b].load(), r = std::abs (module->getMeter().grDb[(size_t) b].load());
                lv[b] += 0.3f * (l - lv[b]);
                gr[b] += (r > gr[b] ? 0.5f : 0.12f) * (r - gr[b]);
            }
        if (++frame % 2 == 0) spec.update (processor.outputTap, processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 48000.0);
        const char* names[] { "LOW", "LO-MID", "HI-MID", "HIGH" };
        stat.clear();
        for (int b = 0; b < 4; ++b) stat << names[b] << utf8 (" −") << juce::String (gr[b], 1) << " dB · ";
        stat << hz (get (ids::mbX[0])) << " / " << hz (get (ids::mbX[1])) << " / " << hz (get (ids::mbX[2]));
        repaint();
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto q = toDemo (e.position);
        dragX = -1; dragB = -1;
        const auto xs = edges();
        for (int i = 0; i < 3; ++i) if (std::abs (q.x - xs[(size_t) i + 1]) < 14.0f) dragX = i;
        if (dragX < 0)
            for (int b = 0; b < 4; ++b)
                if (q.x >= xs[(size_t) b] && q.x < xs[(size_t) b + 1]) { dragB = b; band = b; if (onBand) onBand (b); }
        mouseDrag (e);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        const auto q = toDemo (e.position);
        if (dragX >= 0)
        {
            if (auto* p = processor.param (ids::mbX[dragX], k))
            {
                float f = 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, (q.x - 30.0f) / 1340.0f));
                if (dragX > 0) f = std::max (f, get (ids::mbX[dragX - 1]) * 1.3f);
                if (dragX < 2) f = std::min (f, get (ids::mbX[dragX + 1]) / 1.3f);
                p->setValueNotifyingHost (p->convertTo0to1 (f));
            }
        }
        else if (dragB >= 0)
            if (auto* p = processor.param (ids::mbThr[dragB], k))
                p->setValueNotifyingHost (p->convertTo0to1 (juce::jlimit (-60.0f, 0.0f, -(q.y - 30.0f) / 310.0f * 60.0f)));
    }
    void mouseUp (const juce::MouseEvent&) override { dragX = dragB = -1; }
    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto q = toDemo (e.position);
        const auto xs = edges();
        bool onX = false;
        for (int i = 0; i < 3; ++i) onX = onX || std::abs (q.x - xs[(size_t) i + 1]) < 14.0f;
        setMouseCursor (onX ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::UpDownResizeCursor);
    }

private:
    float get (const char* id) const { return processor.apvts.getRawParameterValue (ids::inst (id, k))->load(); }
    static float X (float f) { return 30.0f + 1340.0f * std::log (f / 20.0f) / std::log (1000.0f); }
    static float yd (float db) { return 30.0f + (-db) / 60.0f * 310.0f; }
    std::array<float, 5> edges() const { return { 30.0f, X (get (ids::mbX[0])), X (get (ids::mbX[1])), X (get (ids::mbX[2])), 1370.0f }; }
    void draw (juce::Graphics& g) override
    {
        // le spectre de la sortie, en trait pâle
        std::vector<juce::Point<float>> sp;
        for (size_t i = 0; i < spec.db.size(); ++i)
            sp.push_back ({ 30.0f + 1340.0f * (float) i / (float) (spec.db.size() - 1), juce::jlimit (30.0f, 340.0f, 40.0f + (-20.0f - spec.db[i]) / 80.0f * 300.0f) });
        g.setColour (juce::Colour (244, 236, 236).withAlpha (0.22f));
        g.strokePath (poly (sp), juce::PathStrokeType (2.0f, juce::PathStrokeType::curved));

        const auto xs = edges();
        int solo = -1;
        for (int b = 0; b < 4; ++b) if (get (ids::mbSolo[b]) > 0.5f) solo = b;
        for (int b = 0; b < 4; ++b)
        {
            const float x = xs[(size_t) b], w = xs[(size_t) b + 1] - x;
            const bool on = b == band;
            g.setColour (on ? cEmber.withAlpha (0.10f) : (solo >= 0 && solo != b ? juce::Colours::black.withAlpha (0.35f) : juce::Colours::white.withAlpha (0.015f)));
            g.fillRect (juce::Rectangle<float> (x, 0.0f, w, 360.0f));
            // le rideau de réduction (14 par dB), qui respire
            const float pulse = 0.9f + 0.1f * std::sin (time() * 4.0f + (float) b);
            const float gh = std::min (300.0f, gr[b] * 14.0f * pulse);
            if (gh > 0.5f)
            {
                g.setGradientFill (juce::ColourGradient (cEmber.withAlpha (0.55f), 0.0f, 0.0f, cEmber.withAlpha (0.0f), 0.0f, gh, false));
                g.fillRect (juce::Rectangle<float> (x, 0.0f, w, gh));
            }
            // le niveau de la bande
            const float ly = yd (juce::jlimit (-60.0f, 0.0f, lv[b]));
            g.setColour (juce::Colour (244, 236, 236).withAlpha (0.75f));
            g.fillRoundedRectangle (juce::Rectangle<float> (x + w * 0.5f - 11.0f, ly, 22.0f, std::max (0.0f, 340.0f - ly)), 5.0f);
            // le seuil
            const float ty = yd (get (ids::mbThr[b]));
            juce::Path tl; tl.startNewSubPath (x + 4.0f, ty); tl.lineTo (x + w - 4.0f, ty);
            g.setColour (cPink);
            if (on) g.strokePath (tl, juce::PathStrokeType (2.5f)); else dashed (g, tl, 2.5f, 6.0f, 6.0f);
            const auto dot = juce::Rectangle<float> (16.0f, 16.0f).withCentre ({ x + w - 22.0f, ty });
            g.setColour (cEmber);
            g.fillEllipse (dot);
            g.setColour (juce::Colours::white);
            g.drawEllipse (dot, 2.0f);
        }
        for (int i = 1; i <= 3; ++i)
        {
            const float x = xs[(size_t) i];
            g.setColour (cBone.withAlpha (0.6f));
            g.drawLine (x, 0.0f, x, 360.0f, 2.0f);
            const auto pill = juce::Rectangle<float> (14.0f, 32.0f).withCentre ({ x, 180.0f });
            g.setColour (cBone.withAlpha (0.35f));
            g.fillRoundedRectangle (pill.expanded (4.0f), 9.0f);
            g.setColour (cBone);
            g.fillRoundedRectangle (pill, 7.0f);
        }
    }

    OniriqueVocalBoxAudioProcessor& processor;
    chainmods::Multi* module;
    int k, dragX = -1, dragB = -1, frame = 0;
    float lv[4] { -60.0f, -60.0f, -60.0f, -60.0f }, gr[4] {};
    MiniSpectrum spec;
};

//==============================================================================
// EQ ANALOG : sa courbe (LOW à sa fréquence, MUD qui creuse, HI, AIR, LOW CUT), comme la courbe d'EQ de la démo
class AnalogViz : public Viz
{
public:
    AnalogViz (OniriqueVocalBoxAudioProcessor& p, int code) : processor (p), k (modules::instanceOf (code))
    {
        title = "EQ ANALOG · COULEUR DE CONSOLE";
        legend = { { "COURBE", cEmber } };
    }
    void tick() override
    {
        auto v = [&] (const char* id) { const float x = get (id); return (x > 0.0f ? "+" : "") + juce::String (x, 1); };
        stat = "LOW " + v (ids::low) + " · MUD " + v (ids::mud) + " · HI " + v (ids::high) + " · AIR " + v (ids::air);
        repaint();
    }

private:
    float get (const char* id) const { return processor.apvts.getRawParameterValue (ids::inst (id, k))->load(); }
    float dbAt (float f) const
    {
        static constexpr float lowHz[] { 80.0f, 100.0f, 125.0f };
        const float lf = lowHz[juce::jlimit (0, 2, juce::roundToInt (get (ids::lowFreq)))];
        const float lo = get (ids::low) * 0.6f / (1.0f + std::pow (f / lf, 2.0f));
        const float mud = -get (ids::mud) * 0.45f * std::exp (-std::pow (std::log2 (f / 350.0f) / 0.8f, 2.0f));
        const float hi = get (ids::high) * 0.5f * std::exp (-std::pow (std::log2 (f / 3500.0f) / 1.2f, 2.0f));
        const float air = get (ids::air) * 0.6f / (1.0f + std::pow (12000.0f / f, 2.0f));
        const float cut = get (ids::lowCut) > 0.5f ? -10.0f * std::log10 (1.0f + std::pow (85.0f / f, 6.0f)) : 0.0f;
        return lo + mud + hi + air + cut;
    }
    void draw (juce::Graphics& g) override
    {
        g.setColour (juce::Colours::white.withAlpha (0.14f));
        g.drawHorizontalLine (180, 0.0f, 1400.0f);
        g.setColour (juce::Colours::white.withAlpha (0.04f));
        for (float x : { 140.0f, 420.0f, 700.0f, 980.0f, 1260.0f }) g.drawVerticalLine ((int) x, 0.0f, 360.0f);
        std::vector<juce::Point<float>> pts;
        for (int i = 0; i <= 140; ++i)
        {
            const float f = 20.0f * std::pow (1000.0f, (float) i / 140.0f);
            pts.push_back ({ (float) i * 10.0f, 180.0f - juce::jlimit (-15.0f, 15.0f, dbAt (f)) * 11.0f });
        }
        auto fill = poly (pts);
        fill.lineTo (1400.0f, 360.0f); fill.lineTo (0.0f, 360.0f); fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient (cEmber.withAlpha (0.55f), 0.0f, 0.0f, cEmber.withAlpha (0.0f), 0.0f, 360.0f, false));
        g.fillPath (fill);
        const auto curve = poly (pts);
        glowPath (g, curve, cEmber.withAlpha (0.85f), 10.0f, false);
        g.setColour (cEmber);
        g.strokePath (curve, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved));
        for (float x : { 0.1f, 0.4f, 0.68f, 0.92f })
        {
            const float f = 20.0f * std::pow (1000.0f, x);
            const auto dot = juce::Rectangle<float> (18.0f, 18.0f).withCentre ({ x * 1400.0f, 180.0f - juce::jlimit (-15.0f, 15.0f, dbAt (f)) * 11.0f });
            g.setColour (juce::Colour (0xff120c0e));
            g.fillEllipse (dot);
            g.setColour (cPink);
            g.drawEllipse (dot, 3.0f);
        }
    }
    OniriqueVocalBoxAudioProcessor& processor;
    int k;
};

//==============================================================================
// SATURATION : la courbe de transfert (cadre de 320 à gauche) et un sinus avant (pointillé) / après (braise) à droite
class SatViz : public Viz
{
public:
    SatViz (OniriqueVocalBoxAudioProcessor& p, int code) : processor (p), k (modules::instanceOf (code))
    {
        legend = { { "AVANT", juce::Colour (244, 236, 236).withAlpha (0.5f) }, { utf8 ("APRÈS"), cEmber } };
    }
    void tick() override
    {
        static const char* modes[] { "TUBE", "ANALOG", "TAPE", "TRANSFORMER" };
        static const char* sub[] { "H2 DOMINANT · CHAUD", "H3 · CONSOLE", "COMPRESSION DE BANDE · DOUX", "H2 + H3 · GRAS" };
        const int m = juce::jlimit (0, 3, juce::roundToInt (get (ids::satType)));
        title = juce::String ("SATURATION · ") + modes[m];
        stat = utf8 (sub[m]) + " · DRIVE " + juce::String (get (ids::drive), 1);
        phase += 0.012f;
        repaint();
    }

private:
    float get (const char* id) const { return processor.apvts.getRawParameterValue (ids::inst (id, k))->load(); }
    float fx (float x) const
    {
        const float drive = 0.4f + get (ids::drive) * 0.55f, bias = get (ids::satBias) / 220.0f, mix = get (ids::satMix) * 0.01f;
        const int mode = juce::roundToInt (get (ids::satType));
        float y = mode == 0 ? std::tanh (drive * (x + bias)) - std::tanh (drive * bias)
                : mode == 1 ? std::tanh (drive * x) * (1.0f - 0.08f * x * x)
                : mode == 2 ? std::atan (drive * 1.2f * x) / std::atan (drive * 1.2f) * 0.92f
                            : std::sin (juce::jlimit (-1.5f, 1.5f, drive * 0.7f * (x + bias * 0.6f)));
        y = juce::jlimit (-1.0f, 1.0f, y);
        return x + mix * (y - x);
    }
    void draw (juce::Graphics& g) override
    {
        const auto box = juce::Rectangle<float> (40.0f, 20.0f, 320.0f, 320.0f);
        g.setColour (juce::Colours::black.withAlpha (0.3f));
        g.fillRoundedRectangle (box, 16.0f);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawLine (40.0f, 180.0f, 360.0f, 180.0f, 1.0f);
        g.drawLine (200.0f, 20.0f, 200.0f, 340.0f, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.14f));
        { juce::Path d; d.startNewSubPath (40.0f, 340.0f); d.lineTo (360.0f, 20.0f); dashed (g, d, 1.0f, 4.0f, 6.0f); }
        std::vector<juce::Point<float>> c, pin, pout;
        for (int i = 0; i <= 60; ++i) { const float x = -1.0f + (float) i / 30.0f; c.push_back ({ 200.0f + x * 160.0f, 180.0f - fx (x) * 160.0f }); }
        for (int i = 0; i <= 160; ++i)
        {
            const float x = 0.9f * std::sin ((float) i / 160.0f * 4.0f * kPi + phase);
            pin.push_back ({ 420.0f + (float) i * 6.0f, 180.0f - x * 150.0f });
            pout.push_back ({ 420.0f + (float) i * 6.0f, 180.0f - fx (x) * 150.0f });
        }
        const auto curve = poly (c);
        glowPath (g, curve, cEmber.withAlpha (0.85f), 10.0f, false);
        g.setColour (cEmber);
        g.strokePath (curve, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved));
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawLine (420.0f, 180.0f, 1380.0f, 180.0f, 1.0f);
        g.setColour (juce::Colour (244, 236, 236).withAlpha (0.35f));
        dashed (g, poly (pin), 2.0f, 5.0f, 5.0f);
        g.setColour (cEmber.withAlpha (0.16f));
        g.fillPath (between (pout, pin));
        const auto po = poly (pout);
        glowPath (g, po, cEmber.withAlpha (0.85f), 8.0f, false);
        g.setColour (cEmber);
        g.strokePath (po, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved));
    }
    OniriqueVocalBoxAudioProcessor& processor;
    int k;
    float phase = 0.0f;
};

//==============================================================================
// DELAY : les échos partent vers l'horizon (point de fuite en haut au centre), à gauche et à droite en ping-pong ; leur
// nombre et leur force suivent FEEDBACK et SEND, leur écart la valeur de note
class DelayViz : public Viz
{
public:
    DelayViz (OniriqueVocalBoxAudioProcessor& p, int code) : processor (p), k (modules::instanceOf (code))
    {
        title = utf8 ("DELAY · LES ÉCHOS PARTENT VERS L'HORIZON");
        legend = { { "VOIX", cMist }, { utf8 ("ÉCHOS"), cEmber } };
        std::vector<float> shape;
        for (int i = 0; i <= 30; ++i) shape.push_back (std::pow (std::max (0.0f, std::sin (kPi * (float) i / 30.0f)), 0.8f) * (0.75f + 0.25f * std::sin ((float) i * 1.7f)));
        burst = mirror (shape, -110.0f, 110.0f, 0.0f, 70.0f);
    }
    void tick() override
    {
        const auto names = delayNoteNames();
        const bool sync = get (ids::delaySync) > 0.5f;
        stat = (sync ? names[juce::jlimit (0, names.size() - 1, juce::roundToInt (get (ids::delayNote)))] : juce::String (juce::roundToInt (get (ids::delayTime))) + " ms")
             + " · " + juce::String (count - 1) + utf8 (" ÉCHOS · ") + (get (ids::delayPingPong) > 0.5f ? "PING-PONG" : "MONO")
             + " · " + hz (get (ids::delayLowCut)) + utf8 (" → ") + hz (get (ids::delayHighCut));
        repaint();
    }

private:
    float get (const char* id) const { return processor.apvts.getRawParameterValue (ids::inst (id, k))->load(); }
    void draw (juce::Graphics& g) override
    {
        g.setColour (juce::Colour (255, 140, 150).withAlpha (0.10f));
        for (int x = -1400; x <= 2800; x += 200) g.drawLine ((float) x, 360.0f, 700.0f, 60.0f, 1.0f);
        for (int q = 1; q < 9; ++q) { const float y = 60.0f + 300.0f / (1.0f + (float) q * 0.7f); g.drawHorizontalLine ((int) y, 0.0f, 1400.0f); }
        const bool sync = get (ids::delaySync) > 0.5f;
        const float beats = sync ? delayNoteBeats (juce::roundToInt (get (ids::delayNote))) : get (ids::delayTime) / 500.0f;
        const float fb = get (ids::delayFeedback) * 0.01f, mix = get (ids::delayMix) * 0.01f, step = beats * 1.3f;
        const bool ping = get (ids::delayPingPong) > 0.5f;
        struct E { float x, y, s, op; bool voice; float delay; };
        std::vector<E> ec;
        for (int q = 0; q < 14; ++q)
        {
            const float op = q == 0 ? 1.0f : std::pow (fb, (float) q * 0.55f) * (0.45f + mix * 0.55f);
            if (op < 0.05f) break;
            const float sc = 1.0f / (1.0f + (float) q * step * 0.9f), sd = q == 0 ? 0.0f : (ping ? (q % 2 ? -1.0f : 1.0f) : 0.0f);
            ec.push_back ({ 700.0f + sd * 420.0f * sc, 60.0f + 250.0f * sc, sc, op, q == 0, (float) q * beats * 0.5f });
        }
        count = (int) ec.size();
        const float t = time();
        for (auto it = ec.rbegin(); it != ec.rend(); ++it)
        {
            // chaque écho s'allume à son tour (2.4 s, sommet à 30 %)
            const float ph = std::fmod (t - it->delay + 100.0f, 2.4f) / 2.4f;
            const float pulse = ph < 0.3f ? 0.55f + 0.45f * ph / 0.3f : 1.0f - 0.45f * (ph - 0.3f) / 0.7f;
            auto p = burst;
            p.applyTransform (juce::AffineTransform::scale (it->s).translated (it->x, it->y));
            const auto c = it->voice ? cMist : cEmber;
            glowPath (g, p, cEmber.withAlpha (0.8f * it->op * pulse), 12.0f * it->s, true);
            g.setColour (c.withAlpha (juce::jlimit (0.0f, 1.0f, it->op * pulse)));
            g.fillPath (p);
        }
    }
    OniriqueVocalBoxAudioProcessor& processor;
    int k, count = 1;
    juce::Path burst;
};

//==============================================================================
// REVERB : la voix qui défile et sa queue de reverb derrière (comme VOCAL MINI) ; à droite l'enveloppe de décroissance avec
// le pre-delay ; des étincelles en SHIMMER (et avec la modulation)
class ReverbViz : public Viz
{
public:
    ReverbViz (OniriqueVocalBoxAudioProcessor& p, int code) : processor (p), module (p.module (code)), k (modules::instanceOf (code))
    {
        legend = { { "VOIX", cMist }, { "QUEUE DE REVERB", cEmber } };
        juce::Random r (5);
        for (auto& s : sparks) s = { 40.0f + r.nextFloat() * 940.0f, 180.0f + (r.nextFloat() - 0.5f) * 240.0f, 1.0f + r.nextFloat() * 1.8f, r.nextFloat() * 3.0f };
    }
    void tick() override
    {
        static const char* modes[] { "ROOM", "PLATE 1", "PLATE 2", "HALL", "SHIMMER" };
        const int m = juce::jlimit (0, 4, juce::roundToInt (get (ids::reverbMode)));
        title = juce::String ("REVERB · ") + modes[m] + utf8 (" · LA VOIX ET SA QUEUE, L'ENVELOPPE À DROITE");
        stat = "PRE-DELAY " + juce::String (juce::roundToInt (get (ids::reverbPreDelay))) + " ms · DECAY " + juce::String (get (ids::reverbDecay), 1) + " s · CLEAN "
             + juce::String (juce::roundToInt (get (ids::reverbClean))) + " % · " + hz (get (ids::reverbLowCut)) + utf8 (" → ") + hz (get (ids::reverbHighCut));
        if (module != nullptr) readScope (module->scope, in, out);
        repaint();
    }

private:
    float get (const char* id) const { return processor.apvts.getRawParameterValue (ids::inst (id, k))->load(); }
    void draw (juce::Graphics& g) override
    {
        if (! in.empty())
        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (0, 0, 1000, 360);
            std::vector<float> a (in.size()), b (out.size());
            for (size_t i = 0; i < in.size(); ++i) { a[i] = juce::jlimit (0.0f, 1.0f, in[i] * 1.3f); b[i] = juce::jlimit (0.0f, 1.0f, out[i] * 4.0f); }
            const auto tail = mirror (b, 0.0f, 1000.0f, 180.0f, 150.0f), voice = mirror (a, 0.0f, 1000.0f, 180.0f, 150.0f);
            glowPath (g, tail, cEmber.withAlpha (0.6f), 14.0f, true);
            juce::ColourGradient tg (cEmber.withAlpha (0.75f), 0.0f, 0.0f, juce::Colour (0xffff9aa5).withAlpha (0.35f), 1000.0f, 0.0f, false);
            g.setGradientFill (tg);
            g.fillPath (tail);
            g.setColour (juce::Colour (244, 236, 236).withAlpha (0.95f));
            g.fillPath (voice);
            // étincelles
            const int n = juce::roundToInt (get (ids::reverbMode)) == 4 ? 34 : juce::roundToInt (get (ids::reverbModDepth) / 100.0f * 16.0f);
            const float t = time();
            for (int i = 0; i < std::min (n, (int) sparks.size()); ++i)
            {
                const auto& sp = sparks[(size_t) i];
                const float a2 = 0.5f - 0.5f * std::cos ((t + sp[3]) * 2.0f * kPi / 2.6f);
                g.setColour (juce::Colour (0xffffd6db).withAlpha (a2));
                g.fillEllipse (juce::Rectangle<float> (2.0f * sp[2], 2.0f * sp[2]).withCentre ({ sp[0], sp[1] }));
            }
        }
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillRect (1000.0f, 0.0f, 1.0f, 360.0f);
        // l'enveloppe : plat pendant le pre-delay, puis la décroissance jusqu'à -60 dB
        const float pre = get (ids::reverbPreDelay) / 1000.0f, dec = std::max (0.2f, get (ids::reverbDecay));
        const float T = std::max (1.5f, pre + dec * 1.15f);
        auto X = [&] (float sec) { return 1030.0f + sec / T * 350.0f; };
        auto Y = [] (float db) { return 50.0f + (-db) / 60.0f * 270.0f; };
        std::vector<juce::Point<float>> env { { 1030.0f, 320.0f }, { X (pre), 320.0f }, { X (pre), Y (0.0f) } };
        for (int i = 1; i <= 40; ++i) { const float sec = dec * (float) i / 40.0f; env.push_back ({ X (pre + sec), Y (-60.0f * sec / dec) }); }
        auto fill = poly (env);
        fill.lineTo (1380.0f, 320.0f); fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient (cEmber.withAlpha (0.55f), 0.0f, 40.0f, cEmber.withAlpha (0.0f), 0.0f, 330.0f, false));
        g.fillPath (fill);
        const auto line = poly (env);
        glowPath (g, line, cEmber.withAlpha (0.85f), 8.0f, false);
        g.setColour (cEmber);
        g.strokePath (line, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved));
        g.setColour (cPink);
        { juce::Path m; m.startNewSubPath (X (pre), 40.0f); m.lineTo (X (pre), 330.0f); dashed (g, m, 1.5f, 4.0f, 5.0f); }
    }
    OniriqueVocalBoxAudioProcessor& processor;
    ChainModule* module;
    int k;
    std::vector<float> in, out;
    std::array<std::array<float, 4>, 34> sparks {};
};

//==============================================================================
// DOUBLER : la voix au centre, ses deux doublures (braise) ouvertes à gauche et à droite
class DoublerViz : public Viz
{
public:
    DoublerViz (OniriqueVocalBoxAudioProcessor& p, int code) : processor (p), module (p.module (code)), k (modules::instanceOf (code))
    {
        title = utf8 ("DOUBLER · LA VOIX AU CENTRE, DEUX DOUBLURES OUVERTES EN STÉRÉO");
        legend = { { "VOIX", cMist }, { "DOUBLURES G / D", cEmber } };
    }
    void tick() override
    {
        if (module != nullptr) readScope (module->scope, in, out);
        stat = "AMOUNT " + juce::String (juce::roundToInt (get (ids::doublerAmount))) + " %";
        repaint();
    }

private:
    float get (const char* id) const { return processor.apvts.getRawParameterValue (ids::inst (id, k))->load(); }
    void draw (juce::Graphics& g) override
    {
        if (in.empty()) return;
        const float amt = get (ids::doublerAmount) * 0.01f, off = 46.0f + 0.75f * 80.0f;
        const size_t n = in.size();
        std::vector<float> c (n), l (n), r (n);
        for (size_t i = 0; i < n; ++i)
        {
            c[i] = juce::jlimit (0.0f, 1.0f, in[i] * 1.3f * 0.95f);
            l[i] = juce::jlimit (0.0f, 1.0f, in[(i + n - 4) % n] * 1.3f * (0.9f + 0.1f * std::sin ((float) i * 0.3f)));
            r[i] = juce::jlimit (0.0f, 1.0f, in[(i + n - 8) % n] * 1.3f * (0.9f + 0.1f * std::sin ((float) i * 0.3f + 1.0f)));
        }
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawHorizontalLine ((int) (180.0f - off), 0.0f, 1400.0f);
        g.drawHorizontalLine ((int) (180.0f + off), 0.0f, 1400.0f);
        const float op = 0.25f + amt * 0.75f;
        for (const auto* lane : { &l, &r })
        {
            const auto p = mirror (*lane, 0.0f, 1400.0f, lane == &l ? 180.0f - off : 180.0f + off, 50.0f);
            glowPath (g, p, cEmber.withAlpha (0.7f * op), 8.0f, true);
            g.setGradientFill (emberFill (0.0f, 360.0f));
            g.setOpacity (op);
            g.fillPath (p);
            g.setOpacity (1.0f);
        }
        g.setColour (juce::Colour (244, 236, 236).withAlpha (0.92f));
        g.fillPath (mirror (c, 0.0f, 1400.0f, 180.0f, 70.0f));
    }
    OniriqueVocalBoxAudioProcessor& processor;
    ChainModule* module;
    int k;
    std::vector<float> in, out;
};

// CHORUS, FLANGER, PHONE : leurs visuels animés d'avant, dans le cadre de la démo
class FxViz : public Viz
{
public:
    FxViz (OniriqueVocalBoxAudioProcessor& p, int code, FxVisual::Kind kind, juce::String t) : vis (p, kind, modules::instanceOf (code))
    {
        title = std::move (t);
        addAndMakeVisible (vis);
    }
    void resized() override { vis.setBounds (getLocalBounds().reduced (juce::roundToInt (16.0f * ui::unitOf (*this)), juce::roundToInt (34.0f * ui::unitOf (*this)))); }
    void tick() override { vis.tick(); }

private:
    void draw (juce::Graphics&) override {}
    FxVisual vis;
};

//==============================================================================
// VU crème gradué : réduction de gain (GR) ou niveau de sortie (+8 / +4, +10 / +4) ; zone rouge 0 .. +3 sur l'arc
class VintageVu : public juce::Component, public Animated
{
public:
    explicit VintageVu (std::function<float()> vuDb) : read (std::move (vuDb)) { setInterceptsMouseClicks (false, false); }

    void tick() override
    {
        if (ui::approach (angle, angleFor (read ? read() : -30.0f), 0.16f)) repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        // la boîte : 10 de marge, rayon 16, fond #050405, liseré blanc .12, lueur braise de 30, ombre intérieure
        const float s = std::min (b.getWidth() / 320.0f, b.getHeight() / 195.0f);
        const auto frame = juce::Rectangle<float> (320.0f * s, 195.0f * s).withCentre (b.getCentre());
        for (int k = 3; k >= 1; --k)
        {
            g.setColour (juce::Colour (229, 56, 74).withAlpha (0.06f / (float) k));
            g.fillRoundedRectangle (frame.expanded (10.0f * s * (float) k), 16.0f * s + 10.0f * s * (float) k);
        }
        g.setColour (juce::Colour (0xff050405));
        g.fillRoundedRectangle (frame, 16.0f * s);
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.drawRoundedRectangle (frame.reduced (0.5f), 16.0f * s, 1.0f);

        const auto face = frame.reduced (10.0f * s);
        const float k = face.getWidth() / 300.0f;
        auto P = [&] (float x, float y) { return juce::Point<float> (face.getX() + x * k, face.getY() + y * k); };
        auto polar = [&] (float r, float deg) { const float a = deg * kPi / 180.0f; return P (150.0f + r * std::sin (a), 165.0f - r * std::cos (a)); };
        juce::ColourGradient fg (juce::Colour (0xfffff1c8), P (150, 175).x, P (150, 175).y, juce::Colour (0xffc79a4c), P (150, 175).x + 315.0f * k, P (150, 175).y, true);
        fg.addColour (0.6, juce::Colour (0xffefcf8e));
        g.setGradientFill (fg);
        g.fillRoundedRectangle (face, 10.0f * k);

        const juce::Colour ink (0xff2a2016), red (0xffc0301f);
        auto arc = [&] (float r, float a0, float a1, juce::Colour c, float w)
        {
            juce::Path p;
            for (int i = 0; i <= 40; ++i) { const auto q = polar (r, a0 + (a1 - a0) * (float) i / 40.0f); if (i == 0) p.startNewSubPath (q); else p.lineTo (q); }
            g.setColour (c);
            g.strokePath (p, juce::PathStrokeType (w * k));
        };
        arc (118.0f, -48.0f, 48.0f, ink, 1.6f);
        arc (121.2f, 20.0f, 48.0f, red, 5.0f);
        auto tickAt = [&] (float r1, float r2, float a, juce::Colour c, float w) { g.setColour (c); g.drawLine ({ polar (r1, a), polar (r2, a) }, w * k); };
        for (float a : { -44.0f, -40.0f, -36.0f, -33.0f, -27.0f, -24.0f, -17.0f, -14.0f, -8.0f, -5.0f, -3.0f, 3.0f, 9.0f, 15.0f, 24.0f, 32.0f, 41.0f, 44.0f })
            tickAt (118.0f, 124.0f, a, a > 20.0f ? red : ink, 1.0f);
        const std::pair<const char*, float> major[] { { "20", -48.0f }, { "10", -30.0f }, { "7", -20.0f }, { "5", -11.0f }, { "3", 0.0f }, { "2", 6.0f },
                                                      { "1", 12.0f }, { "0", 20.0f }, { "+1", 28.0f }, { "+2", 37.0f }, { "+3", 48.0f } };
        g.setFont (ui::css (ui::Face::body, 11.0f * k));
        for (const auto& [l, a] : major)
        {
            const auto c = a >= 20.0f ? red : ink;
            tickAt (118.0f, 128.0f, a, c, 1.8f);
            g.setColour (c);
            g.drawText (l, juce::Rectangle<float> (30.0f * k, 14.0f * k).withCentre (polar (138.0f, a)), juce::Justification::centred, false);
        }
        g.setFont (ui::css (ui::Face::mono, 7.0f * k));
        for (const auto& [l, a] : std::initializer_list<std::pair<const char*, float>> { { "0", -48.0f }, { "20", -30.0f }, { "40", -14.0f }, { "60", 0.0f }, { "80", 10.0f }, { "100", 20.0f } })
        {
            tickAt (110.0f, 115.0f, a, ink, 0.8f);
            g.setColour (juce::Colour (0xff5a4630));
            g.drawText (l, juce::Rectangle<float> (24.0f * k, 10.0f * k).withCentre (polar (102.0f, a)), juce::Justification::centred, false);
        }
        g.setColour (ink);
        g.setFont (ui::css (ui::Face::display, 9.0f * k, 0.2f));
        g.drawText ("GAIN REDUCTION", juce::Rectangle<float> (200.0f * k, 14.0f * k).withCentre (P (150, 146)), juce::Justification::centred, false);

        const auto pivot = P (150, 165), tip = polar (113.0f, angle);
        g.setColour (juce::Colour (0xff111111));
        g.drawLine ({ pivot, tip }, 2.2f * k);
        g.setColour (juce::Colour (0xff1d1d1f));
        g.fillEllipse (juce::Rectangle<float> (22.0f * k, 22.0f * k).withCentre (pivot));
        juce::Path glass; glass.addRoundedRectangle (face, 10.0f * k);
        juce::ColourGradient gl (juce::Colours::white.withAlpha (0.35f), 0.0f, face.getY(), juce::Colours::black.withAlpha (0.2f), 0.0f, face.getBottom(), false);
        gl.addColour (0.5, juce::Colours::white.withAlpha (0.03f));
        g.setGradientFill (gl);
        g.fillPath (glass);
    }

private:
    // dB de VU (-20 .. +3) -> angle de l'aiguille sur l'échelle (0 VU à +20°)
    static float angleFor (float vu)
    {
        static const float db[] { -30.0f, -20.0f, -10.0f, -7.0f, -5.0f, -3.0f, -2.0f, -1.0f, 0.0f, 1.0f, 2.0f, 3.0f, 6.0f };
        static const float an[] { -52.0f, -48.0f, -30.0f, -20.0f, -11.0f, 0.0f, 6.0f, 12.0f, 20.0f, 28.0f, 37.0f, 48.0f, 52.0f };
        vu = juce::jlimit (-30.0f, 6.0f, vu);
        for (int i = 0; i < 12; ++i)
            if (vu <= db[i + 1]) return an[i] + (an[i + 1] - an[i]) * (vu - db[i]) / (db[i + 1] - db[i]);
        return 52.0f;
    }
    std::function<float()> read;
    float angle = 20.0f;
};

// la compression en direct (bas des façades) : la voix qui entre (pâle), qui sort (blanche), la réduction en braise
class CompStrip : public juce::Component, public Animated
{
public:
    explicit CompStrip (ChainModule* m) : module (m) { setInterceptsMouseClicks (false, false); }
    void tick() override
    {
        if (module == nullptr) return;
        gr[(size_t) pos] = std::abs (module->getReductionDb());
        pos = (pos + 1) % (int) gr.size();
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        const float u = ui::unitOf (*this);
        const auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.42f));
        g.fillRoundedRectangle (r, 14.0f * u);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawRoundedRectangle (r.reduced (0.5f), 14.0f * u, 1.0f);
        if (module == nullptr) return;
        juce::Graphics::ScopedSaveState s (g);
        juce::Path clip; clip.addRoundedRectangle (r, 14.0f * u);
        g.reduceClipRegion (clip);
        // repère de la démo : 1400 x 112, étiré (preserveAspectRatio none)
        g.addTransform (juce::AffineTransform::scale (r.getWidth() / 1400.0f, r.getHeight() / 112.0f).translated (r.getX(), r.getY()));
        std::vector<float> in, out;
        readScope (module->scope, in, out);
        auto norm = [] (float a) { return juce::jlimit (0.0f, 1.0f, (dbOf (a) + 50.0f) / 50.0f); };
        std::vector<float> a (in.size()), b (out.size());
        for (size_t i = 0; i < in.size(); ++i) { a[i] = norm (in[i]); b[i] = norm (out[i]); }
        g.setColour (juce::Colour (244, 236, 236).withAlpha (0.13f));
        g.fillPath (mirror (a, 0.0f, 1400.0f, 62.0f, 46.0f));
        g.setColour (juce::Colour (244, 236, 236).withAlpha (0.85f));
        g.fillPath (mirror (b, 0.0f, 1400.0f, 62.0f, 46.0f));
        juce::Path curtain;
        curtain.startNewSubPath (0.0f, 0.0f);
        const int m = (int) gr.size();
        for (int i = 0; i < m; ++i) curtain.lineTo (1400.0f * (float) i / (float) (m - 1), std::min (56.0f, gr[(size_t) ((pos + i) % m)] * 4.0f));
        curtain.lineTo (1400.0f, 0.0f);
        curtain.closeSubPath();
        g.setGradientFill (juce::ColourGradient (cEmber.withAlpha (0.85f), 0.0f, 0.0f, cEmber.withAlpha (0.05f), 0.0f, 112.0f, false));
        g.fillPath (curtain);
        g.setColour (cEmber.withAlpha (0.5f));
        g.strokePath (curtain, juce::PathStrokeType (2.0f));
    }
    void paintOverChildren (juce::Graphics& g) override
    {
        const float u = ui::unitOf (*this);
        const auto r = getLocalBounds().toFloat().reduced (14.0f * u, 10.0f * u);
        float mx = 0.0f;
        for (float v : gr) mx = std::max (mx, v);
        g.setFont (ui::css (ui::Face::mono, 9.0f * u, 0.12f));
        g.setColour (juce::Colour (0xffd9b8bd));
        g.drawText (utf8 ("RÉDUCTION DE GAIN"), r, juce::Justification::topLeft, false);
        g.setColour (juce::Colours::white);
        g.drawText (utf8 ("−") + juce::String (mx, 1) + " dB MAX", r, juce::Justification::topRight, false);
    }

private:
    ChainModule* module = nullptr;
    std::array<float, 260> gr {};
    int pos = 0;
};

//==============================================================================
// Façade des compresseurs (1176, LA-2A, PARALLEL) : la première version, sur toute la place du module
class FacePanel final : public ModulePanel
{
public:
    FacePanel (OniriqueVocalBoxAudioProcessor& p, int c, juce::String t, juce::String sub, juce::StringArray meterLabels)
        : ModulePanel (p, c), title (std::move (t)), subtitle (std::move (sub)), meter (meterLabels),
          vu ([this] { return vuValue(); }), strip (engine)
    {
        addAndMakeVisible (vu);  anim.push_back (&vu);
        addAndMakeVisible (strip); anim.push_back (&strip);
        addAndMakeVisible (meter); anim.push_back (&meter);
        rows.push_back ({ "METER", &meter });
    }
    juce::String title, subtitle;
    std::vector<StarKnob*> knobs;
    std::vector<std::pair<juce::String, juce::Component*>> rows;   // RATIO / MODE, puis METER, sous le VU
    int cols = 4;
    float maxKnob = 230.0f;
    GlassTabs meter;

    void addRowFirst (const juce::String& label, juce::Component& c) { rows.insert (rows.begin(), { label, &c }); }

    void resized() override
    {
        const float u = ui::unitOf (*this);
        // padding 30 / 40 ; la bande de compression (112) en bas, 16 au-dessus ; colonne du VU de 420, 40 d'écart
        auto r = getLocalBounds().toFloat().reduced (40.0f * u, 30.0f * u);
        strip.setBounds (r.removeFromBottom (112.0f * u).toNearestInt());
        r.removeFromBottom (16.0f * u);
        auto left = r.removeFromLeft (420.0f * u);
        r.removeFromLeft (40.0f * u);
        // colonne : titre (20), 14, VU, 14, rangées de 42 séparées de 14 ; le tout centré
        const float rowsH = (float) rows.size() * 42.0f * u + (float) rows.size() * 14.0f * u;
        const float vuH = std::min (left.getWidth() * 195.0f / 320.0f, left.getHeight() - 34.0f * u - rowsH);
        const float total = 20.0f * u + 14.0f * u + vuH + rowsH;
        float y = left.getY() + (left.getHeight() - total) * 0.5f;
        headY = y;
        y += 34.0f * u;
        vu.setBounds (juce::Rectangle<float> (left.getX(), y, left.getWidth(), vuH).toNearestInt());
        y += vuH + 14.0f * u;
        for (auto& [label, comp] : rows)
        {
            comp->setBounds (juce::Rectangle<float> (left.getX() + 66.0f * u, y, left.getWidth() - 66.0f * u, 42.0f * u).toNearestInt());
            y += 56.0f * u;
        }
        // les potards : grille, chacun au plus maxKnob
        const int n = (int) knobs.size();
        const int cl = std::max (1, std::min (cols, n));
        const int rws = (n + cl - 1) / cl;
        const float cw = (r.getWidth() - 18.0f * u * (float) (cl - 1)) / (float) cl, chH = r.getHeight() / (float) rws;
        for (int i = 0; i < n; ++i)
        {
            const auto cell = juce::Rectangle<float> (r.getX() + (float) (i % cl) * (cw + 18.0f * u), r.getY() + (float) (i / cl) * chH, cw, chH);
            const float w = std::min (cw, maxKnob * u);
            const float h = std::min (chH, w + (18.0f + 14.0f + 14.0f + 30.0f + 16.0f) * u);
            knobs[(size_t) i]->setBounds (juce::Rectangle<float> (w, h).withCentre (cell.getCentre()).toNearestInt());
        }
    }

    void paint (juce::Graphics& g) override
    {
        const float u = ui::unitOf (*this);
        const auto r = getLocalBounds().toFloat();
        const float rad = 22.0f * u;
        // ombre 0 30 70 noir .6
        for (int k = 3; k >= 1; --k)
        {
            g.setColour (juce::Colours::black.withAlpha (0.15f / (float) k));
            g.fillRoundedRectangle (r.translated (0.0f, 20.0f * u).expanded (12.0f * u * (float) k), rad + 12.0f * u * (float) k);
        }
        juce::ColourGradient bg (juce::Colour (0xff24181b), r.getX() + r.getWidth() * 0.2f, r.getY(), juce::Colour (0xff0a090a),
                                 r.getX() + r.getWidth() * 0.2f + r.getWidth() * 1.2f, r.getY(), true);
        bg.addColour (0.45, juce::Colour (0xff121012));
        g.setGradientFill (bg);
        g.fillRoundedRectangle (r, rad);
        {
            juce::Graphics::ScopedSaveState s (g);
            juce::Path clip; clip.addRoundedRectangle (r, rad);
            g.reduceClipRegion (clip);
            // reflet en biais (115°) et lueur braise floue en bas à gauche
            juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.0f), r.getX() + r.getWidth() * 0.30f, r.getY(),
                                        juce::Colours::white.withAlpha (0.0f), r.getX() + r.getWidth() * 0.60f, r.getBottom(), false);
            sheen.addColour (0.5, juce::Colours::white.withAlpha (0.045f));
            g.setGradientFill (sheen);
            g.fillRect (r);
            const juce::Point<float> gc (r.getX() + 140.0f * u, r.getBottom() + 20.0f * u);
            juce::ColourGradient blob (cEmber.withAlpha (0.16f), gc.x, gc.y, cEmber.withAlpha (0.0f), gc.x + 330.0f * u, gc.y, true);
            g.setGradientFill (blob);
            g.fillEllipse (juce::Rectangle<float> (660.0f * u, 500.0f * u).withCentre (gc));
        }
        // les liserés : argent 2, sombre 4, clair 5
        g.setColour (juce::Colour (0xff8d8f96));
        g.drawRoundedRectangle (r.reduced (1.0f * u), rad - 1.0f * u, 2.0f * u);
        g.setColour (juce::Colour (0xff1d1d21));
        g.drawRoundedRectangle (r.reduced (3.0f * u), rad - 3.0f * u, 2.0f * u);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawRoundedRectangle (r.reduced (4.5f * u), rad - 4.5f * u, 1.0f * u);

        const auto head = juce::Rectangle<float> (40.0f * u, headY, 420.0f * u, 20.0f * u);
        g.setColour (juce::Colour (0xffe9e9ec));
        g.setFont (ui::css (ui::Face::display, 15.0f * u, 0.3f));
        g.drawText (title, head, juce::Justification::centredLeft, false);
        g.setColour (juce::Colour (0xff8c8d93));
        g.setFont (ui::css (ui::Face::mono, 10.0f * u, 0.12f));
        g.drawText (subtitle, head, juce::Justification::centredRight, false);
        g.setColour (juce::Colour (0xff9a9ca3));
        for (auto& [label, comp] : rows)
            g.drawText (label, comp->getBounds().toFloat().withX (40.0f * u).withWidth (52.0f * u), juce::Justification::centredLeft, false);
    }

private:
    // GR, ou le niveau de sortie rapporté au VU (+8 / +4 : la même sortie lue 8 ou 4 dB plus bas), ou OFF (aiguille au repos)
    float vuValue()
    {
        if (engine == nullptr) return -30.0f;
        const int m = meter.getSelected();
        const bool opto = type == modules::opto;
        if (m == 0) return -std::abs (engine->getReductionDb());
        if (! opto && m == 3) return -30.0f;
        float pk = 0.0f;
        for (int i = 0; i < 6; ++i)
        {
            const int w = (engine->scope.write.load() - 1 - i + ModuleScope::kCols) % ModuleScope::kCols;
            pk = std::max (pk, engine->scope.out[w].load());
        }
        const float ref = opto ? (m == 1 ? 10.0f : 4.0f) : (m == 1 ? 8.0f : 4.0f);
        return dbOf (pk) + 18.0f - ref;
    }
    VintageVu vu;
    CompStrip strip;
    float headY = 0.0f;
};

//==============================================================================
/*  La disposition standard de la démo : une rangée d'onglets et de pastilles (verre de 42), le visuel, puis la grille des
    potards (autant de colonnes que de potards jusqu'à 8, sinon 6 ; potards de 120, 104, 92 ou 70). */
class StdPanel final : public ModulePanel
{
public:
    using ModulePanel::ModulePanel;
    Viz* viz = nullptr;
    std::vector<juce::Component*> topRow;
    juce::String note;
    std::vector<StarKnob*> knobs;

    void resized() override
    {
        const float u = ui::unitOf (*this);
        auto r = getLocalBounds().toFloat();
        if (! topRow.empty())
        {
            auto row = r.removeFromTop (42.0f * u);
            float x = row.getX();
            for (auto* c : topRow)
            {
                float w = 0.0f;
                if (auto* t = dynamic_cast<GlassTabs*> (c)) w = t->idealWidth (0.0f);
                else if (auto* gt = dynamic_cast<GlassToggle*> (c)) w = gt->idealWidth();
                c->setBounds (juce::Rectangle<float> (x, row.getY(), w, row.getHeight()).toNearestInt());
                x += w + 14.0f * u;
            }
            noteArea = row.withLeft (x);
            r.removeFromTop (14.0f * u);
        }
        const int n = (int) knobs.size();
        const int cols = n > 8 ? 6 : std::max (1, n);
        const int rows = (n + cols - 1) / cols;
        const float size = n <= 4 ? 120.0f : n <= 6 ? 104.0f : n <= 8 ? 92.0f : 70.0f;
        const float cellH = (size + 53.0f) * u, gap = 10.0f * u;
        if (n > 0)
        {
            auto grid = r.removeFromBottom (cellH * (float) rows + gap * (float) (rows - 1));
            r.removeFromBottom (14.0f * u);
            const float cw = (grid.getWidth() - gap * (float) (cols - 1)) / (float) cols;
            for (int i = 0; i < n; ++i)
                knobs[(size_t) i]->setBounds (juce::Rectangle<float> (grid.getX() + (float) (i % cols) * (cw + gap), grid.getY() + (float) (i / cols) * (cellH + gap), cw, cellH).toNearestInt());
        }
        if (viz != nullptr) viz->setBounds (r.toNearestInt());
    }
    void paint (juce::Graphics& g) override
    {
        if (note.isEmpty()) return;
        const float u = ui::unitOf (*this);
        g.setColour (juce::Colour (0xffa8939a));
        g.setFont (ui::css (ui::Face::mono, 10.0f * u, 0.12f));
        g.drawText (note, noteArea, juce::Justification::centredRight, false);
    }

private:
    juce::Rectangle<float> noteArea;
};

//==============================================================================
// PARAMETRIC EQ : les 8 bandes en pastilles (.bchip), l'analyseur d'ONIRIQUE EQ, et la rangée de la bande choisie
class BandChip : public juce::Component, public Animated
{
public:
    BandChip (OniriqueVocalBoxAudioProcessor& p, int k, int b) : apvts (p.apvts), inst (k), band (b) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    std::function<void()> onClick;
    bool selected = false;
    void tick() override
    {
        const bool on = get (ids::peqOn[band]) > 0.5f;
        bool moved = ui::approach (lit, selected ? 1.0f : 0.0f, 0.16f);
        moved |= ui::approach (led, on ? 1.0f : 0.0f, 0.16f);
        moved |= ui::approach (hov, isMouseOver() ? 1.0f : 0.0f, 0.16f);
        if (moved || typeShown != juce::roundToInt (get (ids::peqType[band]))) { typeShown = juce::roundToInt (get (ids::peqType[band])); repaint(); }
    }
    void paint (juce::Graphics& g) override
    {
        const float u = ui::unitOf (*this);
        const auto r = getLocalBounds().toFloat();
        ui::drawSeg (g, r, lit, hov, u);
        const auto dot = juce::Rectangle<float> (7.0f * u, 7.0f * u).withPosition (r.getX() + 10.0f * u, r.getCentreY() - 3.5f * u);
        if (led > 0.01f) { g.setColour (cEmber.withAlpha (0.4f * led)); g.fillEllipse (dot.expanded (3.0f * u)); }
        g.setColour (cEmber.interpolatedWith (juce::Colour (0xff4a3a3e), 1.0f - led));
        g.fillEllipse (dot);
        g.setColour (juce::Colour (0xffd8c6c8).interpolatedWith (juce::Colours::white, std::max (lit, hov)));
        g.setFont (ui::css (ui::Face::display, 11.0f * u));
        g.drawText (juce::String (band + 1), juce::Rectangle<float> (dot.getRight() + 6.0f * u, r.getY(), 14.0f * u, r.getHeight()), juce::Justification::centredLeft, false);
        static const char* types[] { "BELL", "LO SH", "HI SH", "LO CUT", "HI CUT", "NOTCH" };
        g.setColour (juce::Colour (0xffa8939a));
        g.setFont (ui::css (ui::Face::mono, 8.0f * u, 0.12f));
        g.drawText (led > 0.5f ? types[juce::jlimit (0, 5, typeShown)] : "OFF", juce::Rectangle<float> (dot.getRight() + 22.0f * u, r.getY(), r.getRight() - dot.getRight() - 24.0f * u, r.getHeight()),
                    juce::Justification::centredLeft, false);
    }
    void mouseDown (const juce::MouseEvent&) override
    {
        if (get (ids::peqOn[band]) < 0.5f)
            if (auto* p = apvts.getParameter (ids::inst (ids::peqOn[band], inst))) p->setValueNotifyingHost (1.0f);
        if (onClick) onClick();
    }

private:
    float get (const char* id) const { return apvts.getRawParameterValue (ids::inst (id, inst))->load(); }
    juce::AudioProcessorValueTreeState& apvts;
    int inst, band, typeShown = -1;
    float lit = 0.0f, led = 0.0f, hov = 0.0f;
};

// les six types d'une bande, en grille de 3 x 2 (.seg de 26, JetBrains Mono 9)
class TypeGrid : public juce::Component, public Animated
{
public:
    TypeGrid (juce::AudioProcessorValueTreeState& s, const juce::String& id)
    {
        param = s.getParameter (id);
        attachment = std::make_unique<juce::ParameterAttachment> (*param, [this] (float v) { sel = juce::roundToInt (v); repaint(); });
        attachment->sendInitialUpdate();
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }
    void tick() override
    {
        bool moved = false;
        for (int i = 0; i < 6; ++i) moved |= ui::approach (lit[i], i == sel ? 1.0f : 0.0f, 0.16f), moved |= ui::approach (hov[i], i == hover ? 1.0f : 0.0f, 0.16f);
        if (moved) repaint();
    }
    juce::Rectangle<float> cell (int i) const
    {
        const float u = ui::unitOf (*this);
        const auto r = getLocalBounds().toFloat().reduced (4.0f * u);
        const float w = (r.getWidth() - 6.0f * u) / 3.0f, h = (r.getHeight() - 3.0f * u) / 2.0f;
        return { r.getX() + (float) (i % 3) * (w + 3.0f * u), r.getY() + (float) (i / 3) * (h + 3.0f * u), w, h };
    }
    void paint (juce::Graphics& g) override
    {
        const float u = ui::unitOf (*this);
        ui::drawDemoGlass (g, getLocalBounds().toFloat(), 14.0f * u, u, false);
        static const char* names[] { "BELL", "LOW SHELF", "HIGH SHELF", "LOW CUT", "HIGH CUT", "NOTCH" };
        g.setFont (ui::css (ui::Face::mono, 9.0f * u, 0.14f));
        for (int i = 0; i < 6; ++i)
        {
            ui::drawSeg (g, cell (i), lit[i], hov[i], u);
            g.setColour (juce::Colour (0xffbba7aa).interpolatedWith (juce::Colours::white, std::max (lit[i], hov[i])));
            g.drawText (names[i], cell (i), juce::Justification::centred, false);
        }
    }
    void mouseMove (const juce::MouseEvent& e) override { int h = -1; for (int i = 0; i < 6; ++i) if (cell (i).contains (e.position)) h = i; hover = h; }
    void mouseExit (const juce::MouseEvent&) override { hover = -1; }
    void mouseDown (const juce::MouseEvent& e) override { for (int i = 0; i < 6; ++i) if (cell (i).contains (e.position)) attachment->setValueAsCompleteGesture ((float) i); }

private:
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    int sel = 0, hover = -1;
    float lit[6] {}, hov[6] {};
};

class PeqPanel final : public ModulePanel
{
public:
    PeqPanel (OniriqueVocalBoxAudioProcessor& p, int c) : ModulePanel (p, c), analyzer (p, inst, false)
    {
        for (int b = 0; b < 8; ++b)
        {
            auto& chip = own (std::make_unique<BandChip> (p, inst, b));
            chip.onClick = [this, b] { selectBand (b); };
            chips.push_back (&chip);
            auto& s = sets[(size_t) b];
            s.types = &own (std::make_unique<TypeGrid> (p.apvts, P (ids::peqType[b])));
            s.freq = &knob (ids::peqFreq[b], "FREQ");
            s.gain = &knob (ids::peqGain[b], "GAIN");
            s.q = &knob (ids::peqQ[b], "Q");
            s.thr = &knob (ids::peqThr[b], "THRESHOLD");
            s.dyn = &toggle (ids::peqDyn[b], "DYNAMIQUE");
        }
        listen = &toggle (ids::peqListen, "LISTEN");
        addAndMakeVisible (analyzer); anim.push_back (&analyzer);
        analyzer.onSelect = [this] (int b) { selectBand (b); };
        selectBand (2);
    }
    void tick() override
    {
        ModulePanel::tick();
        const auto& s = sets[(size_t) selected];
        const int t = juce::roundToInt (value (ids::peqType[selected]));
        s.gain->setDimmed (! ParametricEQ::hasGain (t));
        s.thr->setDimmed (value (ids::peqDyn[selected]) < 0.5f);
    }
    void selectBand (int b)
    {
        selected = juce::jlimit (0, 7, b);
        analyzer.setSelectedSilently (selected);
        for (int k = 0; k < 8; ++k)
        {
            chips[(size_t) k]->selected = k == selected;
            auto& s = sets[(size_t) k];
            for (juce::Component* c : { (juce::Component*) s.types, (juce::Component*) s.freq, (juce::Component*) s.gain, (juce::Component*) s.q,
                                        (juce::Component*) s.thr, (juce::Component*) s.dyn })
                c->setVisible (k == selected);
        }
        resized();
        repaint();
    }
    void resized() override
    {
        const float u = ui::unitOf (*this);
        auto r = getLocalBounds().toFloat();
        // rangée des pastilles : verre de 38 (4 de marge, pastilles de 30)
        auto top = r.removeFromTop (38.0f * u);
        listen->setBounds (top.removeFromRight (140.0f * u).toNearestInt());
        chipBox = top.withWidth (std::min (top.getWidth() - 20.0f * u, 8.0f * 118.0f * u + 8.0f * u));
        const float cw = (chipBox.getWidth() - 8.0f * u - 7.0f * 4.0f * u) / 8.0f;
        for (int b = 0; b < 8; ++b)
            chips[(size_t) b]->setBounds (juce::Rectangle<float> (chipBox.getX() + 4.0f * u + (float) b * (cw + 4.0f * u), chipBox.getY() + 4.0f * u, cw, 30.0f * u).toNearestInt());
        r.removeFromTop (12.0f * u);
        // la rangée de la bande : verre de rayon 22, 10 / 16 de marge
        bandRow = r.removeFromBottom (128.0f * u);
        r.removeFromBottom (12.0f * u);
        analyzer.setBounds (r.toNearestInt());
        auto row = bandRow.reduced (16.0f * u, 10.0f * u);
        nameArea = row.removeFromLeft (150.0f * u);
        row.removeFromLeft (16.0f * u);
        auto& s = sets[(size_t) selected];
        s.types->setBounds (row.removeFromLeft (380.0f * u).withSizeKeepingCentre (380.0f * u, 70.0f * u).toNearestInt());
        row.removeFromLeft (16.0f * u);
        s.dyn->setBounds (row.removeFromRight (150.0f * u).withSizeKeepingCentre (150.0f * u, 42.0f * u).toNearestInt());
        row.removeFromRight (16.0f * u);
        const float kw = 96.0f * u;
        float x = row.getCentreX() - 2.0f * kw - 1.5f * 8.0f * u;
        for (auto* k : { s.freq, s.gain, s.q, s.thr })
        {
            const float w = k == s.gain ? 112.0f * u : kw;
            k->setBounds (juce::Rectangle<float> (x, row.getY() - 6.0f * u, w, row.getHeight() + 12.0f * u).toNearestInt());
            x += w + 8.0f * u;
        }
    }
    void paint (juce::Graphics& g) override
    {
        const float u = ui::unitOf (*this);
        ui::drawDemoGlass (g, chipBox, 14.0f * u, u, false);
        ui::drawDemoGlass (g, bandRow, 22.0f * u, u, true);
        // ‹ B3 ›, le nom et l'état
        const auto a = nameArea;
        g.setColour (juce::Colour (0xffd8c6c8));
        g.setFont (ui::css (ui::Face::mono, 14.0f * u));
        g.drawText (juce::String::charToString (0x2039), a.withWidth (24.0f * u), juce::Justification::centred, false);
        g.drawText (juce::String::charToString (0x203a), a.withX (a.getRight() - 24.0f * u).withWidth (24.0f * u), juce::Justification::centred, false);
        static const char* types[] { "BELL", "LOW SHELF", "HIGH SHELF", "LOW CUT", "HIGH CUT", "NOTCH" };
        const bool on = value (ids::peqOn[selected]) > 0.5f, dyn = value (ids::peqDyn[selected]) > 0.5f;
        g.setColour (cBone);
        g.setFont (ui::css (ui::Face::display, 14.0f * u));
        g.drawText ("B" + juce::String (selected + 1), a.withTrimmedBottom (a.getHeight() * 0.5f), juce::Justification::centredBottom, false);
        g.setColour (juce::Colour (0xffa8939a));
        g.setFont (ui::css (ui::Face::mono, 9.0f * u, 0.12f));
        g.drawText (on ? (dyn ? juce::String ("DYNAMIQUE") : juce::String (types[juce::jlimit (0, 5, juce::roundToInt (value (ids::peqType[selected])))]))
                       : utf8 ("ÉTEINTE"), a.withTrimmedTop (a.getHeight() * 0.5f + 4.0f * u), juce::Justification::centredTop, false);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (nameArea.withWidth (30.0f * ui::unitOf (*this)).contains (e.position)) selectBand ((selected + 7) % 8);
        else if (nameArea.withX (nameArea.getRight() - 30.0f * ui::unitOf (*this)).contains (e.position)) selectBand ((selected + 1) % 8);
        else analyzer.closeBubble();
    }

private:
    struct Set { TypeGrid* types = nullptr; StarKnob *freq = nullptr, *gain = nullptr, *q = nullptr, *thr = nullptr; GlassToggle* dyn = nullptr; };
    SpectrumAnalyzer analyzer;
    std::vector<BandChip*> chips;
    std::array<Set, 8> sets {};
    GlassToggle* listen = nullptr;
    juce::Rectangle<float> chipBox, bandRow, nameArea;
    int selected = 2;
};

//==============================================================================
// MULTIBAND : la bande choisie (onglets), son SOLO, ses presets, l'écran des 4 bandes, puis ses réglages et les communs
class MultiPanel final : public ModulePanel
{
public:
    MultiPanel (OniriqueVocalBoxAudioProcessor& p, int c) : ModulePanel (p, c), bands (juce::StringArray { "LOW", "LO-MID", "HI-MID", "HIGH" }), presetsBox (p.apvts, inst)
    {
        viz = &own (std::make_unique<MultiViz> (p, c));
        addAndMakeVisible (bands); anim.push_back (&bands);
        addAndMakeVisible (presetsBox); anim.push_back (&presetsBox);
        for (int b = 0; b < 4; ++b)
        {
            solo[b] = &toggle (ids::mbSolo[b], "SOLO " + juce::StringArray { "LOW", "LO-MID", "HI-MID", "HIGH" }[b]);
            thr[b] = &knob (ids::mbThr[b], "THRESHOLD");
            gain[b] = &knob (ids::mbGain[b], "GAIN");
        }
        common = { &knob (ids::mbAttack, "ATTACK"), &knob (ids::mbRelease, "RELEASE"), &knob (ids::mbOutput, "OUTPUT") };
        bands.onChange = [this] (int b) { show (b); };
        viz->onBand = [this] (int b) { bands.setSelected (b); show (b); };
        bands.setSelected (1);
        show (1);
    }
    void show (int b)
    {
        band = b;
        viz->band = b;
        for (int k = 0; k < 4; ++k) { solo[k]->setVisible (k == b); thr[k]->setVisible (k == b); gain[k]->setVisible (k == b); }
        resized();
    }
    void resized() override
    {
        const float u = ui::unitOf (*this);
        auto r = getLocalBounds().toFloat();
        auto top = r.removeFromTop (42.0f * u);
        bands.setBounds (top.removeFromLeft (bands.idealWidth (0.0f)).toNearestInt());
        top.removeFromLeft (14.0f * u);
        const auto sa = top.removeFromLeft (solo[band]->idealWidth()).toNearestInt();
        for (int k = 0; k < 4; ++k) solo[k]->setBounds (sa);
        presetsBox.setBounds (top.removeFromRight (300.0f * u).toNearestInt());
        r.removeFromTop (14.0f * u);
        const float cellH = (104.0f + 53.0f) * u;
        auto grid = r.removeFromBottom (cellH);
        r.removeFromBottom (14.0f * u);
        viz->setBounds (r.toNearestInt());
        std::vector<StarKnob*> ks { thr[band], gain[band] };
        for (auto* k : common) ks.push_back (k);
        const float gap = 10.0f * u, cw = (grid.getWidth() - 4.0f * gap) / 5.0f;
        for (int i = 0; i < 5; ++i) ks[(size_t) i]->setBounds (juce::Rectangle<float> (grid.getX() + (float) i * (cw + gap), grid.getY(), cw, cellH).toNearestInt());
    }

private:
    MultiViz* viz = nullptr;
    GlassTabs bands;
    MbPresetBox presetsBox;
    GlassToggle* solo[4] {};
    StarKnob *thr[4] {}, *gain[4] {};
    std::vector<StarKnob*> common;
    int band = 1;
};

std::unique_ptr<StdPanel> stdPanel (OniriqueVocalBoxAudioProcessor& p, int code, std::unique_ptr<Viz> v)
{
    auto s = std::make_unique<StdPanel> (p, code);
    s->viz = &s->own (std::move (v));
    return s;
}
} // namespace

//==============================================================================
std::unique_ptr<ModulePanel> createModulePanel (OniriqueVocalBoxAudioProcessor& p, int code)
{
    if (! modules::valid (code)) return nullptr;
    const int type = modules::typeOf (code);
    using T = juce::StringArray;

    switch (type)
    {
        case modules::fet:
        {
            auto f = std::make_unique<FacePanel> (p, code, "1176", "FET LIMITING AMPLIFIER", T { "GR", "+8", "+4", "OFF" });
            f->knobs = { &f->knob (ids::fetIn, "INPUT"), &f->knob (ids::fetOut, "OUTPUT"), &f->knob (ids::fetAttack, "ATTACK"), &f->knob (ids::fetRelease, "RELEASE") };
            f->addRowFirst ("RATIO", f->tabs (ids::fetRatio, T { "4", "8", "12", "20", "ALL" }));
            for (auto* k : f->knobs) k->setFaceStyle (true);
            return f;
        }
        case modules::opto:
        {
            auto f = std::make_unique<FacePanel> (p, code, "LA-2A", "OPTICAL LEVELING AMPLIFIER", T { "GAIN RED", "+10", "+4" });
            f->knobs = { &f->knob (ids::laGain, "GAIN"), &f->knob (ids::laPeak, "PEAK REDUCTION") };
            f->cols = 2;
            f->maxKnob = 300.0f;
            f->addRowFirst ("MODE", f->tabs (ids::laLimit, T { "COMPRESS", "LIMIT" }));
            for (auto* k : f->knobs) k->setFaceStyle (true);
            return f;
        }
        case modules::parallel:
        {
            auto f = std::make_unique<FacePanel> (p, code, "PARALLEL", utf8 ("1176 ÉCRASÉ REMIS DANS LA VOIX"), T { "GR", "+8", "+4", "OFF" });
            f->knobs = { &f->knob (ids::parAmount, "AMOUNT"), &f->knob (ids::parThresh, "THRESHOLD"), &f->knob (ids::parAttack, "ATTACK"),
                         &f->knob (ids::parRelease, "RELEASE"), &f->knob (ids::parDeessAmt, "DE-ESS"), &f->knob (ids::parDeessFreq, "DE-ESS FREQ") };
            f->cols = 3;
            f->maxKnob = 170.0f;
            f->addRowFirst ("AUTO", f->toggle (ids::parAuto, "AUTO"));
            for (auto* k : f->knobs) k->setFaceStyle (true);
            return f;
        }
        case modules::gate:
        {
            auto s = stdPanel (p, code, std::make_unique<WaveViz> (p, code));
            for (auto [id, l] : { std::pair { ids::gateThr, "THRESHOLD" }, std::pair { ids::gateRange, "RANGE" }, std::pair { ids::gateAttack, "ATTACK" },
                                  std::pair { ids::gateHold, "HOLD" }, std::pair { ids::gateRelease, "RELEASE" }, std::pair { ids::gateHyst, "HYSTERESIS" } })
                s->knobs.push_back (&s->knob (id, l));
            return s;
        }
        case modules::deess:
        {
            auto s = stdPanel (p, code, std::make_unique<WaveViz> (p, code));
            s->knobs = { &s->knob (ids::deessFreq, "FREQ"), &s->knob (ids::deessAmt, "AMOUNT") };
            return s;
        }
        case modules::noise:
        {
            auto s = stdPanel (p, code, std::make_unique<WaveViz> (p, code));
            s->knobs = { &s->knob (ids::nsAmount, "AMOUNT") };
            return s;
        }
        case modules::plosive:
        {
            auto s = stdPanel (p, code, std::make_unique<WaveViz> (p, code));
            s->knobs = { &s->knob (ids::plosive, "AMOUNT") };
            return s;
        }
        case modules::reso:
        {
            auto s = stdPanel (p, code, std::make_unique<ResoViz> (p, code));
            s->topRow = { &s->toggle (ids::resoListen, "LISTEN") };
            s->note = utf8 ("GLISSE LES POIGNÉES : FRÉQUENCE À L'HORIZONTALE, SENSIBILITÉ À LA VERTICALE");
            s->knobs = { &s->knob (ids::resoAmt, "AMOUNT"), &s->knob (ids::resoLow, "LOW"), &s->knob (ids::resoMid, "MID"), &s->knob (ids::resoHigh, "HIGH") };
            return s;
        }
        case modules::peq:   return std::make_unique<PeqPanel> (p, code);
        case modules::multi: return std::make_unique<MultiPanel> (p, code);
        case modules::sat:
        {
            auto s = stdPanel (p, code, std::make_unique<SatViz> (p, code));
            s->topRow = { &s->tabs (ids::satType, T { "TUBE", "ANALOG", "TAPE", "TRANSFORMER" }) };
            s->note = utf8 ("QUATRE ÉMULATIONS");
            s->knobs = { &s->knob (ids::drive, "DRIVE"), &s->knob (ids::satBias, "BIAS"), &s->knob (ids::satTone, "TONE"), &s->knob (ids::satWarmth, "WARMTH"),
                         &s->knob (ids::satAir, "AIR"), &s->knob (ids::satDynamics, "DYNAMICS"), &s->knob (ids::satMix, "MIX"), &s->knob (ids::satOutput, "OUTPUT") };
            return s;
        }
        case modules::analog:
        {
            auto s = stdPanel (p, code, std::make_unique<AnalogViz> (p, code));
            s->topRow = { &s->tabs (ids::lowFreq, T { "LOW 80 Hz", "LOW 100 Hz", "LOW 125 Hz" }), &s->toggle (ids::lowCut, "LOW CUT 85 Hz") };
            s->knobs = { &s->knob (ids::low, "LOW"), &s->knob (ids::mud, "MUD"), &s->knob (ids::high, "HI"), &s->knob (ids::air, "AIR") };
            return s;
        }
        case modules::delay:
        {
            auto s = stdPanel (p, code, std::make_unique<DelayViz> (p, code));
            s->topRow = { &s->tabs (ids::delayNote, delayNoteNames()), &s->toggle (ids::delaySync, "SYNC"), &s->toggle (ids::delayPingPong, "PING-PONG") };
            s->note = utf8 ("CALÉ SUR LE TEMPO");
            s->knobs = { &s->knob (ids::delayMix, "SEND"), &s->knob (ids::delayFeedback, "FEEDBACK"), &s->knob (ids::delayTime, "TIME"),
                         &s->knob (ids::delayLowCut, "LOW CUT"), &s->knob (ids::delayHighCut, "HIGH CUT"), &s->knob (ids::delayDucking, "DUCKING") };
            return s;
        }
        case modules::reverb:
        {
            auto s = stdPanel (p, code, std::make_unique<ReverbViz> (p, code));
            s->topRow = { &s->tabs (ids::reverbMode, T { "ROOM", "PLATE 1", "PLATE 2", "HALL", "SHIMMER" }) };
            s->note = "COMME SUR VOCAL MINI, EN PLUS COMPLET";
            s->knobs = { &s->knob (ids::reverbMix, "SEND"), &s->knob (ids::reverbPreDelay, "PRE-DELAY"), &s->knob (ids::reverbDecay, "DECAY"),
                         &s->knob (ids::reverbSize, "SIZE"), &s->knob (ids::reverbDiffusion, "DIFFUSION"), &s->knob (ids::reverbModRate, "MOD RATE"),
                         &s->knob (ids::reverbModDepth, "MOD DEPTH"), &s->knob (ids::reverbLowCut, "LOW CUT"), &s->knob (ids::reverbHighCut, "HIGH CUT"),
                         &s->knob (ids::reverbWidth, "WIDTH"), &s->knob (ids::reverbClean, "CLEAN"), &s->knob (ids::reverbDucking, "DUCKING") };
            return s;
        }
        case modules::doubler:
        {
            auto s = stdPanel (p, code, std::make_unique<DoublerViz> (p, code));
            s->knobs = { &s->knob (ids::doublerAmount, "AMOUNT") };
            return s;
        }
        case modules::chorus:
        {
            auto s = stdPanel (p, code, std::make_unique<FxViz> (p, code, FxVisual::Kind::chorus, "CHORUS · LA VOIX QUI ONDULE"));
            s->knobs = { &s->knob (ids::chorusRate, "RATE"), &s->knob (ids::chorusDepth, "DEPTH"), &s->knob (ids::chorusMix, "MIX") };
            return s;
        }
        case modules::flanger:
        {
            auto s = stdPanel (p, code, std::make_unique<FxViz> (p, code, FxVisual::Kind::flanger, "FLANGER · LE PEIGNE QUI GLISSE"));
            s->knobs = { &s->knob (ids::flangerRate, "RATE"), &s->knob (ids::flangerDepth, "DEPTH"), &s->knob (ids::flangerFeedback, "FEEDBACK"), &s->knob (ids::flangerMix, "MIX") };
            return s;
        }
        case modules::phone:
        {
            auto s = stdPanel (p, code, std::make_unique<FxViz> (p, code, FxVisual::Kind::phone, utf8 ("PHONE · LA VOIX DANS UN TÉLÉPHONE, UNE RADIO")));
            s->knobs = { &s->knob (ids::phoneAmount, "AMOUNT") };
            return s;
        }
        default: break;
    }
    return createGenericPanel (p, code);
}
