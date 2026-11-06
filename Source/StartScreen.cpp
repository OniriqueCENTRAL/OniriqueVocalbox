#include "StartScreen.h"
#include "PluginProcessor.h"
#include "Modules.h"

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
        for (int i = 0; i < got; ++i) pk = std::max (pk, std::abs (tmp[i]));
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
    // l'étoile qui tourne, par-dessus l'orbite et son arc : une lueur douce, l'étoile claire et son cœur blanc
    {
        const float a = t * 0.35f;
        const juce::Point<float> sp (c.x + orbit * std::sin (a), c.y - orbit * std::cos (a));
        ui::softGlowEllipse (g, juce::Rectangle<float> (5.0f * u, 5.0f * u).withCentre (sp), cEmber.withAlpha (0.35f), 7.0f * u);
        ui::softGlowEllipse (g, juce::Rectangle<float> (3.0f * u, 3.0f * u).withCentre (sp), juce::Colours::white.withAlpha (0.35f), 4.0f * u);
        g.setColour (cBone);
        g.fillPath (ui::starPath (sp, 7.5f * u, a));
        g.setColour (juce::Colours::white);
        g.fillEllipse (juce::Rectangle<float> (2.2f * u, 2.2f * u).withCentre (sp));
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
    plan = mix::buildPlan (src, tgt, hints);
    juce::StringArray head;
    head.add (src.valid ? utf8 ("Ta voix : 10 s écoutées") : utf8 ("Ta voix n'a pas été écoutée : réglages d'après la demande seule"));
    if (refState.load() == 2)
        head.add ((refAi.load() ? utf8 ("Référence : la voix séparée par l'IA (40 s de « ") : utf8 ("Référence : la voix prise au centre du mix de « "))
                  + reference.getFileNameWithoutExtension() + utf8 (refAi.load() ? " »)" : " »"));
    else if (refState.load() == 3) head.add (utf8 ("Référence illisible : ") + reference.getFileName());
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
