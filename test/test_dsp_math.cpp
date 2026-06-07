#include "DspMath.hpp"
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
    using namespace moses;
    EXPECT_NEAR(gainToDb(1.0f),    0.0f, 1e-5);
    EXPECT_NEAR(gainToDb(0.5f),   -6.0206f, 1e-3);
    EXPECT_NEAR(gainToDb(2.0f),    6.0206f, 1e-3);
    EXPECT_NEAR(dbToGain(0.0f),    1.0f, 1e-5);
    EXPECT_NEAR(dbToGain(-6.0f),   0.5012f, 1e-3);
    EXPECT_NEAR(dbToGain(6.0f),    1.9953f, 1e-3);
    EXPECT_NEAR(gainToDb(0.0f), gainToDb(1e-9f), 1e-3);
    std::puts("OK test_dsp_math");
    return 0;
}
