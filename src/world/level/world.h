
#ifndef MCPSP_WORLD_WORLD_H
#define MCPSP_WORLD_WORLD_H

#include "world/level/chunk/chunk.h"
#include <stdlib.h>
#include <vector>
#include <unordered_set>
#include <unordered_map>

struct TickNextTickData {
    int x, y, z;
    unsigned char tileId;
    long delay;
    bool operator==(const TickNextTickData& t) const {
        return x == t.x && y == t.y && z == t.z && tileId == t.tileId;
    }
};

#define WORLD_CHUNKS_X 16
#define WORLD_CHUNKS_Z 16
#define WORLD_W (WORLD_CHUNKS_X * CHUNK_SX)
#define WORLD_H CHUNK_SY
#define WORLD_D (WORLD_CHUNKS_Z * CHUNK_SZ)

#define WORLD_VIEW_DIST 64.0f

#define LP_PAGE       128
#define LP_BLK_PAGES  256
#define LP_MAX_BLKS   256
#define LP_ALL0   0xFFFEu
#define LP_ALL15  0xFFFFu
#define LP_SENT   0xFFFEu

#define BS_SECTIONS   (WORLD_CHUNKS_X * WORLD_CHUNKS_Z * N_SECTIONS)
#define BS_CELLS      (16 * 16 * SECTION_SY)
#define BS_PAL_PAGE   (BS_CELLS / 2)
#define BS_PAL_MAX    16

struct BlockSection {
    unsigned char* page;
    unsigned char  pal[BS_PAL_MAX];
    unsigned char  palN;
    unsigned char  uniform;
};

struct World {

    BlockSection bsec[BS_SECTIONS];
    int blockPages;
    unsigned int blockPageBytes;

    unsigned char** dataCol;
    int dataPages;

    unsigned short* lightIdx;
    unsigned char*  lightPool[LP_MAX_BLKS];
    int             lightBlocksUsed;
    int             lightPagesUsed;
    int             lightPagesAlloced;
    unsigned short  lightFreeHead;
    unsigned int    lightOomDrops;
    unsigned char* heightmap;
    ChunkMesh chunks[WORLD_CHUNKS_X * WORLD_CHUNKS_Z];

    bool unsaved[WORLD_CHUNKS_X * WORLD_CHUNKS_Z];

    long time;

    long dayTime;
    std::vector<TickNextTickData> tickNextTickList;

    std::unordered_set<unsigned int> tickSet;

    std::vector<unsigned int> lightQueue;
    bool lightReady;

    std::vector<std::vector<unsigned char> > preservedTileEntities;
};

extern volatile int g_terrainProgress;
extern volatile bool g_terrainThreadDone;

bool worldInitTerrain(World* w, long seed);

bool worldAllocArrays(World* w);

int worldBuildMeshesStep(World* w, int maxChunks);

void worldFindSpawn(World* w, int* outX, int* outZ, int* outFeetY);

void worldFree(World* w);

static inline int worldIndex(int x, int y, int z) {
    return (x * WORLD_D + z) * WORLD_H + y;
}

static inline int bsSection(int x, int y, int z) {
    return (((x >> 4) * WORLD_CHUNKS_Z + (z >> 4)) * N_SECTIONS) + (y >> 4);
}
static inline int bsOffset(int x, int y, int z) {
    return (((x & 15) * 16 + (z & 15)) * SECTION_SY) + (y & (SECTION_SY - 1));
}
static inline unsigned char worldBlock(const World* w, int x, int y, int z) {
    if (y < 0 || y >= WORLD_H) return BLOCK_AIR;
    if (x < 0 || x >= WORLD_W || z < 0 || z >= WORLD_D)
        return BLOCK_INVISIBLE_BEDROCK;
    const BlockSection* s = &w->bsec[bsSection(x, y, z)];
    if (!s->page) return s->uniform;
    int off = bsOffset(x, y, z);
    if (!s->palN) return s->page[off];
    return s->pal[(s->page[off >> 1] >> ((off & 1) << 2)) & 0x0F];
}

void blockAlloc(World* w);
void blockFree(World* w);

bool blockPut(World* w, int x, int y, int z, unsigned char id);

void blockColumnGet(const World* w, int x, int z, unsigned char* out128);
void blockColumnPut(World* w, int x, int z, const unsigned char* in128);
unsigned int blockBytes(const World* w);

void blockStats(const World* w, int* uniform, int* paletted, int* raw);

extern unsigned int g_blockOomDrops;

static inline int worldColumn(int x, int z) { return x * WORLD_D + z; }

#define WORLD_DATA_PAGE (WORLD_H / 2)

static inline unsigned int worldDataBytes(const World* w) {
    return (unsigned int)(WORLD_W * WORLD_D) * sizeof(unsigned char*)
         + (unsigned int)w->dataPages * WORLD_DATA_PAGE;
}
unsigned int lightBytes(const World* w);
static inline unsigned int worldMemBytes(const World* w) {
    return blockBytes(w)
         + (unsigned int)(WORLD_W * WORLD_D)
         + worldDataBytes(w)
         + lightBytes(w);
}

static inline unsigned char worldData(const World* w, int x, int y, int z) {
    if (y < 0 || y >= WORLD_H || x < 0 || x >= WORLD_W || z < 0 || z >= WORLD_D) return 0;
    const unsigned char* pg = w->dataCol[worldColumn(x, z)];
    if (!pg) return 0;
    return (y & 1) ? (unsigned char)(pg[y >> 1] >> 4)
                   : (unsigned char)(pg[y >> 1] & 0x0F);
}

static inline void worldDataPut(World* w, int i, unsigned char v) {
    unsigned char* pg = w->dataCol[i / WORLD_H];
    if (!pg) {

        if (!(v & 0x0F)) return;
        pg = (unsigned char*)calloc(1, WORLD_DATA_PAGE);
        if (!pg) return;
        w->dataPages++;

        w->dataCol[i / WORLD_H] = pg;
    }
    int y = i % WORLD_H;
    unsigned char& b = pg[y >> 1];
    b = (y & 1) ? (unsigned char)((b & 0x0F) | (unsigned char)((v & 0x0F) << 4))
                : (unsigned char)((b & 0xF0) | (v & 0x0F));
}

bool worldSetBlockAndData(World* w, int x, int y, int z, unsigned char id, unsigned char data);
void worldSetData(World* w, int x, int y, int z, unsigned char data);

void worldSetDataNoUpdate(World* w, int x, int y, int z, unsigned char data);

void worldMarkDirty(World* w, int x, int y, int z);

void worldDrainPlayerEdits(World* w, int maxSections);

void worldRebuildAroundNow(World* w, int x, int y, int z);

void lightOnBlockChanged(World* w, int x, int y, int z);

static inline int lightPlaneIdx(int layer, int x, int y, int z) {
    return ((((((x >> 4) << 4) | (z >> 4)) << 7) | y) << 1) | layer;
}
static inline unsigned char* lightPage(const World* w, unsigned int id) {
    return w->lightPool[id >> 8] + ((id & (LP_BLK_PAGES - 1)) << 7);
}

static inline int lightPi(int x, int z) { return ((x & 15) << 4) | (z & 15); }

unsigned int lightPagePromote(World* w, int idxSlot, unsigned char prefill);

static inline int lightLayerGet(const World* w, int layer, int x, int y, int z) {
    unsigned int id = w->lightIdx[lightPlaneIdx(layer, x, y, z)];
    if (id >= LP_SENT) return (int)(id & 1) * 15;
    int pi = lightPi(x, z);
    return (lightPage(w, id)[pi >> 1] >> ((pi & 1) * 4)) & 15;
}
static inline void lightLayerSet(World* w, int layer, int x, int y, int z, int v) {
    int slot = lightPlaneIdx(layer, x, y, z);
    unsigned int id = w->lightIdx[slot];
    if (id >= LP_SENT) {
        if (v == (int)(id & 1) * 15) return;
        id = lightPagePromote(w, slot, (unsigned char)((id & 1) ? 0xFF : 0x00));
        if (id >= LP_SENT) return;
    }
    unsigned char* p = lightPage(w, id);
    int pi = lightPi(x, z);
    unsigned char& b = p[pi >> 1];
    b = (pi & 1) ? (unsigned char)((b & 0x0F) | (v << 4))
                 : (unsigned char)((b & 0xF0) | v);
}

static inline bool lightPlaneAllDark(const World* w, int layer, int x, int y, int z) {
    return w->lightIdx[lightPlaneIdx(layer, x, y, z)] == LP_ALL0;
}

static inline int lightSkyGet(const World* w, int x, int y, int z) {
    if (y >= WORLD_H) return 15;
    if (y < 0 || x < 0 || x >= WORLD_W || z < 0 || z >= WORLD_D) return 0;
    return lightLayerGet(w, 0, x, y, z);
}
static inline int lightBlockGet(const World* w, int x, int y, int z) {
    if (y < 0 || y >= WORLD_H || x < 0 || x >= WORLD_W || z < 0 || z >= WORLD_D) return 0;
    return lightLayerGet(w, 1, x, y, z);
}
static inline void lightSkySet(World* w, int x, int y, int z, int v) {
    if (y < 0 || y >= WORLD_H || x < 0 || x >= WORLD_W || z < 0 || z >= WORLD_D) return;
    lightLayerSet(w, 0, x, y, z, v);
}
static inline void lightBlockSet(World* w, int x, int y, int z, int v) {
    if (y < 0 || y >= WORLD_H || x < 0 || x >= WORLD_W || z < 0 || z >= WORLD_D) return;
    lightLayerSet(w, 1, x, y, z, v);
}

#define TICKS_PER_DAY 19200

extern int g_skyDarken;

float worldTimeOfDay(long dayTime, float a);

void worldSetNightMode(World* w, bool night);

bool worldNightModeTick(World* w);

void worldUpdateSkyDarken(World* w);

static inline bool worldIsDay() { return g_skyDarken < 4; }

static inline int lightRawAtNoProp(const World* w, int x, int y, int z) {

    if (y >= WORLD_H + 8) return 15;

    if (y >= WORLD_H) { int s = 15 - g_skyDarken; return s < 0 ? 0 : s; }
    if (y < 0 || x < 0 || x >= WORLD_W || z < 0 || z >= WORLD_D) return 0;

    int s = lightLayerGet(w, 0, x, y, z) - g_skyDarken, b = lightLayerGet(w, 1, x, y, z);
    if (s < 0) s = 0;
    return s > b ? s : b;
}

static inline int lightRawAt(const World* w, int x, int y, int z) {
    unsigned char id = worldBlock(w, x, y, z);
    if (id == BLOCK_SLAB || id == BLOCK_FARMLAND) {
        int br = lightRawAtNoProp(w, x, y + 1, z);
        int b1 = lightRawAtNoProp(w, x + 1, y, z); if (b1 > br) br = b1;
        int b2 = lightRawAtNoProp(w, x - 1, y, z); if (b2 > br) br = b2;
        int b3 = lightRawAtNoProp(w, x, y, z + 1); if (b3 > br) br = b3;
        int b4 = lightRawAtNoProp(w, x, y, z - 1); if (b4 > br) br = b4;
        return br;
    }
    return lightRawAtNoProp(w, x, y, z);
}

static inline int lightLazy(const World* w, unsigned char* cache, int i, int x, int y, int z) {
    int v = cache[i];
    if (v != 0xFF) return v;
    v = lightRawAt(w, x, y, z);
    cache[i] = (unsigned char)v;
    return v;
}

static inline bool worldCanSeeSky(const World* w, int x, int y, int z) {
    if (y >= WORLD_H) return true;
    if (y < 0 || x < 0 || x >= WORLD_W || z < 0 || z >= WORLD_D) return false;
    return y >= w->heightmap[x * WORLD_D + z];
}

void worldInitLight(World* w);
void worldRecalcHeightmap(World* w);
void worldUpdateLights(World* w);

bool worldSettleLights(World* w);
void worldRemoveBlockLight(World* w, int x, int y, int z);

void lightQueuesReserve(World* w);

bool         lightAlloc(World* w);
void         lightFree(World* w);
void         lightClearAll(World* w);
unsigned int lightBytes(const World* w);

void         lightCompactStep(World* w);
void         lightCompactAll(World* w);

void         lightInitSkyFromHeightmap(World* w);

void         lightLoadChunk(World* w, int cx, int cz,
                            const unsigned char* skyNib, const unsigned char* blockNib);

void worldScheduleTick(World* w, int x, int y, int z, unsigned char id, int tickDelay);
void worldTick(World* w);
void worldSettleLiquids(World* w);
void worldScheduleLoadedLiquids(World* w);
void worldUpdateNeighbors(World* w, int x, int y, int z, unsigned char id);

void liquidFlow(const World* w, int x, int y, int z, unsigned char id,
                float* fx, float* fy, float* fz);

void worldNotifyNeighborsChanged(World* w, int x, int y, int z);

void worldExplode(World* w, float x, float y, float z, float r);

void worldPrimeTnt(World* w, int x, int y, int z, int fuseTicks);

void tntSpawnPrimed(World* w, int x, int y, int z, int fuseTicks);

bool tileMayPlace(World* w, unsigned char id, int x, int y, int z, int face);
void tileNeighborChanged(World* w, int x, int y, int z);
void tileRandomTick(World* w);

void heavyTileTick(World* w, int x, int y, int z, unsigned char id);

void farmlandCheckDry(World* w, int x, int y, int z);

void leafFlagNeighbors(World* w, int x, int y, int z);
void leafDecayTick(World* w, int x, int y, int z);

void worldSpawnResources(World* w, int x, int y, int z, unsigned char id, int data);

void worldSetFrustumCamera(float ex, float ey, float ez, float fx, float fy, float fz,
                           float yawDeg, float fovyDeg, float aspect, float nearD, float farD);

struct Texture;
void worldDraw(const World* w, float camX, float camY, float camZ, float viewDist, const Texture* terrain);

void worldRebuildStep(const World* w, float camX, float camY, float camZ, float viewDist);
void worldDrawWater(const World* w, float camX, float camY, float camZ, float viewDist);

struct BlockHit { bool hit; int x, y, z; int face; float clickX, clickY, clickZ; };
BlockHit worldPick(const World* w, float px, float py, float pz, float yaw, float pitch, float range);

int worldSelectionBoxes(const World* w, int x, int y, int z, float boxes[3][6]);

#endif
