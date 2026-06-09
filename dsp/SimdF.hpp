#pragma once

// SimdF — thin 4-lane float SIMD wrapper
//
// Backend selection (compile-time):
//   __ARM_NEON  → float32x4_t  (Apple Silicon / Bela Cortex-A)
//   __SSE2__    → __m128        (x86-64)
//   else        → scalar float[4] (portable fallback)
//
// Interleave layout (used by AllPassChain to mirror JUCE AudioData::interleaveSamples):
//   interleaveStereo(L, R) packs 2 consecutive frames per channel into one SimdF:
//     lane0 = L[0], lane1 = R[0], lane2 = L[1], lane3 = R[1]
//   deinterleaveStereo reverses this exactly.
//
// All operations are identical in semantics across backends.

#if defined(__ARM_NEON)
  #include <arm_neon.h>
#elif defined(__SSE2__)
  #include <immintrin.h>
#else
  #include <cstring>   // std::memcpy
#endif

struct SimdF {
#if defined(__ARM_NEON)
    // ---- NEON backend -------------------------------------------------------
    using Vec = float32x4_t;
    Vec v;

    SimdF() : v(vdupq_n_f32(0.0f)) {}
    explicit SimdF(float x) : v(vdupq_n_f32(x)) {}
    SimdF(Vec x) : v(x) {}

    static SimdF load(const float* p) { return SimdF(vld1q_f32(p)); }
    void store(float* p) const { vst1q_f32(p, v); }

    SimdF operator+(const SimdF& o) const { return SimdF(vaddq_f32(v, o.v)); }
    SimdF operator-(const SimdF& o) const { return SimdF(vsubq_f32(v, o.v)); }
    SimdF operator*(const SimdF& o) const { return SimdF(vmulq_f32(v, o.v)); }

    // --- interleave/deinterleave (2 frames × 2 channels → 4 lanes) ----------
    // lane0=L[0], lane1=R[0], lane2=L[1], lane3=R[1]
    static SimdF interleaveStereo(const float* L, const float* R) {
        // load L[0],L[1] and R[0],R[1] then zip
        float32x2_t vL = vld1_f32(L);
        float32x2_t vR = vld1_f32(R);
        float32x2x2_t zipped = vzip_f32(vL, vR);
        // zipped.val[0] = {L[0],R[0]}, zipped.val[1] = {L[1],R[1]}
        return SimdF(vcombine_f32(zipped.val[0], zipped.val[1]));
    }

    static void deinterleaveStereo(const SimdF& in, float* L, float* R) {
        float32x2_t lo = vget_low_f32(in.v);   // {L[0], R[0]}
        float32x2_t hi = vget_high_f32(in.v);  // {L[1], R[1]}
        float32x2x2_t uzipped = vuzp_f32(lo, hi);
        // uzipped.val[0] = {L[0], L[1]}, uzipped.val[1] = {R[0], R[1]}
        vst1_f32(L, uzipped.val[0]);
        vst1_f32(R, uzipped.val[1]);
    }

#elif defined(__SSE2__)
    // ---- SSE2 backend -------------------------------------------------------
    using Vec = __m128;
    Vec v;

    SimdF() : v(_mm_setzero_ps()) {}
    explicit SimdF(float x) : v(_mm_set1_ps(x)) {}
    SimdF(Vec x) : v(x) {}

    static SimdF load(const float* p) { return SimdF(_mm_loadu_ps(p)); }
    void store(float* p) const { _mm_storeu_ps(p, v); }

    SimdF operator+(const SimdF& o) const { return SimdF(_mm_add_ps(v, o.v)); }
    SimdF operator-(const SimdF& o) const { return SimdF(_mm_sub_ps(v, o.v)); }
    SimdF operator*(const SimdF& o) const { return SimdF(_mm_mul_ps(v, o.v)); }

    // --- interleave/deinterleave (2 frames × 2 channels → 4 lanes) ----------
    // lane0=L[0], lane1=R[0], lane2=L[1], lane3=R[1]
    static SimdF interleaveStereo(const float* L, const float* R) {
        // Build {L[0], L[1], R[0], R[1]} then shuffle to {L[0],R[0],L[1],R[1]}
        __m128 vL = _mm_loadl_pi(_mm_setzero_ps(), reinterpret_cast<const __m64*>(L)); // {L[0],L[1],0,0}
        __m128 vR = _mm_loadl_pi(_mm_setzero_ps(), reinterpret_cast<const __m64*>(R)); // {R[0],R[1],0,0}
        // unpack low: {L[0],R[0],L[1],R[1]}
        return SimdF(_mm_unpacklo_ps(vL, vR));
    }

    static void deinterleaveStereo(const SimdF& in, float* L, float* R) {
        // in = {L[0], R[0], L[1], R[1]}
        // Extract even lanes (L) and odd lanes (R) into lower 64 bits for storel_pi.
        // _MM_SHUFFLE(w,z,y,x): dst[0]=src[x], dst[1]=src[y], dst[2]=src[z], dst[3]=src[w]
        // L: want { in[0], in[2] } → _MM_SHUFFLE(*,*,2,0)
        // R: want { in[1], in[3] } → _MM_SHUFFLE(*,*,3,1)
        __m128 lpack = _mm_shuffle_ps(in.v, in.v, _MM_SHUFFLE(2,0,2,0));
        __m128 rpack = _mm_shuffle_ps(in.v, in.v, _MM_SHUFFLE(3,1,3,1));
        _mm_storel_pi(reinterpret_cast<__m64*>(L), lpack);
        _mm_storel_pi(reinterpret_cast<__m64*>(R), rpack);
    }

#else
    // ---- Scalar fallback ----------------------------------------------------
    float v[4];

    SimdF() { v[0]=v[1]=v[2]=v[3]=0.0f; }
    explicit SimdF(float x) { v[0]=v[1]=v[2]=v[3]=x; }

    static SimdF load(const float* p) {
        SimdF r;
        r.v[0]=p[0]; r.v[1]=p[1]; r.v[2]=p[2]; r.v[3]=p[3];
        return r;
    }
    void store(float* p) const {
        p[0]=v[0]; p[1]=v[1]; p[2]=v[2]; p[3]=v[3];
    }

    SimdF operator+(const SimdF& o) const {
        SimdF r; r.v[0]=v[0]+o.v[0]; r.v[1]=v[1]+o.v[1]; r.v[2]=v[2]+o.v[2]; r.v[3]=v[3]+o.v[3];
        return r;
    }
    SimdF operator-(const SimdF& o) const {
        SimdF r; r.v[0]=v[0]-o.v[0]; r.v[1]=v[1]-o.v[1]; r.v[2]=v[2]-o.v[2]; r.v[3]=v[3]-o.v[3];
        return r;
    }
    SimdF operator*(const SimdF& o) const {
        SimdF r; r.v[0]=v[0]*o.v[0]; r.v[1]=v[1]*o.v[1]; r.v[2]=v[2]*o.v[2]; r.v[3]=v[3]*o.v[3];
        return r;
    }

    // --- interleave/deinterleave (2 frames × 2 channels → 4 lanes) ----------
    // lane0=L[0], lane1=R[0], lane2=L[1], lane3=R[1]
    static SimdF interleaveStereo(const float* L, const float* R) {
        SimdF r;
        r.v[0]=L[0]; r.v[1]=R[0]; r.v[2]=L[1]; r.v[3]=R[1];
        return r;
    }

    static void deinterleaveStereo(const SimdF& in, float* L, float* R) {
        L[0]=in.v[0]; L[1]=in.v[2];
        R[0]=in.v[1]; R[1]=in.v[3];
    }
#endif
};
