// The two block queries the pathfinder makes, over a flat array world.
#ifndef PATHTEST_LEVEL_H
#define PATHTEST_LEVEL_H

extern int  testGetTile(int x, int y, int z);
extern int  testGetData(int x, int y, int z);

class Level {
public:
    int getTile(int x, int y, int z) const { return testGetTile(x, y, z); }
    int getData(int x, int y, int z) const { return testGetData(x, y, z); }
};

#endif
