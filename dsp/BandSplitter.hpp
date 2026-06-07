#pragma once
#include "LinkwitzRileyFilter.hpp"
#include <array>
#include <cstddef>

namespace moses {

// Two-stage Linkwitz-Riley band splitter giving 4 phase-coherent outputs.
// Mirrors the topology from Moses (JUCE) MultiBandComp.h:141-175 + diagram.
//
//   stage1Low  = LP(xoA) → AP(xoC)
//   stage1High = HP(xoA) → AP(xoB)
//   band1 = LP(xoB) of stage1Low
//   band2 = HP(xoB) of stage1Low
//   band3 = LP(xoC) of stage1High
//   band4 = HP(xoC) of stage1High
class BandSplitter
{
public:
    void prepare(float sampleRate, std::size_t numChannels)
    {
        sampleRate_  = sampleRate;
        numChannels_ = numChannels;
        stage1LowLp_.prepare(sampleRate, numChannels);
        stage1LowAp_.prepare(sampleRate, numChannels);
        stage1HighHp_.prepare(sampleRate, numChannels);
        stage1HighAp_.prepare(sampleRate, numChannels);
        b1Lp_.prepare(sampleRate, numChannels);
        b2Hp_.prepare(sampleRate, numChannels);
        b3Lp_.prepare(sampleRate, numChannels);
        b4Hp_.prepare(sampleRate, numChannels);
        stage1LowLp_.setType(LinkwitzRileyFilter::Type::lowpass);
        stage1LowAp_.setType(LinkwitzRileyFilter::Type::allpass);
        stage1HighHp_.setType(LinkwitzRileyFilter::Type::highpass);
        stage1HighAp_.setType(LinkwitzRileyFilter::Type::allpass);
        b1Lp_.setType(LinkwitzRileyFilter::Type::lowpass);
        b2Hp_.setType(LinkwitzRileyFilter::Type::highpass);
        b3Lp_.setType(LinkwitzRileyFilter::Type::lowpass);
        b4Hp_.setType(LinkwitzRileyFilter::Type::highpass);
    }

    void setCrossovers(float xoB, float xoA, float xoC)
    {
        stage1LowLp_.setCutoffFrequency(xoA);
        stage1LowAp_.setCutoffFrequency(xoC);
        stage1HighHp_.setCutoffFrequency(xoA);
        stage1HighAp_.setCutoffFrequency(xoB);
        b1Lp_.setCutoffFrequency(xoB);
        b2Hp_.setCutoffFrequency(xoB);
        b3Lp_.setCutoffFrequency(xoC);
        b4Hp_.setCutoffFrequency(xoC);
    }

    void reset()
    {
        stage1LowLp_.reset(); stage1LowAp_.reset();
        stage1HighHp_.reset(); stage1HighAp_.reset();
        b1Lp_.reset(); b2Hp_.reset(); b3Lp_.reset(); b4Hp_.reset();
    }

    void processSample(std::size_t channel, float x, float* bandsOut) noexcept
    {
        const float lowStage  = stage1LowAp_.processSample(channel,
                                  stage1LowLp_.processSample(channel, x));
        const float highStage = stage1HighAp_.processSample(channel,
                                  stage1HighHp_.processSample(channel, x));
        bandsOut[0] = b1Lp_.processSample(channel, lowStage);
        bandsOut[1] = b2Hp_.processSample(channel, lowStage);
        bandsOut[2] = b3Lp_.processSample(channel, highStage);
        bandsOut[3] = b4Hp_.processSample(channel, highStage);
    }

private:
    float sampleRate_ = 48000.f;
    std::size_t numChannels_ = 0;
    LinkwitzRileyFilter stage1LowLp_, stage1LowAp_;
    LinkwitzRileyFilter stage1HighHp_, stage1HighAp_;
    LinkwitzRileyFilter b1Lp_, b2Hp_, b3Lp_, b4Hp_;
};

} // namespace moses
