#pragma once
#include "world/level/world.h"

void worldSetFrustumCamera(float, float, float, float, float, float,
                           float, float, float, float, float);
bool sectionVisible(const ChunkMesh* c, const ChunkSection* s);
bool columnVisible(const ChunkMesh* c);

// Geometric bounds of a section, not the mesh bounds. The graph walk has to pass
// through empty sections, whose mesh box is degenerate and would cull the path.
bool sectionBoxVisible(const ChunkMesh* c, int si);
float dist3D_sq(float camX, float camY, float camZ,
                const ChunkMesh* c, float cy0, float cy1);

bool leafOpaqueBand(const ChunkMesh* c, int y0, int y1,
                    float camX, float camY, float camZ, bool fancyGraphics);

bool leafCullBand(const ChunkMesh* c, int y0, int y1,
                  float camX, float camY, float camZ, bool fancyGraphics);
