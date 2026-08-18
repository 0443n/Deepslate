
#include "client/renderer/entity/entity_renderer.h"
#include "client/renderer/particle.h"
#include "client/renderer/render.h"
#include "world/level/level.h"
#include "world/entity/local_player.h"
#include "world/entity/entity.h"
#include "world/level/world.h"
#include "world/level/chunk/chunk.h"
#include "client/player/player_state.h"
#include "platform/path.h"
#include "gpu/texture.h"
#include "gpu/gu.h"
#include "util/mth.h"
#include <math.h>
#include <pspgu.h>
#include <pspgum.h>
#include <pspkernel.h>

extern World g_world;

static Texture s_shadowTex;
static bool    s_shadowReady = false;
static bool    s_shadowOk    = false;

static void buildShadowTexture() {
    s_shadowReady = true;
    s_shadowOk = textureLoad(assetPath("data/images/misc/shadow.png"), &s_shadowTex)
              || textureLoad("data/images/misc/shadow.png", &s_shadowTex);
}

static ChunkVertex s_verts[64 * 6];
static int         s_vcount = 0;

static bool shadowBuried(int xt, int yt, int zt) {
    unsigned char here = worldBlock(&g_world, xt, yt, zt);
    if (here == BLOCK_AIR || !isSolidPhys(here)) return false;
    BlockAABB b[3];
    const int nb = Tile::tiles[here]->getAABB(&g_world, xt, yt, zt, b);
    for (int i = 0; i < nb; i++)
        if (b[i].y1 > (float)yt + 1.0f / 64.0f) return true;
    return false;
}

static void renderTileShadow(float x, float y, float z, int xt, int yt, int zt,
                             float pow, float r, float xo, float yo, float zo) {

    float a = ((pow - (y - (yt + yo)) / 2.0f) * 0.5f) * (lightRawAt(&g_world, xt, yt, zt) / 15.0f);
    if (a < 0) return;
    if (a > 1) a = 1;
    unsigned int col = ((unsigned int)(a * 255.0f)) << 24;

    float x0 = xt + xo;
    float x1 = xt + 1.0f + xo;
    float y0 = yt + yo + 1.0f / 64.0f;
    float z0 = zt + zo;
    float z1 = zt + 1.0f + zo;

    float u0 = (x - x0) / 2.0f / r + 0.5f;
    float u1 = (x - x1) / 2.0f / r + 0.5f;
    float v0 = (z - z0) / 2.0f / r + 0.5f;
    float v1 = (z - z1) / 2.0f / r + 0.5f;

    if (s_vcount + 6 > (int)(sizeof(s_verts) / sizeof(s_verts[0]))) return;
    ChunkVertex* q = &s_verts[s_vcount];
    q[0] = (ChunkVertex){ u0, v0, col, x0, y0, z0 };
    q[1] = (ChunkVertex){ u0, v1, col, x0, y0, z1 };
    q[2] = (ChunkVertex){ u1, v1, col, x1, y0, z1 };
    q[3] = (ChunkVertex){ u1, v1, col, x1, y0, z1 };
    q[4] = (ChunkVertex){ u1, v0, col, x1, y0, z0 };
    q[5] = (ChunkVertex){ u0, v0, col, x0, y0, z0 };
    s_vcount += 6;
}

void renderEntityShadow(float x, float y, float z, float off, float radius, float pow) {
    if (!s_shadowReady) buildShadowTexture();
    if (!s_shadowOk) return;

    float r = radius;

    float ex = x;
    float ey = y + off;
    float ez = z;

    int x0 = Mth::floor(ex - r), x1 = Mth::floor(ex + r);
    int y0 = Mth::floor(ey - r), y1 = Mth::floor(ey);
    int z0 = Mth::floor(ez - r), z1 = Mth::floor(ez + r);

    float xo = x - ex;
    float yo = y - ey;
    float zo = z - ez;

    s_vcount = 0;
    for (int xt = x0; xt <= x1; xt++)
        for (int yt = y0; yt <= y1; yt++)
            for (int zt = z0; zt <= z1; zt++) {
                int t = worldBlock(&g_world, xt, yt - 1, zt);
                if (t > 0 && isCubeShaped((unsigned char)t) && lightRawAt(&g_world, xt, yt, zt) > 3
                    && !shadowBuried(xt, yt, zt))
                    renderTileShadow(x, y + off, z, xt, yt, zt, pow, r, xo, yo + off, zo);
            }
    if (s_vcount == 0) return;

    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuDepthMask(GU_TRUE);
    sceGuDisable(GU_CULL_FACE);

    sceGuDepthOffset(80);

    sceGumMatrixMode(GU_MODEL);
    sceGumPushMatrix();
    sceGumLoadIdentity();
    { ScePspFVector3 b = { -g_relBaseX, -g_relBaseY, -g_relBaseZ }; sceGumTranslate(&b); }
    textureBindNoMip(&s_shadowTex);
    void* v = guFrameCopy(s_verts, s_vcount * sizeof(ChunkVertex));
    if (v) sceGumDrawArray(GU_TRIANGLES,
                    GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
                    s_vcount, 0, v);
    sceGumPopMatrix();
    sceGuDepthOffset(0);

    sceGuEnable(GU_CULL_FACE);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);

    sceGuDepthMask(GU_FALSE);
}

static void drawFlameMesh(void* verts, int n, float x, float y, float z, float s, float h) {
    extern Texture g_terrain;

    sceGuDisable(GU_CULL_FACE);
    sceGuEnable(GU_ALPHA_TEST);
    sceGuAlphaFunc(GU_GREATER, 0, 0xff);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);

    sceGumMatrixMode(GU_MODEL);
    sceGumPushMatrix();
    sceGumLoadIdentity();
    { ScePspFVector3 b = { x - g_relBaseX, y - g_relBaseY, z - g_relBaseZ }; sceGumTranslate(&b); }

    sceGumRotateY(-g_camYawNow * (3.14159265f / 180.0f));
    { ScePspFVector3 sc = { s, s, s }; sceGumScale(&sc); }

    { ScePspFVector3 t = { 0.0f, 0.0f, -0.3f + (float)((int)(h / s)) * 0.02f }; sceGumTranslate(&t); }

    textureBindNoMip(&g_terrain);
    sceGumDrawArray(GU_TRIANGLES,
                    GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
                    n, 0, verts);
    sceGumPopMatrix();

    sceGuEnable(GU_CULL_FACE);
}

void renderEntityFlame(float x, float y, float z, float bbY0, float w, float h) {
    extern bool g_haveTerrain;
    if (!g_haveTerrain) return;

    const float s = w * 1.4f;
    if (s <= 0.0f) return;
    float hh = h / s;
    float yo = y - bbY0;
    const float yo0 = yo;
    float r  = 0.5f;
    float zo = 0.0f;
    int   ss = 0;

    const float T = 1.0f / 16.0f, HT = T / 32.0f;
    const unsigned int col = 0xFFFFFFFFu;

    struct FlameCache { float s, h, yo; void* verts; int count; unsigned int frame; };
    static FlameCache cache[4];
    static int cacheN = 0;
    if (cache[0].frame != guFrameId()) { cacheN = 0; for (int i = 0; i < 4; i++) cache[i].frame = guFrameId(); }
    for (int i = 0; i < cacheN; i++)
        if (cache[i].s == s && cache[i].h == h && cache[i].yo == yo) {
            if (!cache[i].verts) return;

            drawFlameMesh(cache[i].verts, cache[i].count, x, y, z, s, h);
            return;
        }

    s_vcount = 0;
    while (hh > 0.0f && s_vcount + 6 <= (int)(sizeof(s_verts) / sizeof(s_verts[0]))) {
        const int layer = ss & 1;
        float u0 = 15.0f * T + HT, u1 = 16.0f * T - HT;
        const float v0 = (1 + layer) * T + HT, v1 = (2 + layer) * T - HT;
        if ((ss / 2) % 2 == 0) { float t = u1; u1 = u0; u0 = t; }

        const float y0 =  0.0f - yo, y1 = 1.4f - yo;
        ChunkVertex* q = &s_verts[s_vcount];
        q[0] = (ChunkVertex){ u1, v1, col,  r, y0, zo };
        q[1] = (ChunkVertex){ u0, v1, col, -r, y0, zo };
        q[2] = (ChunkVertex){ u0, v0, col, -r, y1, zo };
        q[3] = (ChunkVertex){ u0, v0, col, -r, y1, zo };
        q[4] = (ChunkVertex){ u1, v0, col,  r, y1, zo };
        q[5] = (ChunkVertex){ u1, v1, col,  r, y0, zo };
        s_vcount += 6;

        hh -= 0.45f; yo -= 0.45f; r *= 0.9f; zo += 0.03f; ss++;
    }

    void* v = s_vcount ? guFrameCopy(s_verts, s_vcount * sizeof(ChunkVertex)) : 0;
    if (cacheN < 4) {
        cache[cacheN].s = s; cache[cacheN].h = h; cache[cacheN].yo = yo0;
        cache[cacheN].verts = v; cache[cacheN].count = s_vcount;
        cacheN++;
    }
    if (!v || s_vcount == 0) return;
    drawFlameMesh(v, s_vcount, x, y, z, s, h);
}

void EntityRenderer::postRender(Entity* entity, float x, float y, float z, float a) {

    if (entity->isOnFire()) {
        renderEntityFlame(x, y, z, entity->bb.y0, entity->bbWidth, entity->bbHeight);
        particlesEntityFlame(entity->entityId, x, y - entity->heightOffset, z,
                             entity->bbWidth, entity->bbHeight, entity->xd, entity->zd);
    }

    if (shadowRadius <= 0.0f || !g_level.player) return;
    float dx = entity->x - g_level.player->x, dy = entity->y - g_level.player->y, dz = entity->z - g_level.player->z;
    float dist = dx * dx + dy * dy + dz * dz;
    float pow = (1.0f - dist / (16.0f * 16.0f)) * shadowStrength;
    float r = entity->isBaby() ? shadowRadius * 0.5f : shadowRadius;
    if (pow > 0.0f) renderEntityShadow(x, y, z, entity->getShadowHeightOffs(), r, pow);
}
