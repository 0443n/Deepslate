
#ifndef MCPSP_WORLD_STORAGE_CHUNK_STORAGE_H
#define MCPSP_WORLD_STORAGE_CHUNK_STORAGE_H

struct World;

void chunkStorageInit(const char* absDir);
void chunkStorageShutdown();

bool chunkStorageHasSave(const char* absDir);

bool chunkStorageLoad(World* w, int cx, int cz, bool* outGotLight, bool* outPopulated = 0);

bool chunkStorageSave(World* w, int cx, int cz);

void chunkStorageTakeChestPositions(int** out, int* count);
void chunkStorageClearChestPositions();

#endif
