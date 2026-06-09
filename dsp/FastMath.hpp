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
#include <cstring>
#include "SimdF.hpp"

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

// ===========================================================================
// 4-lane (SIMD) versions — same coefficients/structure as the scalar ones, so
// per lane they match the scalar result (bit-identical up to FMA contraction).
// The scalar fallback reuses the proven scalar functions, so the NEON/SSE paths
// are validated against it (test_fastmath_simd). Used by the 4-bands-at-once
// gain computer in MultibandCompressor.
// ===========================================================================

inline SimdF vmax(const SimdF& a, const SimdF& b) noexcept {
#if defined(__ARM_NEON)
    return SimdF(vmaxq_f32(a.v, b.v));
#elif defined(__SSE2__)
    return SimdF(_mm_max_ps(a.v, b.v));
#else
    SimdF r; for (int i=0;i<4;++i) r.v[i] = a.v[i] > b.v[i] ? a.v[i] : b.v[i]; return r;
#endif
}

// Mask of lanes where a > b. Representation is backend-specific — only consume
// it through vselect().
inline SimdF vcmpgt(const SimdF& a, const SimdF& b) noexcept {
#if defined(__ARM_NEON)
    return SimdF(vreinterpretq_f32_u32(vcgtq_f32(a.v, b.v)));
#elif defined(__SSE2__)
    return SimdF(_mm_cmpgt_ps(a.v, b.v));
#else
    SimdF r; for (int i=0;i<4;++i){ uint32_t m = (a.v[i] > b.v[i]) ? 0xFFFFFFFFu : 0u; std::memcpy(&r.v[i], &m, 4); } return r;
#endif
}

// Per lane: mask ? a : b.
inline SimdF vselect(const SimdF& mask, const SimdF& a, const SimdF& b) noexcept {
#if defined(__ARM_NEON)
    return SimdF(vbslq_f32(vreinterpretq_u32_f32(mask.v), a.v, b.v));
#elif defined(__SSE2__)
    return SimdF(_mm_or_ps(_mm_and_ps(mask.v, a.v), _mm_andnot_ps(mask.v, b.v)));
#else
    SimdF r; for (int i=0;i<4;++i){ uint32_t m; std::memcpy(&m, &mask.v[i], 4); r.v[i] = m ? a.v[i] : b.v[i]; } return r;
#endif
}

inline SimdF fastLog2(const SimdF& x) noexcept {
#if defined(__ARM_NEON)
    const uint32x4_t bits = vreinterpretq_u32_f32(x.v);
    const float32x4_t mant = vreinterpretq_f32_u32(
        vorrq_u32(vandq_u32(bits, vdupq_n_u32(0x007FFFFFu)), vdupq_n_u32(0x3f000000u)));
    float32x4_t res = vmulq_n_f32(vcvtq_f32_u32(bits), 1.1920928955078125e-7f);
    res = vsubq_f32(res, vdupq_n_f32(124.22551499f));
    res = vsubq_f32(res, vmulq_n_f32(mant, 1.498030302f));
    const float32x4_t denom = vaddq_f32(vdupq_n_f32(0.3520887068f), mant);
    res = vsubq_f32(res, vdivq_f32(vdupq_n_f32(1.72587999f), denom));
    return SimdF(res);
#elif defined(__SSE2__)
    const __m128i bits = _mm_castps_si128(x.v);
    const __m128 mant = _mm_castsi128_ps(
        _mm_or_si128(_mm_and_si128(bits, _mm_set1_epi32(0x007FFFFF)), _mm_set1_epi32(0x3f000000)));
    __m128 res = _mm_mul_ps(_mm_cvtepi32_ps(bits), _mm_set1_ps(1.1920928955078125e-7f));
    res = _mm_sub_ps(res, _mm_set1_ps(124.22551499f));
    res = _mm_sub_ps(res, _mm_mul_ps(mant, _mm_set1_ps(1.498030302f)));
    const __m128 denom = _mm_add_ps(_mm_set1_ps(0.3520887068f), mant);
    res = _mm_sub_ps(res, _mm_div_ps(_mm_set1_ps(1.72587999f), denom));
    return SimdF(res);
#else
    SimdF r; for (int i=0;i<4;++i) r.v[i] = fastLog2(x.v[i]); return r;
#endif
}

inline SimdF fastExp2(const SimdF& x) noexcept {
#if defined(__ARM_NEON)
    const float32x4_t p = x.v;
    const uint32x4_t neg = vcltq_f32(p, vdupq_n_f32(0.0f));
    const float32x4_t offset = vbslq_f32(neg, vdupq_n_f32(1.0f), vdupq_n_f32(0.0f));
    const float32x4_t clipp = vmaxq_f32(p, vdupq_n_f32(-126.0f));
    const float32x4_t wf = vcvtq_f32_s32(vcvtq_s32_f32(clipp));   // trunc toward zero
    const float32x4_t z = vaddq_f32(vsubq_f32(clipp, wf), offset);
    const float32x4_t denom = vsubq_f32(vdupq_n_f32(4.84252568f), z);
    float32x4_t expr = vaddq_f32(clipp, vdupq_n_f32(121.2740575f));
    expr = vaddq_f32(expr, vdivq_f32(vdupq_n_f32(27.7280233f), denom));
    expr = vsubq_f32(expr, vmulq_n_f32(z, 1.49012907f));
    const uint32x4_t outbits = vcvtq_u32_f32(vmulq_n_f32(expr, 8388608.0f));
    return SimdF(vreinterpretq_f32_u32(outbits));
#elif defined(__SSE2__)
    const __m128 p = x.v;
    const __m128 neg = _mm_cmplt_ps(p, _mm_setzero_ps());
    const __m128 offset = _mm_or_ps(_mm_and_ps(neg, _mm_set1_ps(1.0f)), _mm_andnot_ps(neg, _mm_setzero_ps()));
    const __m128 clipp = _mm_max_ps(p, _mm_set1_ps(-126.0f));
    const __m128 wf = _mm_cvtepi32_ps(_mm_cvttps_epi32(clipp));   // trunc toward zero
    const __m128 z = _mm_add_ps(_mm_sub_ps(clipp, wf), offset);
    const __m128 denom = _mm_sub_ps(_mm_set1_ps(4.84252568f), z);
    __m128 expr = _mm_add_ps(clipp, _mm_set1_ps(121.2740575f));
    expr = _mm_add_ps(expr, _mm_div_ps(_mm_set1_ps(27.7280233f), denom));
    expr = _mm_sub_ps(expr, _mm_mul_ps(z, _mm_set1_ps(1.49012907f)));
    const __m128i outbits = _mm_cvttps_epi32(_mm_mul_ps(expr, _mm_set1_ps(8388608.0f)));
    return SimdF(_mm_castsi128_ps(outbits));
#else
    SimdF r; for (int i=0;i<4;++i) r.v[i] = fastExp2(x.v[i]); return r;
#endif
}

} // namespace moses
