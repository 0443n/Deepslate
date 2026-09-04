#include "world/level/chunk/chunk_cache.h"
#include "world/level/world.h"
#include "world/level/chunk/chunk.h"
#include "world/level/levelgen/mcpegen.h"
#include "world/level/levelgen/level_source.h"
#include "world/level/storage/chunk_storage.h"

#include <string.h>
#include <math.h>
#include <pspkernel.h>
#include "util/prof.h"

unsigned int g_streamIn = 0, g_streamOut = 0;

struct GenScope {
    World* w; bool saved;
    GenScope(World* world) : w(world), saved(world->lightReady) { w->lightReady = false; }
    ~GenScope() { w->lightReady = saved; }
};

enum { ST_DONE = 0, ST_DECOR0 = 1, ST_LIGHT = 5, ST_MISC = 6 };

static void evict(World* w, int slotIdx) {
    LevelChunk* c = &w->slots[slotIdx];
    if (!c->resident) return;
    profBegin(PROF_SEVICT);

    if (c->unsaved && !worldSlotBusy(c)) chunkStorageSave(w, c->x, c->z);
    c->resident = false;
    blockSlotRecycle(w, slotIdx);
    lightSlotRelease(w, slotIdx);

    for (int i = 0; i < 256; i++) {
        unsigned char** pg = &w->dataCol[(slotIdx << 8) | i];
        if (*pg) { free(*pg); *pg = 0; w->dataPages--; }
    }
    memset(&w->heightmap[slotIdx << 8], 0, 256);

    chunkFreeMesh(&w->chunks[slotIdx]);
    g_streamOut++;
    profEnd(PROF_SEVICT);
}

static bool postProcessPhase(World* w, int cx, int cz, int phase) {
    LevelChunk* c = worldSlot(w, cx, cz);
    if (!c->isAt(cx, cz)) return true;

    if (phase > 0) return chunkPostProcessPhase(w, cx, cz, phase);
    if (c->terrainPopulated) return true;

    if (!activeLevelSource().supportsGenFeatures()) { c->terrainPopulated = true; return true; }

    if (!worldNeighbourSettled(w, cx + 1, cz) || !worldNeighbourSettled(w, cx, cz + 1) ||
        !worldNeighbourSettled(w, cx + 1, cz + 1)) return true;
    c->terrainPopulated = true;

    for (int dz = 0; dz <= 1; dz++)
        for (int dx = 0; dx <= 1; dx++)
            if (worldChunkReady(w, cx + dx, cz + dz))
                worldSlot(w, cx + dx, cz + dz)->unsaved = true;
    return chunkPostProcessPhase(w, cx, cz, 0);
}

static void claim(World* w, int cx, int cz) {
    int slot = worldSlotIndex(w, cx, cz);
    evict(w, slot);

    lightSlotRelease(w, slot);
    LevelChunk* c = &w->slots[slot];
    c->x = cx; c->z = cz;
    c->unsaved = false;
    c->terrainPopulated = false;
    c->stage = ST_DONE;

    c->resident = true;
    chunkInitLazy(&w->chunks[slot], cx * 16, cz * 16);
}

static bool s_pend = false;
static int  s_pendX = 0, s_pendZ = 0;

static int  s_decorPhase = 0;

static void finishBegin(World* w, int cx, int cz) {
    LevelChunk* c = worldSlot(w, cx, cz);

    c->generating = false;
    c->stage = ST_DECOR0;
    s_decorPhase = 0;
    s_pend = true; s_pendX = cx; s_pendZ = cz;
}

bool worldStreamBusy() { return s_pend; }

static bool finishStep(World* w) {
    if (!s_pend) return false;
    const int cx = s_pendX, cz = s_pendZ;
    LevelChunk* c = worldSlot(w, cx, cz);

    if (!c->isAt(cx, cz)) { c->generating = false; c->stage = ST_DONE; s_pend = false; return false; }

    GenScope gen(w);
    switch (c->stage) {

    case 1: case 2: case 3: case 4: {
        static const int kDx[4] = { 0, -1,  0, -1 };
        static const int kDz[4] = { 0,  0, -1, -1 };
        int i = c->stage - ST_DECOR0;
        profBegin(PROF_SDECOR);

        const unsigned int DECOR_BUDGET_US = 2000;
        unsigned int t0 = sceKernelGetSystemTimeLow();
        bool decorDone;
        do {
            decorDone = postProcessPhase(w, cx + kDx[i], cz + kDz[i], s_decorPhase);
            s_decorPhase++;
        } while (!decorDone &&
                 (unsigned int)(sceKernelGetSystemTimeLow() - t0) < DECOR_BUDGET_US);
        profEnd(PROF_SDECOR);
        if (!decorDone) return true;
        s_decorPhase = 0;
        break;
    }

    case ST_LIGHT:
        profBegin(PROF_SLIGHT);
        worldInitChunkLight(w, cx, cz);
        profEnd(PROF_SLIGHT);
        break;
    case ST_MISC:
    default:
        profBegin(PROF_SMISC);

        worldPlaceMushrooms(w);
        worldPlaceFlowers(w);
        worldScheduleChunkTicks(w, cx, cz);
        profEnd(PROF_SMISC);
        c->stage = ST_DONE;
        s_pend = false;
        g_streamIn++;
        return true;
    }
    c->stage++;
    return true;
}

void worldGetChunk(World* w, int cx, int cz) {

    if (!worldChunkInBounds(cx, cz)) return;
    if (worldChunkReady(w, cx, cz)) return;
    claim(w, cx, cz);
    {
        GenScope gen(w);
        bool gotLight = false, populated = true;
        profBegin(PROF_SDISK);
        bool fromDisk = chunkStorageLoad(w, cx, cz, &gotLight, &populated);
        profEnd(PROF_SDISK);
        if (fromDisk) {

            worldSlot(w, cx, cz)->terrainPopulated = populated;
        } else {
            profBegin(PROF_SGEN);
            activeLevelSource().buildChunk(w, cx, cz);
            profEnd(PROF_SGEN);

        }
    }

    finishBegin(w, cx, cz);
    while (finishStep(w)) {}
}

void worldEnsureArea(World* w, int cx, int cz, int r) {
    for (int dz = -r; dz <= r; dz++)
        for (int dx = -r; dx <= r; dx++)
            worldGetChunk(w, cx + dx, cz + dz);
}

// NOTE: terrain used to be generated on a second thread, which real hardware
// would not survive. worldReady admits a chunk the moment claim marks it
// resident, so liquid flow and player edits reach into a chunk while it is
// still being generated, and two threads then race inside secPageUp and
// secRawUp, where secRawUp frees the section page the other one is writing
// through. The block store has to grow a staging buffer before the worker can
// come back.

int worldLoadRadius(const World* w) {
    extern float g_viewDistEff;
    float d = (g_viewDistEff > 0.0f) ? g_viewDistEff : WORLD_VIEW_DIST;
    int r = (int)(d / 16.0f) + 2;
    if (r < 4) r = 4;

    // The load square has to leave the window spare slots. Capping on slotN
    // alone lets it ask for 289 chunks out of 256, and then every slot is
    // resident, nothing is ever evictable, and the heap sits at its ceiling.
    const int spare = w->slotN * w->slotN - 64;
    while (r > 4 && (2 * r + 1) * (2 * r + 1) > spare) r--;
    return r;
}

static inline int loadRadius(const World* w) { return worldLoadRadius(w); }

int worldStream(World* w, float px, float pz, int budgetMs) {

    if (worldFitsInWindow(w)) return 0;
    const int pcx = (int)floorf(px) >> 4, pcz = (int)floorf(pz) >> 4;
    const int R = loadRadius(w);
    const int E = R + 1;
    const unsigned int tStart = sceKernelGetSystemTimeLow();
    int brought = 0;

    // Eviction is memory hygiene, not a prerequisite for loading, since claim
    // frees the one slot a new chunk needs. Terrain generates far slower than
    // the loop can throw chunks away, so evicting on distance alone empties the
    // window the moment the player moves faster than a walk.
    int keep = (2 * R + 1) * (2 * R + 1) + 32;
    if (keep > w->slotN * w->slotN - 32) keep = w->slotN * w->slotN - 32;
    int residentN = 0;
    for (int i = 0; i < w->slotN * w->slotN; i++) if (w->slots[i].resident) residentN++;

    const unsigned int EVICT_BUDGET_US = 2000;
    for (int i = 0; i < w->slotN * w->slotN && residentN > keep; i++) {
        LevelChunk* c = &w->slots[i];
        if (!c->resident) continue;
        if (c->x >= pcx - E && c->x <= pcx + E && c->z >= pcz - E && c->z <= pcz + E) continue;
        if (worldSlotBusy(c)) continue;
        evict(w, i);
        residentN--;
        if ((unsigned int)(sceKernelGetSystemTimeLow() - tStart) > EVICT_BUDGET_US) break;
    }

    if (finishStep(w)) {
        if (!s_pend) brought++;
        return brought;
    }

    {
        if ((unsigned int)(sceKernelGetSystemTimeLow() - tStart) > (unsigned int)budgetMs * 1000u)
            return brought;
        int bestX = 0, bestZ = 0, bestD = 0x7FFFFFFF;
        for (int dz = -R; dz <= R; dz++)
            for (int dx = -R; dx <= R; dx++) {
                int cx = pcx + dx, cz = pcz + dz;
                if (!worldChunkInBounds(cx, cz)) continue;
                if (worldChunkReady(w, cx, cz)) continue;
                int d = dx * dx + dz * dz;
                if (d < bestD) { bestD = d; bestX = cx; bestZ = cz; }
            }
        if (bestD == 0x7FFFFFFF) return brought;

        claim(w, bestX, bestZ);

        bool gotLight = false, populated = true;
        profBegin(PROF_SDISK);
        bool onDisk = chunkStorageLoad(w, bestX, bestZ, &gotLight, &populated);
        profEnd(PROF_SDISK);
        if (onDisk) {
            worldSlot(w, bestX, bestZ)->terrainPopulated = populated;
            finishBegin(w, bestX, bestZ);
            return brought;
        }
        GenScope gen(w);
        profBegin(PROF_SGEN);
        activeLevelSource().buildChunk(w, bestX, bestZ);
        profEnd(PROF_SGEN);
        finishBegin(w, bestX, bestZ);
    }
    return brought;
}

void worldSaveResident(World* w) {
    for (int i = 0; i < w->slotN * w->slotN; i++) {
        LevelChunk* c = &w->slots[i];

        if (c->resident && c->unsaved && !worldSlotBusy(c)) chunkStorageSave(w, c->x, c->z);
    }
}
