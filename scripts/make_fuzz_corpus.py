#!/usr/bin/env python3
"""Turn tests/data/golden/frames.txt into a libFuzzer seed corpus.

Each seed is: one chunk-size byte (the fuzz target uses data[0] % 64 + 1) followed by
the frame bytes, plus a few multi-frame and corrupted variants.
Usage: scripts/make_fuzz_corpus.py [output_dir]   (default: fuzz/corpus)
"""
import pathlib
import sys

root = pathlib.Path(__file__).resolve().parent.parent
out = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else root / "fuzz" / "corpus"
out.mkdir(parents=True, exist_ok=True)

frames = {}
for line in (root / "tests" / "data" / "golden" / "frames.txt").read_text().splitlines():
    if not line or line.startswith("#"):
        continue
    name, hex_bytes = line.split()
    frames[name] = bytes.fromhex(hex_bytes)

for name, frame in frames.items():
    (out / name).write_bytes(bytes([7]) + frame)

stream = b"".join(frames.values())
(out / "all_frames_chunk1").write_bytes(bytes([0]) + stream)
(out / "noise_then_frames").write_bytes(bytes([12]) + b"\xa5\x5a\x01\x01\x0f\xa0garbage" + stream)
corrupted = bytearray(frames["sample_basic"])
corrupted[10] ^= 0xFF
(out / "corrupted_then_valid").write_bytes(bytes([3]) + bytes(corrupted) + frames["heartbeat"])
print(f"wrote {len(list(out.iterdir()))} seeds to {out}")
