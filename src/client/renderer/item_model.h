#pragma once

#include "world/level/chunk/chunk.h"
struct Texture;

class ItemModelRenderer {
public:

    bool build(short id, unsigned char data, int bowStage = -1);

    bool isFlat() const { return m_flat; }
    int  count()  const { return m_count; }

    void draw(unsigned int brCol, bool noMip, bool priority = false);

    bool buildShared(short id, unsigned char data, unsigned int brCol);
    void drawShared(bool noMip);

    static void drawMesh(ChunkVertex* m, int n, unsigned int brCol,
                         const Texture* tex, bool noMip);

    static void applyFlatPreTransform();

private:

    static const int MESH_MAX = 4700;

    // NOTE: the GE reads m_base directly, so it holds the shaded vertices and
    // m_baseCol keeps the unshaded colours needed to reshade after a light change.
    ChunkVertex    m_base[MESH_MAX] __attribute__((aligned(16)));
    unsigned int   m_baseCol[MESH_MAX];
    unsigned int   m_shadeCol = 0;
    bool           m_shaded = false;
    int            m_count = 0;
    bool           m_flat  = false;
    short          m_id = -1;
    unsigned char  m_data = 0xFF;
    int            m_bowStage = -2;
    const Texture* m_tex = 0;
    int            m_sharedSlot = -1;
    unsigned int   m_fallbackCol = 0xFFFFFFFFu;
};
