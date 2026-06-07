#!/usr/bin/env python3
"""Compare two 16-bit PCM wav files sample-by-sample and report peak/RMS
residual in dB. Exit 0 if peak ≤ -80 dB, 1 otherwise.

Usage:  compare_wavs.py <reference.wav> <test.wav> [--threshold-db -80]
"""
import math, struct, sys, wave

def read_wav(path):
    with wave.open(path, 'rb') as w:
        nchan = w.getnchannels()
        width = w.getsampwidth()
        fs    = w.getframerate()
        nframes = w.getnframes()
        raw = w.readframes(nframes)
    assert width == 2, f'{path}: expected 16-bit PCM, got width={width}'
    samples = struct.unpack('<' + 'h' * (len(raw) // 2), raw)
    return nchan, fs, [s / 32768.0 for s in samples]

threshold_db = -80.0
args = sys.argv[1:]
if '--threshold-db' in args:
    idx = args.index('--threshold-db')
    threshold_db = float(args[idx + 1])
    del args[idx:idx + 2]

a_ch, a_fs, a = read_wav(args[0])
b_ch, b_fs, b = read_wav(args[1])
assert a_ch == b_ch and a_fs == b_fs, \
    f'header mismatch: {a_ch}ch@{a_fs}Hz vs {b_ch}ch@{b_fs}Hz'

n = min(len(a), len(b))
peak = 0.0
sqsum = 0.0
for i in range(n):
    d = a[i] - b[i]
    if abs(d) > peak: peak = abs(d)
    sqsum += d * d
rms = math.sqrt(sqsum / n) if n else 1e-12

def db(x): return 20.0 * math.log10(max(x, 1e-12))

print(f'peak residual: {db(peak):+.2f} dB')
print(f'RMS  residual: {db(rms):+.2f} dB')
print(f'threshold:     {threshold_db:+.2f} dB peak')

sys.exit(0 if db(peak) <= threshold_db else 1)
