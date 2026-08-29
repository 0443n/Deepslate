#pragma once

#ifndef PROF
#define PROF 0
#endif

enum {
    PROF_TICK,
    PROF_TPLAYER,
    PROF_TWORLD,
    PROF_TRAND,
    PROF_TPEND,
    PROF_TENT,
    PROF_TTE,
    PROF_TPART,
    PROF_WORLD,
    PROF_STREAM,
    PROF_SGEN,
    PROF_SDECOR,
    PROF_SLIGHT,
    PROF_SDISK,
    PROF_SEVICT,
    PROF_SMISC,
    PROF_LIGHT,
    PROF_REBUILD,
    PROF_CULL,

    // PROF_CULL split three ways, plus the three stages that follow it inside
    // PROF_WORLD but were never accounted for.
    PROF_CEVICT,
    PROF_CWALK,
    PROF_CMARK,
    PROF_CGATHER,
    PROF_CSUBMIT,
    PROF_CSYNC,

    PROF_RSCAN,
    PROF_RBUILD,

    PROF_MEMIT,
    PROF_MPACK,
    PROF_MALLOC,
    PROF_MCONV,

    PROF_SKY,
    PROF_ENTITY,
    PROF_WATER,
    PROF_PART,
    PROF_HUD,

    PROF_HITEM,
    PROF_HBAR,
    PROF_HDBG,

    PROF_GSTART,
    PROF_GPRE,
    PROF_GMID,
    PROF_GPOST,
    PROF_GOUTLINE,
    PROF_GHAND,
    PROF_GFIRE,

    PROF_GESYNC,
    PROF_VBLANK,
    PROF_N
};

enum { PROFC_PARTICLES, PROFC_SECTIONS, PROFC_PENDLIST, PROFC_STREAMIN,
       PROFC_DRAWLIVE, PROFC_PACKVERTS,

       // Opaque sections actually submitted, the number occlusion culling moves.
       PROFC_DRAWNSEC,

       // Vertices handed to the GE per frame, all passes.
       PROFC_DRAWNVERT,

       // The same total split by draw layer, so it is clear whether terrain cubes
       // or decoration dominates the vertex budget.
       PROFC_VOPAQUE, PROFC_VNOMIP, PROFC_VLEAVES, PROFC_VWATER,


       // Sections the cave-cull walk actually enqueued, the walk's own work.
       PROFC_WALKNODES,

       // MERGE_PROBE only. Layer 0 faces emitted, and the quads a greedy merge
       // keyed on tile plus corner light would have produced instead.
       PROFC_MFACES, PROFC_MQUADS,

       PROFC_MARKED, PROFC_N };

#if PROF
void profListBytes(unsigned bytes);
void profAdd(int slot, int n);
void profBegin(int slot);
void profEnd(int slot);
void profFrameEnd(void);
#else
static inline void profListBytes(unsigned) {}
static inline void profAdd(int, int) {}
static inline void profBegin(int) {}
static inline void profEnd(int) {}
static inline void profFrameEnd(void) {}
#endif
