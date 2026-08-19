
#ifndef MCPSP_GPU_GU_H
#define MCPSP_GPU_GU_H

#include <pspgu.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

void* guFrameAlloc(int bytes);

void* guFrameAllocPriority(int bytes);

extern int g_dither;

void guSetDither(int wanted);

int guDitherWanted(void);

#ifndef MCPSP_DIAG
#define MCPSP_DIAG 0
#endif

extern unsigned int g_frameMarks;

extern unsigned int g_canaryBroken;

extern unsigned int g_pixDriftHits;

enum {
    GU_MARK_SKY_BACKDROP = 0,
    GU_MARK_SKY_DOME,
    GU_MARK_SKY_BODIES,
    GU_MARK_CLOUDS,
    GU_MARK_TERRAIN,
    GU_MARK_WATER,
    GU_MARK_ENTITIES,
    GU_MARK_PARTICLES,
    GU_MARK_HAND,
    GU_MARK_HUD,
    GU_MARK_OVERLAY,
    GU_MARK_MENU,
    GU_MARK_HINTS,

    GU_MARK_MENU_BG,
    GU_MARK_MENU_CONTENT,
    GU_MARK_UI_SPRITE,
    GU_MARK_UI_TEXT,
    GU_MARK_COUNT
};

#if MCPSP_DIAG
static inline void guMark(int bit) { g_frameMarks |= (1u << bit); }
#else
static inline void guMark(int) {}
#endif

unsigned int guFrameId(void);

static inline void* guFrameCopy(const void* src, int bytes) {
    void* p = guFrameAlloc(bytes);
    if (p) memcpy(p, src, bytes);
    return p;
}

#define GU_BUF_WIDTH  512
#define GU_SCR_WIDTH  480
#define GU_SCR_HEIGHT 272

static inline int guShortListTrigger(unsigned before, unsigned mid,
                                     unsigned after, unsigned peak) {
    if (peak <= 4096) return 0;
    return mid < peak / 4 && before > peak / 2 && after > peak / 2;
}

#define DIAG_OVERLAY 0

static inline int guRowIsUniform(const unsigned short* row, int x0, int step, int count) {
    const unsigned short first = row[x0];
    for (int i = 1; i < count; i++)
        if (row[x0 + i * step] != first) return 0;
    return 1;
}

void guDumpFrameLog(void);

void guInit(void);

void* guVramAllocTexture(unsigned int bytes);

void guVramFreeTexture(void* ptr);

unsigned int guVramFree(void);

void guTerm(void);

bool guStartFrame(unsigned int clearColor);

void guEndFrame(void);

void guFinishFrame(void);
void guPresent(void);

void guSuspendForDialog(void);
void guResumeFromDialog(void);
void guDialogBegin(unsigned int clearColor);
void guDialogEnd(void);
void guDialogPresent(void);

void guWaitGeIdle(void);

bool guSavePhotoPng(const char* path, int shrink);

void guPerspective(float fovDeg, float nearZ, float farZ);

void guOrtho(void);

#ifdef __cplusplus
}
#endif

#endif
