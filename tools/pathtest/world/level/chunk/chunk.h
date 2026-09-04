// The block predicates the pathfinder consults, with the same ids the game uses.
#ifndef PATHTEST_CHUNK_H
#define PATHTEST_CHUNK_H

enum {
    BLOCK_AIR = 0, BLOCK_STONE = 1, BLOCK_GRASS = 2, BLOCK_DIRT = 3,
    BLOCK_WATER = 8, BLOCK_CALM_WATER = 9, BLOCK_LAVA = 10, BLOCK_CALM_LAVA = 11,
    BLOCK_FENCE = 85, BLOCK_DOOR_WOOD = 64, BLOCK_DOOR_IRON = 71,
    BLOCK_FENCE_GATE = 107
};

static inline bool isFence(unsigned char id)     { return id == BLOCK_FENCE; }
static inline bool isFenceGate(unsigned char id) { return id == BLOCK_FENCE_GATE; }
static inline bool isDoor(unsigned char id)      { return id == BLOCK_DOOR_WOOD || id == BLOCK_DOOR_IRON; }
static inline bool isWaterId(unsigned char id)   { return id == BLOCK_WATER || id == BLOCK_CALM_WATER; }
static inline bool isLavaId(unsigned char id)    { return id == BLOCK_LAVA  || id == BLOCK_CALM_LAVA; }
static inline bool isSolidPhys(unsigned char id) {
    return id == BLOCK_STONE || id == BLOCK_GRASS || id == BLOCK_DIRT || id == BLOCK_FENCE;
}

#endif
