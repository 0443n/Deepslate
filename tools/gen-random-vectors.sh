#!/bin/sh
# Regenerates rust/tests/random_vectors.txt from the C++ Random. Run this only
# when Random.h itself changes, and expect the Rust test to fail if it does.
set -e
cd "$(dirname "$0")/.."
g++ -O2 -Isrc -o /tmp/ds-gen-random-vectors tools/gen-random-vectors.cpp
/tmp/ds-gen-random-vectors > rust/tests/random_vectors.txt
rm -f /tmp/ds-gen-random-vectors
wc -l rust/tests/random_vectors.txt
