#include "world/level/dimension.h"
#include "world/level/world.h"
#include "world/level/level.h"
#include "world/entity/local_player.h"
#include "world/level/storage/level_storage.h"
#include "world/level/tile/nether_portal.h"
#include "world/level/levelgen/level_source.h"
#include "world/level/levelgen/nether_gen.h"
#include "client/player/player.h"
#include "client/player/player_state.h"
#include "world/level/chunk/chunk_cache.h"
#include "util/mth.h"

int  g_dimSwapTarget = -1;
bool g_dimSwapping   = false;

static int s_destX = 0, s_destZ = 0;

// Vanilla's 8:1 ratio, measured from the middle of this fixed square world so a
// round trip lands back near where it started instead of drifting to a corner.
static void scaleCoords(int from, int x, int z, int* ox, int* oz) {
    float s = (from == DIM_OVERWORLD) ? 0.125f : 8.0f;
    int cx = WORLD_W / 2, cz = WORLD_D / 2;
    int nx = cx + (int)((float)(x - cx) * s);
    int nz = cz + (int)((float)(z - cz) * s);
    if (nx < 8) nx = 8;
    if (nx > WORLD_W - 9) nx = WORLD_W - 9;
    if (nz < 8) nz = 8;
    if (nz > WORLD_D - 9) nz = WORLD_D - 9;
    *ox = nx; *oz = nz;
}

void dimensionRequest(int target) {
    if (g_dimSwapping || g_dimSwapTarget >= 0) return;
    if (target == LevelStorage::getActiveDim()) return;
    g_dimSwapTarget = target;
}

void dimensionSwapTearDown(World* w) {
    int from = LevelStorage::getActiveDim();
    scaleCoords(from, Mth::floor(g_level.player->x), Mth::floor(g_level.player->z),
                &s_destX, &s_destZ);

    LevelStorage::saveDimension(w);

    worldGenWorkerStop();
    g_level.removeAllEntities();
    g_level.removeAllTileEntities();
    worldFree(w);

    LevelStorage::setActiveDim(g_dimSwapTarget);
    g_dimSwapTarget = -1;
    g_dimSwapping = true;
}

bool dimensionSwapBuild(World* w) {
    const int dim  = LevelStorage::getActiveDim();
    const long seed = LevelStorage::getActiveSeed();

    if (!LevelStorage::loadDimension(w, s_destX >> 4, s_destZ >> 4)) {
        if (dim == DIM_NETHER) netherGenInit(seed);
        int type = (dim == DIM_NETHER) ? WORLD_TYPE_NETHER : LevelStorage::getActiveWorldType();
        if (!worldInitTerrain(w, seed, type)) return false;
    }

    int px, py, pz;
    NetherPortal::findOrCreate(w, s_destX, s_destZ, &px, &py, &pz);
    g_level.player->x = px + 0.5f;
    g_level.player->z = pz + 0.5f;
    g_level.player->y = py + PLAYER_EYE;
    g_level.player->portalIgnore = true;

    // level.dat carries the dimension and the new position, so a quit right after
    // stepping through does not put the player back on the far side.
    LevelStorage::save(w, LevelStorage::getActiveDir(), seed,
                       LevelStorage::getActiveGameType(), LevelStorage::getActiveName(), false);
    return true;
}
