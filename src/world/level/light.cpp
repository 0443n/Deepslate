#include <pspkernel.h>

#include "world/level/world.h"

#include <string.h>
#include <pspkernel.h>

static inline int hmIdx(int x, int z) { return x * WORLD_D + z; }
static inline bool isSkyLitAt(const World* w, int x, int y, int z) {
    if (y >= WORLD_H) return true;
    if (y < 0 || x < 0 || x >= WORLD_W || z < 0 || z >= WORLD_D) return false;
    return y >= w->heightmap[hmIdx(x, z)];
}

static const signed char kLN[6][3] = {{-1,0,0},{1,0,0},{0,-1,0},{0,1,0},{0,0,-1},{0,0,1}};
static inline unsigned int lpack(int x, int y, int z) {
    return ((unsigned)x << 15) | ((unsigned)z << 7) | (unsigned)y;
}
static std::vector<unsigned int> g_lightBfs;

#define LIGHT_BFS_RESERVE   32768
#define LIGHT_QUEUE_RESERVE 32768

static inline unsigned int qpack(int layer, int x, int y, int z) {
    return ((unsigned)layer << 23) | lpack(x, y, z);
}

static void lightFlood(World* w, int layer) {
    size_t head = 0;
    while (head < g_lightBfs.size()) {
        if ((head & 1023) == 0) sceKernelDelayThread(100);
        unsigned int p = g_lightBfs[head++];
        int x = (p >> 15) & 0xFF, z = (p >> 7) & 0xFF, y = p & 0x7F;
        int cur = (layer == 0) ? lightSkyGet(w, x, y, z) : lightBlockGet(w, x, y, z);
        if (cur <= 1) continue;
        for (int d = 0; d < 6; d++) {
            int nx = x + kLN[d][0], ny = y + kLN[d][1], nz = z + kLN[d][2];
            if (nx < 0 || nx >= WORLD_W || ny < 0 || ny >= WORLD_H || nz < 0 || nz >= WORLD_D) continue;
            int op = lightOpacity(worldBlock(w, nx, ny, nz)); if (op < 1) op = 1;
            int nl = cur - op;
            if (nl <= 0) continue;
            int have = (layer == 0) ? lightSkyGet(w, nx, ny, nz) : lightBlockGet(w, nx, ny, nz);
            if (have < nl) {
                if (layer == 0) lightSkySet(w, nx, ny, nz, nl); else lightBlockSet(w, nx, ny, nz, nl);
                g_lightBfs.push_back(lpack(nx, ny, nz));
            }
        }
    }
    g_lightBfs.clear();
}

void worldRecalcHeightmap(World* w) {
    for (int x = 0; x < WORLD_W; x++) {
        if ((x & 31) == 0) sceKernelDelayThread(100);
        for (int z = 0; z < WORLD_D; z++) {
            int hy = 0;
            for (int y = WORLD_H - 1; y >= 0; y--) {
                if (lightOpacity(worldBlock(w, x, y, z)) > 0) { hy = y + 1; break; }
            }
            w->heightmap[hmIdx(x, z)] = (unsigned char)hy;
        }
    }
}

void lightQueuesReserve(World* w) {
    g_lightBfs.reserve(LIGHT_BFS_RESERVE);
    w->lightQueue.reserve(LIGHT_QUEUE_RESERVE);
}

static void lightBfsShrink() {
    std::vector<unsigned int>().swap(g_lightBfs);
    g_lightBfs.reserve(LIGHT_BFS_RESERVE);
}

void worldInitLight(World* w) {
    lightClearAll(w);

    worldRecalcHeightmap(w);
    lightInitSkyFromHeightmap(w);

    g_lightBfs.clear();
    for (int x = 0; x < WORLD_W; x++) {
        if ((x & 31) == 0) {
            sceKernelDelayThread(100);
            g_terrainProgress = 60 + (x * 10) / WORLD_W;
        }
        for (int z = 0; z < WORLD_D; z++)
        for (int y = 0; y < WORLD_H; y++) {
            int s = lightSkyGet(w, x, y, z);
            if (s <= 1) continue;
            for (int d = 0; d < 6; d++) {
                int nx = x + kLN[d][0], ny = y + kLN[d][1], nz = z + kLN[d][2];
                if (nx < 0 || nx >= WORLD_W || ny < 0 || ny >= WORLD_H || nz < 0 || nz >= WORLD_D) continue;
                if (lightOpacity(worldBlock(w, nx, ny, nz)) >= 15) continue;
                if (lightSkyGet(w, nx, ny, nz) < s - 1) { g_lightBfs.push_back(lpack(x, y, z)); break; }
            }
        }
    }
    lightFlood(w, 0);
    g_terrainProgress = 80;

    g_lightBfs.clear();
    for (int x = 0; x < WORLD_W; x++) {
        if ((x & 31) == 0) {
            sceKernelDelayThread(100);
            g_terrainProgress = 80 + (x * 5) / WORLD_W;
        }
        for (int z = 0; z < WORLD_D; z++)
        for (int y = 0; y < WORLD_H; y++) {
            int e = lightEmit(worldBlock(w, x, y, z));
            if (e > 0) { lightBlockSet(w, x, y, z, e); g_lightBfs.push_back(lpack(x, y, z)); }
        }
    }
    lightFlood(w, 1);

    lightBfsShrink();

    lightCompactAll(w);
}

static void lightEnqueue(World* w, int layer, int x, int y, int z) {

    w->lightQueue.push_back(qpack(layer, x, y, z));
}

static void lightUpdateIfOther(World* w, int layer, int x, int y, int z, int expected) {
    if (x < 0 || x >= WORLD_W || y < 0 || y >= WORLD_H || z < 0 || z >= WORLD_D) return;
    if (layer == 0) { if (isSkyLitAt(w, x, y, z)) expected = 15; }
    else { int e = lightEmit(worldBlock(w, x, y, z)); if (e > expected) expected = e; }
    int have = (layer == 0) ? lightSkyGet(w, x, y, z) : lightBlockGet(w, x, y, z);
    if (have != expected) lightEnqueue(w, layer, x, y, z);
}

static void lightUpdateCell(World* w, int layer, int x, int y, int z) {
    int old = (layer == 0) ? lightSkyGet(w, x, y, z) : lightBlockGet(w, x, y, z);
    unsigned char tile = worldBlock(w, x, y, z);
    int opac = lightOpacity(tile); if (opac == 0) opac = 1;
    int emit = 0;
    if (layer == 0) { if (isSkyLitAt(w, x, y, z)) emit = 15; }
    else emit = lightEmit(tile);

    int target;
    if (opac >= 15 && emit == 0) target = 0;
    else {
        int m = 0;
        for (int d = 0; d < 6; d++) {
            int nx = x + kLN[d][0], ny = y + kLN[d][1], nz = z + kLN[d][2];
            int v = (layer == 0) ? lightSkyGet(w, nx, ny, nz) : lightBlockGet(w, nx, ny, nz);
            if (v > m) m = v;
        }
        target = m - opac; if (target < 0) target = 0;
        if (emit > target) target = emit;
    }

    if (old != target) {
        if (layer == 0) lightSkySet(w, x, y, z, target); else lightBlockSet(w, x, y, z, target);
        worldMarkDirty(w, x, y, z);
        int t = target - 1; if (t < 0) t = 0;
        for (int d = 0; d < 6; d++)
            lightUpdateIfOther(w, layer, x + kLN[d][0], y + kLN[d][1], z + kLN[d][2], t);
    }
}

static bool lightDrain(World* w, unsigned int budgetUs) {
    unsigned int tStart = sceKernelGetSystemTimeLow();
    int steps = 0;
    while (!w->lightQueue.empty()) {
        unsigned int q = w->lightQueue.back();
        w->lightQueue.pop_back();
        lightUpdateCell(w, (int)(q >> 23) & 1, (int)((q >> 15) & 0xFF),
                        (int)(q & 0x7F), (int)((q >> 7) & 0xFF));

        if ((++steps & 63) == 0 && sceKernelGetSystemTimeLow() - tStart >= budgetUs) break;
    }
    return w->lightQueue.empty();
}

void worldUpdateLights(World* w) {
    static const unsigned int TIME_BUDGET_US = 1500;
    lightDrain(w, TIME_BUDGET_US);
}

bool worldSettleLights(World* w) {
    static const unsigned int SETTLE_BUDGET_US = 6000;
    return lightDrain(w, SETTLE_BUDGET_US);
}

void worldRemoveBlockLight(World* w, int x, int y, int z) {
    struct RN { int x, y, z, lvl; };
    static std::vector<RN> rem;
    rem.clear();
    g_lightBfs.clear();

    int start = lightBlockGet(w, x, y, z);
    if (start <= 0) return;
    lightBlockSet(w, x, y, z, 0);
    worldMarkDirty(w, x, y, z);
    rem.push_back({ x, y, z, start });

    size_t head = 0;
    while (head < rem.size()) {
        RN n = rem[head++];
        for (int d = 0; d < 6; d++) {
            int nx = n.x + kLN[d][0], ny = n.y + kLN[d][1], nz = n.z + kLN[d][2];
            if (nx < 0 || nx >= WORLD_W || ny < 0 || ny >= WORLD_H || nz < 0 || nz >= WORLD_D) continue;
            int nl = lightBlockGet(w, nx, ny, nz);
            if (nl == 0) continue;
            if (nl < n.lvl) {

                int e = lightEmit(worldBlock(w, nx, ny, nz));
                lightBlockSet(w, nx, ny, nz, 0);
                worldMarkDirty(w, nx, ny, nz);
                if (e > 0) { lightBlockSet(w, nx, ny, nz, e); g_lightBfs.push_back(lpack(nx, ny, nz)); }
                else rem.push_back({ nx, ny, nz, nl });
            } else {

                g_lightBfs.push_back(lpack(nx, ny, nz));
            }
        }
    }
    lightFlood(w, 1);
}

void lightOnBlockChanged(World* w, int x, int y, int z) {

    int oldH = w->heightmap[hmIdx(x, z)];
    int newH = 0;
    for (int yy = WORLD_H - 1; yy >= 0; yy--) {
        if (lightOpacity(worldBlock(w, x, yy, z)) > 0) { newH = yy + 1; break; }
    }
    w->heightmap[hmIdx(x, z)] = (unsigned char)newH;

    int lo = y, hi = y + 1;
    if (oldH < lo) lo = oldH;
    if (newH < lo) lo = newH;
    if (oldH > hi) hi = oldH;
    if (newH > hi) hi = newH;
    if (lo < 0) lo = 0;
    if (hi > WORLD_H) hi = WORLD_H;
    for (int yy = lo; yy < hi; yy++) lightEnqueue(w, 0, x, yy, z);
    lightEnqueue(w, 1, x, y, z);
}
