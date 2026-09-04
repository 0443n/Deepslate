#include "rs/rs.h"

#include "world/level/world.h"
#include "world/level/level.h"
#include "world/entity/local_player.h"
#include "world/entity/mob.h"
#include "world/entity/mob_factory.h"
#include "world/entity/mob_category.h"
#include "world/entity/animal/animal.h"
#include "world/difficulty.h"

#include <cmath>

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

extern "C" int ds_world_data(World* w, int x, int y, int z) {
    return worldData(w, x, y, z);
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

// Brightness rides as thousandths, an int, because the boundary only carries
// those and the AI never wants more resolution than that.
extern "C" int ds_world_brightness(World* w, int x, int y, int z) {
    (void)w;
    return (int)(g_level.getBrightness(x, y, z) * 1000.0f);
}

extern "C" int ds_atan2f(int yBits, int xBits) {
    union { float f; int i; } y, x, r;
    y.i = yBits; x.i = xBits;
    r.f = atan2f(y.f, x.f);
    return r.i;
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

// --- mob spawning ---------------------------------------------------------
//
// Rust owns the decisions, see rust/src/spawner.rs. These are the questions it
// asks of the level, plus the one call that hands an accepted spot back.

extern "C" int ds_spawn_tile(Level* l, int x, int y, int z) {
    return l->getTile(x, y, z);
}

extern "C" int ds_spawn_solid(Level* l, int x, int y, int z) {
    return l->isSolidBlockingTile(x, y, z) ? 1 : 0;
}

extern "C" int ds_spawn_brightness(Level* l, int x, int y, int z) {
    return l->getRawBrightness(x, y, z);
}

extern "C" int ds_spawn_top_solid(Level* l, int x, int z) {
    return l->getTopSolidBlock(x, z);
}

extern "C" int ds_spawn_chunk_ready(Level* l, int cx, int cz) {
    return l->hasChunksAt(cx * 16, 0, cz * 16, cx * 16 + 15, 0, cz * 16 + 15) ? 1 : 0;
}

extern "C" int ds_spawn_count_base(Level* l, int base) {
    return l->countInstanceOfBaseType(base);
}

extern "C" int ds_spawn_count_type(Level* l, int mobId) {
    return l->countInstanceOfType(mobId);
}

// Vanilla counts the cap over the chunks loaded around a player. This world is
// small enough to stay resident whole, so the radius is applied by hand, or
// animals on the far side hold the cap full for good.
extern "C" int ds_spawn_count_creatures_near(Level* l, int pxBits, int pzBits, int r) {
    union { float f; int i; } px, pz;
    px.i = pxBits; pz.i = pzBits;
    int n = 0;
    for (size_t i = 0; i < l->entities.size(); i++) {
        Entity* e = l->entities[i];
        if (!e || e->removed || !e->isMob()) continue;
        if (e->getCreatureBaseType() != MobCategory::creature.baseType) continue;
        float dx = e->x - px.f, dz = e->z - pz.f;
        if (dx * dx + dz * dz <= (float)(r * r)) n++;
    }
    return n;
}

extern "C" int ds_spawn_player(Level* l, float* out3) {
    LocalPlayer* p = l->player;
    if (!p) return 0;
    out3[0] = p->x;
    // The feet, which is where a spawn distance is measured from.
    out3[1] = p->y - p->heightOffset;
    out3[2] = p->z;
    return 1;
}

extern "C" int ds_spawn_player_within(Level* l, const float* pos3, int r) {
    return l->getNearestPlayer(pos3[0], pos3[1], pos3[2], (float)r) ? 1 : 0;
}

extern "C" int ds_spawn_peaceful(Level* l) {
    return l->getDifficulty() == Difficulty::PEACEFUL ? 1 : 0;
}

extern "C" int ds_spawn_slot_count(Level* l) {
    return l->w->slotN * l->w->slotN;
}

extern "C" int ds_spawn_slot_chunk(Level* l, int i) {
    if (i < 0 || i >= l->w->slotN * l->w->slotN) return -1;
    const LevelChunk* lc = &l->w->slots[i];
    if (!lc->resident) return -1;
    return (lc->x & 0xffff) | ((lc->z & 0xffff) << 16);
}

extern "C" int ds_spawn_place(Level* l, const DsSpawnReq* req) {
    Mob* m = MobFactory::createMob(req->mobId, l);
    if (!m) return 0;
    m->moveTo(req->x, req->y, req->z, req->yRot, 0.0f);
    if (!m->canSpawn()) { delete m; return 0; }
    if (req->baby && m->getCreatureBaseType() == MobCategory::creature.baseType)
        ((Animal*)m)->setAge(-24000);
    l->addEntity(m);
    return 1;
}
