#include "client/renderer/portal_anim.h"
#include "platform/png_loader.h"
#include "platform/dcache.h"
#include "platform/time.h"
#include "gpu/texture.h"
#include <cstring>

extern Texture g_terrain;
extern bool    g_haveTerrain;

// Vanilla ships the portal as a strip of 32 frames and plays one per tick, so the
// strip is blitted over the atlas cell the same way the water animator writes its own.
#define PORTAL_FRAMES 32
#define PORTAL_TILE   16
#define PORTAL_COL    8
#define PORTAL_ROW    14

static unsigned int s_frames[PORTAL_FRAMES * PORTAL_TILE * PORTAL_TILE];
static bool s_loaded = false;
static bool s_tried  = false;

static bool loadFrames() {
    int w = 0, h = 0;
    PngReader* r = pngOpen("data/images/portal.png", &w, &h);
    if (!r) return false;
    bool ok = (w == PORTAL_TILE && h == PORTAL_FRAMES * PORTAL_TILE);
    for (int y = 0; ok && y < h; y++)
        ok = pngReadRow(r, (unsigned char*)&s_frames[y * PORTAL_TILE]);
    pngClose(r);
    return ok;
}

void animatePortalTexture() {
    static float timer = 0.0f;
    static int   frame = 0;

    if (!g_haveTerrain) return;
    if (!s_tried) { s_tried = true; s_loaded = loadFrames(); }
    // Without the strip the atlas keeps its own still frame, which is the right fallback.
    if (!s_loaded) return;

    float now = gameSeconds();
    if (now - timer < 0.05f) return;
    timer = now;

    unsigned int* tex  = (unsigned int*)g_terrain.data;
    int           texW = g_terrain.texW;
    const unsigned int* src = &s_frames[frame * PORTAL_TILE * PORTAL_TILE];
    for (int y = 0; y < PORTAL_TILE; y++)
        memcpy(&tex[(PORTAL_ROW * PORTAL_TILE + y) * texW + PORTAL_COL * PORTAL_TILE],
               &src[y * PORTAL_TILE], PORTAL_TILE * sizeof(unsigned int));

    frame = (frame + 1) % PORTAL_FRAMES;
    dcacheFlush(&tex[PORTAL_ROW * PORTAL_TILE * texW],
                PORTAL_TILE * texW * sizeof(unsigned int));
}
