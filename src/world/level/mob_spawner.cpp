// The spawner itself is Rust now, see rust/src/spawner.rs. All that is left on
// this side is the level source gate, since whether a world spawns mobs at all
// is a property of the generator that made it.
//
// mob_spawner.cpp's old logic is gone rather than kept as a parity reference,
// because it read Level and the entity list directly and could never have been
// compiled on the host. rust/tests/spawner.rs covers it instead.

#include "world/level/mob_spawner.h"
#include "world/level/level.h"
#include "world/level/levelgen/level_source.h"
#include "rs/rs.h"

namespace MobSpawner {

void populateInitial(Level* level) {
    if (!activeLevelSource().spawnsMobs()) return;
    ds_spawn_populate(level, (int)worldSeed());
}

// Modern Minecraft spawns mobs in creative too. MCPE 0.6.1 gated it off, which
// left a creative world permanently empty.
void tick(Level* level, bool spawnEnemies, bool spawnFriendlies) {
    if (!activeLevelSource().spawnsMobs()) return;
    ds_spawn_run(level, spawnEnemies ? 1 : 0, spawnFriendlies ? 1 : 0);
}

}
