#pragma once
#include "DspMath.hpp"

namespace moses {

// Feedforward compressor that takes a precomputed envelope level (linear gain)
// and returns a multiplicative gain to apply to the band signal.
// Matches Moses (JUCE) MultiBandComp.h:215-247 algebra.
class BandCompressor
{
public:
    void  setThresholdDb(float db) noexcept { thresholdDb_ = db; }
    void  setRatio(float r)         noexcept { slope_ = 1.f - (1.f / r); }
    void  setMakeupDb(float db)     noexcept { makeupDb_ = db; }

    float computeGain(float envelope) noexcept
    {
        float grDb = slope_ * (thresholdDb_ - gainToDb(envelope));
        grDb = std::min(grDb, 0.f);
        lastGrDb_ = grDb;
        return dbToGain(grDb + makeupDb_);
    }

    float lastGainReductionDb() const noexcept { return lastGrDb_; }

    void reset() noexcept { lastGrDb_ = 0.f; }

private:
    float thresholdDb_ = 0.f;
    float slope_       = 0.f;
    float makeupDb_    = 0.f;
    float lastGrDb_    = 0.f;
};

} // namespace moses
