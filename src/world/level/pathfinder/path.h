
#ifndef MCPSP_WORLD_LEVEL_PATHFINDER_PATH_H
#define MCPSP_WORLD_LEVEL_PATHFINDER_PATH_H

#include "world/level/pathfinder/vec3.h"

class Entity;

class Path {
public:
    Path();

    void  copyPoints(const short* xyz, int length);
    void  destroy();
    void  next();

    int   getSize() const;
    bool  isEmpty() const;
    bool  isDone() const;

    int   getIndex() const;
    void  setIndex(int index);

    Vec3  currentPos(Entity* e) const;
    Vec3  getPos(Entity* e, int index) const;

    void  getPoint(int index, int& x, int& y, int& z) const;
    void  lastPoint(int& x, int& y, int& z) const;

private:

    // NOTE: only the position survives the search, the g/h/f and cameFrom
    // bookkeeping stays behind in PathFinder::_nodes.
    struct Point { short x, y, z; };

    static const int MAX_PATH = 64;
    Point points[MAX_PATH];
    short length;
    short index;
};

#endif
