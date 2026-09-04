
#ifndef MCPSP_WORLD_CHUNK_CACHE_H
#define MCPSP_WORLD_CHUNK_CACHE_H

struct World;

void worldGetChunk(World* w, int cx, int cz);

void worldEnsureArea(World* w, int cx, int cz, int r);

int worldStream(World* w, float px, float pz, int budgetMs);

bool worldStreamBusy();

// Chunk radius the window can actually feed, which caps usable view distance.
int worldLoadRadius(const World* w);

void worldSaveResident(World* w);

extern unsigned int g_streamIn, g_streamOut;

#endif
