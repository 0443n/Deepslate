// The generator lives in Rust now, see rust/src/mcpegen.rs and rust/src/nether.rs.
// This is the C++ side of that boundary, holding the entry points the rest of
// the game calls and the two window loops that stayed here because they drive
// the chunk cache and the loading progress bar.
//
// mcpegen.cpp, nether_gen.cpp and caves.cpp are no longer built. They stay in
// the tree as the reference tools/gen-vectors.sh checks the Rust against.

#include "world/level/levelgen/mcpegen.h"
#include "world/level/levelgen/nether_gen.h"
#include "world/level/chunk/chunk_cache.h"
#include "world/level/world.h"
#include "rs/rs.h"

#include <pspkernel.h>
#include <pspthreadman.h>

void worldGenInit(long seed, int genMask) {
    ds_gen_init((int)seed, genMask);
}

void worldGenFree() {
    ds_gen_free();
}

void chunkGenerateTerrain(World* w, int cx, int cz) {
    ds_gen_terrain(w, cx, cz);
}

bool chunkPostProcessPhase(World* w, int cx, int cz, int phase) {
    return ds_gen_phase(w, cx, cz, phase) != 0;
}

void worldPlaceFlowers(World* w) {
    ds_gen_place_flowers(w);
}

void worldPlaceMushrooms(World* w) {
    ds_gen_place_mushrooms(w);
}

void netherGenInit(long seed) {
    ds_nether_init((int)seed);
}

void netherGenFree() {
    ds_nether_free();
}

void netherGenerateChunk(World* w, int cx, int cz) {
    ds_nether_chunk(w, cx, cz);
}

void worldGenerateMCPE(World* w, long seed, int genMask) {
    worldGenInit(seed, genMask);
    worldGenerateWindow(w);
}

void worldGenerateWindow(World* w) {
    const int side = worldFitsInWindow(w) ? WORLD_SIZE_CHUNKS : w->slotN;
    // A streamed world spawns the player in its middle, so the first window has
    // to be built there and not at the origin.
    const int base = worldFitsInWindow(w) ? 0 : (WORLD_SIZE_CHUNKS - side) / 2;
    int totalChunks = side * side;
    int doneChunks = 0;
    for (int cz = 0; cz < side; cz++)
    for (int cx = 0; cx < side; cx++) {
        worldGetChunk(w, base + cx, base + cz);
        doneChunks++;
        g_terrainProgress = (doneChunks * 50) / totalChunks;

        sceKernelDelayThread(100);
    }
}
