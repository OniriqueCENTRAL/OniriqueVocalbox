#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"

namespace onirique
{
/*  Presets internes du module MULTIBAND : des bases de départ pour la voix.
    Ils ne touchent que les réglages du multibande (coupures, seuils, gains, attaque, relâchement) ;
    le nom choisi est rangé dans l'état du plugin ("mbPreset").
*/
struct MultibandPreset
{
    const char* name;
    const char* description;
    float x[3], thr[4], gain[4], attack, release;
};

inline const std::vector<MultibandPreset>& multibandPresets()
{
    static const std::vector<MultibandPreset> p {
        { "Init",          "Neutral starting point.",
          { 200, 1500, 6000 },  { -20, -20, -20, -20 }, { 0, 0, 0, 0 },       15, 200 },
        { "Vocal Smooth",  "Even, creamy levelling across the whole voice.",
          { 180, 1500, 6000 },  { -24, -22, -24, -26 }, { 0, 0, 0, 0.5f },    20, 250 },
        { "Vocal Glue",    "Gentle glue: the voice sits in the mix without losing life.",
          { 200, 2000, 7000 },  { -20, -20, -20, -20 }, { 0, 0, 0, 0 },       30, 300 },
        { "De-Harsh",      "Tames harsh presence and hiss only when they jump out.",
          { 250, 2500, 5500 },  { 0, 0, -28, -18 },     { 0, 0, 0, 0 },       8,  120 },
        { "Warm Body",     "Controls boom and boxiness, keeps the chest warm.",
          { 150, 900, 5000 },   { -18, -22, 0, 0 },     { 1, 0.5f, 0, 0 },    25, 300 },
        { "Air Control",   "Lets you push the air while keeping S and breaths in check.",
          { 200, 1500, 8000 },  { 0, 0, -14, -26 },     { 0, 0, 0, 1.5f },    10, 150 },
        { "Rap Punch",     "Tight, upfront and dense, fast but never grainy.",
          { 120, 1200, 5000 },  { -22, -18, -20, -22 }, { 1, 0, 1, 1 },       12, 140 },
        { "Broadcast",     "Dense and steady, radio-style level.",
          { 160, 1600, 6500 },  { -26, -26, -26, -26 }, { 1, 1, 1, 1 },       15, 220 },
    };
    return p;
}

inline void applyMultibandPreset (juce::AudioProcessorValueTreeState& apvts, const MultibandPreset& p)
{
    auto set = [&] (const char* id, float v)
    {
        if (auto* prm = apvts.getParameter (id))
        {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (prm->convertTo0to1 (v));
            prm->endChangeGesture();
        }
    };
    for (int i = 0; i < 3; ++i) set (ids::mbX[i], p.x[i]);
    for (int b = 0; b < 4; ++b) { set (ids::mbThr[b], p.thr[b]); set (ids::mbGain[b], p.gain[b]); }
    set (ids::mbAttack, p.attack);
    set (ids::mbRelease, p.release);
    apvts.state.setProperty ("mbPreset", p.name, nullptr);
}
} // namespace onirique
