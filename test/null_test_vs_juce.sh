#!/usr/bin/env bash
# Render the same input wav through Moses-JUCE and Moses-DPF using an offline
# VST3 host, then compare the outputs sample-by-sample.
#
# Requirements:
#   - A VST3 host capable of file-in/file-out rendering. We use carla-single
#     by default; replace with whatever you have on this box (pluginval, a
#     bespoke VST3 host, etc.).
#   - MOSES_JUCE_VST3 pointing at the Moses-JUCE bundle (built natively).
#
# Acceptance: peak residual ≤ -80 dB (relaxable to -60 dB per spec §11).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
FIXTURE="${HERE}/fixtures/input.wav"
DPF_VST3="${HERE}/../build/bin/Moses.vst3"
JUCE_VST3="${MOSES_JUCE_VST3:?Set MOSES_JUCE_VST3 to the Moses-JUCE VST3 bundle}"
THRESHOLD_DB="${THRESHOLD_DB:--80}"

OUT_JUCE="${HERE}/fixtures/out_juce.wav"
OUT_DPF="${HERE}/fixtures/out_dpf.wav"

if [[ ! -f "${FIXTURE}" ]]; then
    echo "Generating test fixture..."
    python3 "${HERE}/fixtures/generate_input.py" "${FIXTURE}"
fi

if command -v carla-single >/dev/null 2>&1; then
    carla-single vst3 "${JUCE_VST3}" "${FIXTURE}" "${OUT_JUCE}"
    carla-single vst3 "${DPF_VST3}"  "${FIXTURE}" "${OUT_DPF}"
else
    echo "ERROR: no carla-single found. Install Carla or adapt this script to" >&2
    echo "       use your local offline VST3 host (pluginval, etc.)." >&2
    exit 2
fi

python3 "${HERE}/compare_wavs.py" "${OUT_JUCE}" "${OUT_DPF}" --threshold-db "${THRESHOLD_DB}"
