#ifndef MCPSP_WORLD_LEVEL_LEVELGEN_NETHER_GEN_H
#define MCPSP_WORLD_LEVEL_LEVELGEN_NETHER_GEN_H

struct World;

void netherGenInit(long seed);
void netherGenFree();
void netherGenerateChunk(World* w, int cx, int cz);

#endif
