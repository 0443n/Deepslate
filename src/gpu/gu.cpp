#include "gpu/gu.h"
#include "gpu/gu_list_size.h"
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

#define GU_CALL_LIST_WORDS 256

static unsigned int __attribute__((aligned(16)))
    g_callList[GU_CALL_LIST_WORDS + CANARY_WORDS];
unsigned int g_callCanaryBroken = 0;

static void* g_callListUncached = 0;

#define GU_DEFER_MAX 512
static void* g_deferBuf[2][GU_DEFER_MAX];
static int   g_deferN[2] = { 0, 0 };
static int   g_deferCur  = 0;
unsigned int g_deferStalls = 0;

void guDeferFree(void* p) {
    if (!p) return;
    if (g_deferN[g_deferCur] < GU_DEFER_MAX) {
        g_deferBuf[g_deferCur][g_deferN[g_deferCur]++] = p;
        return;
    }

    const int other = g_deferCur ^ 1;
    if (g_deferN[other] < GU_DEFER_MAX) {
        g_deferBuf[other][g_deferN[other]++] = p;
        g_deferStalls++;
        return;
    }

    sceGuSync(0, 0);
    free(p);
    g_deferStalls++;
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

// guFinishFrame is the only other caller, so an overrun inside a pass went
// unseen until the frame was already over.
int guListCanaryCheck(void) { return canaryCheck(guListCanary(g_listIdx)); }

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
unsigned int g_frameAllocListFails = 0;

unsigned int g_listPeakBytes = 0;
volatile int g_inFrame = 0;
unsigned int g_listOverruns  = 0;
unsigned int g_listBadFinish = 0;

static unsigned guFinishBytes(void) {
    const int ret = sceGuFinish();
    if (!guListSizeIsSane(ret, GU_LIST_BYTES)) {
        g_listBadFinish++;
        return 0;
    }
    return (unsigned)ret;
}

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
    (void)bytes;
    g_frameAllocFails++;

    if (gate != 1) g_frameAllocListFails++;
}

static void* frameAllocUpTo(int bytes, unsigned int limit) {
    if (bytes <= 0) return 0;
    if (g_frameScratch + (unsigned int)bytes > limit) { guTraceFail(bytes, 1); return 0; }

    const unsigned int cost = (((unsigned int)bytes + 3u) & ~3u) + 8u;

    const unsigned int margin = (limit == GU_SCRATCH_BUDGET) ? (GU_LIST_MARGIN / 4)
                                                             : GU_LIST_MARGIN;
    if (g_listUsed + cost + margin > GU_LIST_BYTES) { guTraceFail(bytes, 2); return 0; }

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

#define GU_FB_COUNT 2
static void* g_fb[GU_FB_COUNT] = { 0 };
static void* g_zbp = 0;
static int   g_drawIdx = 0;

static bool s_dialogUp = false;

static inline void* guFbAddr(int idx) {
    return (void*)(((unsigned int)sceGeEdramGetAddr() + (unsigned int)g_fb[idx])
                   | 0x40000000u);
}

static int s_postedIdx     = -1;

unsigned int g_vcSameRefresh = 0;
unsigned int g_vcDrops       = 0;
int g_vcLast = 0, g_vcMin = 9999, g_vcMax = 0;
static int s_vcPrev = -1;
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

static int s_guBootStatus = -1;

static void guApplyPersistentState(void) {
    if (s_guBootStatus >= 0) sceGuSetAllStatus(s_guBootStatus);

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

    sceGuTexScale(1.0f, 1.0f);
    sceGuTexOffset(0.0f, 0.0f);
}

void guInit(void) {

    for (int i = 0; i < GU_LIST_COUNT; i++) {
        g_listUncached[i] = (void*)((unsigned int)g_list[i] | 0x40000000u);

        canaryArm(guListCanary(i));
    canaryArm((volatile unsigned int*)&g_callList[GU_CALL_LIST_WORDS]);
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

    s_guBootStatus = sceGuGetAllStatus();

    sceGuFinish();
    sceGuSync(0, 0);

    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);

    sceDisplaySetFrameBuf(guFbAddr(1), GU_BUF_WIDTH,
                          PSP_DISPLAY_PIXEL_FORMAT_565, PSP_DISPLAY_SETBUF_NEXTFRAME);

    s_postedIdx     = 1;
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

static int guFreeBuffer(void) { return s_postedIdx == 0 ? 1 : 0; }

static void guSelectDrawBuffer(void) {
    if (g_drawIdx == s_postedIdx) {
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
    g_inFrame = 1;
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
    g_inFrame = 0;

    profBegin(PROF_GESYNC);

    unsigned listBytes = guFinishBytes();
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

    {
        const int vc = (int)sceDisplayGetVcount();
        if (s_vcPrev >= 0) {
            const int d = vc - s_vcPrev;
            g_vcLast = d;
            if (d < g_vcMin) g_vcMin = d;
            if (d > g_vcMax) g_vcMax = d;
            if (d == 0)      g_vcSameRefresh++;
            else if (d >= 2) g_vcDrops++;
        }
        s_vcPrev = vc;
    }

    const int shown = g_drawIdx;
    sceDisplaySetFrameBuf(guFbAddr(shown), GU_BUF_WIDTH,
                          PSP_DISPLAY_PIXEL_FORMAT_565, PSP_DISPLAY_SETBUF_NEXTFRAME);
    s_postedIdx     = shown;

    (void)shown;
    g_drawIdx = guFreeBuffer();

    profBegin(PROF_VBLANK);
    sceDisplayWaitVblankStart();
    profEnd(PROF_VBLANK);
}

volatile bool g_guDialogActive = false;

void guSuspendForDialog(void) {
    g_guDialogActive = true;

    sceGuSync(0, 0);
    guFlushDeferredFrees();
    s_dialogUp = true;
}

void guResumeFromDialog(void) {
    g_guDialogActive = false;
    sceGuSync(0, 0);

    sceGuStart(GU_DIRECT, g_callListUncached);
    guApplyPersistentState();

    const int callRet = sceGuFinish();
    const unsigned used = guListSizeIsSane(callRet, GU_CALL_LIST_WORDS * 4u)
                        ? (unsigned)callRet : (g_listBadFinish++, 0u);
    sceGuSync(0, 0);
    if (used >= GU_CALL_LIST_WORDS * 4) g_listOverruns++;

    {
        const int c = canaryCheck((const volatile unsigned int*)&g_callList[GU_CALL_LIST_WORDS]);
        if (c) g_callCanaryBroken = (unsigned)c;
    }

    s_dialogUp = false;

}

void guDialogBegin(unsigned int clearColor) {
    g_listIdx ^= 1;
    sceGuStart(GU_DIRECT, guListCur());
    g_frameScratch = 0;
    g_listUsed     = 0;
    g_frameId++;

    guSelectDrawBuffer();

    sceGuClearColor(clearColor);
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
}

void guDialogEnd(void) {

    unsigned listBytes = guFinishBytes();
    profListBytes(listBytes);
    if (listBytes > g_listPeakBytes) g_listPeakBytes = listBytes;
    if (listBytes >= GU_LIST_BYTES) g_listOverruns++;
    sceGuSync(0, 0);
    guCheckListCanary();

    guFlushDeferredFrees();
}

void guDialogPresent(void) {

    guPresent();
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
