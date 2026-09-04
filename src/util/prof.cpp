#include "util/prof.h"
#include <pspdisplay.h>

volatile unsigned char g_profPhase = 0;

static const char* const kSlotName[PROF_N] = {
    "tick",
    "tplayer",
    "tworld",
    "trand",
    "tpend",
    "tent",
    "tte",
    "tpart",
    "world",
    "stream",
    "sgen",
    "sdecor",
    "slight",
    "sdisk",
    "sevict",
    "smisc",
    "light",
    "rebuild",
    "cull",
    "cevict",
    "cwalk",
    "cmark",
    "cgather",
    "csubmit",
    "csync",
    "rscan",
    "rbuild",
    "memit",
    "mpack",
    "malloc",
    "mconv",
    "sky",
    "entity",
    "water",
    "part",
    "hud",
    "hitem",
    "hbar",
    "hdbg",
    "gstart",
    "gpre",
    "gmid",
    "gpost",
    "goutline",
    "ghand",
    "gfire",
    "gesync",
    "vblank",
};

const char* profSlotName(int slot) {
    return (slot >= 0 && slot < PROF_N) ? kSlotName[slot] : "?";
}

// Four rows of six blocks, most significant on the left, white for one. Small
// bit strips did not survive being photographed off the screen; these do.
static unsigned short* s_fb = 0;
static int s_bw = 0;

enum { BLK_W = 20, BLK_H = 12, BLK_STEP = 26, BLK_BITS = 6, ROW_STEP = 14, PHASE_ROWS = 5 };

static int s_cur[PHASE_ROWS]   = { 0, 0, 0, 0, 0 };
static int s_drawn[PHASE_ROWS] = { -1, -1, -1, -1, -1 };

static void drawBit(int row, int b, int set) {
    unsigned short c = set ? 0xFFFF : 0xF800;
    unsigned short* p = s_fb + (2 + row * ROW_STEP) * s_bw + b * BLK_STEP;
    for (int y = 0; y < BLK_H; y++, p += s_bw)
        for (int x = 0; x < BLK_W; x++) p[x] = c;
}

// Only the bits that actually changed are repainted, so a change costs a few
// hundred stores rather than a few thousand.
void phaseRow(int row, int value) {
    if (row < 0 || row >= PHASE_ROWS) return;
    value &= (1 << BLK_BITS) - 1;
    s_cur[row] = value;
    if (!s_fb || value == s_drawn[row]) return;
    int diff = (s_drawn[row] < 0) ? ((1 << BLK_BITS) - 1) : (value ^ s_drawn[row]);
    for (int b = 0; b < BLK_BITS; b++) {
        int m = 1 << (BLK_BITS - 1 - b);
        if (diff & m) drawBit(row, b, value & m);
    }
    s_drawn[row] = value;
}

void phaseFrameBegin(void) {
    void* fb = 0; int bw = 0, pf = 0;
    if (sceDisplayGetFrameBuf(&fb, &bw, &pf, PSP_DISPLAY_SETBUF_IMMEDIATE) < 0
        || !fb || bw <= 0) { s_fb = 0; return; }
    s_fb = (unsigned short*)((unsigned int)fb | 0x40000000u);
    s_bw = bw;
    // Rows 1 and up carry over otherwise, and a stale value reads exactly like a
    // fresh one. All ones is a value no caller writes, so it means never reached.
    for (int r = 1; r < PHASE_ROWS; r++) s_cur[r] = (1 << BLK_BITS) - 1;
    // The two buffers alternate, so each one is repainted in full once a frame.
    for (int r = 0; r < PHASE_ROWS; r++) { s_drawn[r] = -1; phaseRow(r, s_cur[r]); }
}

void phaseMark(int slot) { phaseRow(0, slot); }

#if PROF

#include <pspthreadman.h>
#include <stdio.h>
#include "platform/path.h"

extern bool g_worldBuilt;

extern unsigned int g_meshFallbacks;

#define PROF_REOPEN_LINES 15

static FILE* s_fp = 0;
static int s_profOpened = 0;
int g_profLines = -1;

static unsigned int s_cnt[PROFC_N];
static unsigned int s_t0[PROF_N];
static unsigned char s_open[PROF_N];
static unsigned int s_acc[PROF_N];
static unsigned int s_frames;
static unsigned int s_flushT0;
static unsigned int s_lastFrame;
static unsigned int s_maxFrame;

static unsigned int s_maxList;

static unsigned int s_minList;

void profAdd(int slot, int n) { s_cnt[slot] += (unsigned int)n; }

void profListBytes(unsigned bytes) {
    if (bytes > s_maxList) s_maxList = bytes;
    if (!s_minList || bytes < s_minList) s_minList = bytes;
}

void profBegin(int slot) {
    g_profPhase = (unsigned char)slot;
    phaseMark(slot);

    s_t0[slot] = sceKernelGetSystemTimeLow();
    s_open[slot] = 1;
}

void profEnd(int slot) {

    if (!s_open[slot]) return;
    s_acc[slot] += sceKernelGetSystemTimeLow() - s_t0[slot];
    s_open[slot] = 0;
}

void profFrameEnd(void) {
    unsigned int now = sceKernelGetSystemTimeLow();
    if (!g_worldBuilt) { s_flushT0 = 0; return; }
    if (!s_flushT0) {
        s_flushT0 = now; s_lastFrame = now; s_frames = 0; s_maxFrame = 0; s_maxList = 0; s_minList = 0;
        for (int i = 0; i < PROF_N; i++) s_acc[i] = 0;
        return;
    }
    unsigned int dt = now - s_lastFrame;
    s_lastFrame = now;
    if (dt > s_maxFrame) s_maxFrame = dt;
    ++s_frames;

    unsigned int elapsed = now - s_flushT0;
    if (elapsed < 1000000u) return;

    float f = (float)s_frames;
    unsigned int avg[PROF_N];
    for (int i = 0; i < PROF_N; i++) avg[i] = (unsigned int)(s_acc[i] / f);
    unsigned int frame = (unsigned int)(elapsed / f);
    int accounted = (int)(avg[PROF_TICK] + avg[PROF_WORLD] + avg[PROF_SKY] +
                          avg[PROF_ENTITY] + avg[PROF_WATER] + avg[PROF_PART] +
                          avg[PROF_HUD] + avg[PROF_GESYNC] + avg[PROF_VBLANK] +
                          avg[PROF_GSTART] + avg[PROF_GPRE] + avg[PROF_GMID] + avg[PROF_GPOST]);

    if (!s_fp) {
        const char* mode = s_profOpened ? "a" : "w";
        s_fp = fopen(assetPath("prof.txt"), mode);

        if (!s_fp) s_fp = fopen("ms0:/prof.txt", mode);
        if (s_fp) { s_profOpened = 1; if (g_profLines < 0) g_profLines = 0; }
    }
    FILE* fp = s_fp;
    if (fp) {
        ++g_profLines;
        fprintf(fp, "fps %.1f frame %u max %u list %u lmin %u | tick %u (plr %u wtick %u [rand %u pend %u] ent %u te %u part %u) "
                    "world %u (stream %u [gen %u dec %u lit %u disk %u evict %u misc %u] light %u rebuild %u [scan %u build %u (emit %u pack %u [alloc %u conv %u])] cull %u [ev %u walk %u mark %u] gath %u sub %u sync %u) "
                    "sky %u ent %u water %u part %u hud %u (item %u bar %u dbg %u) gesync %u vblank %u "
                    "| gstart %u gpre %u gmid %u gpost %u [out %u hand %u fire %u] other %d "
                    "| n(part %.0f sect %.1f pend %.0f strm %.2f live %.2f fb %u vert %.0f mark %.1f drawn %.0f dvert %.0f [op %.0f nm %.0f lv %.0f wt %.0f] wnode %.0f mface %.0f mquad %.0f)\n",
                f * 1000000.0f / (float)elapsed, frame, s_maxFrame, s_maxList, s_minList,
                avg[PROF_TICK], avg[PROF_TPLAYER], avg[PROF_TWORLD],
                avg[PROF_TRAND], avg[PROF_TPEND], avg[PROF_TENT],
                avg[PROF_TTE], avg[PROF_TPART],
                avg[PROF_WORLD], avg[PROF_STREAM], avg[PROF_SGEN], avg[PROF_SDECOR],
                avg[PROF_SLIGHT], avg[PROF_SDISK], avg[PROF_SEVICT], avg[PROF_SMISC],
                avg[PROF_LIGHT], avg[PROF_REBUILD],
                avg[PROF_RSCAN], avg[PROF_RBUILD], avg[PROF_MEMIT], avg[PROF_MPACK],
                avg[PROF_MALLOC], avg[PROF_MCONV], avg[PROF_CULL],
                avg[PROF_CEVICT], avg[PROF_CWALK], avg[PROF_CMARK],
                avg[PROF_CGATHER], avg[PROF_CSUBMIT], avg[PROF_CSYNC],
                avg[PROF_SKY], avg[PROF_ENTITY], avg[PROF_WATER], avg[PROF_PART],
                avg[PROF_HUD], avg[PROF_HITEM], avg[PROF_HBAR], avg[PROF_HDBG],
                avg[PROF_GESYNC], avg[PROF_VBLANK],
                avg[PROF_GSTART], avg[PROF_GPRE], avg[PROF_GMID], avg[PROF_GPOST],
                avg[PROF_GOUTLINE], avg[PROF_GHAND], avg[PROF_GFIRE],
                (int)frame - accounted,
                s_cnt[PROFC_PARTICLES] / f, s_cnt[PROFC_SECTIONS] / f, s_cnt[PROFC_PENDLIST] / f,
                s_cnt[PROFC_STREAMIN] / f, s_cnt[PROFC_DRAWLIVE] / f, g_meshFallbacks,
                s_cnt[PROFC_PACKVERTS] / f, s_cnt[PROFC_MARKED] / f,
                s_cnt[PROFC_DRAWNSEC] / f, s_cnt[PROFC_DRAWNVERT] / f,
                s_cnt[PROFC_VOPAQUE] / f, s_cnt[PROFC_VNOMIP] / f,
                s_cnt[PROFC_VLEAVES] / f, s_cnt[PROFC_VWATER] / f,
                s_cnt[PROFC_WALKNODES] / f,
                s_cnt[PROFC_MFACES] / f, s_cnt[PROFC_MQUADS] / f);
        fflush(fp);

        // NOTE: the directory entry only records the size on close, so a power-off
        // with the handle open loses the whole capture. Reopening bounds that.
        if (g_profLines % PROF_REOPEN_LINES == 0) { fclose(s_fp); s_fp = 0; }
    }

    for (int i = 0; i < PROF_N; i++) s_acc[i] = 0;
    for (int i = 0; i < PROFC_N; i++) s_cnt[i] = 0;
    s_frames = 0;
    s_maxFrame = 0;
    s_maxList = 0;
    s_minList = 0;

    s_flushT0 = sceKernelGetSystemTimeLow();
    s_lastFrame = s_flushT0;
}

#endif
