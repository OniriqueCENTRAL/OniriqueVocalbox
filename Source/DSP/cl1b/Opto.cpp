#include "Opto.h"

#include <algorithm>
#include <cmath>

namespace obx
{

namespace
{
    constexpr double kPi = 3.14159265358979323846;

    // The figures the hardware is known by: the attack and release ranges,
    // the fixed setting's 1 ms and 50 ms, the threshold as the level of the
    // first dB of reduction, the meter's -18 dBFS. What the cell and the
    // valve do around them is set by reasoning about the parts, not measured.
    const Voice kVintage {
        14.0f,              // knee
        2.0f, 70.0f,        // cell: lag, quick recovery
        3500.0f, 0.40f,     // slow recovery, and its share once worn
        1500.0f, 8000.0f,   // wear, and forgetting
        0.0022f, 0.034f,    // valve: second, third
        0.05f,              // iron
        10.0f,              // coupling
        60.0f, 0.5f, 0.8f,  // low resonance
        12000.0f, -1.0f     // top end
    };

    const Voice kModern {
        9.0f,
        0.4f, 40.0f,
        1500.0f, 0.25f,
        2500.0f, 6000.0f,
        0.0007f, 0.024f,
        0.03f,
        5.0f,
        40.0f, 0.2f, 0.7f,
        18000.0f, -0.3f
    };

    /** How much of the way a one-pole goes in one sample, for a time constant. */
    float coefficient (double ms, double rate)
    {
        return (float) (1.0 - std::exp (-1000.0 / (std::max (ms, 0.001) * rate)));
    }

    float dbToGain (float db) noexcept
    {
        return std::pow (10.0f, db * 0.05f);
    }

    // The filters, from Robert Bristow-Johnson's cookbook.
    template <typename Filter>
    void set (Filter& f, double b0, double b1, double b2, double a0, double a1, double a2)
    {
        f.b0 = (float) (b0 / a0);
        f.b1 = (float) (b1 / a0);
        f.b2 = (float) (b2 / a0);
        f.a1 = (float) (a1 / a0);
        f.a2 = (float) (a2 / a0);
    }

    template <typename Filter>
    void peaking (Filter& f, double rate, double hz, double db, double q)
    {
        const auto a = std::pow (10.0, db / 40.0);
        const auto w = 2.0 * kPi * std::min (hz, 0.45 * rate) / rate;
        const auto alpha = std::sin (w) / (2.0 * q);
        set (f, 1.0 + alpha * a, -2.0 * std::cos (w), 1.0 - alpha * a,
                1.0 + alpha / a, -2.0 * std::cos (w), 1.0 - alpha / a);
    }

    template <typename Filter>
    void highShelf (Filter& f, double rate, double hz, double db)
    {
        const auto a = std::pow (10.0, db / 40.0);
        const auto w = 2.0 * kPi * std::min (hz, 0.45 * rate) / rate;
        const auto c = std::cos (w);
        const auto alpha = std::sin (w) / 2.0 * std::sqrt ((a + 1.0 / a) * (1.0 / 0.7 - 1.0) + 2.0);
        const auto r = 2.0 * std::sqrt (a) * alpha;
        set (f, a * ((a + 1.0) + (a - 1.0) * c + r), -2.0 * a * ((a - 1.0) + (a + 1.0) * c), a * ((a + 1.0) + (a - 1.0) * c - r),
                (a + 1.0) - (a - 1.0) * c + r, 2.0 * ((a - 1.0) - (a + 1.0) * c), (a + 1.0) - (a - 1.0) * c - r);
    }

    template <typename Filter>
    void highPass (Filter& f, double rate, double hz)
    {
        const auto w = 2.0 * kPi * hz / rate;
        const auto c = std::cos (w);
        const auto alpha = std::sin (w) / (2.0 * 0.70710678);
        set (f, (1.0 + c) / 2.0, -(1.0 + c), (1.0 + c) / 2.0, 1.0 + alpha, -2.0 * c, 1.0 - alpha);
    }
}

const Voice& voiceOf (Generation generation) noexcept
{
    return generation == Generation::modern ? kModern : kVintage;
}

//==============================================================================
void Compressor::prepare (double rate, int channelCount)
{
    sampleRate = rate > 0.0 ? rate : 48000.0;
    factor = sampleRate < 70000.0 ? 2 : 1;
    numChannels = std::clamp (channelCount, 1, kMaxChannels);

    lowCutFor = -1;
    setSettings (settings);
    reset();
}

void Compressor::reset() noexcept
{
    for (auto& ch : channels)
    {
        ch.band.reset();
        ch.inFlux = ch.outFlux = ch.hpX = ch.hpY = 0.0f;
        ch.bump.clear();
        ch.shelf.clear();
        ch.sidechain.clear();
    }

    keyHold = fixedState = manualState = quick = slow = memory = reduction = 0.0f;
    lastGain = 1.0f;

    makeup = settings.outputDb <= kOutputOffDb + 0.01f ? 0.0f : dbToGain (settings.outputDb);
    mix = std::clamp (settings.parallel, 0.0f, 1.0f);
    active = settings.bypass ? 0.0f : 1.0f;

    needlePos = settings.meter == Meter::compression ? 1.0f : 0.0f;
    needleSpeed = 0.0f;
}

void Compressor::setSettings (const Settings& s) noexcept
{
    settings = s;
    voice = &voiceOf (s.generation);

    if (s.generation != filtersFor || s.lowCut != lowCutFor)
        updateFilters();

    keyDecay = 1.0f - coefficient (5.0, sampleRate);
    fixedAttack = coefficient (1.0, sampleRate);
    fixedRelease = coefficient (50.0, sampleRate);
    manualAttack = coefficient (std::clamp (s.attackMs, 0.5f, 300.0f), sampleRate);
    manualRelease = coefficient (std::clamp (s.releaseMs, 50.0f, 10000.0f), sampleRate);

    cellAttack = coefficient (voice->cellAttackMs, sampleRate);
    cellRelease = coefficient (voice->cellReleaseMs, sampleRate);
    memoryRelease = coefficient (voice->memoryReleaseMs, sampleRate);
    memoryCharge = coefficient (voice->memoryChargeMs, sampleRate);
    memoryFade = coefficient (voice->memoryFadeMs, sampleRate);

    // The knee is placed so that a steady level at the threshold is brought
    // down by exactly 1 dB, which is what the threshold means on this unit.
    slope = 1.0f - 1.0f / std::clamp (s.ratio, 2.0f, 10.0f);
    knee = std::max (voice->kneeDb, 2.2f / slope);
    kneeCentre = s.thresholdDb + 0.5f * knee - std::sqrt (2.0f * knee / slope);

    glide = coefficient (20.0, sampleRate);
}

void Compressor::updateFilters() noexcept
{
    filtersFor = settings.generation;
    lowCutFor = settings.lowCut;

    const auto running = sampleRate * factor;

    hpCoef = (float) std::exp (-2.0 * kPi * voice->lowCutHz / running);
    fluxCoef = (float) (1.0 - std::exp (-2.0 * kPi * 25.0 / running));

    for (auto& ch : channels)
    {
        peaking (ch.bump, running, voice->bumpHz, voice->bumpDb, voice->bumpQ);
        highShelf (ch.shelf, running, voice->shelfHz, voice->shelfDb);

        if (settings.lowCut > 0)
            highPass (ch.sidechain, sampleRate, settings.lowCut == 1 ? 80.0 : 220.0);
        else
            set (ch.sidechain, 1.0, 0.0, 0.0, 1.0, 0.0, 0.0);
    }
}

//==============================================================================
float Compressor::staticReduction (float levelDb) const noexcept
{
    if (settings.thresholdDb >= kThresholdOffDb - 0.01f)
        return 0.0f;

    const auto over = levelDb - kneeCentre;
    const auto half = 0.5f * knee;

    if (over <= -half)
        return 0.0f;

    if (over < half)
    {
        const auto t = over + half;
        return slope * t * t / (2.0f * knee);
    }

    return slope * over;
}

float Compressor::cell (float target) noexcept
{
    quick += (target > quick ? cellAttack : cellRelease) * (target - quick);
    slow += (target > slow ? cellAttack : memoryRelease) * (target - slow);

    // Twelve dB held for a while wears the cell through.
    const auto load = std::min (quick * (1.0f / 12.0f), 1.0f);
    memory += (load > memory ? memoryCharge : memoryFade) * (load - memory);

    return quick + voice->memoryShare * memory * (slow - quick);
}

float Compressor::tube (float x) const noexcept
{
    // Odd harmonics from a soft ceiling far above 0 VU, even ones from the
    // valve's lean to one side. The coupling after it takes the offset away.
    const auto g = voice->tubeOdd;
    const auto odd = std::tanh (g * x) / g;
    return odd - voice->tubeEven * odd * odd;
}

float Compressor::colour (Channel& ch, float x, float gain) noexcept
{
    const auto a = voice->transformer;
    auto s = x * (1.0f / kZeroVu);

    // The input transformer: its flux is the signal's lows, and only they
    // saturate, and only when they are loud.
    ch.inFlux += fluxCoef * (s - ch.inFlux);
    s -= ch.inFlux - std::tanh (a * ch.inFlux) / a;

    // The cell's attenuation, then the valve stage that makes the gain back up.
    s = tube (s * gain * makeup);

    ch.outFlux += fluxCoef * (s - ch.outFlux);
    s -= ch.outFlux - std::tanh (a * ch.outFlux) / a;

    const auto coupled = hpCoef * (ch.hpY + s - ch.hpX);
    ch.hpX = s;
    ch.hpY = coupled;

    return ch.shelf.run (ch.bump.run (coupled)) * kZeroVu;
}

//==============================================================================
void Compressor::process (const float* const* in, float* const* out, int numSamples,
                          const float* const* sidechain, int numSidechain) noexcept
{
    const auto makeupTarget = settings.outputDb <= kOutputOffDb + 0.01f ? 0.0f : dbToGain (settings.outputDb);
    const auto mixTarget = std::clamp (settings.parallel, 0.0f, 1.0f);
    const auto activeTarget = settings.bypass ? 0.0f : 1.0f;
    const auto external = settings.externalSidechain;
    const auto keyChannels = std::min (numSidechain, kMaxChannels);

    // A full-wave rectified sine averages 2/pi of its peak.
    const auto meterScale = (float) (kPi / 2.0) / kZeroVu;
    const auto dt = (float) (1.0 / sampleRate);
    constexpr float omega = 13.0f, damping = 0.8f;   // 300 ms to the mark, 1.5 % over it

    for (int i = 0; i < numSamples; ++i)
    {
        // The detector, at the base rate: whatever feeds it, through the low cut.
        // Set to external with nothing plugged in, it hears nothing, as the
        // hardware would.
        float key = 0.0f;

        if (external)
        {
            for (int c = 0; c < keyChannels; ++c)
                key = std::max (key, std::abs (channels[(size_t) c].sidechain.run (sidechain[c][i])));
        }
        else
        {
            for (int c = 0; c < numChannels; ++c)
                key = std::max (key, std::abs (channels[(size_t) c].sidechain.run (in[c][i])));
        }

        // The rectified level charges the timing networks' capacitors, as on
        // the circuit: through the attack while it is above what they hold,
        // and they drain through the release.
        keyHold = std::max (key, keyHold * keyDecay);
        fixedState += (keyHold > fixedState ? fixedAttack : fixedRelease) * (keyHold - fixedState);
        manualState += (keyHold > manualState ? manualAttack : manualRelease) * (keyHold - manualState);

        // FIX./MAN.: a peak shorter than the attack setting is caught by the
        // fixed network and let go of quickly, down to what the knobs'
        // network has gathered meanwhile, which then lets go at its own pace.
        const auto held = settings.timing == Timing::fixed ? fixedState
                        : settings.timing == Timing::manual ? manualState
                                                            : std::max (fixedState, manualState);

        // Only then the threshold and the ratio, and the light on the cell.
        const auto level = 20.0f * std::log10 (std::max (held, 1.0e-6f) * (1.0f / kZeroVu));
        reduction = cell (staticReduction (level));
        const auto gain = dbToGain (-reduction);

        makeup += glide * (makeupTarget - makeup);
        mix += glide * (mixTarget - mix);
        active += glide * (activeTarget - active);

        float inPeak = 0.0f, outPeak = 0.0f;

        for (int c = 0; c < numChannels; ++c)
        {
            auto& ch = channels[(size_t) c];
            const auto x = in[c][i];
            float y;

            if (factor == 2)
            {
                float u0, u1;
                ch.band.up (x, u0, u1);

                const auto w0 = colour (ch, u0, lastGain + 0.5f * (gain - lastGain));
                const auto w1 = colour (ch, u1, gain);

                // The dry half of the parallel mix takes the same filters as
                // the compressed one, so the two add up in phase.
                y = ch.band.down (u0 + mix * (w0 - u0), u1 + mix * (w1 - u1));
            }
            else
            {
                const auto w = colour (ch, x, gain);
                y = x + mix * (w - x);
            }

            y = x + active * (y - x);
            out[c][i] = y;

            inPeak = std::max (inPeak, std::abs (x));
            outPeak = std::max (outPeak, std::abs (y));
        }

        lastGain = gain;

        // The needle: a coil pulling against a spring through a damper, fed
        // with the rectified signal, or with the gain on COMPRESSION.
        const auto drive = settings.meter == Meter::compression ? gain
                         : (settings.meter == Meter::input ? inPeak : outPeak) * meterScale;

        needleSpeed += (omega * omega * (drive - needlePos) - 2.0f * damping * omega * needleSpeed) * dt;
        needlePos += needleSpeed * dt;

        if (needlePos < 0.0f)
        {
            needlePos = 0.0f;
            needleSpeed = std::max (needleSpeed, 0.0f);
        }
        else if (needlePos > 1.5f)
        {
            needlePos = 1.5f;
            needleSpeed = std::min (needleSpeed, 0.0f);
        }
    }
}

} // namespace obx
