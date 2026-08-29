#ifndef MCPSP_WORLD_LEVEL_DIMENSION_H
#define MCPSP_WORLD_LEVEL_DIMENSION_H

struct World;

// Target dimension a portal has asked for, or -1 when nothing is pending.
extern int  g_dimSwapTarget;
// True from the moment the old world is torn down until the new one is meshed.
extern bool g_dimSwapping;

void dimensionRequest(int target);

// Saves what is being left and frees the world, keeping the player. Game thread.
void dimensionSwapTearDown(World* w);

// Loads or generates what is being entered and stands the player in its portal.
// Runs on the terrain thread, like the first build of a world does.
bool dimensionSwapBuild(World* w);

#endif
