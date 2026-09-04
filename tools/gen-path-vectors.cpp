// Records what the C++ PathFinder returns for a battery of queries over a
// fixed obstacle world, so rust/tests/path_parity.rs can replay the same
// script against the Rust port and compare node for node.
//
// Built with tools/pathtest first on the include path, so the real pathfinder
// sources compile unmodified against a flat array world. See tools/gen-vectors.sh.

#include "world/level/pathfinder/path_finder.h"
#include "world/level/pathfinder/path.h"
#include "world/entity/entity.h"
#include "world/level/chunk/chunk.h"
#include "world/level/level.h"

#include <cstdio>
#include <cstring>

static const int W = 64, H = 32, D = 64;
static unsigned char g_id[W * H * D];
static unsigned char g_data[W * H * D];

static bool inside(int x, int y, int z) {
    return x >= 0 && x < W && y >= 0 && y < H && z >= 0 && z < D;
}
static int at(int x, int y, int z) { return (y * D + z) * W + x; }

int testGetTile(int x, int y, int z) { return inside(x, y, z) ? g_id[at(x, y, z)] : 0; }
int testGetData(int x, int y, int z) { return inside(x, y, z) ? g_data[at(x, y, z)] : 0; }

static void put(int x, int y, int z, int id, int data = 0) {
    if (!inside(x, y, z)) return;
    g_id[at(x, y, z)] = (unsigned char)id;
    g_data[at(x, y, z)] = (unsigned char)data;
}

// Ground at y=8 everywhere, then the shapes a mob actually gets stuck on.
static void buildWorld() {
    memset(g_id, 0, sizeof(g_id));
    memset(g_data, 0, sizeof(g_data));

    for (int z = 0; z < D; z++)
        for (int x = 0; x < W; x++)
            for (int y = 0; y <= 8; y++)
                put(x, y, z, y == 8 ? BLOCK_GRASS : BLOCK_STONE);

    // A one block step the mob can climb, and a two block wall it cannot.
    for (int z = 4; z < 28; z++) put(20, 9, z, BLOCK_STONE);
    for (int z = 4; z < 28; z++) { put(34, 9, z, BLOCK_STONE); put(34, 10, z, BLOCK_STONE); }

    // A gap in each so a route exists at all.
    put(20, 9, 16, BLOCK_AIR);
    put(34, 9, 22, BLOCK_AIR); put(34, 10, 22, BLOCK_AIR);

    // A pond, a lava pool, a fence line and a door, one hazard per lane.
    for (int z = 34; z < 42; z++)
        for (int x = 6; x < 16; x++) { put(x, 8, z, BLOCK_DIRT); put(x, 9, z, BLOCK_WATER); }
    for (int z = 34; z < 40; z++)
        for (int x = 24; x < 30; x++) { put(x, 8, z, BLOCK_DIRT); put(x, 9, z, BLOCK_LAVA); }
    for (int x = 38; x < 56; x++) put(x, 9, 36, BLOCK_FENCE);
    put(46, 9, 36, BLOCK_DOOR_WOOD, 4);
    put(46, 10, 36, BLOCK_DOOR_WOOD, 8);

    // A staircase up and a four block drop, the two edges of getNodeFor.
    for (int i = 0; i < 6; i++)
        for (int z = 48; z < 56; z++)
            for (int y = 9; y <= 9 + i; y++) put(44 + i, y, z, BLOCK_STONE);
    for (int z = 48; z < 60; z++)
        for (int x = 4; x < 14; x++) { put(x, 8, z, BLOCK_AIR); put(x, 4, z, BLOCK_STONE); }
}

struct Query {
    float ex, ey, ez, width, height;
    bool inWater, avoidWater;
    int tx, ty, tz;
    float maxDist;
};

int main() {
    buildWorld();

    static Level level;
    static PathFinder finder;
    finder.setLevel(&level);

    // Mob shapes, two sizes so the isFree box is exercised at 1 and 2 wide.
    const float SMALL_W = 0.6f, SMALL_H = 1.8f;
    const float BIG_W = 1.4f, BIG_H = 1.6f;

    Query qs[] = {
        // straight line over open ground
        { 4.5f, 9.0f,  4.5f, SMALL_W, SMALL_H, false, false, 16,  9,  4, 32.0f },
        // through the climbable step
        { 4.5f, 9.0f, 16.5f, SMALL_W, SMALL_H, false, false, 30,  9, 16, 40.0f },
        // into the two block wall, no route without the gap
        {24.5f, 9.0f, 10.5f, SMALL_W, SMALL_H, false, false, 40,  9, 10, 40.0f },
        // the same wall with the gap in reach
        {24.5f, 9.0f, 21.5f, SMALL_W, SMALL_H, false, false, 40,  9, 22, 40.0f },
        // across the pond, allowed to swim
        { 4.5f, 9.0f, 37.5f, SMALL_W, SMALL_H, false, false, 18,  9, 37, 32.0f },
        // the same crossing while avoiding water
        { 4.5f, 9.0f, 37.5f, SMALL_W, SMALL_H, false, true,  18,  9, 37, 32.0f },
        // starting in the water
        {10.5f, 9.0f, 37.5f, SMALL_W, SMALL_H, true,  false, 20,  9, 37, 32.0f },
        // lava in the way
        {20.5f, 9.0f, 36.5f, SMALL_W, SMALL_H, false, false, 33,  9, 36, 32.0f },
        // fence line with a single open door
        {42.5f, 9.0f, 32.5f, SMALL_W, SMALL_H, false, false, 42,  9, 40, 32.0f },
        // up the staircase
        {40.5f, 9.0f, 51.5f, SMALL_W, SMALL_H, false, false, 52, 15, 51, 32.0f },
        // over the four block drop
        { 2.5f, 9.0f, 53.5f, SMALL_W, SMALL_H, false, false, 16,  9, 53, 32.0f },
        // a wide mob through the same gap
        {24.5f, 9.0f, 21.5f, BIG_W,   BIG_H,   false, false, 40,  9, 22, 40.0f },
        // a wide mob at the climbable step
        { 4.5f, 9.0f, 16.5f, BIG_W,   BIG_H,   false, false, 30,  9, 16, 40.0f },
        // a target with nothing under it, the downward probe
        { 8.5f, 9.0f, 50.5f, SMALL_W, SMALL_H, false, false,  9, 12, 53, 32.0f },
        // target out of range, the closest approach fallback
        { 4.5f, 9.0f,  4.5f, SMALL_W, SMALL_H, false, false, 60,  9, 60,  8.0f },
    };

    const int n = (int)(sizeof(qs) / sizeof(qs[0]));
    for (int i = 0; i < n; i++) {
        const Query& q = qs[i];
        Entity e;
        e.x = q.ex; e.y = q.ey; e.z = q.ez;
        e.bbWidth = q.width; e.bbHeight = q.height;
        e.inWater = q.inWater;
        e.bb.x0 = q.ex - q.width / 2;  e.bb.x1 = q.ex + q.width / 2;
        e.bb.y0 = q.ey;                e.bb.y1 = q.ey + q.height;
        e.bb.z0 = q.ez - q.width / 2;  e.bb.z1 = q.ez + q.width / 2;

        Path path;
        finder.avoidWater = q.avoidWater;
        bool found = finder.findPath(&path, &e, q.tx, q.ty, q.tz, q.maxDist);

        printf("Q %d %d %d\n", i, found ? 1 : 0, path.getSize());
        for (int k = 0; k < path.getSize(); k++) {
            int px, py, pz;
            path.getPoint(k, px, py, pz);
            printf("P %d %d %d %d\n", k, px, py, pz);
        }
    }
    return 0;
}
