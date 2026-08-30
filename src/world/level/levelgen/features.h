#ifndef FEATURES_H__
#define FEATURES_H__

// What the Rust port in rust/src/ does not cover, the shared helpers and the
// three saplings tile_bush.cpp grows with bonemeal.

struct World;
class Random;

void setBlock(World* w, int x, int y, int z, unsigned char id, unsigned char data = 0);
bool isSolidGen(unsigned char id);
int heightmapAt(World* w, int x, int z);

bool isTreeClear(unsigned char b);

bool treeSpaceClear(World* w, int x, int y, int z, int treeHeight,
                    int (*radiusAt)(int layer, int treeHeight, int arg), int arg);

void treeBasic(World* w, Random& random, int x, int y, int z,
               int minHeight, unsigned char leafData, unsigned char logData);

void treeOak(World* w, Random& random, int x, int y, int z);
void treeBirch(World* w, Random& random, int x, int y, int z);
void treeSpruce(World* w, Random& random, int x, int y, int z);

#endif
