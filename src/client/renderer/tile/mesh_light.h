
#ifndef MCPSP_CLIENT_RENDERER_TILE_MESH_LIGHT_H
#define MCPSP_CLIENT_RENDERER_TILE_MESH_LIGHT_H

#include "world/level/chunk/chunk.h"
#include "world/level/world.h"

static inline void faceCornerColors(const World* w, const unsigned char* lc,
                                    unsigned char* llc, int nbi, int nx, int ny, int nz,
                                    int f, unsigned char id, unsigned int tint,
                                    unsigned int shade, unsigned int cc[2][2]) {
    float sm[2][2];
    smoothFaceLight(w, lc, llc, nbi, nx, ny, nz, f, sm);
    unsigned int shadeTint = mulColor(shade, tint);
    float emit = g_brightRamp[lightEmit(id)];
    for (int i = 0; i < 2; i++)
    for (int j = 0; j < 2; j++)
        cc[i][j] = mulColor(shadeTint, brightColorF(sm[i][j] < emit ? emit : sm[i][j]));
}

#endif
