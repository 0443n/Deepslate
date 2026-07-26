#ifndef MCPSP_PLATFORM_TIME_H
#define MCPSP_PLATFORM_TIME_H

#include <pspkernel.h>

static inline bool timeReached(unsigned int nowUs, unsigned int deadlineUs) {
    return (int)(nowUs - deadlineUs) >= 0;
}

extern SceInt64 g_timeBootUs;

static inline float nowSeconds() {
    if (g_timeBootUs == 0) {
        g_timeBootUs = sceKernelGetSystemTimeWide();
    }
    return (float)(sceKernelGetSystemTimeWide() - g_timeBootUs) / 1000000.0f;
}

#endif
