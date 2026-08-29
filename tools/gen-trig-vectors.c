#include <pspkernel.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

PSP_MODULE_INFO("trig", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);

static unsigned int b(float f){ unsigned int u; memcpy(&u,&f,4); return u; }
static float f(unsigned int u){ float x; memcpy(&x,&u,4); return x; }
static void emit(float x){ printf("t %08x %08x %08x\n", b(x), b(sinf(x)), b(cosf(x))); }

#define PI 3.14159265f

int main(void)
{
    // Everything levelgen reaches, sampled densely.
    for (int i = 0; i < 4096; i++)
        emit((float)i / 4096.0f * (8.0f * PI) - (4.0f * PI));

    // The exact expressions in caves, ore, clay and the nether ripple.
    for (int count = 8; count <= 64; count += 8)
        for (int d = 0; d <= count; d++) emit((float)d * PI / (float)count);
    for (int i = 0; i < 1024; i++) emit((float)i / 1024.0f * PI);
    for (int y = 0; y < 17; y++) emit((float)y * 3.14159265f * 6.0f / 17.0f);

    // The whole range the medium reduction covers, out to 2^7*(pi/2).
    for (int i = 0; i < 8192; i++)
        emit((float)i / 8192.0f * 402.0f - 201.0f);

    // Straddling every threshold fdlibm branches on, bit by bit. 0x3f490fd8 is
    // pi/4, 0x3fc90fd0 the near-pi/2 case, 0x4016cbe4 3pi/4, 0x43490f80 the
    // cutoff above which the medium path gives up.
    static const unsigned int edges[] = {
        0x3f490fd8u, 0x3fc90fd0u, 0x4016cbe4u, 0x43490f80u, 0x32000000u, 0x3e99999au, 0x3f480000u
    };
    for (unsigned e = 0; e < sizeof edges / sizeof *edges; e++)
        for (int d = -8; d <= 8; d++) {
            emit(f(edges[e] + d));
            emit(-f(edges[e] + d));
        }

    // Either side of every multiple of pi/2 the npio2_hw table guards.
    for (int n = 1; n < 32; n++) {
        float base = (float)n * (PI / 2.0f);
        unsigned int ib = b(base);
        for (int d = -4; d <= 4; d++) { emit(f(ib + d)); emit(-f(ib + d)); }
    }

    printf("DONE\n");
    sceKernelExitGame();
    return 0;
}
