#ifndef MCPSP_WORLD_LEVEL_TILE_NETHER_PORTAL_H
#define MCPSP_WORLD_LEVEL_TILE_NETHER_PORTAL_H

struct World;

namespace NetherPortal {

// Lights the frame enclosing the empty block a flint and steel was used against.
bool light(World* w, int x, int y, int z);

// Cheap per-block frame test, the four in-plane neighbours must be portal or obsidian.
bool framed(const World* w, int x, int y, int z);

// Clears the whole connected portal plane, used once its frame is broken.
void extinguish(World* w, int x, int y, int z);

// Nearest existing portal to a point, or a freshly built one when there is none.
// Always succeeds, the fallback carves its own pocket. Returns the block to stand in.
void findOrCreate(World* w, int x, int z, int* outX, int* outY, int* outZ);

}

#endif
