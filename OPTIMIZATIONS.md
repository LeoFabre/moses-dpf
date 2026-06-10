# Optimization notes

This document describes the CPU optimization work done on the Moses DSP for the
production setup on Bela (PocketBeagle2, Cortex-A53, aarch64). Builds use
`-O3 -march=armv8-a -mtune=cortex-a53`. All optimizations were validated with an
offline A/B regression gate (the same audio rendered through two git refs, float
dumps compared in dBFS), unit tests, and real-device benchmarks on the Cortex-A53.

**Bottom line: 1635 → 995 ns/sample end-to-end on the Cortex-A53, a 1.64× speedup**,
with the heaviest single win coming from the Tier-4 NEON vectorization (1.59× on its own).

All of the work described below is merged on `main`.

## What was optimized

### Tier 1 — flush-to-zero denormals (`2a4b649`)

`dsp/DenormalGuard.hpp` arms FPCR.FZ (flush-to-zero) once on the audio thread at
the top of `Plugin::run()`. aarch64-only, no-op on host builds. This protects all
IIR/feedback decay tails from the Cortex-A53 denormal penalty.

### Tier 2 — numerically-equivalent per-sample wins (`2a4b649`)

Rewrites that change no math, only how it is computed:

- drop per-sample zero-init of the band array;
- hoist kill/listen handling out of the mix loop (branchless masked sum);
- branchless `min()` in the gain clamp;
- reciprocal-multiply for `1/a0` in the Linkwitz-Riley recompute;
- `__restrict` on hot buffers.

Validated bit-equivalent (or below −80 dB residual) by the offline gate; all DSP
unit tests stay green.

### Tier 3 — fast-math on the DSP objects only (`2da8d60`)

`-funsafe-math-optimizations` applied to the Moses DSP compilation units only (not
the whole plugin). Gated by the same A/B comparison.

### Tier 4a — collapsed gain computer (`3ba5ed0`)

The compressor's `computeGain` did a dB round-trip per sample — a libm `log10f`
**and** `powf` that largely cancel. It was algebraically collapsed to

```
gain = makeupGain * min(1, (env / threshLin)^(-slope))
```

with `threshLin` / `invThreshLin` / `makeupGain` / `negSlope` precomputed on
parameter change. Per sample this is one branch plus (above threshold) a single
`fastExp2(negSlope * fastLog2(r))`, using Mineiro-style minimax `log2`/`exp2`
approximations built from `+ - *` and one divide. Gain-reduction metering is
recovered from the same `log2`, with no extra transcendental.

Accuracy: worst-case **0.0009 dB** vs the libm formula over a full parameter/envelope
sweep. Engine A/B residual: −101 dB peak on the worst preset, gentle preset
bit-identical.

### Tier 4b — NEON SoA 4-band vectorization (`5ed27a9` foundation, `d81aae5` wiring)

The per-band(outer)/per-sample(inner) loop was restructured to per-sample with all
4 bands as one `SimdF` lane-vector (band index = lane):

- the envelope follower tick (one-pole + attack/release select) runs across the
  4 bands with `vcmpgt`/`vselect` — bit-identical to scalar;
- the collapsed Tier-4a gain computer runs vectorized via 4-lane
  `fastLog2`/`fastExp2`;
- `rebuildBankSimd()` gathers coefficients once per block instead of per sample.

Residual vs the scalar path: **≤ −116.5 dB** worst case; the gentle preset is
bit-identical.

## What was tried and rejected

Nothing significant was abandoned in this repo — the band-parallel structure of a
4-band multiband compressor maps naturally onto 4 NEON lanes, so the vectorization
landed cleanly.

## Validation

- **Offline A/B regression gate**: identical input rendered through two git refs,
  float output dumps compared in dBFS. Thresholds are calibrated: bit-identical
  reads as ≈ −999 dB, sub-audible coefficient-path drift ≈ −85 dB, a real
  regression shows up around −14 dB. Numerically-equivalent tiers must be
  bit-identical or below −80 dB; the output-changing Tier-4 steps were accepted at
  −101 dB / −116.5 dB plus an on-device listening check.
- **Unit tests**: all 9 Moses DSP tests pass at every step; `test_gain_approx`
  asserts the 0.0009 dB bound of the Tier-4a approximation.
- **Real-device benchmarks**: ns/sample measured on the Cortex-A53 itself, not on
  a host machine.
