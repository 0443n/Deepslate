#include "rs/rs.h"

#include <cstdlib>
#include <malloc.h>

bool rsAbiOk() {
    return ds_abi_check(1, 2, 3, 4) == 1234;
}

// Rust's global allocator on the PSP, see rust/src/heap.rs. memalign because a
// Layout can ask for more than malloc's guarantee.
extern "C" void* ds_alloc(int align, int size) {
    if (align < (int)sizeof(void*)) align = (int)sizeof(void*);
    return memalign((size_t)align, (size_t)size);
}

extern "C" void ds_free(void* ptr) {
    free(ptr);
}
