#pragma once
#include "BandSplitter.hpp"
#include "BandCompressor.hpp"
#include "EnvelopeFollower.hpp"
#include "DspMath.hpp"
#include <array>
#include <cstddef>
#include <vector>

namespace moses {

inline constexpr int kNumBands    = 4;
inline constexpr int kMaxChannels = 2;

class MultibandCompressor
{
public:
    void prepare(float sampleRate, std::size_t numChannels, std::size_t maxBlock)
    {
        sampleRate_  = sampleRate;
        numChannels_ = numChannels;
        splitter_.prepare(sampleRate, numChannels);
        for (int b = 0; b < kNumBands; ++b) {
            for (std::size_t c = 0; c < numChannels; ++c) {
                env_[b][c].prepare(sampleRate);
                env_[b][c].setAttackTimeMs(attackMs_[b]);
                env_[b][c].setReleaseTimeMs(releaseMs_[b]);
            }
        }
        bandBuf_.assign(kNumBands, std::vector<std::array<float, kMaxChannels>>(maxBlock));
    }

    void setCrossovers(float xoB, float xoA, float xoC) noexcept
    {
        // Anti-overlap (Moses MultiBandComp.h:122-138 logic). The original
        // uses tempA/tempB/tempC (untouched inputs) on the RHS of each clamp,
        // not the freshly-modified values, so we keep originals around.
        const float origA = xoA, origB = xoB, origC = xoC;
        float newA = std::max(origA, origB * 1.25f);
        const float newB = std::min(origB, origA * 0.8f);
        newA = std::min(newA, origC * 0.8f);
        const float newC = std::max(origC, origA * 1.25f);
        xoB_ = newB; xoA_ = newA; xoC_ = newC;
        splitter_.setCrossovers(newB, newA, newC);
    }

    void setStereoLink(bool linked) noexcept { stereo_ = linked; }

    void setBandThresholdDb(int b, float v) noexcept { comp_[b].setThresholdDb(v); }
    void setBandRatio       (int b, float v) noexcept { comp_[b].setRatio(v); }
    void setBandMakeupDb    (int b, float v) noexcept { comp_[b].setMakeupDb(v); }
    void setBandAttackMs    (int b, float v) noexcept
    {
        attackMs_[b] = v;
        for (auto& e : env_[b]) e.setAttackTimeMs(v);
    }
    void setBandReleaseMs   (int b, float v) noexcept
    {
        releaseMs_[b] = v;
        for (auto& e : env_[b]) e.setReleaseTimeMs(v);
    }
    void setBandKill        (int b, bool on) noexcept { kill_[b]   = on; }
    void setBandListen      (int b, bool on) noexcept { listen_[b] = on; }

    void reset() noexcept
    {
        splitter_.reset();
        for (auto& row : env_)  for (auto& e : row) e.reset();
        for (auto& c   : comp_) c.reset();
    }

    float bandGainReductionDb(int b) const noexcept { return grDb_[b]; }
    float bandOutputLevel    (int b, int ch) const noexcept { return levelOut_[b][ch]; }

    float crossoverFreqA() const noexcept { return xoA_; }
    float crossoverFreqB() const noexcept { return xoB_; }
    float crossoverFreqC() const noexcept { return xoC_; }

    void process(const float* const* in, float* const* out, std::size_t n) noexcept
    {
        // 1) Split into bands.
        for (std::size_t s = 0; s < n; ++s) {
            for (std::size_t c = 0; c < numChannels_; ++c) {
                std::array<float, kNumBands> bands{};
                splitter_.processSample(c, in[c][s], bands.data());
                for (int b = 0; b < kNumBands; ++b) bandBuf_[b][s][c] = bands[b];
            }
        }
        // 2) Per-band envelope + compression.
        for (int b = 0; b < kNumBands; ++b) {
            grDb_[b] = 0.f;
            for (std::size_t s = 0; s < n; ++s) {
                float gainL = 1.f, gainR = 1.f;
                if (stereo_ && numChannels_ == 2) {
                    const float maxAbs = std::max(std::abs(bandBuf_[b][s][0]),
                                                  std::abs(bandBuf_[b][s][1]));
                    const float env = env_[b][0].tick(maxAbs);
                    gainL = gainR = comp_[b].computeGain(env);
                } else {
                    for (std::size_t c = 0; c < numChannels_; ++c) {
                        const float env = env_[b][c].tick(std::abs(bandBuf_[b][s][c]));
                        const float g = comp_[b].computeGain(env);
                        if (c == 0) gainL = g; else gainR = g;
                    }
                }
                bandBuf_[b][s][0] *= gainL;
                if (numChannels_ > 1) bandBuf_[b][s][1] *= gainR;
                if (comp_[b].lastGainReductionDb() < grDb_[b])
                    grDb_[b] = comp_[b].lastGainReductionDb();
            }
        }
        // 3) Mix: listen overrides; kill silences.
        bool anyListen = false;
        for (int b = 0; b < kNumBands; ++b) if (listen_[b]) { anyListen = true; break; }
        for (std::size_t c = 0; c < numChannels_; ++c) {
            for (std::size_t s = 0; s < n; ++s) {
                float acc = 0.f;
                for (int b = 0; b < kNumBands; ++b) {
                    if (kill_[b]) continue;
                    if (anyListen && !listen_[b]) continue;
                    acc += bandBuf_[b][s][c];
                }
                out[c][s] = acc;
            }
        }
        // 4) Per-band peak output levels for metering.
        for (int b = 0; b < kNumBands; ++b) {
            for (std::size_t c = 0; c < numChannels_; ++c) {
                float peak = 0.f;
                for (std::size_t s = 0; s < n; ++s) {
                    const float a = std::abs(bandBuf_[b][s][c]);
                    if (a > peak) peak = a;
                }
                levelOut_[b][c] = peak;
            }
        }
    }

private:
    float sampleRate_  = 48000.f;
    std::size_t numChannels_ = 0;
    float xoA_ = 250.f, xoB_ = 75.f, xoC_ = 5000.f;
    bool  stereo_      = true;

    std::array<float, kNumBands> attackMs_  {10.f,10.f,10.f,10.f};
    std::array<float, kNumBands> releaseMs_ {50.f,50.f,50.f,50.f};
    std::array<bool,  kNumBands> kill_   {false,false,false,false};
    std::array<bool,  kNumBands> listen_ {false,false,false,false};

    BandSplitter splitter_;
    std::array<std::array<EnvelopeFollower, kMaxChannels>, kNumBands> env_;
    std::array<BandCompressor, kNumBands> comp_;
    std::vector<std::vector<std::array<float, kMaxChannels>>> bandBuf_;

    std::array<float, kNumBands> grDb_ {0.f,0.f,0.f,0.f};
    std::array<std::array<float, kMaxChannels>, kNumBands> levelOut_ {};
};

} // namespace moses
