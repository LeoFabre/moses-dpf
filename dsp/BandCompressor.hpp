#pragma once
#include "DspMath.hpp"
#include "FastMath.hpp"

namespace moses {

// Feedforward compressor that takes a precomputed envelope level (linear gain)
// and returns a multiplicative gain to apply to the band signal.
// Matches Moses (JUCE) MultiBandComp.h:215-247 algebra.
//
// The original per-sample path did env -> 20*log10 (gainToDb) -> dB scale ->
// 10^(.) (dbToGain), i.e. a libm log10f AND a libm powf that largely undo each
// other. Collapsed algebraically (Tier-4a):
//
//   grDb = slope*(threshDb - 20*log10(env)) = -slope * 20*log10(env/threshLin)
//   gain = dbToGain(min(grDb,0) + makeupDb)
//        = makeupGain * min(1, (env/threshLin)^(-slope))
//
// so above threshold it is ONE power, evaluated with FastMath (2^/log2 polys);
// below threshold it is just makeupGain. threshLin/invThreshLin/makeupGain/
// negSlope are precomputed on parameter change (cold path). The metering grDb is
// recovered from the same log2 (no extra transcendental). Output is approximate
// (~0.01 dB) but smooth, bounded and non-accumulating (gain is applied directly
// to the band, not recirculated) — validated by the relaxed regression gate and
// an on-Bela listen, not by bit equality.
class BandCompressor
{
public:
    void  setThresholdDb(float db) noexcept
    {
        thresholdDb_ = db;
        threshLin_   = dbToGain(db);
        invThreshLin_ = 1.0f / threshLin_;
    }
    void  setRatio(float r)         noexcept { slope_ = 1.f - (1.f / r); negSlope_ = -slope_; }
    void  setMakeupDb(float db)     noexcept { makeupDb_ = db; makeupGain_ = dbToGain(db); }

    float computeGain(float envelope) noexcept
    {
        const float r = envelope * invThreshLin_;   // env / threshLin
        if (r <= 1.0f) {                             // below threshold: no reduction
            lastGrDb_ = 0.f;
            return makeupGain_;
        }
        const float l2 = fastLog2(r);                // > 0 here
        // grDb = -slope * 20*log10(r) = negSlope * (20/log2(10)) * log2(r)
        lastGrDb_ = negSlope_ * 6.0205999133f * l2;  // <= 0
        return makeupGain_ * fastExp2(negSlope_ * l2);   // makeupGain * r^(-slope)
    }

    float lastGainReductionDb() const noexcept { return lastGrDb_; }

    void reset() noexcept { lastGrDb_ = 0.f; }

private:
    float thresholdDb_  = 0.f;
    float slope_        = 0.f;
    float makeupDb_     = 0.f;
    float lastGrDb_     = 0.f;
    // Precomputed (cold path) for the collapsed gain computer.
    float threshLin_    = 1.f;
    float invThreshLin_ = 1.f;
    float negSlope_     = 0.f;
    float makeupGain_   = 1.f;
};

} // namespace moses
