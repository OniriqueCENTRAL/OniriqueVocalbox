#include "PluginEditor.h"

using namespace onirique;

/*  Page FX : les effets de voix (fin de chaîne), en plein écran dans la fenêtre du plugin, dans l'esprit des rangées de
    VOCAL MINI : trois bandes (FX, DELAY, REVERB), le titre à gauche, des potards. Rien n'est caché : chaque réglage est
    sous la main.
*/

//==============================================================================
void FxVisual::tick()
{
    if (! isShowing()) return;
    static const char* onIds[] { ids::chorusOn, ids::flangerOn, ids::delayOn, ids::reverbOn, ids::doublerOn, ids::phoneOn };
    const bool isOn = param (onIds[(int) kind]) > 0.5f;
    ui::approach (on, isOn ? 1.0f : 0.0f, 0.12f);
    float rate = 0.5f;
    if (kind == Kind::chorus) rate = param (ids::chorusRate);
    else if (kind == Kind::flanger) rate = param (ids::flangerRate);
    else if (kind == Kind::reverb) rate = 0.15f + 0.25f * param (ids::reverbModRate);
    else if (kind == Kind::doubler) rate = 0.3f;
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
        // couloir en perspective : un sol quadrillé qui file vers l'horizon, chaque écho y est une onde lumineuse posée à
        // sa distance (2 s jusqu'à l'horizon), plus petite et plus pâle à chaque répétition (FEEDBACK). En PING-PONG les
        // échos alternent à gauche et à droite. Une impulsion part de devant et allume chaque écho en passant.
        // Le décor fixe (lueur, halo, lignes de fuite) est dessiné une fois dans une image ; à chaque image, seuls les
        // traverses, les ondes et les faisceaux (un seul dégradé pour tous) sont redessinés.
        const bool sync = param (ids::delaySync) > 0.5f, ping = param (ids::delayPingPong) > 0.5f;
        const float timeS = std::max (0.03f, sync ? (float) (60.0 / 120.0 * delayNoteBeats (juce::roundToInt (param (ids::delayNote))))
                                                  : param (ids::delayTime) * 0.001f);
        const float fb = param (ids::delayFeedback) * 0.01f;
        const float bright = juce::jlimit (0.0f, 1.0f, (param (ids::delayHighCut) - 2000.0f) / 14000.0f);   // HIGH CUT : échos plus clairs
        const float cx = r.getCentreX(), horizon = r.getY() + r.getHeight() * 0.16f, floorY = r.getBottom() - 4.0f * u;
        const float halfW = r.getWidth() * 0.62f;
        auto depth = [] (float z) { return 1.0f / (1.0f + 5.0f * z); };            // échelle à la distance z (0 devant, 1 au fond)
        auto at = [&] (float lateral, float z) { const float s = depth (z); return juce::Point<float> (cx + lateral * halfW * s, horizon + (floorY - horizon) * s); };

        {
            const float ps = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
            const int w = juce::jmax (1, juce::roundToInt (r.getWidth() * ps)), h = juce::jmax (1, juce::roundToInt (r.getHeight() * ps));
            if (! backdrop.isValid() || backdrop.getWidth() != w || backdrop.getHeight() != h)
            {
                backdrop = juce::Image (juce::Image::ARGB, w, h, true);
                juce::Graphics bg (backdrop);
                bg.addTransform (juce::AffineTransform::translation (-r.getX(), -r.getY()).scaled (ps));
                juce::ColourGradient sky (ui::blood.withAlpha (0.10f), cx, r.getY(), ui::blood.withAlpha (0.18f), cx, r.getBottom(), false);
                sky.addColour ((horizon - r.getY()) / r.getHeight(), ui::ember.withAlpha (0.20f));
                bg.setGradientFill (sky);
                bg.fillRect (r);
                bg.setGradientFill (juce::ColourGradient (ui::ember.withAlpha (0.22f), cx, horizon, ui::ember.withAlpha (0.0f), cx + r.getWidth() * 0.45f, horizon, true));
                bg.fillRect (r);
                for (int i = -4; i <= 4; ++i)
                {
                    const float lat = (float) i / 4.0f;
                    bg.setColour (ui::ember.withAlpha (0.05f + 0.07f * (1.0f - std::abs (lat))));
                    bg.drawLine (juce::Line<float> (at (lat, 0.0f), at (lat, 1.0f)), std::max (1.0f, 0.8f * u));
                }
            }
            g.setOpacity (a);
            g.drawImage (backdrop, r, juce::RectanglePlacement::stretchToFit);
            g.setOpacity (1.0f);
        }
        // les traverses avancent doucement vers nous
        const float scroll = (float) std::fmod (phase * 0.35, 1.0);
        for (int j = 0; j < 10; ++j)
        {
            const float z = std::fmod (((float) j + 1.0f - scroll) / 10.0f, 1.0f);
            const auto l = at (-1.0f, z), rr = at (1.0f, z);
            g.setColour (ui::ember.withAlpha (a * 0.12f * (1.0f - z)));
            g.drawLine (l.x, l.y, rr.x, rr.y, std::max (1.0f, 0.8f * u));
        }

        // les échos, du plus lointain au plus proche (les proches passent devant)
        const float travel = (float) std::fmod (phase * 0.5, 1.0) * 2.0f;          // l'impulsion : 0..2 s
        struct Echo { float z, amp, lat; int k; };
        Echo echoes[24];
        int count = 0;
        float amp = 1.0f;
        for (int k = 0; k < 24 && amp > 0.03f; ++k)
        {
            const float t = timeS * (float) k;
            if (t > 2.0f) break;
            echoes[count++] = { 0.10f + 0.90f * t / 2.0f, amp, ping && k > 0 ? ((k & 1) ? -0.45f : 0.45f) : 0.0f, k };
            amp = k == 0 ? 0.85f : amp * fb;
        }
        juce::Path beams;   // tous les faisceaux : un seul dégradé, du sol vers l'horizon
        for (int i = count - 1; i >= 0; --i)
        {
            const auto& e = echoes[i];
            const float s = depth (e.z);
            const auto c = at (e.lat, e.z);
            const float lit = std::exp (-std::abs (travel - e.z * 2.0f) * 9.0f);   // l'impulsion passe sur cet écho
            const float alpha = a * e.amp * (0.45f + 0.55f * lit);
            const float rw = halfW * 0.20f * s * (0.6f + 0.4f * e.amp), rh = rw * 0.28f;
            const auto col = (e.k == 0 ? ui::bone : ui::ember.interpolatedWith (ui::lilac, 0.5f * bright));
            g.setColour (col.withAlpha (alpha * 0.18f));
            g.fillEllipse (juce::Rectangle<float> (rw * 2.6f, rh * 2.6f).withCentre (c));
            g.setColour (col.withAlpha (alpha));
            g.drawEllipse (juce::Rectangle<float> (rw * 2.0f, rh * 2.0f).withCentre (c), std::max (1.0f, 2.2f * u * s + 0.6f * u));
            const float beamH = (floorY - horizon) * 0.95f * s * e.amp;
            beams.addRectangle (juce::Rectangle<float> (std::max (1.5f * u, 4.0f * u * s), beamH).withCentre ({ c.x, c.y - beamH * 0.5f }));
        }
        g.setGradientFill (juce::ColourGradient (ui::ember.interpolatedWith (ui::lilac, 0.4f).withAlpha (0.75f * a), cx, floorY,
                                                 ui::ember.withAlpha (0.0f), cx, horizon, false));
        g.fillPath (beams);
    }
    else if (kind == Kind::doubler)
    {
        // la voix au centre (blanche), ses deux doubles de part et d'autre (rouges), un peu décalés, qui dérivent
        const float amt = param (ids::doublerAmount) * 0.01f;
        for (int v = 0; v < 3; ++v)
        {
            const float side = v == 0 ? 0.0f : (v == 1 ? -1.0f : 1.0f);
            juce::Path p;
            for (int i = 0; i <= 80; ++i)
            {
                const float t = (float) i / 80.0f;
                const float drift = side * (0.02f + 0.03f * amt) * std::sin (ph * (v == 1 ? 0.9f : 1.3f) + (float) v);
                const float env = std::sin (t * 3.14159f);
                const float y = r.getCentreY() + side * r.getHeight() * 0.22f * (0.4f + 0.6f * amt)
                              + r.getHeight() * 0.16f * env * std::sin ((t + drift) * tw * 3.0f);
                if (i == 0) p.startNewSubPath (r.getX() + 6.0f * u, y); else p.lineTo (r.getX() + 6.0f * u + (r.getWidth() - 12.0f * u) * t, y);
            }
            const auto c = (v == 0 ? ui::bone : ui::ember).withAlpha (a * (v == 0 ? 0.9f : 0.35f + 0.5f * amt));
            if (v > 0) ui::strokeGlow (g, p, ui::ember, 1.2f * u, 0.35f * on * amt);
            g.setColour (c);
            g.strokePath (p, juce::PathStrokeType (std::max (1.0f, (v == 0 ? 1.8f : 1.4f) * u), juce::PathStrokeType::curved));
        }
    }
    else if (kind == Kind::phone)
    {
        // la bande qui reste : elle se resserre autour de 1.7 kHz quand le bouton monte (axe log 50 Hz - 20 kHz)
        const float amt = param (ids::phoneAmount) * 0.01f * on;
        const float lo = 120.0f + 400.0f * amt, hi = 9000.0f - 6000.0f * amt;
        auto xOf = [&] (float hz) { return r.getX() + r.getWidth() * std::log (hz / 50.0f) / std::log (400.0f); };
        juce::Path p;
        for (int i = 0; i <= 120; ++i)
        {
            const float t = (float) i / 120.0f, hz = 50.0f * std::pow (400.0f, t);
            const float hp = 1.0f / (1.0f + std::pow (lo / hz, 4.0f)), lp = 1.0f / (1.0f + std::pow (hz / hi, 4.0f));
            const float bump = 1.0f + 0.6f * amt * std::exp (-std::pow (std::log (hz / 1700.0f) * 2.2f, 2.0f));
            const float y = r.getBottom() - 6.0f * u - (r.getHeight() - 16.0f * u) * 0.75f * hp * lp * bump;
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
        g.setColour (ui::haze.withAlpha (0.5f * a));
        for (float hz : { lo, hi }) g.fillRect (juce::Rectangle<float> (xOf (hz) - 0.5f * u, r.getY() + 6.0f * u, std::max (1.0f, u), r.getHeight() - 12.0f * u));
    }
    else
    {
        // l'affichage de VOCAL MINI : la voix (blanc), la queue de reverb entendue (braise) et ce que CLEAN / DUCKING
        // retirent (rose pâle), en barres miroir qui défilent de droite à gauche (~2.7 s, lu dans le moteur de reverb).
        // Les barres d'une même couleur forment un seul tracé, et le passé s'estompe par un seul dégradé : trois remplissages
        // par image au lieu de plusieurs centaines.
        const auto& rev = processor.getReverbEngine();
        const auto area = r.reduced (10.0f * u, 10.0f * u);
        const float cy = area.getCentreY(), half = area.getHeight() * 0.5f;
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.fillRect (juce::Rectangle<float> (area.getX(), cy - 0.5f * u, area.getWidth(), std::max (1.0f, u)));

        const int bars = 120, stride = 4;
        const int H = mini::VoiceReverb::kHistory;
        const int newest = rev.getHistoryPos() - 1;
        const float slotW = area.getWidth() / (float) bars;
        const float sub = (float) ((newest + 1) % stride) / (float) stride;   // décalage fin entre deux barres : défilement fluide
        const float bw = std::max (1.2f, slotW * 0.42f);
        auto h01 = [] (float v) { return v > 1.0e-6f ? std::pow (juce::jlimit (0.0f, 1.0f, (20.0f * std::log10 (v) + 54.0f) / 54.0f), 1.25f) : 0.0f; };
        juce::Path pPre, pWet, pDry;
        auto bar = [&] (juce::Path& p, float x, float v01)
        {
            if (v01 <= 0.002f) return;
            const float hh = std::max (bw * 0.5f, v01 * half);
            p.addRoundedRectangle (juce::Rectangle<float> (bw, 2.0f * hh).withCentre ({ x, cy }), bw * 0.5f);
        };
        for (int i = 0; i < bars; ++i)
        {
            float dry = 0.0f, wet = 0.0f, pre = 0.0f;
            const int last = newest - (bars - 1 - i) * stride - ((newest + 1) % stride);
            for (int s = 0; s < stride; ++s)
            {
                float d, w, p;
                rev.getHistory ((last - s + H) % H, d, w, p);
                dry = std::max (dry, d); wet = std::max (wet, w); pre = std::max (pre, p);
            }
            const float x = area.getX() + ((float) i + 0.5f - sub) * slotW;
            if (x < area.getX() || x > area.getRight()) continue;
            if (pre > wet * 1.15f) bar (pPre, x, h01 (pre));
            bar (pWet, x, h01 (wet));
            bar (pDry, x, h01 (dry) * 0.92f);
        }
        auto fill = [&] (const juce::Path& p, juce::Colour c, float alpha)
        {
            if (p.isEmpty()) return;
            g.setGradientFill (juce::ColourGradient (c.withAlpha (alpha * 0.35f * a), area.getX(), 0.0f, c.withAlpha (alpha * a), area.getRight(), 0.0f, false));
            g.fillPath (p);
        };
        fill (pPre, ui::lilac, 0.16f);                                   // ce que CLEAN / DUCKING retirent
        fill (pWet, ui::ember.interpolatedWith (ui::lilac, 0.25f), 0.75f); // la queue entendue
        fill (pDry, ui::bone, 0.85f);                                    // la voix
    }
}

//==============================================================================
FxView::FxView (OniriqueVocalBoxAudioProcessor& p, juce::LookAndFeel& lookAndFeel)
    : processor (p), lnf (lookAndFeel),
      delayNote (p.apvts, ids::delayNote, "Delay length in notes, locked to your session tempo (drag up/down)."),
      delayTime (p.apvts, ids::delayTime, "Delay time in ms, free (drag up/down)."),
      reverbMode (p.apvts, ids::reverbMode, { "ROOM", "PLATE 1", "PLATE 2", "HALL", "SHIMMER" },
                  { "Room: short and close, natural early reflections. Puts the voice in a real space without washing it.",
                    "Plate 1: the classic bright vocal plate, dense and smooth, no early reflections.",
                    "Plate 2: a darker, wider plate with more movement: lush ballads, R&B.",
                    "Hall: big, open space with early reflections and a long, soft tail.",
                    "Shimmer: an octave above, fed back into the tail: airy, ethereal pads behind the voice." }),
      reverbMix (p.apvts, ids::reverbMix, "SEND", "", "", KnobComponent::Layout::textBelow,
                 "Level of the reverb return. The reverb is an aux track: the voice always stays at 100 %, the reverb is added on top "
                 "(raise it as much as you like, the voice never drowns; DUCKING clears it under the words). Double-click to reset.", lookAndFeel)
{
    setWantsKeyboardFocus (true);
    setOpaque (true);

    addAndMakeVisible (exitButton);
    exitButton.setText ("EXIT FX");
    exitButton.setTooltip ("Back to the plugin (Esc).");
    exitButton.onClick = [this] { if (onClose) onClose(); };
    children.push_back (&exitButton);

    // chaque effet : sa pastille marche / arrêt (fondu sans clic), à son nom
    toggle (doublerOn, ids::doublerOn, "Doubler on/off: a real double take left and right, the lead voice stays in the centre.");
    toggle (phoneOn, ids::phoneOn, "Phone on/off: from a full voice to a telephone.");
    toggle (chorusOn, ids::chorusOn, "Chorus on/off.");
    toggle (flangerOn, ids::flangerOn, "Flanger on/off.");
    toggle (delayOn, ids::delayOn, "Delay on/off (aux track: the voice always stays at 100 %).");
    toggle (reverbOn, ids::reverbOn, "Reverb on/off (aux track: the voice always stays at 100 %).");

    doublerAmt = &knob (ids::doublerAmount, "AMOUNT", "Level of the two doubles (left and right). The lead voice stays in the centre, untouched.");
    phoneAmt   = &knob (ids::phoneAmount, "AMOUNT", "From a full voice to a telephone: the band narrows and the small speaker bites. The level stays the same.");
    chorusRate  = &knob (ids::chorusRate, "RATE", "Speed of the chorus movement.");
    chorusDepth = &knob (ids::chorusDepth, "DEPTH", "How far the voices drift apart: subtle widening up to a lush ensemble.");
    chorusMix   = &knob (ids::chorusMix, "MIX", "Chorus amount. The level stays the same at any setting (auto gain match).");
    flangerRate  = &knob (ids::flangerRate, "RATE", "Speed of the flanger sweep.");
    flangerDepth = &knob (ids::flangerDepth, "DEPTH", "Width of the sweep.");
    flangerFb    = &knob (ids::flangerFeedback, "FEEDBACK", "Resonance of the sweep (negative = hollow, positive = metallic jet).", true);
    flangerMix   = &knob (ids::flangerMix, "MIX", "Flanger amount. The level stays the same at any setting (auto gain match).");

    for (auto* v : { &delayVis, &reverbVis }) { addAndMakeVisible (v); children.push_back (v); }
    toggle (delaySync, ids::delaySync, "Sync: delay length in notes, locked to your session tempo. Off: free time in ms.");
    toggle (delayPing, ids::delayPingPong, "Ping-pong: the echoes bounce left / right.");
    for (auto* d : { &delayNote, &delayTime }) { addAndMakeVisible (d); children.push_back (d); }
    delayNote.setPrefix ("NOTE ");
    delayTime.setPrefix ("TIME ");
    delayFb   = &knob (ids::delayFeedback, "FEEDBACK", "How many repeats. They soften a little each time, like tape.");
    delayMix  = &knob (ids::delayMix, "SEND", "Level of the delay return. The delay is an aux track: the voice always stays at 100 %, the echoes are added on top.");
    delayLow  = &knob (ids::delayLowCut, "LOW CUT", "Removes the low end of the repeats: clean echoes that don't muddy the voice.");
    delayHigh = &knob (ids::delayHighCut, "HIGH CUT", "Darkens the repeats so they sit behind the voice.");
    delayDuck = &knob (ids::delayDucking, "DUCKING", "The echoes step back while you sing and come back in the gaps.");

    addAndMakeVisible (reverbMode);
    addAndMakeVisible (reverbMix);
    children.push_back (&reverbMode);
    children.push_back (&reverbMix);
    revPre      = &knob (ids::reverbPreDelay, "PRE-DELAY", "Gap before the reverb starts: keeps the words clear in front of the tail.");
    revDecay    = &knob (ids::reverbDecay, "DECAY", "Length of the tail.");
    revSize     = &knob (ids::reverbSize, "SIZE", "Size of the space.");
    revDiff     = &knob (ids::reverbDiffusion, "DIFFUSION", "Density of the tail: low = distinct reflections, high = smooth wash.");
    revWidth    = &knob (ids::reverbWidth, "WIDTH", "Stereo width of the tail (it never cancels in mono).");
    revModRate  = &knob (ids::reverbModRate, "MOD RATE", "Speed of the movement inside the tail (keeps it from ringing).");
    revModDepth = &knob (ids::reverbModDepth, "MOD DEPTH", "Amount of movement inside the tail: more = lusher, chorus-like.");
    revLow      = &knob (ids::reverbLowCut, "LOW CUT", "Removes the low end of the reverb: no mud under the voice.");
    revHigh     = &knob (ids::reverbHighCut, "HIGH CUT", "Darkens the tail so it doesn't fight the sibilance.");
    revClean    = &knob (ids::reverbClean, "CLEAN", "Every new phrase starts clean: no sibilance in the reverb, less mud in the tail, and the old tail is cleared when you sing again.");
    revDuck     = &knob (ids::reverbDucking, "DUCKING", "The reverb steps back while you sing and blooms in the gaps.");

    fxBand     = { "FX", "WIDTH & COLOUR", {}, [this] { return on (ids::doublerOn) || on (ids::phoneOn) || on (ids::chorusOn) || on (ids::flangerOn); } };
    delayBand  = { "DELAY", "AUX SEND", {}, [this] { return on (ids::delayOn); } };
    reverbBand = { "REVERB", "AUX SEND", {}, [this] { return on (ids::reverbOn); } };
}

FxView::~FxView() = default;

KnobComponent& FxView::knob (const char* id, const juce::String& name, const juce::String& tip, bool bipolar)
{
    knobs.push_back (std::make_unique<KnobComponent> (processor.apvts, id, name, "", "", KnobComponent::Layout::textBelow,
                                                      tip + " Double-click to reset.", lnf, bipolar));
    auto& k = *knobs.back();
    addAndMakeVisible (k);
    children.push_back (&k);
    return k;
}

PillToggle& FxView::toggle (PillToggle& t, const char* id, const juce::String& tip)
{
    addAndMakeVisible (t);
    t.setTooltip (tip);
    attachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, id, t));
    children.push_back (&t);
    return t;
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
    const bool sync = on (ids::delaySync);
    delayNote.setVisible (sync);
    delayTime.setVisible (! sync);
    // témoins des titres de bande : ne redessine que quand l'un d'eux change
    const bool lit[3] { fxBand.lit(), delayBand.lit(), reverbBand.lit() };
    if (lit[0] != litShown[0] || lit[1] != litShown[1] || lit[2] != litShown[2])
    {
        for (int i = 0; i < 3; ++i) litShown[i] = lit[i];
        repaint();
    }
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

    const float gap = 14.0f * u;
    const float bandH = (r.getHeight() - 2.0f * gap) * 0.29f;
    fxBand.bounds = r.removeFromTop (bandH);
    r.removeFromTop (gap);
    delayBand.bounds = r.removeFromTop (bandH);
    r.removeFromTop (gap);
    reverbBand.bounds = r;
    dividers.clear();

    // le contenu d'une bande : à droite du bloc de titre, comme les rangées de VOCAL MINI
    auto content = [&] (const Band& b) { return b.bounds.withTrimmedLeft (kTitleW * u).reduced (14.0f * u, 14.0f * u); };
    const float pillH = 30.0f * u, pillGap = 10.0f * u, sepW = 24.0f * u;
    // les pastilles marche / arrêt : toutes de la même taille, calées en haut à gauche de leur zone, sur une même ligne
    auto pill = [&] (juce::Component& p, juce::Rectangle<float>& area)
    {
        p.setBounds (area.removeFromTop (pillH).withWidth (std::min (area.getWidth(), 170.0f * u)).toNearestInt());
        area.removeFromTop (pillGap);
    };
    auto divider = [&] (juce::Rectangle<float>& c, float share)
    {
        const auto sep = c.removeFromLeft (sepW);
        dividers.push_back (sep.withSizeKeepingCentre (1.0f, sep.getHeight() * share));
    };
    auto placeKnobs = [&] (juce::Rectangle<float> area, std::initializer_list<KnobComponent*> ks)
    {
        const float w = area.getWidth() / (float) ks.size();
        const float D = std::min ({ 92.0f * u, w - 14.0f * u, (area.getHeight() - 26.0f * u) / 1.47f });
        int i = 0;
        for (auto* k : ks)
        {
            k->setMaxDiameter (D);
            k->setBounds (juce::Rectangle<float> (area.getX() + w * (float) i++, area.getY(), w, area.getHeight()).toNearestInt());
        }
    };

    {
        // FX : DOUBLER (1) | PHONE (1) | CHORUS (3) | FLANGER (4) : une colonne par potard, un filet entre les effets
        auto c = content (fxBand);
        struct G { PillToggle* t; std::initializer_list<KnobComponent*> ks; };
        const G groups[] { { &doublerOn, { doublerAmt } }, { &phoneOn, { phoneAmt } },
                           { &chorusOn, { chorusRate, chorusDepth, chorusMix } }, { &flangerOn, { flangerRate, flangerDepth, flangerFb, flangerMix } } };
        const float colW = (c.getWidth() - 3.0f * sepW) / 9.0f;
        for (int gi = 0; gi < 4; ++gi)
        {
            const auto& g = groups[gi];
            auto col = c.removeFromLeft (colW * (float) g.ks.size());
            if (gi < 3) divider (c, 0.8f);
            pill (*g.t, col);
            placeKnobs (col, g.ks);
        }
    }
    {
        // DELAY : marche / arrêt et échos animés | SYNC, NOTE ou TIME, PING-PONG au-dessus des potards
        auto c = content (delayBand);
        auto left = c.removeFromLeft (c.getWidth() * 0.26f);
        pill (delayOn, left);
        delayVis.setBounds (left.toNearestInt());
        divider (c, 0.8f);
        auto row = c.removeFromTop (pillH);
        const float bw = std::min (170.0f * u, (row.getWidth() - 24.0f * u) / 3.0f);
        auto opts = row;   // SYNC, NOTE ou TIME, PING-PONG : même ligne et même largeur que les pastilles
        delaySync.setBounds (opts.removeFromLeft (bw).toNearestInt());
        opts.removeFromLeft (12.0f * u);
        const auto noteR = opts.removeFromLeft (bw);
        delayNote.setBounds (noteR.toNearestInt());
        delayTime.setBounds (noteR.toNearestInt());
        opts.removeFromLeft (12.0f * u);
        delayPing.setBounds (opts.removeFromLeft (bw).toNearestInt());
        c.removeFromTop (pillGap);
        placeKnobs (c, { delayFb, delayMix, delayLow, delayHigh, delayDuck });
    }
    {
        // REVERB : marche / arrêt, espaces et rubans animés à gauche | deux rangées de potards, SEND en tête
        auto c = content (reverbBand);
        auto left = c.removeFromLeft (c.getWidth() * 0.34f);
        auto head = left.removeFromTop (pillH);
        reverbOn.setBounds (head.removeFromLeft (std::min (head.getWidth() * 0.32f, 170.0f * u)).toNearestInt());
        head.removeFromLeft (12.0f * u);
        reverbMode.setBounds (head.toNearestInt());
        left.removeFromTop (pillGap);
        reverbVis.setBounds (left.toNearestInt());
        divider (c, 0.85f);
        auto top = c.removeFromTop (c.getHeight() * 0.5f);
        placeKnobs (top, { &reverbMix, revPre, revDecay, revSize, revDiff, revWidth });
        placeKnobs (c, { revModRate, revModDepth, revLow, revHigh, revClean, revDuck });
    }
}

// une bande : verre sombre et filet ; à gauche son titre (comme VOCAL MINI : trait braise allumé quand un effet tourne,
// titre, sous-titre), séparé du contenu par un filet
void FxView::paintBand (juce::Graphics& g, const Band& b) const
{
    const float u = ui::editorScale (*this);
    const auto r = b.bounds;
    const float rad = 14.0f * u;
    g.setGradientFill (juce::ColourGradient (ui::deep2.withAlpha (0.95f), 0.0f, r.getY(), ui::deep.withAlpha (0.97f), 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle (r, rad);
    g.setColour (ui::line);
    g.drawRoundedRectangle (r.reduced (0.5f), rad, std::max (1.0f, u));
    g.setColour (juce::Colours::white.withAlpha (0.045f));
    g.drawHorizontalLine (juce::roundToInt (r.getY() + 1.5f * u), r.getX() + rad, r.getRight() - rad);

    const bool lit = b.lit && b.lit();
    const auto block = r.withWidth (kTitleW * u).reduced (20.0f * u, 0.0f);
    const auto bar = juce::Rectangle<float> (3.0f * u, 46.0f * u).withCentre ({ block.getX() + 1.5f * u, r.getCentreY() });
    if (lit) { g.setColour (ui::ember.withAlpha (0.35f)); g.fillRoundedRectangle (bar.expanded (2.5f * u), 3.0f * u); }
    g.setColour (lit ? ui::ember : ui::haze.withAlpha (0.35f));
    g.fillRoundedRectangle (bar, 1.5f * u);
    const auto text = block.withTrimmedLeft (16.0f * u);
    g.setColour (ui::mist);
    g.setFont (ui::fitFont (ui::Face::display, 22.0f * u, text.getWidth(), b.title, true, 0.08f));
    g.drawText (b.title, text.withHeight (28.0f * u).withCentre ({ text.getCentreX(), r.getCentreY() - 10.0f * u }), juce::Justification::centredLeft, false);
    g.setColour (ui::haze);
    g.setFont (ui::fitFont (ui::Face::mono, 11.0f * u, text.getWidth(), b.subtitle, false, 0.08f));
    g.drawText (b.subtitle, text.withHeight (16.0f * u).withCentre ({ text.getCentreX(), r.getCentreY() + 14.0f * u }), juce::Justification::centredLeft, false);
    g.setColour (ui::line);
    g.fillRect (juce::Rectangle<float> (r.getX() + kTitleW * u, r.getY() + 14.0f * u, 1.0f, r.getHeight() - 28.0f * u));
}

void FxView::paint (juce::Graphics& g)
{
    const float u = ui::editorScale (*this);
    const auto r = getLocalBounds().toFloat();
    g.fillAll (ui::night);   // opaque : rien du plugin n'est redessiné derrière la page
    g.setColour (ui::deep.withAlpha (0.94f));   // verre sombre (DA de VOCAL MINI)
    g.fillRoundedRectangle (r, 16.0f * u);
    g.setGradientFill (juce::ColourGradient (ui::blood.withAlpha (0.35f), r.getRight(), r.getY(), ui::ember.withAlpha (0.0f),
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
        const juce::String h = "End of the chain: doubler > phone > chorus > flanger on the voice, then DELAY and REVERB as aux tracks (the voice stays at 100 %).  Esc = back";
        const float hx = titleArea.getX() + 8.0f * u + tw + 10.0f * u;
        g.setFont (ui::fitFont (ui::Face::body, 13.0f * u, (float) exitButton.getX() - 16.0f * u - hx, h, false, 0.03f));
        g.drawText (h, juce::Rectangle<float> (hx, titleArea.getY(), (float) exitButton.getX() - 16.0f * u - hx, titleArea.getHeight()),
                    juce::Justification::centredLeft, false);
    }
    for (auto* b : { &fxBand, &delayBand, &reverbBand }) paintBand (g, *b);
    g.setColour (ui::line);
    for (const auto& d : dividers) g.fillRect (d);
}
