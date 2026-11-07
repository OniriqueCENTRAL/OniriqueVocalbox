// Onirique OptoBox — the sound of the plugin, without JUCE
#pragma once

#include <array>

#include "Halfband.h"

namespace obx
{

constexpr int kMaxChannels = 2;

/** 0 VU on the front panel: a sine whose peaks reach -18 dBFS, which is how
    the hardware's meter is calibrated. Every level of the panel counts from
    here, the threshold's included. */
constexpr float kZeroVu = 0.125892541f;

constexpr float kOutputOffDb = -60.0f;     // where the output knob says "off"
constexpr float kThresholdOffDb = 20.0f;   // and the threshold

enum class Timing { fixed, fixedManual, manual };
enum class Meter { input, compression, output };
enum class Generation { vintage, modern };     // mk I, mk II

/** What the front panel is set to. */
struct Settings
{
    float outputDb = 0.0f;       // make-up gain: kOutputOffDb (off) .. +30
    float thresholdDb = 0.0f;    // where 1 dB of reduction is reached: +20 (off) .. -40
    float ratio = 4.0f;          // 2 .. 10
    float attackMs = 12.0f;      // 0.5 .. 300
    float releaseMs = 700.0f;    // 50 .. 10 000
    Timing timing = Timing::manual;
    Meter meter = Meter::compression;
    bool externalSidechain = false;
    Generation generation = Generation::vintage;
    int lowCut = 0;              // the sidechain's: 0 off, 1 at 80 Hz, 2 at 220 Hz
    float parallel = 1.0f;       // 0 all dry .. 1 all compressed
    bool bypass = false;
};

/** What tells the two generations apart, as numbers.

    The optical cell is what an opto compressor is: a light-dependent resistor
    lit by the sidechain. It lags a little behind the light, recovers in two
    stages (most of the way quickly, the rest slowly), and the longer and
    harder it has been lit, the more of it is in the slow stage: it remembers.
    The older cell of the mk I lags more and remembers more; the knee of its
    curve is wider, its valve and transformers colour more, and its top end
    is a little softer. */
struct Voice
{
    float kneeDb;
    float cellAttackMs, cellReleaseMs;        // the lag, and the quick part of the recovery
    float memoryReleaseMs, memoryShare;       // the slow part, and how much of it a worn cell holds
    float memoryChargeMs, memoryFadeMs;       // how fast the cell wears, and forgets
    float tubeEven, tubeOdd;                  // second and third harmonic of the valve stage
    float transformer;                        // how soon the iron saturates in the lows
    float lowCutHz;                           // the coupling of the stages
    float bumpHz, bumpDb, bumpQ;              // the transformers' low resonance
    float shelfHz, shelfDb;                   // and their top end
};

const Voice& voiceOf (Generation) noexcept;

/** A stereo-linked optical compressor with its valve make-up stage, on one
    or two channels. Distortion and gain changes run at twice the rate below
    70 kHz. */
class Compressor
{
public:
    void prepare (double sampleRate, int numChannels);
    void reset() noexcept;

    /** Takes effect from the next block; the gain and the mix glide there. */
    void setSettings (const Settings&) noexcept;
    const Settings& getSettings() const noexcept { return settings; }

    /** in and out may be the same buffers. sidechain: the external one, and
        numSidechain 0 when the host gives none. */
    void process (const float* const* in, float* const* out, int numSamples,
                  const float* const* sidechain, int numSidechain) noexcept;

    /** The reduction being applied now, in dB. */
    float reductionDb() const noexcept { return reduction; }

    /** The VU needle's deflection: 1 is 0 VU, 1.4125 (+3) the end of the
        scale. It is moved here, sample by sample, as the meter's coil and
        spring would move it. */
    float needle() const noexcept { return needlePos; }

    int oversampling() const noexcept { return factor; }

    /** The static curve: how many dB a steady level (in dB from 0 VU, read at
        its peaks) is brought down by, with the settings as they are. */
    float staticReduction (float levelDb) const noexcept;

    /** The valve stage alone, in VU units (1 is 0 VU): what it does to a level. */
    float tube (float x) const noexcept;

private:
    struct Biquad
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        float z1 = 0.0f, z2 = 0.0f;

        float run (float x) noexcept
        {
            const auto y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }

        void clear() noexcept { z1 = z2 = 0.0f; }
    };

    struct Channel
    {
        Halfband band;
        float inFlux = 0.0f, outFlux = 0.0f;   // the two transformers
        float hpX = 0.0f, hpY = 0.0f;           // the coupling
        Biquad bump, shelf;
        Biquad sidechain;                        // the low cut, on whichever signal feeds the detector
    };

    void updateFilters() noexcept;
    float colour (Channel&, float x, float gain) noexcept;
    float cell (float target) noexcept;

    Settings settings;
    const Voice* voice = &voiceOf (Generation::vintage);
    Generation filtersFor = Generation::vintage;
    int lowCutFor = -1;

    double sampleRate = 48000.0;
    int factor = 2, numChannels = 2;
    std::array<Channel, kMaxChannels> channels;

    // The rectifier, which holds a peak for a few ms rather than drop to
    // nothing at every zero crossing; then the timing networks ahead of the
    // cell, the fixed one and the knobs', each holding a level.
    float keyHold = 0.0f, keyDecay = 0.0f;
    float fixedState = 0.0f, manualState = 0.0f;
    float fixedAttack = 0.0f, fixedRelease = 0.0f, manualAttack = 0.0f, manualRelease = 0.0f;

    // The cell.
    float quick = 0.0f, slow = 0.0f, memory = 0.0f;
    float cellAttack = 0.0f, cellRelease = 0.0f, memoryRelease = 0.0f, memoryCharge = 0.0f, memoryFade = 0.0f;

    float kneeCentre = 0.0f, knee = 12.0f, slope = 0.75f;
    float reduction = 0.0f, lastGain = 1.0f;

    // What glides: the make-up gain, the mix and the bypass.
    float makeup = 1.0f, mix = 1.0f, active = 1.0f, glide = 0.0f;

    // The coupling and the transformers' flux, at the running rate.
    float hpCoef = 0.0f, fluxCoef = 0.0f;

    // The meter.
    float needlePos = 0.0f, needleSpeed = 0.0f;
};

} // namespace obx
