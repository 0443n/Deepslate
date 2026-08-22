#include "gpu/gu.h"
#include <stdlib.h>
#include "platform/canary.h"
#include "platform/dcache.h"

#include <pspdisplay.h>
#include <pspge.h>
#include <pspgu.h>
#include <pspgum.h>
#include <pspkernel.h>

#include "util/prof.h"
#include "gpu/texture.h"
#include "gpu/vram_alloc.h"

#define GU_LIST_COUNT 2
#ifndef GU_LIST_KB
#define GU_LIST_KB 448
#endif
#define GU_LIST_BYTES ((unsigned)GU_LIST_KB * 1024u)
static unsigned int __attribute__((aligned(16)))
    g_list[GU_LIST_COUNT][GU_LIST_BYTES / 4 + CANARY_WORDS];
static void*    g_listUncached[GU_LIST_COUNT] = { 0, 0 };
static int      g_listIdx = 0;

static unsigned int __attribute__((aligned(16))) g_callList[64];

static void* g_callListUncached = 0;

#define GU_DEFER_MAX 512
static void* g_deferBuf[2][GU_DEFER_MAX];
static int   g_deferN[2] = { 0, 0 };
static int   g_deferCur  = 0;
unsigned int g_deferStalls = 0;

void guDeferFree(void* p) {
    if (!p) return;
    if (g_deferN[g_deferCur] >= GU_DEFER_MAX) {
        sceGuSync(0, 0);
        free(p);
        g_deferStalls++;
        return;
    }
    g_deferBuf[g_deferCur][g_deferN[g_deferCur]++] = p;
}

static void guFlushDeferredFrees(void) {
    const int old = g_deferCur ^ 1;
    for (int i = 0; i < g_deferN[old]; i++) free(g_deferBuf[old][i]);
    g_deferN[old] = 0;
    g_deferCur    = old;
}

static inline volatile unsigned int* guListCanary(int i) {
    return (volatile unsigned int*)&g_list[i][GU_LIST_BYTES / 4];
}
static inline void* guListCur(void) { return g_listUncached[g_listIdx]; }

#ifndef BSS_PAD_KB
#define BSS_PAD_KB 0
#endif
#if BSS_PAD_KB > 0
static volatile unsigned char g_bssPad[BSS_PAD_KB * 1024];
#endif

#define GU_SCRATCH_BUDGET (384 * 1024)

#define GU_SCRATCH_GENERAL (336 * 1024)
static unsigned int g_frameScratch = 0;
static unsigned int g_frameId = 0;

unsigned int g_frameAllocFails = 0;

unsigned int g_listPeakBytes = 0;
unsigned int g_listOverruns  = 0;

unsigned int g_canaryBroken = 0;
unsigned int guFrameId(void) { return g_frameId; }

int g_dither = 0;

static int g_ditherWant = 0;

void guSetDither(int wanted) {
    g_ditherWant = wanted;
    if (wanted && g_dither) sceGuEnable(GU_DITHER);
    else                    sceGuDisable(GU_DITHER);
}

int guDitherWanted(void) { return g_ditherWant; }

static unsigned int g_listUsed = 0;

#define GU_LIST_MARGIN (64 * 1024)

static inline void guTraceFail(int bytes, unsigned gate) {
    (void)bytes; (void)gate;
    g_frameAllocFails++;
}

static void* frameAllocUpTo(int bytes, unsigned int limit) {
    if (bytes <= 0) return 0;
    if (g_frameScratch + (unsigned int)bytes > limit) { guTraceFail(bytes, 1); return 0; }

    const unsigned int cost = (((unsigned int)bytes + 3u) & ~3u) + 8u;
    if (g_listUsed + cost + GU_LIST_MARGIN > GU_LIST_BYTES) { guTraceFail(bytes, 2); return 0; }

    void* p = sceGuGetMemory(bytes);
    if (!p) return 0;

    const unsigned int off = (unsigned int)p - (unsigned int)guListCur();

    if (off + cost > GU_LIST_BYTES) {

        g_listUsed = GU_LIST_BYTES;
        guTraceFail(bytes, 3);
        return 0;
    }
    g_listUsed = off + cost;

    g_frameScratch += (unsigned int)bytes;
    return p;
}

void guListSync(void) {

    if (g_listUsed + 8u > GU_LIST_BYTES) { g_listUsed = GU_LIST_BYTES; return; }
    void* p = sceGuGetMemory(0);
    if (!p) return;
    const unsigned int off = (unsigned int)p - (unsigned int)guListCur();
    g_listUsed = (off < GU_LIST_BYTES) ? off : GU_LIST_BYTES;
}

void* guFrameAlloc(int bytes)         { return frameAllocUpTo(bytes, GU_SCRATCH_GENERAL); }
void* guFrameAllocPriority(int bytes) { return frameAllocUpTo(bytes, GU_SCRATCH_BUDGET); }

static unsigned int g_vramOffset = 0;

#define GU_FB_COUNT 3
static void* g_fb[GU_FB_COUNT] = { 0 };
static void* g_zbp = 0;
static int   g_drawIdx = 0;

static bool s_dialogUp = false;

static inline void* guFbAddr(int idx) {
    return (void*)(((unsigned int)sceGeEdramGetAddr() + (unsigned int)g_fb[idx])
                   | 0x40000000u);
}

static int s_dlgDraw  = 0;
static int s_dlgShown = 1;
static int s_postedIdx     = -1;
static int s_prevPostedIdx = -1;
unsigned int g_drawLiveHits = 0;
unsigned int g_drawLiveOurs = 0;

void guWaitGeIdle(void) { sceGuSync(0, 0); }

static unsigned int guMemSize(unsigned int width, unsigned int height,
                              unsigned int psm) {
    unsigned int bytesPerPixel;
    switch (psm) {
        case GU_PSM_T4:   return (width * height) >> 1;
        case GU_PSM_T8:   bytesPerPixel = 1; break;
        case GU_PSM_5650:
        case GU_PSM_5551:
        case GU_PSM_4444:
        case GU_PSM_T16:  bytesPerPixel = 2; break;
        case GU_PSM_8888:
        case GU_PSM_T32:  bytesPerPixel = 4; break;
        default:          bytesPerPixel = 4; break;
    }
    return width * height * bytesPerPixel;
}

static void* guVramAlloc(unsigned int width, unsigned int height,
                         unsigned int psm) {
    void* result = (void*)(unsigned long)g_vramOffset;
    g_vramOffset += guMemSize(width, height, psm);
    return result;
}

static unsigned int guVramTotal(void) {
    const unsigned int have = sceGeEdramGetSize();
    const unsigned int cap  = 2u * 1024 * 1024;
    return have < cap ? have : cap;
}

void* guVramAllocTexture(unsigned int bytes) {
    unsigned int off = vramAlloc(bytes);
    if (off == VRAM_ALLOC_NONE) return 0;
    return (void*)((unsigned int)sceGeEdramGetAddr() + off);
}

void guVramFreeTexture(void* ptr) {
    if (!ptr) return;
    vramFreeAt((unsigned int)ptr - (unsigned int)sceGeEdramGetAddr());
}

unsigned int guVramFree(void) {
    return vramBytesFree();
}

static const ScePspIMatrix4 kDitherA = {
    { -2,  1, -1,  2 },
    { -1,  2, -2,  1 },
    {  2, -1,  1, -2 },
    {  1, -2,  2, -1 },
};
static const ScePspIMatrix4 kDitherB = {
    { -2, -1,  2,  1 },
    {  1,  2, -1, -2 },
    { -1, -2,  1,  2 },
    {  2,  1, -2, -1 },
};

static void guSetDitherPhase(int phase) {
    (void)phase;
    sceGuSetDither((ScePspIMatrix4*)&kDitherA);
}

static void guApplyPersistentState(void) {
    sceGuDepthBuffer(g_zbp, GU_BUF_WIDTH);

    sceGuOffset(2048 - (GU_SCR_WIDTH / 2), 2048 - (GU_SCR_HEIGHT / 2));
    sceGuViewport(2048, 2048, GU_SCR_WIDTH, GU_SCR_HEIGHT);

    sceGuDepthRange(0xc350, 0x2710);

    sceGuScissor(0, 0, GU_SCR_WIDTH, GU_SCR_HEIGHT);
    sceGuEnable(GU_SCISSOR_TEST);

    sceGuDepthFunc(GU_GEQUAL);
    sceGuEnable(GU_DEPTH_TEST);

    sceGuFrontFace(GU_CW);

    sceGuShadeModel(GU_SMOOTH);
    sceGuEnable(GU_CULL_FACE);
    sceGuEnable(GU_CLIP_PLANES);

    guSetDitherPhase(g_listIdx);
    guSetDither(1);

    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);

    sceGuTexFilter(GU_NEAREST, GU_NEAREST);
    sceGuAlphaFunc(GU_GREATER, 0, 0xff);
    sceGuEnable(GU_ALPHA_TEST);

    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
}

void guInit(void) {

    for (int i = 0; i < GU_LIST_COUNT; i++) {
        g_listUncached[i] = (void*)((unsigned int)g_list[i] | 0x40000000u);

        canaryArm(guListCanary(i));
    }

    g_callListUncached = (void*)((unsigned int)g_callList | 0x40000000u);
    g_listIdx = 0;

    for (int i = 0; i < GU_FB_COUNT; i++)
        g_fb[i] = guVramAlloc(GU_BUF_WIDTH, GU_SCR_HEIGHT, GU_PSM_5650);
    g_zbp = guVramAlloc(GU_BUF_WIDTH, GU_SCR_HEIGHT, GU_PSM_4444);
    g_drawIdx = 0;

    vramAllocInit(g_vramOffset, guVramTotal());

    sceGuInit();

    sceDisplaySetMode(0, GU_SCR_WIDTH, GU_SCR_HEIGHT);

    sceGuStart(GU_DIRECT, guListCur());

    sceGuDrawBuffer(GU_PSM_5650, g_fb[0], GU_BUF_WIDTH);
    sceGuDispBuffer(GU_SCR_WIDTH, GU_SCR_HEIGHT, g_fb[1], GU_BUF_WIDTH);
    guApplyPersistentState();

    sceGuFinish();
    sceGuSync(0, 0);

    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);

    sceDisplaySetFrameBuf(guFbAddr(1), GU_BUF_WIDTH,
                          PSP_DISPLAY_PIXEL_FORMAT_565, PSP_DISPLAY_SETBUF_NEXTFRAME);

    s_postedIdx     = 1;
    s_prevPostedIdx = -1;
    g_drawIdx       = 0;

}

void guTerm(void) {
    sceGuTerm();
}

static void guApplyFrameBaseline(void) {
    sceGuDepthFunc(GU_GEQUAL);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuDepthMask(GU_FALSE);
    sceGuEnable(GU_CULL_FACE);
    sceGuFrontFace(GU_CW);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuEnable(GU_ALPHA_TEST);
    sceGuAlphaFunc(GU_GREATER, 0, 0xff);
    sceGuDisable(GU_FOG);
    sceGuEnable(GU_TEXTURE_2D);
}

static int guFreeBuffer(void) {
    for (int i = 0; i < GU_FB_COUNT; i++)
        if (i != s_postedIdx && i != s_prevPostedIdx) return i;
    return g_drawIdx;
}

static void guSelectDrawBuffer(void) {
    if (g_drawIdx == s_postedIdx || g_drawIdx == s_prevPostedIdx) {
        g_drawLiveHits++;
        g_drawLiveOurs++;
        profAdd(PROFC_DRAWLIVE, 1);
        g_drawIdx = guFreeBuffer();
    }
    sceGuDrawBuffer(GU_PSM_5650, g_fb[g_drawIdx], GU_BUF_WIDTH);
}

bool guStartFrame(unsigned int clearColor) {

    if (s_dialogUp) return false;

    g_listIdx ^= 1;
    sceGuStart(GU_DIRECT, guListCur());
    g_frameScratch = 0;
    g_listUsed     = 0;
    g_frameId++;

    guSelectDrawBuffer();

    guSetDitherPhase(g_listIdx);

    guApplyFrameBaseline();

    guSetDither(0);

    sceGuScissor(0, 0, GU_SCR_WIDTH, GU_SCR_HEIGHT);

    sceGuClearColor(clearColor);
    sceGuClearDepth(0);

    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
    return true;
}

static void guCheckListCanary(void) {
    const int c = canaryCheck(guListCanary(g_listIdx));
    if (c) g_canaryBroken = (unsigned)c;
}

void guFinishFrame(void) {

    profBegin(PROF_GESYNC);

    unsigned listBytes = (unsigned)sceGuFinish();
    profListBytes(listBytes);

    if (listBytes > g_listPeakBytes) g_listPeakBytes = listBytes;
    if (listBytes >= GU_LIST_BYTES) g_listOverruns++;

    {

        extern bool g_worldBuilt;
        static unsigned s_listPeak = 0;
        static unsigned s_frameNo = 0;
        static bool     s_lastScene = false;

        static unsigned s_hist[2] = { 0, 0 };
        static unsigned s_histShown[2] = { 0, 0 };
        static unsigned s_histDrawn[2] = { 0, 0 };

        if (g_worldBuilt != s_lastScene) {
            s_lastScene = g_worldBuilt;
            s_listPeak = 0;
            s_hist[0] = s_hist[1] = 0;
        } else {
            s_frameNo++;
            if (listBytes > s_listPeak) s_listPeak = listBytes;

            void* shown = 0; int bw = 0, pf = 0;
            sceDisplayGetFrameBuf(&shown, &bw, &pf, 0);
            s_hist[0] = s_hist[1];
            s_hist[1] = listBytes;
            s_histShown[0] = s_histShown[1];
            s_histShown[1] = (unsigned)shown;
            s_histDrawn[0] = s_histDrawn[1];
            s_histDrawn[1] = (unsigned)sceGeEdramGetAddr() + (unsigned)g_fb[g_drawIdx];
        }
    }

    sceGuSync(0, 0);
    guCheckListCanary();

    guFlushDeferredFrees();
    profEnd(PROF_GESYNC);
}

void guPresent(void) {

    const int shown = g_drawIdx;
    sceDisplaySetFrameBuf(guFbAddr(shown), GU_BUF_WIDTH,
                          PSP_DISPLAY_PIXEL_FORMAT_565, PSP_DISPLAY_SETBUF_NEXTFRAME);
    s_prevPostedIdx = s_postedIdx;
    s_postedIdx     = shown;

    g_drawIdx = guFreeBuffer();

    profBegin(PROF_VBLANK);
    sceDisplayWaitVblankStart();
    profEnd(PROF_VBLANK);
}

void guSuspendForDialog(void) {

    sceGuSync(0, 0);
    guFlushDeferredFrees();

    s_dlgShown = (s_postedIdx >= 0) ? s_postedIdx : 1;
    s_dlgDraw  = guFreeBuffer();
    if (s_dlgDraw == s_dlgShown) s_dlgDraw = (s_dlgShown + 1) % GU_FB_COUNT;

    sceGuStart(GU_DIRECT, g_callListUncached);
    sceGuDrawBuffer(GU_PSM_5650, g_fb[s_dlgDraw], GU_BUF_WIDTH);
    sceGuDispBuffer(GU_SCR_WIDTH, GU_SCR_HEIGHT, g_fb[s_dlgShown], GU_BUF_WIDTH);
    sceGuFinish();
    sceGuSync(0, 0);

    g_drawIdx  = s_dlgDraw;
    s_dialogUp = true;
}

void guResumeFromDialog(void) {
    sceGuSync(0, 0);

    sceDisplaySetFrameBuf(guFbAddr(s_dlgShown), GU_BUF_WIDTH,
                          PSP_DISPLAY_PIXEL_FORMAT_565, PSP_DISPLAY_SETBUF_NEXTFRAME);
    s_postedIdx     = s_dlgShown;
    s_prevPostedIdx = s_dlgDraw;
    g_drawIdx       = guFreeBuffer();

    sceGuStart(GU_DIRECT, g_callListUncached);
    guApplyPersistentState();
    sceGuFinish();
    sceGuSync(0, 0);

    s_dialogUp = false;
}

void guDialogBegin(unsigned int clearColor) {
    g_listIdx ^= 1;
    sceGuStart(GU_DIRECT, guListCur());
    g_frameScratch = 0;
    g_listUsed     = 0;
    g_frameId++;

    guApplyPersistentState();

    guApplyFrameBaseline();
    guSetDither(0);
    sceGuScissor(0, 0, GU_SCR_WIDTH, GU_SCR_HEIGHT);
    sceGuClearColor(clearColor);
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
}

void guDialogEnd(void) {

    unsigned listBytes = (unsigned)sceGuFinish();
    profListBytes(listBytes);
    if (listBytes > g_listPeakBytes) g_listPeakBytes = listBytes;
    if (listBytes >= GU_LIST_BYTES) g_listOverruns++;
    sceGuSync(0, 0);
    guCheckListCanary();

    guFlushDeferredFrees();
}

void guDialogPresent(void) {

    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
    int t = s_dlgDraw; s_dlgDraw = s_dlgShown; s_dlgShown = t;
    g_drawIdx = s_dlgDraw;
}

void guEndFrame(void) {
    profEnd(PROF_HUD);
    guFinishFrame();
    guPresent();
    profFrameEnd();
}

#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
bool guSavePhotoPng(const char* path, int shrink) {
    if (shrink < 1) shrink = 1;
    const int outW = GU_SCR_WIDTH / shrink, outH = GU_SCR_HEIGHT / shrink;
    const int shotBytes = GU_BUF_WIDTH * GU_SCR_HEIGHT * 2;

    unsigned short* shot = (unsigned short*)memalign(64, shotBytes);
    if (!shot) return false;

    dcacheFlush(shot, shotBytes);

    guWaitGeIdle();
    sceGuStart(GU_DIRECT, guListCur());
    sceGuCopyImage(GU_PSM_5650, 0, 0, GU_SCR_WIDTH, GU_SCR_HEIGHT, GU_BUF_WIDTH,
                   (void*)((unsigned int)sceGeEdramGetAddr() + (unsigned int)g_fb[g_drawIdx]),
                   0, 0, GU_BUF_WIDTH, shot);
    sceGuFinish();
    sceGuSync(0, 0);

    const unsigned short* shotRd = (const unsigned short*)((unsigned int)shot | 0x40000000u);

    FILE* f = fopen(path, "wb");
    if (!f) { free(shot); return false; }
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, 0, 0, 0);
    png_infop info = png ? png_create_info_struct(png) : 0;
    if (!png || !info || setjmp(png_jmpbuf(png))) {
        if (png) png_destroy_write_struct(&png, info ? &info : 0);
        fclose(f);
        free(shot);
        return false;
    }
    png_init_io(png, f);
    png_set_IHDR(png, info, outW, outH, 8, PNG_COLOR_TYPE_RGB,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);

    unsigned char row[GU_SCR_WIDTH * 3];
    const unsigned int n = (unsigned int)(shrink * shrink);
    for (int y = 0; y < outH; y++) {
        for (int x = 0; x < outW; x++) {
            unsigned int r = 0, g = 0, b = 0;
            for (int sy = 0; sy < shrink; sy++) {
                const unsigned short* src = shotRd + (y * shrink + sy) * GU_BUF_WIDTH;
                for (int sx = 0; sx < shrink; sx++) {
                    unsigned short p = src[x * shrink + sx];
                    r += (unsigned int)(( p        & 0x1F) << 3);
                    g += (unsigned int)(((p >> 5)  & 0x3F) << 2);
                    b += (unsigned int)(((p >> 11) & 0x1F) << 3);
                }
            }
            row[x * 3 + 0] = (unsigned char)(r / n);
            row[x * 3 + 1] = (unsigned char)(g / n);
            row[x * 3 + 2] = (unsigned char)(b / n);
        }
        png_write_row(png, row);
    }
    png_write_end(png, info);
    png_destroy_write_struct(&png, &info);
    fclose(f);
    free(shot);
    return true;
}

void guOrtho(void) {

    guSetDither(1);
    sceGumMatrixMode(GU_PROJECTION);
    sceGumLoadIdentity();
    sceGumOrtho(0, GU_SCR_WIDTH, GU_SCR_HEIGHT, 0, -1.0f, 1.0f);
    sceGumMatrixMode(GU_VIEW);
    sceGumLoadIdentity();
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
}

void guPerspective(float fovDeg, float nearZ, float farZ) {
    const float aspect = (float)GU_SCR_WIDTH / (float)GU_SCR_HEIGHT;

    guSetDither(1);
    sceGumMatrixMode(GU_PROJECTION);
    sceGumLoadIdentity();
    sceGumPerspective(fovDeg, aspect, nearZ, farZ);

    sceGumMatrixMode(GU_VIEW);
    sceGumLoadIdentity();

    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
}

#include "platform/path.h"
