
#ifndef MCPSP_UTIL_FAST_MEMCPY_H
#define MCPSP_UTIL_FAST_MEMCPY_H

#include <stddef.h>

void memcpy_vfpu(void* dst, const void* src, size_t size);

#endif
