#include "MultibandCompressor.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>

int main() {
    using moses::MultibandCompressor;
    MultibandCompressor dsp;
    dsp.prepare(48000.f, 2, 64);

    // Inverted order: B=500, A=200, C=300. Expect B < A < C with ≥25% spread.
    dsp.setCrossovers(500.f, 200.f, 300.f);
    const float B = dsp.crossoverFreqB();
    const float A = dsp.crossoverFreqA();
    const float C = dsp.crossoverFreqC();
    std::fprintf(stderr, "B=%.1f A=%.1f C=%.1f\n", B, A, C);
    if (!(B < A && A < C)) {
        std::fprintf(stderr, "FAIL: ordering invariant violated\n");
        std::exit(1);
    }
    if (A / B < 1.249f || C / A < 1.249f) {
        std::fprintf(stderr,
            "FAIL: 25%% spread invariant violated (A/B=%.3f, C/A=%.3f)\n",
            A/B, C/A);
        std::exit(1);
    }
    std::puts("OK test_anti_overlap");
    return 0;
}
