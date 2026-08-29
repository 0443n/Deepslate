#!/bin/sh
# Regenerates the reference vectors in rust/tests from the C++ they replace.
# Run this only when the C++ changes, and expect the Rust tests to fail if it
# did anything more than move code around.
set -e
cd "$(dirname "$0")/.."

SRC="src/world/level/levelgen"
OUT=/tmp/ds-gen-vectors

g++ -O2 -Isrc -o "$OUT" tools/gen-random-vectors.cpp
"$OUT" > rust/tests/random_vectors.txt

g++ -O2 -Isrc -o "$OUT" tools/gen-noise-vectors.cpp \
    "$SRC/ImprovedNoise.cpp" "$SRC/PerlinNoise.cpp" "$SRC/Synth.cpp"
"$OUT" > rust/tests/noise_vectors.txt

rm -f "$OUT"
wc -l rust/tests/*_vectors.txt
