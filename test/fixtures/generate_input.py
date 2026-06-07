#!/usr/bin/env python3
"""Generate a 10s stereo 48 kHz wav containing pink-ish noise + log tone sweep.

Pure stdlib so it runs without numpy. The pink approximation is a 1-pole IIR
low-passed white noise, which is good enough for null-testing a multiband.
"""
import math, random, struct, sys, wave

fs       = 48000
duration = 10.0
n        = int(fs * duration)
seed     = 42

rng = random.Random(seed)

# Pink-ish noise: low-pass filtered white.
pink = [0.0] * n
b = 0.0
for i in range(n):
    w = rng.gauss(0.0, 0.2)
    b = 0.99 * b + 0.01 * w
    pink[i] = b * 10.0

# Log sweep 20 Hz → 15 kHz.
sweep = [0.0] * n
phase = 0.0
for i in range(n):
    t = i / fs
    freq = 20.0 * (15000.0 / 20.0) ** (t / duration)
    phase += 2.0 * math.pi * freq / fs
    sweep[i] = 0.2 * math.sin(phase)

# Mix and stereo-spread.
left  = [pink[i] + sweep[i]            for i in range(n)]
right = [pink[i] * 0.9 + sweep[i]      for i in range(n)]

peak = max(max(abs(x) for x in left), max(abs(x) for x in right), 1e-9)
scale = 0.95 / peak

path = sys.argv[1] if len(sys.argv) > 1 else 'input.wav'
with wave.open(path, 'wb') as w:
    w.setnchannels(2)
    w.setsampwidth(2)
    w.setframerate(fs)
    frames = bytearray()
    for i in range(n):
        l = max(-1.0, min(1.0, left[i]  * scale))
        r = max(-1.0, min(1.0, right[i] * scale))
        frames += struct.pack('<hh', int(l * 32767), int(r * 32767))
    w.writeframes(bytes(frames))

print(f'wrote {path}: {n} frames stereo 48000 Hz')
