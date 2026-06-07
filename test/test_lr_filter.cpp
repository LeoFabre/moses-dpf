#include "LinkwitzRileyFilter.hpp"
#include "DspMath.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#define EXPECT_NEAR(a, b, eps) do {                                          \
    const double _a = double(a), _b = double(b), _e = double(eps);           \
    if (std::abs(_a - _b) > _e) {                                            \
        std::fprintf(stderr, "FAIL %s:%d: %.6f != %.6f (eps=%.6g)\n",        \
            __FILE__, __LINE__, _a, _b, _e);                                 \
        std::exit(1);                                                        \
    }                                                                        \
} while (0)

static float steadyStateRms(moses::LinkwitzRileyFilter& f,
                            float fs, float freqHz, int channel)
{
    const int prerollSamples = int(fs * 0.5f);
    const int measureSamples = int(fs * 0.25f);
    const float omega = 2.0f * moses::kPi * freqHz / fs;
    double acc = 0.0;
    for (int n = 0; n < prerollSamples + measureSamples; ++n) {
        const float x = std::sin(omega * float(n));
        const float y = f.processSample(channel, x);
        if (n >= prerollSamples) acc += double(y) * double(y);
    }
    return std::sqrt(float(acc / double(measureSamples)));
}

int main() {
    using moses::LinkwitzRileyFilter;
    constexpr float fs = 48000.0f;
    constexpr float fc = 1000.0f;
    const float inputRms = std::sqrt(0.5f);

    {
        LinkwitzRileyFilter lp;
        lp.prepare(fs, 1);
        lp.setType(LinkwitzRileyFilter::Type::lowpass);
        lp.setCutoffFrequency(fc);
        const float rms = steadyStateRms(lp, fs, fc, 0);
        EXPECT_NEAR(moses::gainToDb(rms / inputRms), -6.0f, 0.3f);
    }
    {
        LinkwitzRileyFilter lp;
        lp.prepare(fs, 1);
        lp.setType(LinkwitzRileyFilter::Type::lowpass);
        lp.setCutoffFrequency(fc);
        const float rms = steadyStateRms(lp, fs, fc * 10.0f, 0);
        const float gainDb = moses::gainToDb(rms / inputRms);
        if (gainDb > -40.0f) {
            std::fprintf(stderr, "FAIL: LP @10*fc gain %.2f dB > -40\n", gainDb);
            std::exit(1);
        }
    }
    {
        LinkwitzRileyFilter hp;
        hp.prepare(fs, 1);
        hp.setType(LinkwitzRileyFilter::Type::highpass);
        hp.setCutoffFrequency(fc);
        const float rms = steadyStateRms(hp, fs, fc, 0);
        EXPECT_NEAR(moses::gainToDb(rms / inputRms), -6.0f, 0.3f);
    }
    {
        LinkwitzRileyFilter hp;
        hp.prepare(fs, 1);
        hp.setType(LinkwitzRileyFilter::Type::highpass);
        hp.setCutoffFrequency(fc);
        const float rms = steadyStateRms(hp, fs, fc * 0.1f, 0);
        const float gainDb = moses::gainToDb(rms / inputRms);
        if (gainDb > -40.0f) {
            std::fprintf(stderr, "FAIL: HP @0.1*fc gain %.2f dB > -40\n", gainDb);
            std::exit(1);
        }
    }
    // LR4 invariant: |LP(f) + HP(f)|_rms ≈ |x(f)|_rms across the band.
    {
        const float testFreqs[] = { 100.0f, 500.0f, 1000.0f, 2000.0f, 8000.0f };
        for (float f : testFreqs) {
            LinkwitzRileyFilter lp, hp;
            lp.prepare(fs, 1); hp.prepare(fs, 1);
            lp.setType(LinkwitzRileyFilter::Type::lowpass);  lp.setCutoffFrequency(fc);
            hp.setType(LinkwitzRileyFilter::Type::highpass); hp.setCutoffFrequency(fc);
            const int preroll = int(fs * 0.5f);
            const int N       = int(fs * 0.25f);
            const float omega = 2.f * moses::kPi * f / fs;
            double acc = 0.0;
            for (int n = 0; n < preroll + N; ++n) {
                const float x = std::sin(omega * float(n));
                const float y = lp.processSample(0, x) + hp.processSample(0, x);
                if (n >= preroll) acc += double(y) * double(y);
            }
            const float sumRms = std::sqrt(float(acc / double(N)));
            EXPECT_NEAR(moses::gainToDb(sumRms / inputRms), 0.0f, 0.3f);
        }
    }
    // Allpass mode preserves magnitude at every frequency.
    {
        LinkwitzRileyFilter ap;
        ap.prepare(fs, 1);
        ap.setType(LinkwitzRileyFilter::Type::allpass);
        ap.setCutoffFrequency(fc);
        const float testFreqs[] = { 100.0f, 1000.0f, 5000.0f };
        for (float f : testFreqs) {
            const float rms = steadyStateRms(ap, fs, f, 0);
            EXPECT_NEAR(moses::gainToDb(rms / inputRms), 0.0f, 0.3f);
        }
    }
    std::puts("OK test_lr_filter");
    return 0;
}
