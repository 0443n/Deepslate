#include "world/level/chunk/chunk.h"
#include "world/level/world.h"
#include "world/level/tile/tile.h"

#include <string.h>

// Advanced Cave Culling, after tomcc 2014. Flood fill the open cells of one
// section and record which boundary faces each connected pocket can reach.
// Two faces are linked when some pocket touches both, so the render walk can ask
// "entered through A, can I leave through B" without touching block data.

#define SEC_N   (16 * 16 * SECTION_SY)
#define CELL(x, y, z) ((((x) * 16) + (z)) * SECTION_SY + (y))

#define ST_SOLID 1
#define ST_SEEN  2

static inline bool cellOpaque(const World* w, int x, int y, int z) {
    const Tile* t = Tile::tiles[worldBlock(w, x, y, z)];
    return t && t->opaque;
}

unsigned short sectionVisMask(const World* w, int ox, int y0, int oz) {
    unsigned char uid = 0;
    if (blockSectionUniform(w, ox, y0, oz, &uid)) {
        const Tile* t = Tile::tiles[uid];
        return (t && t->opaque) ? 0 : VIS_ALL;
    }

    unsigned char  st[SEC_N];
    unsigned short stack[SEC_N];

    for (int x = 0; x < 16; x++)
        for (int z = 0; z < 16; z++)
            for (int y = 0; y < SECTION_SY; y++)
                st[CELL(x, y, z)] = cellOpaque(w, ox + x, y0 + y, oz + z) ? ST_SOLID : 0;

    unsigned short mask = 0;

    for (int seed = 0; seed < SEC_N; seed++) {
        if (st[seed]) continue;

        int sp = 0;
        stack[sp++] = (unsigned short)seed;
        st[seed] |= ST_SEEN;
        int faces = 0;

        while (sp) {
            int c = stack[--sp];
            int y = c % SECTION_SY;
            int z = (c / SECTION_SY) & 15;
            int x = c / (SECTION_SY * 16);

            if (x == 0)               faces |= 1 << 0;
            if (x == 15)              faces |= 1 << 1;
            if (y == 0)               faces |= 1 << 2;
            if (y == SECTION_SY - 1)  faces |= 1 << 3;
            if (z == 0)               faces |= 1 << 4;
            if (z == 15)              faces |= 1 << 5;

            if (x > 0)              { int n = CELL(x - 1, y, z); if (!st[n]) { st[n] |= ST_SEEN; stack[sp++] = (unsigned short)n; } }
            if (x < 15)             { int n = CELL(x + 1, y, z); if (!st[n]) { st[n] |= ST_SEEN; stack[sp++] = (unsigned short)n; } }
            if (y > 0)              { int n = CELL(x, y - 1, z); if (!st[n]) { st[n] |= ST_SEEN; stack[sp++] = (unsigned short)n; } }
            if (y < SECTION_SY - 1) { int n = CELL(x, y + 1, z); if (!st[n]) { st[n] |= ST_SEEN; stack[sp++] = (unsigned short)n; } }
            if (z > 0)              { int n = CELL(x, y, z - 1); if (!st[n]) { st[n] |= ST_SEEN; stack[sp++] = (unsigned short)n; } }
            if (z < 15)             { int n = CELL(x, y, z + 1); if (!st[n]) { st[n] |= ST_SEEN; stack[sp++] = (unsigned short)n; } }
        }

        for (int a = 0; a < 6; a++) {
            if (!(faces & (1 << a))) continue;
            for (int b = a + 1; b < 6; b++)
                if (faces & (1 << b)) mask |= (unsigned short)(1u << visPairBit(a, b));
        }
    }
    return mask;
}
