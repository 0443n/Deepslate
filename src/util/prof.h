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

// Live even with the profiler off. One byte stored per section entry is cheap
// enough to ship, and it lets the watchdog name the phase a hung frame died in.
extern volatile unsigned char g_profPhase;
const char* profSlotName(int slot);

// A CPU fault kills every thread at once, so nothing written to the stick
// survives it. The displayed framebuffer does, because the display controller
// keeps scanning it out, so the frame's progress is drawn straight into it.
void phaseFrameBegin(void);
void phaseMark(int slot);

// Row 0 is the frame phase, rows 1 to 3 are whatever the phase wants to name.
void phaseRow(int row, int value);

// libpng keeps its signature in small data, past the code guard's end, and
// something writes 16 bit pixels over it. Two words are cheap enough to check
// at every phase change, which names the phase that did it.
extern const unsigned int* g_sdWatch;
extern unsigned int g_sdWant0, g_sdWant1;
void guardTripped(int slot);
static inline void guardTripCheck(int slot) {
    if (!g_sdWatch) return;
    if (g_sdWatch[0] != g_sdWant0 || g_sdWatch[1] != g_sdWant1) guardTripped(slot);
}

#if PROF
void profListBytes(unsigned bytes);
void profAdd(int slot, int n);
void profBegin(int slot);
void profEnd(int slot);
void profFrameEnd(void);
#else
static inline void profListBytes(unsigned) {}
static inline void profAdd(int, int) {}
static inline void profBegin(int slot) {
    if ((int)g_profPhase != slot) { g_profPhase = (unsigned char)slot; phaseMark(slot); }
    guardTripCheck(slot);
}
static inline void profEnd(int) {}
static inline void profFrameEnd(void) {}
#endif
