
#ifndef MCPSP_GPU_SPRITE_H
#define MCPSP_GPU_SPRITE_H

struct Texture;

void spriteDraw(const Texture* tex,
                float dx, float dy, float dw, float dh,
                float sx, float sy, float sw, float sh,
                unsigned int color);

void spriteDrawRot(const Texture* tex,
                   float ox, float oy, float cs, float sn,
                   float lx, float ly, float lw, float lh,
                   float sx, float sy, float sw, float sh,
                   unsigned int color);

// One draw call for many sprites off the same texture. Returns 0 when the frame
// scratch cannot hold the batch, in which case the caller falls back to spriteDraw.
void* spriteBatchAlloc(int quads);
void  spriteBatchAdd(void* buf, int i,
                     float dx, float dy, float dw, float dh,
                     float sx, float sy, float sw, float sh,
                     unsigned int color);
void  spriteBatchDraw(void* buf, int quads);

void spriteDrawFull(const Texture* tex, float dx, float dy, unsigned int color);

void spriteDrawTiled(const Texture* tex,
                     float dx, float dy, float dw, float dh,
                     float tileScreenPx, unsigned int color);

#endif
