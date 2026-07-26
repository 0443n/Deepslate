
#include "world/level/world.h"

#include <stdlib.h>
#include <string.h>

unsigned int g_blockOomDrops = 0;

void blockAlloc(World* w) {

    memset(w->bsec, 0, sizeof(w->bsec));
    for (int i = 0; i < BS_SECTIONS; i++) w->bsec[i].uniform = BLOCK_AIR;
    w->blockPages = 0;
    w->blockPageBytes = 0;
}

void blockFree(World* w) {
    for (int i = 0; i < BS_SECTIONS; i++)
        if (w->bsec[i].page) { free(w->bsec[i].page); w->bsec[i].page = 0; }
    blockAlloc(w);
}

unsigned int blockBytes(const World* w) {
    return (unsigned int)sizeof(w->bsec) + w->blockPageBytes;
}

void blockStats(const World* w, int* uniform, int* paletted, int* raw) {
    int u = 0, p = 0, r = 0;
    for (int i = 0; i < BS_SECTIONS; i++) {
        if (!w->bsec[i].page) u++;
        else if (w->bsec[i].palN) p++;
        else r++;
    }
    *uniform = u; *paletted = p; *raw = r;
}

static bool secPageUp(World* w, BlockSection* s) {
    unsigned char* p = (unsigned char*)calloc(1, BS_PAL_PAGE);
    if (!p) { g_blockOomDrops++; return false; }
    s->pal[0] = s->uniform;
    s->palN = 1;
    s->page = p;
    w->blockPages++;
    w->blockPageBytes += BS_PAL_PAGE;
    return true;
}

static bool secRawUp(World* w, BlockSection* s) {
    unsigned char* raw = (unsigned char*)malloc(BS_CELLS);
    if (!raw) { g_blockOomDrops++; return false; }
    const unsigned char* p = s->page;
    for (int i = 0; i < BS_CELLS; i += 2) {
        unsigned char b = p[i >> 1];
        raw[i]     = s->pal[b & 0x0F];
        raw[i + 1] = s->pal[b >> 4];
    }
    free(s->page);
    s->page = raw;
    s->palN = 0;
    w->blockPageBytes += BS_CELLS - BS_PAL_PAGE;
    return true;
}

static bool secWrite(World* w, BlockSection* s, int off, unsigned char id) {
    if (!s->palN) { s->page[off] = id; return true; }
    int slot = -1;
    for (int i = 0; i < s->palN; i++) if (s->pal[i] == id) { slot = i; break; }
    if (slot < 0) {
        if (s->palN == BS_PAL_MAX) {
            if (!secRawUp(w, s)) return false;
            s->page[off] = id;
            return true;
        }
        slot = s->palN++;
        s->pal[slot] = id;
    }
    unsigned char& b = s->page[off >> 1];
    b = (off & 1) ? (unsigned char)((b & 0x0F) | (slot << 4))
                  : (unsigned char)((b & 0xF0) | slot);
    return true;
}

bool blockPut(World* w, int x, int y, int z, unsigned char id) {
    if (y < 0 || y >= WORLD_H || x < 0 || x >= WORLD_W || z < 0 || z >= WORLD_D) return false;
    BlockSection* s = &w->bsec[bsSection(x, y, z)];
    if (!s->page) {

        if (id == s->uniform) return true;
        if (!secPageUp(w, s)) return false;
    }
    return secWrite(w, s, bsOffset(x, y, z), id);
}

void blockColumnGet(const World* w, int x, int z, unsigned char* out128) {

    for (int y = 0; y < WORLD_H; y++) out128[y] = worldBlock(w, x, y, z);
}

void blockColumnPut(World* w, int x, int z, const unsigned char* in128) {
    if (x < 0 || x >= WORLD_W || z < 0 || z >= WORLD_D) return;

    for (int y0 = 0; y0 < WORLD_H; y0 += SECTION_SY) {
        BlockSection* s = &w->bsec[bsSection(x, y0, z)];
        int off0 = bsOffset(x, y0, z);
        for (int dy = 0; dy < SECTION_SY; dy++) {
            unsigned char id = in128[y0 + dy];
            if (!s->page) {
                if (id == s->uniform) continue;
                if (!secPageUp(w, s)) break;
            }
            secWrite(w, s, off0 + dy, id);
        }
    }
}
