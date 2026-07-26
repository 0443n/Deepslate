
#include "world/level/tile/tile_behavior.h"
#include "world/level/tile/tile.h"
#include "world/level/level.h"
#include "world/entity/falling_tile.h"
#include <stdlib.h>

static inline bool heavyIsFree(World* w, int x, int y, int z) {
    unsigned char id = worldBlock(w, x, y, z);
    return id == BLOCK_AIR || isLiquidId(id);
}

void heavyTileTick(World* w, int x, int y, int z, unsigned char id) {
    if (y >= 0 && heavyIsFree(w, x, y - 1, z)) {
        FallingTile* e = new FallingTile(&g_level, x + 0.5f, y + 0.5f, z + 0.5f,
                                         id, worldData(w, x, y, z));
        g_level.addEntity(e);
    }
}

bool tileMayPlace(World* w, unsigned char id, int x, int y, int z, int face) {
    Tile* t = Tile::tiles[id];
    BlockAABB boxes[3];
    int n = t->getAABB(w, x, y, z, boxes);
    for (int i = 0; i < n; i++) {
        AABB box(boxes[i].x0, boxes[i].y0, boxes[i].z0,
                 boxes[i].x1, boxes[i].y1, boxes[i].z1);
        if (!g_level.isUnobstructed(box)) return false;
    }
    return t->mayPlace(w, x, y, z, face);
}

void tileNeighborChanged(World* w, int x, int y, int z) {

    Tile::tiles[worldBlock(w, x, y, z)]->neighborChanged(w, x, y, z);
}

static unsigned int s_randValue = 42184323;

typedef char assert_ref_dims[(CHUNK_SX == 16 && CHUNK_SZ == 16 &&
                              (WORLD_H & (WORLD_H - 1)) == 0) ? 1 : -1];

void tileRandomTick(World* w) {
    for (int cx = 0; cx < WORLD_CHUNKS_X; cx++)
    for (int cz = 0; cz < WORLD_CHUNKS_Z; cz++) {
        int xo = cx * CHUNK_SX, zo = cz * CHUNK_SZ;
        for (int i = 0; i < 20; i++) {
            s_randValue = s_randValue * 3u + 1013904223u;
            unsigned int val = s_randValue >> 2;
            int x = xo + (int)(val & 15);
            int z = zo + (int)((val >> 8) & 15);
            int y = (int)((val >> 16) & (WORLD_H - 1));
            Tile* t = Tile::tiles[worldBlock(w, x, y, z)];
            if (t->randomTicks) t->randomTick(w, x, y, z);
        }
    }
}
