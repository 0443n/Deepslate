#include "world/level/tile/nether_portal.h"
#include "world/level/world.h"
#include "world/level/chunk/chunk.h"
#include "world/level/tile/tile.h"

// Interior limits, the same ones vanilla uses. Width is measured across the
// portal plane and height up from the floor of the frame.
#define PORTAL_MIN_W 2
#define PORTAL_MAX_W 21
#define PORTAL_MIN_H 3
#define PORTAL_MAX_H 21

namespace {

bool isFrame(unsigned char id) { return id == BLOCK_OBSIDIAN; }

// Fire counts because flint and steel lights the air block by setting it alight.
bool isEmpty(unsigned char id) {
    return id == BLOCK_AIR || id == BLOCK_FIRE || id == BLOCK_PORTAL;
}

struct Shape {
    int dx, dz;
    int x, y, z;
    int w, h;
};

bool frameAt(const World* w, int x, int y, int z) {
    return isFrame(worldBlock(w, x, y, z));
}

// Grows the interior out from one empty block, first sideways to the frame on
// either side, then upward. Every step also checks the wall it runs along, so a
// shape that returns true is a complete frame.
bool measure(const World* w, int x, int y, int z, int dx, int dz, Shape* out) {
    while (y > 0 && isEmpty(worldBlock(w, x, y - 1, z))) --y;
    if (!frameAt(w, x, y - 1, z)) return false;

    int left = 0;
    while (left < PORTAL_MAX_W &&
           isEmpty(worldBlock(w, x - dx * (left + 1), y, z - dz * (left + 1)))) ++left;
    if (!frameAt(w, x - dx * (left + 1), y, z - dz * (left + 1))) return false;

    int right = 0;
    while (left + right + 1 < PORTAL_MAX_W &&
           isEmpty(worldBlock(w, x + dx * (right + 1), y, z + dz * (right + 1)))) ++right;
    if (!frameAt(w, x + dx * (right + 1), y, z + dz * (right + 1))) return false;

    const int width = left + right + 1;
    if (width < PORTAL_MIN_W) return false;

    const int x0 = x - dx * left, z0 = z - dz * left;
    for (int i = 0; i < width; i++)
        if (!frameAt(w, x0 + dx * i, y - 1, z0 + dz * i)) return false;

    int height = 0;
    while (height < PORTAL_MAX_H) {
        const int row = y + height;
        bool blocked = false;
        for (int i = 0; i < width && !blocked; i++)
            if (!isEmpty(worldBlock(w, x0 + dx * i, row, z0 + dz * i))) blocked = true;
        if (blocked) break;
        if (!frameAt(w, x0 - dx, row, z0 - dz)) return false;
        if (!frameAt(w, x0 + dx * width, row, z0 + dz * width)) return false;
        ++height;
    }
    if (height < PORTAL_MIN_H) return false;

    for (int i = 0; i < width; i++)
        if (!frameAt(w, x0 + dx * i, y + height, z0 + dz * i)) return false;

    out->dx = dx; out->dz = dz;
    out->x = x0; out->y = y; out->z = z0;
    out->w = width; out->h = height;
    return true;
}

}

namespace NetherPortal {

bool light(World* w, int x, int y, int z) {
    if (!isEmpty(worldBlock(w, x, y, z))) return false;

    Shape s;
    unsigned char axis;
    if      (measure(w, x, y, z, 1, 0, &s)) axis = PORTAL_AXIS_X;
    else if (measure(w, x, y, z, 0, 1, &s)) axis = PORTAL_AXIS_Z;
    else return false;

    for (int h = 0; h < s.h; h++)
        for (int i = 0; i < s.w; i++)
            worldSetBlockAndData(w, s.x + s.dx * i, s.y + h, s.z + s.dz * i,
                                 BLOCK_PORTAL, axis);

    worldRelightBox(w, s.x - s.dx - 1, s.y - 1, s.z - s.dz - 1,
                       s.x + s.dx * s.w + 1, s.y + s.h, s.z + s.dz * s.w + 1);
    return true;
}

bool framed(const World* w, int x, int y, int z) {
    const int dx = (worldData(w, x, y, z) & PORTAL_AXIS_MASK) == PORTAL_AXIS_X;
    const int dz = 1 - dx;

    static const int kSide[4][3] = { { 0, 1, 0 }, { 0, -1, 0 }, { 1, 0, 1 }, { -1, 0, -1 } };
    for (int i = 0; i < 4; i++) {
        const int nx = x + kSide[i][0] * dx;
        const int ny = y + kSide[i][1];
        const int nz = z + kSide[i][2] * dz;
        const unsigned char nb = worldBlock(w, nx, ny, nz);
        if (nb != BLOCK_PORTAL && !isFrame(nb)) return false;
    }
    return true;
}

// The opening, not the shell. Both loops below run one past it on every side, so
// these give vanilla's 2x3 portal inside a 4x5 obsidian frame.
#define PORTAL_IW 2
#define PORTAL_IH 3

// True when the pocket the frame needs is free and something solid holds it up.
static bool sitsHere(const World* w, int x, int y, int z, int dx, int dz) {
    if (y < 1 || y + PORTAL_IH >= WORLD_H) return false;
    for (int i = -1; i <= PORTAL_IW; i++) {
        int bx = x + dx * i, bz = z + dz * i;
        if (bx < 0 || bz < 0 || bx >= WORLD_W || bz >= WORLD_D) return false;
        if (!Tile::tiles[worldBlock(w, bx, y - 1, bz)]->solidPhys) return false;
        for (int h = 0; h <= PORTAL_IH; h++)
            if (!isEmpty(worldBlock(w, bx, y + h, bz))) return false;
    }
    return true;
}

// Lays the obsidian shell and fills the interior. The pocket is cleared first so
// this also works on the carved fallback site.
static void raise(World* w, int x, int y, int z, int dx, int dz) {
    unsigned char axis = dx ? PORTAL_AXIS_X : PORTAL_AXIS_Z;
    int px = -dz, pz = dx;

    for (int i = -1; i <= PORTAL_IW; i++)
        for (int h = -1; h <= PORTAL_IH; h++)
            for (int s = -1; s <= 1; s++) {
                int bx = x + dx * i + px * s, by = y + h, bz = z + dz * i + pz * s;
                if (bx < 0 || bz < 0 || bx >= WORLD_W || bz >= WORLD_D) continue;
                if (by < 0 || by >= WORLD_H) continue;

                bool shell = (i == -1 || i == PORTAL_IW || h == -1 || h == PORTAL_IH);
                if (s != 0) { if (h == -1) worldSetBlockAndData(w, bx, by, bz, BLOCK_OBSIDIAN, 0);
                              else if (!shell) worldSetBlockAndData(w, bx, by, bz, BLOCK_AIR, 0);
                              continue; }
                if (shell) worldSetBlockAndData(w, bx, by, bz, BLOCK_OBSIDIAN, 0);
                else       worldSetBlockAndData(w, bx, by, bz, BLOCK_PORTAL, axis);
            }

    worldRelightBox(w, x - 2, y - 2, z - 2,
                       x + dx * PORTAL_IW + 2, y + PORTAL_IH + 1, z + dz * PORTAL_IW + 2);
}

void findOrCreate(World* w, int x, int z, int* outX, int* outY, int* outZ) {
    const int R = 24;
    int bestD = 1 << 30, bx = -1, by = 0, bz = 0;
    for (int sx = x - R; sx <= x + R; sx++)
        for (int sz = z - R; sz <= z + R; sz++) {
            if (sx < 0 || sz < 0 || sx >= WORLD_W || sz >= WORLD_D) continue;
            for (int sy = 1; sy < WORLD_H - 1; sy++) {
                if (worldBlock(w, sx, sy, sz) != BLOCK_PORTAL) continue;
                int d = (sx - x) * (sx - x) + (sz - z) * (sz - z);
                if (d < bestD) { bestD = d; bx = sx; by = sy; bz = sz; }
                break;
            }
        }
    if (bx >= 0) { *outX = bx; *outY = by; *outZ = bz; return; }

    // No portal within reach, so pick the highest ledge near the target that the
    // frame fits on, preferring the exact column and widening out from there.
    for (int r = 0; r <= R; r++)
        for (int sx = x - r; sx <= x + r; sx++)
            for (int sz = z - r; sz <= z + r; sz++) {
                if (r > 0 && sx != x - r && sx != x + r && sz != z - r && sz != z + r) continue;
                if (sx < 2 || sz < 2 || sx >= WORLD_W - 2 || sz >= WORLD_D - 2) continue;
                for (int sy = WORLD_H - PORTAL_IH - 2; sy >= 2; sy--) {
                    for (int a = 0; a < 2; a++) {
                        int dx = a ? 0 : 1, dz = a ? 1 : 0;
                        if (!sitsHere(w, sx, sy, sz, dx, dz)) continue;
                        raise(w, sx, sy, sz, dx, dz);
                        *outX = sx; *outY = sy; *outZ = sz;
                        return;
                    }
                }
            }

    // Nothing was open anywhere, so carve a shelf and stand the frame on it.
    int fy = WORLD_H / 2;
    if (x < 2) x = 2;
    if (x > WORLD_W - 8) x = WORLD_W - 8;
    if (z < 2) z = 2;
    if (z > WORLD_D - 8) z = WORLD_D - 8;
    for (int i = -1; i <= PORTAL_IW; i++)
        for (int s = -1; s <= 1; s++)
            worldSetBlockAndData(w, x + i, fy - 1, z + s, BLOCK_OBSIDIAN, 0);
    raise(w, x, fy, z, 1, 0);
    *outX = x; *outY = fy; *outZ = z;
}

void extinguish(World* w, int x, int y, int z) {
    // Flood fill rather than letting each block's neighbour update remove the next,
    // which would recurse once per portal block and is far too deep for the stack.
    struct P { short x, y, z; };
    static P stack[PORTAL_MAX_W * PORTAL_MAX_H];
    int n = 0;

    if (worldBlock(w, x, y, z) != BLOCK_PORTAL) return;
    const int dx = (worldData(w, x, y, z) & PORTAL_AXIS_MASK) == PORTAL_AXIS_X;
    const int dz = 1 - dx;

    stack[n].x = (short)x; stack[n].y = (short)y; stack[n].z = (short)z; n++;
    worldSetBlockAndData(w, x, y, z, BLOCK_AIR, 0);

    int x0 = x, y0 = y, z0 = z, x1 = x, y1 = y, z1 = z;
    static const int kSide[4][3] = { { 0, 1, 0 }, { 0, -1, 0 }, { 1, 0, 1 }, { -1, 0, -1 } };
    while (n > 0) {
        const P p = stack[--n];
        if (p.x < x0) x0 = p.x;
        if (p.x > x1) x1 = p.x;
        if (p.y < y0) y0 = p.y;
        if (p.y > y1) y1 = p.y;
        if (p.z < z0) z0 = p.z;
        if (p.z > z1) z1 = p.z;
        for (int i = 0; i < 4; i++) {
            const int nx = p.x + kSide[i][0] * dx;
            const int ny = p.y + kSide[i][1];
            const int nz = p.z + kSide[i][2] * dz;
            if (worldBlock(w, nx, ny, nz) != BLOCK_PORTAL) continue;
            if (n >= (int)(sizeof stack / sizeof stack[0])) continue;
            worldSetBlockAndData(w, nx, ny, nz, BLOCK_AIR, 0);
            stack[n].x = (short)nx; stack[n].y = (short)ny; stack[n].z = (short)nz; n++;
        }
    }
    worldRelightBox(w, x0 - 1, y0 - 1, z0 - 1, x1 + 1, y1 + 1, z1 + 1);
}

}
