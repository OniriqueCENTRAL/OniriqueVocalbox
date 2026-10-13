#include "PluginEditor.h"

using namespace onirique;

/*  ASSISTANT IA : fenêtre dans le plugin.
    1) STYLE (auto-détection ou choix) et INTENSITÉ ;
    2) LISTEN : on lance la lecture dans le DAW, la voix brute (entrée du plugin) est captée jusqu'à 12 s de voix
       (les silences ne comptent pas) ; ANALYSE NOW possible dès 4 s ;
    3) analyse + optimisation sur un fil à part (Assistant/AssistantEngine.h), avec la progression étape par étape ;
    4) résultats (voix, niveaux, sifflantes, équilibre tonal comparé à la cible, réglages proposés) puis APPLY.
       UNDO remet exactement les réglages d'avant. Changer de style ou d'intensité recalcule sans réécouter.
*/

namespace
{
const char* kStyleLabels[assist::numStyles] { "AUTO-DETECT", "RAP / KICKAGE", "SINGING", "AUTOTUNE", "POP", "R&B" };
const char* kIntensity[3] { "SUBTLE", "BALANCED", "BOLD" };
constexpr double kMaxCaptureSec = 40.0;

juce::String hz (float f)
{
    return f >= 1000.0f ? juce::String (f / 1000.0f, 1) + " kHz" : juce::String (juce::roundToInt (f)) + " Hz";
}

void sparkle (juce::Graphics& g, juce::Point<float> p, float r, juce::Colour c)
{
    juce::Path s;
    const float k = 0.2f * r;
    s.startNewSubPath (p.x, p.y - r);
    s.lineTo (p.x + k, p.y - k); s.lineTo (p.x + r, p.y); s.lineTo (p.x + k, p.y + k);
    s.lineTo (p.x, p.y + r);     s.lineTo (p.x - k, p.y + k); s.lineTo (p.x - r, p.y); s.lineTo (p.x - k, p.y - k);
    s.closeSubPath();
    g.setColour (c);
    g.fillPath (s);
}
} // namespace

AssistantPanel::AssistantPanel (OniriqueVocalBoxAudioProcessor& p) : juce::Thread ("Onirique Assistant"), processor (p)
{
    setWantsKeyboardFocus (true);
    levelHist.assign (150, -100.0f);
    scratch.assign ((size_t) SpectrumTap::kSize, 0.0f);
}

AssistantPanel::~AssistantPanel()
{
    processor.assistListening.store (false);
    cancel = true;
    stopThread (8000);
}

float AssistantPanel::u() const noexcept { return ui::editorScale (*this); }

juce::Rectangle<float> AssistantPanel::card() const noexcept
{
    const float s = u();
    return juce::Rectangle<float> (1200.0f * s, 780.0f * s).withCentre (getLocalBounds().toFloat().getCentre())
               .translated (0.0f, (1.0f - appear) * 24.0f * s);
}

void AssistantPanel::open()
{
    closing = false;
    setVisible (true);
    toFront (true);
    grabKeyboardFocus();
}

void AssistantPanel::close()
{
    if (state == State::listening)
    {
        processor.assistListening.store (false);
        state = proposal.values.empty() ? State::idle : State::ready;
    }
    closing = true;
}

void AssistantPanel::resized() {}

#if ONIRIQUE_UI_TEST
void AssistantPanel::testWithFile (const juce::File& f)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (f));
    if (rd == nullptr) return;
    const int n = (int) std::min<juce::int64> (rd->lengthInSamples, (juce::int64) (rd->sampleRate * 30));
    juce::AudioBuffer<float> buf ((int) rd->numChannels, n);
    rd->read (&buf, 0, n, 0, true, true);
    capture.assign ((size_t) n, 0.0f);
    for (int c = 0; c < buf.getNumChannels(); ++c)
        for (int i = 0; i < n; ++i) capture[(size_t) i] += buf.getSample (c, i) / (float) buf.getNumChannels();
    captureFs = rd->sampleRate;
    voiceSec = kTargetVoiceSec;
    startWork (false);
}
#endif

//------------------------------------------------------------------------------
void AssistantPanel::startListening()
{
    if (isThreadRunning()) return;
    captureFs = processor.getSampleRate();
    if (captureFs <= 0.0)
    {
        failure = "The plugin is not running yet: press play in your DAW, then LISTEN.";
        state = State::failed;
        return;
    }
    capture.clear();
    capture.reserve ((size_t) (captureFs * kMaxCaptureSec));
    voiceSec = silentSec = 0.0f;
    levelNow = -100.0f;
    std::fill (levelHist.begin(), levelHist.end(), -100.0f);
    blockAcc = 0.0;
    blockN = 0;
    while (processor.assistTap.pull (scratch.data(), (int) scratch.size()) > 0) {}   // vide ce qui traînait
    processor.assistListening.store (true);
    failure.clear();
    state = State::listening;
}

void AssistantPanel::finishListening()
{
    processor.assistListening.store (false);
    if (voiceSec < kMinVoiceSec)
    {
        failure = "Not enough voice heard (" + juce::String (voiceSec, 1) + " s). Play a part where the vocal sings or raps, then LISTEN again.";
        state = State::failed;
        return;
    }
    startWork (false);
}

void AssistantPanel::startWork (bool reuseAnalysis)
{
    if (isThreadRunning()) return;
    if (! reuseAnalysis || engine == nullptr)
    {
        std::map<juce::String, float> defaults;
        for (auto* prm : processor.getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
                defaults[r->getParameterID()] = r->convertFrom0to1 (r->getDefaultValue());
        engine = std::make_unique<assist::Engine> (capture, captureFs, std::move (defaults));
        reuseAnalysis = false;
    }
    reuse = reuseAnalysis;
    cancel = false;
    workDone = false;
    {
        const juce::ScopedLock sl (lock);
        progress = 0.0f;
        stage = reuse ? "Rebuilding the chain" : "Starting";
    }
    shownProgress = 0.0f;
    state = State::analysing;
    startThread();
}

void AssistantPanel::run()
{
    auto prog = [this] (float p, const juce::String& s)
    {
        const juce::ScopedLock sl (lock);
        progress = p;
        stage = s;
    };
    assist::Analysis a = reuse ? analysis : engine->analyse (prog, cancel);
    assist::Proposal pr;
    if (a.valid && ! cancel) pr = engine->propose (a, style, intensity, prog, cancel);
    {
        const juce::ScopedLock sl (lock);
        analysis = a;
        if (a.valid && ! cancel) proposal = pr;
    }
    workDone = true;
}

void AssistantPanel::apply()
{
    if ((state != State::ready && state != State::applied) || proposal.values.empty()) return;
    if (undoValues.empty())
    {
        // réglages d'avant l'assistant : gardés même si on réapplique après avoir changé de style
        undoValues = processor.presets.currentValues();
        undoName = processor.presets.getCurrentName();
    }
    processor.presets.applyValues (proposal.values, juce::String ("AI ") + assist::styleName (proposal.style));
    state = State::applied;
}

void AssistantPanel::undo()
{
    if (state != State::applied || undoValues.empty()) return;
    processor.presets.applyValues (undoValues, undoName);
    undoValues.clear();
    state = State::ready;
}

//------------------------------------------------------------------------------
void AssistantPanel::tick()
{
    if (! isVisible()) return;
    ui::approach (appear, closing ? 0.0f : 1.0f, closing ? 0.25f : 0.16f);
    if (closing && appear < 0.02f) { setVisible (false); return; }
    pulse += 1.0f / 60.0f;

    if (state == State::listening)
    {
        // voix brute : on garde tout (40 s max), on compte les dixièmes de seconde où la voix est présente
        const int blockLen = std::max (1, (int) (captureFs * 0.1));
        int got;
        while ((got = processor.assistTap.pull (scratch.data(), (int) scratch.size())) > 0)
            for (int i = 0; i < got; ++i)
            {
                const float x = scratch[(size_t) i];
                if (capture.size() < (size_t) (captureFs * kMaxCaptureSec)) capture.push_back (x);
                blockAcc += (double) x * x;
                if (++blockN >= blockLen)
                {
                    const float db = (float) (10.0 * std::log10 (blockAcc / blockN + 1.0e-12));
                    levelHist.erase (levelHist.begin());
                    levelHist.push_back (db);
                    levelNow = db;
                    (db > -50.0f ? voiceSec : silentSec) += 0.1f;
                    blockAcc = 0.0;
                    blockN = 0;
                }
            }
        if (voiceSec >= kTargetVoiceSec || capture.size() >= (size_t) (captureFs * kMaxCaptureSec))
            finishListening();
    }
    else if (state == State::analysing)
    {
        {
            const juce::ScopedLock sl (lock);
            ui::approach (shownProgress, progress, 0.15f);
        }
        if (workDone && ! isThreadRunning())
        {
            if (analysis.valid && ! proposal.values.empty()) state = State::ready;
            else { failure = analysis.error.isNotEmpty() ? analysis.error : juce::String ("The analysis was interrupted."); state = State::failed; }
        }
    }
    repaint();
}

//------------------------------------------------------------------------------
int AssistantPanel::zoneAt (juce::Point<float> p) const
{
    for (auto it = zones.rbegin(); it != zones.rend(); ++it)
        if (it->second.contains (p)) return it->first;
    return card().contains (p) ? zNone : zBackdrop;
}

void AssistantPanel::mouseMove (const juce::MouseEvent& e)
{
    const int z = zoneAt (e.position);
    const bool clickable = z != zNone && z != zBackdrop;
    hover = z;
    setMouseCursor (clickable ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void AssistantPanel::mouseExit (const juce::MouseEvent&) { hover = zNone; }

void AssistantPanel::mouseDown (const juce::MouseEvent& e)
{
    const int z = zoneAt (e.position);
    const bool hasResults = state == State::ready || state == State::applied;
    if (z == zClose) close();
    else if (z >= zStyle && z < zStyle + assist::numStyles)
    {
        style = z - zStyle;
        if (hasResults) startWork (true);
    }
    else if (z >= zIntensity && z < zIntensity + 3)
    {
        intensity = z - zIntensity;
        if (hasResults) startWork (true);
    }
    else if (z == zListen)
    {
        if (state == State::listening)
        {
            if (voiceSec >= kMinVoiceSec) finishListening();
            else { processor.assistListening.store (false); state = proposal.values.empty() ? State::idle : State::ready; }
        }
        else if (state != State::analysing) startListening();
    }
    else if (z == zAnalyseNow && state == State::listening) finishListening();
    else if (z == zApply) apply();
    else if (z == zUndo) undo();
}

bool AssistantPanel::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey) { close(); return true; }
    return false;
}

//------------------------------------------------------------------------------
void AssistantPanel::paintButtonPill (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, bool primary, bool enabled, int zone) const
{
    const float s = u(), h = r.getHeight();
    const bool hot = enabled && hover == zone;
    juce::Path p;
    p.addRoundedRectangle (r, h * 0.5f);
    if (primary && enabled)
    {
        ui::strokeGlow (g, p, ui::ember, 1.5f * s, hot ? 1.0f : 0.6f);
        g.setGradientFill (juce::ColourGradient (ui::ember.withAlpha (hot ? 0.95f : 0.8f), r.getX(), r.getY(), ui::blood, r.getRight(), r.getBottom(), false));
        g.fillPath (p);
        g.setColour (ui::bone.withAlpha (0.8f));
        g.strokePath (p, juce::PathStrokeType (std::max (1.0f, 1.2f * s)));
    }
    else
    {
        g.setColour (juce::Colours::black.withAlpha (0.36f));
        g.fillPath (p);
        g.setColour (ui::ember.withAlpha (hot ? 0.16f : 0.0f));
        g.fillPath (p);
        g.setColour (ui::line.interpolatedWith (ui::ember, hot ? 0.8f : 0.2f).withMultipliedAlpha (enabled ? 1.0f : 0.5f));
        g.strokePath (p, juce::PathStrokeType (std::max (1.0f, 1.2f * s)));
    }
    g.setColour ((primary && enabled ? ui::bone : ui::mist).withAlpha (enabled ? 1.0f : 0.35f));
    g.setFont (ui::fitFont (ui::Face::body, h * 0.42f, r.getWidth() - h, text, true, 0.10f));
    g.drawText (text, r, juce::Justification::centred, false);
    if (enabled) addZone (zone, r);
}

void AssistantPanel::paint (juce::Graphics& g)
{
    zones.clear();
    const float s = u();
    g.setColour (juce::Colours::black.withAlpha (0.66f * appear));
    g.fillAll();
    if (appear < 0.01f) return;
    g.setOpacity (1.0f);

    const auto c = card();
    {
        // ombre douce (quelques voiles) + verre sombre + liseré braise
        for (int i = 3; i >= 1; --i)
        {
            g.setColour (juce::Colours::black.withAlpha (0.16f * appear));
            g.fillRoundedRectangle (c.expanded (6.0f * s * (float) i).translated (0.0f, 8.0f * s), 22.0f * s + 6.0f * s * (float) i);
        }
        juce::Path cp;
        cp.addRoundedRectangle (c, 22.0f * s);
        g.setGradientFill (juce::ColourGradient (ui::deep2, c.getX(), c.getY(), ui::night, c.getX(), c.getBottom(), false));
        g.fillPath (cp);
        g.setGradientFill (juce::ColourGradient (ui::ember.withAlpha (0.14f), c.getRight(), c.getY(), ui::ember.withAlpha (0.0f),
                                                 c.getRight() - c.getWidth() * 0.5f, c.getY() + c.getHeight() * 0.4f, true));
        g.fillPath (cp);
        g.setColour (ui::line.interpolatedWith (ui::ember, 0.35f));
        g.strokePath (cp, juce::PathStrokeType (std::max (1.0f, 1.2f * s)));
    }

    auto inner = c.reduced (28.0f * s, 22.0f * s);

    // ---- en-tête
    {
        auto head = inner.removeFromTop (64.0f * s);
        const float tw = 0.5f + 0.5f * std::sin (pulse * 2.4f);
        sparkle (g, { head.getX() + 18.0f * s, head.getY() + 22.0f * s }, 17.0f * s, ui::bone);
        sparkle (g, { head.getX() + 36.0f * s, head.getY() + 8.0f * s }, 7.0f * s, ui::ember.withAlpha (0.6f + 0.4f * tw));
        const float tx = head.getX() + 54.0f * s;
        const auto f1 = ui::font (ui::Face::display, 28.0f * s, true, 0.08f);
        g.setFont (f1);
        g.setColour (ui::mist);
        g.drawText ("AI ASSISTANT", juce::Rectangle<float> (tx, head.getY(), 400.0f * s, 36.0f * s), juce::Justification::centredLeft, false);
        const float w1 = ui::textWidth (f1, "AI ASSISTANT");
        g.setColour (ui::ember);
        g.setFont (ui::font (ui::Face::mono, 12.0f * s, true, 0.12f));
        g.drawText ("ONIRIQUE VOCALBOX", juce::Rectangle<float> (tx + w1 + 16.0f * s, head.getY() + 12.0f * s, 220.0f * s, 16.0f * s),
                    juce::Justification::centredLeft, false);
        g.setColour (ui::haze);
        g.setFont (ui::font (ui::Face::body, 14.5f * s));
        g.drawText ("Listens to your vocal while the song plays, then builds the whole VocalBox chain for it: clean EQ, resonances, "
                    "compression, de-essing, colour and level.",
                    juce::Rectangle<float> (tx, head.getY() + 36.0f * s, head.getWidth() - 120.0f * s, 22.0f * s), juce::Justification::centredLeft, true);

        // fermeture
        const auto xr = juce::Rectangle<float> (36.0f * s, 36.0f * s).withPosition (head.getRight() - 36.0f * s, head.getY());
        const bool hot = hover == zClose;
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillEllipse (xr);
        g.setColour (ui::line.interpolatedWith (ui::ember, hot ? 0.9f : 0.2f));
        g.drawEllipse (xr.reduced (0.5f), std::max (1.0f, 1.2f * s));
        juce::Path x;
        const auto xc = xr.getCentre();
        const float k = 7.0f * s;
        x.startNewSubPath (xc.x - k, xc.y - k); x.lineTo (xc.x + k, xc.y + k);
        x.startNewSubPath (xc.x + k, xc.y - k); x.lineTo (xc.x - k, xc.y + k);
        g.setColour (hot ? ui::bone : ui::haze);
        g.strokePath (x, juce::PathStrokeType (std::max (1.3f, 2.0f * s), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        addZone (zClose, xr.expanded (4.0f * s));
    }
    inner.removeFromTop (14.0f * s);

    auto left = inner.removeFromLeft (360.0f * s);
    inner.removeFromLeft (24.0f * s);
    paintLeft (g, left);
    paintRight (g, inner);
}

void AssistantPanel::paintLeft (juce::Graphics& g, juce::Rectangle<float> r)
{
    const float s = u();
    auto label = [&] (const juce::String& t)
    {
        auto lr = r.removeFromTop (20.0f * s);
        g.setColour (ui::haze);
        g.setFont (ui::font (ui::Face::body, 12.5f * s, true, 0.16f));
        g.drawText (t, lr, juce::Justification::centredLeft, false);
        r.removeFromTop (8.0f * s);
    };
    const bool hasResults = state == State::ready || state == State::applied;

    // ---- 1 STYLE
    label ("1   STYLE OF THE SONG");
    {
        const float gw = (r.getWidth() - 10.0f * s) * 0.5f, gh = 40.0f * s;
        for (int i = 0; i < assist::numStyles; ++i)
        {
            const auto cr = juce::Rectangle<float> (r.getX() + (float) (i % 2) * (gw + 10.0f * s), r.getY() + (float) (i / 2) * (gh + 8.0f * s), gw, gh);
            const bool sel = style == i, hot = hover == zStyle + i;
            juce::Path p;
            p.addRoundedRectangle (cr, 12.0f * s);
            g.setColour (juce::Colours::black.withAlpha (0.34f));
            g.fillPath (p);
            if (sel)
            {
                ui::strokeGlow (g, p, ui::ember, 1.2f * s, 0.6f);
                g.setGradientFill (juce::ColourGradient (ui::ember.withAlpha (0.38f), cr.getX(), cr.getY(), ui::blood.withAlpha (0.4f), cr.getRight(), cr.getBottom(), false));
                g.fillPath (p);
            }
            else if (hot)
            {
                g.setColour (ui::ember.withAlpha (0.12f));
                g.fillPath (p);
            }
            g.setColour (sel ? ui::ember.interpolatedWith (ui::bone, 0.4f) : ui::line.interpolatedWith (ui::ember, hot ? 0.6f : 0.0f));
            g.strokePath (p, juce::PathStrokeType (std::max (1.0f, 1.2f * s)));
            juce::String t = kStyleLabels[i];
            if (i == assist::styleAuto && hasResults && analysis.valid) t = "AUTO: " + juce::String (assist::styleName (analysis.detected));
            g.setColour (sel ? ui::bone : ui::mist.withAlpha (0.85f));
            g.setFont (ui::fitFont (ui::Face::body, 14.0f * s, cr.getWidth() - 16.0f * s, t, true, 0.08f));
            g.drawText (t, cr, juce::Justification::centred, false);
            if (state != State::analysing && state != State::listening) addZone (zStyle + i, cr);
        }
        r.removeFromTop (3.0f * (gh + 8.0f * s) + 10.0f * s);
    }

    // ---- 2 INTENSITÉ
    label ("2   INTENSITY");
    {
        auto row = r.removeFromTop (36.0f * s);
        juce::Path bg;
        bg.addRoundedRectangle (row, row.getHeight() * 0.5f);
        g.setColour (juce::Colours::black.withAlpha (0.36f));
        g.fillPath (bg);
        g.setColour (ui::line);
        g.strokePath (bg, juce::PathStrokeType (std::max (1.0f, s)));
        const float w = row.getWidth() / 3.0f;
        for (int i = 0; i < 3; ++i)
        {
            const auto seg = juce::Rectangle<float> (row.getX() + w * (float) i, row.getY(), w, row.getHeight()).reduced (3.0f * s);
            if (intensity == i)
            {
                g.setGradientFill (juce::ColourGradient (ui::ember.withAlpha (0.55f), seg.getX(), seg.getY(), ui::blood.withAlpha (0.6f), seg.getRight(), seg.getBottom(), false));
                g.fillRoundedRectangle (seg, seg.getHeight() * 0.5f);
            }
            else if (hover == zIntensity + i)
            {
                g.setColour (ui::ember.withAlpha (0.12f));
                g.fillRoundedRectangle (seg, seg.getHeight() * 0.5f);
            }
            g.setColour (intensity == i ? ui::bone : ui::haze);
            g.setFont (ui::fitFont (ui::Face::body, 13.0f * s, seg.getWidth() - 8.0f * s, kIntensity[i], true, 0.10f));
            g.drawText (kIntensity[i], seg, juce::Justification::centred, false);
            if (state != State::analysing && state != State::listening) addZone (zIntensity + i, seg);
        }
        r.removeFromTop (18.0f * s);
    }

    // ---- 3 ÉCOUTE : grand bouton rond, anneau de progression
    label ("3   LISTEN TO THE VOCAL");
    {
        const float D = 138.0f * s;
        const auto circle = juce::Rectangle<float> (D, D).withCentre ({ r.getCentreX(), r.getY() + D * 0.5f + 4.0f * s });
        const bool listening = state == State::listening, busy = state == State::analysing;
        const bool hot = hover == zListen;
        const float breathe = 0.5f + 0.5f * std::sin (pulse * (listening ? 5.0f : 2.0f));
        juce::Path disk;
        disk.addEllipse (circle);
        ui::strokeGlow (g, disk, ui::ember, 2.0f * s, (listening ? 0.6f + 0.4f * breathe : 0.35f) + (hot ? 0.3f : 0.0f));
        g.setGradientFill (juce::ColourGradient (ui::ember.withAlpha (listening ? 0.55f : 0.32f + (hot ? 0.15f : 0.0f)), circle.getX(), circle.getY(),
                                                 ui::blood.withAlpha (0.7f), circle.getRight(), circle.getBottom(), false));
        g.fillPath (disk);
        g.setColour (ui::line.interpolatedWith (ui::ember, 0.5f));
        g.strokePath (disk, juce::PathStrokeType (std::max (1.0f, 1.2f * s)));

        // anneau : voix captée (écoute) ou progression (analyse)
        const float prog = listening ? juce::jlimit (0.0f, 1.0f, voiceSec / kTargetVoiceSec) : (busy ? shownProgress : (state == State::idle || state == State::failed ? 0.0f : 1.0f));
        const auto ringR = circle.expanded (9.0f * s);
        juce::Path track;
        track.addCentredArc (ringR.getCentreX(), ringR.getCentreY(), ringR.getWidth() * 0.5f, ringR.getHeight() * 0.5f, 0.0f, 0.0f, juce::MathConstants<float>::twoPi, true);
        g.setColour (ui::line.withMultipliedAlpha (1.4f));
        g.strokePath (track, juce::PathStrokeType (std::max (1.5f, 3.0f * s)));
        if (prog > 0.002f)
        {
            juce::Path arc;
            arc.addCentredArc (ringR.getCentreX(), ringR.getCentreY(), ringR.getWidth() * 0.5f, ringR.getHeight() * 0.5f, 0.0f, 0.0f,
                               prog * juce::MathConstants<float>::twoPi, true);
            ui::strokeGlow (g, arc, ui::ember, 2.0f * s, 0.8f);
            g.setColour (ui::bone);
            g.strokePath (arc, juce::PathStrokeType (std::max (2.0f, 3.5f * s), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        juce::String main = "LISTEN", sub = "press play in your DAW";
        if (listening)     { main = voiceSec >= kMinVoiceSec ? "ANALYSE" : "STOP"; sub = juce::String (voiceSec, 1) + " s of voice"; }
        else if (busy)     { main = juce::String (juce::roundToInt (shownProgress * 100.0f)) + " %"; sub = "working"; }
        else if (state == State::ready || state == State::applied) { main = "LISTEN"; sub = "listen again"; }
        g.setColour (ui::bone);
        g.setFont (ui::fitFont (ui::Face::display, 24.0f * s, D * 0.8f, main, true, 0.08f));
        g.drawText (main, circle.withTrimmedBottom (D * 0.16f), juce::Justification::centred, false);
        g.setColour (ui::mist.withAlpha (0.8f));
        g.setFont (ui::fitFont (ui::Face::body, 12.0f * s, D * 0.8f, sub, false, 0.04f));
        g.drawText (sub, circle.withTrimmedTop (D * 0.42f), juce::Justification::centred, false);
        if (! busy) addZone (zListen, circle);
        r.removeFromTop (D + 26.0f * s);
    }

    // ---- état sous le bouton
    {
        juce::String st;
        switch (state)
        {
            case State::idle:      st = "Play the song (solo the vocal if you can). The assistant listens to the vocal coming into the plugin."; break;
            case State::listening: st = voiceSec >= kMinVoiceSec ? "Listening... it analyses by itself at 12 s of voice, or press ANALYSE."
                                                                 : (silentSec > 3.0f && voiceSec < 0.5f ? "No vocal heard yet: is the song playing, with the plugin on the vocal track?"
                                                                                                         : "Listening to the vocal... keep it playing."); break;
            case State::analysing: { const juce::ScopedLock sl (lock); st = stage + "..."; } break;
            case State::ready:     st = "Ready. Check the proposal on the right, then APPLY."; break;
            case State::applied:   st = "Applied to the VocalBox. Tweak anything you like: UNDO brings your previous settings back."; break;
            case State::failed:    st = failure; break;
        }
        auto sr = r.removeFromTop (44.0f * s);
        g.setColour (state == State::failed ? ui::ember.interpolatedWith (ui::bone, 0.3f) : ui::mist.withAlpha (0.85f));
        g.setFont (ui::font (ui::Face::body, 13.5f * s));
        g.drawFittedText (st, sr.toNearestInt(), juce::Justification::centredTop, 3, 0.9f);
    }

    // ---- APPLIQUER / ANNULER (en bas de la colonne)
    {
        auto bottom = r.removeFromBottom (96.0f * s);
        const auto applyR = bottom.removeFromTop (48.0f * s);
        bottom.removeFromTop (10.0f * s);
        const auto undoR = bottom.removeFromTop (36.0f * s);
        paintButtonPill (g, applyR, state == State::applied ? "APPLIED TO VOCALBOX" : "APPLY TO VOCALBOX", true, hasResults && state != State::applied, zApply);
        if (state == State::applied)
        {
            // coche
            juce::Path ck;
            const float cx = applyR.getX() + 26.0f * s, cy = applyR.getCentreY();
            ck.startNewSubPath (cx - 7.0f * s, cy);
            ck.lineTo (cx - 2.0f * s, cy + 5.0f * s);
            ck.lineTo (cx + 8.0f * s, cy - 6.0f * s);
            g.setColour (ui::ember);
            g.strokePath (ck, juce::PathStrokeType (std::max (1.5f, 2.4f * s), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        paintButtonPill (g, undoR.withSizeKeepingCentre (undoR.getWidth() * 0.6f, undoR.getHeight()), "UNDO", false, state == State::applied, zUndo);
        if (state == State::listening && voiceSec >= kMinVoiceSec)
            paintButtonPill (g, r.removeFromTop (34.0f * s).withSizeKeepingCentre (r.getWidth() * 0.6f, 34.0f * s), "ANALYSE NOW", false, true, zAnalyseNow);
    }
}

void AssistantPanel::paintRight (juce::Graphics& g, juce::Rectangle<float> r)
{
    const float s = u();
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.fillRoundedRectangle (r, 16.0f * s);
    g.setColour (ui::line);
    g.drawRoundedRectangle (r.reduced (0.5f), 16.0f * s, std::max (1.0f, s));
    r = r.reduced (20.0f * s, 16.0f * s);
    switch (state)
    {
        case State::listening: paintListening (g, r); break;
        case State::analysing: paintProgress (g, r); break;
        case State::ready:
        case State::applied:   paintResults (g, r); break;
        default:               paintIntro (g, r); break;
    }
}

void AssistantPanel::paintIntro (juce::Graphics& g, juce::Rectangle<float> r)
{
    const float s = u();
    g.setColour (ui::mist);
    g.setFont (ui::font (ui::Face::display, 20.0f * s, true, 0.06f));
    g.drawText ("HOW IT WORKS", r.removeFromTop (30.0f * s), juce::Justification::centredLeft, false);
    r.removeFromTop (8.0f * s);
    struct Step { const char* title; const char* text; };
    const Step steps[] {
        { "LISTENS", "Press LISTEN and play the song. The assistant records the raw vocal entering the plugin and only counts the moments "
                     "where the voice is there (12 s of voice is plenty)." },
        { "ANALYSES", "Level and dynamics, tonal balance against a polished vocal of the chosen style, sibilance (one or two S zones), "
                      "plosives, narrow resonances, pitch, sung or rapped delivery, autotune." },
        { "BUILDS THE CHAIN", "It designs the parametric EQ (low cut, clean-up cuts, dynamic cuts for harshness), the EQ ANALOG colour, "
                              "then runs the whole VocalBox offline on your voice to tune the 1176, the LA-2A, the multiband and the "
                              "de-essers to measured targets, and matches the output level." },
        { "YOU DECIDE", "Read the proposal, change the style or intensity (it rebuilds instantly), APPLY, and UNDO any time." },
    };
    int n = 1;
    for (const auto& st : steps)
    {
        auto row = r.removeFromTop (92.0f * s);
        const auto badge = juce::Rectangle<float> (34.0f * s, 34.0f * s).withPosition (row.getX(), row.getY() + 4.0f * s);
        g.setColour (ui::ember.withAlpha (0.22f));
        g.fillEllipse (badge);
        g.setColour (ui::ember);
        g.drawEllipse (badge.reduced (0.5f), std::max (1.0f, 1.2f * s));
        g.setColour (ui::bone);
        g.setFont (ui::font (ui::Face::mono, 15.0f * s, true));
        g.drawText (juce::String (n++), badge, juce::Justification::centred, false);
        auto tr = row.withTrimmedLeft (50.0f * s);
        g.setColour (ui::mist);
        g.setFont (ui::font (ui::Face::body, 15.0f * s, true, 0.12f));
        g.drawText (st.title, tr.removeFromTop (22.0f * s), juce::Justification::centredLeft, false);
        g.setColour (ui::haze);
        g.setFont (ui::font (ui::Face::body, 13.5f * s));
        g.drawFittedText (st.text, tr.toNearestInt(), juce::Justification::topLeft, 3, 0.95f);
        r.removeFromTop (6.0f * s);
    }
    if (state == State::failed && failure.isNotEmpty())
    {
        auto fr = r.removeFromTop (54.0f * s);
        g.setColour (ui::ember.withAlpha (0.14f));
        g.fillRoundedRectangle (fr, 12.0f * s);
        g.setColour (ui::ember);
        g.drawRoundedRectangle (fr.reduced (0.5f), 12.0f * s, std::max (1.0f, s));
        g.setColour (ui::bone);
        g.setFont (ui::font (ui::Face::body, 14.0f * s, true));
        g.drawFittedText (failure, fr.reduced (14.0f * s, 6.0f * s).toNearestInt(), juce::Justification::centredLeft, 2, 0.9f);
    }
    else
    {
        g.setColour (ui::haze.withAlpha (0.7f));
        g.setFont (ui::font (ui::Face::body, 12.5f * s));
        g.drawFittedText ("Everything is computed on your computer: nothing is uploaded. Session settings (MIX / LIVE, oversampling, "
                          "bypass) are never touched.", r.removeFromTop (40.0f * s).toNearestInt(), juce::Justification::topLeft, 2, 0.95f);
    }
}

void AssistantPanel::paintListening (juce::Graphics& g, juce::Rectangle<float> r)
{
    const float s = u();
    g.setColour (ui::mist);
    g.setFont (ui::font (ui::Face::display, 20.0f * s, true, 0.06f));
    g.drawText ("LISTENING", r.removeFromTop (30.0f * s), juce::Justification::centredLeft, false);
    r.removeFromTop (6.0f * s);

    // onde : niveau des 15 dernières secondes, en miroir, la voix en braise / os, le silence discret
    auto wave = r.removeFromTop (r.getHeight() * 0.55f);
    g.setColour (juce::Colours::black.withAlpha (0.3f));
    g.fillRoundedRectangle (wave, 12.0f * s);
    {
        const int n = (int) levelHist.size();
        const float bw = wave.getWidth() / (float) n;
        for (int i = 0; i < n; ++i)
        {
            const float db = levelHist[(size_t) i];
            const float a = juce::jlimit (0.0f, 1.0f, (db + 66.0f) / 60.0f);
            const float hh = std::max (1.5f * s, a * (wave.getHeight() * 0.44f));
            const auto bar = juce::Rectangle<float> (wave.getX() + bw * (float) i + bw * 0.18f, wave.getCentreY() - hh, bw * 0.64f, 2.0f * hh);
            const bool voice = db > -50.0f;
            g.setColour (voice ? ui::ember.interpolatedWith (ui::bone, a * 0.6f).withAlpha (0.35f + 0.65f * (float) i / (float) n)
                               : ui::haze.withAlpha (0.25f));
            g.fillRoundedRectangle (bar, bw * 0.3f);
        }
    }
    r.removeFromTop (16.0f * s);

    // voix captée / objectif
    auto pr = r.removeFromTop (60.0f * s);
    g.setColour (ui::haze);
    g.setFont (ui::font (ui::Face::body, 13.0f * s, true, 0.12f));
    g.drawText ("VOICE CAPTURED", pr.removeFromTop (20.0f * s), juce::Justification::centredLeft, false);
    g.setColour (ui::bone);
    g.setFont (ui::font (ui::Face::mono, 15.0f * s, true));
    g.drawText (juce::String (voiceSec, 1) + " / " + juce::String ((int) kTargetVoiceSec) + " s", pr.removeFromTop (20.0f * s).withTrimmedLeft (0.0f),
                juce::Justification::centredRight, false);
    const auto bar = pr.removeFromTop (10.0f * s);
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (bar, 5.0f * s);
    g.setGradientFill (juce::ColourGradient (ui::ember, bar.getX(), 0.0f, ui::bone, bar.getRight(), 0.0f, false));
    g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, voiceSec / kTargetVoiceSec)), 5.0f * s);
    r.removeFromTop (10.0f * s);

    g.setColour (ui::mist.withAlpha (0.85f));
    g.setFont (ui::font (ui::Face::body, 13.5f * s));
    const juce::String lvl = levelNow < -90.0f ? juce::String ("-inf") : juce::String (levelNow, 1);
    g.drawFittedText ("Input level now: " + lvl + " dB RMS.  Tip: let the most typical part of the vocal play (verse or hook); "
                      "silences are skipped automatically.", r.removeFromTop (44.0f * s).toNearestInt(), juce::Justification::topLeft, 2, 0.95f);
}

void AssistantPanel::paintProgress (juce::Graphics& g, juce::Rectangle<float> r)
{
    const float s = u();
    juce::String st;
    {
        const juce::ScopedLock sl (lock);
        st = stage;
    }
    g.setColour (ui::mist);
    g.setFont (ui::font (ui::Face::display, 20.0f * s, true, 0.06f));
    g.drawText (reuse ? "REBUILDING THE CHAIN" : "ANALYSING YOUR VOCAL", r.removeFromTop (30.0f * s), juce::Justification::centredLeft, false);
    r.removeFromTop (14.0f * s);

    auto bar = r.removeFromTop (14.0f * s);
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (bar, 7.0f * s);
    const auto fillR = bar.withWidth (std::max (bar.getHeight(), bar.getWidth() * shownProgress));
    g.setGradientFill (juce::ColourGradient (ui::ember, fillR.getX(), 0.0f, ui::bone, fillR.getRight(), 0.0f, false));
    g.fillRoundedRectangle (fillR, 7.0f * s);
    // reflet qui glisse
    const float sweep = std::fmod (pulse * 0.6f, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.25f));
    g.fillRoundedRectangle (juce::Rectangle<float> (fillR.getX() + (fillR.getWidth() - 40.0f * s) * sweep, fillR.getY(), 40.0f * s, fillR.getHeight()).getIntersection (fillR), 7.0f * s);
    r.removeFromTop (10.0f * s);
    g.setColour (ui::bone);
    g.setFont (ui::font (ui::Face::body, 15.0f * s, true));
    g.drawText (st + "...", r.removeFromTop (24.0f * s), juce::Justification::centredLeft, false);
    r.removeFromTop (14.0f * s);

    struct Step { float at; const char* name; };
    const Step steps[] { { 0.02f, "Levels and dynamics" }, { 0.10f, "Tonal balance, sibilance, resonances" }, { 0.22f, "Pitch, delivery, autotune" },
                         { 0.30f, "Plosives" }, { 0.36f, "Parametric EQ and EQ ANALOG" }, { 0.45f, "1176 (simulated on your voice)" },
                         { 0.58f, "LA-2A (simulated on your voice)" }, { 0.66f, "Multiband" }, { 0.74f, "De-esser(s)" }, { 0.90f, "Output level" } };
    const int nSteps = (int) (sizeof (steps) / sizeof (steps[0]));
    for (int i = 0; i < nSteps; ++i)
    {
        const auto& stp = steps[i];
        if (reuse && stp.at < 0.36f) continue;
        const float next = i + 1 < nSteps ? steps[i + 1].at : 1.01f;
        const bool doneStep = shownProgress >= next - 0.001f, current = ! doneStep && shownProgress >= stp.at - 0.001f;
        auto row = r.removeFromTop (28.0f * s);
        const auto dot = juce::Rectangle<float> (16.0f * s, 16.0f * s).withCentre ({ row.getX() + 10.0f * s, row.getCentreY() });
        if (doneStep)
        {
            g.setColour (ui::ember);
            g.fillEllipse (dot);
            juce::Path ck;
            ck.startNewSubPath (dot.getX() + 4.0f * s, dot.getCentreY());
            ck.lineTo (dot.getCentreX() - 1.0f * s, dot.getBottom() - 4.5f * s);
            ck.lineTo (dot.getRight() - 3.5f * s, dot.getY() + 4.5f * s);
            g.setColour (ui::night);
            g.strokePath (ck, juce::PathStrokeType (std::max (1.2f, 1.8f * s), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        else
        {
            g.setColour (current ? ui::ember.withAlpha (0.5f + 0.5f * std::sin (pulse * 8.0f)) : ui::line.withMultipliedAlpha (1.5f));
            g.drawEllipse (dot.reduced (1.0f), std::max (1.0f, 1.5f * s));
        }
        g.setColour (doneStep ? ui::mist : (current ? ui::bone : ui::haze.withAlpha (0.6f)));
        g.setFont (ui::font (ui::Face::body, 14.0f * s, current));
        g.drawText (stp.name, row.withTrimmedLeft (30.0f * s), juce::Justification::centredLeft, false);
    }
}

void AssistantPanel::paintResults (juce::Graphics& g, juce::Rectangle<float> r)
{
    const float s = u();
    const auto& a = analysis;

    // ---- trois cartes : VOIX, NIVEAUX, SIFFLANTES
    auto row = r.removeFromTop (118.0f * s);
    const float cw = (row.getWidth() - 2.0f * 12.0f * s) / 3.0f;
    auto statCard = [&] (int i, const juce::String& title, const juce::String& big, const juce::StringArray& lines)
    {
        const auto cr = juce::Rectangle<float> (row.getX() + (float) i * (cw + 12.0f * s), row.getY(), cw, row.getHeight());
        g.setColour (juce::Colours::black.withAlpha (0.3f));
        g.fillRoundedRectangle (cr, 12.0f * s);
        g.setColour (ui::line);
        g.drawRoundedRectangle (cr.reduced (0.5f), 12.0f * s, std::max (1.0f, s));
        auto in = cr.reduced (14.0f * s, 10.0f * s);
        g.setColour (ui::haze);
        g.setFont (ui::font (ui::Face::body, 11.5f * s, true, 0.16f));
        g.drawText (title, in.removeFromTop (16.0f * s), juce::Justification::centredLeft, false);
        g.setColour (ui::bone);
        g.setFont (ui::fitFont (ui::Face::display, 21.0f * s, in.getWidth(), big, true, 0.05f));
        g.drawText (big, in.removeFromTop (28.0f * s), juce::Justification::centredLeft, false);
        for (auto& l : lines)
        {
            g.setColour (ui::mist.withAlpha (0.85f));
            g.setFont (ui::fitFont (ui::Face::body, 12.5f * s, in.getWidth(), l, false, 0.02f));
            g.drawText (l, in.removeFromTop (17.0f * s), juce::Justification::centredLeft, false);
        }
    };
    {
        const juce::String reg = a.f0Median <= 0.0f ? juce::String ("register not found")
                               : "Register " + juce::String (juce::roundToInt (a.f0Median)) + " Hz (" + (a.f0Median < 165.0f ? "low" : a.f0Median < 260.0f ? "mid" : "high") + ")";
        statCard (0, "VOICE", assist::styleName (proposal.style),
                  { reg, "Autotune " + juce::String (a.autotune > 0.55f ? "detected" : "not detected") + " (" + juce::String (juce::roundToInt (a.autotune * 100.0f)) + " %)",
                    "Pitched " + juce::String (juce::roundToInt (a.voicedRatio * 100.0f)) + " %" + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + "notes " + juce::String (a.avgRunSec, 2) + " s" });
        statCard (1, "LEVEL", juce::String (a.rmsDb, 1) + " dB RMS",
                  { "Peaks " + juce::String (a.peakDb, 1) + " dB", "Dynamic range " + juce::String (a.dynRangeDb, 1) + " dB",
                    "Noise floor " + (a.noiseDb < -99.0f ? juce::String ("-") : juce::String (juce::roundToInt (a.noiseDb)) + " dB") });
        statCard (2, "SIBILANCE", a.sibShare > 0.004f ? "S at " + hz (a.sibFreq) : juce::String ("Few S"),
                  { a.sibFreq2 > 0.0f ? "Second S zone at " + hz (a.sibFreq2) : juce::String ("One S zone"),
                    "S level " + juce::String (juce::roundToInt (a.sibDb)) + " dB vs the voice",
                    "Plosives " + juce::String (juce::roundToInt (a.plosivesPerMin)) + " / min" });
    }
    r.removeFromTop (12.0f * s);

    // ---- équilibre tonal : spectre moyen de la voix comparé à la cible du style + écarts par zone
    auto tone = r.removeFromTop (196.0f * s);
    g.setColour (juce::Colours::black.withAlpha (0.3f));
    g.fillRoundedRectangle (tone, 12.0f * s);
    g.setColour (ui::line);
    g.drawRoundedRectangle (tone.reduced (0.5f), 12.0f * s, std::max (1.0f, s));
    {
        auto in = tone.reduced (14.0f * s, 10.0f * s);
        auto head = in.removeFromTop (18.0f * s);
        g.setColour (ui::haze);
        g.setFont (ui::font (ui::Face::body, 11.5f * s, true, 0.16f));
        g.drawText ("TONAL BALANCE", head, juce::Justification::centredLeft, false);
        g.setFont (ui::font (ui::Face::mono, 11.0f * s));
        g.setColour (ui::bone);
        g.drawText ("your voice", head.withTrimmedLeft (head.getWidth() * 0.32f), juce::Justification::centredLeft, false);
        g.setColour (ui::ember);
        g.drawText ("target (" + juce::String (assist::styleName (proposal.style)) + ")", head.withTrimmedLeft (head.getWidth() * 0.44f),
                    juce::Justification::centredLeft, false);
        in.removeFromTop (6.0f * s);
        auto chart = in.removeFromLeft (in.getWidth() * 0.6f);
        in.removeFromLeft (14.0f * s);

        if (a.spectrum.size() > 4 && a.target.size() == a.spectrum.size())
        {
            float lo = 1.0e9f, hi = -1.0e9f;
            for (size_t k = 0; k < a.spectrum.size(); ++k)
            {
                const float f = 50.0f * std::pow (2.0f, (float) k / 6.0f);
                if (f < 70.0f) continue;
                lo = std::min ({ lo, a.spectrum[k], a.target[k] });
                hi = std::max ({ hi, a.spectrum[k], a.target[k] });
            }
            lo = std::max (lo, hi - 45.0f);
            auto xOf = [&] (size_t k) { return chart.getX() + (float) k / (float) (a.spectrum.size() - 1) * chart.getWidth(); };
            auto yOf = [&] (float d) { return chart.getBottom() - 14.0f * s - juce::jlimit (0.0f, 1.0f, (d - lo) / (hi - lo + 1.0e-3f)) * (chart.getHeight() - 18.0f * s); };
            for (float fq : { 100.0f, 1000.0f, 10000.0f })
            {
                const float x = chart.getX() + std::log2 (fq / 50.0f) * 6.0f / (float) (a.spectrum.size() - 1) * chart.getWidth();
                g.setColour (ui::line);
                g.fillRect (juce::Rectangle<float> (x, chart.getY(), 1.0f, chart.getHeight() - 14.0f * s));
                g.setColour (ui::haze.withAlpha (0.8f));
                g.setFont (ui::font (ui::Face::mono, 10.0f * s));
                g.drawText (fq >= 1000.0f ? juce::String ((int) (fq / 1000.0f)) + "k" : juce::String ((int) fq),
                            juce::Rectangle<float> (x + 3.0f * s, chart.getBottom() - 13.0f * s, 40.0f * s, 12.0f * s), juce::Justification::centredLeft, false);
            }
            juce::Path pv, pt;
            for (size_t k = 0; k < a.spectrum.size(); ++k)
            {
                const float x = xOf (k);
                if (k == 0) { pv.startNewSubPath (x, yOf (a.spectrum[k])); pt.startNewSubPath (x, yOf (a.target[k])); }
                else        { pv.lineTo (x, yOf (a.spectrum[k]));          pt.lineTo (x, yOf (a.target[k])); }
            }
            juce::Path fill (pv);
            fill.lineTo (chart.getRight(), chart.getBottom() - 14.0f * s);
            fill.lineTo (chart.getX(), chart.getBottom() - 14.0f * s);
            fill.closeSubPath();
            g.setGradientFill (juce::ColourGradient (ui::bone.withAlpha (0.25f), 0.0f, chart.getY(), ui::bone.withAlpha (0.0f), 0.0f, chart.getBottom(), false));
            g.fillPath (fill);
            g.setColour (ui::bone.withAlpha (0.9f));
            g.strokePath (pv, juce::PathStrokeType (std::max (1.0f, 1.4f * s), juce::PathStrokeType::curved));
            ui::strokeGlow (g, pt, ui::ember, 1.6f * s, 0.6f);
            g.setColour (ui::ember);
            g.strokePath (pt, juce::PathStrokeType (std::max (1.2f, 2.0f * s), juce::PathStrokeType::curved));
        }

        // barres d'écart par zone (centre = cible)
        const float bh = in.getHeight() / (float) assist::kZones;
        for (int z = 0; z < assist::kZones; ++z)
        {
            auto zr = juce::Rectangle<float> (in.getX(), in.getY() + bh * (float) z, in.getWidth(), bh).reduced (0.0f, 2.0f * s);
            const auto nameR = zr.removeFromLeft (zr.getWidth() * 0.34f);
            const auto valR = zr.removeFromRight (zr.getWidth() * 0.22f);
            g.setColour (ui::haze);
            g.setFont (ui::fitFont (ui::Face::body, 11.0f * s, nameR.getWidth(), assist::zoneName (z), true, 0.06f));
            g.drawText (assist::zoneName (z), nameR, juce::Justification::centredLeft, false);
            const float dv = a.zoneDev[(size_t) z];
            const auto track = zr.reduced (4.0f * s, zr.getHeight() * 0.3f);
            g.setColour (juce::Colours::black.withAlpha (0.45f));
            g.fillRoundedRectangle (track, track.getHeight() * 0.5f);
            g.setColour (ui::line.withMultipliedAlpha (2.0f));
            g.fillRect (juce::Rectangle<float> (track.getCentreX(), track.getY() - 2.0f * s, 1.0f, track.getHeight() + 4.0f * s));
            const float w = juce::jlimit (-1.0f, 1.0f, dv / 10.0f) * track.getWidth() * 0.5f;
            const auto fillR = w >= 0.0f ? track.withX (track.getCentreX()).withWidth (w) : track.withX (track.getCentreX() + w).withWidth (-w);
            g.setColour (std::abs (dv) < 2.0f ? ui::haze : (dv > 0.0f ? ui::ember : ui::bone.withAlpha (0.8f)));
            g.fillRoundedRectangle (fillR, track.getHeight() * 0.5f);
            g.setColour (ui::mist);
            g.setFont (ui::font (ui::Face::mono, 11.0f * s));
            g.drawText ((dv > 0.0f ? "+" : "") + juce::String (dv, 1), valR, juce::Justification::centredRight, false);
        }
    }
    r.removeFromTop (12.0f * s);

    // ---- ce que l'assistant règle, module par module
    g.setColour (juce::Colours::black.withAlpha (0.3f));
    g.fillRoundedRectangle (r, 12.0f * s);
    g.setColour (state == State::applied ? ui::ember.withAlpha (0.7f) : ui::line);
    g.drawRoundedRectangle (r.reduced (0.5f), 12.0f * s, std::max (1.0f, s));
    auto in = r.reduced (14.0f * s, 10.0f * s);
    g.setColour (ui::haze);
    g.setFont (ui::font (ui::Face::body, 11.5f * s, true, 0.16f));
    g.drawText (state == State::applied ? "APPLIED TO THE VOCALBOX" : "WHAT THE ASSISTANT WILL SET", in.removeFromTop (18.0f * s), juce::Justification::centredLeft, false);
    in.removeFromTop (4.0f * s);
    const float lh = std::min (21.0f * s, in.getHeight() / (float) std::max (1, proposal.lines.size()));
    for (auto& line : proposal.lines)
    {
        auto lr = in.removeFromTop (lh);
        const int split = line.indexOf ("  ");
        const juce::String name = split > 0 ? line.substring (0, split) : line;
        const juce::String rest = split > 0 ? line.substring (split).trim() : juce::String();
        const auto nf = ui::font (ui::Face::body, std::min (13.5f * s, lh * 0.7f), true, 0.10f);
        g.setColour (ui::ember.interpolatedWith (ui::bone, 0.25f));
        g.setFont (nf);
        const auto nameR = lr.removeFromLeft (142.0f * s);
        g.drawText (name, nameR, juce::Justification::centredLeft, false);
        g.setColour (ui::mist.withAlpha (0.9f));
        g.setFont (ui::fitFont (ui::Face::body, std::min (13.0f * s, lh * 0.68f), lr.getWidth(), rest, false, 0.0f));
        g.drawText (rest, lr, juce::Justification::centredLeft, false);
    }
}
