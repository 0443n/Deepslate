
#include "world/level/chunk/chunk.h"
#include <malloc.h>
#include <pspkernel.h>

unsigned int g_meshBytes = 0;

void chunkFreeLayer(MeshLayer* l) {
    if (l->v) { free(l->v); g_meshBytes -= (unsigned int)l->bytes; }
    l->v = 0; l->idx = 0; l->count = 0; l->unique = 0; l->bytes = 0;
}

static int packInto(DrawVertex* d, unsigned short* ix, int uniCap,
                    const ChunkVertex* s, int n, int ox, int oy, int oz) {
    int nUnique = 0;
    for (int g = 0; g < n; g += 6) {
        int m = (n - g < 6) ? (n - g) : 6;
        int base = nUnique;
        for (int k = 0; k < m; k++) {
            const ChunkVertex& sv = s[g + k];
            DrawVertex q;
            q.u = uvQ(sv.u); q.v = uvQ(sv.v); q.color = sv.color;
            q.x = posQ(sv.x - ox); q.y = posQ(sv.y - oy); q.z = posQ(sv.z - oz); q.w = 0;

            int found = -1;
            for (int u = base; u < nUnique; u++) {
                if (d[u].x == q.x && d[u].y == q.y && d[u].z == q.z &&
                    d[u].u == q.u && d[u].v == q.v && d[u].color == q.color) { found = u; break; }
            }
            if (found < 0) {
                if (nUnique >= uniCap) return -1;
                found = nUnique; d[nUnique++] = q;
            }
            ix[g + k] = (unsigned short)found;
        }
    }
    return nUnique;
}

bool chunkPack(MeshLayer* out, const ChunkVertex* s, int n, int ox, int oy, int oz) {
    out->v = 0; out->idx = 0; out->count = 0; out->unique = 0; out->bytes = 0;
    if (n <= 0) return true;

    int uniCap = (n / 6) * 4 + (n % 6);

    for (int attempt = 0; attempt < 2; attempt++) {
        size_t vBytes = ((size_t)uniCap * sizeof(DrawVertex) + 15u) & ~(size_t)15u;
        size_t iBytes = (size_t)n * sizeof(unsigned short);
        unsigned char* block = (unsigned char*)memalign(16, vBytes + iBytes);
        if (!block) return false;

        DrawVertex*     d  = (DrawVertex*)block;
        unsigned short* ix = (unsigned short*)(block + vBytes);

        int nUnique = packInto(d, ix, uniCap, s, n, ox, oy, oz);
        if (nUnique < 0) { free(block); uniCap = n; continue; }

        if (nUnique > 65536) { free(block); return false; }

        sceKernelDcacheWritebackInvalidateRange(block, vBytes + iBytes);
        out->v = d; out->idx = ix; out->count = n; out->unique = nUnique;
        out->bytes = (int)(vBytes + iBytes);
        g_meshBytes += (unsigned int)out->bytes;
        return true;
    }
    return false;
}
