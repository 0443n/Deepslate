// Just enough Entity for src/world/level/pathfinder to compile on the host.
// See tools/gen-path-vectors.cpp.
#ifndef PATHTEST_ENTITY_H
#define PATHTEST_ENTITY_H

#include "world/phys/aabb.h"

class Entity {
public:
    Entity() : x(0), y(0), z(0), bbWidth(0.6f), bbHeight(1.8f), inWater(false) {}
    bool isInWater() const { return inWater; }

    float x, y, z;
    AABB  bb;
    float bbWidth, bbHeight;
    bool  inWater;
};

#endif
