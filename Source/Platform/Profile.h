#pragma once

// Scope timings for any thread, recorded into a fixed lock-free buffer and
// written as a Chrome trace (open it in https://ui.perfetto.dev). The macros
// compile to nothing unless PROFILE is defined (the `profile` build flag).

#include <stdatomic.h>
#include <stdio.h>

typedef struct P_ProfileEvent {
    const char *name;  // string literal
    u64 begin;         // PlatformNowMicroseconds
    u64 duration;
    u32 thread;        // 1 is the thread that called P_ProfileInit
} P_ProfileEvent;

typedef struct P_Profiler {
    P_ProfileEvent *events;
    u64 capacity;
    atomic_uint_fast64_t count;  // may pass capacity; the excess events were dropped
    atomic_uint_fast32_t threadCount;
} P_Profiler;

global P_Profiler P_Profile;

function b32  P_ProfileInit(u64 capacity);
function void P_ProfileRelease(void);
// Does nothing before P_ProfileInit or once the buffer is full.
function void P_ProfileRecord(const char *name, u64 begin, u64 end);
// Call once the other threads have stopped recording. Returns the number of events written.
function u64  P_ProfileWriteTrace(FILE *file);

#if defined(PROFILE)
// `ProfileScope("Name") { ... }` times the block. Leaving it with return,
// break or goto skips the record.
#define ProfileScope(name) \
    for (u64 P__profileBegin = PlatformNowMicroseconds(), P__profileOnce = 1; P__profileOnce; \
         P_ProfileRecord("" name, P__profileBegin, PlatformNowMicroseconds()), P__profileOnce = 0)
#define ProfileRecordSince(name, begin) P_ProfileRecord("" name, (begin), PlatformNowMicroseconds())
#else
#define ProfileScope(name)
#define ProfileRecordSince(name, begin) ((void)(begin))
#endif
