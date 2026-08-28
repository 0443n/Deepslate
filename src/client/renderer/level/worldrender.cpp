
#include "world/level/world.h"
#include "world/level/chunk/chunk_cache.h"

#include "gpu/texture.h"
#include "gpu/gu.h"
#include "util/prof.h"
#include "platform/time.h"

#include <stdlib.h>
#include <malloc.h>
#include <pspkernel.h>
#include <pspgu.h>
#include <pspgum.h>
#include <math.h>

#include "client/renderer/level/frustum.h"

static inline void streamFreeSection(ChunkSection* s) {
    if (s->mesh)   { guDeferFree(s->mesh);   s->mesh = 0; }
    if (s->water)  { guDeferFree(s->water);  s->water = 0; }
    if (s->leaves) { guDeferFree(s->leaves); s->leaves = 0; }
    if (s->noMip)  { guDeferFree(s->noMip);  s->noMip = 0; }
    s->vertexCount = s->waterCount = s->leavesCount = s->noMipCount = 0;
    s->noMipLavaStart = 0;
    s->dirty = true;
}

// Every visible section with geometry, gathered once per frame and sorted near to
// far. All the draw passes walk this instead of sweeping 2048 sections each.
struct VisSec { float d2; const ChunkSection* s; };
static VisSec g_visList[WORLD_CHUNKS_X * WORLD_CHUNKS_Z * N_SECTIONS];
static int    g_visN;
static int cmpVisAsc(const void* a, const void* b) {
    float da = ((const VisSec*)a)->d2, db = ((const VisSec*)b)->d2;
    return (da > db) - (da < db);
}

extern float g_camX, g_camY, g_camZ;

volatile int g_meshOOM = 0;

float g_viewDistEff = 0.0f;
static float s_lastSlider = 0.0f;
static int   s_oomFrames = 0;

#define OOM_FRAMES_BEFORE_BACKOFF 60

float worldViewDistEffective(float slider) {
    if (slider != s_lastSlider) {
        s_lastSlider = slider;
        g_viewDistEff = slider;
        s_oomFrames = 0;
    }
    return g_viewDistEff;
}

static const float MIP_CRISP_RADIUS     = 16.0f;
static const float MIP_BLOCKS_PER_LEVEL = 16.0f;

static int s_terrainMipCount = 0;

#define LEAF_INTERIOR_RADIUS 32.0f

float g_fogCullDist = 0.0f;

bool g_eyeInLava = false;
static inline float drawCull(float viewDist) {
    return (g_fogCullDist > 0.0f && g_fogCullDist < viewDist) ? g_fogCullDist : viewDist;
}

bool worldColumnDrawn(const World* w, float x, float z) {
    int cx = ((int)floorf(x)) >> 4, cz = ((int)floorf(z)) >> 4;
    if (!worldChunkReady(w, cx, cz)) return false;
    return worldMesh(w, cx, cz)->drawn;
}

void worldRebuildStep(const World* cw, float camX, float camY, float camZ, float viewDist) {
    World* w = (World*)cw;

    chunkMeshHeapProbe();

    profBegin(PROF_STREAM);
    profAdd(PROFC_STREAMIN, worldStream(w, camX, camZ, 4));
    profEnd(PROF_STREAM);

    profBegin(PROF_LIGHT);
    worldUpdateLights(w);
    profEnd(PROF_LIGHT);
    profBegin(PROF_REBUILD);

    static const int MAX_HELD_FRAMES = 12;
    static int s_heldFrames = 0;
    bool lightSettling = !w->lightQueue.empty() && s_heldFrames < MAX_HELD_FRAMES;
    s_heldFrames = lightSettling ? s_heldFrames + 1 : 0;

    worldDrainPlayerEdits(w, lightSettling ? 0 : 6);

    lightCompactStep(w);

    if (lightSettling) {

    } else {

    static const int MAX_CAND = 48;

    static const unsigned int TIME_BUDGET_US = 2000;
    float buildD2 = viewDist * viewDist;

    profBegin(PROF_RSCAN);
    struct Cand { ChunkMesh* c; int si; float d; } cand[MAX_CAND];
    int nc = 0; float worst = 1e30f;
    for (int i = 0; i < WORLD_CHUNKS_X * WORLD_CHUNKS_Z; i++) {
        if (!w->slots[i].resident || worldSlotBusy(&w->slots[i])) continue;
        ChunkMesh* c = &w->chunks[i];
        float dx = c->cx - camX, dz = c->cz - camZ;
        float hd = dx * dx + dz * dz;
        if (hd > buildD2) continue;

        if (!worldChunkMeshable(w, w->slots[i].x, w->slots[i].z)) continue;
        if (nc == MAX_CAND && hd >= worst) continue;
        for (int si = 0; si < N_SECTIONS; si++) {
            if (!c->sec[si].dirty) continue;
            float dy = (float)(si * SECTION_SY + SECTION_SY / 2) - camY;
            float wd = hd + dy * dy * 4.0f;
            if (nc < MAX_CAND) {
                int j = nc++;
                for (; j > 0 && cand[j-1].d > wd; j--) cand[j] = cand[j-1];
                cand[j].c = c; cand[j].si = si; cand[j].d = wd;
                worst = cand[nc-1].d;
            } else if (wd < worst) {
                int j = MAX_CAND - 1;
                for (; j > 0 && cand[j-1].d > wd; j--) cand[j] = cand[j-1];
                cand[j].c = c; cand[j].si = si; cand[j].d = wd;
                worst = cand[MAX_CAND-1].d;
            }
        }
    }
    profEnd(PROF_RSCAN);
    profBegin(PROF_RBUILD);
    unsigned int tStart = sceKernelGetSystemTimeLow();
    int built = 0;

    // A section build is atomic and averages milliseconds, so checking the budget
    // after the fact lets a cheap first section start one that overruns it badly.
    static unsigned int s_sectionUs = TIME_BUDGET_US;
    for (int k = 0; k < nc; k++) {
        const unsigned int t0 = sceKernelGetSystemTimeLow();
        chunkBuildSection(cand[k].c, w, cand[k].si);
        built++;
        const unsigned int now = sceKernelGetSystemTimeLow();
        s_sectionUs = (s_sectionUs * 3u + (now - t0)) / 4u;
        if (now - tStart + s_sectionUs >= TIME_BUDGET_US) break;
    }
    profAdd(PROFC_SECTIONS, built);
    profEnd(PROF_RBUILD);
    }
    profEnd(PROF_REBUILD);
}

int g_occlusion = 1;

// Advanced Cave Culling, after tomcc 2014. Breadth-first walk outward from the
// camera's own section. A neighbour is entered only when the visibility mask says
// the entry face can reach the exit face, and never through a direction already
// travelled in reverse. Frustum is the last test because it is the dearest.
struct VisNode {
    short         cx, cz;
    unsigned char si, from, dirs;
};

static VisNode      s_visQ[WORLD_CHUNKS_X * WORLD_CHUNKS_Z * N_SECTIONS];
static unsigned char s_visSeen[WORLD_CHUNKS_X * WORLD_CHUNKS_Z * N_SECTIONS];

static const signed char kFaceDX[6] = { -1, 1,  0, 0,  0, 0 };
static const signed char kFaceDY[6] = {  0, 0, -1, 1,  0, 0 };
static const signed char kFaceDZ[6] = {  0, 0,  0, 0, -1, 1 };

#define VIS_START 255

bool worldWalkVisible(World* w, float camX, float camY, float camZ, float maxD2) {
    const int ccx = (int)floorf(camX) >> 4;
    const int ccz = (int)floorf(camZ) >> 4;

    if (!worldChunkInBounds(ccx, ccz) || !worldChunkSettled(w, ccx, ccz)) return false;

    int csi = (int)floorf(camY) / SECTION_SY;
    if (csi < 0) csi = 0; else if (csi >= N_SECTIONS) csi = N_SECTIONS - 1;

    for (int i = 0; i < WORLD_CHUNKS_X * WORLD_CHUNKS_Z; i++)
        for (int si = 0; si < N_SECTIONS; si++) w->chunks[i].sec[si].visible = false;

    memset(s_visSeen, 0, sizeof s_visSeen);

    int head = 0, tail = 0;
    {
        VisNode* q = &s_visQ[tail++];
        q->cx = (short)ccx; q->cz = (short)ccz;
        q->si = (unsigned char)csi; q->from = VIS_START; q->dirs = 0;
        s_visSeen[worldSlotIndex(w, ccx, ccz) * N_SECTIONS + csi] = 1;
    }

    while (head < tail) {
        const VisNode n = s_visQ[head++];
        ChunkMesh*    c = &w->chunks[worldSlotIndex(w, n.cx, n.cz)];
        ChunkSection* s = &c->sec[n.si];

        if (sectionVisible(c, s)) s->visible = true;

        for (int f = 0; f < 6; f++) {
            if (n.dirs & (1 << (f ^ 1))) continue;
            if (n.from != VIS_START && !visCanSee(s->visMask, n.from, f)) continue;

            const int nsi = n.si + kFaceDY[f];
            if (nsi < 0 || nsi >= N_SECTIONS) continue;

            const int ncx = n.cx + kFaceDX[f], ncz = n.cz + kFaceDZ[f];
            if (!worldChunkInBounds(ncx, ncz) || !worldChunkSettled(w, ncx, ncz)) continue;

            const int nci = worldSlotIndex(w, ncx, ncz);
            const int key = nci * N_SECTIONS + nsi;
            if (s_visSeen[key]) continue;

            ChunkMesh* nc = &w->chunks[nci];
            const float dx = nc->cx - camX, dz = nc->cz - camZ;
            if (dx * dx + dz * dz > maxD2) continue;
            if (!sectionBoxVisible(nc, nsi)) continue;

            s_visSeen[key] = 1;
            VisNode* q = &s_visQ[tail++];
            q->cx = (short)ncx; q->cz = (short)ncz;
            q->si = (unsigned char)nsi;
            q->from = (unsigned char)(f ^ 1);
            q->dirs = (unsigned char)(n.dirs | (1 << f));
        }
    }
    return true;
}

void worldDraw(const World* cw, float camX, float camY, float camZ, float viewDist, const Texture* terrain) {
    World* w = (World*)cw;

    if (g_meshOOM) {
        g_meshOOM = 0;
        if (++s_oomFrames >= OOM_FRAMES_BEFORE_BACKOFF) {
            s_oomFrames = 0;

            float next = (g_viewDistEff > 32.0f) ? 32.0f : 16.0f;
            if (next < g_viewDistEff) g_viewDistEff = next;
        }
    } else if (s_oomFrames > 0) {
        s_oomFrames--;
    }

    if (!gameFrozen()) worldRebuildStep(w, camX, camY, camZ, viewDist);

    profBegin(PROF_CULL);

    float keepD2 = (viewDist + 32.0f) * (viewDist + 32.0f);
    for (int i = 0; i < WORLD_CHUNKS_X * WORLD_CHUNKS_Z; i++) {
        if (!w->slots[i].resident || worldSlotBusy(&w->slots[i])) continue;
        ChunkMesh* c = &w->chunks[i];
        float dx = c->cx - camX, dz = c->cz - camZ;
        if (dx * dx + dz * dz <= keepD2) continue;
        for (int si = 0; si < N_SECTIONS; si++) {
            ChunkSection* s = &c->sec[si];
            if (s->mesh || s->water || s->leaves || s->noMip) streamFreeSection(s);
        }
    }

    float maxD2 = drawCull(viewDist) * drawCull(viewDist);

    bool walked = g_occlusion && worldWalkVisible(w, camX, camY, camZ, maxD2);

    for (int i = 0; i < WORLD_CHUNKS_X * WORLD_CHUNKS_Z; i++) {
        ChunkMesh* c = &w->chunks[i];

        if (!w->slots[i].resident || worldSlotBusy(&w->slots[i])) {
            c->drawn = false;
            for (int si = 0; si < N_SECTIONS; si++) c->sec[si].visible = false;
            continue;
        }
        float dx = c->cx - camX, dz = c->cz - camZ;
        c->drawn = (dx * dx + dz * dz <= maxD2);

        if (walked) continue;

        bool off = (dx * dx + dz * dz > maxD2 || !columnVisible(c));
        for (int si = 0; si < N_SECTIONS; si++) {
            ChunkSection* s = &c->sec[si];
            s->visible = off ? false : sectionVisible(c, s);
        }
    }
    profEnd(PROF_CULL);

    g_visN = 0;
    int nOpaque = 0;
    for (int i = 0; i < WORLD_CHUNKS_X * WORLD_CHUNKS_Z; i++) {
        const ChunkMesh* c = &w->chunks[i];
        float dx = c->cx - camX, dz = c->cz - camZ;
        for (int si = 0; si < N_SECTIONS; si++) {
            const ChunkSection* s = &c->sec[si];
            if (!s->visible) continue;
            if (s->vertexCount == 0 && s->noMipCount == 0 &&
                s->leavesCount == 0 && s->waterCount == 0) continue;
            float dy = (float)(si * SECTION_SY + SECTION_SY / 2) - camY;
            const float d2 = dx * dx + dy * dy + dz * dz;
            g_visList[g_visN].d2 = d2;
            g_visList[g_visN].s = s;
            g_visN++;
            if (s->vertexCount) nOpaque++;

            // Interior leaves are dropped by distance at draw time, so the counters
            // have to apply the same test or they report work the GE never sees.
            extern int g_fancyGraphics, g_fancyLeaves;
            const int lv = (g_fancyGraphics && g_fancyLeaves &&
                            d2 > LEAF_INTERIOR_RADIUS * LEAF_INTERIOR_RADIUS)
                         ? 0 : s->leavesCount;

            const int op = chunkOpaqueDrawCount(s);
            const int nm = chunkNoMipDrawCount(s);
            profAdd(PROFC_DRAWNVERT, op + nm + lv + s->waterCount);
            profAdd(PROFC_VOPAQUE, op);
            profAdd(PROFC_VNOMIP,  nm);
            profAdd(PROFC_VLEAVES, lv);
            profAdd(PROFC_VWATER,  s->waterCount);
        }
    }
    profAdd(PROFC_DRAWNSEC, nOpaque);
    qsort(g_visList, g_visN, sizeof(VisSec), cmpVisAsc);
    sceGuDisable(GU_ALPHA_TEST);

    extern int g_noMipmap;
    bool distMip = !g_noMipmap && terrain && terrain->mipCount > 0;
    float maxLvl = distMip ? (float)terrain->mipCount : 0.0f;
    s_terrainMipCount = terrain ? terrain->mipCount : 0;

    if (terrain) {
        if (g_noMipmap) textureBindNoMip(terrain);
        else            textureBind(terrain);
    }
    for (int i = 0; i < g_visN; i++) {
        if (g_visList[i].s->vertexCount == 0) continue;
        if (distMip) {
            float lvl = (sqrtf(g_visList[i].d2) - MIP_CRISP_RADIUS) * (1.0f / MIP_BLOCKS_PER_LEVEL);
            if (lvl < 0.0f) lvl = 0.0f; else if (lvl > maxLvl) lvl = maxLvl;
            sceGuTexLevelMode(GU_TEXTURE_CONST, lvl);
        }
        chunkDrawSection(g_visList[i].s);
    }
    if (distMip) textureMipAuto();
    sceGuEnable(GU_ALPHA_TEST);

    if (terrain) {
        bool any = false;
        for (int i = 0; i < g_visN; i++) {
            {
                const ChunkSection* s = g_visList[i].s;
                if (s->noMipCount == 0) continue;
                if (!any) {
                    if (distMip) {
                        textureBind(terrain);
                        sceGuTexFilter(GU_NEAREST_MIPMAP_NEAREST, GU_NEAREST);
                    } else {
                        textureBindNoMip(terrain);
                    }
                    any = true;
                }
                if (distMip) {
                    float lvl = (sqrtf(g_visList[i].d2) - MIP_CRISP_RADIUS) * (1.0f / MIP_BLOCKS_PER_LEVEL);
                    if (lvl < 0.0f) lvl = 0.0f; else if (lvl > maxLvl) lvl = maxLvl;
                    sceGuTexLevelMode(GU_TEXTURE_CONST, lvl);
                }
                chunkDrawNoMipSection(s, g_eyeInLava ? NOMIP_NO_LAVA : NOMIP_ALL);
            }
        }

        if (g_eyeInLava) {

            if (distMip) sceGuTexLevelMode(GU_TEXTURE_CONST, 0.0f);
            sceGuFrontFace(GU_CW);
            for (int i = 0; i < g_visN; i++) {
                {
                    const ChunkSection* s = g_visList[i].s;
                    if (s->noMipCount == 0) continue;
                    chunkDrawNoMipSection(s, NOMIP_LAVA);
                }
            }
            sceGuFrontFace(GU_CCW);
        }
        if (distMip) textureMipAuto();
        if (any) {
            extern int g_noMipmap;
            if (g_noMipmap) textureBindNoMip(terrain);
            else textureBind(terrain);
        }
    }

    extern int g_fancyGraphics, g_fancyLeaves;
    static int s_prevLeafMode = -1;
    int leafMode = g_fancyGraphics | (g_fancyLeaves << 1);
    if (leafMode != s_prevLeafMode) {
        s_prevLeafMode = leafMode;
        for (int i = 0; i < WORLD_CHUNKS_X * WORLD_CHUNKS_Z; i++) {
            ChunkMesh* c = &w->chunks[i];
            for (int si = 0; si < N_SECTIONS; si++) {
                ChunkSection* s = &c->sec[si];
                if (s->leavesCount || s->noMipCount) s->dirty = true;
            }
        }
    }

    if (distMip)
        sceGuTexFilter(g_fancyGraphics ? GU_NEAREST_MIPMAP_NEAREST
                                       : GU_NEAREST_MIPMAP_LINEAR, GU_NEAREST);
    sceGuEnable(GU_ALPHA_TEST);

    // In fancy mode this layer holds only the leaf faces buried inside a canopy, and
    // those are visible through the texture holes at arm's length, not across a valley.
    const bool interiorOnly = g_fancyGraphics && g_fancyLeaves;
    const float interiorR2 = LEAF_INTERIOR_RADIUS * LEAF_INTERIOR_RADIUS;

    for (int i = 0; i < g_visN; i++) {
        const ChunkSection* s = g_visList[i].s;
        if (s->leavesCount == 0) continue;
        if (interiorOnly && g_visList[i].d2 > interiorR2) break;
        if (distMip) {
            float lvl = (sqrtf(g_visList[i].d2) - MIP_CRISP_RADIUS) * (1.0f / MIP_BLOCKS_PER_LEVEL);
            if (lvl < 0.0f) lvl = 0.0f; else if (lvl > maxLvl) lvl = maxLvl;
            sceGuTexLevelMode(GU_TEXTURE_CONST, lvl);
        }
        chunkDrawLeavesSection(s);
    }

    if (distMip) {
        sceGuTexFilter(GU_NEAREST_MIPMAP_LINEAR, GU_NEAREST);
        textureMipAuto();
    }

    guListSync();
}

void worldDrawWater(const World* w, float camX, float camY, float camZ, float viewDist) {
    (void)w; (void)camX; (void)camY; (void)camZ; (void)viewDist;

    extern int g_noMipmap;
    bool distMip = !g_noMipmap && s_terrainMipCount > 0;
    float maxLvl = (float)s_terrainMipCount;

    // Water is transparent, so it walks the shared list backwards to get far to near.
    for (int i = g_visN - 1; i >= 0; i--) {
        const ChunkSection* s = g_visList[i].s;
        if (s->waterCount == 0) continue;
        if (distMip) {
            float lvl = (sqrtf(g_visList[i].d2) - MIP_CRISP_RADIUS) * (1.0f / MIP_BLOCKS_PER_LEVEL);
            if (lvl < 0.0f) lvl = 0.0f; else if (lvl > maxLvl) lvl = maxLvl;
            sceGuTexLevelMode(GU_TEXTURE_CONST, lvl);
        }
        chunkDrawWaterSection(s);
    }
    if (distMip) textureMipAuto();
    guListSync();
}
