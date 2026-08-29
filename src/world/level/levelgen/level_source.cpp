#include "world/level/levelgen/level_source.h"
#include "world/level/world.h"
#include "world/level/chunk/chunk.h"
#include "world/level/levelgen/mcpegen.h"
#include "world/level/levelgen/nether_gen.h"
#include "world/level/levelgen/gen_features.h"
#include "world/level/storage/level_storage.h"

#include <cstring>

namespace {

class RandomLevelSource : public LevelSource {
public:
    void buildTerrain(World* w, long seed) {

        worldGenerateMCPE(w, seed, LevelStorage::getActiveGenMask());
        worldSettleLiquids(w);
    }
    void buildChunk(World* w, int cx, int cz) { chunkGenerateTerrain(w, cx, cz); }
    const char* label() const { return "Old"; }
};

class FlatLevelSource : public LevelSource {
public:

    void buildTerrain(World* w, long ) { worldGenerateWindow(w); }

    void buildChunk(World* w, int cx, int cz) {

        unsigned char col[WORLD_H];
        std::memset(col, BLOCK_AIR, sizeof(col));
        col[0] = BLOCK_BEDROCK;
        col[1] = BLOCK_DIRT;
        col[2] = BLOCK_DIRT;
        col[3] = BLOCK_GRASS;

        for (int gz = cz * CHUNK_SZ; gz < cz * CHUNK_SZ + CHUNK_SZ; gz++)
            for (int gx = cx * CHUNK_SX; gx < cx * CHUNK_SX + CHUNK_SX; gx++)
                blockColumnPut(w, gx, gz, col);
    }

    bool spawnsMobs() const { return false; }

    bool supportsGenFeatures() const { return false; }

    bool hasBedrockFog() const { return false; }
    float clearColorScale() const { return 1.0f; }

    int forcedGameType() const { return 1; }
    const char* label() const { return "Flat"; }
};

class NetherLevelSource : public LevelSource {
public:
    void buildTerrain(World* w, long seed) {
        netherGenInit(seed);
        worldGenerateWindow(w);
    }
    void buildChunk(World* w, int cx, int cz) { netherGenerateChunk(w, cx, cz); }

    // The overworld cave and feature passes have nothing to add under a bedrock roof.
    bool supportsGenFeatures() const { return false; }
    bool spawnsMobs() const { return false; }

    bool hasBedrockFog() const { return false; }
    float clearColorScale() const { return 1.0f; }
    unsigned int fixedSkyColor() const { return 0xFF0A0C25u; }
    const char* label() const { return "Nether"; }
};

RandomLevelSource s_random;
FlatLevelSource   s_flat;
NetherLevelSource s_nether;

}

LevelSource& levelSourceFor(int worldType) {
    if (worldType == WORLD_TYPE_NETHER) return (LevelSource&)s_nether;
    return (worldType == WORLD_TYPE_FLAT) ? (LevelSource&)s_flat : (LevelSource&)s_random;
}

LevelSource& activeLevelSource() {
    if (LevelStorage::getActiveDim() == DIM_NETHER) return (LevelSource&)s_nether;
    return levelSourceFor(LevelStorage::getActiveWorldType());
}
