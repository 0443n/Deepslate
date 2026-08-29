
#ifndef MCPSP_WORLD_STORAGE_LEVEL_STORAGE_H
#define MCPSP_WORLD_STORAGE_LEVEL_STORAGE_H

#include "world/level/levelgen/level_source.h"
#include "world/level/levelgen/gen_features.h"

struct World;

enum { DIM_OVERWORLD = 0, DIM_NETHER = 1 };

namespace LevelStorage {

bool hasSave(const char* absDir);

bool save(World* w, const char* absDir, long seed, int gameType, const char* levelName,
          bool fullSave = false);

bool load(World* w, const char* absDir, long* outSeed, int* outGameType);

void applyLoadedHotbar();

bool loadedValidPlayerPos();

bool readInfo(const char* absDir, char* nameOut, int nameCap, int* outGameType, long* outSeed);

void setActiveWorld(const char* absDir, long seed, int gameType, const char* levelName,
                    int worldType = WORLD_TYPE_OLD, int genMask = GEN_FEATURES_ALL_ON);
const char* getActiveDir();
long getActiveSeed();
int getActiveGameType();
int getActiveWorldType();
int getActiveGenMask();
const char* getActiveName();

int  getActiveDim();
void setActiveDim(int dim);

// Where one dimension's chunks and entities live. level.dat belongs to the whole
// world and always stays at the root.
void dimDir(char* out, int cap, const char* absDir, int dim);

// Chunks and entities only, for swapping dimensions without disturbing level.dat.
bool saveDimension(World* w);
bool loadDimension(World* w, int centreCx, int centreCz);

}

#endif
