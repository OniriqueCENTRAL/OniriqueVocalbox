#include "ModulePanels.h"

using namespace onirique;

/*  Les écrans des modules, comme dans la démo validée : chaque module occupe toute la place du panneau.
    - 1176, LA-2A, PARALLEL : façade noire à cadre argent, grand VU crème gradué, potards étoile, la compression en direct.
    - GATE, DE-ESSER, NOISE, PLOSIVES : la voix qui défile, ce que le module retire en braise.
    - RESONANCE, MULTIBAND, PARAMETRIC EQ : leurs écrans interactifs (ceux d'ONIRIQUE EQ pour l'EQ).
    - SATURATION : courbe de transfert et sinus avant / après ; EQ ANALOG : sa courbe.
    - Effets : leurs visuels animés (échos vers l'horizon, voix et queue de reverb...). */

namespace
{
constexpr float kPi = juce::MathConstants<float>::pi;

//==============================================================================
// VU crème gradué (réduction de gain) : la zone rouge 0 .. +3 sur l'arc, l'aiguille en balistique VU
class VintageVu : public juce::Component, public Animated
{
public:
    explicit VintageVu (std::function<float()> grDb) : read (std::move (grDb)) { setInterceptsMouseClicks (false, false); }

    void tick() override
    {
        const float gr = juce::jlimit (0.0f, 30.0f, read ? read() : 0.0f);
        if (ui::approach (angle, angleFor (gr), 0.16f)) repaint();
    }

    void paint (juce::Graphics& g) override
    {
        // dessin dans un repère 300 x 175, mis à l'échelle de la place
        const auto b = getLocalBounds().toFloat();
        const float s = std::min (b.getWidth() / 320.0f, b.getHeight() / 195.0f);
        const auto frame = juce::Rectangle<float> (320.0f * s, 195.0f * s).withCentre (b.getCentre());
        g.setColour (juce::Colour (0xff050405));
        g.fillRoundedRectangle (frame, 16.0f * s);
        g.setColour (ui::ember.withAlpha (0.18f));
        g.drawRoundedRectangle (frame.expanded (2.0f * s), 18.0f * s, 6.0f * s);
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.drawRoundedRectangle (frame.reduced (0.5f), 16.0f * s, 1.0f);

        const auto face = frame.reduced (10.0f * s);
        const float k = face.getWidth() / 300.0f;
        auto P = [&] (float x, float y) { return juce::Point<float> (face.getX() + x * k, face.getY() + y * k); };
        auto polar = [&] (float r, float deg) { const float a = deg * kPi / 180.0f; return P (150.0f + r * std::sin (a), 165.0f - r * std::cos (a)); };
        juce::ColourGradient fg (juce::Colour (0xfffff1c8), P (150, 175).x, P (150, 175).y, juce::Colour (0xffc79a4c), P (150, 175).x + 300.0f * k, P (150, 175).y, true);
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
        g.setFont (ui::font (ui::Face::body, 11.0f * k, true));
        for (const auto& [l, a] : major)
        {
            const auto c = a >= 20.0f ? red : ink;
            tickAt (118.0f, 128.0f, a, c, 1.8f);
            g.setColour (c);
            g.drawText (l, juce::Rectangle<float> (30.0f * k, 14.0f * k).withCentre (polar (138.0f, a)), juce::Justification::centred, false);
        }
        g.setFont (ui::font (ui::Face::mono, 7.0f * k));
        for (const auto& [l, a] : std::initializer_list<std::pair<const char*, float>> { { "0", -48.0f }, { "20", -30.0f }, { "40", -14.0f }, { "60", 0.0f }, { "80", 10.0f }, { "100", 20.0f } })
        {
            tickAt (110.0f, 115.0f, a, ink, 0.8f);
            g.setColour (juce::Colour (0xff5a4630));
            g.drawText (l, juce::Rectangle<float> (24.0f * k, 10.0f * k).withCentre (polar (102.0f, a)), juce::Justification::centred, false);
        }
        g.setColour (ink);
        g.setFont (ui::font (ui::Face::display, 9.0f * k));
        g.drawText ("GAIN REDUCTION", juce::Rectangle<float> (200.0f * k, 14.0f * k).withCentre (P (150, 143)), juce::Justification::centred, false);

        // l'aiguille
        const auto pivot = P (150, 165), tip = polar (113.0f, angle);
        g.setColour (juce::Colour (0xff111111));
        g.drawLine ({ pivot, tip }, 2.2f * k);
        g.setColour (juce::Colour (0xff1d1d1f));
        g.fillEllipse (juce::Rectangle<float> (22.0f * k, 22.0f * k).withCentre (pivot));
        // le verre
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.35f), 0.0f, face.getY(), juce::Colours::black.withAlpha (0.2f), 0.0f, face.getBottom(), false));
        juce::Path glass; glass.addRoundedRectangle (face, 10.0f * k);
        g.fillPath (glass);
    }

private:
    // réduction de gain (dB) -> angle de l'aiguille sur l'échelle du VU (0 dB à +20°, 20 dB à -48°)
    static float angleFor (float gr)
    {
        static const float db[] { 0.0f, 1.0f, 2.0f, 3.0f, 5.0f, 7.0f, 10.0f, 20.0f, 30.0f };
        static const float an[] { 20.0f, 12.0f, 6.0f, 0.0f, -11.0f, -20.0f, -30.0f, -48.0f, -52.0f };
        for (int i = 0; i < 8; ++i)
            if (gr <= db[i + 1]) return an[i] + (an[i + 1] - an[i]) * (gr - db[i]) / (db[i + 1] - db[i]);
        return -52.0f;
    }
    std::function<float()> read;
    float angle = 20.0f;
};

//==============================================================================
// La forme d'onde du module qui défile (les 6 dernières secondes) : ce qui entre, ce qui sort ; l'écart (ce que le module
// retire) en braise. dbScale : échelle en dB (GATE), avec le seuil à glisser.
class WaveScope : public juce::Component, public Animated, public juce::SettableTooltipClient
{
public:
    WaveScope (ChainModule* m, bool db) : module (m), dbScale (db) {}
    std::function<float()> threshold;               // GATE : le seuil (dB) et sa ligne
    std::function<void (float)> setThreshold;
    juce::String removedLabel = juce::String::fromUTF8 ("RETIRÉ");

    void tick() override
    {
        if (module == nullptr) return;
        const int w = module->scope.write.load (std::memory_order_acquire);
        if (w != lastWrite || (threshold && threshold() != lastThr)) { lastWrite = w; lastThr = threshold ? threshold() : 0.0f; repaint(); }
    }

    void paint (juce::Graphics& g) override
    {
        const float u = ui::unitOf (*this);
        const auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff0b0709));
        g.fillRoundedRectangle (r, 20.0f * u);
        juce::ColourGradient bg (juce::Colour (0xff1d1013).withAlpha (0.9f), r.getX(), r.getY(), juce::Colour (0xff070506).withAlpha (0.0f), r.getX() + r.getWidth() * 0.6f, r.getBottom(), true);
        g.setGradientFill (bg);
        g.fillRoundedRectangle (r, 20.0f * u);
        g.setColour (juce::Colour (0xffffdce1).withAlpha (0.1f));
        g.drawRoundedRectangle (r.reduced (0.5f), 20.0f * u, 1.0f);
        if (module == nullptr) return;

        const auto plot = r.reduced (18.0f * u, 30.0f * u);
        const float cy = plot.getCentreY(), h = plot.getHeight() * 0.5f;
        for (int k = 1; k < 4; ++k)
        {
            g.setColour (juce::Colours::white.withAlpha (0.035f));
            g.drawHorizontalLine (juce::roundToInt (plot.getY() + plot.getHeight() * (float) k / 4.0f), plot.getX(), plot.getRight());
        }
        auto norm = [&] (float a) { return dbScale ? juce::jlimit (0.0f, 1.0f, ((a > 1.0e-6f ? 20.0f * std::log10 (a) : -120.0f) + 70.0f) / 70.0f)
                                                   : juce::jlimit (0.0f, 1.0f, a * 1.1f); };
        const int n = ModuleScope::kCols, w = module->scope.write.load (std::memory_order_acquire);
        juce::Path pin, pout;
        std::vector<juce::Point<float>> topIn, topOut;
        for (int i = 0; i < n; ++i)
        {
            const int idx = (w + i) % n;
            const float x = plot.getX() + plot.getWidth() * (float) i / (float) (n - 1);
            topIn.push_back ({ x, cy - h * norm (module->scope.in[idx].load (std::memory_order_relaxed)) });
            topOut.push_back ({ x, cy - h * norm (module->scope.out[idx].load (std::memory_order_relaxed)) });
        }
        auto mirror = [&] (const std::vector<juce::Point<float>>& top)
        {
            juce::Path p;
            p.startNewSubPath (top.front());
            for (size_t i = 1; i < top.size(); ++i) p.lineTo (top[i]);
            for (size_t i = top.size(); i-- > 0;) p.lineTo (top[i].x, 2.0f * cy - top[i].y);
            p.closeSubPath();
            return p;
        };
        pin = mirror (topIn);
        pout = mirror (topOut);
        juce::ColourGradient emb (juce::Colour (0xffff6b7a), 0.0f, plot.getY(), juce::Colour (0xff7a0d1c), 0.0f, plot.getBottom(), false);
        {
            juce::Graphics::ScopedSaveState s (g);
            g.setGradientFill (emb);
            g.setOpacity (0.9f);
            g.fillPath (pin);
        }
        ui::strokeGlow (g, pin, ui::ember, 1.0f * u, 0.5f);
        g.setColour (ui::mist.withAlpha (0.93f));
        g.fillPath (pout);

        if (threshold)
        {
            const float t = norm (std::pow (10.0f, threshold() / 20.0f));
            for (float y : { cy - h * t, cy + h * t })
            {
                juce::Path line;
                line.startNewSubPath (plot.getX(), y); line.lineTo (plot.getRight(), y);
                juce::Path dashed;
                const float dash[] { 6.0f * u, 6.0f * u };
                juce::PathStrokeType (1.5f * u).createDashedStroke (dashed, line, dash, 2);
                g.setColour (juce::Colour (0xffff8e9a));
                g.fillPath (dashed);
            }
            g.setColour (juce::Colour (0xffff8e9a));
            g.setFont (ui::font (ui::Face::mono, 10.0f * u));
            g.drawText ("SEUIL " + juce::String (threshold(), 1) + " dB", juce::Rectangle<float> (plot.getRight() - 160.0f * u, cy - h * t - 18.0f * u, 160.0f * u, 14.0f * u),
                        juce::Justification::centredRight, false);
        }

        // légende
        g.setFont (ui::font (ui::Face::mono, 9.0f * u));
        auto chip = [&] (float x, juce::Colour c, const juce::String& t)
        {
            g.setColour (c);
            g.fillRoundedRectangle (juce::Rectangle<float> (x, r.getY() + 16.0f * u, 14.0f * u, 4.0f * u), 2.0f * u);
            g.setColour (ui::haze);
            g.drawText (t, juce::Rectangle<float> (x + 20.0f * u, r.getY() + 10.0f * u, 90.0f * u, 16.0f * u), juce::Justification::centredLeft, false);
        };
        chip (r.getRight() - 230.0f * u, ui::mist, juce::String::fromUTF8 ("GARDÉ"));
        chip (r.getRight() - 120.0f * u, ui::ember, removedLabel);
    }

    void mouseDown (const juce::MouseEvent& e) override { if (setThreshold) mouseDrag (e); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! setThreshold) return;
        const float u = ui::unitOf (*this);
        const auto plot = getLocalBounds().toFloat().reduced (18.0f * u, 30.0f * u);
        const float t = std::abs (e.position.y - plot.getCentreY()) / (plot.getHeight() * 0.5f);
        setThreshold (juce::jlimit (-80.0f, 0.0f, t * 70.0f - 70.0f));
    }

private:
    ChainModule* module = nullptr;
    bool dbScale = false;
    int lastWrite = -1;
    float lastThr = 0.0f;
};

//==============================================================================
// La compression en direct (bas des façades) : la voix qui entre (pâle), qui sort (blanche), la réduction en braise
class CompStrip : public juce::Component, public Animated
{
public:
    explicit CompStrip (ChainModule* m) : module (m) { setInterceptsMouseClicks (false, false); gr.fill (0.0f); }
    void tick() override
    {
        if (module == nullptr) return;
        gr[(size_t) pos] = module->getReductionDb();
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
        const auto plot = r.reduced (8.0f * u, 6.0f * u);
        const float cy = plot.getCentreY() + 6.0f * u, h = plot.getHeight() * 0.42f;
        auto norm = [] (float a) { return juce::jlimit (0.0f, 1.0f, ((a > 1.0e-6f ? 20.0f * std::log10 (a) : -120.0f) + 50.0f) / 50.0f); };
        const int n = ModuleScope::kCols, w = module->scope.write.load (std::memory_order_acquire);
        for (int pass = 0; pass < 2; ++pass)
        {
            juce::Path p;
            std::vector<juce::Point<float>> top;
            for (int i = 0; i < n; ++i)
            {
                const int idx = (w + i) % n;
                const float a = (pass == 0 ? module->scope.in : module->scope.out)[idx].load (std::memory_order_relaxed);
                top.push_back ({ plot.getX() + plot.getWidth() * (float) i / (float) (n - 1), cy - h * norm (a) });
            }
            p.startNewSubPath (top.front());
            for (auto& q : top) p.lineTo (q);
            for (size_t i = top.size(); i-- > 0;) p.lineTo (top[i].x, 2.0f * cy - top[i].y);
            p.closeSubPath();
            g.setColour (pass == 0 ? ui::mist.withAlpha (0.13f) : ui::mist.withAlpha (0.85f));
            g.fillPath (p);
        }
        juce::Path curtain;
        curtain.startNewSubPath (plot.getX(), plot.getY());
        const int m = (int) gr.size();
        float mx = 0.0f;
        for (int i = 0; i < m; ++i)
        {
            const float v = gr[(size_t) ((pos + i) % m)];
            mx = std::max (mx, v);
            curtain.lineTo (plot.getX() + plot.getWidth() * (float) i / (float) (m - 1), plot.getY() + std::min (plot.getHeight() * 0.6f, v * 4.0f * u));
        }
        curtain.lineTo (plot.getRight(), plot.getY());
        curtain.closeSubPath();
        g.setGradientFill (juce::ColourGradient (ui::ember.withAlpha (0.85f), 0.0f, plot.getY(), ui::ember.withAlpha (0.05f), 0.0f, plot.getY() + plot.getHeight() * 0.6f, false));
        g.fillPath (curtain);
        g.setFont (ui::font (ui::Face::mono, 9.0f * u));
        g.setColour (juce::Colour (0xffd9b8bd));
        g.drawText (juce::String::fromUTF8 ("RÉDUCTION DE GAIN"), r.reduced (14.0f * u, 8.0f * u), juce::Justification::topLeft, false);
        g.setColour (ui::bone);
        g.drawText (juce::String::charToString (0x2212) + juce::String (mx, 1) + " dB MAX", r.reduced (14.0f * u, 8.0f * u), juce::Justification::topRight, false);
    }

private:
    ChainModule* module = nullptr;
    std::array<float, 260> gr {};
    int pos = 0;
};

//==============================================================================
// Façade des compresseurs (1176, LA-2A, PARALLEL) : noire, cadre argent, VU à gauche, potards à droite, compression en bas
class FacePanel final : public ModulePanel
{
public:
    FacePanel (OniriqueVocalBoxAudioProcessor& p, int c, juce::String t, juce::String sub) : ModulePanel (p, c), title (std::move (t)), subtitle (std::move (sub)),
        vu ([this] { return engine != nullptr ? engine->getReductionDb() : 0.0f; }), strip (engine)
    {
        addAndMakeVisible (vu);  anim.push_back (&vu);
        addAndMakeVisible (strip); anim.push_back (&strip);
    }
    juce::String title, subtitle;
    std::vector<StarKnob*> knobs;
    std::vector<std::pair<juce::String, juce::Component*>> rows;   // RATIO, MODE... sous le VU
    int cols = 4;

    void resized() override
    {
        const float u = ui::unitOf (*this);
        auto r = getLocalBounds().toFloat().reduced (30.0f * u, 22.0f * u);
        strip.setBounds (r.removeFromBottom (96.0f * u).toNearestInt());
        r.removeFromBottom (14.0f * u);
        auto left = r.removeFromLeft (400.0f * u);
        r.removeFromLeft (36.0f * u);
        left.removeFromTop (30.0f * u);
        for (auto it = rows.rbegin(); it != rows.rend(); ++it)
        {
            auto row = left.removeFromBottom (40.0f * u);
            it->second->setBounds (row.withTrimmedLeft (70.0f * u).toNearestInt());
            left.removeFromBottom (10.0f * u);
        }
        vu.setBounds (left.reduced (0.0f, 4.0f * u).toNearestInt());
        layoutKnobs (knobs, r, cols);
    }

    void paint (juce::Graphics& g) override
    {
        const float u = ui::unitOf (*this);
        const auto r = getLocalBounds().toFloat();
        juce::ColourGradient bg (juce::Colour (0xff24181b), r.getX() + r.getWidth() * 0.2f, r.getY(), juce::Colour (0xff0a090a), r.getRight(), r.getBottom(), true);
        bg.addColour (0.45, juce::Colour (0xff121012));
        g.setGradientFill (bg);
        g.fillRoundedRectangle (r, 22.0f * u);
        // lueur braise en bas à gauche, reflet en biais
        juce::ColourGradient glow (ui::ember.withAlpha (0.16f), r.getX() + 140.0f * u, r.getBottom() + 60.0f * u, ui::ember.withAlpha (0.0f), r.getX() + 520.0f * u, r.getBottom(), true);
        g.setGradientFill (glow);
        g.fillRoundedRectangle (r, 22.0f * u);
        // cadre argent
        g.setColour (juce::Colour (0xff8d8f96));
        g.drawRoundedRectangle (r.reduced (1.0f * u), 22.0f * u, 2.0f * u);
        g.setColour (juce::Colour (0xff1d1d21));
        g.drawRoundedRectangle (r.reduced (3.0f * u), 21.0f * u, 2.0f * u);

        const auto head = juce::Rectangle<float> (r.getX() + 30.0f * u, r.getY() + 20.0f * u, 400.0f * u, 26.0f * u);
        g.setColour (juce::Colour (0xffe9e9ec));
        g.setFont (ui::font (ui::Face::display, 15.0f * u, true, 0.3f));
        g.drawText (title, head, juce::Justification::centredLeft, false);
        g.setColour (juce::Colour (0xff8c8d93));
        g.setFont (ui::font (ui::Face::mono, 10.0f * u));
        g.drawText (subtitle, head, juce::Justification::centredRight, false);
        for (auto& [label, comp] : rows)
        {
            g.setColour (juce::Colour (0xff9a9ca3));
            g.drawText (label, comp->getBounds().toFloat().withX (head.getX()).withWidth (66.0f * u), juce::Justification::centredLeft, false);
        }
    }

private:
    VintageVu vu;
    CompStrip strip;
};

//==============================================================================
// Nettoyages (GATE, DE-ESSER, NOISE, PLOSIVES) : la forme d'onde, ce qui est retiré, la réduction, les potards
class WavePanel final : public ModulePanel
{
public:
    WavePanel (OniriqueVocalBoxAudioProcessor& p, int c, bool db) : ModulePanel (p, c), scope (engine, db),
        grBar ([this] { return engine != nullptr ? std::abs (engine->getReductionDb()) : 0.0f; }, "Gain reduction of the module (dB).")
    {
        addAndMakeVisible (scope); anim.push_back (&scope);
        addAndMakeVisible (grBar); anim.push_back (&grBar);
    }
    WaveScope scope;
    GainReductionBar grBar;
    std::vector<StarKnob*> knobs;

    void resized() override
    {
        const float u = ui::unitOf (*this);
        auto r = getLocalBounds().toFloat();
        auto bottom = r.removeFromBottom (std::min (170.0f * u, r.getHeight() * 0.42f));
        r.removeFromBottom (12.0f * u);
        grBar.setBounds (r.removeFromRight (54.0f * u).toNearestInt());
        r.removeFromRight (12.0f * u);
        scope.setBounds (r.toNearestInt());
        layoutKnobs (knobs, bottom, 8);
    }
};

//==============================================================================
// Un visuel en grand (effets, saturation, EQ analog...), des onglets au-dessus, les potards dessous
class VisualPanel final : public ModulePanel
{
public:
    using ModulePanel::ModulePanel;
    juce::Component* visual = nullptr;
    std::vector<juce::Component*> topRow;   // onglets et pastilles, de gauche à droite
    std::vector<StarKnob*> knobs;
    int cols = 8;
    float knobH = 160.0f;

    void resized() override
    {
        const float u = ui::unitOf (*this);
        auto r = getLocalBounds().toFloat();
        if (! topRow.empty())
        {
            auto row = r.removeFromTop (38.0f * u);
            float x = row.getX();
            for (auto* c : topRow)
            {
                float w = 150.0f * u;
                if (auto* t = dynamic_cast<GlassTabs*> (c)) w = t->idealWidth (38.0f * u);
                c->setBounds (juce::Rectangle<float> (x, row.getY(), w, row.getHeight()).toNearestInt());
                x += w + 10.0f * u;
            }
            r.removeFromTop (12.0f * u);
        }
        const int rows = ((int) knobs.size() + cols - 1) / std::max (1, cols);
        auto bottom = r.removeFromBottom (std::min (knobH * u * (float) rows, r.getHeight() * 0.55f));
        r.removeFromBottom (10.0f * u);
        if (visual != nullptr) visual->setBounds (r.toNearestInt());
        layoutKnobs (knobs, bottom, cols);
    }
};

// cadre arrondi d'un visuel (les visuels d'effets dessinent sur fond transparent)
class Frame : public juce::Component
{
public:
    explicit Frame (juce::Component& inner) : child (inner) { addAndMakeVisible (child); }
    void resized() override { child.setBounds (getLocalBounds().reduced (juce::roundToInt (10.0f * ui::unitOf (*this)))); }
    void paint (juce::Graphics& g) override
    {
        const float u = ui::unitOf (*this);
        const auto r = getLocalBounds().toFloat();
        juce::ColourGradient bg (juce::Colour (0xff1d1013), r.getX() + r.getWidth() * 0.15f, r.getY(), juce::Colour (0xff070506), r.getRight(), r.getBottom(), true);
        bg.addColour (0.55, juce::Colour (0xff0e0a0c));
        g.setGradientFill (bg);
        g.fillRoundedRectangle (r, 22.0f * u);
        g.setColour (juce::Colour (0xffffdce1).withAlpha (0.10f));
        g.drawRoundedRectangle (r.reduced (0.5f), 22.0f * u, 1.0f);
    }
    juce::Component& child;
};

//==============================================================================
// SATURATION : courbe de transfert (à gauche) et un sinus avant / après (à droite), selon le mode et le DRIVE
class SatVisual : public juce::Component, public Animated
{
public:
    SatVisual (OniriqueVocalBoxAudioProcessor& p, int inst) : apvts (p.apvts), k (inst) { setInterceptsMouseClicks (false, false); }
    void tick() override
    {
        const float d = get (ids::drive), m = get (ids::satType), w = get (ids::satWarmth), mix = get (ids::satMix);
        phase += 0.012f;
        if (d != last[0] || m != last[1] || w != last[2] || mix != last[3]) { last[0] = d; last[1] = m; last[2] = w; last[3] = mix; }
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        const float u = ui::unitOf (*this);
        const auto r = getLocalBounds().toFloat();
        const float drive = 0.4f + get (ids::drive) * 0.55f, mix = get (ids::satMix) * 0.01f, warm = get (ids::satWarmth) * 0.01f;
        const int mode = juce::roundToInt (get (ids::satType));
        auto fx = [&] (float x)
        {
            float y = mode == 0 ? std::tanh (drive * (x + 0.06f * warm)) - std::tanh (drive * 0.06f * warm)
                    : mode == 1 ? std::tanh (drive * x) * (1.0f - 0.08f * x * x)
                                : std::atan (drive * 1.2f * x) / std::atan (drive * 1.2f) * 0.92f;
            y = juce::jlimit (-1.0f, 1.0f, y);
            return x + mix * (y - x);
        };
        const float side = std::min (r.getHeight(), r.getWidth() * 0.3f);
        const auto box = juce::Rectangle<float> (side, side).withPosition (r.getX(), r.getCentreY() - side * 0.5f);
        g.setColour (juce::Colours::black.withAlpha (0.3f));
        g.fillRoundedRectangle (box, 16.0f * u);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawLine (box.getX(), box.getCentreY(), box.getRight(), box.getCentreY(), 1.0f);
        g.drawLine (box.getCentreX(), box.getY(), box.getCentreX(), box.getBottom(), 1.0f);
        {
            juce::Path diag; diag.startNewSubPath (box.getBottomLeft()); diag.lineTo (box.getTopRight());
            juce::Path d2; const float dash[] { 4.0f * u, 6.0f * u };
            juce::PathStrokeType (1.0f).createDashedStroke (d2, diag, dash, 2);
            g.setColour (juce::Colours::white.withAlpha (0.14f));
            g.fillPath (d2);
        }
        juce::Path curve;
        for (int i = 0; i <= 80; ++i)
        {
            const float x = -1.0f + 2.0f * (float) i / 80.0f;
            const juce::Point<float> q (box.getCentreX() + x * side * 0.46f, box.getCentreY() - fx (x) * side * 0.46f);
            if (i == 0) curve.startNewSubPath (q); else curve.lineTo (q);
        }
        ui::strokeGlow (g, curve, ui::ember, 3.0f * u, 0.9f);
        g.setColour (ui::ember);
        g.strokePath (curve, juce::PathStrokeType (3.5f * u, juce::PathStrokeType::curved));

        // le sinus avant / après
        const auto wave = r.withTrimmedLeft (side + 40.0f * u);
        juce::Path pin, pout;
        for (int i = 0; i <= 200; ++i)
        {
            const float t = (float) i / 200.0f, x = wave.getX() + wave.getWidth() * t;
            const float s = 0.9f * std::sin (t * 4.0f * kPi + phase);
            const float yi = wave.getCentreY() - s * wave.getHeight() * 0.42f, yo = wave.getCentreY() - fx (s) * wave.getHeight() * 0.42f;
            if (i == 0) { pin.startNewSubPath (x, yi); pout.startNewSubPath (x, yo); } else { pin.lineTo (x, yi); pout.lineTo (x, yo); }
        }
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawHorizontalLine (juce::roundToInt (wave.getCentreY()), wave.getX(), wave.getRight());
        juce::Path dashedIn; const float dash[] { 5.0f * u, 5.0f * u };
        juce::PathStrokeType (2.0f * u).createDashedStroke (dashedIn, pin, dash, 2);
        g.setColour (ui::mist.withAlpha (0.35f));
        g.fillPath (dashedIn);
        ui::strokeGlow (g, pout, ui::ember, 3.0f * u, 0.8f);
        g.setColour (ui::ember);
        g.strokePath (pout, juce::PathStrokeType (3.0f * u, juce::PathStrokeType::curved));
        g.setFont (ui::font (ui::Face::mono, 10.0f * u));
        g.setColour (ui::haze);
        static const char* names[] { "TUBE · H2 DOMINANT, CHAUD", "ANALOG · H3, CONSOLE", "TAPE · COMPRESSION DE BANDE, DOUX" };
        g.drawText (juce::String::fromUTF8 (names[juce::jlimit (0, 2, mode)]), wave.withHeight (18.0f * u), juce::Justification::topLeft, false);
        g.drawText (juce::String::fromUTF8 ("AVANT (POINTILLÉS) / APRÈS (BRAISE)"), wave.withTop (wave.getBottom() - 18.0f * u), juce::Justification::bottomRight, false);
    }

private:
    float get (const char* id) const { return apvts.getRawParameterValue (ids::inst (id, k))->load(); }
    juce::AudioProcessorValueTreeState& apvts;
    int k;
    float phase = 0.0f, last[4] {};
};

// EQ ANALOG : sa courbe de couleur (LOW à sa fréquence, MUD qui creuse, HI et AIR), et le LOW CUT
class AnalogVisual : public juce::Component, public Animated
{
public:
    AnalogVisual (OniriqueVocalBoxAudioProcessor& p, int inst) : apvts (p.apvts), k (inst) { setInterceptsMouseClicks (false, false); }
    void tick() override
    {
        const std::array<float, 6> now { get (ids::low), get (ids::mud), get (ids::high), get (ids::air), get (ids::lowFreq), get (ids::lowCut) };
        if (now != last) { last = now; repaint(); }
    }
    void paint (juce::Graphics& g) override
    {
        const float u = ui::unitOf (*this);
        const auto r = getLocalBounds().toFloat().reduced (10.0f * u, 20.0f * u);
        static constexpr float lowHz[] { 80.0f, 100.0f, 125.0f };
        const float lf = lowHz[juce::jlimit (0, 2, juce::roundToInt (last[4]))];
        auto db = [&] (float f)
        {
            const float lo = last[0] * 0.6f / (1.0f + std::pow (f / lf, 2.0f));
            const float mud = -last[1] * 0.45f * std::exp (-std::pow (std::log2 (f / 350.0f) / 0.8f, 2.0f));
            const float hi = last[2] * 0.5f * std::exp (-std::pow (std::log2 (f / 3500.0f) / 1.2f, 2.0f));
            const float air = last[3] * 0.6f / (1.0f + std::pow (12000.0f / f, 2.0f));
            const float cut = last[5] > 0.5f ? -10.0f * std::log10 (1.0f + std::pow (85.0f / f, 6.0f)) : 0.0f;
            return lo + mud + hi + air + cut;
        };
        auto X = [&] (float f) { return r.getX() + r.getWidth() * std::log (f / 20.0f) / std::log (1000.0f); };
        auto Y = [&] (float d) { return r.getCentreY() - juce::jlimit (-12.0f, 12.0f, d) / 12.0f * r.getHeight() * 0.5f; };
        g.setColour (juce::Colours::white.withAlpha (0.14f));
        g.drawHorizontalLine (juce::roundToInt (r.getCentreY()), r.getX(), r.getRight());
        g.setFont (ui::font (ui::Face::mono, 9.0f * u));
        for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
        {
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.drawVerticalLine (juce::roundToInt (X (f)), r.getY(), r.getBottom());
            g.setColour (juce::Colour (0xff7d6a6e));
            g.drawText (f >= 1000.0f ? juce::String (f / 1000.0f, 0) + "k" : juce::String ((int) f), juce::Rectangle<float> (40.0f * u, 14.0f * u).withCentre ({ X (f), r.getBottom() + 10.0f * u }),
                        juce::Justification::centred, false);
        }
        juce::Path p;
        for (int i = 0; i <= 200; ++i)
        {
            const float f = 20.0f * std::pow (1000.0f, (float) i / 200.0f);
            if (i == 0) p.startNewSubPath (X (f), Y (db (f))); else p.lineTo (X (f), Y (db (f)));
        }
        juce::Path fill (p);
        fill.lineTo (r.getRight(), r.getBottom()); fill.lineTo (r.getX(), r.getBottom()); fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient (ui::ember.withAlpha (0.5f), 0.0f, r.getY(), ui::ember.withAlpha (0.0f), 0.0f, r.getBottom(), false));
        g.fillPath (fill);
        ui::strokeGlow (g, p, ui::ember, 4.0f * u, 0.9f);
        g.setColour (ui::ember);
        g.strokePath (p, juce::PathStrokeType (4.0f * u, juce::PathStrokeType::curved));
    }

private:
    float get (const char* id) const { return apvts.getRawParameterValue (ids::inst (id, k))->load(); }
    juce::AudioProcessorValueTreeState& apvts;
    int k;
    std::array<float, 6> last {};
};

//==============================================================================
// PARAMETRIC EQ : celui d'ONIRIQUE EQ (analyseur, points, bulle, panneau flottant de la bande), les 8 bandes au-dessus
class PeqPanel final : public ModulePanel
{
public:
    PeqPanel (OniriqueVocalBoxAudioProcessor& p, int c) : ModulePanel (p, c), analyzer (p, inst, true), bandPanel (p.apvts, inst)
    {
        addAndMakeVisible (analyzer); anim.push_back (&analyzer);
        for (int b = 0; b < 8; ++b)
        {
            auto& chip = own (std::make_unique<PeqBandChip> (p.apvts, inst));
            chip.setBand (b);
            chip.onClick = [this, b] { selectBand (b); bandPanel.setOpen (true); };
            chips.push_back (&chip);
        }
        listen = &toggle (ids::peqListen, "LISTEN");
        addChildComponent (bandPanel); anim.push_back (&bandPanel);
        bandPanel.onBand = [this] (int b) { selectBand (b); };
        bandPanel.onClose = [this] { bandPanel.setOpen (false); };
        analyzer.onSelect = [this] (int b) { selectBand (b); bandPanel.setOpen (true); };
        analyzer.onBackgroundClick = [this] { bandPanel.setOpen (false); };
        selectBand (2);
    }
    void selectBand (int b)
    {
        selected = juce::jlimit (0, 7, b);
        analyzer.setSelectedSilently (selected);
        bandPanel.setBand (selected);
        for (int k = 0; k < 8; ++k) chips[(size_t) k]->setHighlighted (k == selected);
    }
    void resized() override
    {
        const float u = ui::unitOf (*this);
        auto r = getLocalBounds().toFloat();
        auto top = r.removeFromTop (34.0f * u);
        listen->setBounds (top.removeFromRight (130.0f * u).toNearestInt());
        top.removeFromRight (12.0f * u);
        const float cw = std::min (130.0f * u, top.getWidth() / 8.0f);
        for (int b = 0; b < 8; ++b) chips[(size_t) b]->setBounds (juce::Rectangle<float> (top.getX() + cw * (float) b, top.getY(), cw - 6.0f * u, top.getHeight()).toNearestInt());
        r.removeFromTop (10.0f * u);
        analyzer.setBounds (r.toNearestInt());
        const float pw = std::min (760.0f * u, r.getWidth() * 0.6f), ph = std::min (210.0f * u, r.getHeight() * 0.55f);
        bandPanel.setBounds (juce::Rectangle<float> (pw, ph).withCentre ({ r.getCentreX(), r.getBottom() - 34.0f * u - ph * 0.5f }).toNearestInt());
    }
    void mouseDown (const juce::MouseEvent&) override { analyzer.closeBubble(); }

private:
    SpectrumAnalyzer analyzer;
    BandPanel bandPanel;
    std::vector<PeqBandChip*> chips;
    GlassToggle* listen = nullptr;
    int selected = 2;
};

//==============================================================================
// RESONANCE : l'écran interactif (bandes à glisser, réduction qui descend du haut) + LISTEN + potards
class ResoPanel final : public ModulePanel
{
public:
    ResoPanel (OniriqueVocalBoxAudioProcessor& p, int c) : ModulePanel (p, c), display (p.apvts, inst)
    {
        addAndMakeVisible (display); anim.push_back (&display);
        listen = &toggle (ids::resoListen, "LISTEN");
        for (auto [id, l] : { std::pair { ids::resoAmt, "AMOUNT" }, std::pair { ids::resoLow, "LOW" }, std::pair { ids::resoMid, "MID" }, std::pair { ids::resoHigh, "HIGH" } })
            knobs.push_back (&knob (id, l));
    }
    void tick() override
    {
        ModulePanel::tick();
        if (auto* m = dynamic_cast<chainmods::Reso*> (engine)) display.updateMeter (m->getMeter(), m->isOn());
    }
    void resized() override
    {
        const float u = ui::unitOf (*this);
        auto r = getLocalBounds().toFloat();
        auto bottom = r.removeFromBottom (std::min (160.0f * u, r.getHeight() * 0.4f));
        r.removeFromBottom (10.0f * u);
        display.setBounds (r.toNearestInt());
        listen->setBounds (juce::Rectangle<float> (130.0f * u, 34.0f * u).withPosition (bottom.getX(), bottom.getCentreY() - 17.0f * u).toNearestInt());
        layoutKnobs (knobs, bottom.withTrimmedLeft (150.0f * u), 4);
    }

private:
    ResonanceDisplay display;
    GlassToggle* listen = nullptr;
    std::vector<StarKnob*> knobs;
};

//==============================================================================
// MULTIBAND : la bande affichée, son SOLO, l'écran des 4 bandes, et les réglages (ceux de la bande + communs)
class MultiPanel final : public ModulePanel
{
public:
    MultiPanel (OniriqueVocalBoxAudioProcessor& p, int c) : ModulePanel (p, c), display (p, inst), bands (juce::StringArray { "LOW", "LO-MID", "HI-MID", "HIGH" }),
        presetsBox (p.apvts, inst)
    {
        addAndMakeVisible (bands); anim.push_back (&bands);
        addAndMakeVisible (display); anim.push_back (&display);
        addAndMakeVisible (presetsBox); anim.push_back (&presetsBox);
        for (int b = 0; b < 4; ++b)
        {
            solo[b] = &toggle (ids::mbSolo[b], "SOLO");
            thr[b] = &knob (ids::mbThr[b], "THRESHOLD");
            gain[b] = &knob (ids::mbGain[b], "GAIN");
        }
        common = { &knob (ids::mbAttack, "ATTACK"), &knob (ids::mbRelease, "RELEASE"), &knob (ids::mbOutput, "OUTPUT") };
        bands.onChange = [this] (int b) { show (b); };
        bands.setSelected (1);
        show (1);
    }
    void show (int b)
    {
        band = b;
        for (int k = 0; k < 4; ++k) { solo[k]->setVisible (k == b); thr[k]->setVisible (k == b); gain[k]->setVisible (k == b); }
        resized();
    }
    void resized() override
    {
        const float u = ui::unitOf (*this);
        auto r = getLocalBounds().toFloat();
        auto top = r.removeFromTop (38.0f * u);
        bands.setBounds (top.removeFromLeft (bands.idealWidth (38.0f * u)).toNearestInt());
        top.removeFromLeft (10.0f * u);
        const auto soloArea = top.removeFromLeft (110.0f * u).toNearestInt();
        for (int k = 0; k < 4; ++k) solo[k]->setBounds (soloArea);
        presetsBox.setBounds (top.removeFromRight (260.0f * u).toNearestInt());
        r.removeFromTop (10.0f * u);
        auto bottom = r.removeFromBottom (std::min (150.0f * u, r.getHeight() * 0.36f));
        r.removeFromBottom (10.0f * u);
        display.setBounds (r.toNearestInt());
        std::vector<StarKnob*> ks { thr[band], gain[band] };
        for (auto* k : common) ks.push_back (k);
        layoutKnobs (ks, bottom, 5);
    }

private:
    MultibandDisplay display;
    GlassTabs bands;
    MbPresetBox presetsBox;
    GlassToggle* solo[4] {};
    StarKnob *thr[4] {}, *gain[4] {};
    std::vector<StarKnob*> common;
    int band = 1;
};

//==============================================================================
// les visuels des effets (ceux de la page FX), dans leur cadre
std::unique_ptr<ModulePanel> effectPanel (OniriqueVocalBoxAudioProcessor& p, int code, FxVisual::Kind kind)
{
    auto panel = std::make_unique<VisualPanel> (p, code);
    auto vis = std::make_unique<FxVisual> (p, kind, modules::instanceOf (code));
    auto& v = *vis;
    auto& frame = panel->own (std::make_unique<Frame> (v));
    panel->keep (std::move (vis));
    panel->addAnim (&v);
    panel->visual = &frame;
    return panel;
}
} // namespace

//==============================================================================
std::unique_ptr<ModulePanel> createModulePanel (OniriqueVocalBoxAudioProcessor& p, int code)
{
    if (! modules::valid (code)) return nullptr;
    const int type = modules::typeOf (code), k = modules::instanceOf (code);
    using T = juce::StringArray;

    switch (type)
    {
        case modules::fet:
        {
            auto f = std::make_unique<FacePanel> (p, code, "1176", "FET LIMITING AMPLIFIER");
            f->knobs = { &f->knob (ids::fetIn, "INPUT"), &f->knob (ids::fetOut, "OUTPUT"), &f->knob (ids::fetAttack, "ATTACK"), &f->knob (ids::fetRelease, "RELEASE") };
            f->rows.push_back ({ "RATIO", &f->tabs (ids::fetRatio, T { "4", "8", "12", "20", "ALL" }) });
            return f;
        }
        case modules::opto:
        {
            auto f = std::make_unique<FacePanel> (p, code, "LA-2A", "OPTICAL LEVELING AMPLIFIER");
            f->knobs = { &f->knob (ids::laGain, "GAIN"), &f->knob (ids::laPeak, "PEAK REDUCTION") };
            f->cols = 2;
            f->rows.push_back ({ "MODE", &f->tabs (ids::laLimit, T { "COMPRESS", "LIMIT" }) });
            return f;
        }
        case modules::parallel:
        {
            auto f = std::make_unique<FacePanel> (p, code, "PARALLEL", juce::String::fromUTF8 ("1176 ÉCRASÉ REMIS DANS LA VOIX"));
            f->knobs = { &f->knob (ids::parAmount, "AMOUNT"), &f->knob (ids::parThresh, "THRESHOLD"), &f->knob (ids::parAttack, "ATTACK"),
                         &f->knob (ids::parRelease, "RELEASE"), &f->knob (ids::parDeessAmt, "DE-ESS"), &f->knob (ids::parDeessFreq, "DE-ESS FREQ") };
            f->cols = 3;
            f->rows.push_back ({ "AUTO", &f->toggle (ids::parAuto, "AUTO") });
            return f;
        }
        case modules::gate:
        {
            auto w = std::make_unique<WavePanel> (p, code, true);
            auto* panel = w.get();
            w->scope.removedLabel = juce::String::fromUTF8 ("COUPÉ");
            w->scope.threshold = [panel] { return panel->value (ids::gateThr); };
            w->scope.setThreshold = [&p, k] (float v) { if (auto* prm = p.param (ids::gateThr, k)) prm->setValueNotifyingHost (prm->convertTo0to1 (v)); };
            w->scope.setTooltip ("Drag up or down to set the threshold.");
            for (auto [id, l] : { std::pair { ids::gateThr, "THRESHOLD" }, std::pair { ids::gateRange, "RANGE" }, std::pair { ids::gateAttack, "ATTACK" },
                                  std::pair { ids::gateHold, "HOLD" }, std::pair { ids::gateRelease, "RELEASE" }, std::pair { ids::gateHyst, "HYSTERESIS" } })
                w->knobs.push_back (&w->knob (id, l));
            return w;
        }
        case modules::deess:
        {
            auto w = std::make_unique<WavePanel> (p, code, false);
            w->scope.removedLabel = juce::String::fromUTF8 ("LES S RETIRÉS");
            w->knobs = { &w->knob (ids::deessFreq, "FREQ"), &w->knob (ids::deessAmt, "AMOUNT") };
            return w;
        }
        case modules::noise:
        {
            auto w = std::make_unique<WavePanel> (p, code, true);
            w->scope.removedLabel = juce::String::fromUTF8 ("SOUFFLE RETIRÉ");
            w->knobs = { &w->knob (ids::nsAmount, "AMOUNT") };
            return w;
        }
        case modules::plosive:
        {
            auto w = std::make_unique<WavePanel> (p, code, false);
            w->scope.removedLabel = juce::String::fromUTF8 ("P / B RETIRÉS");
            w->knobs = { &w->knob (ids::plosive, "AMOUNT") };
            return w;
        }
        case modules::reso:  return std::make_unique<ResoPanel> (p, code);
        case modules::peq:   return std::make_unique<PeqPanel> (p, code);
        case modules::multi: return std::make_unique<MultiPanel> (p, code);
        case modules::sat:
        {
            auto v = std::make_unique<VisualPanel> (p, code);
            auto vis = std::make_unique<SatVisual> (p, k);
            auto& sv = *vis;
            v->visual = &v->own (std::make_unique<Frame> (sv));
            v->keep (std::move (vis));
            v->addAnim (&sv);
            v->topRow = { &v->tabs (ids::satType, T { "TUBE", "ANALOG", "TAPE" }) };
            v->knobs = { &v->knob (ids::drive, "DRIVE"), &v->knob (ids::satWarmth, "WARMTH"), &v->knob (ids::satTone, "TONE"),
                         &v->knob (ids::satAir, "AIR"), &v->knob (ids::satMix, "MIX"), &v->knob (ids::satOutput, "OUTPUT") };
            return v;
        }
        case modules::analog:
        {
            auto v = std::make_unique<VisualPanel> (p, code);
            auto vis = std::make_unique<AnalogVisual> (p, k);
            auto& av = *vis;
            v->visual = &v->own (std::make_unique<Frame> (av));
            v->keep (std::move (vis));
            v->addAnim (&av);
            v->topRow = { &v->tabs (ids::lowFreq, T { "LOW 80 Hz", "LOW 100 Hz", "LOW 125 Hz" }), &v->toggle (ids::lowCut, "LOW CUT 85 Hz") };
            v->knobs = { &v->knob (ids::low, "LOW"), &v->knob (ids::mud, "MUD"), &v->knob (ids::high, "HI"), &v->knob (ids::air, "AIR") };
            return v;
        }
        case modules::delay:
        {
            auto v = effectPanel (p, code, FxVisual::Kind::delay);
            auto* vp = static_cast<VisualPanel*> (v.get());
            vp->topRow = { &vp->tabs (ids::delayNote, delayNoteNames()), &vp->toggle (ids::delaySync, "SYNC"), &vp->toggle (ids::delayPingPong, "PING-PONG") };
            vp->knobs = { &vp->knob (ids::delayMix, "SEND"), &vp->knob (ids::delayFeedback, "FEEDBACK"), &vp->knob (ids::delayTime, "TIME"),
                          &vp->knob (ids::delayLowCut, "LOW CUT"), &vp->knob (ids::delayHighCut, "HIGH CUT"), &vp->knob (ids::delayDucking, "DUCKING") };
            return v;
        }
        case modules::reverb:
        {
            auto v = effectPanel (p, code, FxVisual::Kind::reverb);
            auto* vp = static_cast<VisualPanel*> (v.get());
            vp->topRow = { &vp->tabs (ids::reverbMode, T { "ROOM", "PLATE 1", "PLATE 2", "HALL", "SHIMMER" }) };
            vp->knobs = { &vp->knob (ids::reverbMix, "SEND"), &vp->knob (ids::reverbPreDelay, "PRE-DELAY"), &vp->knob (ids::reverbDecay, "DECAY"),
                          &vp->knob (ids::reverbSize, "SIZE"), &vp->knob (ids::reverbDiffusion, "DIFFUSION"), &vp->knob (ids::reverbModRate, "MOD RATE"),
                          &vp->knob (ids::reverbModDepth, "MOD DEPTH"), &vp->knob (ids::reverbLowCut, "LOW CUT"), &vp->knob (ids::reverbHighCut, "HIGH CUT"),
                          &vp->knob (ids::reverbWidth, "WIDTH"), &vp->knob (ids::reverbClean, "CLEAN"), &vp->knob (ids::reverbDucking, "DUCKING") };
            vp->cols = 6;
            vp->knobH = 120.0f;
            return v;
        }
        case modules::chorus:
        {
            auto v = effectPanel (p, code, FxVisual::Kind::chorus);
            auto* vp = static_cast<VisualPanel*> (v.get());
            vp->knobs = { &vp->knob (ids::chorusRate, "RATE"), &vp->knob (ids::chorusDepth, "DEPTH"), &vp->knob (ids::chorusMix, "MIX") };
            return v;
        }
        case modules::flanger:
        {
            auto v = effectPanel (p, code, FxVisual::Kind::flanger);
            auto* vp = static_cast<VisualPanel*> (v.get());
            vp->knobs = { &vp->knob (ids::flangerRate, "RATE"), &vp->knob (ids::flangerDepth, "DEPTH"), &vp->knob (ids::flangerFeedback, "FEEDBACK"),
                          &vp->knob (ids::flangerMix, "MIX") };
            return v;
        }
        case modules::doubler:
        {
            auto v = effectPanel (p, code, FxVisual::Kind::doubler);
            auto* vp = static_cast<VisualPanel*> (v.get());
            vp->knobs = { &vp->knob (ids::doublerAmount, "AMOUNT") };
            return v;
        }
        case modules::phone:
        {
            auto v = effectPanel (p, code, FxVisual::Kind::phone);
            auto* vp = static_cast<VisualPanel*> (v.get());
            vp->knobs = { &vp->knob (ids::phoneAmount, "AMOUNT") };
            return v;
        }
        default: break;
    }
    return createGenericPanel (p, code);
}
