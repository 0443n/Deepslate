#include "world/level/chunk/chunk.h"
#include <pspgu.h>
#include <pspgum.h>
#include <malloc.h>
#include <pspkernel.h>
#include <pspgum.h>

float g_relBaseX = 0.0f, g_relBaseY = 0.0f, g_relBaseZ = 0.0f;

#define SEAM_OVERSCALE_OPAQUE (32768.0f / 32753.0f)
#define SEAM_OVERSCALE_TRANS  (32768.0f / 32763.0f)

static inline void chunkSetModel(const ChunkSection* s, float scaleMul) {
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    ScePspFVector3 t = { (float)s->ox - g_relBaseX, (float)s->oy - g_relBaseY, (float)s->oz - g_relBaseZ };
    sceGumTranslate(&t);
    float sm = POS_MODEL_SCALE * scaleMul;
    ScePspFVector3 sc = { sm, sm, sm };
    sceGumScale(&sc);
}

#define CHUNK_FMT (GU_TEXTURE_16BIT | GU_COLOR_8888 | GU_VERTEX_16BIT | \
                   GU_INDEX_16BIT | GU_TRANSFORM_3D)

static inline void drawLayer(const MeshLayer* l) {
    if (l->count > 0 && l->v) sceGumDrawArray(GU_TRIANGLES, CHUNK_FMT, l->count, l->idx, l->v);
}

void chunkDrawSection(const ChunkSection* s) {
    if (s->op.count <= 0 || !s->op.v) return;
    chunkSetModel(s, SEAM_OVERSCALE_OPAQUE);
    drawLayer(&s->op);
}

void chunkDrawWaterSection(const ChunkSection* s) {
    if (s->wa.count > 0 && s->wa.v) { chunkSetModel(s, SEAM_OVERSCALE_TRANS); drawLayer(&s->wa); }
}

void chunkDrawLeavesSection(const ChunkSection* s) {
    if (s->le.count > 0 && s->le.v) { chunkSetModel(s, SEAM_OVERSCALE_OPAQUE); drawLayer(&s->le); }
}

void chunkDrawNoMipSection(const ChunkSection* s) {
    if (s->nm.count > 0 && s->nm.v) { chunkSetModel(s, SEAM_OVERSCALE_OPAQUE); drawLayer(&s->nm); }
}

void chunkFreeMesh(ChunkMesh* c) {
    for (int si = 0; si < N_SECTIONS; si++) {
        ChunkSection* s = &c->sec[si];
        chunkFreeLayer(&s->op); chunkFreeLayer(&s->wa);
        chunkFreeLayer(&s->le); chunkFreeLayer(&s->nm);
    }
}
