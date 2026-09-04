#include "platform/path.h"

#include <cstdio>
#include <cstring>
#include <pspkernel.h>
#include <pspthreadman.h>

static char g_base[256] = "ms0:/PSP/GAME/DEEPSLATE/";

// One shared return buffer lets a second thread rewrite a path between the call
// and the fopen that consumes it, which then opens a different file entirely.
// Each thread gets its own pair, so a caller may hold two paths at once.
enum { PATH_ROWS = 6, PATH_DEPTH = 2, PATH_LEN = 320 };
static char   g_buf[PATH_ROWS][PATH_DEPTH][PATH_LEN];
static SceUID g_owner[PATH_ROWS];
static int    g_next[PATH_ROWS];
static SceUID g_rowLock = -1;

void pathInit(const char* argv0) {
    if (g_rowLock < 0) g_rowLock = sceKernelCreateSema("mcPathRows", 0, 1, 1, 0);
    if (!argv0 || !argv0[0])
        return;
    strncpy(g_base, argv0, sizeof(g_base) - 1);
    g_base[sizeof(g_base) - 1] = '\0';

    char* slash = strrchr(g_base, '/');
    if (slash)
        slash[1] = '\0';
}

static int rowFor(SceUID me) {
    if (g_rowLock >= 0) sceKernelWaitSema(g_rowLock, 1, 0);
    int row = -1, freeRow = -1;
    for (int i = 0; i < PATH_ROWS; i++) {
        if (g_owner[i] == me) { row = i; break; }
        if (!g_owner[i] && freeRow < 0) freeRow = i;
    }
    if (row < 0) { row = (freeRow >= 0) ? freeRow : 0; g_owner[row] = me; }
    if (g_rowLock >= 0) sceKernelSignalSema(g_rowLock, 1);
    return row;
}

const char* assetPath(const char* rel) {
    const int row = rowFor(sceKernelGetThreadId());
    char* out = g_buf[row][g_next[row]];
    g_next[row] = (g_next[row] + 1) % PATH_DEPTH;
    snprintf(out, PATH_LEN, "%s%s", g_base, rel);
    return out;
}
