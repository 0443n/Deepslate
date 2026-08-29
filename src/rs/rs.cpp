#include "rs/rs.h"

#include "world/level/world.h"

#include <cstdlib>
#include <malloc.h>

bool rsAbiOk() {
    if (ds_abi_check(1, 2, 3, 4) != 1234) return false;

    // The float half, which goes out through Rust into newlib's sqrtf. Bits
    // cross as ints because the boundary itself only carries those.
    union { float f; int i; } in, out;
    in.f = 4.0f;
    out.i = ds_abi_check_f(in.i);
    return out.f == 2.0f;
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

static inline int unpackZ(int packed) { return (short)(packed & 0xffff); }

extern "C" int ds_world_block(World* w, int x, int y, int z) {
    return worldBlock(w, x, y, z);
}

extern "C" int ds_world_ready(World* w, int x, int z) {
    return worldReady(w, x, z) ? 1 : 0;
}

extern "C" int ds_world_light_raw(World* w, int x, int y, int z) {
    return lightRawAt(w, x, y, z);
}

extern "C" int ds_world_can_see_sky(World* w, int x, int y, int z) {
    return worldCanSeeSky(w, x, y, z) ? 1 : 0;
}

extern "C" void ds_world_set(World* w, int x, int y, int zIdData) {
    worldSetBlockAndData(w, x, y, unpackZ(zIdData),
                         (unsigned char)((zIdData >> 16) & 0xff),
                         (unsigned char)((zIdData >> 24) & 0xff));
}

extern "C" void ds_world_schedule(World* w, int x, int y, int zIdDelay) {
    worldScheduleTick(w, x, y, unpackZ(zIdDelay),
                      (unsigned char)((zIdDelay >> 16) & 0xff),
                      (zIdDelay >> 24) & 0xff);
}

extern "C" void ds_world_put(World* w, int x, int y, int zId) {
    blockPut(w, x, y, unpackZ(zId), (unsigned char)((zId >> 16) & 0xff));
}

extern "C" void ds_world_column_get(World* w, int x, int z, unsigned char* out128) {
    blockColumnGet(w, x, z, out128);
}

extern "C" void ds_world_column_put(World* w, int x, int z, const unsigned char* in128) {
    blockColumnPut(w, x, z, in128);
}
