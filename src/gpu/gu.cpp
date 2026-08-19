#include "gpu/gu.h"
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

unsigned int g_shortListHits = 0;
unsigned int g_shortListBytes = 0;
unsigned int g_shortListPeak = 0;

struct ShortListEvent {
    unsigned frameNo, bytes, peak, prevBytes, nextBytes;
    unsigned shown, drawn;
    unsigned scratch, allocFails, drawLive;
};
static ShortListEvent s_events[32];
static unsigned s_eventCount = 0;

unsigned int g_emptyFrames = 0;
unsigned int g_emptyFrameNo = 0;

static unsigned int g_alarmFrames = 0;

struct EmptyFrameEvent { unsigned frameNo, shown, drawn, listBytes; };
static EmptyFrameEvent s_empties[16];
static unsigned s_emptyCount = 0;

unsigned int g_frameMarks = 0;

unsigned int g_canaryBroken = 0;

struct FrameRec {
    unsigned frameId;
    unsigned us;
    unsigned marks;
    unsigned listBytes;
    unsigned listUsed;
    unsigned scratch;
    unsigned allocFails;
    unsigned failBytes;
    unsigned failGate;
    unsigned failAt;
    unsigned shown, drawn;
    unsigned vramFree;
    unsigned short texFails;
    unsigned short canary;
    unsigned short bufIdx;
    unsigned pixSum;
    unsigned pixDrift;
    unsigned short driftCount;
    unsigned short driftIdx;
    unsigned short driftOld, driftNew;
};

#ifndef GU_TRACE_FRAMES
#if MCPSP_DIAG
#define GU_TRACE_FRAMES 512
#else
#define GU_TRACE_FRAMES 0
#endif
#endif
#if GU_TRACE_FRAMES > 0
static FrameRec s_trace[GU_TRACE_FRAMES];
#endif
static unsigned s_traceCount = 0;

typedef char FrameRec_layout_check[(sizeof(FrameRec) == 76) ? 1 : -1];

static unsigned s_failBytes = 0;
static unsigned s_failGate  = 0;
static unsigned s_failAt    = 0;
static unsigned s_failCount = 0;

static inline void guTraceResetFrame(void) {
    g_frameMarks = 0;
    s_failBytes = 0;
    s_failGate  = 0;
    s_failAt    = 0;
    s_failCount = 0;
}

int g_dither = 0;

static int g_ditherWant = 0;

void guSetDither(int wanted) {
    g_ditherWant = wanted;
    if (wanted && g_dither) sceGuEnable(GU_DITHER);
    else                    sceGuDisable(GU_DITHER);
}

int guDitherWanted(void) { return g_ditherWant; }

unsigned int guFrameId(void) { return g_frameId; }

static unsigned int g_listUsed = 0;

#define GU_LIST_MARGIN (64 * 1024)

static inline void guTraceFail(int bytes, unsigned gate) {
    g_frameAllocFails++;
    s_failCount++;
    if (s_failGate) return;
    s_failGate  = gate;
    s_failBytes = (unsigned)bytes;
    s_failAt    = ((g_frameScratch >> 4) << 16) | ((g_listUsed >> 4) & 0xffffu);
}

static void* frameAllocUpTo(int bytes, unsigned int limit) {
    if (bytes <= 0) return 0;
    if (g_frameScratch + (unsigned int)bytes > limit) { guTraceFail(bytes, 1); return 0; }

    const unsigned int cost = (((unsigned int)bytes + 3u) & ~3u) + 8u;
    if (g_listUsed + cost + GU_LIST_MARGIN > GU_LIST_BYTES) { guTraceFail(bytes, 2); return 0; }

    void* p = sceGuGetMemory(bytes);
    if (!p) return 0;

    const unsigned int off = (unsigned int)p - (unsigned int)guListCur();
    if (off < GU_LIST_BYTES) g_listUsed = off + cost;
    else                     g_listUsed = GU_LIST_BYTES;

    g_frameScratch += (unsigned int)bytes;
    return p;
}

void* guFrameAlloc(int bytes)         { return frameAllocUpTo(bytes, GU_SCRATCH_GENERAL); }
void* guFrameAllocPriority(int bytes) { return frameAllocUpTo(bytes, GU_SCRATCH_BUDGET); }

static unsigned int g_vramOffset = 0;

#define GU_FB_COUNT 3
static void* g_fb[GU_FB_COUNT] = { 0 };
static int   g_drawIdx = 0;

static volatile int g_queuedIdx  = -1;
static volatile int g_frontIdx   = 1;

static inline void* guFbAddr(int idx) {
    return (void*)((unsigned int)sceGeEdramGetAddr() + (unsigned int)g_fb[idx]);
}

unsigned int g_drawLiveHits = 0;
unsigned int g_drawLiveOurs = 0;

static int guAcquireDrawBuffer(int from) {
    for (int i = 0; i < GU_FB_COUNT; i++) {
        int cand = (from + i) % GU_FB_COUNT;
        if (cand != g_frontIdx && cand != g_queuedIdx) return cand;
    }
    return from;
}

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

#define VRAM_TOTAL (2u * 1024 * 1024)

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

void guInit(void) {

    for (int i = 0; i < GU_LIST_COUNT; i++) {
        g_listUncached[i] = (void*)((unsigned int)g_list[i] | 0x40000000u);
#if MCPSP_DIAG
        canaryArm(guListCanary(i));
#endif
    }
    g_listIdx = 0;

    for (int i = 0; i < GU_FB_COUNT; i++)
        g_fb[i] = guVramAlloc(GU_BUF_WIDTH, GU_SCR_HEIGHT, GU_PSM_5650);
    void* zbp = guVramAlloc(GU_BUF_WIDTH, GU_SCR_HEIGHT, GU_PSM_4444);
    g_drawIdx = 0;

    vramAllocInit(g_vramOffset, VRAM_TOTAL);

    sceGuInit();
    sceGuStart(GU_DIRECT, guListCur());

    sceGuDrawBuffer(GU_PSM_5650, g_fb[0], GU_BUF_WIDTH);
    sceGuDispBuffer(GU_SCR_WIDTH, GU_SCR_HEIGHT, g_fb[1], GU_BUF_WIDTH);
    sceGuDepthBuffer(zbp, GU_BUF_WIDTH);

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

    ScePspIMatrix4 dither = {
        { -4,  0, -3,  1 },
        {  2, -2,  3, -1 },
        { -3,  1, -4,  0 },
        {  3, -1,  2, -2 },
    };
    sceGuSetDither(&dither);
    guSetDither(1);

    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);

    sceGuTexFilter(GU_NEAREST, GU_NEAREST);
    sceGuAlphaFunc(GU_GREATER, 0, 0xff);
    sceGuEnable(GU_ALPHA_TEST);

    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);

    sceGuFinish();
    sceGuSync(0, 0);

    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);

    g_frontIdx   = 1;
    g_queuedIdx  = -1;
    g_drawIdx    = 0;

}

void guTerm(void) {
    sceGuTerm();
}

bool guStartFrame(unsigned int clearColor) {

    g_listIdx ^= 1;
    sceGuStart(GU_DIRECT, guListCur());
    g_frameScratch = 0;
    g_listUsed     = 0;
    g_frameId++;
    guTraceResetFrame();

#if DIAG_OVERLAY
    if (g_alarmFrames) { g_alarmFrames--; clearColor = 0xFF0000FFu; }
#endif

    if (g_drawIdx == g_frontIdx || g_drawIdx == g_queuedIdx) {
        g_drawLiveHits++;
        g_drawLiveOurs++;
        profAdd(PROFC_DRAWLIVE, 1);
        g_drawIdx = guAcquireDrawBuffer(g_drawIdx);
    }
    sceGuDrawBuffer(GU_PSM_5650, g_fb[g_drawIdx], GU_BUF_WIDTH);

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

    guSetDither(0);

    sceGuScissor(0, 0, GU_SCR_WIDTH, GU_SCR_HEIGHT);

    sceGuClearColor(clearColor);
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
    return true;
}

#ifndef GU_PIXSAMPLE
#define GU_PIXSAMPLE 0
#endif

#define GU_PIX_COLS   60
#define GU_PIX_ROWS   68
#define GU_PIX_SAMPLES (GU_PIX_COLS * GU_PIX_ROWS)
static unsigned short s_bufPix[GU_FB_COUNT][GU_PIX_SAMPLES];

static inline unsigned guPixOffset(int i) {
    const unsigned row = (unsigned)(i / GU_PIX_COLS) * 4u;
    const unsigned col = (unsigned)(i % GU_PIX_COLS) * 8u;
    return row * GU_BUF_WIDTH + col;
}

static unsigned guSamplePixels(int idx) {
#if !GU_PIXSAMPLE
    (void)idx; return 0;
#else
    const unsigned short* fb =
        (const unsigned short*)((unsigned)guFbAddr(idx) | 0x40000000u);
    unsigned short* keep = s_bufPix[idx];
    unsigned sum = 0;
    for (int i = 0; i < GU_PIX_SAMPLES; i++) {
        const unsigned short p = fb[guPixOffset(i)];
        keep[i] = p;
        sum = sum * 33u + p;
    }
    return sum ? sum : 1u;
#endif
}

static unsigned guRecheckPixels(int idx, unsigned* firstIdx,
                                unsigned* oldVal, unsigned* newVal) {
#if !GU_PIXSAMPLE
    (void)idx; (void)firstIdx; (void)oldVal; (void)newVal; return 0;
#else
    const unsigned short* fb =
        (const unsigned short*)((unsigned)guFbAddr(idx) | 0x40000000u);
    const unsigned short* keep = s_bufPix[idx];
    unsigned changed = 0;
    for (int i = 0; i < GU_PIX_SAMPLES; i++) {
        const unsigned short p = fb[guPixOffset(i)];
        if (p != keep[i]) {
            if (!changed) { *firstIdx = (unsigned)i; *oldVal = keep[i]; *newVal = p; }
            changed++;
        }
    }
    return changed;
#endif
}

static unsigned s_bufSum[GU_FB_COUNT];
static int      s_bufLast = -1;
unsigned int g_pixDriftHits = 0;

static bool s_dialogUp = false;

#define GU_DRIFT_SIGNIFICANT (GU_PIX_SAMPLES / 64)

static void guTraceRecord(unsigned listBytes) {
    static unsigned s_lastUs = 0;
    const unsigned nowUs = sceKernelGetSystemTimeLow();
    void* traceShown = 0; int tw = 0, tpf = 0;
    sceDisplayGetFrameBuf(&traceShown, &tw, &tpf, 0);

#if GU_TRACE_FRAMES > 0
    FrameRec* r = &s_trace[s_traceCount % GU_TRACE_FRAMES];
    r->frameId    = g_frameId;
    r->us     = s_lastUs ? (nowUs - s_lastUs) : 0;
    r->marks      = g_frameMarks;
    r->listBytes  = listBytes;
    r->listUsed   = g_listUsed;
    r->scratch    = g_frameScratch;
    r->allocFails = s_failCount;
    r->failBytes  = s_failBytes;
    r->failGate   = s_failGate;
    r->failAt     = s_failAt;
    r->shown      = (unsigned)traceShown;
    r->drawn      = (unsigned)sceGeEdramGetAddr() + (unsigned)g_fb[g_drawIdx];
    r->vramFree   = guVramFree();
    r->canary     = (unsigned short)canaryCheck(guListCanary(g_listIdx));
    if (r->canary) g_canaryBroken = r->canary;
    r->texFails   = (unsigned short)g_textureBindFailures;
    r->bufIdx     = (unsigned short)((g_drawIdx << 8) | (g_frontIdx & 0xff));
    r->pixDrift = 0; r->driftCount = 0; r->driftIdx = 0;
    r->driftOld = 0; r->driftNew = 0;

    if (!s_dialogUp && s_bufLast >= 0 && s_bufSum[s_bufLast]) {
        unsigned fi = 0, ov = 0, nv = 0;
        const unsigned changed = guRecheckPixels(s_bufLast, &fi, &ov, &nv);
        if (changed) {
            r->pixDrift   = 1;
            r->driftCount = (unsigned short)changed;
            r->driftIdx   = (unsigned short)fi;
            r->driftOld   = (unsigned short)ov;
            r->driftNew   = (unsigned short)nv;
            if (changed >= GU_DRIFT_SIGNIFICANT) g_pixDriftHits++;
        }
    }
    r->pixSum = guSamplePixels(g_drawIdx);
    if (r->pixSum) { s_bufSum[g_drawIdx] = r->pixSum; s_bufLast = g_drawIdx; }
#else
    (void)traceShown;
    { const int c = canaryCheck(guListCanary(g_listIdx)); if (c) g_canaryBroken = (unsigned)c; }
#endif
    s_traceCount++;
    s_lastUs = nowUs;
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

            if (guShortListTrigger(s_hist[0], s_hist[1], listBytes, s_listPeak)) {
                g_shortListHits++;
                g_shortListBytes = s_hist[1];
                g_shortListPeak  = s_listPeak;

                if (s_eventCount < 32) {
                    ShortListEvent* e = &s_events[s_eventCount];
                    e->frameNo    = s_frameNo - 1;
                    e->bytes      = s_hist[1];
                    e->peak       = s_listPeak;
                    e->prevBytes  = s_hist[0];
                    e->nextBytes  = listBytes;
                    e->shown      = s_histShown[1];
                    e->drawn      = s_histDrawn[1];
                    e->scratch    = g_frameScratch;
                    e->allocFails = g_frameAllocFails;
                    e->drawLive   = g_drawLiveHits;
                }
                s_eventCount++;
            }

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

    guTraceRecord(listBytes);

#if DIAG_OVERLAY
    {

        extern bool g_photoPending;
        bool gameProgressScreenUp();

        static bool s_hadContent[2] = { false, false };

        static unsigned s_prevShown = 0, s_prevDrawn = 0, s_prevList = 0;

        if (g_photoPending || gameProgressScreenUp()) {

        } else {

            const unsigned short* fb = (const unsigned short*)
                (((unsigned)sceGeEdramGetAddr() + (unsigned)g_fb[g_drawIdx]) | 0x40000000u);

            const bool hasContent =
                !guRowIsUniform(fb + 255 * GU_BUF_WIDTH, 20, 28, 16);

            if (s_hadContent[0] && !s_hadContent[1] && hasContent) {
                g_emptyFrames++;
                g_emptyFrameNo = g_frameId - 1;
                g_alarmFrames = 30;
                if (s_emptyCount < 16) {
                    EmptyFrameEvent* e = &s_empties[s_emptyCount];
                    e->frameNo   = g_frameId - 1;
                    e->shown     = s_prevShown;
                    e->drawn     = s_prevDrawn;
                    e->listBytes = s_prevList;
                }
                s_emptyCount++;
            }
            {
                void* shown = 0; int bw = 0, pf = 0;
                sceDisplayGetFrameBuf(&shown, &bw, &pf, 0);
                s_prevShown = (unsigned)shown;
                s_prevDrawn = (unsigned)sceGeEdramGetAddr() + (unsigned)g_fb[g_drawIdx];
                s_prevList  = listBytes;
            }
            s_hadContent[0] = s_hadContent[1];
            s_hadContent[1] = hasContent;
        }
    }
#endif
    profEnd(PROF_GESYNC);
}

void guPresent(void) {

    profBegin(PROF_VBLANK);
    sceDisplayWaitVblankStart();
    profEnd(PROF_VBLANK);

    if (g_queuedIdx >= 0) { g_frontIdx = g_queuedIdx; g_queuedIdx = -1; }

    sceDisplaySetFrameBuf(guFbAddr(g_drawIdx), GU_BUF_WIDTH,
                          PSP_DISPLAY_PIXEL_FORMAT_565, PSP_DISPLAY_SETBUF_NEXTFRAME);
    g_queuedIdx = g_drawIdx;

    g_drawIdx = guAcquireDrawBuffer((g_drawIdx + 1) % GU_FB_COUNT);
}

void guSuspendForDialog(void) {
    sceGuSync(0, 0);
    s_dialogUp = true;

    sceDisplayWaitVblankStart();
    if (g_queuedIdx >= 0) { g_frontIdx = g_queuedIdx; g_queuedIdx = -1; }
}

void guResumeFromDialog(void) {
    sceGuSync(0, 0);

    sceDisplaySetFrameBuf(guFbAddr(g_frontIdx), GU_BUF_WIDTH,
                          PSP_DISPLAY_PIXEL_FORMAT_565, PSP_DISPLAY_SETBUF_NEXTFRAME);
    sceDisplayWaitVblankStart();
    g_queuedIdx = -1;
    g_drawIdx   = guAcquireDrawBuffer((g_frontIdx + 1) % GU_FB_COUNT);

    for (int i = 0; i < GU_FB_COUNT; i++) s_bufSum[i] = 0;
    s_bufLast   = -1;
    s_dialogUp  = false;

}

void guDialogBegin(unsigned int clearColor) {

    g_listIdx ^= 1;
    sceGuStart(GU_DIRECT, guListCur());
    g_frameScratch = 0;
    g_listUsed     = 0;
    g_frameId++;
    guTraceResetFrame();
    sceGuDrawBuffer(GU_PSM_5650, g_fb[g_drawIdx], GU_BUF_WIDTH);
    sceGuScissor(0, 0, GU_SCR_WIDTH, GU_SCR_HEIGHT);
    sceGuClearColor(clearColor);
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
}

void guDialogEnd(void) {
    unsigned listBytes = (unsigned)sceGuFinish();
    sceGuSync(0, 0);

    guTraceRecord(listBytes);
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
#if !MCPSP_DIAG
void guDumpFrameLog(void) {}
#else
void guDumpFrameLog(void) {
    FILE* fp = fopen(assetPath("framelog.csv"), "w");
    if (!fp) fp = fopen("ms0:/framelog.csv", "w");
    if (!fp) return;

    fprintf(fp, "# marks bits: 0 skybackdrop 1 skydome 2 skybodies 3 clouds 4 terrain\n");
    fprintf(fp, "# 5 water 6 entities 7 particles 8 hand 9 hud 10 overlay 11 menu 12 hints\n");
    fprintf(fp, "# 13 menubg 14 menucontent 15 uisprite 16 uitext\n");
    fprintf(fp, "# gate: 0=no refusal 1=scratch budget 2=list room. failat = scratch16<<16|listused16\n");
    fprintf(fp, "# a bad frame with bits MISSING = the draw was never issued (read gate/failbytes).\n");
    fprintf(fp, "# a bad frame with bits FULL    = it was issued and did not appear (GE/display).\n");
    fprintf(fp, "# frames seen this session: %u, ring holds the last %u\n",
            s_traceCount, (unsigned)GU_TRACE_FRAMES);

    fprintf(fp, "# empty-frame events: %u | short-list events: %u\n",
            s_emptyCount, s_eventCount);
    {
        unsigned n = s_emptyCount < 16 ? s_emptyCount : 16u;
        for (unsigned i = 0; i < n; i++) {
            const EmptyFrameEvent* e = &s_empties[i];
            fprintf(fp, "# empty frame %u shown %08x drawn %08x %s list %u\n",
                    e->frameNo, e->shown, e->drawn,
                    (e->shown == e->drawn) ? "SAME-BUFFER" : "ok", e->listBytes);
        }
        n = s_eventCount < 32 ? s_eventCount : 32u;
        for (unsigned i = 0; i < n; i++) {
            const ShortListEvent* e = &s_events[i];
            fprintf(fp, "# short-list frame %u list %u prev %u next %u peak %u scratch %u fails %u\n",
                    e->frameNo, e->bytes, e->prevBytes, e->nextBytes,
                    e->peak, e->scratch, e->allocFails);
        }
    }

    fprintf(fp, "frame,us,marks,list,listused,scratch,fails,failbytes,gate,failat,"
                "shown,drawn,samebuf,vramfree,texfails,canary,draw,front,pixsum,pixdrift\n");

#if GU_TRACE_FRAMES > 0

    const unsigned CSV_ROWS = 1024;
    unsigned n = s_traceCount < GU_TRACE_FRAMES ? s_traceCount : (unsigned)GU_TRACE_FRAMES;
    if (n > CSV_ROWS) n = CSV_ROWS;
    const unsigned first = s_traceCount - n;
    for (unsigned i = 0; i < n; i++) {
        const FrameRec* r = &s_trace[(first + i) % GU_TRACE_FRAMES];
        fprintf(fp, "%u,%u,%04x,%u,%u,%u,%u,%u,%u,%08x,%08x,%08x,%d,%u,%u,%u,%u,%u,%08x,%u,%u,%u,%04x,%04x\n",
                r->frameId, r->us, r->marks, r->listBytes, r->listUsed,
                r->scratch, r->allocFails, r->failBytes, r->failGate, r->failAt,
                r->shown, r->drawn, (r->shown == r->drawn) ? 1 : 0,
                r->vramFree, (unsigned)r->texFails, (unsigned)r->canary,
                (unsigned)(r->bufIdx >> 8), (unsigned)(r->bufIdx & 0xff), r->pixSum, r->pixDrift, (unsigned)r->driftCount, (unsigned)r->driftIdx,
                (unsigned)r->driftOld, (unsigned)r->driftNew);
    }
#endif
    fclose(fp);

#if GU_TRACE_FRAMES > 0

    FILE* bf = fopen(assetPath("framelog.bin"), "wb");
    if (!bf) bf = fopen("ms0:/framelog.bin", "wb");
    if (bf) {
        const unsigned hdr[4] = { 0x4d43464cu, (unsigned)sizeof(FrameRec),
                                  (unsigned)GU_TRACE_FRAMES, s_traceCount };
        fwrite(hdr, sizeof(hdr), 1, bf);
        fwrite(s_trace, sizeof(FrameRec), GU_TRACE_FRAMES, bf);
        fclose(bf);
    }
#endif
}
#endif
