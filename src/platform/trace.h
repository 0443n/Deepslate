#ifndef MCPSP_PLATFORM_TRACE_H
#define MCPSP_PLATFORM_TRACE_H

// Breadcrumbs that outlive a hardware lockup. The PSP runs this EBOOT in user
// mode, so no exception handler can be installed, and a freeze or a power-off
// leaves nothing behind unless it was already on the memory stick.

void traceInit();
void traceMark(const char* fmt, ...);
void traceClose();

// Watches the frame counter and records the phase a hung frame died in.
void traceWatchdogStart();
extern volatile unsigned int g_frameSeq;

// Low-water mark of the main thread stack, sampled by the watchdog.
extern unsigned int g_mainStackMin;

// Shadows the read-only segment and repairs any word that changes under it.
void codeGuardInit(void);
void codeGuardStep(void);

// Compares the whole segment at once, so a burst write is caught in its frame.
void codeGuardFull(const char* where);

// How many times the small data tripwire has fired this run.
unsigned int traceSdTrips(void);
bool codePtrOk(unsigned int v);
extern unsigned int g_codeFixes;

// Non-empty when the previous run never reached traceClose.
const char* traceLastCrash();

#endif
