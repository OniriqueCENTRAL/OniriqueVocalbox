// Onirique OptoBox — twice the sample rate, around everything that distorts
#pragma once

#include <array>
#include <cmath>

namespace obx
{

/** A polyphase half-band filter: two chains of first-order allpasses, one per
    phase, running at the low rate. No latency to report and almost no ripple
    in the passband; the price is a phase that is not linear near the top,
    which is why the dry signal of the parallel mix takes the same road. */
class Halfband
{
public:
    static constexpr int kCoefs = 12;

    Halfband()
    {
        // Passband to 0.23 of the high rate (20.3 kHz at 88.2), stopband from 0.27.
        const auto& c = coefficients();

        for (size_t i = 0; i < (size_t) kCoefs; ++i)
        {
            (i % 2 == 0 ? even : odd)[i / 2].a = (float) c[i];
            (i % 2 == 0 ? evenDown : oddDown)[i / 2].a = (float) c[i];
        }
    }

    void reset() noexcept
    {
        for (auto* chain : { &even, &odd, &evenDown, &oddDown })
            for (auto& stage : *chain)
                stage.x1 = stage.y1 = 0.0f;
    }

    /** One sample at the low rate in, the two that stand for it at the high rate out. */
    void up (float x, float& first, float& second) noexcept
    {
        first = run (even, x);
        second = run (odd, x);
    }

    /** Two samples at the high rate in, one at the low rate out. */
    float down (float first, float second) noexcept
    {
        return 0.5f * (run (evenDown, second) + run (oddDown, first));
    }

    /** The allpass coefficients, worked out once (the elliptic design of
        Laurent de Soras's HIIR, for this order and transition). */
    static const std::array<double, kCoefs>& coefficients()
    {
        static const std::array<double, kCoefs> c = design (0.02);
        return c;
    }

private:
    struct Stage
    {
        float a = 0.0f, x1 = 0.0f, y1 = 0.0f;
    };

    using Chain = std::array<Stage, kCoefs / 2>;

    static float run (Chain& chain, float x) noexcept
    {
        for (auto& s : chain)
        {
            const auto y = s.a * (x - s.y1) + s.x1;
            s.x1 = x;
            s.y1 = y;
            x = y;
        }

        return x;
    }

    static std::array<double, kCoefs> design (double transition)
    {
        const double pi = 3.14159265358979323846;

        double k = std::tan ((1.0 - transition * 2.0) * pi / 4.0);
        k *= k;
        const double kksqrt = std::pow (1.0 - k * k, 0.25);
        const double e = 0.5 * (1.0 - kksqrt) / (1.0 + kksqrt);
        const double e2 = e * e;
        const double e4 = e2 * e2;
        const double q = e * (1.0 + e4 * (2.0 + e4 * (15.0 + 150.0 * e4)));

        const int order = kCoefs * 2 + 1;
        std::array<double, kCoefs> out {};

        for (int index = 0; index < kCoefs; ++index)
        {
            const int c = index + 1;
            double num = 0.0, den = 0.0;

            for (int i = 0, sign = 1; i < 40; ++i, sign = -sign)
                num += std::pow (q, (double) (i * (i + 1))) * std::sin ((i * 2 + 1) * c * pi / order) * sign;

            for (int i = 1, sign = -1; i < 40; ++i, sign = -sign)
                den += std::pow (q, (double) (i * i)) * std::cos (i * 2 * c * pi / order) * sign;

            num *= std::pow (q, 0.25);
            den += 0.5;

            const double ww = num / den;
            const double wwsq = ww * ww;
            const double x = std::sqrt ((1.0 - wwsq * k) * (1.0 - wwsq / k)) / (1.0 + wwsq);
            out[(size_t) index] = (1.0 - x) / (1.0 + x);
        }

        return out;
    }

    Chain even, odd, evenDown, oddDown;
};

} // namespace obx
