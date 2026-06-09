// test_gain_approx — the collapsed FastMath gain computer must match the
// original libm formula (env -> log10 -> dB -> pow) to within a small, smooth
// dB error across the parameter and envelope range. Reports the worst-case gain
// error in dB; passes if under a creative-dynamics-safe bound.
#include "BandCompressor.hpp"
#include "DspMath.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace moses;

// Reference: the ORIGINAL per-sample formula, in double, with libm.
static double refGain(double thresholdDb, double ratio, double makeupDb, double env)
{
    const double slope = 1.0 - 1.0 / ratio;
    double grDb = slope * (thresholdDb - 20.0 * std::log10(std::max(env, 1e-9)));
    if (grDb > 0.0) grDb = 0.0;
    return std::pow(10.0, 0.05 * (grDb + makeupDb));
}

int main()
{
    struct Cfg { float thr, ratio, makeup; };
    const Cfg cfgs[] = {
        {-30.f, 4.f,  0.f},   // all_bands_comp preset
        {-12.f, 2.f,  0.f},   // gentle preset
        {-40.f, 8.f,  6.f},   // hard + makeup
        {-6.f,  20.f, 0.f},   // near-limiting
        {-50.f, 1.5f, 3.f},   // soft
    };

    double worstDb = 0.0;
    for (const auto& c : cfgs) {
        BandCompressor comp;
        comp.setThresholdDb(c.thr);
        comp.setRatio(c.ratio);
        comp.setMakeupDb(c.makeup);

        // Sweep envelope from -90 dBFS to +12 dBFS.
        for (int d = -900; d <= 120; ++d) {
            const double envDb = d * 0.1;
            const float  env   = (float) std::pow(10.0, 0.05 * envDb);
            const double gRef  = refGain(c.thr, c.ratio, c.makeup, env);
            const double gNew  = comp.computeGain(env);
            if (gRef > 1e-12 && gNew > 1e-12) {
                const double errDb = std::fabs(20.0 * std::log10(gNew / gRef));
                if (errDb > worstDb) worstDb = errDb;
            }
        }
    }

    std::printf("collapsed FastMath gain vs libm reference: worst-case error = %.4f dB\n", worstDb);
    // Creative-dynamics bound: 0.1 dB is inaudible and constant-ish; flag anything larger.
    if (worstDb <= 0.1) {
        std::puts("OK test_gain_approx (within 0.1 dB)");
        return 0;
    }
    std::puts("FAIL test_gain_approx (worst-case error exceeds 0.1 dB)");
    return 1;
}
