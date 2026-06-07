#include "MultibandCompressor.hpp"
#include "DspMath.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

int main() {
    using namespace moses;
    MultibandCompressor dsp;
    const float fs = 48000.f;
    const std::size_t n = 1024;
    dsp.prepare(fs, 2, n);
    dsp.setCrossovers(75.f, 250.f, 5000.f);
    for (int b = 0; b < kNumBands; ++b) {
        dsp.setBandThresholdDb(b, -20.f);
        dsp.setBandRatio(b, 4.f);
        dsp.setBandAttackMs(b, 5.f);
        dsp.setBandReleaseMs(b, 50.f);
        dsp.setBandMakeupDb(b, 0.f);
    }
    std::vector<float> inL(n, 0.5f), inR(n, 0.5f), outL(n), outR(n);
    const float* ins[2] = { inL.data(), inR.data() };
    float* outs[2]      = { outL.data(), outR.data() };
    dsp.process(ins, outs, n);
    for (std::size_t i = 0; i < n; ++i) {
        if (!std::isfinite(outL[i]) || !std::isfinite(outR[i])) {
            std::fprintf(stderr, "FAIL non-finite at %zu\n", i);
            std::exit(1);
        }
    }
    std::puts("OK test_plugin_offline");
    return 0;
}
