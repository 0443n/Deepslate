// Records what every levelgen feature does to a world, as the exact ordered
// trace of block writes and scheduled ticks it produces from a known seed.
// rust/tests/feature_parity.rs replays the same script against the Rust port.
//
// Built with tools/testworld first on the include path, so the feature sources
// below compile unmodified against the flat test world instead of the real
// chunk store. See tools/gen-vectors.sh.

#include "world/level/world.h"
#include "world/level/levelgen/features.h"
#include "world/level/levelgen/biome.h"
#include "world/level/levelgen/caves.h"
#include "world/level/levelgen/gen_features.h"
#include "world/level/levelgen/mcpegen.h"
#include "world/level/levelgen/nether_gen.h"
#include "world/level/levelgen/Random.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

Tile* Tile::tiles[256];

// Pulled in by worldGenerateWindow and worldGenerateMCPE, which the harness
// never calls but the linker still wants.
volatile int g_terrainProgress;
void worldGetChunk(World*, int, int) {}

// Off for the whole generator run below, which writes far too much to trace.
static bool g_trace = true;

void traceSet(int x, int y, int z, unsigned char id, unsigned char data) {
    if (g_trace) printf("S %d %d %d %u %u\n", x, y, z, (unsigned)id, (unsigned)data);
}

void traceTick(int x, int y, int z, unsigned char id, int tickDelay) {
    if (g_trace) printf("T %d %d %d %u %d\n", x, y, z, (unsigned)id, tickDelay);
}

// Ground that gives every feature something to work with. Ridged height with a
// grass half and a dry sand half, standing water on a fixed lattice, and a
// roofed cavern band, so trees, cactus, reeds, clay, springs and the shade
// loving mushrooms all find their preconditions somewhere.
static void buildTerrain(World* w) {
    std::memset(w->id, BLOCK_AIR, sizeof w->id);
    std::memset(w->data, 0, sizeof w->data);

    const int SEA = 63;
    for (int x = 0; x < WORLD_W; x++)
    for (int z = 0; z < WORLD_D; z++) {
        int h = 58 + (x * 7 + z * 13) % 11;
        bool basin  = ((x >> 4) + (z >> 4)) % 5 == 0;
        bool desert = x >= WORLD_W / 2 && !basin;
        if (basin) h -= 6;

        w->id[x][0][z] = BLOCK_BEDROCK;
        for (int y = 1; y <= h - 4; y++) w->id[x][y][z] = BLOCK_STONE;
        for (int y = h - 3; y < h; y++)  w->id[x][y][z] = desert ? BLOCK_SAND : BLOCK_DIRT;
        w->id[x][h][z] = (basin || desert) ? BLOCK_SAND : BLOCK_GRASS;
        if (basin)
            for (int y = h + 1; y <= SEA; y++) w->id[x][y][z] = BLOCK_CALM_WATER;

        // A cavern band under a stone roof, the only dark air on the map.
        if ((x / 8 + z / 8) % 3 == 0)
            for (int y = 30; y <= 33; y++) w->id[x][y][z] = BLOCK_AIR;
    }
}

// FNV-1a over a chunk's block ids, the compact stand in for dumping terrain.
static unsigned int chunkHash(const World* w, int cx, int cz) {
    unsigned int h = 2166136261u;
    for (int x = cx * 16; x < cx * 16 + 16; x++)
    for (int z = cz * 16; z < cz * 16 + 16; z++)
    for (int y = 0; y < WORLD_H; y++) {
        h ^= w->id[x][y][z];
        h *= 16777619u;
        h ^= w->data[x][y][z];
        h *= 16777619u;
    }
    return h;
}

static void hashAll(const World* w, const char* tag) {
    for (int cz = 0; cz < WORLD_CHUNKS_Z; cz++)
    for (int cx = 0; cx < WORLD_CHUNKS_X; cx++)
        printf("H %s %d %d %08x\n", tag, cx, cz, chunkHash(w, cx, cz));
}

static int surfaceAt(const World* w, int x, int z) {
    return heightmapAt((World*)w, x, z);
}

int main() {
    World* w = (World*)std::malloc(sizeof(World));

    // One seed per feature so a failure in one does not shift the others.
    long seed = 20260829;

    #define FEATURE(name) do { printf("F %s\n", name); buildTerrain(w); } while (0)
    #define CHECKPOINT(r) printf("R %d\n", (r).nextInt(0x40000000))

    {
        FEATURE("classify_biome");
        for (int i = 0; i < 64; i++)
        for (int j = 0; j < 64; j++) {
            float t = i / 63.0f, d = j / 63.0f;
            unsigned char top, mat;
            BiomeId b = classifyBiome(t, d);
            biomeSurface(b, &top, &mat);
            printf("B %d %u %u\n", (int)b, (unsigned)top, (unsigned)mat);
        }
    }

    {
        FEATURE("tree_oak");
        Random r(seed + 1);
        for (int i = 0; i < 40; i++) {
            int x = 20 + i * 5, z = 30 + i * 3;
            treeOak(w, r, x, surfaceAt(w, x, z), z);
        }
        CHECKPOINT(r);
    }

    {
        FEATURE("tree_birch");
        Random r(seed + 2);
        for (int i = 0; i < 40; i++) {
            int x = 22 + i * 5, z = 40 + i * 3;
            treeBirch(w, r, x, surfaceAt(w, x, z), z);
        }
        CHECKPOINT(r);
    }

    {
        FEATURE("tree_spruce");
        Random r(seed + 3);
        for (int i = 0; i < 40; i++) {
            int x = 24 + i * 5, z = 50 + i * 3;
            treeSpruce(w, r, x, surfaceAt(w, x, z), z);
        }
        CHECKPOINT(r);
    }

    {
        FEATURE("tree_pine");
        Random r(seed + 4);
        for (int i = 0; i < 40; i++) {
            int x = 26 + i * 5, z = 60 + i * 3;
            treePine(w, r, x, surfaceAt(w, x, z), z);
        }
        CHECKPOINT(r);
    }

    {
        FEATURE("cactus");
        Random r(seed + 5);
        for (int i = 0; i < 500; i++) {
            int x = WORLD_W / 2 + (i * 13) % 120, z = (i * 7) % 200;
            cactusFeature(w, r, x, surfaceAt(w, x, z), z);
        }
        CHECKPOINT(r);
    }

    {
        FEATURE("reeds");
        Random r(seed + 6);
        // Walk the basin rims, the only place water sits beside dry ground.
        for (int i = 0; i < 900; i++) {
            int x = 2 + (i * 3) % 200, z = 2 + (i * 11) % 200;
            reedsFeature(w, r, x, surfaceAt(w, x, z), z);
        }
        CHECKPOINT(r);
    }

    {
        FEATURE("ore");
        Random r(seed + 7);
        static const unsigned char tiles[] = {
            BLOCK_ORE_COAL, BLOCK_ORE_IRON, BLOCK_ORE_GOLD, BLOCK_GRAVEL, BLOCK_DIRT
        };
        for (int i = 0; i < 50; i++)
            oreFeature(w, r, (i * 11) % 200, 8 + (i * 3) % 40, (i * 17) % 200,
                       tiles[i % 5], 4 + (i % 6) * 6);
        CHECKPOINT(r);
    }

    {
        FEATURE("clay");
        Random r(seed + 8);
        // Clay only starts inside standing water, so aim at sea level.
        for (int i = 0; i < 200; i++) {
            int x = (i * 13) % 200, z = (i * 7) % 200;
            clayFeature(w, r, x, 63, z);
        }
        CHECKPOINT(r);
    }

    {
        FEATURE("spring");
        buildTerrain(w);
        // Carve pockets so the three-rock one-hole test actually fires.
        for (int i = 0; i < 400; i++) {
            int x = 3 + (i * 29) % 200, y = 20 + (i * 11) % 25, z = 3 + (i * 41) % 200;
            w->id[x][y][z] = BLOCK_AIR;
            w->id[x + 1][y][z] = BLOCK_AIR;
        }
        for (int i = 0; i < 400; i++) {
            int x = 3 + (i * 29) % 200, y = 20 + (i * 11) % 25, z = 3 + (i * 41) % 200;
            springFeature(w, x, y, z, (i & 1) ? BLOCK_WATER : BLOCK_LAVA);
        }
    }

    {
        FEATURE("lake");
        Random r(seed + 9);
        for (int i = 0; i < 30; i++) {
            int x = 20 + (i * 23) % 180, z = 20 + (i * 31) % 180;
            lakeFeature(w, r, x, 70, z, (i & 1) ? BLOCK_WATER : BLOCK_LAVA);
        }
        CHECKPOINT(r);
    }

    {
        FEATURE("snow");
        float mTemp[16 * 16];
        for (int i = 0; i < 16 * 16; i++) mTemp[i] = (i % 32) / 40.0f;
        for (int cx = 0; cx < 8; cx++)
        for (int cz = 0; cz < 8; cz++)
            snowCap(w, cx, cz, mTemp);
    }

    {
        FEATURE("flower");
        Random r(seed + 10);
        for (int i = 0; i < 50; i++) {
            int x = 15 + (i * 19) % 180, z = 15 + (i * 29) % 180;
            flowerFeature(w, r, x, surfaceAt(w, x, z), z, (i & 1) ? BLOCK_FLOWER : BLOCK_ROSE);
        }
        printf("P place\n");
        worldPlaceFlowers(w);
        CHECKPOINT(r);
    }

    {
        FEATURE("mushroom");
        Random r(seed + 11);
        // Inside the cavern band, where nothing can see the sky.
        for (int i = 0; i < 120; i++) {
            int x = 17 + (i * 23) % 180, z = 17 + (i * 37) % 180;
            mushroomFeature(w, r, x, 32, z,
                            (i & 1) ? BLOCK_MUSHROOM_BROWN : BLOCK_MUSHROOM_RED);
        }
        printf("P place\n");
        worldPlaceMushrooms(w);
        CHECKPOINT(r);
    }

    {
        FEATURE("caves");
        for (int cx = 2; cx < 8; cx++)
        for (int cz = 2; cz < 8; cz++)
            caveFeature(w, 20260829, cx, cz);
    }

    {
        // The whole generator over a whole world. Block writes are far too many
        // to trace, so each pass reports an FNV-1a per chunk over ids and data
        // instead. Two seeds, because no single one grows every biome, 1337 has
        // the forests, 110 is the only desert with standing cactus, 129 is frozen.
        FEATURE("mcpegen");
        g_trace = false;
        static const int seeds[] = { 1337, 110, 129 };
        for (unsigned si = 0; si < sizeof seeds / sizeof *seeds; si++) {
            printf("G seed %d\n", seeds[si]);
            std::memset(w->id, BLOCK_AIR, sizeof w->id);
            std::memset(w->data, 0, sizeof w->data);
            worldGenInit(seeds[si], GEN_FEATURES_ALL_ON);
            for (int cz = 0; cz < WORLD_CHUNKS_Z; cz++)
            for (int cx = 0; cx < WORLD_CHUNKS_X; cx++)
                chunkGenerateTerrain(w, cx, cz);
            hashAll(w, "terrain");
            for (int phase = 0; phase < 6; phase++) {
                for (int cz = 0; cz < WORLD_CHUNKS_Z; cz++)
                for (int cx = 0; cx < WORLD_CHUNKS_X; cx++)
                    chunkPostProcessPhase(w, cx, cz, phase);
                char tag[16];
                std::snprintf(tag, sizeof tag, "phase%d", phase);
                hashAll(w, tag);
            }
            worldPlaceFlowers(w);
            worldPlaceMushrooms(w);
            hashAll(w, "placed");
            worldGenFree();
        }
        g_trace = true;
    }

    {
        // The nether, which writes through blockPut and so traces nothing
        // either. One seed is enough, it has no biomes to miss.
        FEATURE("nether");
        g_trace = false;
        std::memset(w->id, BLOCK_AIR, sizeof w->id);
        std::memset(w->data, 0, sizeof w->data);
        netherGenInit(20260829);
        for (int cz = 0; cz < WORLD_CHUNKS_Z; cz++)
        for (int cx = 0; cx < WORLD_CHUNKS_X; cx++)
            netherGenerateChunk(w, cx, cz);
        hashAll(w, "nether");
        netherGenFree();
        g_trace = true;
    }

    printf("DONE\n");
    std::free(w);
    return 0;
}
