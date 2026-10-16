#include "PluginEditor.h"

using namespace onirique;

/*  Page FX : les effets de voix (fin de chaîne), en plein écran dans la fenêtre du plugin.
    CHORUS, FLANGER et DELAY en haut, la REVERB en grand dessous (modes, visuel, MIX au centre, réglages de part et
    d'autre, coupe-bas / DUCKING / coupe-haut en bas). Rien n'est caché : chaque réglage est sous la main.
*/

//==============================================================================
PillSlider::PillSlider (juce::AudioProcessorValueTreeState& apvts, const char* paramId, const juce::String& n, const juce::String& tip)
    : name (n)
{
    setTooltip (tip);
    setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
    param = apvts.getParameter (paramId);
    if (param != nullptr)
    {
        attachment = std::make_unique<juce::ParameterAttachment> (*param, [this] (float) { repaint(); });
        attachment->sendInitialUpdate();
    }
}

void PillSlider::tick()
{
    if (param == nullptr) return;
    const float target = param->getValue();
    bool changed = false;
    if (shown < 0.0f) { shown = target; changed = true; }
    changed |= ui::approach (shown, target, 0.3f);
    changed |= ui::approach (glow, dragging ? 1.0f : (hovering ? 0.6f : 0.0f), 0.18f);
    if (changed) repaint();
}

void PillSlider::mouseDown (const juce::MouseEvent&)
{
    if (param == nullptr) return;
    dragging = true;
    startNorm = param->getValue();
    attachment->beginGesture();
}

void PillSlider::mouseDrag (const juce::MouseEvent& e)
{
    if (param == nullptr) return;
    const float range = (float) getWidth() * (e.mods.isShiftDown() ? 4.0f : 1.0f);   // Maj = réglage fin
    const float norm = juce::jlimit (0.0f, 1.0f, startNorm + (float) e.getDistanceFromDragStartX() / std::max (1.0f, range));
    attachment->setValueAsPartOfGesture (param->convertFrom0to1 (norm));
}

void PillSlider::mouseUp (const juce::MouseEvent&)
{
    if (param == nullptr) return;
    dragging = false;
    attachment->endGesture();
}

void PillSlider::mouseDoubleClick (const juce::MouseEvent&)
{
    if (param != nullptr) attachment->setValueAsCompleteGesture (param->convertFrom0to1 (param->getDefaultValue()));
}

void PillSlider::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (param == nullptr) return;
    const float d = std::abs (w.deltaY) > std::abs (w.deltaX) ? w.deltaY : -w.deltaX;
    const float norm = juce::jlimit (0.0f, 1.0f, param->getValue() + d * (e.mods.isShiftDown() ? 0.01f : 0.05f));
    attachment->setValueAsCompleteGesture (param->convertFrom0to1 (norm));
}

void PillSlider::paint (juce::Graphics& g)
{
    if (param == nullptr) return;
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const float h = r.getHeight(), u = h / 36.0f, rad = h * 0.5f;
    juce::Path shape;
    shape.addRoundedRectangle (r, rad);
    ui::strokeGlow (g, shape, ui::ember, 1.0f * u, 0.5f * glow);
    g.setColour (juce::Colours::black.withAlpha (0.38f));
    g.fillPath (shape);

    // remplissage jusqu'à la valeur : dégradé sang -> braise, reflet clair en haut, trait lumineux au bout
    const float v = juce::jlimit (0.0f, 1.0f, shown < 0.0f ? param->getValue() : shown);
    if (v > 0.001f)
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (shape);
        const auto fill = r.withWidth (std::max (h * 0.5f, r.getWidth() * v));
        g.setGradientFill (juce::ColourGradient (ui::blood.withAlpha (0.85f), fill.getX(), 0.0f, ui::ember.withAlpha (0.55f + 0.25f * glow), fill.getRight(), 0.0f, false));
        g.fillRect (fill);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.10f), 0.0f, fill.getY(), juce::Colours::white.withAlpha (0.0f), 0.0f, fill.getCentreY(), false));
        g.fillRect (fill);
        g.setColour (ui::bone.withAlpha (0.75f + 0.25f * glow));
        g.fillRect (juce::Rectangle<float> (fill.getRight() - 2.0f * u, fill.getY() + h * 0.22f, 2.0f * u, h * 0.56f));
    }
    g.setColour (ui::line.interpolatedWith (ui::ember, 0.15f + 0.6f * glow));
    g.strokePath (shape, juce::PathStrokeType (std::max (1.0f, 1.1f * u)));

    auto tr = r.reduced (h * 0.42f, 0.0f);
    const auto vf = ui::font (ui::Face::mono, h * 0.40f, true);
    const juce::String val = param->getCurrentValueAsText();
    const float vw = ui::textWidth (vf, val) + 6.0f * u;
    g.setColour (ui::mist);
    g.setFont (vf);
    g.drawText (val, tr.removeFromRight (vw), juce::Justification::centredRight, false);
    g.setColour (ui::bone.withAlpha (0.88f));
    g.setFont (ui::fitFont (ui::Face::body, h * 0.36f, tr.getWidth() - 4.0f * u, name, true, 0.12f));
    g.drawText (name, tr, juce::Justification::centredLeft, false);
}

//==============================================================================
void FxVisual::tick()
{
    if (! isShowing()) return;
    static const char* onIds[] { ids::chorusOn, ids::flangerOn, ids::delayOn, ids::reverbOn };
    const bool isOn = param (onIds[(int) kind]) > 0.5f;
    ui::approach (on, isOn ? 1.0f : 0.0f, 0.12f);
    float rate = 0.5f;
    if (kind == Kind::chorus) rate = param (ids::chorusRate);
    else if (kind == Kind::flanger) rate = param (ids::flangerRate);
    else if (kind == Kind::reverb) rate = 0.15f + 0.25f * param (ids::reverbModRate);
    phase += (double) rate / 60.0 * (0.35 + 0.65 * on);
    if (kind == Kind::reverb) ui::approach (level, juce::jlimit (0.0f, 1.0f, processor.getReverbTail() * 2.5f), 0.08f);
    repaint();
}

void FxVisual::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    const float u = ui::editorScale (*this);
    g.setColour (juce::Colours::black.withAlpha (0.28f));
    g.fillRoundedRectangle (r, 10.0f * u);
    g.setColour (ui::line);
    g.drawRoundedRectangle (r.reduced (0.5f), 10.0f * u, std::max (1.0f, u));
    const float a = 0.3f + 0.7f * on;
    juce::Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (r.reduced (1.0f).toNearestInt());
    const float tw = juce::MathConstants<float>::twoPi;
    const float ph = (float) std::fmod (phase, 1000.0) * tw;

    if (kind == Kind::chorus)
    {
        // trois voix qui ondulent doucement les unes autour des autres
        const float depth = param (ids::chorusDepth) * 0.01f;
        for (int v = 0; v < 3; ++v)
        {
            juce::Path p;
            for (int i = 0; i <= 80; ++i)
            {
                const float x = r.getX() + r.getWidth() * (float) i / 80.0f;
                const float t = (float) i / 80.0f;
                const float y = r.getCentreY() + r.getHeight() * 0.3f * (0.25f + 0.75f * depth)
                              * std::sin (t * tw * 1.5f + ph + (float) v * 2.1f) * std::sin (t * 3.14159f);
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            const auto c = (v == 0 ? ui::ember : (v == 1 ? ui::bone : ui::haze)).withAlpha (a * (v == 1 ? 0.8f : 0.6f));
            ui::strokeGlow (g, p, c, 1.2f * u, 0.4f * on);
            g.setColour (c);
            g.strokePath (p, juce::PathStrokeType (std::max (1.0f, 1.6f * u), juce::PathStrokeType::curved));
        }
    }
    else if (kind == Kind::flanger)
    {
        // peigne : les creux glissent avec le balayage
        const float depth = param (ids::flangerDepth) * 0.01f, fb = std::abs (param (ids::flangerFeedback)) * 0.01f;
        const float spacing = 0.05f + 0.07f * (0.5f + 0.5f * std::sin (ph)) * (0.3f + 0.7f * depth);
        juce::Path p;
        for (int i = 0; i <= 160; ++i)
        {
            const float t = (float) i / 160.0f;
            const float comb = 0.5f + 0.5f * std::cos (t / spacing * tw);
            const float y = r.getBottom() - 6.0f * u - (r.getHeight() - 14.0f * u) * std::pow (comb, 1.0f + 3.0f * fb);
            if (i == 0) p.startNewSubPath (r.getX(), y); else p.lineTo (r.getX() + r.getWidth() * t, y);
        }
        juce::Path fill (p);
        fill.lineTo (r.getRight(), r.getBottom());
        fill.lineTo (r.getX(), r.getBottom());
        fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient (ui::ember.withAlpha (0.28f * a), 0.0f, r.getY(), ui::ember.withAlpha (0.0f), 0.0f, r.getBottom(), false));
        g.fillPath (fill);
        g.setColour (ui::ember.withAlpha (a));
        g.strokePath (p, juce::PathStrokeType (std::max (1.0f, 1.5f * u), juce::PathStrokeType::curved));
    }
    else if (kind == Kind::delay)
    {
        // échos : une barre par répétition, à sa place dans le temps (2 s sur la largeur), qui s'éteint avec la réinjection
        const bool sync = param (ids::delaySync) > 0.5f, ping = param (ids::delayPingPong) > 0.5f;
        const float timeS = sync ? (float) (60.0 / 120.0 * delayNoteBeats (juce::roundToInt (param (ids::delayNote)))) : param (ids::delayTime) * 0.001f;
        const float fb = param (ids::delayFeedback) * 0.01f;
        const float pulse = (float) std::fmod (phase * 0.6, 1.0);
        float amp = 1.0f;
        for (int k = 0; k < 24 && amp > 0.03f; ++k)
        {
            const float x = r.getX() + 8.0f * u + (r.getWidth() - 16.0f * u) * std::min (1.0f, timeS * (float) k / 2.0f);
            if (x > r.getRight() - 4.0f * u) break;
            const float hgt = (r.getHeight() - 12.0f * u) * amp;
            const bool right = ping && (k & 1);
            const auto bar = juce::Rectangle<float> (x - 2.5f * u, right ? r.getY() + 6.0f * u : r.getBottom() - 6.0f * u - hgt * (ping ? 0.5f : 1.0f),
                                                     5.0f * u, hgt * (ping ? 0.5f : 1.0f));
            const float lit = std::exp (-12.0f * std::abs (pulse * 2.0f / std::max (0.05f, timeS) - (float) k) / 8.0f);
            g.setColour ((k == 0 ? ui::bone : ui::ember).withAlpha (a * (0.45f + 0.55f * lit)));
            g.fillRoundedRectangle (bar, 2.5f * u);
            amp = k == 0 ? 0.85f : amp * fb;
        }
    }
    else
    {
        // rubans de la reverb : leur ampleur suit la queue réelle (et l'allure : la taille, la décroissance, le mode)
        const float size = param (ids::reverbSize) * 0.01f, decay = juce::jlimit (0.0f, 1.0f, std::log (param (ids::reverbDecay) / 0.2f) / std::log (100.0f));
        const int mode = juce::roundToInt (param (ids::reverbMode));
        const float amp = r.getHeight() * (0.16f + 0.26f * juce::jlimit (0.0f, 1.0f, 0.3f * on + 0.9f * level));
        const int lines = 14;
        for (int l = 0; l < lines; ++l)
        {
            const float k = (float) l / (float) (lines - 1);
            juce::Path p;
            for (int i = 0; i <= 90; ++i)
            {
                const float t = (float) i / 90.0f;
                const float env = std::sin (t * 3.14159f) * (0.6f + 0.4f * std::sin (t * 3.14159f * 2.0f + ph * 0.3f));
                float y = std::sin (t * tw * (1.2f + 1.6f * (1.0f - size)) + ph + k * 1.3f) * 0.6f
                        + std::sin (t * tw * (2.7f + 2.0f * decay) - ph * 0.7f + k * 2.1f) * 0.3f;
                if (mode == 4) y += 0.18f * std::sin (t * tw * 9.0f + ph * 2.5f + k * 4.0f);   // SHIMMER : scintillement
                y = r.getCentreY() + (k - 0.5f) * r.getHeight() * 0.10f * env + amp * env * y * (0.55f + 0.45f * (1.0f - std::abs (k - 0.5f) * 2.0f));
                if (i == 0) p.startNewSubPath (r.getX(), y); else p.lineTo (r.getX() + r.getWidth() * t, y);
            }
            const auto c = ui::ember.interpolatedWith (ui::bone, 0.25f + 0.6f * (1.0f - std::abs (k - 0.5f) * 2.0f));
            g.setColour (c.withAlpha (a * (0.25f + 0.5f * (1.0f - std::abs (k - 0.5f) * 2.0f))));
            g.strokePath (p, juce::PathStrokeType (std::max (1.0f, 1.2f * u), juce::PathStrokeType::curved));
        }
    }
}

//==============================================================================
FxView::FxView (OniriqueVocalBoxAudioProcessor& p, juce::LookAndFeel& lnf)
    : processor (p),
      delayNote (p.apvts, ids::delayNote, "Delay length in notes, locked to your session tempo (drag up/down)."),
      reverbMode (p.apvts, ids::reverbMode, { "ROOM", "PLATE 1", "PLATE 2", "HALL", "SHIMMER" },
                  { "Room: short and close, natural early reflections. Puts the voice in a real space without washing it.",
                    "Plate 1: the classic bright vocal plate, dense and smooth, no early reflections.",
                    "Plate 2: a darker, wider plate with more movement: lush ballads, R&B.",
                    "Hall: big, open space with early reflections and a long, soft tail.",
                    "Shimmer: an octave above, fed back into the tail: airy, ethereal pads behind the voice." }),
      reverbMix (p.apvts, ids::reverbMix, "SEND", "0", "100", KnobComponent::Layout::textBelow,
                 "Level of the reverb return. The reverb is an aux track: the voice always stays at 100 %, the reverb is added on top "
                 "(raise it as much as you like, the voice never drowns; DUCKING clears it under the words). Double-click to reset.", lnf)
{
    setWantsKeyboardFocus (true);
    setOpaque (true);

    addAndMakeVisible (exitButton);
    exitButton.setText ("EXIT FX");
    exitButton.setTooltip ("Back to the plugin (Esc).");
    exitButton.onClick = [this] { if (onClose) onClose(); };
    children.push_back (&exitButton);

    struct PowerDef { PowerButton& b; const char* id; const char* name; };
    for (auto& d : { PowerDef { chorusPower, ids::chorusOn, "chorus" }, PowerDef { flangerPower, ids::flangerOn, "flanger" },
                     PowerDef { delayPower, ids::delayOn, "delay" }, PowerDef { reverbPower, ids::reverbOn, "reverb" } })
    {
        addAndMakeVisible (d.b);
        d.b.setTooltip (juce::String ("Turn the ") + d.name + " on/off (click-free).");
        attachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, d.id, d.b));
        children.push_back (&d.b);
    }
    chorusCard  = { "CHORUS", "3 voices  |  auto gain match", {}, &chorusPower };
    flangerCard = { "FLANGER", "jet sweep  |  auto gain match", {}, &flangerPower };
    delayCard   = { "DELAY", "aux track  |  tempo sync  |  ping-pong  |  ducking", {}, &delayPower };
    reverbCard  = { "REVERB", "aux track  |  16-line network  |  5 modes  |  ducking", {}, &reverbPower };

    for (auto* v : { &chorusVis, &flangerVis, &delayVis, &reverbVis })
    {
        addAndMakeVisible (v);
        children.push_back (v);
    }

    chorusRate  = &slider (ids::chorusRate, "RATE", "Speed of the chorus movement.");
    chorusDepth = &slider (ids::chorusDepth, "DEPTH", "How far the voices drift apart: subtle widening up to a lush ensemble.");
    chorusMix   = &slider (ids::chorusMix, "MIX", "Chorus amount. The level stays the same at any setting (auto gain match).");

    flangerRate  = &slider (ids::flangerRate, "RATE", "Speed of the flanger sweep.");
    flangerDepth = &slider (ids::flangerDepth, "DEPTH", "Width of the sweep.");
    flangerFb    = &slider (ids::flangerFeedback, "FEEDBACK", "Resonance of the sweep (negative = hollow, positive = metallic jet).");
    flangerMix   = &slider (ids::flangerMix, "MIX", "Flanger amount. The level stays the same at any setting (auto gain match).");

    addAndMakeVisible (delaySync);
    addAndMakeVisible (delayPing);
    addAndMakeVisible (delayNote);
    for (auto* a : std::initializer_list<Animated*> { &delaySync, &delayPing, &delayNote }) children.push_back (a);
    delaySync.setTooltip ("Sync: delay length in notes, locked to your session tempo. Off: free time in ms.");
    delayPing.setTooltip ("Ping-pong: the echoes bounce left / right.");
    attachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, ids::delaySync, delaySync));
    attachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, ids::delayPingPong, delayPing));
    delayNote.setPrefix ("NOTE ");
    delayTime = &slider (ids::delayTime, "TIME", "Delay time in ms (when SYNC is off).");
    delayFb   = &slider (ids::delayFeedback, "FEEDBACK", "How many repeats. They soften a little each time, like tape.");
    delayMix  = &slider (ids::delayMix, "SEND", "Level of the delay return. The delay is an aux track: the voice always stays at 100 %, the echoes are added on top.");
    delayLow  = &slider (ids::delayLowCut, "LOW CUT", "Removes the low end of the repeats: clean echoes that don't muddy the voice.");
    delayHigh = &slider (ids::delayHighCut, "HIGH CUT", "Darkens the repeats so they sit behind the voice.");
    delayDuck = &slider (ids::delayDucking, "DUCKING", "The echoes step back while you sing and come back in the gaps.");

    addAndMakeVisible (reverbMode);
    addAndMakeVisible (reverbMix);
    children.push_back (&reverbMode);
    children.push_back (&reverbMix);
    revPre      = &slider (ids::reverbPreDelay, "PRE-DELAY", "Gap before the reverb starts: keeps the words clear in front of the tail.");
    revDecay    = &slider (ids::reverbDecay, "DECAY", "Length of the tail.");
    revSize     = &slider (ids::reverbSize, "SIZE", "Size of the space.");
    revDiff     = &slider (ids::reverbDiffusion, "DIFFUSION", "Density of the tail: low = distinct reflections, high = smooth wash.");
    revModRate  = &slider (ids::reverbModRate, "MOD RATE", "Speed of the movement inside the tail (keeps it from ringing).");
    revModDepth = &slider (ids::reverbModDepth, "MOD DEPTH", "Amount of movement inside the tail: more = lusher, chorus-like.");
    revLow      = &slider (ids::reverbLowCut, "LOW CUT", "Removes the low end of the reverb: no mud under the voice.");
    revHigh     = &slider (ids::reverbHighCut, "HIGH CUT", "Darkens the tail so it doesn't fight the sibilance.");
    revDuck     = &slider (ids::reverbDucking, "DUCKING", "The reverb steps back while you sing and blooms in the gaps.");
}

FxView::~FxView() = default;

PillSlider& FxView::slider (const char* id, const juce::String& name, const juce::String& tip)
{
    sliders.push_back (std::make_unique<PillSlider> (processor.apvts, id, name, tip));
    auto& s = *sliders.back();
    addAndMakeVisible (s);
    children.push_back (&s);
    return s;
}

void FxView::setShown (bool shown)
{
    motion.show (*this, shown);
    if (shown) grabKeyboardFocus();
}

void FxView::tick()
{
    if (motion.tick (*this) || ! isVisible()) return;   // en transition : le calque seul s'anime
    for (auto* a : children) a->tick();
    // SYNC : la note remplace le temps en ms
    const bool sync = processor.apvts.getRawParameterValue (ids::delaySync)->load() > 0.5f;
    delayNote.setVisible (sync);
    delayTime->setVisible (! sync);
}

bool FxView::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey) { if (onClose) onClose(); return true; }
    return false;
}

void FxView::resized()
{
    const float u = ui::editorScale (*this);
    auto r = getLocalBounds().toFloat().reduced (16.0f * u, 12.0f * u);
    titleArea = r.removeFromTop (40.0f * u);
    exitButton.setBounds (juce::Rectangle<float> (170.0f * u, 32.0f * u).withRightX (titleArea.getRight()).withY (titleArea.getCentreY() - 16.0f * u).toNearestInt());
    r.removeFromTop (10.0f * u);

    auto top = r.removeFromTop (300.0f * u);
    r.removeFromTop (14.0f * u);
    const float gap = 14.0f * u, cw = (top.getWidth() - 2.0f * gap) / 3.0f;
    chorusCard.bounds  = top.removeFromLeft (cw); top.removeFromLeft (gap);
    flangerCard.bounds = top.removeFromLeft (cw); top.removeFromLeft (gap);
    delayCard.bounds   = top;
    reverbCard.bounds  = r;

    auto body = [u] (juce::Rectangle<float> b) { return b.withTrimmedTop (44.0f * u).reduced (16.0f * u, 0.0f).withTrimmedBottom (14.0f * u); };
    for (auto* c : { &chorusCard, &flangerCard, &delayCard, &reverbCard })
        c->power->setBounds (juce::Rectangle<float> (32.0f * u, 32.0f * u).withCentre ({ c->bounds.getRight() - 24.0f * u, c->bounds.getY() + 22.0f * u }).toNearestInt());

    const float rowH = 32.0f * u, rowGap = 9.0f * u;
    auto stack = [&] (juce::Rectangle<float>& area, std::initializer_list<juce::Component*> comps)
    {
        for (auto* c : comps) { c->setBounds (area.removeFromTop (rowH).toNearestInt()); area.removeFromTop (rowGap); }
    };
    {
        auto b = body (chorusCard.bounds);
        auto vis = b.removeFromTop (b.getHeight() - 3.0f * (rowH + rowGap));
        chorusVis.setBounds (vis.withTrimmedBottom (rowGap).toNearestInt());
        stack (b, { chorusRate, chorusDepth, chorusMix });
    }
    {
        auto b = body (flangerCard.bounds);
        auto vis = b.removeFromTop (b.getHeight() - 4.0f * (rowH + rowGap));
        flangerVis.setBounds (vis.withTrimmedBottom (rowGap).toNearestInt());
        stack (b, { flangerRate, flangerDepth, flangerFb, flangerMix });
    }
    {
        auto b = body (delayCard.bounds);
        auto vis = b.removeFromTop (b.getHeight() - 4.0f * (rowH + rowGap));
        delayVis.setBounds (vis.withTrimmedBottom (rowGap).toNearestInt());
        auto row = b.removeFromTop (rowH);
        delaySync.setBounds (row.removeFromLeft (row.getWidth() * 0.22f).toNearestInt());
        row.removeFromLeft (8.0f * u);
        auto pingR = row.removeFromRight (row.getWidth() * 0.36f);
        row.removeFromRight (8.0f * u);
        delayNote.setBounds (row.toNearestInt());
        delayTime->setBounds (row.toNearestInt());
        delayPing.setBounds (pingR.toNearestInt());
        b.removeFromTop (rowGap);
        stack (b, { delayFb, delayMix });
        auto cuts = b.removeFromTop (rowH);
        const float third = (cuts.getWidth() - 16.0f * u) / 3.0f;
        delayLow->setBounds (cuts.removeFromLeft (third).toNearestInt());
        cuts.removeFromLeft (8.0f * u);
        delayHigh->setBounds (cuts.removeFromLeft (third).toNearestInt());
        cuts.removeFromLeft (8.0f * u);
        delayDuck->setBounds (cuts.toNearestInt());
    }
    {
        // REVERB : modes, visuel, puis réglages de part et d'autre du MIX, puis coupe-bas / DUCKING / coupe-haut
        auto b = body (reverbCard.bounds);
        reverbMode.setBounds (b.removeFromTop (34.0f * u).withSizeKeepingCentre (std::min (b.getWidth(), 620.0f * u), 34.0f * u).toNearestInt());
        b.removeFromTop (12.0f * u);
        auto bottomRow = b.removeFromBottom (rowH + 4.0f * u);
        b.removeFromBottom (14.0f * u);
        auto controls = b.removeFromBottom (3.0f * rowH + 2.0f * 14.0f * u + 30.0f * u);
        b.removeFromBottom (14.0f * u);
        reverbVis.setBounds (b.toNearestInt());

        const float colW = std::min (520.0f * u, (controls.getWidth() - 260.0f * u) * 0.5f);
        auto left = controls.removeFromLeft (colW).withSizeKeepingCentre (colW, 3.0f * rowH + 2.0f * 14.0f * u);
        auto right = controls.removeFromRight (colW).withSizeKeepingCentre (colW, 3.0f * rowH + 2.0f * 14.0f * u);
        for (auto* s : { revPre, revDecay, revSize }) { s->setBounds (left.removeFromTop (rowH).toNearestInt()); left.removeFromTop (14.0f * u); }
        for (auto* s : { revDiff, revModRate, revModDepth }) { s->setBounds (right.removeFromTop (rowH).toNearestInt()); right.removeFromTop (14.0f * u); }
        const float D = std::min (controls.getHeight() / 1.6f, 150.0f * u);
        reverbMix.setMaxDiameter (D);
        reverbMix.setBounds (controls.withSizeKeepingCentre (std::min (controls.getWidth(), D * 1.6f), controls.getHeight()).toNearestInt());

        const float bw = (bottomRow.getWidth() - 2.0f * 24.0f * u) / 3.0f;
        revLow->setBounds (bottomRow.removeFromLeft (bw).withSizeKeepingCentre (bw, rowH).toNearestInt());
        bottomRow.removeFromLeft (24.0f * u);
        revDuck->setBounds (bottomRow.removeFromLeft (bw).withSizeKeepingCentre (bw, rowH).toNearestInt());
        bottomRow.removeFromLeft (24.0f * u);
        revHigh->setBounds (bottomRow.withSizeKeepingCentre (bw, rowH).toNearestInt());
    }
}

void FxView::paintCard (juce::Graphics& g, const Card& c) const
{
    const float u = ui::editorScale (*this);
    const auto r = c.bounds;
    const float rad = 14.0f * u;
    g.setGradientFill (juce::ColourGradient (ui::deep2.withAlpha (0.95f), 0.0f, r.getY(), ui::deep.withAlpha (0.97f), 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle (r, rad);
    g.setColour (ui::line);
    g.drawRoundedRectangle (r.reduced (0.5f), rad, std::max (1.0f, u));
    g.setColour (juce::Colours::white.withAlpha (0.045f));
    g.drawHorizontalLine (juce::roundToInt (r.getY() + 1.5f * u), r.getX() + rad, r.getRight() - rad);

    const bool on = c.power != nullptr && c.power->getToggleState();
    const float dot = 7.0f * u;
    const auto dr = juce::Rectangle<float> (dot, dot).withCentre ({ r.getX() + 22.0f * u, r.getY() + 22.0f * u });
    g.setColour (ui::ember.withAlpha (on ? 0.35f : 0.0f));
    g.fillEllipse (dr.expanded (dot * 0.7f));
    g.setColour (on ? ui::ember : ui::haze.withAlpha (0.5f));
    g.fillEllipse (dr);
    const auto f = ui::font (ui::Face::display, 17.0f * u, true, 0.08f);
    const float tw = ui::textWidth (f, c.title) + 12.0f * u;
    g.setFont (f);
    g.setColour (ui::mist);
    g.drawText (c.title, juce::Rectangle<float> (r.getX() + 36.0f * u, r.getY() + 8.0f * u, tw, 28.0f * u), juce::Justification::centredLeft, false);
    g.setColour (ui::haze.withAlpha (0.85f));
    g.setFont (ui::fitFont (ui::Face::mono, 11.0f * u, r.getWidth() - tw - 110.0f * u, c.subtitle, false, 0.06f));
    g.drawText (c.subtitle, juce::Rectangle<float> (r.getX() + 36.0f * u + tw, r.getY() + 8.0f * u, r.getWidth() - tw - 110.0f * u, 28.0f * u),
                juce::Justification::centredLeft, false);
}

void FxView::paint (juce::Graphics& g)
{
    const float u = ui::editorScale (*this);
    const auto r = getLocalBounds().toFloat();
    g.fillAll (ui::night);   // opaque : rien du plugin n'est redessiné derrière la page
    g.setGradientFill (juce::ColourGradient (ui::deep2, 0.0f, r.getY(), ui::night, 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle (r, 16.0f * u);
    g.setGradientFill (juce::ColourGradient (ui::ember.withAlpha (0.10f), r.getRight(), r.getY(), ui::ember.withAlpha (0.0f),
                                             r.getRight() - r.getWidth() * 0.45f, r.getY() + r.getHeight() * 0.5f, true));
    g.fillRoundedRectangle (r, 16.0f * u);
    g.setColour (ui::line.interpolatedWith (ui::ember, 0.25f));
    g.drawRoundedRectangle (r.reduced (0.5f), 16.0f * u, std::max (1.0f, u));

    {
        const auto f = ui::font (ui::Face::display, 22.0f * u, true, 0.08f);
        const float tw = ui::textWidth (f, "FX") + 14.0f * u;
        g.setFont (f);
        g.setColour (ui::mist);
        g.drawText ("FX", titleArea.withX (titleArea.getX() + 8.0f * u).withWidth (tw), juce::Justification::centredLeft, false);
        g.setColour (ui::haze);
        const juce::String h = "Vocal effects at the end of the chain: chorus > flanger on the voice, then DELAY and REVERB as aux tracks (the voice stays at 100 %).  Esc = back";
        const float hx = titleArea.getX() + 8.0f * u + tw + 10.0f * u;
        g.setFont (ui::fitFont (ui::Face::body, 13.0f * u, (float) exitButton.getX() - 16.0f * u - hx, h, false, 0.03f));
        g.drawText (h, juce::Rectangle<float> (hx, titleArea.getY(), (float) exitButton.getX() - 16.0f * u - hx, titleArea.getHeight()),
                    juce::Justification::centredLeft, false);
    }
    for (auto* c : { &chorusCard, &flangerCard, &delayCard, &reverbCard }) paintCard (g, *c);
}
