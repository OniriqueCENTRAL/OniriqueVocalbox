#include "ProdView.h"
#include "PluginProcessor.h"
#include "Parameters.h"

using namespace onirique;

namespace
{
const juce::Colour& cEmber = onirique::look::ember;
const juce::Colour& cPink = onirique::look::emberPink;
const juce::Colour cBone { 0xffF4ECEC }, cHaze { 0xffa8939a };
juce::String utf8 (const char* s) { return juce::String::fromUTF8 (s); }
}

namespace prodview
{
Analyser::Analyser() : ring (4096, 0.0f), buf (8192, 0.0f) {}

void Analyser::push (const float* x, int n)
{
    const int size = (int) ring.size();
    for (int i = 0; i < n; ++i) { ring[(size_t) pos] = x[i]; pos = (pos + 1) % size; }
}

bool Analyser::compute (double sr, Bands& out)
{
    namespace mix = onirique::mix;
    const int N = (int) ring.size();
    double e = 0.0;
    for (int i = 0; i < N; ++i) { const float v = ring[(size_t) ((pos + i) % N)]; buf[(size_t) i] = v; e += (double) v * v; }
    if (e / N < 1.0e-9) return false;
    std::fill (buf.begin() + N, buf.end(), 0.0f);
    window.multiplyWithWindowingTable (buf.data(), (size_t) N);
    fft.performFrequencyOnlyForwardTransform (buf.data());
    if (sr <= 0.0) sr = 48000.0;
    std::array<double, mix::kBands> acc {};
    for (int k = 1; k < N / 2; ++k)
    {
        const float f = (float) (k * sr / N);
        for (int b = 0; b < mix::kBands; ++b)
            if (f >= mix::kCentres[b] * 0.8909f && f < mix::kCentres[b] * 1.1225f) { acc[(size_t) b] += (double) buf[(size_t) k] * buf[(size_t) k]; break; }
    }
    for (int b = 0; b < mix::kBands; ++b) out[(size_t) b] = acc[(size_t) b] > 1.0e-14 ? (float) (10.0 * std::log10 (acc[(size_t) b])) : -140.0f;
    // les tiers d'octave sans case de FFT (tout en bas) prennent leur voisin
    for (int b = mix::kBands - 2; b >= 0; --b) if (acc[(size_t) b] <= 1.0e-14) out[(size_t) b] = out[(size_t) b + 1];
    double mean = 0.0;
    for (int b = 10; b <= 24; ++b) mean += out[(size_t) b];
    mean /= 15.0;
    for (auto& v : out) v = juce::jlimit (-40.0f, 20.0f, v - (float) mean);
    smoothBands (out);
    return true;
}

void smoothBands (Bands& a)
{
    for (int pass = 0; pass < 2; ++pass)
    {
        const auto src = a;
        for (int b = 1; b + 1 < (int) a.size(); ++b)
            a[(size_t) b] = 0.25f * src[(size_t) b - 1] + 0.5f * src[(size_t) b] + 0.25f * src[(size_t) b + 1];
    }
}

juce::Path smoothCurve (const std::vector<juce::Point<float>>& p)
{
    juce::Path path;
    if (p.empty()) return path;
    path.startNewSubPath (p[0]);
    for (size_t i = 0; i + 1 < p.size(); ++i)
    {
        const auto p0 = p[i == 0 ? 0 : i - 1], p1 = p[i], p2 = p[i + 1], p3 = p[i + 2 < p.size() ? i + 2 : i + 1];
        const auto c1 = p1 + (p2 - p0) / 6.0f, c2 = p2 - (p3 - p1) / 6.0f;
        path.cubicTo (c1, c2, p2);
    }
    return path;
}

std::vector<int> liveConflicts (const Bands& v, const Bands& x)
{
    std::vector<int> c;
    for (int b = 10; b <= 26; ++b)
        if (x[(size_t) b] - v[(size_t) b] > 4.0f && v[(size_t) b] > -12.0f) c.push_back (b);
    return c;
}

juce::String zonesText (const std::vector<int>& bands, int maxCount)
{
    juce::StringArray z;
    for (int i = 0; i < juce::jmin (maxCount, (int) bands.size()); ++i)
    {
        const float f = onirique::mix::kCentres[bands[(size_t) i]];
        z.add (f >= 1000.0f ? juce::String (f / 1000.0f, 1) + " kHz" : juce::String (juce::roundToInt (f)) + " Hz");
    }
    return z.joinIntoString (juce::String::fromUTF8 (" · "));
}

void drawCompare (juce::Graphics& g, juce::Rectangle<float> plot, const Bands& V, const Bands& X, const std::vector<int>& conf,
                  float va, float xa, float u)
{
    namespace mix = onirique::mix;
    auto xOf = [&] (float f) { return plot.getX() + plot.getWidth() * (float) (std::log (f / 20.0f) / std::log (1000.0f)); };
    auto yOf = [&] (float db) { return plot.getBottom() - plot.getHeight() * juce::jlimit (0.0f, 1.0f, (db + 24.0f) / 36.0f); };

    // les conflits : un voile de la couleur de l'interface, du bas vers le haut
    for (int b : conf)
    {
        const float x0 = xOf (mix::kCentres[b] * 0.8909f), x1 = xOf (mix::kCentres[b] * 1.1225f);
        g.setGradientFill (juce::ColourGradient (cEmber.withAlpha (0.0f), 0.0f, plot.getY(), cEmber.withAlpha (0.22f), 0.0f, plot.getBottom(), false));
        g.fillRect (juce::Rectangle<float> (x0, plot.getY(), x1 - x0, plot.getHeight()));
    }
    // la ligne 0 dB et les repères 100 Hz, 1 kHz, 10 kHz
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.fillRect (juce::Rectangle<float> (plot.getX(), yOf (0.0f), plot.getWidth(), 1.0f));
    for (float f : { 100.0f, 1000.0f, 10000.0f }) g.fillRect (juce::Rectangle<float> (xOf (f), plot.getY(), 1.0f, plot.getHeight()));

    auto points = [&] (const Bands& a)
    {
        std::vector<juce::Point<float>> p;
        for (int b = 1; b < mix::kBands - 1; ++b) p.push_back ({ xOf (mix::kCentres[b]), yOf (a[(size_t) b]) });
        return p;
    };
    if (xa > 0.02f)
    {
        const auto px = smoothCurve (points (X));
        juce::Path fill (px);
        fill.lineTo (xOf (mix::kCentres[mix::kBands - 2]), plot.getBottom());
        fill.lineTo (xOf (mix::kCentres[1]), plot.getBottom());
        fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient (cEmber.withAlpha (0.30f * xa), 0.0f, plot.getY(), cEmber.withAlpha (0.02f), 0.0f, plot.getBottom(), false));
        g.fillPath (fill);
        g.setColour (cEmber.withAlpha (xa));
        g.strokePath (px, juce::PathStrokeType (1.6f * u, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    if (va > 0.02f)
    {
        g.setColour (cBone.withAlpha (va));
        g.strokePath (smoothCurve (points (V)), juce::PathStrokeType (1.5f * u, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    // les fréquences sous le tracé
    g.setColour (cHaze.withAlpha (0.7f));
    g.setFont (ui::css (ui::Face::mono, 9.0f * u, 0.1f));
    for (auto [f, t] : { std::pair<float, const char*> { 100.0f, "100" }, { 1000.0f, "1k" }, { 10000.0f, "10k" } })
        g.drawText (t, juce::Rectangle<float> (xOf (f) + 4.0f * u, plot.getBottom() - 14.0f * u, 40.0f * u, 12.0f * u), juce::Justification::centredLeft, false);
}
} // namespace prodview

//==============================================================================
ProdPage::ProdPage (OniriqueVocalBoxAudioProcessor& p) : processor (p)
{
    liveV.fill (-30.0f); liveX.fill (-30.0f);
    onToggle = std::make_unique<GlassToggle> (p.apvts, ids::scOn, utf8 ("ADAPTATION"));
    clean = std::make_unique<StarKnob> (p.apvts, ids::scClean, utf8 ("NETTOYAGE"));
    clarity = std::make_unique<StarKnob> (p.apvts, ids::scClarity, utf8 ("CLARTÉ"));
    for (juce::Component* c : { (juce::Component*) onToggle.get(), (juce::Component*) clean.get(), (juce::Component*) clarity.get() })
    {
        addAndMakeVisible (c);
        if (auto* a = dynamic_cast<Animated*> (c)) anim.push_back (a);
    }
    clean->setTooltip (utf8 ("NETTOYAGE : les coupes de la voix là où la prod est chargée comme elle (la boue partagée), "
                             "seulement quand l'instru y pousse. 100 % : ce que CRÉER LE MIX a décidé ; 0 % : aucune."));
    clarity->setTooltip (utf8 ("CLARTÉ : les remontées de la voix là où l'instru la masque, seulement quand elle la couvre. "
                               "100 % : ce que CRÉER LE MIX a décidé ; 0 % : aucune."));
}

void ProdPage::tick()
{
    for (auto* a : anim) a->tick();
    float tmp[2048];
    int got;
    float vpk = 0.0f;
    while ((got = processor.voiceTap.pull (tmp, 2048)) > 0)
    {
        voiceA.push (tmp, got);
        for (int i = 0; i < got; ++i) vpk = std::max (vpk, std::abs (tmp[i]));
    }
    while ((got = processor.sidechainTap.pull (tmp, 2048)) > 0) instA.push (tmp, got);
    const float xpk = processor.consumeSidechainPeak();
    const float xdb = xpk > 1.0e-6f ? 20.0f * std::log10 (xpk) : -120.0f, vdb = vpk > 1.0e-6f ? 20.0f * std::log10 (vpk) : -120.0f;
    const float xt = juce::jlimit (0.0f, 1.0f, (xdb + 60.0f) / 54.0f);
    scLevel += (xt > scLevel ? 0.35f : 0.06f) * (xt - scLevel);
    ui::approach (instLit, xdb > -54.0f ? 1.0f : 0.0f, 0.06f);
    ui::approach (voiceLit, vdb > -50.0f ? 1.0f : 0.0f, 0.06f);
    if (--countdown <= 0)
    {
        countdown = 3;
        const double sr = processor.getSampleRate();
        prodview::Bands bv {}, bx {};
        // lissage dans le temps : la forme générale, pas chaque note
        if (voiceA.compute (sr, bv)) for (int b = 0; b < onirique::mix::kBands; ++b) liveV[(size_t) b] += 0.15f * (bv[(size_t) b] - liveV[(size_t) b]);
        if (instA.compute (sr, bx)) for (int b = 0; b < onirique::mix::kBands; ++b) liveX[(size_t) b] += 0.15f * (bx[(size_t) b] - liveX[(size_t) b]);
    }
   #if ONIRIQUE_UI_TEST
    // essais locaux : ONIRIQUE_UI_TEST=…,sc simule une voix et une prod (conflit vers 2 kHz, boue vers 400 Hz)
    if (juce::SystemStats::getEnvironmentVariable ("ONIRIQUE_UI_TEST", {}).contains ("sc"))
    {
        for (int b = 0; b < onirique::mix::kBands; ++b)
        {
            const float o = std::log2 (onirique::mix::kCentres[b] / 1000.0f);
            liveV[(size_t) b] = -3.0f * o * o + 1.5f * std::sin ((float) b * 0.9f);
            liveX[(size_t) b] = -2.0f * o + (b == 13 ? 4.0f : 0.0f) + (b == 20 || b == 21 ? 7.0f : 0.0f) - (b > 24 ? 6.0f : 0.0f);
        }
        prodview::smoothBands (liveV); prodview::smoothBands (liveX);
        instLit = voiceLit = 1.0f;
    }
   #endif
    repaint();
}

juce::Rectangle<float> ProdPage::closeArea() const
{
    const float u = ui::unitOf (*this);
    return { (float) getWidth() - 26.0f * u - 36.0f * u, 22.0f * u, 36.0f * u, 36.0f * u };
}

juce::Rectangle<float> ProdPage::plotArea() const
{
    const float u = ui::unitOf (*this);
    return { 26.0f * u, 96.0f * u, (float) getWidth() * 0.66f - 26.0f * u, (float) getHeight() - 96.0f * u - 30.0f * u };
}

void ProdPage::resized()
{
    const float u = ui::unitOf (*this);
    const auto plot = plotArea();
    auto side = juce::Rectangle<float> (plot.getRight() + 26.0f * u, plot.getY(), (float) getWidth() - plot.getRight() - 52.0f * u, plot.getHeight());
    onToggle->setBounds (side.removeFromTop (42.0f * u).withSizeKeepingCentre (juce::jmin (side.getWidth(), 220.0f * u), 42.0f * u).toNearestInt());
    side.removeFromTop (22.0f * u);
    auto knobs = side.removeFromTop (150.0f * u);
    clean->setBounds (knobs.removeFromLeft (knobs.getWidth() * 0.5f).reduced (6.0f * u, 0.0f).toNearestInt());
    clarity->setBounds (knobs.reduced (6.0f * u, 0.0f).toNearestInt());
}

void ProdPage::mouseDown (const juce::MouseEvent& e)
{
    if (closeArea().contains (e.position) && onClose) onClose();
}

juce::StringArray ProdPage::activeBands() const
{
    juce::StringArray lines;
    auto& s = processor.apvts;
    auto val = [&s] (const juce::String& id) { auto* v = s.getRawParameterValue (id); return v != nullptr ? v->load() : 0.0f; };
    const auto chain = processor.getChain();
    for (int code : chain)
    {
        if (! modules::valid (code) || modules::typeOf (code) != modules::peq) continue;
        const int k = modules::instanceOf (code);
        for (int b = 0; b < 8; ++b)
            if (val (ids::inst (ids::peqKey[b], k)) > 0.5f && val (ids::inst (ids::peqDyn[b], k)) > 0.5f && val (ids::inst (ids::peqOn[b], k)) > 0.5f)
            {
                const float f = val (ids::inst (ids::peqFreq[b], k)), gdb = val (ids::inst (ids::peqGain[b], k));
                const float dose = gdb < 0.0f ? val (ids::scClean) : val (ids::scClarity);
                const float eff = val (ids::scOn) > 0.5f ? gdb * dose * 0.01f : 0.0f;
                const juce::String hz = f >= 1000.0f ? juce::String (f / 1000.0f, 1) + " kHz" : juce::String (juce::roundToInt (f)) + " Hz";
                lines.add ((gdb < 0.0f ? utf8 ("COUPE ") : utf8 ("REMONTE ")) + hz + "  " + (eff > 0.0f ? "+" : "") + juce::String (eff, 1)
                           + utf8 (" dB  ·  EQ ") + juce::String (k + 1) + utf8 (" BANDE ") + juce::String (b + 1));
            }
    }
    return lines;
}

void ProdPage::paint (juce::Graphics& g)
{
    const float u = ui::unitOf (*this);
    const auto b = getLocalBounds().toFloat();
    juce::Path frame; frame.addRoundedRectangle (b, 26.0f * u);
    g.setColour (juce::Colour (0xff0b0709));   // opaque : le module derrière ne transparaît pas
    g.fillPath (frame);
    ui::drawDemoGlass (g, b, 26.0f * u, u, true);

    // l'en-tête : le point, le nom, ce qu'elle fait ; à droite l'état de la SIDECHAIN et la croix
    const auto dot = juce::Rectangle<float> (8.0f * u, 8.0f * u).withCentre ({ 34.0f * u, 40.0f * u });
    ui::softGlowEllipse (g, dot, cEmber.withAlpha (0.8f), 8.0f * u);
    g.setColour (cEmber); g.fillEllipse (dot);
    g.setColour (cBone);
    g.setFont (ui::css (ui::Face::display, 22.0f * u, 0.02f));
    g.drawText (utf8 ("ADAPTATION À LA PROD"), juce::Rectangle<float> (52.0f * u, 24.0f * u, 420.0f * u, 32.0f * u), juce::Justification::centredLeft, false);
    g.setColour (cHaze);
    g.setFont (ui::css (ui::Face::mono, 10.0f * u, 0.14f));
    g.drawText (utf8 ("VOCALBOX ÉCOUTE L'INSTRU DE LA SIDECHAIN ET NE RÈGLE QUE TA VOIX"),
                juce::Rectangle<float> (52.0f * u, 56.0f * u, 620.0f * u, 16.0f * u), juce::Justification::centredLeft, false);
    {
        const auto cl = closeArea();
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.fillEllipse (cl);
        g.setColour (cBone);
        const auto c = cl.getCentre();
        const float r = 6.0f * u;
        g.drawLine (c.x - r, c.y - r, c.x + r, c.y + r, 1.6f * u);
        g.drawLine (c.x - r, c.y + r, c.x + r, c.y - r, 1.6f * u);
        // la SIDECHAIN : le témoin et ce qui arrive
        const bool connected = processor.sidechainConnected();
        auto st = juce::Rectangle<float> (cl.getX() - 300.0f * u, cl.getY(), 286.0f * u, cl.getHeight());
        const auto led = juce::Rectangle<float> (8.0f * u, 8.0f * u).withCentre ({ st.getX() + 10.0f * u, st.getCentreY() });
        if (instLit > 0.02f) ui::softGlowEllipse (g, led, cEmber.withAlpha (0.85f * instLit), 9.0f * u);
        g.setColour (cEmber.interpolatedWith (juce::Colour (0xff4a3a3e), 1.0f - instLit));
        g.fillEllipse (led);
        g.setColour (instLit > 0.5f ? cPink : cHaze);
        g.setFont (ui::css (ui::Face::mono, 10.0f * u, 0.14f));
        g.drawText (! connected ? utf8 ("SIDECHAIN NON BRANCHÉE") : instLit > 0.5f ? utf8 ("SIDECHAIN · INSTRU REÇUE") : utf8 ("SIDECHAIN · PAS DE SIGNAL"),
                    st.withTrimmedLeft (22.0f * u), juce::Justification::centredLeft, false);
    }

    // le grand spectre : la voix et l'instru en direct, lissés, les conflits
    const auto plot = plotArea();
    juce::Path pf; pf.addRoundedRectangle (plot, 18.0f * u);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillPath (pf);
    ui::drawDemoGlass (g, plot, 18.0f * u, u, false);
    const auto inner = plot.reduced (18.0f * u, 16.0f * u);
    {
        auto leg = inner.withHeight (14.0f * u);
        g.setFont (ui::css (ui::Face::mono, 10.0f * u, 0.14f));
        g.setColour (cHaze);
        g.drawText (utf8 ("VOIX / INSTRU · EN DIRECT"), leg, juce::Justification::centredLeft, false);
        auto r = leg.removeFromRight (140.0f * u);
        g.setColour (cBone); g.fillRect (r.removeFromLeft (14.0f * u).withSizeKeepingCentre (14.0f * u, 2.0f * u));
        g.setColour (cHaze); g.drawText ("VOIX", r.removeFromLeft (52.0f * u).withTrimmedLeft (6.0f * u), juce::Justification::centredLeft, false);
        g.setColour (cEmber); g.fillRect (r.removeFromLeft (14.0f * u).withSizeKeepingCentre (14.0f * u, 4.0f * u));
        g.setColour (cHaze); g.drawText ("INSTRU", r.withTrimmedLeft (6.0f * u), juce::Justification::centredLeft, false);
    }
    const auto conf = voiceLit > 0.5f && instLit > 0.5f ? prodview::liveConflicts (liveV, liveX) : std::vector<int> {};
    prodview::drawCompare (g, inner.withTrimmedTop (24.0f * u).withTrimmedBottom (22.0f * u), liveV, liveX, conf,
                           0.35f + 0.6f * voiceLit, 0.35f + 0.65f * instLit, u);
    g.setFont (ui::css (ui::Face::mono, 10.0f * u, 0.12f));
    g.setColour (conf.empty() ? cHaze : cPink);
    g.drawText (! conf.empty() ? utf8 ("CONFLITS : ") + prodview::zonesText (conf, 4)
                : voiceLit > 0.5f && instLit > 0.5f ? utf8 ("PAS DE CONFLIT MARQUÉ")
                : instLit < 0.5f ? utf8 ("RIEN SUR LA SIDECHAIN : VÉRIFIE LE ROUTAGE DE L'INSTRU") : utf8 ("JOUE LA VOIX ET L'INSTRU"),
                inner.withTop (inner.getBottom() - 14.0f * u), juce::Justification::centredLeft, true);

    // à droite, sous les réglages : les bandes de l'EQ qui suivent la prod
    {
        const auto side = juce::Rectangle<float> (plot.getRight() + 26.0f * u, plot.getY() + 42.0f * u + 22.0f * u + 150.0f * u + 18.0f * u,
                                                  b.getWidth() - plot.getRight() - 52.0f * u, 0.0f).withBottom (plot.getBottom());
        g.setColour (cHaze);
        g.setFont (ui::css (ui::Face::mono, 10.0f * u, 0.14f));
        g.drawText (utf8 ("CE QUE LA PROD FAIT À TA VOIX"), side.withHeight (16.0f * u), juce::Justification::centredLeft, false);
        const auto lines = activeBands();
        auto r = side.withTrimmedTop (24.0f * u);
        g.setFont (ui::css (ui::Face::mono, 11.0f * u, 0.08f));
        if (lines.isEmpty())
        {
            g.setColour (cHaze.withAlpha (0.8f));
            g.drawFittedText (utf8 ("Aucune bande de l'EQ ne suit la prod.\nLance CRÉER LE MIX avec l'instru sur la sidechain,\nou allume SC · INSTRU sur une bande dynamique de l'EQ."),
                              r.toNearestInt(), juce::Justification::topLeft, 4);
        }
        for (const auto& l : lines)
        {
            if (r.getHeight() < 20.0f * u) break;
            const auto row = r.removeFromTop (22.0f * u);
            g.setColour (cEmber);
            g.fillEllipse (juce::Rectangle<float> (5.0f * u, 5.0f * u).withCentre ({ row.getX() + 4.0f * u, row.getCentreY() }));
            g.setColour (cBone);
            g.drawText (l, row.withTrimmedLeft (16.0f * u), juce::Justification::centredLeft, true);
        }
    }
}

//==============================================================================
void ProdButton::tick()
{
    const float pk = processor.sidechainLastPeak();
    const float db = pk > 1.0e-6f ? 20.0f * std::log10 (pk) : -120.0f;
    ui::approach (signal, db > -54.0f ? 1.0f : 0.0f, 0.06f);
    bool moved = ui::approach (lit, open ? 1.0f : 0.0f, 0.16f);
    moved |= ui::approach (hot, hover ? 1.0f : 0.0f, 0.16f);
    juce::ignoreUnused (moved);
    repaint();
}

void ProdButton::paint (juce::Graphics& g)
{
    const float u = ui::unitOf (*this);
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    juce::Path pill; pill.addRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (juce::Colour (0xff120c0e).withAlpha (0.9f));
    g.fillPath (pill);
    if (lit > 0.01f)
    {
        g.setGradientFill (juce::ColourGradient (cEmber.withAlpha (0.55f * lit), 0.0f, r.getY(), onirique::look::emberDeep.withAlpha (0.55f * lit), 0.0f, r.getBottom(), false));
        g.fillPath (pill);
    }
    if (hot > 0.01f) { g.setColour (juce::Colours::white.withAlpha (0.05f * hot)); g.fillPath (pill); }
    ui::drawDemoGlass (g, r, r.getHeight() * 0.5f, u, false);
    const auto led = juce::Rectangle<float> (7.0f * u, 7.0f * u).withCentre ({ r.getX() + 16.0f * u, r.getCentreY() });
    if (signal > 0.02f) ui::softGlowEllipse (g, led, cEmber.withAlpha (0.85f * signal), 8.0f * u);
    g.setColour (cEmber.interpolatedWith (juce::Colour (0xff4a3a3e), 1.0f - signal));
    g.fillEllipse (led);
    g.setColour (cBone);
    g.setFont (ui::css (ui::Face::mono, 11.0f * u, 0.16f));
    g.drawText ("PROD", r.withTrimmedLeft (28.0f * u), juce::Justification::centred, false);
}
