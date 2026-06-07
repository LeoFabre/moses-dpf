#pragma once
#include "DspMath.hpp"
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace moses {

// 4th-order Linkwitz-Riley filter = cascade of two 2nd-order Butterworth
// biquads (Q = 1/sqrt(2)). Stores per-channel state.
//
// Allpass mode is implemented as (HP + LP) on the same cutoff, which gives
// the 4th-order LR all-pass that is phase-compatible with the crossover —
// LR4 sums to a 4th-order all-pass by construction.
class LinkwitzRileyFilter
{
public:
    enum class Type { lowpass, highpass, allpass };

    void prepare(float sampleRate, std::size_t numChannels)
    {
        sampleRate_ = sampleRate;
        s1_.assign(numChannels, {});
        s2_.assign(numChannels, {});
        apLp1_.assign(numChannels, {});
        apLp2_.assign(numChannels, {});
        apHp1_.assign(numChannels, {});
        apHp2_.assign(numChannels, {});
        recompute();
    }

    void setCutoffFrequency(float fc)
    {
        fc = clamp(fc, 1.0f, 0.45f * sampleRate_);
        if (fc == fc_) return;
        fc_ = fc;
        recompute();
    }

    void setType(Type t)
    {
        if (t == type_) return;
        type_ = t;
        recompute();
    }

    void reset()
    {
        for (auto& s : s1_)    s = {};
        for (auto& s : s2_)    s = {};
        for (auto& s : apLp1_) s = {};
        for (auto& s : apLp2_) s = {};
        for (auto& s : apHp1_) s = {};
        for (auto& s : apHp2_) s = {};
    }

    float processSample(std::size_t channel, float x) noexcept
    {
        auto run = [&](BiquadState& s, float xin) {
            const float y = b0_ * xin + s.z1;
            s.z1 = b1_ * xin - a1_ * y + s.z2;
            s.z2 = b2_ * xin - a2_ * y;
            return y;
        };
        switch (type_) {
            case Type::lowpass:
            case Type::highpass: {
                float y = run(s1_[channel], x);
                y = run(s2_[channel], y);
                return y;
            }
            case Type::allpass: {
                const float lp = runLpBranch(channel, x);
                const float hp = runHpBranch(channel, x);
                return lp + hp;
            }
        }
        return x;
    }

    void process(std::size_t channel, const float* in, float* out, std::size_t n) noexcept
    {
        for (std::size_t i = 0; i < n; ++i) out[i] = processSample(channel, in[i]);
    }

private:
    struct BiquadState { float z1 = 0.f, z2 = 0.f; };

    void recompute()
    {
        const float w  = 2.0f * kPi * fc_ / sampleRate_;
        const float c  = std::cos(w);
        const float s  = std::sin(w);
        const float a  = s * 0.70710678118f;   // alpha = sin(w)/(2Q), Q=1/sqrt(2)
        const float a0 = 1.0f + a;
        a1_ = -2.0f * c / a0;
        a2_ = (1.0f - a) / a0;
        bLp0_ =  (1.0f - c) * 0.5f / a0;
        bLp1_ =  (1.0f - c) / a0;
        bLp2_ =  (1.0f - c) * 0.5f / a0;
        bHp0_ =  (1.0f + c) * 0.5f / a0;
        bHp1_ = -(1.0f + c) / a0;
        bHp2_ =  (1.0f + c) * 0.5f / a0;
        switch (type_) {
            case Type::lowpass:  b0_=bLp0_; b1_=bLp1_; b2_=bLp2_; break;
            case Type::highpass: b0_=bHp0_; b1_=bHp1_; b2_=bHp2_; break;
            case Type::allpass:  b0_=bLp0_; b1_=bLp1_; b2_=bLp2_; break;
        }
    }

    float runLpBranch(std::size_t ch, float x) noexcept
    {
        auto& s1 = apLp1_[ch];
        auto& s2 = apLp2_[ch];
        float y = bLp0_ * x + s1.z1;
        s1.z1 = bLp1_ * x - a1_ * y + s1.z2;
        s1.z2 = bLp2_ * x - a2_ * y;
        const float y2 = bLp0_ * y + s2.z1;
        s2.z1 = bLp1_ * y - a1_ * y2 + s2.z2;
        s2.z2 = bLp2_ * y - a2_ * y2;
        return y2;
    }
    float runHpBranch(std::size_t ch, float x) noexcept
    {
        auto& s1 = apHp1_[ch];
        auto& s2 = apHp2_[ch];
        float y = bHp0_ * x + s1.z1;
        s1.z1 = bHp1_ * x - a1_ * y + s1.z2;
        s1.z2 = bHp2_ * x - a2_ * y;
        const float y2 = bHp0_ * y + s2.z1;
        s2.z1 = bHp1_ * y - a1_ * y2 + s2.z2;
        s2.z2 = bHp2_ * y - a2_ * y2;
        return y2;
    }

    float sampleRate_ = 48000.f;
    float fc_         = 1000.f;
    Type  type_       = Type::lowpass;

    float a1_=0.f, a2_=0.f;
    float b0_=0.f, b1_=0.f, b2_=0.f;
    float bLp0_=0.f, bLp1_=0.f, bLp2_=0.f;
    float bHp0_=0.f, bHp1_=0.f, bHp2_=0.f;

    std::vector<BiquadState> s1_, s2_;
    std::vector<BiquadState> apLp1_, apLp2_, apHp1_, apHp2_;
};

} // namespace moses
