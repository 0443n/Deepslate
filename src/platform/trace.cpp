#include "platform/trace.h"
#include "platform/path.h"
#include "util/prof.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <pspkernel.h>
#include <pspthreadman.h>
#include <pspiofilemgr.h>
#include <pspdisplay.h>
#include <stdlib.h>

static char s_path[320];
static char s_lastCrash[192];
static bool s_ready = false;

static const char* kCleanTag = "EXIT clean";

void traceInit() {
    snprintf(s_path, sizeof(s_path), "%s", assetPath("deepslate_trace.txt"));
    s_lastCrash[0] = 0;

    // The tail of the previous run, kept only when it never marked a clean exit.
    FILE* f = fopen(s_path, "rb");
    if (f) {
        char tail[192];
        tail[0] = 0;
        char line[192];
        bool clean = false;
        while (fgets(line, sizeof(line), f)) {
            if (strstr(line, kCleanTag)) { clean = true; continue; }
            clean = false;
            snprintf(tail, sizeof(tail), "%s", line);
        }
        fclose(f);
        if (!clean && tail[0]) {
            size_t n = strlen(tail);
            while (n && (tail[n - 1] == '\n' || tail[n - 1] == '\r')) tail[--n] = 0;
            snprintf(s_lastCrash, sizeof(s_lastCrash), "%s", tail);
        }
    }

    f = fopen(s_path, "wb");
    if (f) fclose(f);
    s_ready = true;
    traceMark("BOOT");
}

const char* traceLastCrash() { return s_lastCrash; }

void traceMark(const char* fmt, ...) {
    if (!s_ready) return;
    FILE* f = fopen(s_path, "ab");
    if (!f) return;
    fprintf(f, "%8u ", (unsigned)(sceKernelGetSystemTimeLow() / 1000u));
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fflush(f);
    fclose(f);
}


// --- watchdog -------------------------------------------------------------
//
// A frozen console leaves no dump, so the only witness to a hang is a thread
// that was not hung. This one wakes four times a second, watches the frame
// counter, and names the profiler section the main thread stopped inside.

volatile unsigned int g_frameSeq = 0;

// A stack overflow kills every thread at once and leaves no other trace, and
// PPSSPP never reproduces one because it does not enforce stack bounds.
unsigned int g_mainStackMin = 0xFFFFFFFFu;
static SceUID s_mainThid = -1;

static SceUID s_wdThid = -1;
static volatile int s_wdQuit = 0;

// The main thread may be hung holding the malloc or stdio lock, so the stall
// line goes out through the kernel with nothing allocated along the way.
static char* rawNum(char* p, unsigned int v) {
    char tmp[12];
    int n = 0;
    do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) *p++ = tmp[--n];
    return p;
}

static char* rawStr(char* p, const char* s) { while (*s) *p++ = *s++; return p; }

static void rawLine(const char* line, int n) {
    SceUID fd = sceIoOpen(s_path, PSP_O_WRONLY | PSP_O_APPEND | PSP_O_CREAT, 0777);
    if (fd < 0) return;
    sceIoWrite(fd, line, n);
    sceIoClose(fd);
}

static void rawStall(unsigned int quietMs, unsigned int seq) {
    char line[160];
    char* p = line;
    p = rawNum(p, (unsigned)(sceKernelGetSystemTimeLow() / 1000u));
    const char* tag = " STALL ";
    while (*tag) *p++ = *tag++;
    p = rawNum(p, quietMs);
    const char* ms = "ms phase ";
    while (*ms) *p++ = *ms++;
    unsigned char ph = g_profPhase;
    p = rawNum(p, ph);
    *p++ = ' ';
    const char* nm = profSlotName(ph);
    for (int i = 0; i < 16 && nm[i]; i++) *p++ = nm[i];
    const char* sq = " seq ";
    while (*sq) *p++ = *sq++;
    p = rawNum(p, seq);
    *p++ = '\n';
    rawLine(line, (int)(p - line));
}

// A stall may be the memory stick driver itself hanging, and then a raw write
// hangs with it. The screen needs no driver, so the bar goes up first.
static void rawBar() {
    void* fb = 0; int bw = 0, pf = 0;
    if (sceDisplayGetFrameBuf(&fb, &bw, &pf, PSP_DISPLAY_SETBUF_IMMEDIATE) < 0 || !fb || bw <= 0) return;
    unsigned short* px = (unsigned short*)((unsigned int)fb | 0x40000000u);
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 480; x++) px[y * bw + x] = 0xFFFF;
}

static int watchdogMain(SceSize, void*) {
    traceMark("WD alive");
    unsigned int last = 0;
    unsigned int quietMs = 0, nextReport = 2000;
    while (!s_wdQuit) {
        sceKernelDelayThread(250 * 1000);

        if (s_mainThid >= 0) {
            int freeStk = sceKernelGetThreadStackFreeSize(s_mainThid);
            if (freeStk >= 0 && (unsigned int)freeStk < g_mainStackMin)
                g_mainStackMin = (unsigned int)freeStk;
        }

        unsigned int seq = g_frameSeq;
        if (seq != last) { last = seq; quietMs = 0; nextReport = 2000; continue; }
        quietMs += 250;

        // Two seconds is far past a slow frame, and backing off keeps a real
        // lockup from filling the stick with one line per tick.
        if (quietMs >= nextReport) {
            rawBar();
            rawStall(quietMs, seq);
            nextReport *= 2;
        }
    }
    return 0;
}


// Every symptom fits one cause, a stray write into the read-only segment. It
// kills all threads at once when it lands on an instruction, and libpng's own
// signature constant has already been seen corrupted.
extern char _ftext[];
extern char _fdata[];
static void sdWatchInit(void);

static unsigned char* s_shadow = 0;
static unsigned int s_guardLen = 0;
static unsigned int s_guardPos = 0;
unsigned int g_codeFixes = 0;

bool codePtrOk(unsigned int v) {
    return (v & 3) == 0 && v >= (unsigned int)(unsigned long)_ftext
        && v <  (unsigned int)(unsigned long)_fdata;
}

void codeGuardInit(void) {
    unsigned int len = (unsigned int)(_fdata - _ftext);
    if (!len || len > 8u * 1024 * 1024) { traceMark("CG bad len %u", len); return; }
    s_shadow = (unsigned char*)malloc(len);
    if (!s_shadow) { traceMark("CG alloc failed %u", len); return; }
    memcpy(s_shadow, _ftext, len);
    s_guardLen = len;
    traceMark("CG base %08x len %u", (unsigned)(unsigned long)_ftext, len);
    sdWatchInit();
}

// One slice a frame, so the whole segment is covered about once a second and
// the compare never costs a visible millisecond.
void codeGuardStep(void) {
    if (!s_shadow) return;
    const unsigned int SLICE = 64 * 1024;
    unsigned int off = s_guardPos;
    unsigned int n = s_guardLen - off;
    if (n > SLICE) n = SLICE;

    if (memcmp(s_shadow + off, _ftext + off, n) != 0) {
        const unsigned int* a = (const unsigned int*)(s_shadow + off);
        unsigned int* b = (unsigned int*)(_ftext + off);
        int hits = 0;
        for (unsigned int i = 0; i < n / 4; i++) {
            if (a[i] == b[i]) continue;
            if (hits < 6)
                traceMark("CGHIT %08x was %08x now %08x",
                          (unsigned)(unsigned long)(b + i), a[i], b[i]);
            hits++;
            b[i] = a[i];
        }
        traceMark("CGHIT total %d at off %u", hits, off);
        g_codeFixes += (unsigned)hits;
        sceKernelDcacheWritebackAll();
        sceKernelIcacheInvalidateAll();
    }

    off += n;
    s_guardPos = (off >= s_guardLen) ? 0 : off;
}

// A slice a frame takes half a second to come round, so a write that rots a
// constant and then walks into the code kills the machine long before the
// sweep arrives. This compares the lot in one go and names where it landed.
extern char _edata[];

const unsigned int* g_sdWatch = 0;
unsigned int g_sdWant0 = 0, g_sdWant1 = 0;
static unsigned int s_sdTrips = 0;
static const unsigned int* s_sdCached = 0;

// The signature sits in initialised data, so a scan of that region finds it
// without libpng having to export the symbol.
static void sdWatchInit(void) {
    static const unsigned char kSig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    for (char* p = _fdata; p + 8 <= _edata; p += 4) {
        if (memcmp(p, kSig, 8) != 0) continue;
        // A read through the cache can sit on a clean line for minutes while the
        // real word in memory is already rotten, so the watch reads memory.
        s_sdCached = (const unsigned int*)p;
        g_sdWatch = (const unsigned int*)((unsigned int)(unsigned long)p | 0x40000000u);
        g_sdWant0 = g_sdWatch[0];
        g_sdWant1 = g_sdWatch[1];
        traceMark("SDWATCH %08x %08x %08x", (unsigned)(unsigned long)p, g_sdWant0, g_sdWant1);
        return;
    }
    traceMark("SDWATCH not found");
}

// Repairs and keeps going, so one run collects the pattern instead of one hit.
void guardTripped(int slot) {
    unsigned int* w = (unsigned int*)g_sdWatch;
    if (s_sdTrips < 12) {
        const unsigned int* d = w - 4;
        traceMark("SDHIT %s at %08x got %08x %08x cached %08x %08x "
                  "ctx %08x %08x %08x %08x . %08x %08x %08x %08x",
                  profSlotName(slot), (unsigned)(unsigned long)s_sdCached, w[0], w[1],
                  s_sdCached[0], s_sdCached[1],
                  d[0], d[1], d[2], d[3], w[2], w[3], w[4], w[5]);
    }
    s_sdTrips++;
    w[0] = g_sdWant0;
    w[1] = g_sdWant1;
}

unsigned int traceSdTrips(void) { return s_sdTrips; }

void codeGuardFull(const char* where) {
    if (!s_shadow) return;
    if (memcmp(s_shadow, _ftext, s_guardLen) == 0) return;

    const unsigned int* a = (const unsigned int*)s_shadow;
    unsigned int* b = (unsigned int*)_ftext;
    int hits = 0;
    for (unsigned int i = 0; i < s_guardLen / 4; i++) {
        if (a[i] == b[i]) continue;
        if (hits < 8)
            traceMark("CGHIT %s %08x was %08x now %08x", where,
                      (unsigned)(unsigned long)(b + i), a[i], b[i]);
        hits++;
        b[i] = a[i];
    }
    traceMark("CGHIT %s total %d", where, hits);
    g_codeFixes += (unsigned)hits;
    sceKernelDcacheWritebackAll();
    sceKernelIcacheInvalidateAll();
}

void traceWatchdogStart() {
    if (!s_ready || s_wdThid >= 0) return;
    s_mainThid = sceKernelGetThreadId();
    codeGuardInit();

    SceKernelThreadInfo ti;
    memset(&ti, 0, sizeof(ti));
    ti.size = sizeof(ti);
    if (sceKernelReferThreadStatus(s_mainThid, &ti) >= 0)
        traceMark("STACK main %d bytes", ti.stackSize);

    // Above the main thread, or a spinning main thread would starve it.
    s_wdThid = sceKernelCreateThread("mcWatchdog", watchdogMain, 0x10, 4096,
                                     PSP_THREAD_ATTR_USER, 0);
    if (s_wdThid >= 0) sceKernelStartThread(s_wdThid, 0, 0);

    // Silence from the watchdog only means something once we know it was there.
    traceMark("WD thid %d", (int)s_wdThid);
}

static void watchdogStop() {
    if (s_wdThid < 0) return;
    s_wdQuit = 1;
    sceKernelWaitThreadEnd(s_wdThid, 0);
    sceKernelDeleteThread(s_wdThid);
    s_wdThid = -1;
}

void traceClose() {
    watchdogStop();
    traceMark("%s", kCleanTag);
    s_ready = false;
}
