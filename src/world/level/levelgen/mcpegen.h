#ifndef MCPEGEN_H__
#define MCPEGEN_H__

struct World;

void worldGenerateMCPE(World* w, long seed, int genMask);

void worldPlaceMushrooms(World* w);

void worldPlaceFlowers(World* w);

#endif
