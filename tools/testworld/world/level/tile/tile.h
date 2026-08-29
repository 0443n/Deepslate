#ifndef MCPSP_WORLD_TILE_TILE_H
#define MCPSP_WORLD_TILE_TILE_H

// Stub standing in for the real tile.h while the feature sources are compiled
// against the test world. chunk.h needs Tile to parse, but nothing levelgen
// calls reaches these fields.
struct Tile {
    bool replaceable, solidPhys, cube, opaque;
    int  lightBlock, lightEmission;
    static Tile* tiles[256];
};

#endif
