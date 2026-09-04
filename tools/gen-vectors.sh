#!/bin/sh
# Regenerates the vectors in rust/tests that still have C++ to generate them
# from, which is the noise and the Random the game itself keeps using.
#
# feature_vectors.txt has no generator any more, the levelgen C++ it came from
# was deleted once the Rust port replaced it. It is a golden file now, so a
# deliberate generator change means reviewing its diff by hand. Commit 9fcd4fb
# is the last one that still holds the C++ and tools/gen-feature-vectors.cpp.
set -e
cd "$(dirname "$0")/.."

SRC="src/world/level/levelgen"
OUT=/tmp/ds-gen-vectors

g++ -O2 -Isrc -o "$OUT" tools/gen-random-vectors.cpp
"$OUT" > rust/tests/random_vectors.txt

g++ -O2 -Isrc -o "$OUT" tools/gen-noise-vectors.cpp \
    "$SRC/ImprovedNoise.cpp" "$SRC/PerlinNoise.cpp" "$SRC/Synth.cpp"
"$OUT" > rust/tests/noise_vectors.txt

# The pathfinder C++ is still here, compiled against the stub world in
# tools/pathtest rather than the real chunk store.
g++ -O2 -Itools/pathtest -Isrc -o "$OUT" tools/gen-path-vectors.cpp \
    src/world/level/pathfinder/path_finder.cpp src/world/level/pathfinder/path.cpp
"$OUT" > rust/tests/path_vectors.txt

rm -f "$OUT"
wc -l rust/tests/*_vectors.txt
