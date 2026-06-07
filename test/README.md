# Moses-DPF tests

## Unit tests (CTest)

```bash
cmake -B build -DMOSES_BUILD_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

8 standalone executables exercise the DSP modules in isolation.

## Null-test against Moses-JUCE (offline)

`null_test_vs_juce.sh` renders `fixtures/input.wav` through both plugins with
their default presets and compares the outputs. Acceptance: peak residual
≤ -80 dB (relaxable to -60 dB — see spec §11).

```bash
export MOSES_JUCE_VST3=/path/to/Moses-JUCE.vst3
./test/null_test_vs_juce.sh
```

Default host: `carla-single`. If you use a different offline VST3 host, edit
the script accordingly. The fixture is regenerated automatically by
`fixtures/generate_input.py` (pure-stdlib Python, no numpy needed).

The fixture is **not committed** — it's an ephemeral test artifact, regenerated
on demand.
