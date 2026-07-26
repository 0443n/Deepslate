#include "world/level/chunk/chunk.h"
#include "client/renderer/level/frustum.h"
#include <malloc.h>
#include <pspkernel.h>

extern float g_camX, g_camY, g_camZ;
extern int g_fancyGraphics;

extern volatile int g_meshOOM;

#define MESH_PROFILE 0
#if MESH_PROFILE
unsigned int g_tCount = 0, g_tAlloc = 0, g_tEmit = 0, g_tPack = 0;
#endif

extern int g_lowMemPsp;

static int scratchVerts()   { return g_lowMemPsp ? 24576 : 65536; }
static int scratchVertsWL() { return g_lowMemPsp ?  6144 : 16384; }
#define SCRATCH_VERTS    scratchVerts()
#define SCRATCH_VERTS_WL scratchVertsWL()
static ChunkVertex* g_scratch = 0;
static ChunkVertex* g_scratchW = 0;
static ChunkVertex* g_scratchL = 0;
static ChunkVertex* g_scratchN = 0;

static bool meshHeapReserveOk() {
    unsigned MESH_HEAP_RESERVE = g_lowMemPsp ? (1u * 1024 * 1024) : (3u * 1024 * 1024);
    void* p = malloc(MESH_HEAP_RESERVE);
    if (!p) return false;
    free(p);
    return true;
}

static void buildLayer(const World* w, int ox, int oz, int y0, int y1, int layer,
                       MeshLayer* dst, bool leavesOpaque, bool leavesCull, bool* oom) {
    chunkFreeLayer(dst);
    if (!meshHeapReserveOk()) { *oom = true; return; }
    if (!g_scratch)
        g_scratch = (ChunkVertex*)memalign(16, SCRATCH_VERTS * sizeof(ChunkVertex));

    if (g_scratch) {
#if MESH_PROFILE
        unsigned int t0 = sceKernelGetSystemTimeLow();
#endif
        int n = meshPass(w, ox, oz, y0, y1, g_scratch, layer, SCRATCH_VERTS, leavesOpaque, leavesCull);
#if MESH_PROFILE
        unsigned int t1 = sceKernelGetSystemTimeLow(); g_tEmit += t1 - t0;
#endif
        if (n >= 0) {
            if (n == 0) return;
            bool ok = chunkPack(dst, g_scratch, n, ox, y0, oz);
#if MESH_PROFILE
            g_tPack += sceKernelGetSystemTimeLow() - t1;
#endif
            if (!ok) *oom = true;
            return;
        }

    }

#if MESH_PROFILE
    unsigned int s0 = sceKernelGetSystemTimeLow();
#endif
    int count = meshPass(w, ox, oz, y0, y1, 0, layer, 0x7fffffff, leavesOpaque, leavesCull);
#if MESH_PROFILE
    g_tCount += sceKernelGetSystemTimeLow() - s0;
#endif
    if (count == 0) return;
    ChunkVertex* m = (ChunkVertex*)memalign(16, count * sizeof(ChunkVertex));
    if (!m) { *oom = true; return; }
    meshPass(w, ox, oz, y0, y1, m, layer, 0x7fffffff, leavesOpaque, leavesCull);
    bool ok = chunkPack(dst, m, count, ox, y0, oz);
    free(m);
    if (!ok) *oom = true;
}

void chunkBuildSection(ChunkMesh* c, const World* w, int si) {
    ChunkSection* s = &c->sec[si];

    if (!meshHeapReserveOk()) { s->dirty = true; return; }

    int y0 = si * SECTION_SY, y1 = y0 + SECTION_SY;
    int ox = c->ox, oz = c->oz;
    s->ox = ox; s->oy = y0; s->oz = oz;

    s->skyLit = false;
    {
        int sy0 = y0 - 1, sy1 = y1;
        if (sy0 < 0) sy0 = 0;
        if (sy1 > WORLD_H - 1) sy1 = WORLD_H - 1;

        for (int yy = sy0; yy <= sy1 && !s->skyLit; yy++)
            if (!lightPlaneAllDark(w, 0, ox, yy, oz)) s->skyLit = true;

        for (int gx = ox - 1; gx <= ox + CHUNK_SX && !s->skyLit; gx++) {
            if (gx < 0 || gx >= WORLD_W) continue;
            for (int gz = oz - 1; gz <= oz + CHUNK_SZ && !s->skyLit; gz++) {
                if (gz < 0 || gz >= WORLD_D) continue;
                if (gx >= ox && gx < ox + CHUNK_SX &&
                    gz >= oz && gz < oz + CHUNK_SZ) continue;
                for (int yy = sy0; yy <= sy1; yy++)
                    if (lightSkyGet(w, gx, yy, gz)) { s->skyLit = true; break; }
            }
        }
    }

    chunkFreeLayer(&s->op); chunkFreeLayer(&s->wa);
    chunkFreeLayer(&s->le); chunkFreeLayer(&s->nm);

    bool leavesOpaque = leafOpaqueBand(c, y0, y1, g_camX, g_camY, g_camZ, g_fancyGraphics != 0);
    bool leavesCull   = leafCullBand(c, y0, y1, g_camX, g_camY, g_camZ, g_fancyGraphics != 0);

    if (!g_scratch)  g_scratch  = (ChunkVertex*)memalign(16, SCRATCH_VERTS    * sizeof(ChunkVertex));
    if (!g_scratchW) g_scratchW = (ChunkVertex*)memalign(16, SCRATCH_VERTS_WL * sizeof(ChunkVertex));
    if (!g_scratchL) g_scratchL = (ChunkVertex*)memalign(16, SCRATCH_VERTS_WL * sizeof(ChunkVertex));
    if (!g_scratchN) g_scratchN = (ChunkVertex*)memalign(16, SCRATCH_VERTS_WL * sizeof(ChunkVertex));

    bool oom = false;
    bool fast = g_scratch && g_scratchW && g_scratchL && g_scratchN;
    if (fast) {
        int n0, n1, n2, n3;
#if MESH_PROFILE
        unsigned int t0 = sceKernelGetSystemTimeLow();
#endif
        int rc = meshSection(w, ox, oz, y0, y1, g_scratch, g_scratchW, g_scratchL, g_scratchN,
                             SCRATCH_VERTS, SCRATCH_VERTS_WL, SCRATCH_VERTS_WL, SCRATCH_VERTS_WL,
                             &n0, &n1, &n2, &n3, leavesOpaque, leavesCull);
#if MESH_PROFILE
        unsigned int t1 = sceKernelGetSystemTimeLow(); g_tEmit += t1 - t0;
#endif
        if (rc == 0) {

            if (n0 && !chunkPack(&s->op, g_scratch,  n0, ox, y0, oz)) oom = true;
            if (n1 && !chunkPack(&s->wa, g_scratchW, n1, ox, y0, oz)) oom = true;
            if (n2 && !chunkPack(&s->le, g_scratchL, n2, ox, y0, oz)) oom = true;
            if (n3 && !chunkPack(&s->nm, g_scratchN, n3, ox, y0, oz)) oom = true;
#if MESH_PROFILE
            g_tPack += sceKernelGetSystemTimeLow() - t1;
#endif
        } else {
            fast = false;
        }
    }
    if (!fast) {
        buildLayer(w, ox, oz, y0, y1, 0, &s->op, leavesOpaque, leavesCull, &oom);
        buildLayer(w, ox, oz, y0, y1, 1, &s->wa, leavesOpaque, leavesCull, &oom);
        buildLayer(w, ox, oz, y0, y1, 2, &s->le, leavesOpaque, leavesCull, &oom);

        buildLayer(w, ox, oz, y0, y1, 3, &s->nm, leavesOpaque, leavesCull, &oom);
    }
    s->leavesOpaqueBand = leavesOpaque;
    s->leavesCullBand = leavesCull;

    int totalVerts = s->op.count + s->wa.count + s->le.count + s->nm.count;
    if (totalVerts == 0) {
        s->by0 = s->by1 = (float)y0;
        s->lby0 = s->lby1 = (float)y0;
        s->wby0 = s->wby1 = (float)y0;
        if (oom) { g_meshOOM = 1; s->dirty = true; }
        else       s->dirty = false;
        return;
    }

    #define SCAN_Y(L, LO, HI) \
        for (int i = 0; i < (L).unique; i++) { \
            float y = (L).v[i].y / (float)POS_ENC + y0; \
            if (y < (LO)) (LO) = y; if (y > (HI)) (HI) = y; }

    float ylo = 1e9f, yhi = -1e9f;
    SCAN_Y(s->op, ylo, yhi); SCAN_Y(s->wa, ylo, yhi);
    SCAN_Y(s->le, ylo, yhi); SCAN_Y(s->nm, ylo, yhi);
    if (ylo > yhi) { ylo = (float)y0; yhi = (float)y0; }
    s->by0 = ylo; s->by1 = yhi;

    float lylo = 1e9f, lyhi = -1e9f;
    SCAN_Y(s->le, lylo, lyhi);
    if (lylo > lyhi) { lylo = (float)y0; lyhi = (float)y0; }
    s->lby0 = lylo; s->lby1 = lyhi;

    float wylo = 1e9f, wyhi = -1e9f;
    SCAN_Y(s->wa, wylo, wyhi);
    if (wylo > wyhi) { wylo = (float)y0; wyhi = (float)y0; }
    s->wby0 = wylo; s->wby1 = wyhi;
    #undef SCAN_Y

    if (oom) { g_meshOOM = 1; s->dirty = true; }
    else       s->dirty = false;
}

void chunkBuildMesh(ChunkMesh* c, const World* w, int ox, int oz) {
    c->ox = ox; c->oz = oz;
    c->cx = ox + CHUNK_SX * 0.5f;
    c->cz = oz + CHUNK_SZ * 0.5f;
    for (int si = 0; si < N_SECTIONS; si++) {

        chunkBuildSection(c, w, si);
    }
}

void chunkInitLazy(ChunkMesh* c, int ox, int oz) {
    c->ox = ox; c->oz = oz;
    c->cx = ox + CHUNK_SX * 0.5f;
    c->cz = oz + CHUNK_SZ * 0.5f;
    for (int si = 0; si < N_SECTIONS; si++) {
        ChunkSection* s = &c->sec[si];
        s->by0 = s->lby0 = s->wby0 = (float)(si * SECTION_SY);
        s->by1 = s->lby1 = s->wby1 = s->by0;
        s->dirty = true;
    }
}
