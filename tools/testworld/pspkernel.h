#ifndef MCPSP_TEST_PSPKERNEL_H
#define MCPSP_TEST_PSPKERNEL_H

// Stub so the generator sources build on the host. Only the sleep between
// chunks reaches this, and the vector run does not go through that loop.
static inline int sceKernelDelayThread(unsigned int) { return 0; }

#endif
