#include "util/prof.h"
#include "platform/fbtext.h"
#include "client/renderer/render.h"
#include "client/renderer/entity/mob_model.h"
#include "world/entity/mob.h"
#include "world/level/world.h"
#include "world/level/chunk/chunk.h"
#include "client/renderer/item_model.h"
#include "world/item/item.h"
#include "gpu/texture.h"
#include "gpu/gu.h"
#include <pspgu.h>
#include <pspgum.h>
#include <pspkernel.h>
#include <math.h>

extern World g_world;

static const float DEG2RAD = 3.14159265f / 180.0f;

void mobBuildBox(MobVertex* out, float x0, float y0, float z0,
                 float x1, float y1, float z1, int tx, int ty, int w, int h, int d,
                 bool mirror, float grow, float texW, float texH) {
    x0 -= grow; y0 -= grow; z0 -= grow;
    x1 += grow; y1 += grow; z1 += grow;

    const float W = texW, H = texH;
    int n = 0;
    auto addPoly = [&](float ax, float ay, float az, float bx, float by, float bz,
                       float cx, float cy, float cz, float dx, float dy, float dz,
                       float u0, float v0, float u1, float v1) {
        if (mirror) { float t = u0; u0 = u1; u1 = t; }
        out[n++] = {u0, v0, ax, ay, az};
        out[n++] = {u1, v0, bx, by, bz};
        out[n++] = {u1, v1, cx, cy, cz};
        out[n++] = {u1, v1, cx, cy, cz};
        out[n++] = {u0, v1, dx, dy, dz};
        out[n++] = {u0, v0, ax, ay, az};
    };
    addPoly(x1,y0,z1, x0,y0,z1, x0,y0,z0, x1,y0,z0, (tx+d+w)/W,(ty)/H,     (tx+d)/W,(ty+d)/H);
    addPoly(x0,y0,z0, x0,y0,z1, x0,y1,z1, x0,y1,z0, (tx+d)/W,(ty+d)/H,     (tx)/W,(ty+d+h)/H);
    addPoly(x1,y0,z1, x1,y0,z0, x1,y1,z0, x1,y1,z1, (tx+2*d+w)/W,(ty+d)/H, (tx+d+w)/W,(ty+d+h)/H);
    addPoly(x1,y1,z0, x0,y1,z0, x0,y1,z1, x1,y1,z1, (tx+d+w)/W,(ty+d)/H,   (tx+d+2*w)/W,(ty)/H);
    addPoly(x1,y0,z0, x0,y0,z0, x0,y1,z0, x1,y1,z0, (tx+d+w)/W,(ty+d)/H,   (tx+d)/W,(ty+d+h)/H);
    addPoly(x0,y0,z1, x1,y0,z1, x1,y1,z1, x0,y1,z1, (tx+2*d+2*w)/W,(ty+d)/H, (tx+2*d+w)/W,(ty+d+h)/H);
}

static inline unsigned int mul(unsigned int a, unsigned int b) {
    unsigned int aa = ((a >> 24) & 0xFF) * ((b >> 24) & 0xFF) / 255;
    unsigned int bb = ((a >> 16) & 0xFF) * ((b >> 16) & 0xFF) / 255;
    unsigned int gg = ((a >> 8)  & 0xFF) * ((b >> 8)  & 0xFF) / 255;
    unsigned int rr = ( a        & 0xFF) * ( b        & 0xFF) / 255;
    return (aa << 24) | (bb << 16) | (gg << 8) | rr;
}

static inline unsigned int fbits(float f) { union { float f; unsigned int u; } c; c.f = f; return c.u; }

// The old subtract loop needed one turn per revolution, so a rotation that had
// gone wild froze the machine instead of drawing a wrong angle.
static float wrapDeg(float d) {
    if (d >= -180.0f && d <= 180.0f) return d;
    if (!(d > -1.0e7f && d < 1.0e7f)) return 0.0f;
    return d - 360.0f * floorf((d + 180.0f) * (1.0f / 360.0f));
}

MobAnim mobAnimSetup(Mob* mob, float rot, float a) {
    MobAnim m;
    fbTextHex(0, 24, 1, 2);
    fbTextHex(2, 0, fbits(mob->yBodyRot), 8);
    fbTextHex(2, 9, fbits(mob->yBodyRotO), 8);
    fbTextHex(2, 18, fbits(mob->xRot), 8);
    fbTextHex(3, 0, fbits(mob->walkAnimPos), 8);
    fbTextHex(3, 9, fbits(mob->walkAnimSpeed), 8);
    fbTextHex(3, 18, fbits(rot), 8);
    float dBody = wrapDeg(mob->yBodyRot - mob->yBodyRotO);
    m.bodyRot = mob->yBodyRotO + dBody * a;
    m.headYaw = wrapDeg(rot - m.bodyRot);
    m.pitch = mob->xRotO + (mob->xRot - mob->xRotO) * a;
    m.speed = mob->walkAnimSpeedO + (mob->walkAnimSpeed - mob->walkAnimSpeedO) * a;
    if (m.speed > 1.0f) m.speed = 1.0f;
    m.pos = mob->walkAnimPos - mob->walkAnimSpeed * (1.0f - a);
    fbTextHex(0, 24, 2, 2);
    if (mob->isBaby()) m.pos *= 3.0f;
    fbTextHex(0, 24, 3, 2);
    return m;
}

void mobRenderParts(Mob* mob, MobPart* parts, int count, Texture* tex,
                    float x, float y, float z, float ibody, float a, unsigned int tint,
                    float babyHeadY, float babyHeadZ, float modelScale,
                    float overlayWhite, int bowPartIndex, float modelScaleY, short heldItemId) {
    phaseRow(1, 7);
    fbTextHex(0, 24, 4, 2);
    float feet = y - mob->heightOffset;

    int bx = (int)floorf(x), by = (int)floorf(feet + mob->bbHeight * 0.66f), bz = (int)floorf(z);
    unsigned int brCol = g_brightColor[lightRawAt(&g_world, bx, by, bz)];
    if (tint != 0xFFFFFFFFu) brCol = mul(brCol, tint);

    if (mob->hurtTime > 0 || mob->deathTime > 0) {
        const unsigned int HURT_GB = 140;
        unsigned int r  =  brCol         & 0xFFu;
        unsigned int g  = (((brCol >> 8)  & 0xFFu) * HURT_GB) / 255;
        unsigned int b  = (((brCol >> 16) & 0xFFu) * HURT_GB) / 255;
        brCol = (brCol & 0xFF000000u) | (b << 16) | (g << 8) | r;
    }

    if (count > MOB_MAX_PARTS) count = MOB_MAX_PARTS;

    phaseRow(1, 8);
    textureBind(tex);
    sceGuDisable(GU_CULL_FACE);
    phaseRow(1, 9);

    sceGumMatrixMode(GU_MODEL);
    sceGumPushMatrix();
    sceGumLoadIdentity();
    ScePspFVector3 tpos = { x - g_relBaseX, feet - g_relBaseY, z - g_relBaseZ }; sceGumTranslate(&tpos);
    sceGumRotateY((180.0f - ibody) * DEG2RAD);
    if (mob->deathTime > 0) {
        float fall = sqrtf(((mob->deathTime + a - 1.0f) / 20.0f) * 1.6f);
        if (fall > 1.0f) fall = 1.0f;
        sceGumRotateZ(fall * 90.0f * DEG2RAD);
    }
    float msXZ = modelScale / 16.0f;
    float msY  = ((modelScaleY > 0.0f) ? modelScaleY : modelScale) / 16.0f;
    ScePspFVector3 sc = { -msXZ, -msY, msXZ }; sceGumScale(&sc);

    ScePspFVector3 gnd = { 0.0f, -24.0f - 0.125f, 0.0f }; sceGumTranslate(&gnd);

    bool baby = mob->isBaby();
    #define MOB_BABY_XFORM(i)                                                          \
        if (baby) {                                                                    \
            if (parts[i].head) {                                                       \
                ScePspFVector3 ho = { 0.0f, babyHeadY, babyHeadZ }; sceGumTranslate(&ho); \
            } else {                                                                   \
                ScePspFVector3 hs = { 0.5f, 0.5f, 0.5f }; sceGumScale(&hs);            \
                ScePspFVector3 bo = { 0.0f, 24.0f, 0.0f }; sceGumTranslate(&bo);       \
            }                                                                          \
        }

    sceGuDisable(GU_BLEND);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);

    const float MOB_DEPTH_BIAS_BLOCKS = 0.10f;
    {
        float ddx = x - g_camX, ddy = feet - g_camY, ddz = z - g_camZ;
        sceGuDepthOffset(mobDepthBiasUnits(ddx * ddx + ddy * ddy + ddz * ddz,
                                           g_nearZPlane, MOB_DEPTH_BIAS_BLOCKS));
    }

    phaseRow(1, 10);
    sceGuColor(brCol);
    for (int i = 0; i < count; i++) {
        phaseRow(4, i);
        sceGumPushMatrix();
        MOB_BABY_XFORM(i);
        ScePspFVector3 piv = { parts[i].px, parts[i].py, parts[i].pz }; sceGumTranslate(&piv);
        if (parts[i].zRot != 0.0f) sceGumRotateZ(parts[i].zRot);
        if (parts[i].yRot != 0.0f) sceGumRotateY(parts[i].yRot);
        if (parts[i].xRot != 0.0f) sceGumRotateX(parts[i].xRot);
        sceGumDrawArray(GU_TRIANGLES,
                        GU_TEXTURE_32BITF | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
                        36, 0, parts[i].base);
        sceGumPopMatrix();
    }

    phaseRow(1, 11);
    if (bowPartIndex >= 0 && bowPartIndex < count) {
        MobPart& ap = parts[bowPartIndex];
        short drawId = heldItemId ? heldItemId : ITEM_BOW;

        static ItemModelRenderer heldModel;
        if (heldModel.buildShared(drawId, 0, brCol)) {
            sceGumPushMatrix();
            ScePspFVector3 piv = { ap.px, ap.py, ap.pz }; sceGumTranslate(&piv);
            if (ap.zRot != 0.0f) sceGumRotateZ(ap.zRot);
            if (ap.yRot != 0.0f) sceGumRotateY(ap.yRot);
            if (ap.xRot != 0.0f) sceGumRotateX(ap.xRot);
            ScePspFVector3 fist = { -1.0f, 7.0f, 1.0f }; sceGumTranslate(&fist);
            ScePspFVector3 s16  = { 16.0f, 16.0f, 16.0f }; sceGumScale(&s16);
            if (drawId == ITEM_BOW) {
                ScePspFVector3 bt = { 0.0f, 2.0f/16.0f, 5.0f/16.0f }; sceGumTranslate(&bt);
                sceGumRotateY(-20.0f * DEG2RAD);
                ScePspFVector3 bsc = { 10.0f/16.0f, -10.0f/16.0f, 10.0f/16.0f }; sceGumScale(&bsc);
                sceGumRotateX(-100.0f * DEG2RAD); sceGumRotateY(45.0f * DEG2RAD);
            } else {

                ScePspFVector3 ht = { 0.0f, 3.0f/16.0f, 0.0f }; sceGumTranslate(&ht);
                ScePspFVector3 hs = { 10.0f/16.0f, -10.0f/16.0f, 10.0f/16.0f }; sceGumScale(&hs);
                sceGumRotateX(-100.0f * DEG2RAD); sceGumRotateY(45.0f * DEG2RAD);
            }
            ItemModelRenderer::applyFlatPreTransform();
            heldModel.drawShared(true);
            sceGumPopMatrix();
            textureBind(tex);
        }
    }

    phaseRow(1, 12);
    if (overlayWhite > 0.01f) {
        unsigned int wa = (unsigned int)(overlayWhite * 255.0f); if (wa > 255) wa = 255;
        const unsigned int WHITE = (wa << 24) | 0x00FFFFFFu;

        sceGuEnable(GU_BLEND);
        sceGuTexFunc(GU_TFX_ADD, GU_TCC_RGBA);
        sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
        sceGuDepthMask(GU_TRUE);
        sceGuEnable(GU_CULL_FACE);
        sceGuFrontFace(GU_CCW);
        sceGuColor(WHITE);
        for (int i = 0; i < count; i++) {
            sceGumPushMatrix();
            MOB_BABY_XFORM(i);
            ScePspFVector3 piv = { parts[i].px, parts[i].py, parts[i].pz }; sceGumTranslate(&piv);
            if (parts[i].zRot != 0.0f) sceGumRotateZ(parts[i].zRot);
            if (parts[i].yRot != 0.0f) sceGumRotateY(parts[i].yRot);
            if (parts[i].xRot != 0.0f) sceGumRotateX(parts[i].xRot);
            sceGumDrawArray(GU_TRIANGLES,
                            GU_TEXTURE_32BITF | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
                            36, 0, parts[i].base);
            sceGumPopMatrix();
        }
        sceGuDisable(GU_CULL_FACE);
        sceGuDepthMask(GU_FALSE);
        sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    }
    #undef MOB_BABY_XFORM

    phaseRow(1, 13);
    sceGuDepthOffset(0);
    sceGuColor(0xFFFFFFFFu);

    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGumPopMatrix();
    sceGuEnable(GU_CULL_FACE);
}
