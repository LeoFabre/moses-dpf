#include "BandCompressor.hpp"
#include "DspMath.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>

int main() {
    using moses::BandCompressor;

    // Below threshold: gain ≈ 1.0.
    {
        BandCompressor c;
        c.setThresholdDb(-20.f);
        c.setRatio(4.f);
        c.setMakeupDb(0.f);
        const float env = moses::dbToGain(-30.f);
        const float gain = c.computeGain(env);
        if (std::abs(moses::gainToDb(gain)) > 0.05f) {
            std::fprintf(stderr,
                "FAIL: below threshold, expected 0 dB; got %.4f dB\n",
                moses::gainToDb(gain));
            std::exit(1);
        }
    }
    // 12 dB over threshold, ratio 4:1 ⇒ GR = 12*(1-1/4) = 9 dB.
    {
        BandCompressor c;
        c.setThresholdDb(-20.f);
        c.setRatio(4.f);
        c.setMakeupDb(0.f);
        const float env = moses::dbToGain(-20.f + 12.f);
        const float gainDb = moses::gainToDb(c.computeGain(env));
        if (std::abs(gainDb - (-9.f)) > 0.1f) {
            std::fprintf(stderr, "FAIL: expected -9 dB, got %.3f dB\n", gainDb);
            std::exit(1);
        }
        if (std::abs(c.lastGainReductionDb() - (-9.f)) > 0.1f) {
            std::fprintf(stderr, "FAIL: lastGR mismatch %.3f\n", c.lastGainReductionDb());
            std::exit(1);
        }
    }
    // Makeup is additive.
    {
        BandCompressor c;
        c.setThresholdDb(-20.f);
        c.setRatio(4.f);
        c.setMakeupDb(6.f);
        const float env = moses::dbToGain(-20.f + 12.f);
        const float gainDb = moses::gainToDb(c.computeGain(env));
        if (std::abs(gainDb - (-3.f)) > 0.1f) {
            std::fprintf(stderr, "FAIL: expected -3 dB with makeup, got %.3f dB\n", gainDb);
            std::exit(1);
        }
    }
    std::puts("OK test_compressor");
    return 0;
}
