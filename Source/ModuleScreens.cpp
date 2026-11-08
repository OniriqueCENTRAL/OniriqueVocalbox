#include "ModulePanels.h"
#include "Registry.h"
#include "MixAnalysis.h"
#include "Habits.h"

using namespace onirique;

/*  Les écrans des modules, recopiés de la démo validée (Main.dc.html) : mêmes mesures, mêmes couleurs, mêmes textes.
    Les visuels se dessinent dans le repère de la démo (1400 x 360, mis à l'échelle comme un SVG « meet ») ; ils montrent le
    vrai signal : la voix qui entre et ce qui sort de chaque module, les spectres avant / après, les réductions de gain. */

namespace
{
constexpr float kPi = juce::MathConstants<float>::pi;
const juce::Colour& cEmber = onirique::look::ember;
const juce::Colour& cPink = onirique::look::emberPink;
const juce::Colour cBone { 0xffF4ECEC }, cMist { 0xfff4ecec };

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
// courbe arrondie qui passe par les milieux (comme les courbes de l'analyseur de l'EQ)
juce::Path smooth (const std::vector<juce::Point<float>>& pts)
{
    juce::Path p;
    if (pts.size() < 3) return poly (pts);
    p.startNewSubPath (pts[0]);
    for (size_t i = 1; i + 1 < pts.size(); ++i)
    {
        const auto m = (pts[i] + pts[i + 1]) * 0.5f;
        p.quadraticTo (pts[i], m);
    }
    p.lineTo (pts.back());
    return p;
}
juce::Path between (const std::vector<juce::Point<float>>& a, const std::vector<juce::Point<float>>& b)
{
    auto p = smooth (a);
    std::vector<juce::Point<float>> rb (b.rbegin(), b.rend());
    auto q = smooth (rb);
    p.lineTo (rb.front());
    juce::Path::Iterator it (q);
    bool first = true;
    while (it.next())
    {
        if (first) { first = false; continue; }
        if (it.elementType == juce::Path::Iterator::lineTo) p.lineTo (it.x1, it.y1);
        else if (it.elementType == juce::Path::Iterator::quadraticTo) p.quadraticTo (it.x1, it.y1, it.x2, it.y2);
    }
    p.closeSubPath();
    return p;
}

// le spectre à la façon de l'EQ paramétrique : sortie en surface braise (.55 -> .02), entrée en trait pâle (.35)
void drawSpectrum (juce::Graphics& g, const std::vector<juce::Point<float>>& in, const std::vector<juce::Point<float>>& out, float bottom, float top)
{
    if (out.size() < 2) return;
    auto surface = smooth (out);
    surface.lineTo (out.back().x, bottom);
    surface.lineTo (out.front().x, bottom);
    surface.closeSubPath();
    g.setGradientFill (juce::ColourGradient (cEmber.withAlpha (0.55f), 0.0f, top, cEmber.withAlpha (0.02f), 0.0f, bottom, false));
    g.fillPath (surface);
    if (! in.empty())
    {
        g.setColour (juce::Colour (244, 236, 236).withAlpha (0.35f));
        g.strokePath (smooth (in), juce::PathStrokeType (1.6f, juce::PathStrokeType::curved));
    }
}

// les dernières colonnes de l'oscilloscope d'un module (la plus ancienne à gauche)
void readScope (const ModuleScope& s, std::vector<float>& in, std::vector<float>& out, std::vector<float>* gr = nullptr)
{
    const int n = ModuleScope::kCols, w = s.write.load (std::memory_order_acquire);
    in.resize ((size_t) n); out.resize ((size_t) n);
    if (gr != nullptr) gr->resize ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        const int idx = (w + i) % n;
        in[(size_t) i] = s.in[idx].load (std::memory_order_relaxed);
        out[(size_t) i] = s.out[idx].load (std::memory_order_relaxed);
        if (gr != nullptr) (*gr)[(size_t) i] = s.gr[idx].load (std::memory_order_relaxed);
    }
}

//==============================================================================
// Le spectre d'une prise (FFT 4096, fenêtre de Hann), ramené sur des points logarithmiques entre fa et fb : énergie moyenne
// de chaque tranche, lissée entre voisins puis dans le temps (montée vive, retombée douce), comme l'analyseur de l'EQ
class MiniSpectrum
{
public:
    void setRange (float a, float b, int points) { fa = a; fb = b; db.assign ((size_t) points, -120.0f); }
    bool update (SpectrumTap& tap, double rate)
    {
        float tmp[2048];
        int got, total = 0;
        while ((got = tap.pull (tmp, 2048)) > 0)
        {
            total += got;
            for (int i = 0; i < got; ++i) { ring[(size_t) pos] = tmp[i]; pos = (pos + 1) % kN; }
        }
        if (total == 0 && settled) return false;
        for (int i = 0; i < kN; ++i)
        {
            const float w = 0.5f - 0.5f * std::cos (2.0f * kPi * (float) i / (float) kN);
            work[(size_t) i] = ring[(size_t) ((pos + i) % kN)] * w;
        }
        std::fill (work.begin() + kN, work.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (work.data());
        const int n = (int) db.size();
        std::vector<float> now ((size_t) n);
        for (int k = 0; k < n; ++k)
        {
            const float fc = fa * std::pow (fb / fa, ((float) k + 0.5f) / (float) n);
            const float bw = fc * 0.12f;   // une tranche d'1/6 d'octave autour du point
            int b0 = (int) ((fc - bw) / (float) rate * kN), b1 = (int) ((fc + bw) / (float) rate * kN) + 1;
            b0 = juce::jlimit (1, kN / 2 - 1, b0); b1 = juce::jlimit (b0 + 1, kN / 2, b1);
            double e = 0.0;
            for (int b = b0; b < b1; ++b) e += (double) work[(size_t) b] * work[(size_t) b];
            now[(size_t) k] = dbOf ((float) std::sqrt (e / (double) (b1 - b0)) * 4.0f / (float) kN);
        }
        for (int pass = 0; pass < 2; ++pass)
            for (int k = 1; k + 1 < n; ++k) now[(size_t) k] = 0.25f * now[(size_t) k - 1] + 0.5f * now[(size_t) k] + 0.25f * now[(size_t) k + 1];
        float moved = 0.0f;
        for (int k = 0; k < n; ++k)
        {
            auto& d = db[(size_t) k];
            const float step = (now[(size_t) k] > d ? 0.45f : 0.10f) * (now[(size_t) k] - d);
            d += step;
            moved = std::max (moved, std::abs (step));
        }
        settled = total == 0 && moved < 0.01f;
        return true;
    }
    bool settled = false;
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
    titre en haut à gauche, la légende en haut à droite, la phrase d'état en bas à gauche. draw() dessine dans le repère de la
    démo : 360 de haut, et au moins 1400 de large (vw) : plus large que la démo, le dessin s'étend, rien n'est coupé ni vide. */
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
        // le fond (dégradé radial, coûteux) est calculé une fois par taille, puis recopié
        const float sc = g.getInternalContext().getPhysicalPixelScaleFactor();
        const int bw = juce::roundToInt (r.getWidth() * sc), bh = juce::roundToInt (r.getHeight() * sc);
        if (! bgCache.isValid() || bgCache.getWidth() != bw || bgCache.getHeight() != bh)
        {
            bgCache = juce::Image (juce::Image::ARGB, std::max (1, bw), std::max (1, bh), true, juce::SoftwareImageType());
            juce::Graphics cg (bgCache);
            cg.addTransform (juce::AffineTransform::scale (sc));
            juce::ColourGradient bg (juce::Colour (0xff1d1013), r.getWidth() * 0.15f, 0.0f, juce::Colour (0xff070506), r.getWidth() * 0.15f + r.getWidth() * 1.2f, 0.0f, true);
            bg.addColour (0.55, juce::Colour (0xff0e0a0c));
            cg.setGradientFill (bg);
            cg.fillRoundedRectangle (r.withPosition (0.0f, 0.0f), rad);
        }
        g.drawImage (bgCache, r, juce::RectanglePlacement::stretchToFit);
        {
            juce::Graphics::ScopedSaveState s (g);
            juce::Path clip; clip.addRoundedRectangle (r, rad);
            g.reduceClipRegion (clip);
            const auto t = frame();
            vw = t.second;
            g.addTransform (t.first);
            g.setColour (juce::Colours::white.withAlpha (0.035f));
            for (int y = 60; y <= 300; y += 60) g.drawHorizontalLine (y, 0.0f, vw);
            draw (g);
        }
        g.setColour (juce::Colour (255, 220, 225).withAlpha (0.10f));
        g.drawRoundedRectangle (r.reduced (0.5f), rad, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillRect (r.withHeight (1.0f).reduced (rad, 0.0f).translated (0.0f, 1.0f));

        // titre, légende, état (sur un voile sombre, pour rester lisibles par-dessus le dessin)
        auto label = [&] (const juce::String& txt, juce::Rectangle<float> area, juce::Justification j, juce::Colour c)
        {
            const auto f = ui::css (ui::Face::mono, 10.0f * u, 0.12f);
            const float tw = ui::textWidth (f, txt);
            auto back = area.withWidth (tw + 16.0f * u).withSizeKeepingCentre (tw + 16.0f * u, area.getHeight() + 8.0f * u);
            if (j == juce::Justification::centredRight) back.setX (area.getRight() - back.getWidth() + 8.0f * u); else back.setX (area.getX() - 8.0f * u);
            g.setColour (juce::Colour (0xff0b0709).withAlpha (0.72f));
            g.fillRoundedRectangle (back, 8.0f * u);
            g.setColour (c);
            g.setFont (f);
            g.drawText (txt, area, j, false);
        };
        if (title.isNotEmpty()) label (title, { r.getX() + 18.0f * u, r.getY() + 14.0f * u, r.getWidth() * 0.62f, 14.0f * u }, juce::Justification::centredLeft, juce::Colour (0xffd9c6c9));
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
        if (stat.isNotEmpty()) label (stat, { r.getX() + 18.0f * u, r.getBottom() - 26.0f * u, r.getWidth() - 36.0f * u, 14.0f * u }, juce::Justification::centredLeft, juce::Colours::white);
    }

protected:
    virtual void draw (juce::Graphics&) = 0;
    // le repère : 360 de haut ; la largeur suit le cadre (au moins 1400)
    std::pair<juce::AffineTransform, float> frame() const
    {
        const auto r = getLocalBounds().toFloat();
        float sc = r.getHeight() / 360.0f, w = r.getWidth() / std::max (0.001f, sc);
        if (w < 1400.0f) { sc = r.getWidth() / 1400.0f; w = 1400.0f; }
        return { juce::AffineTransform::scale (sc).translated (r.getX() + (r.getWidth() - w * sc) * 0.5f, r.getY() + (r.getHeight() - 360.0f * sc) * 0.5f), w };
    }
    juce::Point<float> toDemo (juce::Point<float> p) const { return p.transformedBy (frame().first.inverted()); }
    float time() const { return (float) (juce::Time::getMillisecondCounterHiRes() * 0.001); }
    float waveEnd() const { return vw - 400.0f; }   // la forme d'onde à gauche, le spectre dans les 400 de droite
    float vw = 1400.0f;
    juce::Image bgCache;
    // ne redessiner que si l'oscilloscope du module a avancé (lecture arrêtée : plus aucun dessin)
    bool scopeMoved (const ChainModule* m)
    {
        if (m == nullptr) return false;
        const int w = m->scope.write.load (std::memory_order_relaxed);
        if (w == lastWrite && ++idle > 2) return false;
        if (w != lastWrite) idle = 0;
        lastWrite = w;
        return true;
    }
    int lastWrite = -1, idle = 0;
};

// le dégradé braise des parties retirées (#ff6b7a -> #7a0d1c, du haut vers le bas)
juce::ColourGradient emberFill (float y0, float y1) { return { onirique::look::emberLit, 0.0f, y0, onirique::look::emberDeep, 0.0f, y1, false }; }

//==============================================================================
// GATE, DE-ESSER, NOISE, PLOSIVES : la voix qui défile (ce qui est gardé en blanc, ce qui est retiré en braise) ; à droite,
// le spectre de la sortie (surface braise) et de l'entrée (trait pâle), comme l'EQ, et le repère de la fréquence réglée
class WaveViz : public Viz
{
public:
    WaveViz (OniriqueVocalBoxAudioProcessor& p, int code) : processor (p), module (p.module (code)), type (modules::typeOf (code)), k (modules::instanceOf (code))
    {
        const float fa = type == modules::deess || type == modules::declick ? 1000.0f : 20.0f, fb = type == modules::deess ? 16000.0f : (type == modules::plosive ? 1000.0f : 20000.0f);
        specIn.setRange (fa, fb, 120);
        specOut.setRange (fa, fb, 120);
        if (type == modules::gate) setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    }

    void tick() override
    {
        if (module == nullptr) return;
        if (! scopeMoved (module) && ! moving)
        {
            // lecture arrêtée : on ne redessine que si un réglage a changé (seuil, fréquence…)
            const auto before = stat;
            updateText();
            if (stat != before) repaint();
            return;
        }
        readScope (module->scope, in, out);
        if (++frame % 2 == 0 && module->tapIn != nullptr)
        {
            specIn.update (*module->tapIn, module->getRate());
            specOut.update (*module->tapOut, module->getRate());
        }
        red = std::max (red * 0.97f, std::abs (module->getReductionDb()));
        updateText();
        repaint();
    }

    // le seuil se règle en glissant (bouton enfoncé, au-delà de 3 px), de l'écart parcouru : un simple clic ne change rien
    void mouseDown (const juce::MouseEvent& e) override
    {
        moving = false;
        startThr = get (ids::gateThr);
        startY = toDemo (e.position).y;
        side = startY < 170.0f ? -1.0f : 1.0f;
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (type != modules::gate) return;
        auto* prm = processor.param (ids::gateThr, k);
        if (prm == nullptr) return;
        if (! moving) { if (e.getDistanceFromDragStart() < 3) return; moving = true; prm->beginChangeGesture(); }
        const float dy = (toDemo (e.position).y - startY) * side;   // en s'éloignant du centre, le seuil monte
        prm->setValueNotifyingHost (prm->convertTo0to1 (juce::jlimit (-70.0f, 0.0f, startThr + dy / 150.0f * 70.0f)));
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (moving) if (auto* prm = processor.param (ids::gateThr, k)) prm->endChangeGesture();
        moving = false;
    }

private:
    float get (const char* id) const { return processor.apvts.getRawParameterValue (ids::inst (id, k))->load(); }
    float norm (float a) const
    {
        if (type == modules::gate) return juce::jlimit (0.0f, 1.0f, (dbOf (a) + 70.0f) / 70.0f);
        return juce::jlimit (0.0f, 1.0f, (dbOf (a) + 60.0f) / 60.0f);   // en dB : une voix douce reste lisible
    }

    void updateText()
    {
        const juce::String dot = juce::String::fromUTF8 (" · ");
        switch (type)
        {
            case modules::gate:
            {
                int closed = 0;
                for (size_t i = 0; i < in.size(); ++i) if (in[i] > 1.0e-5f && out[i] < in[i] * 0.5f) ++closed;
                title = utf8 ("NOISE GATE · CE QUI EST COUPÉ ENTRE LES PHRASES");
                legend = { { "GARDE", cMist }, { utf8 ("COUPÉ"), cEmber } };
                stat = "SEUIL " + juce::String (juce::roundToInt (get (ids::gateThr))) + " dB" + dot + juce::String (juce::roundToInt (100.0f * (float) closed / (float) std::max<size_t> (1, in.size())))
                     + utf8 (" % DU TEMPS FERMÉ · −") + juce::String (juce::roundToInt (get (ids::gateRange))) + " dB";
                break;
            }
            case modules::deess:
            {
                juce::StringArray on;
                for (auto [id, n] : { std::pair { ids::deessS, "S" }, { ids::deessT, "T" }, { ids::deessCH, "CH" }, { ids::deessF, "F" } }) if (get (id) > 0.5f) on.add (n);
                title = utf8 ("DE-ESSER · CE QUI EST RETIRÉ EN BRAISE");
                legend = { { utf8 ("GARDÉ"), cMist }, { utf8 ("RETIRÉ"), cEmber } };
                stat = (on.isEmpty() ? juce::String ("AUCUN SON") : on.joinIntoString (" + ")) + dot + hz (get (ids::deessFreq)) + utf8 (" · −") + juce::String (red, 1) + " dB"
                     + (get (ids::deessClarity) > 0.5f ? dot + "CLARITY " + juce::String (juce::roundToInt (get (ids::deessClarity))) + " %" : juce::String());
                break;
            }
            case modules::noise:
                title = utf8 ("NOISE SUPPRESSOR · LE SOUFFLE RETIRÉ EN BRAISE");
                legend = { { utf8 ("GARDÉ"), cMist }, { utf8 ("RETIRÉ"), cEmber } };
                stat = utf8 ("BRUIT DE FOND −") + juce::String (red, 1) + " dB" + dot + "AMOUNT " + juce::String (juce::roundToInt (get (ids::nsAmount))) + " %";
                break;
            case modules::declick:
            {
                if (auto* m = dynamic_cast<chainmods::DeClickM*> (module)) clickTotal += m->takeClicks();
                title = utf8 ("DE-CLICK · LES BRUITS DE BOUCHE RETIRÉS EN BRAISE");
                legend = { { utf8 ("GARDÉ"), cMist }, { utf8 ("RETIRÉ"), cEmber } };
                stat = juce::String (clickTotal) + (clickTotal > 1 ? utf8 (" CLICS RETIRÉS") : utf8 (" CLIC RETIRÉ")) + dot + "SENSITIVITY "
                     + juce::String (juce::roundToInt (get (ids::dclkSens))) + " %" + (get (ids::dclkListen) > 0.5f ? dot + "LISTEN" : juce::String());
                break;
            }
            case modules::debreath:
                title = utf8 ("DE-BREATH · LES RESPIRATIONS BAISSÉES EN BRAISE");
                legend = { { utf8 ("GARDÉ"), cMist }, { utf8 ("BAISSÉ"), cEmber } };
                stat = (red > 1.0f ? utf8 ("RESPIRATION −") + juce::String (red, 1) + " dB" : juce::String ("VOIX"))
                     + dot + utf8 ("RÉDUCTION −") + juce::String (get (ids::dbrReduction), 0) + " dB" + (get (ids::dbrListen) > 0.5f ? dot + "LISTEN" : juce::String());
                break;
            case modules::dereverb:
            {
                float tail = get (ids::drvTail);
                bool measured = false;
                if (auto* m = dynamic_cast<chainmods::DeReverbM*> (module)) { tail = m->tailSeconds(); measured = m->tailMeasured(); }
                title = utf8 ("DE-REVERB · LA PIÈCE RETIRÉE EN BRAISE");
                legend = { { utf8 ("GARDÉ"), cMist }, { utf8 ("RETIRÉ"), cEmber } };
                stat = utf8 ("PIÈCE ") + juce::String (tail, 2) + " s"
                     + (get (ids::drvAuto) > 0.5f ? (measured ? utf8 (" (MESURÉE)") : utf8 (" (À MESURER : JOUE LA VOIX)")) : juce::String()) + dot + "−"
                     + juce::String (red, 1) + " dB" + dot + "AMOUNT " + juce::String (juce::roundToInt (get (ids::drvAmount))) + " %";
                break;
            }
            default:
                title = utf8 ("PLOSIVES · LES P ET LES B RETIRÉS EN BRAISE");
                legend = { { utf8 ("GARDÉ"), cMist }, { utf8 ("RETIRÉ"), cEmber } };
                stat = utf8 ("P / B −") + juce::String (red, 1) + " dB" + dot + "AMOUNT " + juce::String (juce::roundToInt (get (ids::plosive))) + " %";
                break;
        }
    }

    void draw (juce::Graphics& g) override
    {
        if (module == nullptr || in.empty()) return;
        const float L = waveEnd();
        std::vector<float> a (in.size()), b (out.size());
        for (size_t i = 0; i < in.size(); ++i) { a[i] = norm (in[i]); b[i] = norm (out[i]); }
        const auto removed = mirror (a, 0.0f, L, 170.0f, 150.0f), kept = mirror (b, 0.0f, L, 170.0f, 150.0f);
        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (0, 0, (int) L, 360);
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
                    juce::Path l; l.startNewSubPath (0.0f, y); l.lineTo (L, y);
                    dashed (g, l, 1.5f, 6.0f, 6.0f);
                }
                const int n = (int) in.size();
                for (int i = 0; i < n;)
                {
                    const bool open = out[(size_t) i] > in[(size_t) i] * 0.5f && in[(size_t) i] > 1.0e-4f;
                    if (! open) { ++i; continue; }
                    int e = i;
                    while (e + 1 < n && out[(size_t) e + 1] > in[(size_t) e + 1] * 0.5f && in[(size_t) e + 1] > 1.0e-4f) ++e;
                    const auto bar = juce::Rectangle<float> (L * (float) i / (float) (n - 1), 338.0f, std::max (2.0f, L * (float) (e - i + 1) / (float) (n - 1)), 8.0f);
                    g.setColour (cEmber.withAlpha (0.35f));
                    g.fillRoundedRectangle (bar.expanded (3.0f), 6.0f);
                    g.setColour (cEmber);
                    g.fillRoundedRectangle (bar, 4.0f);
                    i = e + 1;
                }
            }
        }
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillRect (L, 0.0f, 1.0f, 360.0f);

        // le spectre de droite, comme l'analyseur de l'EQ
        if (specIn.db.empty()) return;
        const float x0 = L + 30.0f, x1 = vw - 20.0f;
        auto X = [&] (size_t i) { return x0 + (x1 - x0) * (float) i / (float) (specIn.db.size() - 1); };
        auto Y = [] (float d) { return juce::jlimit (50.0f, 320.0f, 50.0f + (-24.0f - d) / 70.0f * 270.0f); };
        std::vector<juce::Point<float>> pin, pout;
        for (size_t i = 0; i < specIn.db.size(); ++i) { pin.push_back ({ X (i), Y (specIn.db[i]) }); pout.push_back ({ X (i), Y (specOut.db[i]) }); }
        drawSpectrum (g, pin, pout, 320.0f, 50.0f);
        // le repère de la fréquence réglée (DE-ESSER : les S)
        if (type == modules::deess)
        {
            const float f = get (ids::deessFreq);
            const float x = x0 + (x1 - x0) * std::log (f / specIn.fa) / std::log (specIn.fb / specIn.fa);
            g.setColour (cPink);
            { juce::Path m; m.startNewSubPath (x, 50.0f); m.lineTo (x, 320.0f); dashed (g, m, 1.5f, 4.0f, 5.0f); }
            const int idx = juce::jlimit (0, (int) specOut.db.size() - 1, juce::roundToInt ((x - x0) / (x1 - x0) * (float) (specOut.db.size() - 1)));
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
    int type, k, frame = 0, clickTotal = 0;
    float red = 0.0f;
    bool moving = false;
    float startThr = 0.0f, startY = 0.0f, side = 1.0f;
    std::vector<float> in, out;
    MiniSpectrum specIn, specOut;
};

//==============================================================================
// RESONANCE : le spectre en grand sur toute la largeur (20 Hz - 20 kHz) : la sortie en surface braise, l'entrée en trait pâle,
// ce qui est creusé en braise vive ; les zones LOW / MID / HIGH et leurs poignées (fréquence à l'horizontale, sensibilité à
// la verticale)
class ResoViz : public Viz
{
public:
    ResoViz (OniriqueVocalBoxAudioProcessor& p, int code) : processor (p), module (dynamic_cast<chainmods::Reso*> (p.module (code))), k (modules::instanceOf (code))
    {
        setMouseCursor (juce::MouseCursor::CrosshairCursor);
        legend = { { utf8 ("GARDÉ"), cEmber }, { utf8 ("CREUSÉ"), cPink } };
        title = utf8 ("RESONANCE · LES PICS QUI SIFFLENT OU BOURDONNENT, CREUSÉS EN BRAISE");
        spec.fill (-120.0f);
    }
    void tick() override
    {
        float change = 0.0f;
        if (module != nullptr)
        {
            const auto& m = module->getMeter();
            for (int b = 0; b < ResonanceSuppressor::kBands; ++b)
            {
                const float ds = 0.3f * (m.spectrumDb[(size_t) b].load() - spec[(size_t) b]);
                spec[(size_t) b] += ds;
                const float rd = m.reductionDb[(size_t) b].load();
                const float dr = (rd > red[(size_t) b] ? 0.5f : 0.1f) * (rd - red[(size_t) b]);
                red[(size_t) b] += dr;
                change = std::max (change, std::max (std::abs (ds), std::abs (dr)));
            }
        }
        const juce::String dot = juce::String::fromUTF8 (" · ");
        stat = "AMOUNT " + juce::String (juce::roundToInt (get (ids::resoAmt))) + " %" + dot + "LOW " + juce::String (juce::roundToInt (get (ids::resoLow)))
             + " %" + dot + "MID " + juce::String (juce::roundToInt (get (ids::resoMid))) + " %" + dot + "HIGH " + juce::String (juce::roundToInt (get (ids::resoHigh))) + " %";
        // rien ne bouge (lecture arrêtée, réglages fixes) : pas de dessin
        juce::String sig = stat;
        for (auto* id : { ids::resoLowFreq, ids::resoMidFreq, ids::resoHighFreq, ids::resoLowWidth, ids::resoMidWidth, ids::resoHighWidth }) sig << "|" << get (id);
        if (change > 0.02f || sig != lastSig || drag >= 0) repaint();
        lastSig = sig;
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto q = toDemo (e.position);
        drag = -1;
        float best = 40.0f;
        for (int b = 0; b < 3; ++b) { const float d = handle (b).getDistanceFrom (q); if (d < best) { best = d; drag = b; } }
        if (drag >= 0) grab = handle (drag) - q;   // la poignée garde son écart avec la souris : elle ne saute pas
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (drag < 0) return;
        if (e.getDistanceFromDragStart() < 3) return;
        const auto q = toDemo (e.position) + grab;
        const char* fid[] { ids::resoLowFreq, ids::resoMidFreq, ids::resoHighFreq };
        const char* sid[] { ids::resoLow, ids::resoMid, ids::resoHigh };
        if (auto* p = processor.param (fid[drag], k)) p->setValueNotifyingHost (p->convertTo0to1 (fOf (q.x)));
        if (auto* p = processor.param (sid[drag], k)) p->setValueNotifyingHost (p->convertTo0to1 (juce::jlimit (0.0f, 100.0f, (278.0f - q.y) / 220.0f * 100.0f)));
    }
    void mouseUp (const juce::MouseEvent&) override { drag = -1; }

private:
    float get (const char* id) const { return processor.apvts.getRawParameterValue (ids::inst (id, k))->load(); }
    float X (float f) const { return 24.0f + (vw - 48.0f) * std::log (f / 20.0f) / std::log (1000.0f); }
    float fOf (float x) const { return 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, (x - 24.0f) / (vw - 48.0f))); }
    static float Y (float d) { return juce::jlimit (50.0f, 278.0f, 50.0f + (-14.0f - d) / 76.0f * 228.0f); }
    juce::Point<float> handle (int b) const
    {
        const char* fid[] { ids::resoLowFreq, ids::resoMidFreq, ids::resoHighFreq };
        const char* sid[] { ids::resoLow, ids::resoMid, ids::resoHigh };
        return { X (get (fid[b])), 278.0f - get (sid[b]) * 0.01f * 220.0f };
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
            g.drawText (names[b], juce::Rectangle<float> (x0, 282.0f, x1 - x0, 18.0f), juce::Justification::centred, false);
        }
        // les fréquences en bas
        g.setFont (ui::css (ui::Face::mono, 10.0f, 0.12f));
        for (auto [f, t] : { std::pair { 50.0f, "50" }, { 100.0f, "100" }, { 200.0f, "200" }, { 500.0f, "500" }, { 1000.0f, "1k" }, { 2000.0f, "2k" }, { 5000.0f, "5k" }, { 10000.0f, "10k" } })
        {
            g.setColour (juce::Colours::white.withAlpha (0.04f));
            g.fillRect (X (f), 40.0f, 1.0f, 238.0f);
            g.setColour (juce::Colour (0xff7d6a6e));
            g.drawText (t, juce::Rectangle<float> (60.0f, 14.0f).withCentre ({ X (f), 308.0f }), juce::Justification::centred, false);
        }
        std::vector<juce::Point<float>> pin, pout;
        bool any = false;
        for (int b = 0; b < ResonanceSuppressor::kBands; ++b)
        {
            const float f = 20.0f * std::pow (1000.0f, ((float) b + 0.5f) / (float) ResonanceSuppressor::kBands);
            pin.push_back ({ X (f), Y (spec[(size_t) b]) });
            pout.push_back ({ X (f), Y (spec[(size_t) b] - red[(size_t) b]) });
            any = any || spec[(size_t) b] > -100.0f;
        }
        if (any)
        {
            drawSpectrum (g, pin, pout, 278.0f, 50.0f);
            // ce qui est creusé : entre l'entrée et la sortie, en braise vive
            const auto cut = between (pin, pout);
            g.setColour (cPink.withAlpha (0.45f));
            g.fillPath (cut);
        }
        for (int b = 0; b < 3; ++b)
        {
            const auto dot = juce::Rectangle<float> (14.0f, 14.0f).withCentre (handle (b));
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
    juce::Point<float> grab;
    juce::String lastSig;
};

//==============================================================================
// MULTIBAND : quatre colonnes sur toute la largeur ; le niveau de chaque bande, son seuil (glisser dans la bande), sa
// réduction en rideau depuis le haut avec sa valeur écrite en grand, les coupures (glisser les poignées blanches)
class MultiViz : public Viz
{
public:
    MultiViz (OniriqueVocalBoxAudioProcessor& p, int code) : processor (p), module (dynamic_cast<chainmods::Multi*> (p.module (code))), k (modules::instanceOf (code))
    {
        spec.setRange (20.0f, 20000.0f, 160);
        title = utf8 ("MULTIBAND · GLISSE LES COUPURES, MONTE OU DESCENDS LE SEUIL DANS UNE BANDE");
        legend = { { "NIVEAU", cMist }, { utf8 ("RÉDUCTION"), cEmber } };
    }
    std::function<void (int)> onBand;
    int band = 1;

    void tick() override
    {
        float change = 0.0f;
        if (module != nullptr)
            for (int b = 0; b < 4; ++b)
            {
                const float l = module->getMeter().levelDb[(size_t) b].load(), r = std::abs (module->getMeter().grDb[(size_t) b].load());
                const float dl = 0.3f * (l - lv[b]), dg = (r > gr[b] ? 0.5f : 0.12f) * (r - gr[b]);
                lv[b] += dl; gr[b] += dg;
                change = std::max (change, std::max (std::abs (dl), std::abs (dg)));
            }
        if (++frame % 2 == 0 && spec.update (processor.outputTap, processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 48000.0)) change = 1.0f;
        stat = hz (get (ids::mbX[0])) + " / " + hz (get (ids::mbX[1])) + " / " + hz (get (ids::mbX[2]));
        juce::String sig = stat;
        for (int b = 0; b < 4; ++b) sig << "|" << get (ids::mbThr[b]) << get (ids::mbSolo[b]);
        sig << band;
        if (change > 0.02f || sig != lastSig || dragX >= 0 || dragB >= 0) repaint();
        lastSig = sig;
    }
    // un clic choisit la bande sans rien changer ; on règle en gardant le bouton enfoncé et en glissant (au-delà de 3 px),
    // et la valeur bouge de l'écart parcouru depuis sa position de départ : rien ne saute au clic
    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto q = toDemo (e.position);
        dragX = -1; dragB = -1; moving = false;
        const auto xs = edges();
        for (int i = 0; i < 3; ++i) if (std::abs (q.x - xs[(size_t) i + 1]) < 14.0f) dragX = i;
        if (dragX < 0)
            for (int b = 0; b < 4; ++b)
                if (q.x >= xs[(size_t) b] && q.x < xs[(size_t) b + 1]) { dragB = b; if (b != band) { band = b; if (onBand) onBand (b); } }
        startX = dragX >= 0 ? xs[(size_t) dragX + 1] : 0.0f;
        startThr = dragB >= 0 ? get (ids::mbThr[dragB]) : 0.0f;
        startQ = q;
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragX < 0 && dragB < 0) return;
        const auto q = toDemo (e.position);
        juce::RangedAudioParameter* p = dragX >= 0 ? processor.param (ids::mbX[dragX], k) : processor.param (ids::mbThr[dragB], k);
        if (p == nullptr) return;
        if (! moving)
        {
            if (e.getDistanceFromDragStart() < 3) return;
            moving = true;
            p->beginChangeGesture();
        }
        if (dragX >= 0)
        {
            float f = fOf (startX + (q.x - startQ.x));
            if (dragX > 0) f = std::max (f, get (ids::mbX[dragX - 1]) * 1.3f);
            if (dragX < 2) f = std::min (f, get (ids::mbX[dragX + 1]) / 1.3f);
            p->setValueNotifyingHost (p->convertTo0to1 (f));
        }
        else
        {
            const float fine = e.mods.isShiftDown() ? 0.25f : 1.0f;
            p->setValueNotifyingHost (p->convertTo0to1 (juce::jlimit (-60.0f, 0.0f, startThr - (q.y - startQ.y) / 270.0f * 60.0f * fine)));
        }
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (moving)
            if (auto* p = dragX >= 0 ? processor.param (ids::mbX[dragX], k) : processor.param (ids::mbThr[dragB], k)) p->endChangeGesture();
        dragX = dragB = -1;
        moving = false;
    }
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
    float X (float f) const { return 20.0f + (vw - 40.0f) * std::log (f / 20.0f) / std::log (1000.0f); }
    float fOf (float x) const { return 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, (x - 20.0f) / (vw - 40.0f))); }
    static float yd (float db) { return 60.0f + (-db) / 60.0f * 270.0f; }
    std::array<float, 5> edges() const { return { 20.0f, X (get (ids::mbX[0])), X (get (ids::mbX[1])), X (get (ids::mbX[2])), vw - 20.0f }; }
    void draw (juce::Graphics& g) override
    {
        // le spectre de la sortie, comme l'EQ
        std::vector<juce::Point<float>> sp;
        for (size_t i = 0; i < spec.db.size(); ++i)
            sp.push_back ({ X (20.0f * std::pow (1000.0f, ((float) i + 0.5f) / (float) spec.db.size())), juce::jlimit (60.0f, 330.0f, 70.0f + (-20.0f - spec.db[i]) / 80.0f * 260.0f) });
        drawSpectrum (g, {}, sp, 330.0f, 60.0f);

        const auto xs = edges();
        int solo = -1;
        for (int b = 0; b < 4; ++b) if (get (ids::mbSolo[b]) > 0.5f) solo = b;
        static const char* names[] { "LOW", "LO-MID", "HI-MID", "HIGH" };
        for (int b = 0; b < 4; ++b)
        {
            const float x = xs[(size_t) b], w = xs[(size_t) b + 1] - x;
            const bool on = b == band;
            g.setColour (on ? cEmber.withAlpha (0.08f) : (solo >= 0 && solo != b ? juce::Colours::black.withAlpha (0.35f) : juce::Colours::white.withAlpha (0.012f)));
            g.fillRect (juce::Rectangle<float> (x, 0.0f, w, 360.0f));
            // le rideau de réduction : 10 par dB depuis le haut, bord net braise
            const float gh = std::min (240.0f, gr[b] * 10.0f);
            if (gh > 0.5f)
            {
                g.setGradientFill (juce::ColourGradient (cEmber.withAlpha (0.65f), 0.0f, 50.0f, cEmber.withAlpha (0.12f), 0.0f, 50.0f + gh, false));
                g.fillRect (juce::Rectangle<float> (x + 2.0f, 50.0f, w - 4.0f, gh));
                g.setColour (cEmber);
                g.fillRect (juce::Rectangle<float> (x + 2.0f, 50.0f + gh - 2.0f, w - 4.0f, 2.0f));
            }
            // la valeur de la réduction de la bande, en grand, et son nom
            g.setColour (gr[b] > 0.1f ? juce::Colours::white : juce::Colour (0xff7d6a6e));
            g.setFont (ui::css (ui::Face::display, 20.0f, 0.04f));
            g.drawText ((gr[b] >= 0.05f ? utf8 ("−") : juce::String()) + juce::String (gr[b] >= 0.05f ? gr[b] : 0.0f, 1) + " dB", juce::Rectangle<float> (x, 56.0f, w, 28.0f), juce::Justification::centred, false);
            g.setColour (on ? cBone : juce::Colour (0xffa8939a));
            g.setFont (ui::css (ui::Face::mono, 11.0f, 0.12f));
            g.drawText (names[b], juce::Rectangle<float> (x, 312.0f, w, 18.0f), juce::Justification::centred, false);
            // le niveau de la bande
            const float ly = yd (juce::jlimit (-60.0f, 0.0f, lv[b]));
            g.setColour (juce::Colour (244, 236, 236).withAlpha (0.75f));
            g.fillRoundedRectangle (juce::Rectangle<float> (x + w * 0.5f - 11.0f, ly, 22.0f, std::max (0.0f, 306.0f - ly)), 5.0f);
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
    bool moving = false;
    float startX = 0.0f, startThr = 0.0f;
    juce::String lastSig;
    juce::Point<float> startQ;
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
        title = utf8 ("EQ ANALOG · COULEUR DE CONSOLE");
        legend = { { "COURBE", cEmber } };
    }
    void tick() override
    {
        auto v = [&] (const char* id) { const float x = get (id); return (x > 0.0f ? "+" : "") + juce::String (x, 1); };
        const juce::String dot = juce::String::fromUTF8 (" · ");
        stat = "LOW " + v (ids::low) + dot + "MUD " + v (ids::mud) + dot + "HI " + v (ids::high) + dot + "AIR " + v (ids::air);
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
        g.drawHorizontalLine (180, 0.0f, vw);
        g.setColour (juce::Colours::white.withAlpha (0.04f));
        for (float x : { 0.1f, 0.3f, 0.5f, 0.7f, 0.9f }) g.drawVerticalLine ((int) (x * vw), 0.0f, 360.0f);
        std::vector<juce::Point<float>> pts;
        for (int i = 0; i <= 140; ++i)
        {
            const float f = 20.0f * std::pow (1000.0f, (float) i / 140.0f);
            pts.push_back ({ (float) i * vw / 140.0f, 180.0f - juce::jlimit (-15.0f, 15.0f, dbAt (f)) * 11.0f });
        }
        auto fill = smooth (pts);
        fill.lineTo (vw, 360.0f); fill.lineTo (0.0f, 360.0f); fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient (cEmber.withAlpha (0.55f), 0.0f, 0.0f, cEmber.withAlpha (0.0f), 0.0f, 360.0f, false));
        g.fillPath (fill);
        const auto curve = smooth (pts);
        glowPath (g, curve, cEmber.withAlpha (0.85f), 10.0f, false);
        g.setColour (cEmber);
        g.strokePath (curve, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved));
        for (float x : { 0.1f, 0.4f, 0.68f, 0.92f })
        {
            const float f = 20.0f * std::pow (1000.0f, x);
            const auto dot = juce::Rectangle<float> (18.0f, 18.0f).withCentre ({ x * vw, 180.0f - juce::jlimit (-15.0f, 15.0f, dbAt (f)) * 11.0f });
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
        title = utf8 ("SATURATION · ") + modes[m];
        stat = utf8 (sub[m]) + utf8 (" · DRIVE ") + juce::String (get (ids::drive), 1);
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
        const float x0 = 420.0f, step = (vw - 20.0f - x0) / 160.0f;
        for (int i = 0; i <= 160; ++i)
        {
            const float x = 0.9f * std::sin ((float) i / 160.0f * 4.0f * kPi + phase);
            pin.push_back ({ x0 + (float) i * step, 180.0f - x * 150.0f });
            pout.push_back ({ x0 + (float) i * step, 180.0f - fx (x) * 150.0f });
        }
        const auto curve = poly (c);
        glowPath (g, curve, cEmber.withAlpha (0.85f), 10.0f, false);
        g.setColour (cEmber);
        g.strokePath (curve, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved));
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawLine (x0, 180.0f, vw - 20.0f, 180.0f, 1.0f);
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
// PREAMP : la courbe de l'étage (elle se plie avec le gain, et plus tôt en MIC), et à droite le grain qu'elle donne :
// les harmoniques d'une voix tenue, qui s'allument avec la dérive de l'étage de sortie
class PreampViz : public Viz
{
public:
    PreampViz (OniriqueVocalBoxAudioProcessor& p, int code) : processor (p), k (modules::instanceOf (code))
    {
        legend = { { "H1", juce::Colour (244, 236, 236).withAlpha (0.5f) }, { "HARMONIQUES", cEmber } };
    }
    void tick() override
    {
        const bool mic = get (ids::preampMic) > 0.5f;
        const float db = get (ids::preampGain);
        title = utf8 ("PREAMP N73 · ") + (mic ? "MIC" : "LINE");
        stat = utf8 ("GAIN ") + (db > 0.0f ? "+" : "") + juce::String (juce::roundToInt (db)) + utf8 (" dB · GRAIN DE CONSOLE, NIVEAU COMPENSÉ");
        float live = 0.0f;
        if (auto* m = dynamic_cast<chainmods::Preamp*> (processor.module (modules::code (modules::preamp, k)))) live = m->driveNow();
        glow += 0.2f * (live - glow);
        phase += 0.012f;
        repaint();
    }

private:
    float get (const char* id) const { return processor.apvts.getRawParameterValue (ids::inst (id, k))->load(); }
    float amount() const
    {
        const bool mic = get (ids::preampMic) > 0.5f;
        const float db = get (ids::preampGain);
        return juce::jlimit (0.02f, 1.2f, (db - (mic ? 22.0f : 4.0f)) / 34.0f);   // LINE 0 dB : presque propre ; MIC +40 : franchement saturé
    }
    float fx (float x) const
    {
        const float d = amount(), kk = 0.55f + 1.7f * d;
        const float u = kk * x + 0.18f * d * x * x;   // un peu dissymétrique : de la deuxième harmonique
        return std::tanh (u) / std::tanh (kk);
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
        std::vector<juce::Point<float>> c;
        for (int i = 0; i <= 60; ++i) { const float x = -1.0f + (float) i / 30.0f; c.push_back ({ 200.0f + x * 160.0f, 180.0f - juce::jlimit (-1.0f, 1.0f, fx (x)) * 160.0f }); }
        const auto curve = poly (c);
        glowPath (g, curve, cEmber.withAlpha (0.85f), 10.0f, false);
        g.setColour (cEmber);
        g.strokePath (curve, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved));

        // les harmoniques (DFT d'un sinus passé dans la courbe), H1 en référence
        constexpr int N = 128;
        float hs[8] {};
        for (int hN = 1; hN <= 7; ++hN)
        {
            double re = 0.0, im = 0.0;
            for (int i = 0; i < N; ++i)
            {
                const double ph = 2.0 * juce::MathConstants<double>::pi * i / N;
                const double y = fx (0.85f * (float) std::sin (ph));
                re += y * std::cos (hN * ph); im += y * std::sin (hN * ph);
            }
            hs[hN] = (float) std::sqrt (re * re + im * im);
        }
        const float x0 = 440.0f, w = (vw - 40.0f - x0) / 7.0f;
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawLine (x0, 320.0f, vw - 40.0f, 320.0f, 1.0f);
        for (int hN = 1; hN <= 7; ++hN)
        {
            const float db = juce::Decibels::gainToDecibels (hs[hN] / std::max (1.0e-9f, hs[1]), -90.0f);
            const float hgt = juce::jmap (juce::jlimit (-90.0f, 0.0f, db), -90.0f, 0.0f, 6.0f, 270.0f);
            const auto bar = juce::Rectangle<float> (x0 + (float) (hN - 1) * w + w * 0.3f, 320.0f - hgt, w * 0.4f, hgt);
            const float lit = hN == 1 ? 0.35f : juce::jlimit (0.25f, 1.0f, 0.45f + 0.9f * glow + 0.05f * std::sin (phase * 3.0f + (float) hN));
            g.setColour (cEmber.withAlpha (0.18f * lit));
            g.fillRoundedRectangle (bar.expanded (5.0f), 8.0f);
            g.setColour (hN == 1 ? juce::Colour (244, 236, 236).withAlpha (0.45f) : cEmber.withAlpha (lit));
            g.fillRoundedRectangle (bar, 5.0f);
            g.setColour (juce::Colour (0xffbba7aa));
            g.setFont (ui::css (ui::Face::mono, 13.0f, 0.1f));
            g.drawText ("H" + juce::String (hN), juce::Rectangle<float> (bar.getCentreX() - 30.0f, 326.0f, 60.0f, 18.0f), juce::Justification::centred, false);
        }
    }
    OniriqueVocalBoxAudioProcessor& processor;
    int k;
    float phase = 0.0f, glow = 0.0f;
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
        const juce::String dot = juce::String::fromUTF8 (" · ");
        stat = (sync ? names[juce::jlimit (0, names.size() - 1, juce::roundToInt (get (ids::delayNote)))] : juce::String (juce::roundToInt (get (ids::delayTime))) + " ms")
             + dot + juce::String (count - 1) + utf8 (" ÉCHOS") + dot + (get (ids::delayPingPong) > 0.5f ? "PING-PONG" : "MONO")
             + dot + hz (get (ids::delayLowCut)) + utf8 (" → ") + hz (get (ids::delayHighCut));
        repaint();
    }

private:
    float get (const char* id) const { return processor.apvts.getRawParameterValue (ids::inst (id, k))->load(); }
    void draw (juce::Graphics& g) override
    {
        const float cx = vw * 0.5f, spread = vw / 1400.0f;
        g.setColour (onirique::look::emberPink.withAlpha (0.10f));
        for (float x = -vw; x <= 2.0f * vw; x += 200.0f) g.drawLine (x, 360.0f, cx, 60.0f, 1.0f);
        for (int q = 1; q < 9; ++q) { const float y = 60.0f + 300.0f / (1.0f + (float) q * 0.7f); g.drawHorizontalLine ((int) y, 0.0f, vw); }
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
            ec.push_back ({ cx + sd * 420.0f * spread * sc, 60.0f + 250.0f * sc, sc, op, q == 0, (float) q * beats * 0.5f });
        }
        count = (int) ec.size();
        const float t = time();
        for (auto it = ec.rbegin(); it != ec.rend(); ++it)
        {
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
        for (auto& s : sparks) s = { r.nextFloat(), 180.0f + (r.nextFloat() - 0.5f) * 240.0f, 1.0f + r.nextFloat() * 1.8f, r.nextFloat() * 3.0f };
    }
    void tick() override
    {
        static const char* modes[] { "ROOM", "PLATE 1", "PLATE 2", "HALL", "SHIMMER" };
        const int m = juce::jlimit (0, 4, juce::roundToInt (get (ids::reverbMode)));
        const juce::String dot = juce::String::fromUTF8 (" · ");
        title = "REVERB" + dot + modes[m] + utf8 (" · LA VOIX ET SA QUEUE, L'ENVELOPPE À DROITE");
        stat = "PRE-DELAY " + juce::String (juce::roundToInt (get (ids::reverbPreDelay))) + " ms" + dot + "DECAY " + juce::String (get (ids::reverbDecay), 1) + " s" + dot + "CLEAN "
             + juce::String (juce::roundToInt (get (ids::reverbClean))) + " %" + dot + hz (get (ids::reverbLowCut)) + utf8 (" → ") + hz (get (ids::reverbHighCut));
        if (module != nullptr) readScope (module->scope, in, out);
        if (scopeMoved (module) || juce::roundToInt (get (ids::reverbMode)) == 4 || get (ids::reverbModDepth) > 0.5f) repaint();
    }

private:
    float get (const char* id) const { return processor.apvts.getRawParameterValue (ids::inst (id, k))->load(); }
    void draw (juce::Graphics& g) override
    {
        const float L = waveEnd();
        if (! in.empty())
        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (0, 0, (int) L, 360);
            std::vector<float> a (in.size()), b (out.size());
            for (size_t i = 0; i < in.size(); ++i) { a[i] = juce::jlimit (0.0f, 1.0f, in[i] * 1.3f); b[i] = juce::jlimit (0.0f, 1.0f, out[i] * 4.0f); }
            const auto tail = mirror (b, 0.0f, L, 180.0f, 150.0f), voice = mirror (a, 0.0f, L, 180.0f, 150.0f);
            glowPath (g, tail, cEmber.withAlpha (0.6f), 14.0f, true);
            juce::ColourGradient tg (cEmber.withAlpha (0.75f), 0.0f, 0.0f, onirique::look::emberPink.withAlpha (0.35f), L, 0.0f, false);
            g.setGradientFill (tg);
            g.fillPath (tail);
            g.setColour (juce::Colour (244, 236, 236).withAlpha (0.95f));
            g.fillPath (voice);
            const int n = juce::roundToInt (get (ids::reverbMode)) == 4 ? 34 : juce::roundToInt (get (ids::reverbModDepth) / 100.0f * 16.0f);
            const float t = time();
            for (int i = 0; i < std::min (n, (int) sparks.size()); ++i)
            {
                const auto& sp = sparks[(size_t) i];
                const float a2 = 0.5f - 0.5f * std::cos ((t + sp[3]) * 2.0f * kPi / 2.6f);
                g.setColour (onirique::look::emberPink.interpolatedWith (juce::Colours::white, 0.55f).withAlpha (a2));
                g.fillEllipse (juce::Rectangle<float> (2.0f * sp[2], 2.0f * sp[2]).withCentre ({ 40.0f + sp[0] * (L - 60.0f), sp[1] }));
            }
        }
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillRect (L, 0.0f, 1.0f, 360.0f);
        const float pre = get (ids::reverbPreDelay) / 1000.0f, dec = std::max (0.2f, get (ids::reverbDecay));
        const float T = std::max (1.5f, pre + dec * 1.15f), x0 = L + 30.0f, x1 = vw - 20.0f;
        auto X = [&] (float sec) { return x0 + sec / T * (x1 - x0); };
        auto Y = [] (float db) { return 50.0f + (-db) / 60.0f * 270.0f; };
        std::vector<juce::Point<float>> env { { x0, 320.0f }, { X (pre), 320.0f }, { X (pre), Y (0.0f) } };
        for (int i = 1; i <= 40; ++i) { const float sec = dec * (float) i / 40.0f; env.push_back ({ X (pre + sec), Y (-60.0f * sec / dec) }); }
        auto fill = poly (env);
        fill.lineTo (x1, 320.0f); fill.closeSubPath();
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
        if (scopeMoved (module)) repaint();
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
            // en dB (-60 .. 0) : une voix douce reste lisible
            auto nd = [] (float a) { return juce::jlimit (0.0f, 1.0f, (dbOf (a) + 60.0f) / 60.0f); };
            c[i] = nd (in[i]) * 0.95f;
            l[i] = nd (in[(i + n - 4) % n]) * (0.9f + 0.1f * std::sin ((float) i * 0.3f));
            r[i] = nd (in[(i + n - 8) % n]) * (0.9f + 0.1f * std::sin ((float) i * 0.3f + 1.0f));
        }
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawHorizontalLine ((int) (180.0f - off), 0.0f, vw);
        g.drawHorizontalLine ((int) (180.0f + off), 0.0f, vw);
        const float op = 0.25f + amt * 0.75f;
        for (const auto* lane : { &l, &r })
        {
            const auto p = mirror (*lane, 0.0f, vw, lane == &l ? 180.0f - off : 180.0f + off, 50.0f);
            glowPath (g, p, cEmber.withAlpha (0.7f * op), 8.0f, true);
            g.setGradientFill (emberFill (0.0f, 360.0f));
            g.setOpacity (op);
            g.fillPath (p);
            g.setOpacity (1.0f);
        }
        g.setColour (juce::Colour (244, 236, 236).withAlpha (0.92f));
        g.fillPath (mirror (c, 0.0f, vw, 180.0f, 70.0f));
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
        // une marge tout autour pour que la lueur tienne dans le composant (sinon elle est coupée net sur les bords)
        const float pad = 22.0f * ui::unitOf (*this);
        const float s = std::min ((b.getWidth() - 2.0f * pad) / 320.0f, (b.getHeight() - 2.0f * pad) / 195.0f);
        const auto frame = juce::Rectangle<float> (320.0f * s, 195.0f * s).withCentre (b.getCentre());
        if (! glowCache.isValid() || glowCache.getWidth() != getWidth() || glowCache.getHeight() != getHeight())
        {
            // la lueur floue ne change qu'avec la taille : calculée une fois, l'aiguille bouge par-dessus
            glowCache = juce::Image (juce::Image::ARGB, std::max (1, getWidth()), std::max (1, getHeight()), true, juce::SoftwareImageType());
            juce::Graphics cg (glowCache);
            ui::softGlowRect (cg, frame, 16.0f * s, onirique::look::ember.withAlpha (0.32f), pad * 0.9f);
        }
        g.drawImageAt (glowCache, 0, 0);
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
    juce::Image glowCache;
};

// la compression en direct (bas des façades) : la voix qui entre (pâle), qui sort (blanche), et la réduction en braise
// depuis le haut, colonne par colonne avec la forme d'onde (même horloge : ce qu'on voit au même endroit est le même instant)
class CompStrip : public juce::Component, public Animated
{
public:
    explicit CompStrip (ChainModule* m) : module (m) { setInterceptsMouseClicks (false, false); }
    void tick() override
    {
        if (module == nullptr) return;
        const int w = module->scope.write.load (std::memory_order_relaxed);
        if (w == lastWrite) return;   // lecture arrêtée : rien à redessiner
        lastWrite = w;
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
        std::vector<float> in, out, gr;
        readScope (module->scope, in, out, &gr);
        mx = 0.0f;
        for (float v : gr) mx = std::max (mx, v);
        juce::Graphics::ScopedSaveState s (g);
        juce::Path clip; clip.addRoundedRectangle (r, 14.0f * u);
        g.reduceClipRegion (clip);
        // repère de la démo : 1400 x 112, étiré (preserveAspectRatio none)
        g.addTransform (juce::AffineTransform::scale (r.getWidth() / 1400.0f, r.getHeight() / 112.0f).translated (r.getX(), r.getY()));
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
        for (int i = 0; i < m; ++i) curtain.lineTo (1400.0f * (float) i / (float) (m - 1), std::min (56.0f, gr[(size_t) i] * 4.0f));
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
        g.setFont (ui::css (ui::Face::mono, 9.0f * u, 0.12f));
        g.setColour (juce::Colour (0xffd9b8bd));
        g.drawText (utf8 ("RÉDUCTION DE GAIN"), r, juce::Justification::topLeft, false);
        g.setColour (juce::Colours::white);
        g.drawText (utf8 ("−") + juce::String (mx, 1) + " dB MAX", r, juce::Justification::topRight, false);
    }

private:
    ChainModule* module = nullptr;
    float mx = 0.0f;
    int lastWrite = -1;
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
/*  CL1B : une façade à lui, rien du 1176. À gauche la fenêtre de la cellule : la lampe, dont le filament s'allume avec la
    compression, éclaire la cellule optique (le faisceau suit la réduction) ; dessous, l'échelle de réduction horizontale
    0 .. 20 dB, avec la crête tenue. À droite les six réglages sur deux rangées. En bas, l'historique de la compression. */
class Cl1bPanel final : public ModulePanel
{
public:
    Cl1bPanel (OniriqueVocalBoxAudioProcessor& p, int c) : ModulePanel (p, c), strip (engine)
    {
        addAndMakeVisible (strip); anim.push_back (&strip);
        knobs = { &knob (ids::cl1bThreshold, "THRESHOLD"), &knob (ids::cl1bRatio, "RATIO"), &knob (ids::cl1bGain, "GAIN"),
                  &knob (ids::cl1bAttack, "ATTACK"), &knob (ids::cl1bRelease, "RELEASE"), &knob (ids::cl1bMix, "MIX") };
    }
    void tick() override
    {
        ModulePanel::tick();
        const float gr = engine != nullptr ? std::abs (engine->getReductionDb()) : 0.0f;
        shown += (gr > shown ? 0.5f : 0.12f) * (gr - shown);
        if (gr > hold) { hold = gr; holdT = 60; } else if (--holdT < 0) hold = std::max (0.0f, hold - 0.25f);
        flicker = 0.97f + 0.03f * std::sin ((float) juce::Time::getMillisecondCounter() * 0.011f);
        if (std::abs (shown - drawn) > 0.02f || hold != drawnHold || shown > 0.05f)
        {
            drawn = shown; drawnHold = hold;
            repaint (window.toNearestInt().expanded (30));
        }
    }
    void resized() override
    {
        const float u = ui::unitOf (*this);
        auto r = getLocalBounds().toFloat().reduced (40.0f * u, 30.0f * u);
        strip.setBounds (r.removeFromBottom (112.0f * u).toNearestInt());
        r.removeFromBottom (16.0f * u);
        head = r.removeFromTop (40.0f * u);
        r.removeFromTop (12.0f * u);
        window = r.removeFromLeft (std::min (520.0f * u, r.getWidth() * 0.42f));
        r.removeFromLeft (40.0f * u);
        layoutKnobs (knobs, r, 3);
    }
    void paint (juce::Graphics& g) override
    {
        const float u = ui::unitOf (*this);
        const auto r = getLocalBounds().toFloat();
        const float rad = 18.0f * u;
        // la façade : noir chaud, une bande de braise sous le titre, quatre vis
        juce::ColourGradient bg (juce::Colour (0xff1a1213), r.getX(), r.getY(), juce::Colour (0xff070506), r.getX(), r.getBottom(), false);
        g.setGradientFill (bg);
        g.fillRoundedRectangle (r, rad);
        g.setColour (juce::Colour (0xff2b1d20));
        g.drawRoundedRectangle (r.reduced (1.0f * u), rad, 2.0f * u);
        for (auto pt : { r.getTopLeft(), r.getTopRight(), r.getBottomLeft(), r.getBottomRight() })
        {
            const juce::Point<float> c (pt.x + (pt.x < r.getCentreX() ? 18.0f : -18.0f) * u, pt.y + (pt.y < r.getCentreY() ? 18.0f : -18.0f) * u);
            g.setColour (juce::Colour (0xff2a2526));
            g.fillEllipse (juce::Rectangle<float> (11.0f * u, 11.0f * u).withCentre (c));
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.drawLine (c.x - 3.5f * u, c.y + 3.5f * u, c.x + 3.5f * u, c.y - 3.5f * u, 1.4f * u);
        }
        g.setColour (juce::Colour (0xffeee6e6));
        g.setFont (ui::css (ui::Face::display, 22.0f * u, 0.4f));
        g.drawText ("CL1B", head.withWidth (140.0f * u), juce::Justification::centredLeft, false);
        g.setColour (juce::Colour (0xff9a8c8f));
        g.setFont (ui::css (ui::Face::mono, 10.0f * u, 0.16f));
        g.drawText (utf8 ("OPTICAL TUBE COMPRESSOR · CELLULE ET LAMPE"), head.withTrimmedLeft (130.0f * u), juce::Justification::centredLeft, false);
        juce::ColourGradient band (cEmber.withAlpha (0.0f), head.getX(), 0.0f, cEmber.withAlpha (0.0f), head.getRight(), 0.0f, false);
        band.addColour (0.3, cEmber.withAlpha (0.8f));
        g.setGradientFill (band);
        g.fillRect (head.getX(), head.getBottom() + 2.0f * u, head.getWidth(), 1.5f * u);

        // la fenêtre de la cellule
        const auto w = window;
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (w, 16.0f * u);
        g.setColour (juce::Colour (255, 220, 225).withAlpha (0.10f));
        g.drawRoundedRectangle (w.reduced (0.5f), 16.0f * u, 1.0f * u);
        const float lit = juce::jlimit (0.0f, 1.0f, shown / 14.0f) * flicker;
        auto glass = w.withTrimmedBottom (w.getHeight() * 0.32f).reduced (26.0f * u, 22.0f * u);
        // la lampe : une ampoule de verre, son filament
        const auto bulb = glass.removeFromLeft (glass.getWidth() * 0.42f);
        const auto bulbBody = juce::Rectangle<float> (bulb.getWidth() * 0.62f, bulb.getHeight() * 0.92f).withCentre (bulb.getCentre());
        juce::ColourGradient halo (cEmber.withAlpha (0.10f + 0.55f * lit), bulbBody.getCentreX(), bulbBody.getCentreY(),
                                   cEmber.withAlpha (0.0f), bulbBody.getCentreX() + bulbBody.getWidth() * 1.1f, bulbBody.getCentreY(), true);
        g.setGradientFill (halo);
        g.fillEllipse (bulbBody.expanded (bulbBody.getWidth() * 0.6f));
        {
            // la lampe : une enveloppe de verre au sommet arrondi sur un culot sombre, l'anode grise, le filament au centre
            const auto glassR = bulbBody.withTrimmedBottom (bulbBody.getHeight() * 0.14f);
            const auto base = bulbBody.withTop (glassR.getBottom()).reduced (bulbBody.getWidth() * 0.12f, 0.0f);
            juce::Path env;
            const float rw = glassR.getWidth() * 0.5f;
            env.startNewSubPath (glassR.getX(), glassR.getBottom());
            env.lineTo (glassR.getX(), glassR.getY() + rw);
            env.addCentredArc (glassR.getCentreX(), glassR.getY() + rw, rw, rw, 0.0f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi);
            env.lineTo (glassR.getRight(), glassR.getBottom());
            env.closeSubPath();
            g.setColour (juce::Colours::white.withAlpha (0.07f));
            g.fillPath (env);
            g.setColour (juce::Colours::white.withAlpha (0.26f));
            g.strokePath (env, juce::PathStrokeType (1.5f * u));
            g.setColour (juce::Colour (0xff2a2224));
            g.fillRoundedRectangle (base, 4.0f * u);
            g.setColour (juce::Colours::white.withAlpha (0.12f));
            for (int i = 1; i < 3; ++i) g.fillRect (base.getX(), base.getY() + base.getHeight() * (float) i / 3.0f, base.getWidth(), 1.0f * u);
            // l'anode : une plaque grise, ouverte devant le filament
            const auto plate = juce::Rectangle<float> (glassR.getWidth() * 0.56f, glassR.getHeight() * 0.42f).withCentre ({ glassR.getCentreX(), glassR.getCentreY() + glassR.getHeight() * 0.06f });
            g.setColour (juce::Colour (0xff3a3335).withAlpha (0.9f));
            g.drawRect (plate, 2.0f * u);
            // les deux supports et le filament (une spirale serrée), qui rougit puis blanchit avec la compression
            const float cx = plate.getCentreX(), top = plate.getY() + plate.getHeight() * 0.2f, bot = plate.getBottom() - plate.getHeight() * 0.2f;
            g.setColour (juce::Colour (0xff5a5254));
            g.drawLine (cx - 9.0f * u, bot, cx - 9.0f * u, base.getY(), 1.2f * u);
            g.drawLine (cx + 9.0f * u, bot, cx + 9.0f * u, base.getY(), 1.2f * u);
            juce::Path fil;
            fil.startNewSubPath (cx - 9.0f * u, bot);
            const int turns = 7;
            for (int i = 0; i <= turns * 8; ++i)
            {
                const float t = (float) i / (float) (turns * 8);
                const float y = bot + (top - bot) * std::sin (t * juce::MathConstants<float>::pi);
                fil.lineTo (cx - 9.0f * u + 18.0f * u * t + 3.0f * u * std::sin (t * juce::MathConstants<float>::twoPi * (float) turns), y);
            }
            glowPath (g, fil, cEmber.withAlpha (0.25f + 0.75f * lit), 6.0f + 12.0f * lit, false);
            g.setColour (cEmber.interpolatedWith (juce::Colours::white, 0.55f * lit).withAlpha (0.4f + 0.6f * lit));
            g.strokePath (fil, juce::PathStrokeType (1.6f * u));
            // un reflet sur le verre
            g.setColour (juce::Colours::white.withAlpha (0.10f));
            g.fillRoundedRectangle (juce::Rectangle<float> (glassR.getX() + rw * 0.25f, glassR.getY() + rw * 0.5f, rw * 0.18f, glassR.getHeight() * 0.55f), rw * 0.09f);
        }
        // le faisceau, puis la cellule optique (une pastille de verre qui s'éclaire)
        const float side = std::min (glass.getWidth(), glass.getHeight()) * 0.62f;
        const auto cell = juce::Rectangle<float> (side, side).withCentre (glass.getCentre().translated (glass.getWidth() * 0.12f, 0.0f));
        {
            juce::Path beam;
            beam.startNewSubPath (bulbBody.getRight(), bulbBody.getCentreY() - 10.0f * u);
            beam.lineTo (cell.getCentreX(), cell.getY());
            beam.lineTo (cell.getCentreX(), cell.getBottom());
            beam.lineTo (bulbBody.getRight(), bulbBody.getCentreY() + 10.0f * u);
            beam.closeSubPath();
            juce::ColourGradient bm (cEmber.withAlpha (0.45f * lit), bulbBody.getRight(), 0.0f, cEmber.withAlpha (0.05f * lit), cell.getX(), 0.0f, false);
            g.setGradientFill (bm);
            g.fillPath (beam);
        }
        g.setColour (juce::Colour (0xff120c0d));
        g.fillEllipse (cell);
        juce::ColourGradient cg (cEmber.interpolatedWith (juce::Colours::white, 0.3f).withAlpha (0.15f + 0.8f * lit), cell.getCentreX(), cell.getCentreY(),
                                 cEmber.withAlpha (0.0f), cell.getRight(), cell.getCentreY(), true);
        g.setGradientFill (cg);
        g.fillEllipse (cell.reduced (cell.getWidth() * 0.08f));
        g.setColour (juce::Colours::white.withAlpha (0.18f));
        g.drawEllipse (cell, 1.4f * u);
        {   // le sillon de la cellule (la piste résistive)
            juce::Path z;
            const auto in = cell.reduced (cell.getWidth() * 0.28f);
            for (int i = 0; i < 5; ++i)
            {
                const float y = in.getY() + in.getHeight() * (float) i / 4.0f;
                if (i == 0) z.startNewSubPath (in.getX(), y); else z.lineTo (i % 2 ? in.getRight() : in.getX(), y);
            }
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.strokePath (z, juce::PathStrokeType (1.6f * u));
        }
        g.setColour (juce::Colour (0xff9a8c8f));
        g.setFont (ui::css (ui::Face::mono, 9.0f * u, 0.14f));
        g.drawText ("LAMPE", juce::Rectangle<float> (bulb.getX(), w.getY() + 10.0f * u, bulb.getWidth(), 12.0f * u), juce::Justification::centred, false);
        g.drawText ("CELLULE", juce::Rectangle<float> (cell.getX() - 30.0f * u, w.getY() + 10.0f * u, cell.getWidth() + 60.0f * u, 12.0f * u), juce::Justification::centred, false);

        // l'échelle de réduction, horizontale
        auto sc = w.withTrimmedTop (w.getHeight() * 0.70f).reduced (26.0f * u, 14.0f * u);
        const auto track = sc.withHeight (10.0f * u).withY (sc.getY() + 6.0f * u);
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.fillRoundedRectangle (track, 5.0f * u);
        auto xOf = [&] (float db) { return track.getX() + track.getWidth() * std::sqrt (juce::jlimit (0.0f, 20.0f, db) / 20.0f); };
        const auto fillR = track.withRight (xOf (shown));
        juce::ColourGradient fg (cEmber.withAlpha (0.7f), track.getX(), 0.0f, cEmber.interpolatedWith (juce::Colours::white, 0.35f), track.getRight(), 0.0f, false);
        g.setGradientFill (fg);
        g.fillRoundedRectangle (fillR, 5.0f * u);
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.fillRect (juce::Rectangle<float> (2.0f * u, track.getHeight() + 6.0f * u).withCentre ({ xOf (hold), track.getCentreY() }));
        g.setFont (ui::css (ui::Face::mono, 9.0f * u, 0.1f));
        for (float db : { 0.0f, 1.0f, 2.0f, 4.0f, 6.0f, 10.0f, 15.0f, 20.0f })
        {
            const float x = xOf (db);
            g.setColour (juce::Colours::white.withAlpha (0.25f));
            g.fillRect (x - 0.5f * u, track.getBottom() + 4.0f * u, 1.0f * u, 5.0f * u);
            g.setColour (juce::Colour (0xffbba7aa));
            g.drawText (juce::String ((int) db), juce::Rectangle<float> (x - 14.0f * u, track.getBottom() + 10.0f * u, 28.0f * u, 12.0f * u), juce::Justification::centred, false);
        }
        g.setColour (juce::Colour (0xffe9e1e1));
        g.setFont (ui::css (ui::Face::mono, 11.0f * u, 0.12f));
        g.drawText ("GR  -" + juce::String (shown, 1) + " dB", juce::Rectangle<float> (track.getX(), track.getY() - 22.0f * u, track.getWidth(), 14.0f * u),
                    juce::Justification::centredRight, false);
    }

private:
    std::vector<StarKnob*> knobs;
    CompStrip strip;
    juce::Rectangle<float> head, window;
    float shown = 0.0f, drawn = -1.0f, hold = 0.0f, drawnHold = -1.0f, flicker = 1.0f;
    int holdT = 0;
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
//==============================================================================
// CREATIVE FX : quatre pastilles (DOUBLER, CHORUS, FLANGER, PHONE) ; le témoin allume ou coupe l'effet, le nom l'affiche
// dessous avec son écran complet ; AU HASARD tire un point de départ musical
class CreativeChips : public juce::Component, public Animated
{
public:
    CreativeChips (OniriqueVocalBoxAudioProcessor& p, int k) : processor (p), inst (k) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    std::function<void (int)> onSelect;
    std::function<void()> onDice;
    int selected = 0;

    static constexpr int kTypes[4] { modules::doubler, modules::chorus, modules::flanger, modules::phone };
    static const char* name (int i) { static const char* n[4] { "DOUBLER", "CHORUS", "FLANGER", "PHONE" }; return n[i]; }

    void tick() override
    {
        bool moved = false;
        for (int i = 0; i < 5; ++i)
        {
            const bool on = i < 4 && isOn (i);
            moved |= ui::approach (lit[i], on ? 1.0f : 0.0f, 0.2f);
            moved |= ui::approach (hot[i], i == hover ? 1.0f : 0.0f, 0.16f);
            moved |= ui::approach (sel[i], i == selected ? 1.0f : 0.0f, 0.2f);
        }
        if (moved) repaint();
    }
    void paint (juce::Graphics& g) override
    {
        const float u = ui::unitOf (*this);
        const auto box = chipsBox();
        ui::drawDemoGlass (g, box, box.getHeight() * 0.5f, u, false);
        for (int i = 0; i < 4; ++i)
        {
            const auto r = chip (i);
            if (sel[i] > 0.01f) ui::drawSeg (g, r, sel[i], 0.0f, u);
            else if (hot[i] > 0.01f) ui::drawSeg (g, r, 0.0f, hot[i], u);
            // le témoin : braise allumé, gris éteint
            const auto led = juce::Rectangle<float> (10.0f * u, 10.0f * u).withCentre ({ r.getX() + 22.0f * u, r.getCentreY() });
            if (lit[i] > 0.01f) ui::softGlowEllipse (g, led, cEmber.withAlpha (0.8f * lit[i]), 8.0f * u);
            g.setColour (cEmber.interpolatedWith (juce::Colour (0xff4a3a3e), 1.0f - lit[i]));
            g.fillEllipse (led);
            g.setColour (juce::Colour (0xffbba7aa).interpolatedWith (juce::Colours::white, std::max (sel[i], hot[i])));
            g.setFont (ui::css (ui::Face::mono, 11.0f * u, 0.14f));
            g.drawText (name (i), r.withTrimmedLeft (36.0f * u), juce::Justification::centredLeft, false);
        }
        // AU HASARD
        const auto d = dice();
        ui::drawDemoGlass (g, d, d.getHeight() * 0.5f, u, false);
        if (hot[4] > 0.01f) ui::drawSeg (g, d.reduced (4.0f * u), 0.0f, hot[4], u);
        const auto ic = juce::Rectangle<float> (14.0f * u, 14.0f * u).withCentre ({ d.getX() + 26.0f * u, d.getCentreY() });
        g.setColour (cPink);
        g.drawRoundedRectangle (ic, 3.0f * u, 1.5f * u);
        for (auto pt : { ic.getCentre(), ic.getTopLeft() + juce::Point<float> (4.0f * u, 4.0f * u), ic.getBottomRight() - juce::Point<float> (4.0f * u, 4.0f * u) })
            g.fillEllipse (juce::Rectangle<float> (2.6f * u, 2.6f * u).withCentre (pt));
        g.setColour (juce::Colour (0xffbba7aa).interpolatedWith (juce::Colours::white, hot[4]));
        g.setFont (ui::css (ui::Face::mono, 11.0f * u, 0.14f));
        g.drawText ("AU HASARD", d.withTrimmedLeft (44.0f * u), juce::Justification::centredLeft, false);
    }
    void mouseMove (const juce::MouseEvent& e) override { const int h = zoneAt (e.position); if (h != hover) { hover = h; repaint(); } }
    void mouseExit (const juce::MouseEvent&) override { hover = -1; }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int z = zoneAt (e.position);
        if (z == 4) { if (onDice) onDice(); return; }
        if (z < 0) return;
        // le témoin (ou un clic sur la pastille déjà affichée) allume / coupe ; ailleurs, la pastille s'affiche
        const bool onLed = e.position.x < chip (z).getX() + 36.0f * ui::unitOf (*this);
        if (onLed || z == selected)
        {
            if (auto* prm = processor.param (modules::info (kTypes[z]).onId, inst)) prm->setValueNotifyingHost (isOn (z) ? 0.0f : 1.0f);
        }
        if (z != selected) { selected = z; if (onSelect) onSelect (z); }
    }

private:
    bool isOn (int i) const { return processor.apvts.getRawParameterValue (ids::inst (modules::info (kTypes[i]).onId, inst))->load() > 0.5f; }
    juce::Rectangle<float> chipsBox() const
    {
        const float u = ui::unitOf (*this);
        return getLocalBounds().toFloat().withWidth (4.0f * 150.0f * u + 8.0f * u);
    }
    juce::Rectangle<float> chip (int i) const
    {
        const float u = ui::unitOf (*this);
        const auto b = chipsBox().reduced (4.0f * u);
        return { b.getX() + (float) i * 150.0f * u, b.getY(), 150.0f * u - (i < 3 ? 4.0f * u : 0.0f), b.getHeight() };
    }
    juce::Rectangle<float> dice() const
    {
        const float u = ui::unitOf (*this);
        const auto b = chipsBox();
        return { b.getRight() + 14.0f * u, b.getY(), 150.0f * u, b.getHeight() };
    }
    int zoneAt (juce::Point<float> p) const
    {
        for (int i = 0; i < 4; ++i) if (chip (i).contains (p)) return i;
        return dice().contains (p) ? 4 : -1;
    }

    OniriqueVocalBoxAudioProcessor& processor;
    int inst, hover = -1;
    float lit[5] {}, hot[5] {}, sel[5] {};
};

class CreativePanel final : public ModulePanel
{
public:
    CreativePanel (OniriqueVocalBoxAudioProcessor& p, int c) : ModulePanel (p, c), chips (p, modules::instanceOf (c))
    {
        addAndMakeVisible (chips);
        addAnim (&chips);
        chips.onSelect = [this] (int i) { show (i); };
        chips.onDice = [this] { roll(); };
        // l'effet affiché d'abord : le premier allumé
        int first = 0;
        for (int i = 0; i < 4; ++i)
            if (p.apvts.getRawParameterValue (ids::inst (modules::info (CreativeChips::kTypes[i]).onId, inst))->load() > 0.5f) { first = i; break; }
        chips.selected = first;
        show (first);
    }
    void tick() override
    {
        chips.tick();
        if (sub != nullptr) sub->tick();
    }
    void resized() override
    {
        const float u = ui::unitOf (*this);
        auto r = getLocalBounds().toFloat();
        chips.setBounds (r.removeFromTop (42.0f * u).toNearestInt());
        r.removeFromTop (14.0f * u);
        if (sub != nullptr) sub->setBounds (r.toNearestInt());
    }

private:
    void show (int i)
    {
        if (sub != nullptr) removeChildComponent (sub.get());
        sub = createModulePanel (processor, modules::code (CreativeChips::kTypes[i], inst));
        addAndMakeVisible (*sub);
        resized();
    }
    // un point de départ musical : un ou deux effets allumés, des réglages dans leur zone utile
    void roll()
    {
        auto& r = juce::Random::getSystemRandom();
        auto set = [this] (const char* id, float v) { if (auto* p = processor.param (id, inst)) p->setValueNotifyingHost (p->convertTo0to1 (v)); };
        auto between = [&r] (float a, float b) { return a + (b - a) * r.nextFloat(); };
        bool on[4] { r.nextFloat() < 0.6f, r.nextFloat() < 0.4f, r.nextFloat() < 0.3f, r.nextFloat() < 0.15f };
        if (! (on[0] || on[1] || on[2] || on[3])) on[r.nextInt (3)] = true;
        if (on[1] && on[2]) on[r.nextBool() ? 1 : 2] = false;   // chorus et flanger ensemble : trop
        set (ids::doublerAmount, between (30.0f, 70.0f));
        set (ids::chorusRate, between (0.3f, 1.4f)); set (ids::chorusDepth, between (20.0f, 60.0f)); set (ids::chorusMix, between (15.0f, 40.0f));
        set (ids::flangerRate, between (0.08f, 0.5f)); set (ids::flangerDepth, between (30.0f, 80.0f));
        set (ids::flangerFeedback, between (10.0f, 55.0f)); set (ids::flangerMix, between (20.0f, 45.0f));
        set (ids::phoneAmount, between (35.0f, 80.0f));
        for (int i = 0; i < 4; ++i) set (modules::info (CreativeChips::kTypes[i]).onId, on[i] ? 1.0f : 0.0f);
        for (int i = 0; i < 4; ++i) if (on[i]) { chips.selected = i; show (i); break; }
    }

    CreativeChips chips;
    std::unique_ptr<ModulePanel> sub;
};

//==============================================================================
/*  TUNER : une façade à lui. En haut à gauche l'écran de hauteur (comme le mode graphique d'un accordeur de studio) : les
    lignes des notes, celles de la gamme éclairées ; la voix entendue en pointillé clair, la voix accordée en braise.
    À droite, la note chantée en grand et l'écart en cents (entendu / corrigé). Dessous, le clavier de la tonalité (un clic
    choisit la tonique, les touches de la gamme s'allument) et la gamme, puis le registre, HARD TUNE et FORMANT, et
    les cinq potards. */
class TuneKeyboard : public juce::Component
{
public:
    TuneKeyboard (OniriqueVocalBoxAudioProcessor& p, int k) : processor (p), inst (k) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (juce::Graphics& g) override
    {
        const float u = ui::unitOf (*this);
        const auto r = getLocalBounds().toFloat();
        const int key = juce::roundToInt (get (ids::tuneKey)), sc = juce::roundToInt (get (ids::tuneScale));
        const std::uint16_t m = tune::pitchClassMask (tune::scales[juce::jlimit (0, tune::numScales - 1, sc)].mask, key);
        static const bool black[12] { false, true, false, true, false, false, true, false, true, false, true, false };
        static const char* names[12] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const float w = r.getWidth() / 7.0f;
        int white = 0;
        // touches blanches puis noires par-dessus
        for (int pass = 0; pass < 2; ++pass)
        {
            white = 0;
            for (int n = 0; n < 12; ++n)
            {
                juce::Rectangle<float> kr;
                if (! black[n]) { kr = { r.getX() + (float) white * w, r.getY(), w, r.getHeight() }; ++white; }
                else kr = { r.getX() + (float) white * w - w * 0.3f, r.getY(), w * 0.6f, r.getHeight() * 0.58f };
                if ((pass == 0) == black[n]) continue;
                keyRect[n] = kr;
                const bool inScale = (m >> n) & 1, tonic = n == key;
                const auto body = kr.reduced (1.5f * u, 0.0f);
                g.setColour (black[n] ? juce::Colour (0xff0d0a0b) : juce::Colour (0xff1c1517));
                g.fillRoundedRectangle (body, 5.0f * u);
                if (inScale)
                {
                    g.setColour (cEmber.withAlpha (tonic ? 0.85f : 0.28f));
                    g.fillRoundedRectangle (body.withTrimmedTop (body.getHeight() * 0.55f).reduced (3.0f * u, 3.0f * u), 4.0f * u);
                }
                g.setColour (juce::Colour (255, 220, 225).withAlpha (tonic ? 0.5f : 0.12f));
                g.drawRoundedRectangle (body, 5.0f * u, (tonic ? 1.6f : 1.0f) * u);
                if (! black[n] || tonic)
                {
                    g.setColour (tonic ? juce::Colours::white : juce::Colour (0xff9a8c8f));
                    g.setFont (ui::css (ui::Face::mono, 9.5f * u, 0.08f));
                    g.drawText (names[n], body.withTrimmedTop (body.getHeight() * 0.18f).withHeight (14.0f * u), juce::Justification::centred, false);
                }
            }
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        static const bool black[12] { false, true, false, true, false, false, true, false, true, false, true, false };
        int hit = -1;
        for (int n = 0; n < 12; ++n) if (black[n] && keyRect[n].contains (e.position)) hit = n;
        if (hit < 0) for (int n = 0; n < 12; ++n) if (! black[n] && keyRect[n].contains (e.position)) hit = n;
        if (hit < 0) return;
        if (auto* prm = processor.apvts.getParameter (ids::inst (ids::tuneKey, inst)))
        {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (prm->convertTo0to1 ((float) hit));
            prm->endChangeGesture();
        }
        repaint();
    }
    float get (const char* id) const { return processor.apvts.getRawParameterValue (ids::inst (id, inst))->load(); }

private:
    OniriqueVocalBoxAudioProcessor& processor;
    int inst;
    juce::Rectangle<float> keyRect[12];
};

// les sélecteurs de la tonalité, comme dans ONIRIQUE TUNER : KEY (les 12 toniques) et SCALE (les 15 gammes) ; une gélule de
// verre, un clic ouvre le menu. Le clavier dessous choisit aussi la tonique.
class ScaleSelector : public juce::Component
{
public:
    ScaleSelector (OniriqueVocalBoxAudioProcessor& p, int k, bool keyMode) : processor (p), inst (k), isKey (keyMode)
    { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (juce::Graphics& g) override
    {
        const float u = ui::unitOf (*this);
        const auto r = getLocalBounds().toFloat().reduced (1.0f);
        const float rad = r.getHeight() * 0.5f;
        g.setColour (juce::Colour (0xff120d0f).withAlpha (0.92f));
        g.fillRoundedRectangle (r, rad);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (hover ? 0.08f : 0.05f), 0.0f, r.getY(), juce::Colours::white.withAlpha (0.0f), 0.0f, r.getBottom(), false));
        g.fillRoundedRectangle (r, rad);
        g.setColour (juce::Colour (255, 220, 225).withAlpha (hover ? 0.22f : 0.12f));
        g.drawRoundedRectangle (r, rad, 1.0f * u);
        auto t = r.reduced (18.0f * u, 0.0f);
        g.setFont (ui::css (ui::Face::mono, 9.0f * u, 0.16f));
        g.setColour (juce::Colour (0xff9a8c8f));
        g.drawText (isKey ? "KEY" : "SCALE", t.removeFromLeft ((isKey ? 36.0f : 52.0f) * u), juce::Justification::centredLeft, false);
        g.setFont (ui::css (ui::Face::mono, 11.0f * u, 0.14f));
        g.setColour (juce::Colours::white);
        const int v = juce::roundToInt (value());
        g.drawText (isKey ? juce::String (tune::noteNames[juce::jlimit (0, 11, v)])
                          : juce::String (tune::scales[juce::jlimit (0, tune::numScales - 1, v)].name).toUpperCase(),
                    t.withTrimmedRight (20.0f * u), juce::Justification::centredLeft, true);
        juce::Path chev;
        const float cx = t.getRight() - 6.0f * u, cy = t.getCentreY();
        chev.startNewSubPath (cx - 5.0f * u, cy - 2.5f * u); chev.lineTo (cx, cy + 2.5f * u); chev.lineTo (cx + 5.0f * u, cy - 2.5f * u);
        g.setColour (cEmber);
        g.strokePath (chev, juce::PathStrokeType (1.6f * u, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseDown (const juce::MouseEvent&) override
    {
        juce::PopupMenu m;
        m.setLookAndFeel (&getLookAndFeel());   // le menu de VOCALBOX (verre sombre, braise), pas celui du système
        const int now = juce::roundToInt (value());
        if (isKey)
            for (int i = 0; i < 12; ++i) m.addItem (i + 1, tune::noteNames[i], true, i == now);
        else
        {
            auto add = [&] (int i) { m.addItem (i + 1, juce::String (tune::scales[i].name).toUpperCase(), true, i == now); };
            add (0);
            m.addSectionHeader (utf8 ("MAJEURES ET MINEURES"));
            for (int i : { 1, 2, 3, 4 }) add (i);
            m.addSectionHeader ("MODES");
            for (int i : { 5, 6, 7, 8, 9 }) add (i);
            m.addSectionHeader (utf8 ("PENTATONIQUES ET AUTRES"));
            for (int i : { 10, 11, 12, 13, 14 }) add (i);
        }
        juce::Component::SafePointer<ScaleSelector> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (getWidth()), [safe] (int r)
        {
            if (safe == nullptr || r <= 0) return;
            if (auto* prm = safe->processor.apvts.getParameter (ids::inst (safe->isKey ? ids::tuneKey : ids::tuneScale, safe->inst)))
            {
                prm->beginChangeGesture();
                prm->setValueNotifyingHost (prm->convertTo0to1 ((float) (r - 1)));
                prm->endChangeGesture();
            }
            safe->repaint();
        });
    }
    float value() const { return processor.apvts.getRawParameterValue (ids::inst (isKey ? ids::tuneKey : ids::tuneScale, inst))->load(); }

private:
    OniriqueVocalBoxAudioProcessor& processor;
    int inst;
    bool isKey, hover = false;
};

class TunerPanel final : public ModulePanel
{
public:
    TunerPanel (OniriqueVocalBoxAudioProcessor& p, int c) : ModulePanel (p, c), keys (p, inst), keySel (p, inst, true), scale (p, inst, false)
    {
        addAndMakeVisible (keys);
        addAndMakeVisible (keySel);
        addAndMakeVisible (scale);
        voice = &tabs (ids::tuneVoice, juce::StringArray { "SOPRANO", "ALTO / TENOR", "BARITONE" });
        hard = &toggle (ids::tuneHard, "HARD TUNE");
        formant = &toggle (ids::tuneFormant, "FORMANT");
        knobs = { &knob (ids::tuneRetune, "RETUNE SPEED"), &knob (ids::tuneHumanize, "HUMANIZE"), &knob (ids::tuneFlex, "FLEX-TUNE"),
                  &knob (ids::tuneVibrato, "NATURAL VIBRATO"), &knob (ids::tuneMix, "MIX") };
        hist.resize (kHist);
    }

    void tick() override
    {
        ModulePanel::tick();
        bool got = false;
        if (auto* t = dynamic_cast<chainmods::Tuner*> (engine))
        {
            tune::DisplayFrame f;
            while (t->popFrame (f))
            {
                hist[(size_t) hpos] = { f.inMidi, f.outMidi, f.voiced && f.level > 3.0e-4f };
                hpos = (hpos + 1) % kHist;
                got = true;
                if (f.voiced && f.level > 1.0e-4f) { lastIn = f.inMidi; lastOut = f.outMidi; lastNote = f.noteMidi; quiet = 0; }
            }
        }
        if (! got && ++quiet > 30) { lastIn = 0.0f; }
        // le centre de l'écran suit la voix en douceur
        if (lastIn > 0.0f) centre += 0.05f * (lastNote - centre);
        if (got || keys.isMouseOverOrDragging()) { repaint (screen.toNearestInt().expanded (4)); repaint (readout.toNearestInt().expanded (4)); }
        if (keyStamp != stamp()) { keyStamp = stamp(); keys.repaint(); keySel.repaint(); scale.repaint(); repaint (screen.toNearestInt()); }
    }

    void resized() override
    {
        const float u = ui::unitOf (*this);
        auto r = getLocalBounds().toFloat().reduced (40.0f * u, 30.0f * u);
        head = r.removeFromTop (40.0f * u);
        r.removeFromTop (14.0f * u);
        const float cellH = (104.0f + 53.0f) * u;
        auto knobRow = r.removeFromBottom (cellH);
        r.removeFromBottom (14.0f * u);
        auto row2 = r.removeFromBottom (42.0f * u);
        r.removeFromBottom (12.0f * u);
        auto row1 = r.removeFromBottom (54.0f * u);
        r.removeFromBottom (16.0f * u);
        readout = r.removeFromRight (std::min (330.0f * u, r.getWidth() * 0.3f));
        r.removeFromRight (20.0f * u);
        screen = r;
        keys.setBounds (row1.removeFromLeft (std::min (420.0f * u, row1.getWidth() * 0.45f)).toNearestInt());
        row1.removeFromLeft (18.0f * u);
        keySel.setBounds (row1.removeFromLeft (std::min (row1.getWidth(), 130.0f * u)).withSizeKeepingCentre (std::min (row1.getWidth(), 130.0f * u), 42.0f * u).toNearestInt());
        row1.removeFromLeft (12.0f * u);
        scale.setBounds (row1.removeFromLeft (std::min (row1.getWidth(), 280.0f * u)).withSizeKeepingCentre (std::min (row1.getWidth(), 280.0f * u), 42.0f * u).toNearestInt());
        auto place = [&] (juce::Component& c, float w) { c.setBounds (row2.removeFromLeft (std::min (w, row2.getWidth())).toNearestInt()); row2.removeFromLeft (14.0f * u); };
        place (*voice, voice->idealWidth (0.0f));
        place (*hard, hard->idealWidth());
        place (*formant, formant->idealWidth());
        const float gap = 10.0f * u, cw = (knobRow.getWidth() - 4.0f * gap) / 5.0f;
        for (int i = 0; i < 5; ++i)
            knobs[(size_t) i]->setBounds (juce::Rectangle<float> (knobRow.getX() + (float) i * (cw + gap), knobRow.getY(), cw, cellH).toNearestInt());
    }

    void paint (juce::Graphics& g) override
    {
        const float u = ui::unitOf (*this);
        const auto r = getLocalBounds().toFloat();
        const float rad = 20.0f * u;
        // la façade : nuit profonde, une ligne de portée qui traverse le haut
        juce::ColourGradient bg (juce::Colour (0xff160f12), r.getX(), r.getY(), juce::Colour (0xff060506), r.getRight(), r.getBottom(), false);
        bg.addColour (0.55, juce::Colour (0xff0d090b));
        g.setGradientFill (bg);
        g.fillRoundedRectangle (r, rad);
        g.setColour (juce::Colour (255, 220, 225).withAlpha (0.09f));
        g.drawRoundedRectangle (r.reduced (1.0f * u), rad, 1.5f * u);
        g.setColour (juce::Colour (0xffeee6e6));
        g.setFont (ui::css (ui::Face::display, 22.0f * u, 0.4f));
        g.drawText ("TUNER", head.withWidth (150.0f * u), juce::Justification::centredLeft, false);
        g.setColour (juce::Colour (0xff9a8c8f));
        g.setFont (ui::css (ui::Face::mono, 10.0f * u, 0.16f));
        g.drawText (utf8 ("ONIRIQUE PITCH CORRECTION · HQ, LE PLUS TRANSPARENT"),
                    head.withTrimmedLeft (140.0f * u), juce::Justification::centredLeft, false);
        // cinq lignes de portée en braise, comme une partition qui file sous le titre
        for (int i = 0; i < 5; ++i)
        {
            juce::ColourGradient band (cEmber.withAlpha (0.0f), head.getX(), 0.0f, cEmber.withAlpha (0.0f), head.getRight(), 0.0f, false);
            band.addColour (0.25, cEmber.withAlpha (i == 2 ? 0.7f : 0.18f));
            g.setGradientFill (band);
            g.fillRect (head.getX(), head.getBottom() + (2.0f + 3.0f * (float) i) * u, head.getWidth(), 0.8f * u);
        }
        paintScreen (g, u);
        paintReadout (g, u);
    }

private:
    struct Pt { float in = 0.0f, out = 0.0f; bool voiced = false; };
    static constexpr int kHist = 1400;   // ~3,8 s de mesures

    juce::int64 stamp() const { return (juce::int64) value (ids::tuneKey) * 16 + (juce::int64) value (ids::tuneScale); }
    std::uint16_t scaleMask() const
    {
        return tune::pitchClassMask (tune::scales[juce::jlimit (0, tune::numScales - 1, juce::roundToInt (value (ids::tuneScale)))].mask, juce::roundToInt (value (ids::tuneKey)));
    }

    void paintScreen (juce::Graphics& g, float u)
    {
        const auto s = screen;
        g.setColour (juce::Colours::black.withAlpha (0.42f));
        g.fillRoundedRectangle (s, 16.0f * u);
        g.setColour (juce::Colour (255, 220, 225).withAlpha (0.08f));
        g.drawRoundedRectangle (s.reduced (0.5f), 16.0f * u, 1.0f * u);
        juce::Graphics::ScopedSaveState save (g);
        juce::Path clip; clip.addRoundedRectangle (s, 16.0f * u);
        g.reduceClipRegion (clip);
        const float span = 14.0f;   // demi-tons affichés
        const float c = centre > 0.0f ? centre : 60.0f;
        auto Y = [&] (float midi) { return s.getCentreY() - (midi - c) / span * s.getHeight(); };
        const auto m = scaleMask();
        static const char* names[12] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        g.setFont (ui::css (ui::Face::mono, 9.0f * u, 0.06f));
        // les noms : toutes les notes de la gamme s'il y a la place, sinon une sur deux (la tonique toujours)
        const float step = s.getHeight() / span;
        const int key = juce::roundToInt (value (ids::tuneKey));
        int shownCount = 0;
        for (int n = (int) std::floor (c - span * 0.5f) - 1; n <= (int) std::ceil (c + span * 0.5f) + 1; ++n)
        {
            const int pc = ((n % 12) + 12) % 12;
            const bool on = (m >> pc) & 1;
            const float y = Y ((float) n);
            g.setColour (on ? juce::Colour (255, 220, 225).withAlpha (pc == key ? 0.16f : 0.08f)
                            : juce::Colours::white.withAlpha (0.02f));
            g.fillRect (s.getX() + 40.0f * u, y - 0.5f * u, s.getWidth() - 40.0f * u, 1.0f * u);
            const bool label = on && (pc == key || step >= 13.0f * u || (step >= 7.0f * u && shownCount++ % 2 == 0));
            if (label)
            {
                g.setColour (juce::Colour (0xff9a8c8f));
                g.drawText (juce::String (names[pc]) + juce::String (n / 12 - 1), juce::Rectangle<float> (s.getX() + 8.0f * u, y - 7.0f * u, 32.0f * u, 14.0f * u),
                            juce::Justification::centredLeft, false);
            }
        }
        // la voix entendue (pointillé clair) et la voix accordée (braise), de la plus ancienne à la plus récente
        juce::Path pin, pout;
        bool inOpen = false, outOpen = false;
        const float x0 = s.getX() + 44.0f * u, w = s.getWidth() - 56.0f * u;
        // une mesure isolée (moins de 20 ms de voix) n'est pas tracée : un bruit, pas une note
        std::vector<char> keep ((size_t) kHist, 0);
        for (int i = 0; i < kHist;)
        {
            auto ok = [&] (int j) { const auto& q = hist[(size_t) ((hpos + j) % kHist)]; return q.voiced && q.in > 20.0f && q.out > 20.0f; };
            if (! ok (i)) { ++i; continue; }
            int e = i;
            while (e < kHist && ok (e)) ++e;
            if (e - i >= 8) std::fill (keep.begin() + i, keep.begin() + e, (char) 1);
            i = e;
        }
        for (int i = 0; i < kHist; ++i)
        {
            const auto& pt = hist[(size_t) ((hpos + i) % kHist)];
            const float x = x0 + w * (float) i / (float) (kHist - 1);
            if (! keep[(size_t) i]) { inOpen = outOpen = false; continue; }
            const float yi = Y (pt.in), yo = Y (pt.out);
            if (inOpen) pin.lineTo (x, yi); else { pin.startNewSubPath (x, yi); inOpen = true; }
            if (outOpen) pout.lineTo (x, yo); else { pout.startNewSubPath (x, yo); outOpen = true; }
        }
        g.setColour (juce::Colour (244, 236, 236).withAlpha (0.42f));
        dashed (g, pin, 1.4f * u, 3.0f * u, 4.0f * u);
        glowPath (g, pout, cEmber.withAlpha (0.8f), 8.0f * u, false);
        g.setColour (cEmber);
        g.strokePath (pout, juce::PathStrokeType (2.6f * u, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        // la tête de lecture
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.fillRect (x0 + w, s.getY(), 1.0f * u, s.getHeight());
    }

    void paintReadout (juce::Graphics& g, float u)
    {
        const auto r = readout;
        g.setColour (juce::Colours::black.withAlpha (0.42f));
        g.fillRoundedRectangle (r, 16.0f * u);
        g.setColour (juce::Colour (255, 220, 225).withAlpha (0.08f));
        g.drawRoundedRectangle (r.reduced (0.5f), 16.0f * u, 1.0f * u);
        static const char* names[12] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const bool live = lastIn > 0.0f;
        const int note = juce::roundToInt (lastNote);
        const auto top = r.withHeight (r.getHeight() * 0.58f);
        g.setColour (live ? juce::Colours::white : juce::Colour (0xff5a4f52));
        g.setFont (live ? ui::css (ui::Face::display, std::min (64.0f * u, top.getHeight() * 0.5f), 0.1f)
                        : ui::css (ui::Face::mono, std::min (40.0f * u, top.getHeight() * 0.35f), 0.1f));
        g.drawText (live ? juce::String (names[((note % 12) + 12) % 12]) + juce::String (note / 12 - 1) : juce::String ("--"), top, juce::Justification::centred, false);
        // l'écart en cents : la voix entendue (trait clair) et accordée (braise) sur une échelle de -50 à +50
        const auto bar = juce::Rectangle<float> (r.getX() + 26.0f * u, r.getY() + r.getHeight() * 0.68f, r.getWidth() - 52.0f * u, 10.0f * u);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.fillRoundedRectangle (bar, 5.0f * u);
        g.setColour (juce::Colours::white.withAlpha (0.25f));
        g.fillRect (bar.getCentreX() - 0.5f * u, bar.getY() - 6.0f * u, 1.0f * u, bar.getHeight() + 12.0f * u);
        auto X = [&] (float cents) { return bar.getCentreX() + juce::jlimit (-50.0f, 50.0f, cents) / 50.0f * bar.getWidth() * 0.5f; };
        const float cin = live ? (lastIn - lastNote) * 100.0f : 0.0f, cout = live ? (lastOut - lastNote) * 100.0f : 0.0f;
        if (live)
        {
            g.setColour (juce::Colour (244, 236, 236).withAlpha (0.6f));
            g.fillRoundedRectangle (juce::Rectangle<float> (3.0f * u, 22.0f * u).withCentre ({ X (cin), bar.getCentreY() }), 1.5f * u);
            g.setColour (cEmber.withAlpha (0.35f));
            g.fillEllipse (juce::Rectangle<float> (22.0f * u, 22.0f * u).withCentre ({ X (cout), bar.getCentreY() }));
            g.setColour (cEmber);
            g.fillEllipse (juce::Rectangle<float> (12.0f * u, 12.0f * u).withCentre ({ X (cout), bar.getCentreY() }));
        }
        g.setColour (juce::Colour (0xff9a8c8f));
        g.setFont (ui::css (ui::Face::mono, 9.5f * u, 0.12f));
        g.drawText (live ? utf8 ("ENTENDU ") + juce::String (juce::roundToInt (cin)) + utf8 (" ¢ · ACCORDÉ ") + juce::String (juce::roundToInt (cout)) + utf8 (" ¢")
                         : utf8 ("CHANTE : LA NOTE S'AFFICHE ICI"),
                    juce::Rectangle<float> (r.getX(), bar.getBottom() + 12.0f * u, r.getWidth(), 16.0f * u), juce::Justification::centred, false);
    }

    TuneKeyboard keys;
    ScaleSelector keySel, scale;
    GlassTabs* voice = nullptr;
    GlassToggle *hard = nullptr, *formant = nullptr;
    std::vector<StarKnob*> knobs;
    std::vector<Pt> hist;
    int hpos = 0, quiet = 0;
    juce::int64 keyStamp = -1;
    float lastIn = 0.0f, lastOut = 0.0f, lastNote = 60.0f, centre = 60.0f;
    juce::Rectangle<float> screen, readout, head;
};

} // namespace

//==============================================================================
/*  AUTO MIX, après le mix : en haut, « affiner en mots » (une phrase, et rien n'est reconstruit : les macros et deux réglages
    précis bougent, le compte rendu dit ce qui a été compris) ; au milieu les quatre macros, de gros potards qui pilotent toute
    la chaîne (PRÉSENCE et CHALEUR : une courbe douce en fin de chaîne ; TENUE : tous les compresseurs ; ESPACE : la réverbe et
    le delay) ; en bas le rôle de la voix, sa place dans le panoramique, les autres VOCALBOX du projet, et NOUVEAU MIX. */
namespace
{
const juce::Colour cHaze { 0xffa8939a };

class PillButton : public juce::Component
{
public:
    explicit PillButton (juce::String t) : text (std::move (t)) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    std::function<void()> onClick;
    bool lit = true;
    void paint (juce::Graphics& g) override
    {
        const float u = ui::unitOf (*this);
        const auto r = getLocalBounds().toFloat().reduced (1.0f);
        const float rad = r.getHeight() * 0.5f;
        if (lit)
        {
            g.setGradientFill (juce::ColourGradient (cEmber.withAlpha (hover ? 0.75f : 0.6f), 0.0f, r.getY(), onirique::look::emberDeep.withAlpha (0.6f), 0.0f, r.getBottom(), false));
            g.fillRoundedRectangle (r, rad);
            g.setColour (onirique::look::emberLit.withAlpha (0.6f));
        }
        else
        {
            g.setColour (juce::Colours::white.withAlpha (hover ? 0.07f : 0.04f));
            g.fillRoundedRectangle (r, rad);
            g.setColour (juce::Colour (255, 220, 225).withAlpha (0.14f));
        }
        g.drawRoundedRectangle (r, rad, 1.0f * u);
        g.setColour (juce::Colours::white);
        g.setFont (ui::css (ui::Face::mono, 11.0f * u, 0.16f));
        g.drawText (text, r, juce::Justification::centred, false);
    }
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseUp (const juce::MouseEvent& e) override { if (getLocalBounds().contains (e.getPosition()) && onClick) onClick(); }
    float idealWidth() const { return ui::textWidth (ui::css (ui::Face::mono, 11.0f, 0.16f), text) * ui::unitOf (*this) + 44.0f * ui::unitOf (*this); }
private:
    juce::String text;
    bool hover = false;
};
} // namespace

class MixPage final : public ModulePanel
{
public:
    MixPage (OniriqueVocalBoxAudioProcessor& p, std::function<void()> newMix)
        : ModulePanel (p, 0), refineGo (utf8 ("AFFINER")), again (utf8 ("NOUVEAU MIX")),
          role (p.apvts, ids::voiceRole, juce::StringArray { "LEAD", "BACKS", "ADLIBS", "DOUBLES" })
    {
        for (auto [id, label] : { std::pair { ids::macroPresence, utf8 ("PRÉSENCE") }, std::pair { ids::macroWarmth, utf8 ("CHALEUR") },
                                  std::pair { ids::macroHold, juce::String ("TENUE") }, std::pair { ids::macroSpace, juce::String ("ESPACE") } })
        {
            auto& k = own (std::make_unique<StarKnob> (p.apvts, id, label));
            macros.push_back (&k);
        }
        pan = &own (std::make_unique<StarKnob> (p.apvts, ids::outPan, "PAN"));
        role.round = true;
        addAndMakeVisible (role); anim.push_back (&role);
        field.setMultiLine (false);
        field.setReturnKeyStartsNewLine (false);
        field.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
        field.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        field.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
        field.setColour (juce::TextEditor::textColourId, cBone);
        field.setColour (juce::TextEditor::highlightColourId, cEmber.withAlpha (0.35f));
        field.setColour (juce::CaretComponent::caretColourId, cEmber);
        field.setTextToShowWhenEmpty (utf8 ("Affine en mots : un peu moins de réverbe, plus en avant, plus sombre, moins d'autotune…"), cHaze);
        field.onReturnKey = [this] { refine(); };
        addAndMakeVisible (field);
        refineGo.onClick = [this] { refine(); };
        addAndMakeVisible (refineGo);
        again.lit = false;
        again.onClick = std::move (newMix);
        addAndMakeVisible (again);

        // ses habitudes : ce qu'il change d'habitude après AUTO MIX, proposé une fois, juste après le mix
        if (p.habitOffer.exchange (false)) habit = onirique::habits::mixHabit();
        habitYes.onClick = [this] { applyHabit(); };
        habitNo.lit = false;
        habitNo.onClick = [this] { habit = {}; habitYes.setVisible (false); habitNo.setVisible (false); feedbackText.clear(); resized(); repaint(); };
        addChildComponent (habitYes);
        addChildComponent (habitNo);
        habitYes.setVisible (habit.any());
        habitNo.setVisible (habit.any());
    }

    void tick() override
    {
        ModulePanel::tick();
        if (++frame % 30 == 0) { const auto now = projectText(); if (now != shownProject) { shownProject = now; repaint(); } }
    }

    void resized() override
    {
        const float u = ui::unitOf (*this);
        auto r = getLocalBounds().toFloat().reduced (40.0f * u, 26.0f * u);
        bar = r.removeFromTop (60.0f * u);
        const float gw = refineGo.idealWidth();
        refineGo.setBounds (juce::Rectangle<float> (bar.getRight() - 8.0f * u - gw, bar.getY() + 9.0f * u, gw, bar.getHeight() - 18.0f * u).toNearestInt());
        field.setBounds (bar.withTrimmedLeft (54.0f * u).withRight ((float) refineGo.getX() - 12.0f * u).reduced (0.0f, 14.0f * u).toNearestInt());
        field.setFont (ui::css (ui::Face::body, 16.0f * u));
        field.applyFontToAllText (ui::css (ui::Face::body, 16.0f * u));
        feedback = r.removeFromTop ((habit.any() ? 48.0f : 30.0f) * u);
        if (habit.any())
        {
            auto fb = feedback.reduced (0.0f, 6.0f * u);
            const float nw = habitNo.idealWidth(), yw = habitYes.idealWidth();
            habitNo.setBounds (fb.removeFromRight (nw).toNearestInt());
            fb.removeFromRight (10.0f * u);
            habitYes.setBounds (fb.removeFromRight (yw).toNearestInt());
        }
        auto bottom = r.removeFromBottom (116.0f * u);
        r.removeFromBottom (12.0f * u);
        // les quatre macros, grandes, côte à côte
        const float gap = 24.0f * u, cw = (r.getWidth() - 3.0f * gap) / 4.0f;
        for (int i = 0; i < 4; ++i)
        {
            const auto cell = juce::Rectangle<float> (r.getX() + (float) i * (cw + gap), r.getY(), cw, r.getHeight());
            const float w = std::min (cw, 260.0f * u), h = std::min (cell.getHeight(), w + 92.0f * u);
            macros[(size_t) i]->setBounds (juce::Rectangle<float> (w, h).withCentre (cell.getCentre()).toNearestInt());
        }
        const float rw = role.idealWidth (40.0f * u);
        role.setBounds (juce::Rectangle<float> (bottom.getX() + 96.0f * u, bottom.getCentreY() - 20.0f * u, rw, 40.0f * u).toNearestInt());
        roleLabel = juce::Rectangle<float> (bottom.getX(), bottom.getY(), 90.0f * u, bottom.getHeight());
        const float aw = again.idealWidth();
        again.setBounds (juce::Rectangle<float> (bottom.getRight() - aw, bottom.getCentreY() - 20.0f * u, aw, 40.0f * u).toNearestInt());
        pan->setBounds (juce::Rectangle<float> (role.getRight() + 30.0f * u, bottom.getY(), 110.0f * u, bottom.getHeight()).toNearestInt());   // dans sa rangée : ne touche plus les macros
        pan->setShowText (true, true);
        projectArea = juce::Rectangle<float> ((float) pan->getRight() + 20.0f * u, bottom.getY(), (float) again.getX() - pan->getRight() - 40.0f * u, bottom.getHeight());
    }

    void paint (juce::Graphics& g) override
    {
        const float u = ui::unitOf (*this);
        // la barre d'affinage : la gélule de verre de la barre de recherche
        const float rad = bar.getHeight() * 0.5f;
        g.setColour (juce::Colour (0xff120d0f).withAlpha (0.9f));
        g.fillRoundedRectangle (bar, rad);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.06f), 0.0f, bar.getY(), juce::Colours::white.withAlpha (0.0f), 0.0f, bar.getBottom(), false));
        g.fillRoundedRectangle (bar, rad);
        g.setColour (juce::Colour (255, 220, 225).withAlpha (0.14f));
        g.drawRoundedRectangle (bar.reduced (0.5f), rad, 1.0f * u);
        {
            // l'étoile d'AUTO MIX à gauche du champ
            juce::Path star;
            const auto c = juce::Point<float> (bar.getX() + 30.0f * u, bar.getCentreY());
            const float s = 9.0f * u;
            star.startNewSubPath (c.x, c.y - s);
            star.quadraticTo (c.x, c.y, c.x + s, c.y); star.quadraticTo (c.x, c.y, c.x, c.y + s);
            star.quadraticTo (c.x, c.y, c.x - s, c.y); star.quadraticTo (c.x, c.y, c.x, c.y - s);
            g.setColour (cEmber);
            g.fillPath (star);
        }
        g.setFont (ui::css (ui::Face::mono, 10.0f * u, 0.12f));
        if (habit.any() && feedbackText.isEmpty())
        {
            // la suggestion : une bande de verre braise, le texte, APPLIQUER et NON MERCI à droite
            const auto band = feedback.reduced (0.0f, 2.0f * u);
            g.setColour (cEmber.withAlpha (0.10f));
            g.fillRoundedRectangle (band, band.getHeight() * 0.5f);
            g.setColour (cEmber.withAlpha (0.35f));
            g.drawRoundedRectangle (band, band.getHeight() * 0.5f, 1.0f * u);
            g.setColour (cBone);
            g.setFont (ui::css (ui::Face::body, 14.0f * u));
            g.drawText (habit.describe() + utf8 (" Appliquer les réglages qui s'en rapprochent le plus ?"),
                        band.withTrimmedLeft (22.0f * u).withRight ((float) habitYes.getX() - 12.0f * u), juce::Justification::centredLeft, true);
        }
        else
        {
            g.setColour (understood ? juce::Colour (0xffd9b8bd) : cHaze);
            g.drawText (feedbackText.isNotEmpty() ? feedbackText : utf8 ("RIEN N'EST RECONSTRUIT : LES MACROS BOUGENT, TU PEUX LES REPRENDRE À LA MAIN"),
                        feedback.withTrimmedLeft (24.0f * u), juce::Justification::centredLeft, true);
        }
        g.setColour (cHaze);
        g.setFont (ui::css (ui::Face::mono, 10.0f * u, 0.12f));
        g.drawText (utf8 ("CETTE VOIX"), roleLabel, juce::Justification::centredLeft, false);
        g.drawText (shownProject, projectArea, juce::Justification::centredLeft, true);
    }

private:
    juce::String projectText() const
    {
        const auto o = onirique::registry::others (&processor);
        if (o.empty()) return utf8 ("SEULE VOCALBOX DU PROJET");
        static const char* rn[4] { "LEAD", "BACKS", "ADLIBS", "DOUBLES" };
        static const char* kn[12] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        juce::StringArray s;
        for (const auto& e : o)
            s.add (juce::String (rn[juce::jlimit (0, 3, e.role)]) + (e.key >= 0 ? juce::String (" (") + kn[e.key % 12] + (e.key >= 12 ? "m)" : ")") : juce::String()));
        return utf8 ("AUSSI DANS LE PROJET : ") + s.joinIntoString (utf8 (" · "));
    }

    void refine()
    {
        const auto text = field.getText().trim();
        if (text.isEmpty()) return;
        const auto r = onirique::mix::parseRefine (text);
        understood = r.any();
        if (! understood)
        {
            feedbackText = utf8 ("PAS COMPRIS · ESSAIE « MOINS DE RÉVERBE », « PLUS EN AVANT », « PLUS SOMBRE », « MOINS D'AUTOTUNE »");
            repaint();
            return;
        }
        auto& apvts = processor.apvts;
        auto bump = [&apvts] (const juce::String& id, float delta, float lo, float hi, bool mul)
        {
            if (auto* prm = apvts.getParameter (id))
            {
                const float now = apvts.getRawParameterValue (id)->load();
                const float v = juce::jlimit (lo, hi, mul ? now * delta : now + delta);
                prm->beginChangeGesture();
                prm->setValueNotifyingHost (prm->convertTo0to1 (v));
                prm->endChangeGesture();
            }
        };
        bump (ids::macroPresence, r.presence, -100.0f, 100.0f, false);
        bump (ids::macroWarmth, r.warmth, -100.0f, 100.0f, false);
        bump (ids::macroHold, r.hold, -100.0f, 100.0f, false);
        bump (ids::macroSpace, r.space, -100.0f, 100.0f, false);
        // les réglages précis : les modules de la chaîne qui en ont
        for (int c : processor.getChain())
        {
            if (! modules::valid (c)) continue;
            const int t = modules::typeOf (c), k = modules::instanceOf (c);
            if (t == modules::delay && r.delayMul != 1.0f) bump (ids::inst (ids::delayMix, k), r.delayMul, 0.0f, 100.0f, true);
            if (t == modules::tuner && r.retuneMul != 1.0f) bump (ids::inst (ids::tuneRetune, k), r.retuneMul, 0.0f, 400.0f, true);
            if (t == modules::debreath && r.breathDb != 0.0f) bump (ids::inst (ids::dbrReduction, k), -r.breathDb, 0.0f, 30.0f, false);
        }
        processor.learnRefine (text);   // ONR Neural Learning (s'il est allumé) : la retouche en mots part avec la note du mix
        feedbackText = utf8 ("COMPRIS : ") + r.understood.joinIntoString (", ").toUpperCase();
        field.clear();
        repaint();
    }

    juce::TextEditor field;
    void applyHabit()
    {
        juce::StringArray done;
        for (auto [id, v, name] : { std::tuple { ids::macroPresence, habit.presence, utf8 ("PRÉSENCE") }, std::tuple { ids::macroWarmth, habit.warmth, utf8 ("CHALEUR") },
                                    std::tuple { ids::macroHold, habit.hold, juce::String ("TENUE") }, std::tuple { ids::macroSpace, habit.space, juce::String ("ESPACE") } })
        {
            if (v == 0.0f) continue;
            if (auto* prm = processor.apvts.getParameter (id))
            {
                prm->beginChangeGesture();
                prm->setValueNotifyingHost (prm->convertTo0to1 (v));
                prm->endChangeGesture();
            }
            done.add (name + (v > 0 ? " +" : utf8 (" −")) + juce::String (juce::roundToInt (std::abs (v))));
        }
        habit = {};
        habitYes.setVisible (false); habitNo.setVisible (false);
        understood = true;
        feedbackText = utf8 ("TES RÉGLAGES HABITUELS SONT APPLIQUÉS : ") + done.joinIntoString (utf8 (" · "));
        resized();
        repaint();
    }

    onirique::habits::MixHabit habit;
    PillButton habitYes { utf8 ("APPLIQUER") }, habitNo { utf8 ("NON MERCI") };
    PillButton refineGo, again;
    GlassTabs role;
    std::vector<StarKnob*> macros;
    StarKnob* pan = nullptr;
    juce::Rectangle<float> bar, feedback, roleLabel, projectArea;
    juce::String feedbackText, shownProject;
    bool understood = false;
    int frame = 0;
};

std::unique_ptr<ModulePanel> createMixPage (OniriqueVocalBoxAudioProcessor& p, std::function<void()> onNewMix)
{
    return std::make_unique<MixPage> (p, std::move (onNewMix));
}

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
            s->knobs = { &s->knob (ids::deessFreq, "FREQ"), &s->knob (ids::deessAmt, "AMOUNT"), &s->knob (ids::deessClarity, "CLARITY") };
            s->topRow = { &s->toggle (ids::deessS, "S"), &s->toggle (ids::deessT, "T"), &s->toggle (ids::deessCH, "CH"), &s->toggle (ids::deessF, "F") };
            s->note = juce::String::fromUTF8 ("COCHE LES SONS À TRAITER · CLARITY REDONNE DE L'ÉCLAT SANS LES RAMENER");
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
            s->topRow = { &s->toggle (ids::plosListen, "LISTEN") };
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
            auto s = stdPanel (p, code, std::make_unique<FxViz> (p, code, FxVisual::Kind::chorus, "CHORUS \xb7 LA VOIX QUI ONDULE"));
            s->knobs = { &s->knob (ids::chorusRate, "RATE"), &s->knob (ids::chorusDepth, "DEPTH"), &s->knob (ids::chorusMix, "MIX") };
            return s;
        }
        case modules::flanger:
        {
            auto s = stdPanel (p, code, std::make_unique<FxViz> (p, code, FxVisual::Kind::flanger, "FLANGER \xb7 LE PEIGNE QUI GLISSE"));
            s->knobs = { &s->knob (ids::flangerRate, "RATE"), &s->knob (ids::flangerDepth, "DEPTH"), &s->knob (ids::flangerFeedback, "FEEDBACK"), &s->knob (ids::flangerMix, "MIX") };
            return s;
        }
        case modules::phone:
        {
            auto s = stdPanel (p, code, std::make_unique<FxViz> (p, code, FxVisual::Kind::phone, utf8 ("PHONE · LA VOIX DANS UN TÉLÉPHONE, UNE RADIO")));
            s->knobs = { &s->knob (ids::phoneAmount, "AMOUNT") };
            return s;
        }
        case modules::creative: return std::make_unique<CreativePanel> (p, code);
        case modules::preamp:
        {
            auto s = stdPanel (p, code, std::make_unique<PreampViz> (p, code));
            s->topRow = { &s->tabs (ids::preampMic, T { "LINE", "MIC" }) };
            s->note = utf8 ("EN TÊTE DE CHAÎNE · LE GAIN POUSSE LE GRAIN, PAS LE VOLUME");
            s->knobs = { &s->knob (ids::preampGain, "GAIN"), &s->knob (ids::preampOutput, "OUTPUT") };
            return s;
        }
        case modules::cl1b: return std::make_unique<Cl1bPanel> (p, code);
        case modules::tuner: return std::make_unique<TunerPanel> (p, code);
        case modules::declick:
        {
            auto s = stdPanel (p, code, std::make_unique<WaveViz> (p, code));
            s->topRow = { &s->toggle (ids::dclkListen, "LISTEN") };
            s->note = utf8 ("CLICS DE BOUCHE, SALIVE, LÈVRES · LES CONSONNES ET LA VOIX NE SONT PAS TOUCHÉES");
            s->knobs = { &s->knob (ids::dclkSens, "SENSITIVITY"), &s->knob (ids::dclkAmount, "AMOUNT") };
            return s;
        }
        case modules::debreath:
        {
            auto s = stdPanel (p, code, std::make_unique<WaveViz> (p, code));
            s->topRow = { &s->toggle (ids::dbrListen, "LISTEN") };
            s->note = utf8 ("LES RESPIRATIONS BAISSÉES, PAS COUPÉES · LES S ET LES FINS DE MOTS GARDÉS");
            s->knobs = { &s->knob (ids::dbrReduction, "REDUCTION"), &s->knob (ids::dbrSens, "SENSITIVITY") };
            return s;
        }
        case modules::dereverb:
        {
            auto s = stdPanel (p, code, std::make_unique<WaveViz> (p, code));
            s->topRow = { &s->toggle (ids::drvAuto, "AUTO"), &s->toggle (ids::drvListen, "LISTEN") };
            s->note = utf8 ("AUTO MESURE LA PIÈCE SUR LES FINS DE PHRASES");
            s->knobs = { &s->knob (ids::drvAmount, "AMOUNT"), &s->knob (ids::drvTail, "TAIL") };
            return s;
        }
        default: break;
    }
    return createGenericPanel (p, code);
}
