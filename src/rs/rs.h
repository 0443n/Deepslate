#ifndef DEEPSLATE_RS_H__
#define DEEPSLATE_RS_H__

// Declarations for the Rust static archive. Every signature here is limited to
// four integer or pointer arguments returning one, the only shape rustc's o32
// output and psp-gcc's EABI32 agree on. Floats cross as their bit pattern.
extern "C" {

int ds_abi_check(int a, int b, int c, int d);
int ds_abi_check_f(int bits);
void* ds_alloc(int align, int size);
void ds_free(void* ptr);

// The world calls the generator makes. The five and six argument ones do not
// fit the four argument shape, so the payload rides in the last word beside a
// signed 16 bit z, which the 256 block world fits inside many times over.
int ds_world_block(struct World* w, int x, int y, int z);
int ds_world_ready(struct World* w, int x, int z);
int ds_world_light_raw(struct World* w, int x, int y, int z);
int ds_world_can_see_sky(struct World* w, int x, int y, int z);
void ds_world_set(struct World* w, int x, int y, int zIdData);
void ds_world_schedule(struct World* w, int x, int y, int zIdDelay);
void ds_world_put(struct World* w, int x, int y, int zId);
void ds_world_column_get(struct World* w, int x, int z, unsigned char* out128);
void ds_world_column_put(struct World* w, int x, int z, const unsigned char* in128);

// The generator itself, see rust/src/ffi.rs.
void ds_gen_init(int seed, int genMask);
void ds_gen_free();
void ds_gen_terrain(struct World* w, int cx, int cz);
int  ds_gen_phase(struct World* w, int cx, int cz, int phase);
void ds_gen_place_flowers(struct World* w);
void ds_gen_place_mushrooms(struct World* w);
void ds_nether_init(int seed);
void ds_nether_free();
void ds_nether_chunk(struct World* w, int cx, int cz);

}

// False when the Rust and C++ halves disagree on argument passing, which means
// the toolchain changed under us and nothing linked from Rust can be trusted.
bool rsAbiOk();

#endif
