#ifndef DEEPSLATE_RS_H__
#define DEEPSLATE_RS_H__

// Declarations for the Rust static archive. Every signature here is limited to
// four integer or pointer arguments returning one, the only shape rustc's o32
// output and psp-gcc's EABI32 agree on. Floats cross as their bit pattern.
class Level;

extern "C" {

int ds_abi_check(int a, int b, int c, int d);
int ds_abi_check_f(int bits);
void* ds_alloc(int align, int size);
void ds_free(void* ptr);

// The world calls the generator makes. The five and six argument ones do not
// fit the four argument shape, so the payload rides in the last word beside a
// signed 16 bit z, which the 256 block world fits inside many times over.
int ds_world_block(struct World* w, int x, int y, int z);
int ds_world_data(struct World* w, int x, int y, int z);
int ds_world_ready(struct World* w, int x, int z);
int ds_world_light_raw(struct World* w, int x, int y, int z);
int ds_world_can_see_sky(struct World* w, int x, int y, int z);
int ds_world_brightness(struct World* w, int x, int y, int z);
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

// The pathfinder, see rust/src/pathfinder.rs. The query and the result cross by
// pointer, which keeps the call itself inside the four argument shape.
struct DsPathQuery {
    float bbX0, bbY0, bbZ0;
    float x, z;
    float bbWidth, bbHeight;
    float tx, ty, tz;
    float maxDist;
    int   inWater;
    int   avoidWater;
};

struct DsPathOut {
    int   len;
    short pts[64][3];
};

void ds_path_init(const unsigned char* blockFlags);
void ds_path_free();
int  ds_path_find(struct World* w, const struct DsPathQuery* q, struct DsPathOut* out);

// atan2 needs two floats, the one shape the two conventions disagree on, so the
// angle crosses as bit patterns. See rust/src/newlib.rs.
int ds_atan2f(int yBits, int xBits);

// The mob AI, see rust/src/mob. One snapshot in, one set of intents out, so the
// call itself stays inside the four argument shape.

struct DsEntityView {
    int   valid, alive, canSee, extra;
    float x, y, z;
    float bbY0, bbY1;
    float head, width;
};

struct DsMobIn {
    int kind, age, noActionTime, fleeTime, attackTime, health;
    int onFire, inWater, inLava, onGround, horizCollision;
    int lastHurtTime, gateCanAttack, gateKeepTarget, targetSlot;
    int isDay, fireImmune;

    float x, y, z;
    float bbX0, bbY0, bbZ0;
    float bbWidth, bbHeight, headHeight;
    float yRot, xRot;
    // Where the body points. yRot is the head, which the look control moves
    // independently.
    float yBodyRot;
    float xd, yd, zd;
    float runSpeed;

    struct DsEntityView player;
    struct DsEntityView attacker;
    struct DsEntityView parent;
};

struct DsMobOut {
    float xxa, yya, yRot, xRot;
    int   jumping, setVel;
    float xd, yd, zd;
    int   targetSlot, attack, ranged;
    float rangedDist;
    int   swellDir, eatTick, eatBlock;
    // Where the head points. Separate from yRot, which steers the body.
    float yHeadRot;
    // Seconds of fire to set and smoke puffs to throw, both 0 for nothing.
    int   ignite, smoke;
    int   alertOthers;
};

// What DsMobOut::eatBlock asks for, see EAT_ in rust/src/mob/goals/animal.rs.
#define DS_EAT_GRASS_BLOCK 1
#define DS_EAT_TALL_GRASS  2

// Mob spawning, see rust/src/spawner.rs. Rust picks the spots, C++ builds the
// mob and answers whether it accepted the one it was offered.
struct DsSpawnReq {
    int   mobId, baby;
    float x, y, z, yRot;
};

int  ds_spawn_tile(Level* l, int x, int y, int z);
int  ds_spawn_solid(Level* l, int x, int y, int z);
int  ds_spawn_brightness(Level* l, int x, int y, int z);
int  ds_spawn_top_solid(Level* l, int x, int z);
int  ds_spawn_chunk_ready(Level* l, int cx, int cz);
int  ds_spawn_count_base(Level* l, int base);
int  ds_spawn_count_type(Level* l, int mobId);
int  ds_spawn_count_creatures_near(Level* l, int pxBits, int pzBits, int r);
int  ds_spawn_player(Level* l, float* out3);
int  ds_spawn_player_within(Level* l, const float* pos3, int r);
int  ds_spawn_peaceful(Level* l);
int  ds_spawn_slot_count(Level* l);
int  ds_spawn_slot_chunk(Level* l, int i);
int  ds_spawn_place(Level* l, const struct DsSpawnReq* req);

void ds_spawn_init(int seed);
void ds_spawn_run(Level* l, int enemies, int friendlies);
void ds_spawn_populate(Level* l, int seed);

struct DsMobAi;
struct DsMobAi* ds_mob_new(int kind, int seed, int temptItem);
void ds_mob_free(struct DsMobAi* ai);
void ds_mob_tick(struct DsMobAi* ai, struct World* w,
                 const struct DsMobIn* in, struct DsMobOut* out);
int  ds_mob_debug(struct DsMobAi* ai, unsigned char* buf, int cap);

}

// Mob kinds ds_mob_new takes, matching rust/src/mob/mod.rs.
enum {
    DS_MOB_PIG = 0, DS_MOB_COW = 1, DS_MOB_CHICKEN = 2, DS_MOB_SHEEP = 3,
    DS_MOB_ZOMBIE = 4, DS_MOB_PIG_ZOMBIE = 5, DS_MOB_SKELETON = 6,
    DS_MOB_CREEPER = 7, DS_MOB_SPIDER = 8
};

// Which snapshot view the AI settled on as the mob's target.
enum { DS_TARGET_NONE = 0, DS_TARGET_PLAYER = 1, DS_TARGET_ATTACKER = 2 };

// Block flag bits ds_path_init takes, matching rust/src/pathfinder.rs.
enum {
    DS_BLOCK_SOLID = 1, DS_BLOCK_WATER = 2, DS_BLOCK_LAVA = 4,
    DS_BLOCK_FENCE = 8, DS_BLOCK_DOOR = 16
};

// False when the Rust and C++ halves disagree on argument passing, which means
// the toolchain changed under us and nothing linked from Rust can be trusted.
bool rsAbiOk();

#endif
