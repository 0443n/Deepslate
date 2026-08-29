#ifndef MCPSP_WORLD_WORLD_H
#define MCPSP_WORLD_WORLD_H

// Test double for the block store, used only by tools/gen-feature-vectors.cpp.
// The real World is a palettized chunk store the host cannot build, so the
// feature sources are compiled against a flat array instead. Every function
// here has a byte-for-byte counterpart in rust/tests/feature_parity.rs, which
// is what makes the two traces comparable.

#include "world/level/chunk/chunk.h"

#include <cstring>

#define WORLD_CHUNKS_X 16
#define WORLD_CHUNKS_Z 16
#define WORLD_W (WORLD_CHUNKS_X * CHUNK_SX)
#define WORLD_H CHUNK_SY
#define WORLD_D (WORLD_CHUNKS_Z * CHUNK_SZ)

struct World {
    unsigned char id[WORLD_W][WORLD_H][WORLD_D];
    unsigned char data[WORLD_W][WORLD_H][WORLD_D];
};

// Recorded by the double so the trace can be replayed against the Rust port.
void traceSet(int x, int y, int z, unsigned char id, unsigned char data);
void traceTick(int x, int y, int z, unsigned char id, int tickDelay);

static inline bool worldReady(const World*, int x, int z) {
    return x >= 0 && x < WORLD_W && z >= 0 && z < WORLD_D;
}

static inline unsigned char worldBlock(const World* w, int x, int y, int z) {
    if (y < 0 || y >= WORLD_H) return BLOCK_AIR;
    if (!worldReady(w, x, z)) return BLOCK_INVISIBLE_BEDROCK;
    return w->id[x][y][z];
}

static inline bool worldCanSeeSky(const World* w, int x, int y, int z) {
    if (y < 0 || !worldReady(w, x, z)) return false;
    for (int yy = y + 1; yy < WORLD_H; yy++)
        if (w->id[x][yy][z] != BLOCK_AIR) return false;
    return true;
}

// Sky or nothing, which is enough to drive both sides of every light test the
// flower and mushroom features make.
static inline int lightRawAt(const World* w, int x, int y, int z) {
    return worldCanSeeSky(w, x, y, z) ? 15 : 0;
}

static inline bool blockPut(World* w, int x, int y, int z, unsigned char id) {
    if (y < 0 || y >= WORLD_H || !worldReady(w, x, z)) return false;
    w->id[x][y][z] = id;
    return true;
}

static inline void blockColumnGet(const World* w, int x, int z, unsigned char* out128) {
    if (!worldReady(w, x, z)) { memset(out128, BLOCK_INVISIBLE_BEDROCK, WORLD_H); return; }
    for (int y = 0; y < WORLD_H; y++) out128[y] = w->id[x][y][z];
}

static inline void blockColumnPut(World* w, int x, int z, const unsigned char* in128) {
    if (!worldReady(w, x, z)) return;
    for (int y = 0; y < WORLD_H; y++) w->id[x][y][z] = in128[y];
}

static inline bool worldFitsInWindow(const World*) { return true; }

#define WORLD_SIZE_CHUNKS WORLD_CHUNKS_X
extern volatile int g_terrainProgress;

static inline bool worldSetBlockAndData(World* w, int x, int y, int z,
                                        unsigned char id, unsigned char data) {
    if (y < 0 || y >= WORLD_H || !worldReady(w, x, z)) return false;
    w->id[x][y][z] = id;
    w->data[x][y][z] = data;
    traceSet(x, y, z, id, data);
    return true;
}

static inline void worldScheduleTick(World* w, int x, int y, int z,
                                     unsigned char id, int tickDelay) {
    (void)w;
    traceTick(x, y, z, id, tickDelay);
}

#endif
