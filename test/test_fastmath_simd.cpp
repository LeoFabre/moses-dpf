// test_fastmath_simd — the 4-lane fastLog2/fastExp2 must match the scalar ones
// lane-for-lane (up to FMA-contraction ULPs). On NEON/SSE hosts this validates
// the real vector path against the already-accuracy-proven scalar functions.
#include "FastMath.hpp"
#include <cmath>
#include <cstdio>

using namespace moses;

int main()
{
    double worstLog = 0.0, worstExpRel = 0.0;

    // log2 over x in ~[2^-14, 2^16]
    for (int i = 0; i < 10000; i += 4) {
        float xs[4];
        for (int k = 0; k < 4; ++k) xs[k] = (float) std::pow(2.0, -14.0 + (i + k) * 0.003);
        float out[4]; fastLog2(SimdF::load(xs)).store(out);
        for (int k = 0; k < 4; ++k)
            worstLog = std::fmax(worstLog, std::fabs((double) out[k] - fastLog2(xs[k])));
    }

    // exp2 over p in [-30, 30]
    for (int i = 0; i < 12000; i += 4) {
        float ps[4];
        for (int k = 0; k < 4; ++k) ps[k] = (float) (-30.0 + (i + k) * 0.005);
        float out[4]; fastExp2(SimdF::load(ps)).store(out);
        for (int k = 0; k < 4; ++k) {
            const float s = fastExp2(ps[k]);
            const double d = std::fabs((double) out[k] - s);
            worstExpRel = std::fmax(worstExpRel, s != 0.f ? d / std::fabs((double) s) : d);
        }
    }

    std::printf("fastLog2 SIMD vs scalar: max abs diff = %.3e\n", worstLog);
    std::printf("fastExp2 SIMD vs scalar: max rel diff = %.3e\n", worstExpRel);
    if (worstLog < 1e-3 && worstExpRel < 1e-3) {
        std::puts("OK test_fastmath_simd");
        return 0;
    }
    std::puts("FAIL test_fastmath_simd");
    return 1;
}
