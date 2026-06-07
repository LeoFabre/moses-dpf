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

## DPF revision

Pinned to DPF commit `4238e1c7f0351bbe488d79f0899c540543ac7583`.
