#ifndef MCPSP_WORLD_LEVEL_PATHFINDER_PATH_BRIDGE_H
#define MCPSP_WORLD_LEVEL_PATHFINDER_PATH_BRIDGE_H

struct World;

// Hands the Rust side the block property table. Called once, after the tiles
// are registered and before any mob ticks.
void pathFinderInit();
void pathFinderFree();

#endif
