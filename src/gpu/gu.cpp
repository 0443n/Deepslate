#include "gpu/gu.h"
#include "platform/dcache.h"

#include <pspdisplay.h>
#include <pspge.h>
#include <pspgu.h>
#include <pspgum.h>
#include <pspkernel.h>

#include "util/prof.h"
#include "gpu/vram_alloc.h"

static unsigned int __attribute__((aligned(16))) g_list[524288 / 4];
static void* g_listUncached = 0;

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

int g_dither = 0;

void guSetDither(int wanted) {
    if (wanted && g_dither) sceGuEnable(GU_DITHER);
    else                    sceGuDisable(GU_DITHER);
}

unsigned int guFrameId(void) { return g_frameId; }

static void* frameAllocUpTo(int bytes, unsigned int limit) {
    if (bytes <= 0) return 0;
    if (g_frameScratch + (unsigned int)bytes > limit) { g_frameAllocFails++; return 0; }
    void* p = sceGuGetMemory(bytes);
    if (!p) return 0;
    g_frameScratch += (unsigned int)bytes;
    return p;
}

void* guFrameAlloc(int bytes)         { return frameAllocUpTo(bytes, GU_SCRATCH_GENERAL); }
void* guFrameAllocPriority(int bytes) { return frameAllocUpTo(bytes, GU_SCRATCH_BUDGET); }

static unsigned int g_vramOffset = 0;

#define GU_FB_COUNT 3
static void* g_fb[GU_FB_COUNT] = { 0, 0, 0 };
static int   g_drawIdx = 0;

static volatile int g_frontIdx   = 1;
static volatile int g_pendingIdx = -1;

struct GuSubmitted { signed char idx; bool display; };
static volatile GuSubmitted g_submitted[GU_FB_COUNT];
static volatile unsigned    g_submitHead  = 0;
static volatile unsigned    g_submitCount = 0;

static volatile unsigned    g_geFinished  = 0;

static inline void* guFbAddr(int idx) {
    return (void*)((unsigned int)sceGeEdramGetAddr() + (unsigned int)g_fb[idx]);
}

unsigned int g_drawLiveHits = 0;

unsigned int g_drawLiveNext = 0;

unsigned int g_drawLiveOurs = 0;

unsigned int g_geCallbackLate = 0;

unsigned int g_frameSkips = 0;

int g_vblankRegisterFail = 0;

static bool g_geCallbackSet   = false;
static bool g_vblankRegistered = false;

static void guMarkSubmitted(int idx, bool display) {
    int intr = sceKernelCpuSuspendIntr();
    if (g_submitCount < GU_FB_COUNT) {
        unsigned tail = (g_submitHead + g_submitCount) % GU_FB_COUNT;
        g_submitted[tail].idx     = (signed char)idx;
        g_submitted[tail].display = display;
        g_submitCount++;
    }
    sceKernelCpuResumeIntr(intr);
}

static bool g_suppressPresent = false;
void guSuppressNextPresent(void) { g_suppressPresent = true; }

static bool guIsSubmittedLocked(int idx) {
    for (unsigned o = 0; o < g_submitCount; o++)
        if (g_submitted[(g_submitHead + o) % GU_FB_COUNT].idx == (signed char)idx) return true;
    return false;
}

static void guPublishLocked(void) {

    if (g_geFinished > g_submitCount) g_geFinished = g_submitCount;
    while (g_submitCount > 0 && g_geFinished > 0) {
        const int  idx  = g_submitted[g_submitHead].idx;
        const bool disp = g_submitted[g_submitHead].display;
        if (disp && g_pendingIdx >= 0) break;
        g_submitHead = (g_submitHead + 1) % GU_FB_COUNT;
        g_submitCount--;
        g_geFinished--;

        if (disp) { g_pendingIdx = idx; break; }
    }
}

static void guPublishFinishedGeBuffer(void) {
    int intr = sceKernelCpuSuspendIntr();
    guPublishLocked();
    sceKernelCpuResumeIntr(intr);
}

static void guApplyPendingDisplay(void) {
    int intr = sceKernelCpuSuspendIntr();
    if (g_pendingIdx >= 0) {
        sceDisplaySetFrameBuf(guFbAddr(g_pendingIdx), GU_BUF_WIDTH,
                              PSP_DISPLAY_PIXEL_FORMAT_565, PSP_DISPLAY_SETBUF_IMMEDIATE);
        g_frontIdx   = g_pendingIdx;
        g_pendingIdx = -1;

        guPublishLocked();
    }
    sceKernelCpuResumeIntr(intr);
}

unsigned int g_geCbCount = 0;
static void guGeFinishCallback(int ) {
    int intr = sceKernelCpuSuspendIntr();
    g_geCbCount++;
    g_geFinished++;
    guPublishLocked();
    sceKernelCpuResumeIntr(intr);
}
static void guVblankHandler(int , void* ) { guApplyPendingDisplay(); }

static bool guAcquireDrawBuffer(void) {
    int intr = sceKernelCpuSuspendIntr();
    bool got = false;
    if (g_submitCount == 0) {
        int start = (g_drawIdx + 1) % GU_FB_COUNT;
        for (int o = 0; o < GU_FB_COUNT; o++) {
            int idx = (start + o) % GU_FB_COUNT;
            if (idx == g_frontIdx)   continue;
            if (idx == g_pendingIdx) continue;
            if (guIsSubmittedLocked(idx)) continue;
            g_drawIdx = idx;
            got = true;
            break;
        }
    }
    sceKernelCpuResumeIntr(intr);
    return got;
}

static void guDrainSynchronously(void) {
    sceGuSync(0, 0);

    int credit = sceKernelCpuSuspendIntr();
    g_geFinished = g_submitCount;
    sceKernelCpuResumeIntr(credit);

    guPublishFinishedGeBuffer();
    if (g_pendingIdx >= 0) {
        sceDisplayWaitVblankStart();
        guApplyPendingDisplay();
    }
    if (g_submitCount > 0) {
        guPublishFinishedGeBuffer();
        if (g_pendingIdx >= 0) {
            sceDisplayWaitVblankStart();
            guApplyPendingDisplay();
        }
    }
}

void guWaitGeIdle(void) { guDrainSynchronously(); }

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

    g_listUncached = (void*)((unsigned int)g_list | 0x40000000u);

    for (int i = 0; i < GU_FB_COUNT; i++)
        g_fb[i] = guVramAlloc(GU_BUF_WIDTH, GU_SCR_HEIGHT, GU_PSM_5650);
    void* zbp = guVramAlloc(GU_BUF_WIDTH, GU_SCR_HEIGHT, GU_PSM_4444);
    g_drawIdx = 0;

    vramAllocInit(g_vramOffset, VRAM_TOTAL);

    sceGuInit();
    sceGuStart(GU_DIRECT, g_listUncached);

    sceGuDrawBuffer(GU_PSM_5650, g_fb[0], GU_BUF_WIDTH);
    sceGuDispBuffer(GU_SCR_WIDTH, GU_SCR_HEIGHT, g_fb[1], GU_BUF_WIDTH);
    sceGuDepthBuffer(zbp, GU_BUF_WIDTH);

    sceGuOffset(2048 - (GU_SCR_WIDTH / 2), 2048 - (GU_SCR_HEIGHT / 2));
    sceGuViewport(2048, 2048, GU_SCR_WIDTH, GU_SCR_HEIGHT);
    sceGuDepthRange(65535, 0);

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

    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);

    sceGuFinish();
    sceGuSync(0, 0);

    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);

    g_frontIdx   = 1;
    g_drawIdx    = 0;
    g_pendingIdx = -1;
    g_submitHead = 0;
    g_submitCount = 0;
    g_geFinished = 0;

    sceGuSetCallback(GU_CALLBACK_FINISH, guGeFinishCallback);
    g_geCallbackSet = true;

    int rc = sceKernelRegisterSubIntrHandler(PSP_VBLANK_INT, 0, (void*)guVblankHandler, 0);
    if (rc >= 0 && sceKernelEnableSubIntr(PSP_VBLANK_INT, 0) >= 0) {
        g_vblankRegistered = true;
    } else {
        g_vblankRegisterFail = (rc < 0) ? rc : -1;
    }
}

void guTerm(void) {

    if (g_vblankRegistered) {
        sceKernelDisableSubIntr(PSP_VBLANK_INT, 0);
        sceKernelReleaseSubIntrHandler(PSP_VBLANK_INT, 0);
        g_vblankRegistered = false;
    }
    if (g_geCallbackSet) {
        sceGuSetCallback(GU_CALLBACK_FINISH, 0);
        g_geCallbackSet = false;
    }
    sceGuTerm();
}

bool guStartFrame(unsigned int clearColor) {

    int tries = 0;

    const int maxTries = (g_geCallbackLate > 2) ? 0 : 8;
    while (!guAcquireDrawBuffer()) {
        if (tries >= maxTries) {
            g_geCallbackLate++;
            guDrainSynchronously();
            if (!guAcquireDrawBuffer()) { g_frameSkips++; return false; }
            break;
        }
        sceDisplayWaitVblankStartCB();
        tries++;
    }

    sceGuStart(GU_DIRECT, g_listUncached);
    g_frameScratch = 0;
    g_frameId++;

#if DIAG_OVERLAY
    if (g_alarmFrames) { g_alarmFrames--; clearColor = 0xFF0000FFu; }
#endif

    if (g_drawIdx == g_frontIdx || g_drawIdx == g_pendingIdx) {
        g_drawLiveHits++;
        g_drawLiveOurs++;
        profAdd(PROFC_DRAWLIVE, 1);
    }
    sceGuDrawBuffer(GU_PSM_5650, g_fb[g_drawIdx], GU_BUF_WIDTH);

    sceGuScissor(0, 0, GU_SCR_WIDTH, GU_SCR_HEIGHT);

    sceGuClearColor(clearColor);
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
    return true;
}

void guFinishFrame(void) {

    profBegin(PROF_GESYNC);

    const bool display = !g_suppressPresent;
    g_suppressPresent = false;
    guMarkSubmitted(g_drawIdx, display);
    unsigned listBytes = (unsigned)sceGuFinish();
    profListBytes(listBytes);

    if (listBytes > g_listPeakBytes) g_listPeakBytes = listBytes;
    if (listBytes >= sizeof(g_list)) g_listOverruns++;

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

#if DIAG_OVERLAY
    guWaitGeIdle();
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

}

void guSuspendForDialog(void) {
    sceGuSync(0, 0);
    if (g_vblankRegistered) {
        sceKernelDisableSubIntr(PSP_VBLANK_INT, 0);
        sceKernelReleaseSubIntrHandler(PSP_VBLANK_INT, 0);
        g_vblankRegistered = false;
    }
    if (g_geCallbackSet) {
        sceGuSetCallback(GU_CALLBACK_FINISH, 0);
        g_geCallbackSet = false;
    }
    int intr = sceKernelCpuSuspendIntr();
    g_submitHead = 0; g_submitCount = 0; g_geFinished = 0; g_pendingIdx = -1;
    sceKernelCpuResumeIntr(intr);
    g_suppressPresent = false;
}

void guResumeFromDialog(void) {
    sceGuSync(0, 0);
    int intr = sceKernelCpuSuspendIntr();
    g_submitHead = 0; g_submitCount = 0; g_geFinished = 0; g_pendingIdx = -1;
    sceKernelCpuResumeIntr(intr);
    g_suppressPresent = false;

    sceDisplaySetFrameBuf(guFbAddr(g_frontIdx), GU_BUF_WIDTH,
                          PSP_DISPLAY_PIXEL_FORMAT_565, PSP_DISPLAY_SETBUF_NEXTFRAME);
    sceDisplayWaitVblankStart();

    sceGuSetCallback(GU_CALLBACK_FINISH, guGeFinishCallback);
    g_geCallbackSet = true;
    if (sceKernelRegisterSubIntrHandler(PSP_VBLANK_INT, 0, (void*)guVblankHandler, 0) >= 0 &&
        sceKernelEnableSubIntr(PSP_VBLANK_INT, 0) >= 0) {
        g_vblankRegistered = true;
    }
}

void guDialogBegin(unsigned int clearColor) {
    sceGuStart(GU_DIRECT, g_listUncached);
    g_frameScratch = 0;
    g_frameId++;
    sceGuDrawBuffer(GU_PSM_5650, g_fb[g_drawIdx], GU_BUF_WIDTH);
    sceGuScissor(0, 0, GU_SCR_WIDTH, GU_SCR_HEIGHT);
    sceGuClearColor(clearColor);
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
}

void guDialogEnd(void) {
    sceGuFinish();
    sceGuSync(0, 0);
}

void guDialogPresent(void) {
    sceDisplaySetFrameBuf(guFbAddr(g_drawIdx), GU_BUF_WIDTH,
                          PSP_DISPLAY_PIXEL_FORMAT_565, PSP_DISPLAY_SETBUF_NEXTFRAME);
    g_frontIdx = g_drawIdx;
    g_drawIdx  = (g_drawIdx + 1) % GU_FB_COUNT;
    sceDisplayWaitVblankStart();
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
    sceGuStart(GU_DIRECT, g_listUncached);
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
    guSetDither(0);
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
void guDumpFrameLog(void) {
    if (!s_eventCount && !s_emptyCount) return;

    FILE* fp = fopen(assetPath("framelog.txt"), "w");
    if (!fp) fp = fopen("ms0:/framelog.txt", "w");
    if (!fp) return;

    fprintf(fp, "empty frames: %u (first %u kept)\n",
            s_emptyCount, s_emptyCount < 16 ? s_emptyCount : 16u);
    {
        unsigned n = s_emptyCount < 16 ? s_emptyCount : 16u;
        for (unsigned i = 0; i < n; i++) {
            const EmptyFrameEvent* e = &s_empties[i];
            fprintf(fp, "frame %u | shown %08x drawn %08x %s | list %u\n",
                    e->frameNo, e->shown, e->drawn,
                    (e->shown == e->drawn) ? "SAME-BUFFER" : "ok",
                    e->listBytes);
        }
    }

    fprintf(fp, "short-list events: %u (first %u kept)\n",
            s_eventCount, s_eventCount < 32 ? s_eventCount : 32u);
    unsigned n = s_eventCount < 32 ? s_eventCount : 32u;
    for (unsigned i = 0; i < n; i++) {
        const ShortListEvent* e = &s_events[i];

        fprintf(fp, "frame %u | list %u prev %u next %u peak %u (%u%%) | shown %08x drawn %08x %s"
                    " | scratch %u fails %u drawlive %u\n",
                e->frameNo, e->bytes, e->prevBytes, e->nextBytes, e->peak,
                e->peak ? e->bytes * 100 / e->peak : 0,
                e->shown, e->drawn,
                (e->shown == e->drawn) ? "SAME-BUFFER" : "ok",
                e->scratch, e->allocFails, e->drawLive);
    }
    fclose(fp);
}
