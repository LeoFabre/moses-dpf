# Moses DPF

Port of [Moses](https://github.com/lfabre/Moses) (JUCE) to DPF for headless use on Bela.

## Build

```bash
git clone --recursive <this-repo>
cd moses-dpf
cmake -B build -DMOSES_BUILD_UI=ON   # desktop with UI
cmake --build build -j
```

For headless / embedded (no OpenGL deps):

```bash
cmake -B build -DMOSES_BUILD_UI=OFF
cmake --build build -j
```

Artifacts land in `build/bin/Moses.vst3/` and `build/bin/Moses.lv2/`.

## Shm metering

Moses is the v1 writer of the `/nexus-meters` shared-memory metering contract
(`dsp/MeterShm.hpp`, layout v1 — duplicated per plugin like DenormalGuard; spec:
`docs/superpowers/specs/2026-06-13-meter-shm-pipeline-design.md` in nexus-preamp).

| Param | Index | Symbol | Range | Default |
|---|---|---|---|---|
| Meter Slot | 40 (last) | `meter_slot` | −1..31 (integer) | −1 (off) |

Linear VST3 normalization: `norm = (slot + 1) / 32` (slot 0 → 0.03125, slot 2 → 0.09375).

Slot layout published per block when slot ≥ 0:

- `values[0..3]` — per-band post-comp peak `|sample|` over the block, max across both channels
- `values[4..7]` — per-band gain reduction in dB (≥ 0, 0 = none)

The segment is mapped in the plugin constructor (not `activate()` — sushi may apply
`initial_state` before activation); the slot is claimed lazily from `run()` (atomic RMW,
RT-safe). A runtime slot change claims the new slot without unclaiming the old one
(readers key on `frame_counter` movement; a restart clears stale claims).
`NEXUS_METERS_NAME` overrides the segment name for tests.

## Performance

See [OPTIMIZATIONS.md](OPTIMIZATIONS.md) for the Cortex-A53 optimization work and measured gains.

## DPF revision

Pinned to DPF commit `4238e1c7f0351bbe488d79f0899c540543ac7583`.
