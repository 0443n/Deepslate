#include "world/level/chunk/chunk.h"
#include "gpu/gu.h"
#include "platform/dcache.h"
#include "util/prof.h"
#include "world/level/chunk/mesh_sink.h"
#include "util/fast_memcpy.h"
#include <pspgu.h>
#include <pspgum.h>
#include <malloc.h>
#include <pspkernel.h>
#include <pspgum.h>

void chunkPackInto(DrawVertex* d, const ChunkVertex* s, int n,
                   int ox, int oy, int oz, int* qlo, int* qhi) {
    profAdd(PROFC_PACKVERTS, n);
    profBegin(PROF_MCONV);
    int lo = *qlo, hi = *qhi;
    for (int i = 0; i < n; i++) {
        d[i].u = uvQ(s[i].u); d[i].v = uvQ(s[i].v); d[i].color = s[i].color;
        d[i].x = posQ(s[i].x - ox); d[i].y = posQ(s[i].y - oy); d[i].z = posQ(s[i].z - oz); d[i].w = 0;
        if (d[i].y < lo) lo = d[i].y;
        if (d[i].y > hi) hi = d[i].y;
    }
    *qlo = lo; *qhi = hi;
    profEnd(PROF_MCONV);
}

float chunkPackDecodeY(int q, int oy) { return (float)q / (float)POS_ENC + oy; }

#define INDEX_SCRATCH 65536
static unsigned short* g_idxScratch = 0;
static bool g_idxScratchFailed = false;

static unsigned short* indexScratch() {
    if (!g_idxScratch && !g_idxScratchFailed) {
        g_idxScratch = (unsigned short*)memalign(16, INDEX_SCRATCH * sizeof(unsigned short));
        if (!g_idxScratch) g_idxScratchFailed = true;
    }
    return g_idxScratch;
}

static inline bool sameVert(const DrawVertex& a, const DrawVertex& b) {
    return a.u == b.u && a.v == b.v && a.color == b.color &&
           a.x == b.x && a.y == b.y && a.z == b.z;
}

// Every emitter builds a quad as six vertices over four distinct corners, so each
// group of six collapses to four with no search beyond the group itself.
static int dedupQuads(DrawVertex* v, int n, unsigned short* idx) {
    int w = 0;
    for (int g = 0; g < n; g += 6) {
        int base = w;
        for (int t = 0; t < 6; t++) {
            const DrawVertex& src = v[g + t];
            int hit = -1;
            for (int u = base; u < w; u++)
                if (sameVert(v[u], src)) { hit = u; break; }
            if (hit < 0) { hit = w; v[w++] = src; }
            idx[g + t] = (unsigned short)hit;
        }
    }
    return w;
}

// Returns the vertex array, with the index array packed straight after it. Sets
// nUnique to zero when the layer had to stay unindexed.
static DrawVertex* packIndexed(DrawVertex* v, int n, unsigned short* nUnique) {
    *nUnique = 0;
    unsigned short* scratch = (n % 6 == 0 && n <= INDEX_SCRATCH) ? indexScratch() : 0;
    if (scratch) {
        int uq = dedupQuads(v, n, scratch);
        if (uq <= 65535) {
            size_t vb = (size_t)uq * sizeof(DrawVertex), ib = (size_t)n * sizeof(unsigned short);
            profBegin(PROF_MALLOC);
            DrawVertex* d = (DrawVertex*)memalign(64, vb + ib);
            profEnd(PROF_MALLOC);
            if (d) {
                memcpy_vfpu(d, v, vb);
                memcpy((unsigned char*)d + vb, scratch, ib);
                dcacheFlush(d, vb + ib);
                *nUnique = (unsigned short)uq;
                return d;
            }
        }
    }

    profBegin(PROF_MALLOC);
    DrawVertex* d = (DrawVertex*)memalign(64, (size_t)n * sizeof(DrawVertex));
    profEnd(PROF_MALLOC);
    if (!d) return 0;
    memcpy_vfpu(d, v, (size_t)n * sizeof(DrawVertex));
    dcacheFlush(d, (size_t)n * sizeof(DrawVertex));
    return d;
}

DrawVertex* chunkPackFinish(DrawVertex* staging, int n, unsigned short* nUnique) {
    return packIndexed(staging, n, nUnique);
}

DrawVertex* chunkPack(const ChunkVertex* s, int n, int ox, int oy, int oz,
                      float* ylo, float* yhi, unsigned short* nUnique) {
    DrawVertex* tmp = (DrawVertex*)memalign(16, (size_t)n * sizeof(DrawVertex));
    if (!tmp) { *nUnique = 0; return 0; }
    int qlo = 32767, qhi = -32768;
    chunkPackInto(tmp, s, n, ox, oy, oz, &qlo, &qhi);
    if (ylo) *ylo = chunkPackDecodeY(qlo, oy);
    if (yhi) *yhi = chunkPackDecodeY(qhi, oy);
    DrawVertex* d = packIndexed(tmp, n, nUnique);
    free(tmp);
    return d;
}

float g_relBaseX = 0.0f, g_relBaseY = 0.0f, g_relBaseZ = 0.0f;

#define SEAM_OVERSCALE_OPAQUE (32768.0f / 32753.0f)
#define SEAM_OVERSCALE_TRANS  (32768.0f / 32763.0f)

static inline void chunkSetModel(const ChunkSection* s, float scaleMul) {
    const float sm = POS_MODEL_SCALE * scaleMul;
    ScePspFMatrix4 m;
    m.x.x = sm;   m.x.y = 0.0f; m.x.z = 0.0f; m.x.w = 0.0f;
    m.y.x = 0.0f; m.y.y = sm;   m.y.z = 0.0f; m.y.w = 0.0f;
    m.z.x = 0.0f; m.z.y = 0.0f; m.z.z = sm;   m.z.w = 0.0f;
    m.w.x = (float)s->ox - g_relBaseX;
    m.w.y = (float)s->oy - g_relBaseY;
    m.w.z = (float)s->oz - g_relBaseZ;
    m.w.w = 1.0f;
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadMatrix(&m);
}

#define CHUNK_FMT (GU_TEXTURE_16BIT | GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_3D)

static inline void chunkDrawLayer(const DrawVertex* v, unsigned short nUnique,
                                  int count, int first) {
    if (!nUnique) { sceGumDrawArray(GU_TRIANGLES, CHUNK_FMT, count, 0, v + first); return; }
    const unsigned short* idx = (const unsigned short*)(v + nUnique);
    sceGumDrawArray(GU_TRIANGLES, CHUNK_FMT | GU_INDEX_16BIT, count, idx + first, v);
}

void chunkDrawSection(const ChunkSection* s) {
    if (s->vertexCount <= 0 || !s->mesh) return;
    chunkSetModel(s, SEAM_OVERSCALE_OPAQUE);
    chunkDrawLayer(s->mesh, s->meshVCount, s->vertexCount, 0);
}

void chunkDrawWaterSection(const ChunkSection* s) {
    if (s->waterCount > 0 && s->water) {
        chunkSetModel(s, SEAM_OVERSCALE_TRANS);
        chunkDrawLayer(s->water, s->waterVCount, s->waterCount, 0);
    }
}

void chunkDrawLeavesSection(const ChunkSection* s) {
    if (s->leavesCount > 0 && s->leaves) {
        chunkSetModel(s, SEAM_OVERSCALE_OPAQUE);
        chunkDrawLayer(s->leaves, s->leavesVCount, s->leavesCount, 0);
    }
}

void chunkDrawNoMipSection(const ChunkSection* s, int part) {
    if (s->noMipCount <= 0 || !s->noMip) return;

    int first = 0, count = s->noMipCount;
    if (part == NOMIP_NO_LAVA) count = s->noMipLavaStart;
    else if (part == NOMIP_LAVA) { first = s->noMipLavaStart; count = s->noMipCount - first; }
    if (count <= 0) return;
    chunkSetModel(s, SEAM_OVERSCALE_OPAQUE);
    chunkDrawLayer(s->noMip, s->noMipVCount, count, first);
}

void chunkFreeMesh(ChunkMesh* c) {
    for (int si = 0; si < N_SECTIONS; si++) {
        ChunkSection* s = &c->sec[si];
        if (s->mesh)   { guDeferFree(s->mesh);   s->mesh = 0; }
        if (s->water)  { guDeferFree(s->water);  s->water = 0; }
        if (s->leaves) { guDeferFree(s->leaves); s->leaves = 0; }
        if (s->noMip)  { guDeferFree(s->noMip);  s->noMip = 0; }
        s->vertexCount = s->waterCount = s->leavesCount = s->noMipCount = 0;
        s->meshVCount = s->waterVCount = s->leavesVCount = s->noMipVCount = 0;
        s->noMipLavaStart = 0;
    }
}
