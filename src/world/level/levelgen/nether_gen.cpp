// Not built. The Rust port in rust/src/ replaces this, see
// src/world/level/levelgen/gen_bridge.cpp. It stays in the tree because
// tools/gen-vectors.sh compiles it to produce the vectors the port is
// checked against, so editing it here changes nothing until the port follows.

#include "world/level/levelgen/nether_gen.h"
#include "world/level/levelgen/PerlinNoise.h"
#include "world/level/levelgen/Random.h"
#include "world/level/world.h"
#include "world/level/chunk/chunk.h"

#include <algorithm>
#include <cmath>
#include <cstring>

// Noise is sampled on a 5x17x5 grid per chunk, so a cell spans 4 blocks across
// and 8 up. Vanilla's nether uses the same grid and the constants below with it.
#define NG_XS   5
#define NG_YS   17
#define NG_CW   4
#define NG_CH   8

// Vanilla's dominant octave spans 191 blocks, and this world is only 256 square, so
// at vanilla's scale the whole nether is one lobe. Compressing the horizontal input
// fits several caverns in instead.
#define NG_SQUEEZE  3.0f
#define NG_SCALE_XZ (684.412f * NG_SQUEEZE)
#define NG_SCALE_Y  2053.236f

// Whatever the density leaves open under this height floods with lava.
#define NG_LAVA_LEVEL 32

// Vanilla only swaps exposed netherrack for gravel or soul sand in this band
// around its sea level, and leaves every other exposed face alone.
#define NG_SURFACE_LO 60
#define NG_SURFACE_HI 65

// Every height level is rescaled to this spread, which is roughly what vanilla's
// density carries and so keeps the ripple's share of it the same.
#define NG_SPREAD 20.0f

// How far above the median the solid threshold sits, in those units. It sits well
// ahead of the openness that survives, since an isolated low corner of a 4x8x4 cell
// only opens a sliver of that cell.
#define NG_OPENNESS 8.0f

namespace {

struct NetherGen {
    Random rnd, decoRnd;
    PerlinNoise lperlin1, lperlin2, selector, soulNoise, gravelNoise;
    float *ar, *br, *sel, *soul, *gravel;
    float density[NG_XS * NG_XS * NG_YS];
    float ripple[NG_YS];
    float cap[NG_YS];
    float mid[NG_YS];
    float gain[NG_YS];

    NetherGen(long seed)
      : rnd(seed), decoRnd(seed),
        lperlin1(&rnd, 16), lperlin2(&rnd, 16), selector(&rnd, 8),
        soulNoise(&rnd, 4), gravelNoise(&rnd, 4),
        ar(0), br(0), sel(0), soul(0), gravel(0) {

        // The cosine gives the nether its stacked terraces. The cubic seals both
        // ends outright, so it is kept apart from what calibration is allowed to undo.
        for (int y = 0; y < NG_YS; y++) {
            ripple[y] = cosf((float)y * 3.14159265f * 6.0f / (float)NG_YS) * 2.0f;
            cap[y] = 0.0f;
            mid[y] = 0.0f;
            gain[y] = 1.0f;
            float d = (y > NG_YS / 2) ? (float)(NG_YS - 1 - y) : (float)y;
            if (d < 4.0f) { d = 4.0f - d; cap[y] = -d * d * d * 10.0f; }
        }
    }
    ~NetherGen() { delete[] ar; delete[] br; delete[] sel; delete[] soul; delete[] gravel; }

    void calibrate();
    void computeDensity(int cx, int cz);
    void fillColumns(World* w, int cx, int cz);
    void buildSurfaces(World* w, int cx, int cz);
    void decorate(World* w, int cx, int cz);
};

// The largest octave is longer than this world is wide and taller than it is deep,
// so a fixed 256 block square sits inside a single lobe of it and comes out solid
// rock. Vanilla leans on an endless world to average that out. Here the field is
// sampled once over the whole world and each height level is recentred and rescaled
// to a common spread, which is what keeps the caverns connected at every height.
void NetherGen::calibrate() {
    const int N = 17;
    const float xs = (float)(WORLD_W / NG_CW) / (float)(N - 1);
    const float ys = (float)(NG_YS - 1) / (float)(N - 1);
    const float S = NG_SCALE_XZ, SY = NG_SCALE_Y;

    float *ca = 0, *cb = 0, *cs = 0;
    cs = selector.getRegion(cs, 0, 0, 0, N, N, N, S / 80.0f * xs, SY / 60.0f * ys, S / 80.0f * xs);
    ca = lperlin1.getRegion(ca, 0, 0, 0, N, N, N, S * xs, SY * ys, S * xs);
    cb = lperlin2.getRegion(cb, 0, 0, 0, N, N, N, S * xs, SY * ys, S * xs);

    float level[N * N];
    for (int iy = 0; iy < NG_YS; iy++) {
        for (int ix = 0; ix < N; ix++)
            for (int iz = 0; iz < N; iz++) {
                int i = (ix * N + iz) * N + iy;
                float lo = ca[i] / 512.0f, hi = cb[i] / 512.0f;
                float t  = (cs[i] / 10.0f + 1.0f) / 2.0f;
                level[ix * N + iz] = (t < 0.0f) ? lo : (t > 1.0f) ? hi : lo + (hi - lo) * t;
            }
        std::sort(level, level + N * N);
        // The 16th to 84th span is two standard deviations, without the tails'
        // say over it that the plain deviation would give them.
        float sd = (level[(int)(0.84f * N * N)] - level[(int)(0.16f * N * N)]) * 0.5f;
        mid[iy]  = level[N * N / 2];
        gain[iy] = (sd > 1e-4f) ? NG_SPREAD / sd : 1.0f;
    }
    delete[] ca; delete[] cb; delete[] cs;
}

void NetherGen::computeDensity(int cx, int cz) {
    const float S = NG_SCALE_XZ, SY = NG_SCALE_Y;
    float x = (float)(cx * (NG_XS - 1)), z = (float)(cz * (NG_XS - 1));

    sel = selector.getRegion(sel, x, 0.0f, z, NG_XS, NG_YS, NG_XS,
                             S / 80.0f, SY / 60.0f, S / 80.0f);
    ar  = lperlin1.getRegion(ar, x, 0.0f, z, NG_XS, NG_YS, NG_XS, S, SY, S);
    br  = lperlin2.getRegion(br, x, 0.0f, z, NG_XS, NG_YS, NG_XS, S, SY, S);

    int p = 0;
    for (int ix = 0; ix < NG_XS; ix++)
        for (int iz = 0; iz < NG_XS; iz++)
            for (int iy = 0; iy < NG_YS; iy++, p++) {
                float lo = ar[p] / 512.0f, hi = br[p] / 512.0f;
                float t  = (sel[p] / 10.0f + 1.0f) / 2.0f;
                float v  = (t < 0.0f) ? lo : (t > 1.0f) ? hi : lo + (hi - lo) * t;
                v = (v - mid[iy]) * gain[iy] - NG_OPENNESS;
                v -= ripple[iy];
                v -= cap[iy];
                if (iy > NG_YS - 4) {
                    float s = (float)(iy - (NG_YS - 4)) / 3.0f;
                    v = v * (1.0f - s) + -10.0f * s;
                }
                density[p] = v;
            }
}

void NetherGen::fillColumns(World* w, int cx, int cz) {
    for (int xc = 0; xc < NG_XS - 1; xc++)
    for (int zc = 0; zc < NG_XS - 1; zc++)
    for (int yc = 0; yc < NG_YS - 1; yc++) {
        const float yStep = 1.0f / (float)NG_CH;
        int i00 = ((xc + 0) * NG_XS + (zc + 0)) * NG_YS + yc;
        int i01 = ((xc + 0) * NG_XS + (zc + 1)) * NG_YS + yc;
        int i10 = ((xc + 1) * NG_XS + (zc + 0)) * NG_YS + yc;
        int i11 = ((xc + 1) * NG_XS + (zc + 1)) * NG_YS + yc;

        float s0 = density[i00], s1 = density[i01], s2 = density[i10], s3 = density[i11];
        float s0a = (density[i00 + 1] - s0) * yStep;
        float s1a = (density[i01 + 1] - s1) * yStep;
        float s2a = (density[i10 + 1] - s2) * yStep;
        float s3a = (density[i11 + 1] - s3) * yStep;

        for (int y = 0; y < NG_CH; y++) {
            const float xStep = 1.0f / (float)NG_CW;
            float a0 = s0, a1 = s1;
            float a0a = (s2 - s0) * xStep, a1a = (s3 - s1) * xStep;

            for (int x = 0; x < NG_CW; x++) {
                const float zStep = 1.0f / (float)NG_CW;
                float val = a0, vala = (a1 - a0) * zStep;

                for (int z = 0; z < NG_CW; z++) {
                    int gx = cx * 16 + xc * NG_CW + x;
                    int gz = cz * 16 + zc * NG_CW + z;
                    int gy = yc * NG_CH + y;

                    unsigned char id = BLOCK_AIR;
                    if (gy < NG_LAVA_LEVEL) id = BLOCK_CALM_LAVA;
                    if (val > 0.0f)         id = BLOCK_NETHERRACK;
                    blockPut(w, gx, gy, gz, id);

                    val += vala;
                }
                a0 += a0a; a1 += a1a;
            }
            s0 += s0a; s1 += s1a; s2 += s2a; s3 += s3a;
        }
    }
}

void NetherGen::buildSurfaces(World* w, int cx, int cz) {
    const float s = 1.0f / 32.0f;
    rnd.setSeed((long)(cx * 341872712l + cz * 132899541l));
    soul   = soulNoise.getRegion(soul,     (float)(cx * 16), (float)(cz * 16), 0.0f,
                                 16, 16, 1, s, s, 1.0f);
    gravel = gravelNoise.getRegion(gravel, (float)(cx * 16), 109.0134f, (float)(cz * 16),
                                   16, 1, 16, s, 1.0f, s);

    for (int x = 0; x < 16; x++)
    for (int z = 0; z < 16; z++) {
        bool wantSoul   = (soul[z + x * 16]   + rnd.nextFloat() * 0.2f) > 0.0f;
        bool wantGravel = (gravel[z + x * 16] + rnd.nextFloat() * 0.2f) > 0.0f;
        int  gx = cx * 16 + x, gz = cz * 16 + z;

        // Counts down through the top of each exposed run, which is where vanilla
        // swaps netherrack for the patch material. The pair is only reassigned
        // inside the band, so a choice made there carries on down the column.
        int run = -1;
        unsigned char top = BLOCK_NETHERRACK, filler = BLOCK_NETHERRACK;

        for (int y = WORLD_H - 1; y >= 0; y--) {
            if (y >= WORLD_H - 1 - decoRnd.nextInt(5) || y <= decoRnd.nextInt(5)) {
                blockPut(w, gx, y, gz, BLOCK_BEDROCK);
                continue;
            }
            unsigned char id = worldBlock(w, gx, y, gz);
            if (id == BLOCK_AIR || id == BLOCK_CALM_LAVA) { run = -1; continue; }
            if (id != BLOCK_NETHERRACK) continue;

            if (run == -1) {
                if (y >= NG_SURFACE_LO && y <= NG_SURFACE_HI) {
                    top = BLOCK_NETHERRACK; filler = BLOCK_NETHERRACK;
                    if (wantGravel) { top = BLOCK_GRAVEL;    filler = BLOCK_NETHERRACK; }
                    if (wantSoul)   { top = BLOCK_SOUL_SAND; filler = BLOCK_SOUL_SAND; }
                }
                if (top == BLOCK_NETHERRACK) { run = 0; continue; }
                blockPut(w, gx, y, gz, top);
                // Netherrack filler would only write back the block already there.
                run = (filler == BLOCK_NETHERRACK) ? 0 : 3;
                continue;
            }
            if (run > 0) { blockPut(w, gx, y, gz, filler); run--; }
        }
    }
}

void NetherGen::decorate(World* w, int cx, int cz) {
    decoRnd.setSeed((long)(cx * 93781121l + cz * 65432197l));

    // Glowstone hangs from whatever ceiling the blob finds, which is the only light
    // down here besides the lava. The seed block is what the growth rule counts from.
    for (int n = 0; n < 10; n++) {
        int bx = cx * 16 + decoRnd.nextInt(16);
        int bz = cz * 16 + decoRnd.nextInt(16);
        int by = 4 + decoRnd.nextInt(WORLD_H - 12);
        if (worldBlock(w, bx, by, bz) != BLOCK_AIR) continue;
        if (worldBlock(w, bx, by + 1, bz) != BLOCK_NETHERRACK) continue;
        blockPut(w, bx, by, bz, BLOCK_GLOWSTONE);

        for (int i = 0; i < 1500; i++) {
            int px = bx + decoRnd.nextInt(8) - decoRnd.nextInt(8);
            int py = by - decoRnd.nextInt(12);
            int pz = bz + decoRnd.nextInt(8) - decoRnd.nextInt(8);
            if (px < 0 || pz < 0 || px >= WORLD_W || pz >= WORLD_D || py < 1) continue;
            if (worldBlock(w, px, py, pz) != BLOCK_AIR) continue;

            int touching = 0;
            if (worldBlock(w, px - 1, py, pz) == BLOCK_GLOWSTONE) touching++;
            if (worldBlock(w, px + 1, py, pz) == BLOCK_GLOWSTONE) touching++;
            if (worldBlock(w, px, py, pz - 1) == BLOCK_GLOWSTONE) touching++;
            if (worldBlock(w, px, py, pz + 1) == BLOCK_GLOWSTONE) touching++;
            if (worldBlock(w, px, py - 1, pz) == BLOCK_GLOWSTONE) touching++;
            if (worldBlock(w, px, py + 1, pz) == BLOCK_GLOWSTONE) touching++;
            if (touching != 1) continue;
            blockPut(w, px, py, pz, BLOCK_GLOWSTONE);
        }
    }

    // Open fires on the netherrack shores. Each seed scatters, since a lone point
    // sample almost never lands on a floor.
    for (int n = 0; n < 8; n++) {
        int bx = cx * 16 + decoRnd.nextInt(16);
        int bz = cz * 16 + decoRnd.nextInt(16);
        int by = 4 + decoRnd.nextInt(WORLD_H - 12);

        for (int i = 0; i < 64; i++) {
            int px = bx + decoRnd.nextInt(8) - decoRnd.nextInt(8);
            int py = by + decoRnd.nextInt(4) - decoRnd.nextInt(4);
            int pz = bz + decoRnd.nextInt(8) - decoRnd.nextInt(8);
            if (px < 0 || pz < 0 || px >= WORLD_W || pz >= WORLD_D || py < 1) continue;
            if (worldBlock(w, px, py, pz) != BLOCK_AIR) continue;
            if (worldBlock(w, px, py - 1, pz) != BLOCK_NETHERRACK) continue;
            blockPut(w, px, py, pz, BLOCK_FIRE);
        }
    }
}

NetherGen* s_gen = 0;

}

void netherGenInit(long seed) {
    netherGenFree();
    s_gen = new NetherGen(seed);
    s_gen->calibrate();
}

void netherGenFree() {
    delete s_gen; s_gen = 0;
}

void netherGenerateChunk(World* w, int cx, int cz) {
    if (!s_gen) netherGenInit(0);
    s_gen->computeDensity(cx, cz);
    s_gen->fillColumns(w, cx, cz);
    s_gen->buildSurfaces(w, cx, cz);
    s_gen->decorate(w, cx, cz);
}
