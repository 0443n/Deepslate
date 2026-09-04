// The pathfinder and its only caller, the mob AI, both live in Rust now. All
// that is left on this side is handing Rust the block property table, since
// Tile::tiles is where it is defined.
//
// path_finder.cpp and path.cpp are no longer built. They stay in the tree as the
// reference tools/gen-path-vectors.cpp checks the Rust against.

#include "world/level/pathfinder/path_bridge.h"
#include "world/entity/entity.h"
#include "world/level/chunk/chunk.h"
#include "world/level/tile/tile.h"
#include "rs/rs.h"

static bool s_ready = false;

void pathFinderInit() {
    unsigned char flags[256];
    for (int id = 0; id < 256; id++) {
        unsigned char b = (unsigned char)id;
        unsigned char f = 0;
        // Tile::tiles is the single source of truth, so the Rust side never
        // needs a block table of its own.
        if (Tile::tiles[id] && isSolidPhys(b)) f |= DS_BLOCK_SOLID;
        if (isWaterId(b))                      f |= DS_BLOCK_WATER;
        if (isLavaId(b))                       f |= DS_BLOCK_LAVA;
        if (isFence(b) || isFenceGate(b))      f |= DS_BLOCK_FENCE;
        if (isDoor(b))                         f |= DS_BLOCK_DOOR;
        flags[id] = f;
    }
    ds_path_init(flags);
    s_ready = true;
}

void pathFinderFree() {
    ds_path_free();
    s_ready = false;
}
