#pragma once
#include "BandSplitter.hpp"
#include "BandCompressor.hpp"
#include "EnvelopeFollower.hpp"
#include "FastMath.hpp"
#include "SimdF.hpp"
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
        envLevelL_ = SimdF(0.f);   // SoA envelope state (4 bands per lane)
        envLevelR_ = SimdF(0.f);
    }

    float bandGainReductionDb(int b) const noexcept { return grDb_[b]; }
    float bandOutputLevel    (int b, int ch) const noexcept { return levelOut_[b][ch]; }

    float crossoverFreqA() const noexcept { return xoA_; }
    float crossoverFreqB() const noexcept { return xoB_; }
    float crossoverFreqC() const noexcept { return xoC_; }

    void process(const float* const* __restrict in, float* const* __restrict out, std::size_t n) noexcept
    {
        // 1) Split into bands.
        for (std::size_t s = 0; s < n; ++s) {
            for (std::size_t c = 0; c < numChannels_; ++c) {
                std::array<float, kNumBands> bands;  // all 4 lanes written by processSample
                splitter_.processSample(c, in[c][s], bands.data());
                for (int b = 0; b < kNumBands; ++b) bandBuf_[b][s][c] = bands[b];
            }
        }
        // 2) Per-band envelope + compression — all 4 bands processed as one
        //    SimdF (SoA: band index = lane). The envelope one-pole and the
        //    collapsed Tier-4a gain computer (fastLog2/fastExp2) run 4-wide.
        //    This mirrors the scalar path lane-for-lane (it matches up to FMA
        //    contraction in the polynomials), so the audible output is the
        //    same as the scalar Tier-4a version — the win is doing the band
        //    transcendentals in one NEON op instead of four.
        rebuildBankSimd();   // gather per-band coefs/params into 4-lane vectors
        SimdF grMin(0.f);    // most-negative grDb per band (starts at 0, like scalar)

        if (stereo_ && numChannels_ == 2) {
            for (std::size_t s = 0; s < n; ++s) {
                float ma[kNumBands];
                for (int b = 0; b < kNumBands; ++b)
                    ma[b] = std::max(std::abs(bandBuf_[b][s][0]),
                                     std::abs(bandBuf_[b][s][1]));
                const SimdF env = tickEnv(envLevelL_, SimdF::load(ma));
                SimdF grDb;
                const SimdF gain = computeGainVec(env, &grDb);
                float g[kNumBands]; gain.store(g);
                for (int b = 0; b < kNumBands; ++b) {
                    bandBuf_[b][s][0] *= g[b];
                    bandBuf_[b][s][1] *= g[b];
                }
                grMin = vmin(grMin, grDb);
            }
        } else if (numChannels_ == 2) {        // unlinked stereo: per-channel env
            for (std::size_t s = 0; s < n; ++s) {
                float aL[kNumBands], aR[kNumBands];
                for (int b = 0; b < kNumBands; ++b) {
                    aL[b] = std::abs(bandBuf_[b][s][0]);
                    aR[b] = std::abs(bandBuf_[b][s][1]);
                }
                const SimdF envL = tickEnv(envLevelL_, SimdF::load(aL));
                SimdF grL; const SimdF gainL = computeGainVec(envL, &grL);
                const SimdF envR = tickEnv(envLevelR_, SimdF::load(aR));
                SimdF grR; const SimdF gainR = computeGainVec(envR, &grR);
                float gL[kNumBands], gR[kNumBands]; gainL.store(gL); gainR.store(gR);
                for (int b = 0; b < kNumBands; ++b) {
                    bandBuf_[b][s][0] *= gL[b];
                    bandBuf_[b][s][1] *= gR[b];
                }
                grMin = vmin(grMin, grR);      // scalar tracks the last channel (R)
            }
        } else {                                // mono
            for (std::size_t s = 0; s < n; ++s) {
                float a[kNumBands];
                for (int b = 0; b < kNumBands; ++b)
                    a[b] = std::abs(bandBuf_[b][s][0]);
                const SimdF env = tickEnv(envLevelL_, SimdF::load(a));
                SimdF grDb;
                const SimdF gain = computeGainVec(env, &grDb);
                float g[kNumBands]; gain.store(g);
                for (int b = 0; b < kNumBands; ++b)
                    bandBuf_[b][s][0] *= g[b];
                grMin = vmin(grMin, grDb);
            }
        }
        grMin.store(grDb_.data());
        // 3) Mix: listen overrides; kill silences.
        // Per-band enable decision is loop-invariant — hoist it out of the
        // per-sample loop so the inner loop is a branchless sum.
        bool anyListen = false;
        for (int b = 0; b < kNumBands; ++b) if (listen_[b]) { anyListen = true; break; }
        std::array<float, kNumBands> bandEnable;
        for (int b = 0; b < kNumBands; ++b) {
            const bool enabled = !kill_[b] && !(anyListen && !listen_[b]);
            bandEnable[b] = enabled ? 1.f : 0.f;
        }
        for (std::size_t c = 0; c < numChannels_; ++c) {
            for (std::size_t s = 0; s < n; ++s) {
                float acc = 0.f;
                for (int b = 0; b < kNumBands; ++b)
                    acc += bandEnable[b] * bandBuf_[b][s][c];
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
    // Gather the scalar per-band coefs/params into 4-lane vectors (band = lane).
    // Cheap, once per block — the cold-path setters keep the scalar objects as
    // the single source of truth (incl. the exp()-derived envelope coefs).
    void rebuildBankSimd() noexcept
    {
        float aC[kNumBands], rC[kNumBands], invT[kNumBands], ns[kNumBands], mk[kNumBands];
        for (int b = 0; b < kNumBands; ++b) {
            aC[b]   = env_[b][0].attackCoef();      // both channels share coefs
            rC[b]   = env_[b][0].releaseCoef();
            invT[b] = comp_[b].invThreshLin();
            ns[b]   = comp_[b].negSlope();
            mk[b]   = comp_[b].makeupGain();
        }
        attackCoef4_   = SimdF::load(aC);
        releaseCoef4_  = SimdF::load(rC);
        invThreshLin4_ = SimdF::load(invT);
        negSlope4_     = SimdF::load(ns);
        makeupGain4_   = SimdF::load(mk);
    }

    // One-pole envelope, 4 bands at once. Mirrors EnvelopeFollower::tick:
    //   coef = (level < absIn) ? attack : release;  level = absIn + coef*(level-absIn)
    SimdF tickEnv(SimdF& level, const SimdF& absIn) const noexcept
    {
        const SimdF mask = vcmpgt(absIn, level);    // absIn > level  ⇔  level < absIn
        const SimdF coef = vselect(mask, attackCoef4_, releaseCoef4_);
        level = absIn + coef * (level - absIn);
        return level;
    }

    // Collapsed gain computer, 4 bands at once. Exact vector mirror of
    // BandCompressor::computeGain. Writes the masked grDb (0 below threshold)
    // to *grDb and returns the per-band gain.
    SimdF computeGainVec(const SimdF& env, SimdF* grDb) const noexcept
    {
        const SimdF r    = env * invThreshLin4_;            // env / threshLin
        const SimdF mask = vcmpgt(r, SimdF(1.0f));          // r > 1
        const SimdF l2   = fastLog2(r);
        const SimdF grAbove   = negSlope4_ * SimdF(6.0205999133f) * l2;
        const SimdF gainAbove = makeupGain4_ * fastExp2(negSlope4_ * l2);
        *grDb = vselect(mask, grAbove, SimdF(0.f));
        return vselect(mask, gainAbove, makeupGain4_);
    }

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

    // SoA hot-path state (band index = SIMD lane). envLevel{L,R}_ persist across
    // blocks (cleared in reset()); the rest are rebuilt each block from the
    // scalar param objects by rebuildBankSimd().
    SimdF envLevelL_, envLevelR_;
    SimdF attackCoef4_, releaseCoef4_;
    SimdF invThreshLin4_, negSlope4_, makeupGain4_;
};

} // namespace moses
