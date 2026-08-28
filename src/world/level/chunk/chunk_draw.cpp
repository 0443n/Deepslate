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

// Outward normal of an axis aligned quad, taken from its winding. Returns -1
// when no axis is constant, which leaves the quad in the always drawn tail.
static inline int quadFace(const DrawVertex* q) {
    int a;
    if (q[1].x == q[0].x && q[2].x == q[0].x && q[3].x == q[0].x &&
        q[4].x == q[0].x && q[5].x == q[0].x) a = 0;
    else if (q[1].y == q[0].y && q[2].y == q[0].y && q[3].y == q[0].y &&
             q[4].y == q[0].y && q[5].y == q[0].y) a = 1;
    else if (q[1].z == q[0].z && q[2].z == q[0].z && q[3].z == q[0].z &&
             q[4].z == q[0].z && q[5].z == q[0].z) a = 2;
    else return -1;

    const int a1 = (a + 1) % 3, a2 = (a + 2) % 3;
    const short* p0 = &q[0].x;
    const short* p1 = &q[1].x;
    const short* p2 = &q[2].x;
    const int nrm = (p1[a1] - p0[a1]) * (p2[a2] - p0[a2])
                  - (p1[a2] - p0[a2]) * (p2[a1] - p0[a1]);
    if (nrm == 0) return -1;
    return a * 2 + (nrm > 0 ? 1 : 0);
}

#define MAX_SORT_QUADS 10923
static unsigned char s_quadFace[MAX_SORT_QUADS];

// Sorts the first sortN vertices into facing groups and copies the rest through
// untouched, which is how the lava tail stays in place.
DrawVertex* chunkPackFinishSorted(const DrawVertex* staging, int n, int sortN,
                                  unsigned short faceEnd[6]) {
    for (int b = 0; b < 6; b++) faceEnd[b] = 0;

    const int nq = sortN / 6;
    if (sortN <= 0 || sortN > n || sortN % 6 || nq > MAX_SORT_QUADS || n > 65535)
        return chunkPackFinish(staging, n);

    profBegin(PROF_MALLOC);
    DrawVertex* d = (DrawVertex*)memalign(64, (size_t)n * sizeof(DrawVertex));
    profEnd(PROF_MALLOC);
    if (!d) return 0;

    int cnt[7] = { 0, 0, 0, 0, 0, 0, 0 };
    for (int q = 0; q < nq; q++) {
        const int f = quadFace(staging + q * 6);
        const unsigned char b = (unsigned char)(f < 0 ? 6 : f);
        s_quadFace[q] = b;
        cnt[b]++;
    }

    int start[7], at = 0;
    for (int b = 0; b < 7; b++) { start[b] = at; at += cnt[b]; }
    for (int b = 0; b < 6; b++) faceEnd[b] = (unsigned short)((start[b] + cnt[b]) * 6);

    for (int q = 0; q < nq; q++) {
        const unsigned char b = s_quadFace[q];
        DrawVertex* dst = d + start[b] * 6;
        start[b]++;
        const DrawVertex* src = staging + q * 6;
        for (int i = 0; i < 6; i++) dst[i] = src[i];
    }
    if (n > sortN)
        memcpy_vfpu(d + sortN, staging + sortN, (size_t)(n - sortN) * sizeof(DrawVertex));

    dcacheFlush(d, (size_t)n * sizeof(DrawVertex));
    return d;
}

DrawVertex* chunkPackFinish(const DrawVertex* staging, int n) {
    profBegin(PROF_MALLOC);
    DrawVertex* d = (DrawVertex*)memalign(64, (size_t)n * sizeof(DrawVertex));
    profEnd(PROF_MALLOC);
    if (!d) return 0;

    memcpy_vfpu(d, staging, (size_t)n * sizeof(DrawVertex));
    dcacheFlush(d, (size_t)n * sizeof(DrawVertex));
    return d;
}

DrawVertex* chunkPack(const ChunkVertex* s, int n, int ox, int oy, int oz,
                      float* ylo, float* yhi) {
    profBegin(PROF_MALLOC);
    DrawVertex* d = (DrawVertex*)memalign(64, (size_t)n * sizeof(DrawVertex));
    profEnd(PROF_MALLOC);
    if (!d) return 0;
    int qlo = 32767, qhi = -32768;
    chunkPackInto(d, s, n, ox, oy, oz, &qlo, &qhi);
    if (ylo) *ylo = chunkPackDecodeY(qlo, oy);
    if (yhi) *yhi = chunkPackDecodeY(qhi, oy);
    dcacheFlush(d, (size_t)n * sizeof(DrawVertex));
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

extern float g_camX, g_camY, g_camZ;

static inline void sectionFacing(const ChunkSection* s, bool want[6]) {
    want[0] = g_camX < (float)(s->ox + CHUNK_SX);
    want[1] = g_camX > (float)s->ox;
    want[2] = g_camY < (float)(s->oy + SECTION_SY);
    want[3] = g_camY > (float)s->oy;
    want[4] = g_camZ < (float)(s->oz + CHUNK_SZ);
    want[5] = g_camZ > (float)s->oz;
}

// Emits the facing groups the camera can see, merging neighbours into one call.
static void drawFacing(const DrawVertex* mesh, const unsigned short faceEnd[6],
                       const bool want[6], unsigned int fmt) {
    for (int b = 0; b < 6; ) {
        if (!want[b]) { b++; continue; }
        int e = b;
        while (e < 6 && want[e]) e++;
        const int first = b ? faceEnd[b - 1] : 0;
        const int count = faceEnd[e - 1] - first;
        if (count > 0) sceGumDrawArray(GU_TRIANGLES, fmt, count, 0, mesh + first);
        b = e;
    }
}

int chunkOpaqueDrawCount(const ChunkSection* s) {
    if (s->vertexCount <= 0 || !s->mesh) return 0;
    if (!s->faceEnd[5]) return s->vertexCount;
    bool want[6];
    sectionFacing(s, want);
    int n = s->vertexCount - s->faceEnd[5];
    for (int b = 0; b < 6; b++)
        if (want[b]) n += s->faceEnd[b] - (b ? s->faceEnd[b - 1] : 0);
    return n;
}

int chunkNoMipDrawCount(const ChunkSection* s) {
    if (s->noMipCount <= 0 || !s->noMip) return 0;
    if (!s->nmFaceEnd[5]) return s->noMipCount;
    bool want[6];
    sectionFacing(s, want);
    int n = s->noMipCount - s->nmFaceEnd[5];
    for (int b = 0; b < 6; b++)
        if (want[b]) n += s->nmFaceEnd[b] - (b ? s->nmFaceEnd[b - 1] : 0);
    return n;
}

void chunkDrawSection(const ChunkSection* s) {
    if (s->vertexCount <= 0 || !s->mesh) return;
    chunkSetModel(s, SEAM_OVERSCALE_OPAQUE);
    const unsigned int fmt = GU_TEXTURE_16BIT | GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_3D;

    if (!s->faceEnd[5]) {
        sceGumDrawArray(GU_TRIANGLES, fmt, s->vertexCount, 0, s->mesh);
        return;
    }

    // A face is only visible from its own side of its plane, so roughly half the
    // groups never have to reach the GE. The bounds test errs towards drawing.
    bool want[6];
    sectionFacing(s, want);
    drawFacing(s->mesh, s->faceEnd, want, fmt);

    const int tail = s->vertexCount - s->faceEnd[5];
    if (tail > 0) sceGumDrawArray(GU_TRIANGLES, fmt, tail, 0, s->mesh + s->faceEnd[5]);
}

void chunkDrawWaterSection(const ChunkSection* s) {
    if (s->waterCount > 0 && s->water) {
        chunkSetModel(s, SEAM_OVERSCALE_TRANS);
        sceGumDrawArray(GU_TRIANGLES,
                        GU_TEXTURE_16BIT | GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_3D,
                        s->waterCount, 0, s->water);
    }
}

void chunkDrawLeavesSection(const ChunkSection* s) {
    if (s->leavesCount > 0 && s->leaves) {
        chunkSetModel(s, SEAM_OVERSCALE_OPAQUE);
        sceGumDrawArray(GU_TRIANGLES,
                        GU_TEXTURE_16BIT | GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_3D,
                        s->leavesCount, 0, s->leaves);
    }
}

void chunkDrawNoMipSection(const ChunkSection* s, int part) {
    if (s->noMipCount <= 0 || !s->noMip) return;
    const unsigned int fmt = GU_TEXTURE_16BIT | GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_3D;

    if (part != NOMIP_LAVA && s->nmFaceEnd[5]) {
        chunkSetModel(s, SEAM_OVERSCALE_OPAQUE);
        bool want[6];
        sectionFacing(s, want);
        drawFacing(s->noMip, s->nmFaceEnd, want, fmt);

        // Cross shaped plants and the lava tail sort into no group and always draw.
        const int end = (part == NOMIP_NO_LAVA) ? s->noMipLavaStart : s->noMipCount;
        const int tail = end - s->nmFaceEnd[5];
        if (tail > 0) sceGumDrawArray(GU_TRIANGLES, fmt, tail, 0, s->noMip + s->nmFaceEnd[5]);
        return;
    }

    int first = 0, count = s->noMipCount;
    if (part == NOMIP_NO_LAVA) count = s->noMipLavaStart;
    else if (part == NOMIP_LAVA) { first = s->noMipLavaStart; count = s->noMipCount - first; }
    if (count <= 0) return;
    chunkSetModel(s, SEAM_OVERSCALE_OPAQUE);
    sceGumDrawArray(GU_TRIANGLES, fmt, count, 0, s->noMip + first);
}

void chunkFreeMesh(ChunkMesh* c) {
    for (int si = 0; si < N_SECTIONS; si++) {
        ChunkSection* s = &c->sec[si];
        if (s->mesh)   { guDeferFree(s->mesh);   s->mesh = 0; }
        if (s->water)  { guDeferFree(s->water);  s->water = 0; }
        if (s->leaves) { guDeferFree(s->leaves); s->leaves = 0; }
        if (s->noMip)  { guDeferFree(s->noMip);  s->noMip = 0; }
        s->vertexCount = s->waterCount = s->leavesCount = s->noMipCount = 0;
        s->noMipLavaStart = 0;
        for (int b = 0; b < 6; b++) s->faceEnd[b] = s->nmFaceEnd[b] = 0;
    }
}
