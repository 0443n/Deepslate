
#ifndef MCPSP_CLIENT_ENTITY_MOB_MODEL_H
#define MCPSP_CLIENT_ENTITY_MOB_MODEL_H

class Mob;
struct Texture;

struct MobVertex { float u, v; float x, y, z; };

struct SkinVertex { float u, v; unsigned int color; float x, y, z; };

struct MobAnim {
    float bodyRot;
    float headYaw;
    float pitch;
    float speed;
    float pos;
};
MobAnim mobAnimSetup(Mob* mob, float rot, float a);

#define MOB_MAX_PARTS 16

struct MobPart { MobVertex base[36]; float px, py, pz; float xRot, yRot, zRot; bool head; };

void mobBuildBox(MobVertex* out, float x0, float y0, float z0,
                 float x1, float y1, float z1, int tx, int ty, int w, int h, int d,
                 bool mirror, float grow, float texW = 64.0f, float texH = 32.0f);

inline void mobBoxToColoured(SkinVertex* dst, const MobVertex* src, int n, unsigned int col) {
    for (int i = 0; i < n; i++) {
        dst[i].u = src[i].u; dst[i].v = src[i].v;
        dst[i].x = src[i].x; dst[i].y = src[i].y; dst[i].z = src[i].z;
        dst[i].color = col;
    }
}

void mobRenderParts(Mob* mob, MobPart* parts, int count, Texture* tex,
                    float x, float y, float z, float ibody, float a, unsigned int tint,
                    float babyHeadY = 8.0f, float babyHeadZ = 4.0f, float modelScale = 1.0f,
                    float overlayWhite = 0.0f, int bowPartIndex = -1, float modelScaleY = -1.0f,
                    short heldItemId = 0);

static inline int mobDepthBiasUnits(float d2, float nearZ, float biasBlocks) {
    if (d2 < 0.25f) d2 = 0.25f;
    float units = 65535.0f * nearZ * biasBlocks / d2;
    if (units > 4000.0f) units = 4000.0f;

    return (int)units;
}

#endif
