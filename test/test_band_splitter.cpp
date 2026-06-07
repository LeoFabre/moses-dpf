#include "BandSplitter.hpp"
#include "DspMath.hpp"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#define EXPECT_NEAR(a, b, eps) do {                                          \
    const double _a = double(a), _b = double(b), _e = double(eps);           \
    if (std::abs(_a - _b) > _e) {                                            \
        std::fprintf(stderr, "FAIL %s:%d: %.6f != %.6f (eps=%.6g)\n",        \
            __FILE__, __LINE__, _a, _b, _e);                                 \
        std::exit(1);                                                        \
    }                                                                        \
} while (0)

int main() {
    using moses::BandSplitter;
    constexpr float fs       = 48000.f;
    constexpr float xoB      = 200.f;
    constexpr float xoA      = 800.f;
    constexpr float xoC      = 4000.f;
    constexpr std::size_t numCh = 2;

    BandSplitter bs;
    bs.prepare(fs, numCh);
    bs.setCrossovers(xoB, xoA, xoC);

    const float testFreqs[] = { 80.f, 500.f, 1500.f, 5000.f, 12000.f };
    for (float f : testFreqs) {
        const int preroll = int(fs * 0.5f);
        const int N       = int(fs * 0.25f);
        const float omega = 2.f * moses::kPi * f / fs;
        double acc = 0.0;
        bs.reset();
        for (int n = 0; n < preroll + N; ++n) {
            const float x = std::sin(omega * float(n));
            std::array<float, 4> bandOut{};
            bs.processSample(0, x, bandOut.data());
            float summed = 0.f;
            for (float b : bandOut) summed += b;
            if (n >= preroll) acc += double(summed) * double(summed);
        }
        const float sumRms = std::sqrt(float(acc / double(N)));
        const float inRms  = std::sqrt(0.5f);
        const float gainDb = moses::gainToDb(sumRms / inRms);
        std::fprintf(stderr, "f=%.0f Hz sum-gain=%.3f dB\n", f, gainDb);
        EXPECT_NEAR(gainDb, 0.0f, 0.5f);
    }
    std::puts("OK test_band_splitter");
    return 0;
}
