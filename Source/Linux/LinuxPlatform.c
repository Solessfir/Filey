#include <sys/mman.h>
#include <unistd.h>

function void *PlatformVMReserve(u64 size)
{
    void *result = mmap(NULL, (size_t)size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    return result == MAP_FAILED ? NULL : result;
}

function b32 PlatformVMCommit(void *ptr, u64 size)
{
    // Pages are backed lazily on first touch, like a Windows commit.
    return mprotect(ptr, (size_t)size, PROT_READ | PROT_WRITE) == 0;
}

function void PlatformVMRelease(void *ptr, u64 size)
{
    munmap(ptr, (size_t)size);
}

function u64 PlatformVMPageSize(void)
{
    return (u64)sysconf(_SC_PAGESIZE);
}
