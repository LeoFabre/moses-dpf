#include "MultibandCompressor.hpp"
#include "DspMath.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

int main() {
    using moses::MultibandCompressor;
    constexpr float fs = 48000.f;
    constexpr int   N  = int(fs * 0.5f);
    constexpr int   ch = 2;

    MultibandCompressor mbc;
    mbc.prepare(fs, ch, N);
    mbc.setCrossovers(75.f, 250.f, 5000.f);
    mbc.setStereoLink(true);
    for (int b = 0; b < 4; ++b) {
        mbc.setBandThresholdDb(b, 0.f);
        mbc.setBandAttackMs(b, 10.f);
        mbc.setBandReleaseMs(b, 50.f);
        mbc.setBandRatio(b, 4.f);
        mbc.setBandMakeupDb(b, 0.f);
        mbc.setBandListen(b, false);
        mbc.setBandKill(b, false);
    }

    std::vector<float> inL(N), inR(N), outL(N), outR(N);
    const float omega = 2.f * moses::kPi * 1000.f / fs;
    for (int n = 0; n < N; ++n) { inL[n] = std::sin(omega*n); inR[n] = inL[n]; }
    const float* ins[2]  = { inL.data(), inR.data() };
    float* outs[2]       = { outL.data(), outR.data() };
    mbc.process(ins, outs, N);

    for (int n = 0; n < N; ++n) {
        if (!std::isfinite(outL[n]) || !std::isfinite(outR[n])) {
            std::fprintf(stderr, "FAIL: non-finite at n=%d\n", n);
            std::exit(1);
        }
    }
    double accIn = 0.0, accOut = 0.0;
    const int start = N - N/4;
    for (int n = start; n < N; ++n) {
        accIn  += double(inL[n]) * inL[n];
        accOut += double(outL[n]) * outL[n];
    }
    const float ratio = float(std::sqrt(accOut / accIn));
    if (std::abs(moses::gainToDb(ratio)) > 1.0f) {
        std::fprintf(stderr,
            "FAIL: at-threshold output deviates: %.3f dB\n",
            moses::gainToDb(ratio));
        std::exit(1);
    }

    // Kill band 1 shouldn't affect 1 kHz materially.
    mbc.setBandKill(0, true);
    mbc.reset();
    mbc.process(ins, outs, N);
    accOut = 0.0;
    for (int n = start; n < N; ++n) accOut += double(outL[n]) * outL[n];
    if (std::abs(moses::gainToDb(float(std::sqrt(accOut / accIn)))) > 1.0f) {
        std::fprintf(stderr, "FAIL: killing band1 affected 1 kHz output\n");
        std::exit(1);
    }
    std::puts("OK test_multiband");
    return 0;
}
