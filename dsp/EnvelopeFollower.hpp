#pragma once
#include <cmath>

namespace moses {

class EnvelopeFollower
{
public:
    void prepare(float sampleRate) noexcept
    {
        sampleRate_ = sampleRate;
        recompute();
    }

    void setAttackTimeMs(float ms) noexcept   { attackMs_  = ms; recompute(); }
    void setReleaseTimeMs(float ms) noexcept  { releaseMs_ = ms; recompute(); }

    void reset() noexcept { level_ = 0.f; }

    float tick(float absInput) noexcept
    {
        const float coef = (level_ < absInput) ? attackCoef_ : releaseCoef_;
        level_ = absInput + coef * (level_ - absInput);
        return level_;
    }

    float level() const noexcept { return level_; }

private:
    void recompute() noexcept
    {
        attackCoef_  = std::exp(-1.f / ((attackMs_  * 1e-3f) * sampleRate_));
        releaseCoef_ = std::exp(-1.f / ((releaseMs_ * 1e-3f) * sampleRate_));
    }

    float sampleRate_  = 48000.f;
    float attackMs_    = 10.f;
    float releaseMs_   = 100.f;
    float attackCoef_  = 0.f;
    float releaseCoef_ = 0.f;
    float level_       = 0.f;
};

} // namespace moses
