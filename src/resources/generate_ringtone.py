#!/usr/bin/env python3
"""Generate a simple two-tone phone ringtone WAV.

Output: ringtone.wav next to this script (or first CLI arg).
Format: PCM 16-bit, 16 kHz, mono, ~3 seconds.
"""
from __future__ import annotations

import math
import struct
import sys
import wave
from pathlib import Path

SAMPLE_RATE = 16000
AMPLITUDE = 0.5

def tone(freq_a: float, freq_b: float, duration_s: float) -> bytes:
    samples = int(SAMPLE_RATE * duration_s)
    out = bytearray()
    for i in range(samples):
        t = i / SAMPLE_RATE
        v = AMPLITUDE * 0.5 * (math.sin(2 * math.pi * freq_a * t)
                               + math.sin(2 * math.pi * freq_b * t))
        out += struct.pack("<h", int(v * 32767))
    return bytes(out)

def silence(duration_s: float) -> bytes:
    return b"\x00\x00" * int(SAMPLE_RATE * duration_s)

def main() -> None:
    out_path = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).parent / "ringtone.wav"
    chunks = []
    for _ in range(2):
        chunks.append(tone(480.0, 620.0, 0.4))
        chunks.append(silence(0.2))
        chunks.append(tone(480.0, 620.0, 0.4))
        chunks.append(silence(0.7))
    with wave.open(str(out_path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SAMPLE_RATE)
        w.writeframes(b"".join(chunks))
    print(f"Wrote {out_path} ({out_path.stat().st_size} bytes)")

if __name__ == "__main__":
    main()
