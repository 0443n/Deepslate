// Dumps draws from the C++ Random so the Rust port can be checked against it.
// Floats print as their bit pattern, since the point is exactness, not
// closeness. Build and run through tools/gen-random-vectors.sh.

#include "world/level/levelgen/Random.h"

#include <cstdio>
#include <cstring>

static unsigned int bits(float f) {
    unsigned int u;
    std::memcpy(&u, &f, sizeof u);
    return u;
}

static void dump(long seed) {
    Random r(seed);
    std::printf("seed %ld\n", seed);

    for (int i = 0; i < 64; i++) std::printf("i %d\n", r.nextInt());
    for (int i = 1; i <= 64; i++) std::printf("b %d %d\n", i, r.nextInt(i));
    for (int i = 0; i < 64; i++) std::printf("f %08x\n", bits(r.nextFloat()));
    for (int i = 0; i < 64; i++) std::printf("o %d\n", r.nextBoolean() ? 1 : 0);
    for (int i = 0; i < 64; i++) std::printf("g %08x\n", bits(r.nextGaussian()));

    // A reseed mid-stream has to reset the gaussian carry as well as the state.
    long reseed = seed ^ 0x5f5f5f5f;
    r.setSeed(reseed);
    std::printf("reseed %ld\n", reseed);
    for (int i = 0; i < 32; i++) std::printf("r %d\n", r.nextInt(1000));
}

int main() {
    static const long seeds[] = { 0, 1, -1, 42, 1337, 5489, 0x7fffffff,
                                  -2147483647L - 1, 123456789, -987654321 };
    for (unsigned i = 0; i < sizeof seeds / sizeof *seeds; i++) dump(seeds[i]);
    return 0;
}
