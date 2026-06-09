#pragma once
// FastMath — cheap scalar log2/exp2/pow for the band gain computer.
//
// Mineiro-style minimax approximations (float bit-trick exponent extraction +
// a small rational term). Relative error ~1e-3, i.e. ~0.01 dB on a gain — far
// below audibility for Moses's creative-dynamics use, where the gain is applied
// directly to the band (no feedback recirculation, so the small smooth error
// stays bounded). Branch-free and built only from +/-/* and one divide, so the
// same form vectorizes 4-bands-wide on NEON later.
//
// These REPLACE the per-sample libm log10f/powf in BandCompressor::computeGain.
// Acceptance is the relaxed regression gate + an on-Bela A/B listen, not bit
// equality. See tools/regression and docs/superpowers for the Tier-4a rationale.
#include <cstdint>

namespace moses {

inline float fastLog2(float x) noexcept
{
    union { float f; uint32_t i; } vx = { x };
    union { uint32_t i; float f; } mx = { (vx.i & 0x007FFFFFu) | 0x3f000000u };
    const float y = (float) vx.i * 1.1920928955078125e-7f;   // i * 2^-23
    return y - 124.22551499f
             - 1.498030302f * mx.f
             - 1.72587999f / (0.3520887068f + mx.f);
}

inline float fastExp2(float p) noexcept
{
    const float offset = (p < 0.0f) ? 1.0f : 0.0f;
    const float clipp  = (p < -126.0f) ? -126.0f : p;
    const int   w      = (int) clipp;
    const float z      = clipp - (float) w + offset;
    union { uint32_t i; float f; } v = {
        (uint32_t) ((1 << 23) * (clipp + 121.2740575f
                                 + 27.7280233f / (4.84252568f - z)
                                 - 1.49012907f * z))
    };
    return v.f;
}

// x^p for x > 0. = 2^(p * log2 x).
inline float fastPow(float x, float p) noexcept
{
    return fastExp2(p * fastLog2(x));
}

} // namespace moses
