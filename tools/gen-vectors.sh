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

g++ -O2 -Itools/testworld -Isrc -o "$OUT" tools/gen-feature-vectors.cpp \
    "$SRC/features_common.cpp" "$SRC/feature_tree_oak.cpp" "$SRC/feature_tree_birch.cpp" \
    "$SRC/feature_tree_spruce.cpp" "$SRC/feature_tree_pine.cpp" "$SRC/feature_flower.cpp" \
    "$SRC/feature_mushroom.cpp" "$SRC/feature_cactus.cpp" "$SRC/feature_reeds.cpp" \
    "$SRC/feature_ore.cpp" "$SRC/feature_clay.cpp" "$SRC/feature_spring.cpp" \
    "$SRC/feature_lake.cpp" "$SRC/feature_snow.cpp" "$SRC/biome.cpp" \
    "$SRC/caves.cpp" "$SRC/mcpegen.cpp" "$SRC/gen_features.cpp" \
    "$SRC/ImprovedNoise.cpp" "$SRC/PerlinNoise.cpp" "$SRC/Synth.cpp"
"$OUT" > rust/tests/feature_vectors.txt

rm -f "$OUT"
wc -l rust/tests/*_vectors.txt
