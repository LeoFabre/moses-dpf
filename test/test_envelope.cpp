#include "EnvelopeFollower.hpp"
#include "DspMath.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>

int main() {
    using moses::EnvelopeFollower;
    constexpr float fs = 48000.f;

    // Attack t63: ~10 ms.
    {
        EnvelopeFollower env;
        env.prepare(fs);
        env.setAttackTimeMs(10.f);
        env.setReleaseTimeMs(200.f);
        const int target63  = int(0.010f * fs);
        float value = 0.f;
        for (int n = 0; n < target63 + 1; ++n) value = env.tick(1.0f);
        const float expected = 1.f - std::exp(-1.f);
        if (std::abs(value - expected) > 0.03f) {
            std::fprintf(stderr,
                "FAIL attack t63: got %.4f, expected ~%.4f\n", value, expected);
            std::exit(1);
        }
    }
    // Release t63: ~50 ms.
    {
        EnvelopeFollower env;
        env.prepare(fs);
        env.setAttackTimeMs(1.f);
        env.setReleaseTimeMs(50.f);
        for (int n = 0; n < int(0.1f * fs); ++n) env.tick(1.0f);
        const int n63 = int(0.050f * fs);
        float value = 0.f;
        for (int n = 0; n < n63; ++n) value = env.tick(0.0f);
        const float expected = std::exp(-1.f);
        if (std::abs(value - expected) > 0.03f) {
            std::fprintf(stderr,
                "FAIL release t63: got %.4f, expected ~%.4f\n", value, expected);
            std::exit(1);
        }
    }
    std::puts("OK test_envelope");
    return 0;
}
