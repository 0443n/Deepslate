#pragma once

#define PROF 0

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
    PROF_LIGHT,
    PROF_REBUILD,
    PROF_CULL,
    PROF_RSCAN,
    PROF_RBUILD,
    PROF_SKY,
    PROF_ENTITY,
    PROF_WATER,
    PROF_PART,
    PROF_HUD,
    PROF_GESYNC,
    PROF_VBLANK,
    PROF_N
};

enum { PROFC_PARTICLES, PROFC_SECTIONS, PROFC_PENDLIST, PROFC_N };

#if PROF
void profAdd(int slot, int n);
void profBegin(int slot);
void profEnd(int slot);
void profFrameEnd(void);
#else
static inline void profAdd(int, int) {}
static inline void profBegin(int) {}
static inline void profEnd(int) {}
static inline void profFrameEnd(void) {}
#endif
