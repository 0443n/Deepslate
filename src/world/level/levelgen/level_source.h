
#ifndef MCPSP_WORLD_LEVEL_LEVELGEN_LEVEL_SOURCE_H
#define MCPSP_WORLD_LEVEL_LEVELGEN_LEVEL_SOURCE_H

struct World;

// The nether sits past the count on purpose, it is reached through a portal and
// never offered as a choice when creating a world.
enum { WORLD_TYPE_OLD = 0, WORLD_TYPE_FLAT = 1, WORLD_TYPE_COUNT = 2,
       WORLD_TYPE_NETHER = 2 };

class LevelSource {
public:
    virtual ~LevelSource() {}

    virtual void buildTerrain(World* w, long seed) = 0;

    virtual void buildChunk(World* w, int cx, int cz) = 0;

    virtual bool spawnsMobs() const { return true; }

    virtual bool supportsGenFeatures() const { return true; }

    virtual bool hasBedrockFog() const { return true; }
    virtual float clearColorScale() const { return 1.0f / 32.0f; }

    virtual int forcedGameType() const { return -1; }

    // Non-zero pins sky, fog and clouds to one ABGR colour and drops the day cycle.
    virtual unsigned int fixedSkyColor() const { return 0; }

    virtual const char* label() const = 0;
};

LevelSource& levelSourceFor(int worldType);

LevelSource& activeLevelSource();

#endif
