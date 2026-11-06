#include "StartScreen.h"
#include "PluginProcessor.h"
#include "Modules.h"
#include "ProdView.h"

using namespace onirique;

namespace
{
constexpr float kPi = juce::MathConstants<float>::pi;
const juce::Colour& cEmber = onirique::look::ember;
const juce::Colour& cPink = onirique::look::emberPink;
const juce::Colour cBone { 0xffF4ECEC }, cHaze { 0xffa8939a };
juce::String utf8 (const char* s) { return juce::String::fromUTF8 (s); }
constexpr int kDots = 1500;
}

StartScreen::StartScreen (OniriqueVocalBoxAudioProcessor& p) : processor (p)
{
    // les points répartis régulièrement sur la sphère (spirale de Fibonacci)
    dots.reserve (kDots);
    const float golden = kPi * (3.0f - std::sqrt (5.0f));
    for (int i = 0; i < kDots; ++i)
    {
        const float y = 1.0f - 2.0f * ((float) i + 0.5f) / (float) kDots, r = std::sqrt (1.0f - y * y), a = golden * (float) i;
        dots.push_back ({ r * std::cos (a), y, r * std::sin (a) });
    }
    t0 = juce::Time::getMillisecondCounterHiRes() * 0.001;
    // SIDECHAIN : 4096 échantillons de voix et d'instrumentale pour leurs spectres (mis à jour toutes les 3 images)
    vRing.assign (4096, 0.0f);
    xRing.assign (4096, 0.0f);
    fftBuf.assign (8192, 0.0f);
    fft = std::make_unique<juce::dsp::FFT> (12);
    window = std::make_unique<juce::dsp::WindowingFunction<float>> ((size_t) 4096, juce::dsp::WindowingFunction<float>::hann, false);
    liveV.fill (-30.0f); liveX.fill (-30.0f);

    search.setMultiLine (false);
    search.setReturnKeyStartsNewLine (false);
    search.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    search.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    search.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    search.setColour (juce::TextEditor::textColourId, cBone);
    search.setColour (juce::TextEditor::highlightColourId, cEmber.withAlpha (0.35f));
    search.setColour (juce::TextEditor::highlightedTextColourId, juce::Colours::white);
    search.setColour (juce::CaretComponent::caretColourId, cEmber);
    search.onReturnKey = [this] { submit(); };
    addAndMakeVisible (search);
   #if ONIRIQUE_UI_TEST
    // essais locaux : ONIRIQUE_UI_MIX="demande" lance CRÉER LE MIX à l'ouverture (ONIRIQUE_UI_REF=morceau de référence)
    if (const auto m = juce::SystemStats::getEnvironmentVariable ("ONIRIQUE_UI_MIX", {}); m.isNotEmpty())
    {
        search.setText (m, false);
        if (const auto r = juce::SystemStats::getEnvironmentVariable ("ONIRIQUE_UI_REF", {}); r.isNotEmpty()) reference = juce::File (r);
        juce::Component::SafePointer<StartScreen> safe (this);
        juce::Timer::callAfterDelay (500, [safe] { if (safe != nullptr) safe->submit(); });
    }
   #endif
}

StartScreen::~StartScreen()
{
    stopWorker();
    processor.stopCapture();
}

void StartScreen::stopWorker()
{
    cancel.store (true);
    if (worker.joinable()) worker.join();
    cancel.store (false);
}

juce::Rectangle<float> StartScreen::sphereArea() const
{
    const float u = ui::unitOf (*this);
    const auto b = getLocalBounds().toFloat();
    const float top = 150.0f * u, bottom = b.getHeight() - 150.0f * u;
    const float d = std::min (bottom - top, b.getWidth() * 0.5f);
    return juce::Rectangle<float> (d, d).withCentre ({ b.getCentreX(), (top + bottom) * 0.5f });
}

juce::Rectangle<float> StartScreen::barArea() const
{
    const float u = ui::unitOf (*this);
    const auto b = getLocalBounds().toFloat();
    const float w = std::min (980.0f * u, b.getWidth() - 80.0f * u);
    return { b.getCentreX() - w * 0.5f, b.getBottom() - 104.0f * u, w, 60.0f * u };
}

juce::Rectangle<float> StartScreen::goArea() const
{
    const float u = ui::unitOf (*this);
    const auto bar = barArea();
    const float w = 22.0f * u + 14.0f * u + ui::textWidth (ui::css (ui::Face::mono, 11.0f * u, 0.14f), utf8 ("CRÉER LE MIX")) + 26.0f * u;
    // une gélule qui épouse le bout de la barre (même arrondi, 5 de marge tout autour)
    return { bar.getRight() - 5.0f * u - w - 14.0f * u, bar.getY() + 5.0f * u, w + 14.0f * u, bar.getHeight() - 10.0f * u };
}

void StartScreen::resized()
{
    const float u = ui::unitOf (*this);
    const auto bar = barArea();
    search.setBounds (bar.withTrimmedLeft (54.0f * u).withRight (goArea().getX() - 12.0f * u).reduced (0.0f, 12.0f * u).toNearestInt());
    search.setFont (ui::css (ui::Face::body, 16.0f * u));
    search.applyFontToAllText (ui::css (ui::Face::body, 16.0f * u));
    search.setIndents (0, juce::roundToInt (std::max (0.0f, ((float) search.getHeight() - 16.0f * u * 1.25f) * 0.5f)));
    halo = {};
}

void StartScreen::tick()
{
    // le niveau de la voix qui entre (prise de l'analyseur : la chaîne est vide, personne d'autre ne la lit)
    float tmp[2048], pk = 0.0f;
    int got;
    while ((got = processor.inputTap.pull (tmp, 2048)) > 0)
    {
        for (int i = 0; i < got; ++i) pk = std::max (pk, std::abs (tmp[i]));
        pushRing (vRing, vPos, tmp, got);
    }
    // l'instrumentale sur la SIDECHAIN : son niveau (le témoin) et son spectre
    {
        float xpk = processor.consumeSidechainPeak();
        while ((got = processor.sidechainTap.pull (tmp, 2048)) > 0) pushRing (xRing, xPos, tmp, got);
        const float xdb = xpk > 1.0e-6f ? 20.0f * std::log10 (xpk) : -120.0f;
        const float xt = juce::jlimit (0.0f, 1.0f, (xdb + 60.0f) / 54.0f);
        scLevel += (xt > scLevel ? 0.35f : 0.06f) * (xt - scLevel);
        if (--fftCountdown <= 0)
        {
            fftCountdown = 3;
            std::array<float, onirique::mix::kBands> bv {}, bx {};
            bandsOf (vRing, vPos, bv);
            bandsOf (xRing, xPos, bx);
            prodview::smoothBands (bv);   // la forme générale, sans les pics d'un tiers d'octave
            prodview::smoothBands (bx);
            for (int b = 0; b < onirique::mix::kBands; ++b)
            {
                liveV[(size_t) b] += 0.15f * (bv[(size_t) b] - liveV[(size_t) b]);   // et lissé dans le temps
                liveX[(size_t) b] += 0.15f * (bx[(size_t) b] - liveX[(size_t) b]);
            }
        }
        ui::approach (liveVoice, level > 0.15f ? 1.0f : 0.0f, 0.08f);
        ui::approach (liveInst, scLevel > 0.12f ? 1.0f : 0.0f, 0.08f);
       #if ONIRIQUE_UI_TEST
        // essais locaux : ONIRIQUE_UI_TEST=…,sc simule une voix et une prod (conflit vers 2 kHz, boue vers 400 Hz)
        if (juce::SystemStats::getEnvironmentVariable ("ONIRIQUE_UI_TEST", {}).contains ("sc"))
        {
            for (int b = 0; b < onirique::mix::kBands; ++b)
            {
                const float o = std::log2 (onirique::mix::kCentres[b] / 1000.0f);
                liveV[(size_t) b] = -3.0f * o * o + 2.0f * std::sin ((float) b * 0.9f);
                liveX[(size_t) b] = -2.0f * o + (b == 13 ? 4.0f : 0.0f) + (b == 20 || b == 21 ? 7.0f : 0.0f) - (b > 24 ? 6.0f : 0.0f);
            }
            scLevel = 0.7f; liveInst = liveVoice = 1.0f;
        }
       #endif
    }
    const float db = pk > 1.0e-6f ? 20.0f * std::log10 (pk) : -120.0f;
    const float target = juce::jlimit (0.0f, 1.0f, (db + 50.0f) / 44.0f);
    level += (target > level ? 0.35f : 0.06f) * (target - level);
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    ui::approach (listen, stage == Stage::listening ? 1.0f : 0.0f, 0.05f);
    ui::approach (dropGlow, dragging ? 1.0f : 0.0f, 0.15f);
    ui::approach (goHot, overGo ? 1.0f : 0.0f, 0.16f);
    energy += 0.08f * (std::max (level, 0.7f * listen) - energy);
    if (stage == Stage::listening)
    {
        progress = processor.captureProgress();
        const int rs = refState.load (std::memory_order_acquire);
        // les deux écoutes en même temps : ta voix (10 s de lecture) et la voix de la référence (séparée par l'IA)
        const bool takeDone = processor.captureDone() || forceBuild;
        const juce::String ref = rs == 1 ? utf8 ("RÉFÉRENCE : J'ISOLE LA VOIX ") + juce::String (juce::roundToInt (refProgress.load() * 100.0f)) + " %" : juce::String();
        if (! takeDone)
            status = (now - stageStart > 6.0 && progress < 0.02f)
                       ? utf8 ("PAS DE SON : LANCE LA LECTURE, OU RECLIQUE POUR CRÉER SANS ÉCOUTER")
                       : utf8 ("LANCE LA LECTURE : J'ÉCOUTE TA VOIX · ") + juce::String (juce::roundToInt (progress * 10.0f)) + " / 10 S";
        else status = juce::String();
        if (ref.isNotEmpty()) status = status.isEmpty() ? ref : status + utf8 ("  ·  ") + ref;
        if (rs != 1 && (processor.captureDone() || forceBuild)) finish();
    }
    else if (stage == Stage::report && now - stageStart > 6.0)
        apply();
    repaint();
}

void StartScreen::paint (juce::Graphics& g)
{
    const float u = ui::unitOf (*this);
    const auto b = getLocalBounds().toFloat();
    const float t = (float) (juce::Time::getMillisecondCounterHiRes() * 0.001 - t0);

    // le texte, en haut
    g.setColour (cBone);
    g.setFont (ui::css (ui::Face::display, 34.0f * u, 0.01f));
    g.drawText (utf8 ("Quel son pour ta voix ?"), juce::Rectangle<float> (0.0f, 44.0f * u, b.getWidth(), 44.0f * u), juce::Justification::centred, false);
    g.setColour (cHaze);
    g.setFont (ui::css (ui::Face::mono, 11.0f * u, 0.14f));
    g.drawText (utf8 ("DÉCRIS LE MIX D'UN ARTISTE OU D'UN STYLE, OU GLISSE UN DE SES MORCEAUX SUR LA SPHÈRE"),
                juce::Rectangle<float> (0.0f, 98.0f * u, b.getWidth(), 16.0f * u), juce::Justification::centred, false);

    // la sphère
    const auto sa = sphereArea();
    const auto c = sa.getCentre();
    const float R = sa.getWidth() * 0.36f;
    if (! halo.isValid())
    {
        // le halo braise derrière la sphère : calculé une fois par taille
        const int hs = std::max (1, juce::roundToInt (R * 3.2f));
        halo = juce::Image (juce::Image::ARGB, hs, hs, true, juce::SoftwareImageType());
        juce::Graphics hg (halo);
        juce::ColourGradient gr (cEmber.withAlpha (0.22f), (float) hs * 0.5f, (float) hs * 0.5f, cEmber.withAlpha (0.0f), (float) hs * 0.5f, 0.0f, true);
        gr.addColour (0.45, cEmber.withAlpha (0.08f));
        hg.setGradientFill (gr);
        hg.fillEllipse (0.0f, 0.0f, (float) hs, (float) hs);
    }
    g.setOpacity (0.75f + 0.25f * energy + 0.4f * dropGlow);
    g.drawImageAt (halo, juce::roundToInt (c.x - (float) halo.getWidth() * 0.5f), juce::roundToInt (c.y - (float) halo.getHeight() * 0.5f));
    g.setOpacity (1.0f);

    // l'orbite fine et sa petite étoile qui tourne
    const float orbit = R * 1.32f;
    g.setColour (juce::Colours::white.withAlpha (0.06f + 0.10f * dropGlow));
    g.drawEllipse (juce::Rectangle<float> (2.0f * orbit, 2.0f * orbit).withCentre (c), 1.0f);
    {
        const float a = t * 0.35f;
        const juce::Point<float> sp (c.x + orbit * std::sin (a), c.y - orbit * std::cos (a));
        ui::softGlowEllipse (g, juce::Rectangle<float> (6.0f * u, 6.0f * u).withCentre (sp), cEmber.withAlpha (0.9f), 10.0f * u);
        g.setColour (cBone);
        g.fillPath (ui::starPath (sp, 6.0f * u, a));
    }
    // pendant l'écoute : l'orbite se remplit de braise au fil des 10 s de voix
    if (stage == Stage::listening && progress > 0.002f)
    {
        juce::Path arc;
        arc.addCentredArc (c.x, c.y, orbit, orbit, 0.0f, 0.0f, juce::MathConstants<float>::twoPi * juce::jmin (1.0f, progress), true);
        juce::Path stroke;   // la lueur suit le trait (un arc rempli ferait un disque)
        juce::PathStrokeType (2.5f * u, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createStrokedPath (stroke, arc);
        ui::softGlow (g, stroke, cEmber.withAlpha (0.8f), 10.0f * u);
        g.setColour (cEmber);
        g.strokePath (arc, juce::PathStrokeType (2.5f * u, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // les points : rotation lente, ondes sphériques qui partent d'un pôle et parcourent la surface ; plus la voix est forte,
    // plus les ondes se creusent et vont vite
    const float spin = t * (0.22f + 0.35f * listen), tilt = 0.42f;
    const float cs = std::cos (spin), sn = std::sin (spin), ct = std::cos (tilt), st = std::sin (tilt);
    const float amp = 0.035f + 0.16f * energy + 0.05f * dropGlow;
    const float speed = 1.6f + 3.0f * energy;
    const float base = std::max (1.0f, 1.25f * u);
    for (const auto& d : dots)
    {
        // l'onde : distance (angle) au pôle nord du repère de la sphère, et une seconde onde plus lente depuis le côté
        const float th1 = std::acos (juce::jlimit (-1.0f, 1.0f, d.y));
        const float th2 = std::acos (juce::jlimit (-1.0f, 1.0f, d.x));
        const float w = 0.65f * std::sin (9.0f * th1 - t * speed) + 0.35f * std::sin (6.0f * th2 + t * speed * 0.6f);
        const float rr = 1.0f + amp * w;
        // rotation autour de l'axe vertical, puis inclinaison
        float x = (d.x * cs + d.z * sn) * rr, z = (-d.x * sn + d.z * cs) * rr, y = d.y * rr;
        const float y2 = y * ct - z * st, z2 = y * st + z * ct;
        const float depth = (z2 + 1.0f) * 0.5f;   // 0 au fond, 1 devant
        const float crest = juce::jlimit (0.0f, 1.0f, 0.5f + 0.5f * w);
        const float alpha = 0.10f + 0.85f * depth * depth;
        const float sz = base * (0.7f + 1.3f * depth) * (1.0f + 0.5f * crest * energy);
        const auto col = cEmber.interpolatedWith (cBone, 0.25f + 0.6f * crest * depth).withAlpha (alpha);
        g.setColour (col);
        g.fillEllipse (c.x + x * R - sz * 0.5f, c.y + y2 * R - sz * 0.5f, sz, sz);
    }

    // le compte rendu, par-dessus la sphère : ce qui a été entendu et décidé
    if (stage == Stage::report && ! plan.report.isEmpty())
    {
        const float lh = 24.0f * u;
        const int nLines = juce::jmin (12, plan.report.size());
        auto box = juce::Rectangle<float> (std::min (1000.0f * u, b.getWidth() - 80.0f * u), (float) nLines * lh + 36.0f * u).withCentre (c);
        // le panneau SIDECHAIN à droite : le compte rendu s'arrête avant lui
        if (processor.sidechainConnected() || context.valid)
        {
            const float right = scPanelArea().getX() - 20.0f * u;
            if (box.getRight() > right)
            {
                const float w = std::min (box.getWidth(), right - 40.0f * u);
                box = box.withWidth (w).withRightX (right);
            }
        }
        g.setColour (juce::Colour (0xff0b0709).withAlpha (0.82f));
        g.fillRoundedRectangle (box, 22.0f * u);
        ui::drawDemoGlass (g, box, 22.0f * u, u, false);
        auto r = box.reduced (28.0f * u, 18.0f * u);
        for (int i = 0; i < nLines; ++i)
        {
            const auto row = r.removeFromTop (lh);
            g.setColour (i == 0 ? cBone : juce::Colour (0xffd9c6c9));
            g.setFont (ui::css (ui::Face::body, 14.0f * u));
            g.drawText (plan.report[i], row.withTrimmedLeft (20.0f * u), juce::Justification::centredLeft, true);
            g.setColour (cEmber);
            g.fillEllipse (juce::Rectangle<float> (5.0f * u, 5.0f * u).withCentre ({ row.getX() + 6.0f * u, row.getCentreY() }));
        }
    }

    paintSidechain (g, u);

    // l'état, sous la sphère
    juce::String line = status;
    if (line.isEmpty() && reference.existsAsFile()) line = utf8 ("RÉFÉRENCE : ") + reference.getFileName().toUpperCase();
    if (line.isEmpty() && dragging) line = utf8 ("DÉPOSE LE MORCEAU : LA VOIX DE L'ARTISTE SERA ÉCOUTÉE");
    if (line.isNotEmpty())
    {
        g.setColour (dragging ? cPink : cHaze);
        g.setFont (ui::css (ui::Face::mono, 11.0f * u, 0.14f));
        g.drawText (line, juce::Rectangle<float> (0.0f, barArea().getY() - 34.0f * u, b.getWidth(), 16.0f * u), juce::Justification::centred, false);
    }

    // la barre de recherche : gélule de verre, loupe, champ, bouton CRÉER LE MIX
    const auto bar = barArea();
    juce::Path pill; pill.addRoundedRectangle (bar, bar.getHeight() * 0.5f);
    ui::softGlow (g, pill, cEmber.withAlpha (0.18f + 0.25f * (search.hasKeyboardFocus (true) ? 1.0f : 0.0f)), 26.0f * u);
    g.setColour (juce::Colour (0xff120c0e).withAlpha (0.9f));
    g.fillPath (pill);
    ui::drawDemoGlass (g, bar, bar.getHeight() * 0.5f, u, false);
    {
        const auto ic = juce::Rectangle<float> (16.0f * u, 16.0f * u).withCentre ({ bar.getX() + 30.0f * u, bar.getCentreY() });
        g.setColour (cPink);
        g.drawEllipse (ic.reduced (2.0f * u).translated (-1.5f * u, -1.5f * u), 1.6f * u);
        g.drawLine (ic.getRight() - 4.5f * u, ic.getBottom() - 4.5f * u, ic.getRight(), ic.getBottom(), 1.8f * u);
    }
    // l'exemple dans le champ vide (dessiné ici : celui du TextEditor ne s'affiche pas avec un fond transparent)
    if (search.isEmpty() && ! search.hasKeyboardFocus (true))
    {
        g.setColour (cHaze.withAlpha (0.85f));
        g.setFont (ui::css (ui::Face::body, 16.0f * u));
        g.drawText (utf8 ("Ex. : voix de Travis Scott, aérée et sombre · R&B doux façon SZA · rap FR très en avant"), search.getBounds().toFloat(),
                    juce::Justification::centredLeft, true);
    }
    const auto go = goArea();
    {
        // le bouton en gélule : dégradé braise, liseré rose doux, reflet en haut ; il s'éclaire au survol
        const float rad = go.getHeight() * 0.5f, k = 0.85f + 0.15f * goHot;
        g.setGradientFill (juce::ColourGradient (onirique::look::ember.withAlpha (0.78f * k), 0.0f, go.getY(),
                                                 onirique::look::emberDeep.withAlpha (0.85f * k), 0.0f, go.getBottom(), false));
        g.fillRoundedRectangle (go, rad);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.16f), 0.0f, go.getY(), juce::Colours::white.withAlpha (0.0f), 0.0f, go.getCentreY(), false));
        g.fillRoundedRectangle (go.reduced (1.0f), rad);
        g.setColour (onirique::look::emberLit.withAlpha (0.45f + 0.25f * goHot));
        g.drawRoundedRectangle (go.reduced (0.5f), rad, 1.0f);
    }
    g.setColour (juce::Colours::white);
    g.fillPath (ui::starPath ({ go.getX() + 30.0f * u, go.getCentreY() }, 7.0f * u, t * 0.6f));
    g.setFont (ui::css (ui::Face::mono, 11.0f * u, 0.14f));
    g.drawText (utf8 ("CRÉER LE MIX"), go.withTrimmedLeft (44.0f * u), juce::Justification::centredLeft, false);
}

void StartScreen::mouseMove (const juce::MouseEvent& e)
{
    const bool o = goArea().contains (e.position);
    if (o != overGo) { overGo = o; setMouseCursor (o ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor); }
}

void StartScreen::mouseDown (const juce::MouseEvent& e)
{
    if (goArea().contains (e.position)) submit();
    else if (barArea().contains (e.position)) search.grabKeyboardFocus();
    repaint();
}

bool StartScreen::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;mp3;aif;aiff;flac;m4a;ogg")) return true;
    return false;
}

void StartScreen::filesDropped (const juce::StringArray& files, int, int)
{
    dragging = false;
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;mp3;aif;aiff;flac;m4a;ogg")) { reference = juce::File (f); status.clear(); break; }
}

void StartScreen::submit()
{
    if (stage == Stage::listening) { forceBuild = true; return; }   // recliquer : créer sans attendre la prise
    if (stage == Stage::report) { apply(); return; }               // recliquer : monter la chaîne tout de suite
    const auto text = search.getText().trim();
    if (text.isEmpty() && ! reference.existsAsFile())
    {
        status = utf8 ("DÉCRIS LE MIX VOULU OU GLISSE UN MORCEAU DE RÉFÉRENCE");
        search.grabKeyboardFocus();
        return;
    }
    hints = mix::parseHints (text);
    stopWorker();
    refState.store (0);
    if (reference.existsAsFile())
    {
        // la référence : lue, sa voix isolée et analysée sur un autre thread (l'interface reste fluide)
        refState.store (1);
        refProgress.store (0.0f);
        refAi.store (false);
        worker = std::thread ([this, file = reference]
        {
            juce::AudioBuffer<float> song;
            double sr = 44100.0;
            if (! mix::readSong (file, song, sr)) { refState.store (3, std::memory_order_release); return; }
            if (cancel.load()) return;
            // la voix séparée par l'IA (demucs, dans le plugin) ; sans le modèle, celle du centre du mix
            std::vector<float> voice;
            double vr = 0.0;
            const bool ai = mix::separateVoice (song, sr, voice, vr, [this] (float p) { refProgress.store (p); return ! cancel.load(); });
            if (cancel.load()) return;
            if (ai)
            {
                refProfile = mix::analyse (voice.data(), (int) voice.size(), vr);
                refAi.store (true);
            }
            else
            {
                voice = mix::isolateVoice (song, [this] { return cancel.load(); });
                if (cancel.load()) return;
                refProfile = mix::analyse (voice.data(), (int) voice.size(), sr);
                refProfile.fromMix = song.getNumChannels() > 1;
            }
            refState.store (refProfile.valid ? 2 : 3, std::memory_order_release);
        });
    }
    processor.startCapture();
    forceBuild = false;
    stage = Stage::listening;
    stageStart = juce::Time::getMillisecondCounterHiRes() * 0.001;
    status.clear();
}

void StartScreen::finish()
{
    if (worker.joinable()) worker.join();
    processor.stopCapture();
    mix::Profile src, tgt;
    if (processor.captureDone())
    {
        const auto& take = processor.capturedTake();
        src = mix::analyse (take.data(), (int) take.size(), processor.captureRate());
    }
    if (refState.load() == 2) tgt = refProfile;
    // l'instrumentale reçue sur la SIDECHAIN pendant la prise : la voix se règle pour sa prod
    context = {};
    if (processor.captureDone() && processor.captureHasInstrumental())
    {
        const auto& take = processor.capturedTake();
        const auto& inst = processor.capturedInstrumental();
        context = mix::analyseContext (take.data(), inst.data(), (int) juce::jmin (take.size(), inst.size()), processor.captureRate());
    }
    plan = mix::buildPlan (src, tgt, hints, context);
    juce::StringArray head;
    head.add (src.valid ? utf8 ("Ta voix : 10 s écoutées") : utf8 ("Ta voix n'a pas été écoutée : réglages d'après la demande seule"));
    if (refState.load() == 2)
        head.add ((refAi.load() ? utf8 ("Référence : la voix séparée par l'IA (40 s de « ") : utf8 ("Référence : la voix prise au centre du mix de « "))
                  + reference.getFileNameWithoutExtension() + utf8 (refAi.load() ? " »)" : " »"));
    else if (refState.load() == 3) head.add (utf8 ("Référence illisible : ") + reference.getFileName());
    if (context.valid)
    {
        juce::StringArray zones;
        for (int i = 0; i < juce::jmin (3, (int) context.conflicts.size()); ++i)
        {
            const float f = mix::kCentres[context.conflicts[(size_t) i]];
            zones.add (f >= 1000.0f ? juce::String (f / 1000.0f, 1) + " kHz" : juce::String (juce::roundToInt (f)) + " Hz");
        }
        head.add (utf8 ("Instru : écoutée sur la SIDECHAIN · ") + (zones.isEmpty() ? utf8 ("pas de conflit marqué avec la voix")
                                                                     : utf8 ("conflits vers ") + zones.joinIntoString (", ")));
    }
    else if (processor.sidechainConnected())
        head.add (utf8 ("SIDECHAIN branchée mais muette pendant la prise : réglages d'après la voix seule"));
    if (! hints.understood.isEmpty()) head.add (utf8 ("Compris : ") + hints.understood.joinIntoString (", "));
    if (! hints.unknown.isEmpty() && refState.load() != 2)
        head.add (utf8 ("« ") + hints.unknown.joinIntoString (" ") + utf8 (" » : glisse un de ses morceaux sur la sphère pour coller à sa voix"));
    head.addArray (plan.report);
    plan.report = head;
    stage = Stage::report;
    stageStart = juce::Time::getMillisecondCounterHiRes() * 0.001;
    status = utf8 ("JE MONTE LA CHAÎNE… (CLIQUE POUR ALLER PLUS VITE)");
}

void StartScreen::apply()
{
    stage = Stage::idle;
    status.clear();
    auto& apvts = processor.apvts;
    auto set = [&apvts] (const juce::String& id, float v)
    {
        if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
    };
    modules::Chain chain {};
    size_t n = 0;
    for (const auto& s : plan.modules)
    {
        if (n >= chain.size()) break;
        // réglages d'usine, allumé, puis les réglages décidés
        const int k = juce::jlimit (0, kInstances - 1, s.instance);
        for (const auto& id : modules::paramIds (s.type))
            if (auto* p = apvts.getParameter (ids::inst (id.toRawUTF8(), k))) p->setValueNotifyingHost (p->getDefaultValue());
        set (ids::inst (modules::info (s.type).onId, k), 1.0f);
        for (const auto& [id, v] : s.params) set (ids::inst (id.toRawUTF8(), k), v);
        chain[n++] = modules::code (s.type, k);
    }
    // la voix amenée au niveau d'une voix mixée à l'entrée (les modules compensent leur réduction) ; AUTO GAIN reste coupé
    set (ids::input, plan.inputDb);
    set (ids::output, 0.0f);
    set (ids::autoGain, 0.0f);
    processor.setChain (chain);
}

//==============================================================================
// SIDECHAIN : l'instrumentale reçue, son spectre comparé à celui de la voix, les zones où elle la masque
void StartScreen::pushRing (std::vector<float>& ring, int& pos, const float* x, int n)
{
    const int size = (int) ring.size();
    for (int i = 0; i < n; ++i) { ring[(size_t) pos] = x[i]; pos = (pos + 1) % size; }
}

void StartScreen::bandsOf (const std::vector<float>& ring, int pos, std::array<float, onirique::mix::kBands>& out)
{
    namespace mix = onirique::mix;
    const int N = (int) ring.size();
    double e = 0.0;
    for (int i = 0; i < N; ++i) { const float v = ring[(size_t) ((pos + i) % N)]; fftBuf[(size_t) i] = v; e += (double) v * v; }
    if (e / N < 1.0e-9) { out.fill (-30.0f); return; }   // rien ne passe
    std::fill (fftBuf.begin() + N, fftBuf.end(), 0.0f);
    window->multiplyWithWindowingTable (fftBuf.data(), (size_t) N);
    fft->performFrequencyOnlyForwardTransform (fftBuf.data());
    const double sr = processor.captureRate() > 0.0 ? processor.captureRate() : 48000.0;
    std::array<double, mix::kBands> acc {};
    for (int k = 1; k < N / 2; ++k)
    {
        const float f = (float) (k * sr / N);
        for (int b = 0; b < mix::kBands; ++b)
            if (f >= mix::kCentres[b] * 0.8909f && f < mix::kCentres[b] * 1.1225f) { acc[(size_t) b] += (double) fftBuf[(size_t) k] * fftBuf[(size_t) k]; break; }
    }
    double mean = 0.0;
    for (int b = 0; b < mix::kBands; ++b) out[(size_t) b] = acc[(size_t) b] > 1.0e-14 ? (float) (10.0 * std::log10 (acc[(size_t) b])) : -140.0f;
    for (int b = 10; b <= 24; ++b) mean += out[(size_t) b];
    mean /= 15.0;
    for (auto& v : out) v = juce::jlimit (-40.0f, 20.0f, v - (float) mean);   // la forme : chacun ramené à sa moyenne 200 Hz .. 5 kHz
}

juce::Rectangle<float> StartScreen::scPanelArea() const
{
    const float u = ui::unitOf (*this);
    const float w = 360.0f * u, h = 150.0f * u;
    return { (float) getWidth() - 36.0f * u - w, barArea().getY() - 64.0f * u - h, w, h };
}

void StartScreen::paintSidechain (juce::Graphics& g, float u)
{
    namespace mix = onirique::mix;
    bool connected = processor.sidechainConnected();
   #if ONIRIQUE_UI_TEST
    if (juce::SystemStats::getEnvironmentVariable ("ONIRIQUE_UI_TEST", {}).contains ("sc")) connected = true;
   #endif
    const bool showReport = stage == Stage::report && context.valid;
    const auto area = scPanelArea();

    // la pastille SIDECHAIN : le témoin s'allume quand l'instrumentale arrive
    {
        const auto chip = juce::Rectangle<float> (area.getX(), area.getY() - 42.0f * u, area.getWidth(), 32.0f * u);
        juce::Path pill; pill.addRoundedRectangle (chip, chip.getHeight() * 0.5f);
        g.setColour (juce::Colour (0xff120c0e).withAlpha (0.88f));
        g.fillPath (pill);
        ui::drawDemoGlass (g, chip, chip.getHeight() * 0.5f, u, false);
        const auto led = juce::Rectangle<float> (8.0f * u, 8.0f * u).withCentre ({ chip.getX() + 18.0f * u, chip.getCentreY() });
        if (liveInst > 0.02f) ui::softGlowEllipse (g, led, cEmber.withAlpha (0.85f * liveInst), 9.0f * u);
        g.setColour (cEmber.interpolatedWith (juce::Colour (0xff4a3a3e), 1.0f - liveInst));
        g.fillEllipse (led);
        g.setColour (cBone);
        g.setFont (ui::css (ui::Face::mono, 11.0f * u, 0.16f));
        g.drawText ("SIDECHAIN", chip.withTrimmedLeft (32.0f * u).withWidth (90.0f * u), juce::Justification::centredLeft, false);
        const juce::String state = ! connected ? utf8 ("NON BRANCHÉE")
                                 : liveInst > 0.5f ? utf8 ("INSTRU REÇUE") : utf8 ("PAS DE SIGNAL");
        g.setColour (liveInst > 0.5f ? cPink : cHaze);
        g.setFont (ui::css (ui::Face::mono, 10.0f * u, 0.12f));
        g.drawText (state, chip.withTrimmedLeft (122.0f * u).withTrimmedRight (88.0f * u), juce::Justification::centredLeft, true);
        // le niveau reçu
        const auto meter = juce::Rectangle<float> (70.0f * u, 4.0f * u).withCentre ({ chip.getRight() - 50.0f * u, chip.getCentreY() });
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillRoundedRectangle (meter, 2.0f * u);
        g.setColour (cEmber);
        g.fillRoundedRectangle (meter.withWidth (meter.getWidth() * scLevel), 2.0f * u);
    }

    if (! connected && ! showReport)
    {
        // pas de sidechain : comment la brancher
        g.setColour (cHaze);
        g.setFont (ui::css (ui::Face::mono, 10.0f * u, 0.12f));
        g.drawFittedText (utf8 ("ENVOIE L'INSTRU SUR L'ENTRÉE SIDECHAIN :\nLA VOIX SE RÈGLERA POUR SA PROD"),
                          juce::Rectangle<float> (area.getX() + 14.0f * u, area.getY() - 4.0f * u, area.getWidth() - 28.0f * u, 34.0f * u).toNearestInt(),
                          juce::Justification::topLeft, 2);
        return;
    }

    // le panneau : la voix (trait clair) et l'instrumentale (couleur de l'interface), leurs formes comparées ; les conflits en surbrillance
    juce::Path frame; frame.addRoundedRectangle (area, 18.0f * u);
    g.setColour (juce::Colour (0xff0b0709).withAlpha (0.84f));
    g.fillPath (frame);
    ui::drawDemoGlass (g, area, 18.0f * u, u, false);
    auto inner = area.reduced (16.0f * u, 12.0f * u);
    auto head = inner.removeFromTop (16.0f * u);
    g.setFont (ui::css (ui::Face::mono, 10.0f * u, 0.14f));
    g.setColour (cHaze);
    g.drawText (showReport ? utf8 ("VOIX / INSTRU · LES 10 S ÉCOUTÉES") : utf8 ("VOIX / INSTRU · EN DIRECT"), head, juce::Justification::centredLeft, false);
    {
        auto leg = head.removeFromRight (130.0f * u);
        g.setColour (cBone); g.fillRect (leg.removeFromLeft (14.0f * u).withSizeKeepingCentre (14.0f * u, 2.0f * u));
        g.setColour (cHaze); g.drawText ("VOIX", leg.removeFromLeft (48.0f * u).withTrimmedLeft (6.0f * u), juce::Justification::centredLeft, false);
        g.setColour (cEmber); g.fillRect (leg.removeFromLeft (14.0f * u).withSizeKeepingCentre (14.0f * u, 4.0f * u));
        g.setColour (cHaze); g.drawText ("INSTRU", leg.withTrimmedLeft (6.0f * u), juce::Justification::centredLeft, false);
    }
    auto foot = inner.removeFromBottom (16.0f * u);
    const auto plot = inner.reduced (0.0f, 6.0f * u);
    prodview::Bands V = showReport ? context.voice : liveV;   // le compte rendu : la mesure des 10 s, lissée comme le direct
    if (showReport) prodview::smoothBands (V);
    prodview::Bands X = showReport ? context.inst : liveX;
    if (showReport) prodview::smoothBands (X);

    // les conflits, puis les deux formes en courbes douces (le même tracé que la page PROD)
    std::vector<int> conf;
    if (showReport) conf = context.conflicts;
    else if (liveVoice > 0.5f && liveInst > 0.5f) conf = prodview::liveConflicts (V, X);
    prodview::drawCompare (g, plot, V, X, conf, showReport ? 0.95f : 0.4f + 0.55f * liveVoice, showReport ? 1.0f : 0.4f + 0.6f * liveInst, u);

    // en bas : les conflits détectés, en clair
    juce::StringArray zones;
    for (int i = 0; i < juce::jmin (4, (int) conf.size()); ++i)
    {
        const float f = mix::kCentres[conf[(size_t) i]];
        zones.add (f >= 1000.0f ? juce::String (f / 1000.0f, 1) + " kHz" : juce::String (juce::roundToInt (f)) + " Hz");
    }
    g.setFont (ui::css (ui::Face::mono, 10.0f * u, 0.12f));
    g.setColour (zones.isEmpty() ? cHaze : cPink);
    const bool listening = (liveVoice > 0.5f && liveInst > 0.5f) || showReport;
    const juce::String text = ! zones.isEmpty() ? utf8 ("CONFLITS : ") + zones.joinIntoString (" · ")
                            : listening ? utf8 ("PAS DE CONFLIT MARQUÉ")
                            : liveInst < 0.5f ? utf8 ("RIEN SUR LA SIDECHAIN : VÉRIFIE LE ROUTAGE DE L'INSTRU")
                                              : utf8 ("JOUE LA VOIX ET L'INSTRU");
    g.drawText (text, foot, juce::Justification::centredLeft, true);
}
