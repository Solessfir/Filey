// Profiler buffer and trace writer.

#include <inttypes.h>

global thread_local u32 P__profileThread;

function u32 P__ProfileThread(void)
{
    if (P__profileThread == 0) {
        P__profileThread = (u32)atomic_fetch_add_explicit(&P_Profile.threadCount, 1, memory_order_relaxed) + 1;
    }
    return P__profileThread;
}

function b32 P_ProfileInit(u64 capacity)
{
    Assert(P_Profile.events == NULL);
    u64 page = PlatformVMPageSize();
    u64 size = AlignPow2(capacity * sizeof(P_ProfileEvent), page);
    void *memory = PlatformVMReserve(size);
    if (memory == NULL || !PlatformVMCommit(memory, size)) {
        if (memory != NULL) PlatformVMRelease(memory, size);
        return false;
    }
    P_Profile.events = memory;
    P_Profile.capacity = size / sizeof(P_ProfileEvent);
    atomic_store_explicit(&P_Profile.count, 0, memory_order_relaxed);
    atomic_store_explicit(&P_Profile.threadCount, 0, memory_order_relaxed);
    P__profileThread = 0;
    P__ProfileThread();
    return true;
}

function void P_ProfileRelease(void)
{
    if (P_Profile.events != NULL) {
        PlatformVMRelease(P_Profile.events, AlignPow2(P_Profile.capacity * sizeof(P_ProfileEvent), PlatformVMPageSize()));
    }
    P_Profile = (P_Profiler){0};
    P__profileThread = 0;
}

function void P_ProfileRecord(const char *name, u64 begin, u64 end)
{
    if (P_Profile.events == NULL) {
        return;
    }
    u64 index = atomic_fetch_add_explicit(&P_Profile.count, 1, memory_order_relaxed);
    if (index < P_Profile.capacity) {
        P_Profile.events[index] = (P_ProfileEvent){ name, begin, end - begin, P__ProfileThread() };
    }
}

function u64 P_ProfileWriteTrace(FILE *file)
{
    u64 count = Min((u64)atomic_load_explicit(&P_Profile.count, memory_order_acquire), P_Profile.capacity);
    u32 threads = (u32)atomic_load_explicit(&P_Profile.threadCount, memory_order_relaxed);
    fprintf(file, "{\"traceEvents\":[\n");
    for (u32 thread = 1; thread <= threads; ++thread) {
        fprintf(file, "{\"name\":\"thread_name\",\"ph\":\"M\",\"pid\":1,\"tid\":%u,\"args\":{\"name\":\"%s\"}},\n",
                thread, thread == 1 ? "Main" : "Worker");
    }
    // Names are string literals without characters that need escaping.
    for (u64 i = 0; i < count; ++i) {
        P_ProfileEvent *event = &P_Profile.events[i];
        fprintf(file, "{\"name\":\"%s\",\"ph\":\"X\",\"pid\":1,\"tid\":%u,\"ts\":%" PRIu64 ",\"dur\":%" PRIu64 "},\n",
                event->name, event->thread, event->begin, event->duration);
    }
    // A closing metadata event keeps every real event followed by a comma.
    fprintf(file, "{\"name\":\"process_name\",\"ph\":\"M\",\"pid\":1,\"args\":{\"name\":\"Filey\"}}\n]}\n");
    return count;
}
