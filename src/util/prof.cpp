#include "util/prof.h"
#if PROF

#include <pspthreadman.h>
#include <stdio.h>
#include "platform/path.h"

extern bool g_worldBuilt;

static FILE* s_fp = 0;
int g_profLines = -1;

static unsigned int s_cnt[PROFC_N];
static unsigned int s_t0[PROF_N];
static unsigned char s_open[PROF_N];
static unsigned int s_acc[PROF_N];
static unsigned int s_frames;
static unsigned int s_flushT0;
static unsigned int s_lastFrame;
static unsigned int s_maxFrame;

void profAdd(int slot, int n) { s_cnt[slot] += (unsigned int)n; }

void profBegin(int slot) {

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
        s_flushT0 = now; s_lastFrame = now; s_frames = 0; s_maxFrame = 0;
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
                          avg[PROF_HUD] + avg[PROF_GESYNC] + avg[PROF_VBLANK]);

    if (!s_fp) {
        s_fp = fopen(assetPath("prof.txt"), "w");

        if (!s_fp) s_fp = fopen("ms0:/prof.txt", "w");
        if (s_fp) g_profLines = 0;
    }
    FILE* fp = s_fp;
    if (fp) {
        ++g_profLines;
        fprintf(fp, "fps %.1f frame %u max %u | tick %u (plr %u wtick %u [rand %u pend %u] ent %u te %u part %u) "
                    "world %u (light %u rebuild %u [scan %u build %u] cull %u) "
                    "sky %u ent %u water %u part %u hud %u gesync %u vblank %u | other %d "
                    "| n(part %.0f sect %.1f pend %.0f)\n",
                f * 1000000.0f / (float)elapsed, frame, s_maxFrame,
                avg[PROF_TICK], avg[PROF_TPLAYER], avg[PROF_TWORLD],
                avg[PROF_TRAND], avg[PROF_TPEND], avg[PROF_TENT],
                avg[PROF_TTE], avg[PROF_TPART],
                avg[PROF_WORLD], avg[PROF_LIGHT], avg[PROF_REBUILD],
                avg[PROF_RSCAN], avg[PROF_RBUILD], avg[PROF_CULL],
                avg[PROF_SKY], avg[PROF_ENTITY], avg[PROF_WATER], avg[PROF_PART],
                avg[PROF_HUD], avg[PROF_GESYNC], avg[PROF_VBLANK],
                (int)frame - accounted,
                s_cnt[PROFC_PARTICLES] / f, s_cnt[PROFC_SECTIONS] / f, s_cnt[PROFC_PENDLIST] / f);
        fflush(fp);
    }

    for (int i = 0; i < PROF_N; i++) s_acc[i] = 0;
    for (int i = 0; i < PROFC_N; i++) s_cnt[i] = 0;
    s_frames = 0;
    s_maxFrame = 0;

    s_flushT0 = sceKernelGetSystemTimeLow();
    s_lastFrame = s_flushT0;
}

#endif
