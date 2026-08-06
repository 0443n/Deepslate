#ifndef MESH_SINK_H
#define MESH_SINK_H

struct ChunkVertex;
struct MeshSink;

struct MeshSink {
    ChunkVertex* buf[4];
    int          cap[4];
    int          n[4];
    int          total[4];

    bool       (*flush)(MeshSink*, int layer);
    void*        ctx;
};

static inline bool sinkReserve(MeshSink* sk, int layer, int need) {
    if (sk->n[layer] + need <= sk->cap[layer]) return true;
    if (!sk->flush) return false;
    if (!sk->flush(sk, layer)) return false;

    return need <= sk->cap[layer];
}

static inline int sinkCount(const MeshSink* sk, int layer) {
    return sk->total[layer] + sk->n[layer];
}

int meshSectionSink(const World* w, int ox, int oz, int y0, int y1,
                    MeshSink* sk, int* nLava, bool leavesOpaque, bool leavesCull);

struct DrawVertex;
struct World;

void chunkPackInto(DrawVertex* d, const ChunkVertex* s, int n,
                   int ox, int oy, int oz, int* qlo, int* qhi);

DrawVertex* chunkPackFinish(const DrawVertex* staging, int n);

float chunkPackDecodeY(int q, int oy);

#endif
