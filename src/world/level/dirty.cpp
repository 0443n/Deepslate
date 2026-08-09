#include "world/level/world.h"
#include "util/prof.h"
#include "world/level/chunk/chunk.h"
#include <pspkernel.h>

#define PLAYER_EDIT_QUEUE_CAP 128
static int g_editQueue[PLAYER_EDIT_QUEUE_CAP][2];
static int g_editQueueN = 0;

static bool g_inEditQueue[WORLD_CHUNKS_X * WORLD_CHUNKS_Z][N_SECTIONS];

static inline void markSecDirty(World* w, int cx, int cz, int y) {

    if (!worldChunkSettled(w, cx, cz)) return;
    if (y < 0 || y >= WORLD_H) return;
    int si = y / SECTION_SY;

    ChunkSection* csec = &worldMesh(w, cx, cz)->sec[si];
    if (!csec->dirty) profAdd(PROFC_MARKED, 1);
    csec->dirty = true;

    if (!w->lightReady) return;

    worldSlot(w, cx, cz)->unsaved = true;

    if (w->simTick) return;

    int ci = worldSlotIndex(w, cx, cz);
    if (g_inEditQueue[ci][si]) return;
    if (g_editQueueN >= PLAYER_EDIT_QUEUE_CAP) return;
    g_editQueue[g_editQueueN][0] = ci; g_editQueue[g_editQueueN][1] = si; g_editQueueN++;
    g_inEditQueue[ci][si] = true;
}

bool g_smoothLighting = true;

void worldMarkAllDirty(World* w) {
    for (int ci = 0; ci < WORLD_CHUNKS_X * WORLD_CHUNKS_Z; ci++)
        for (int si = 0; si < N_SECTIONS; si++)
            w->chunks[ci].sec[si].dirty = true;
}

void worldMarkDirty(World* w, int x, int y, int z) {

    int cx[2], cz[2], sy[2], ncx = 1, ncz = 1, nsy = 1;
    cx[0] = (x - 1) >> 4;  if (((x + 1) >> 4) != cx[0]) cx[ncx++] = (x + 1) >> 4;
    cz[0] = (z - 1) >> 4;  if (((z + 1) >> 4) != cz[0]) cz[ncz++] = (z + 1) >> 4;
    int ylo = (y > 0) ? y - 1 : 0;
    int yhi = (y < WORLD_H - 1) ? y + 1 : WORLD_H - 1;
    sy[0] = ylo;           if ((yhi >> 4) != (ylo >> 4)) sy[nsy++] = yhi;
    for (int a = 0; a < ncx; a++)
        for (int b = 0; b < ncz; b++)
            for (int c = 0; c < nsy; c++)
                markSecDirty(w, cx[a], cz[b], sy[c]);
}

void worldSetData(World* w, int x, int y, int z, unsigned char data) {
    if (y < 0 || y >= WORLD_H || !worldReady(w, x, z)) return;
    worldDataPut(w, worldIndex(w, x, y, z), data);
    worldMarkDirty(w, x, y, z);
}

void worldSetDataNoUpdate(World* w, int x, int y, int z, unsigned char data) {
    if (y < 0 || y >= WORLD_H || !worldReady(w, x, z)) return;
    worldDataPut(w, worldIndex(w, x, y, z), data);
}

bool worldSetBlockAndData(World* w, int x, int y, int z, unsigned char id, unsigned char data) {
    if (y < 0 || y >= WORLD_H || !worldReady(w, x, z)) return false;
    unsigned char was = worldBlock(w, x, y, z);

    if (!blockPut(w, x, y, z, id)) return false;
    worldDataPut(w, worldIndex(w, x, y, z), data);
    worldMarkDirty(w, x, y, z);
    if (w->lightReady) {
        lightOnBlockChanged(w, x, y, z);

        if (lightEmit(was) > 0 && lightEmit(id) == 0) worldRemoveBlockLight(w, x, y, z);
    }
    return true;
}

static int g_editBurst = 0;

static bool editQueuePromote(int ci, int si) {
    if (!g_inEditQueue[ci][si]) return false;
    for (int i = 0; i < g_editQueueN; i++) {
        if (g_editQueue[i][0] != ci || g_editQueue[i][1] != si) continue;
        for (int j = i; j > 0; j--) {
            g_editQueue[j][0] = g_editQueue[j-1][0];
            g_editQueue[j][1] = g_editQueue[j-1][1];
        }
        g_editQueue[0][0] = ci; g_editQueue[0][1] = si;
        return true;
    }
    return false;
}

void worldRebuildAroundNow(World* w, int x, int y, int z) {
    if (y < 0 || y >= WORLD_H) return;

    int burst[8][2], nb = 0;
    for (int dz = 1; dz >= -1; dz--)
    for (int dx = 1; dx >= -1; dx--)
    for (int dy = 1; dy >= -1; dy--) {
        int nx = x + dx, ny = y + dy, nz = z + dz;
        if (ny < 0 || ny >= WORLD_H) continue;
        int cx = nx >> 4, cz = nz >> 4;
        if (!worldChunkSettled(w, cx, cz)) continue;
        int ci = worldSlotIndex(w, cx, cz), si = ny / SECTION_SY;
        if (!editQueuePromote(ci, si)) continue;
        bool seen = false;
        for (int k = 0; k < nb; k++) if (burst[k][0] == ci && burst[k][1] == si) { seen = true; break; }
        if (!seen && nb < 8) { burst[nb][0] = ci; burst[nb][1] = si; nb++; }
    }

    if (worldChunkSettled(w, x >> 4, z >> 4))
        editQueuePromote(worldSlotIndex(w, x >> 4, z >> 4), y / SECTION_SY);
    g_editBurst = nb;
}

int worldEditQueueDepth() { return g_editQueueN; }
int worldEditQueueFront(int field) { return g_editQueueN ? g_editQueue[0][field] : -1; }

void worldDrainPlayerEdits(World* w, int maxSections) {

    static const unsigned int TIME_BUDGET_US = 1000;

    unsigned int tStart = sceKernelGetSystemTimeLow();
    int burst = g_editBurst; g_editBurst = 0;
    int n = g_editQueueN < maxSections ? g_editQueueN : maxSections;
    if (n < burst) n = g_editQueueN < burst ? g_editQueueN : burst;
    for (int i = 0; i < n; i++) {
        int ci = g_editQueue[0][0], si = g_editQueue[0][1];
        for (int j = 1; j < g_editQueueN; j++) { g_editQueue[j-1][0] = g_editQueue[j][0]; g_editQueue[j-1][1] = g_editQueue[j][1]; }
        g_editQueueN--;
        g_inEditQueue[ci][si] = false;
        ChunkMesh* c = &w->chunks[ci];
        if (c->sec[si].dirty) chunkBuildSection(c, w, si);
        if (i + 1 >= burst && sceKernelGetSystemTimeLow() - tStart >= TIME_BUDGET_US)
            break;
    }
}

void worldScheduleTick(World* w, int x, int y, int z, unsigned char id, int tickDelay) {

    if (y < 0 || y >= WORLD_H || !worldChunkSettled(w, x >> 4, z >> 4)) return;

    unsigned int key = (unsigned int)worldIndex(w, x, y, z);
    if (!w->tickSet.insert(key).second) return;
    TickNextTickData td = {x, y, z, id, w->time + tickDelay};
    w->tickNextTickList.push_back(td);
}
