// Dumps ImprovedNoise and PerlinNoise output so the Rust port can be checked
// against it. Every float prints as its bit pattern. Driven by
// tools/gen-vectors.sh.

#include "world/level/levelgen/ImprovedNoise.h"
#include "world/level/levelgen/PerlinNoise.h"
#include "world/level/levelgen/Random.h"

#include <cstdio>
#include <cstring>

static unsigned int bits(float f) {
    unsigned int u;
    std::memcpy(&u, &f, sizeof u);
    return u;
}

static void dumpImproved(long seed) {
    Random r(seed);
    ImprovedNoise n(&r);
    std::printf("improved %ld %08x %08x %08x\n", seed, bits(n.xo), bits(n.yo), bits(n.zo));

    // Negative, fractional and far-from-origin samples all take different
    // branches through the floor and the wrap.
    static const float coords[] = { 0.0f, 0.5f, 1.0f, -0.5f, -1.0f, 3.25f,
                                    -7.75f, 255.5f, 256.5f, -256.5f, 1000.125f };
    const int nc = sizeof coords / sizeof *coords;
    for (int i = 0; i < nc; i++)
        for (int j = 0; j < nc; j++)
            for (int k = 0; k < nc; k += 3)
                std::printf("n %08x\n", bits(n.noise(coords[i], coords[j], coords[k])));

    // The flat branch of add, which reaches for grad2 where the other does not.
    float flat[8 * 1 * 6];
    for (int i = 0; i < 8 * 6; i++) flat[i] = 0.0f;
    n.add(flat, 3.0f, 10.0f, -5.0f, 8, 1, 6, 0.25f, 1.0f, 0.5f, 2.0f);
    for (int i = 0; i < 8 * 6; i++) std::printf("a %08x\n", bits(flat[i]));

    // The general branch, with ySize big enough to exercise the corner cache.
    float vol[5 * 9 * 5];
    for (int i = 0; i < 5 * 9 * 5; i++) vol[i] = 0.0f;
    n.add(vol, -2.0f, 0.0f, 4.0f, 5, 9, 5, 0.5f, 0.125f, 0.5f, 1.5f);
    for (int i = 0; i < 5 * 9 * 5; i++) std::printf("v %08x\n", bits(vol[i]));
}

static void dumpPerlin(long seed, int levels) {
    Random r(seed);
    PerlinNoise pn(&r, levels);
    std::printf("perlin %ld %d\n", seed, levels);

    for (int i = -4; i <= 4; i++)
        for (int j = -4; j <= 4; j++)
            std::printf("p2 %08x\n", bits(pn.getValue(i * 1.5f, j * 0.75f)));

    for (int i = -3; i <= 3; i++)
        for (int j = -3; j <= 3; j++)
            for (int k = -3; k <= 3; k++)
                std::printf("p3 %08x\n", bits(pn.getValue(i * 1.25f, j * 0.5f, k * 2.0f)));

    float region[5 * 17 * 5];
    pn.getRegion(region, 1.0f, 2.0f, 3.0f, 5, 17, 5, 0.5f, 0.25f, 0.5f);
    for (int i = 0; i < 5 * 17 * 5; i++) std::printf("pr %08x\n", bits(region[i]));

    float flat[16 * 16];
    pn.getRegion(flat, -32, 48, 16, 16, 0.03125f, 0.03125f, 1.0f);
    for (int i = 0; i < 16 * 16; i++) std::printf("pf %08x\n", bits(flat[i]));
}

int main() {
    static const long seeds[] = { 0, 1, -1, 42, 1337, 123456789 };
    for (unsigned i = 0; i < sizeof seeds / sizeof *seeds; i++) dumpImproved(seeds[i]);

    // The level counts McpeGen and NetherGen actually build.
    static const int levels[] = { 2, 4, 8, 10, 16 };
    for (unsigned i = 0; i < sizeof seeds / sizeof *seeds; i++)
        for (unsigned j = 0; j < sizeof levels / sizeof *levels; j++)
            dumpPerlin(seeds[i], levels[j]);
    return 0;
}
