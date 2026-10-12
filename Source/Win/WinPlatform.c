// winnls.h names a parameter `function`, which collides with the base macro.
#pragma push_macro("function")
#undef function
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#pragma pop_macro("function")

function void *PlatformVMReserve(u64 size)
{
    return VirtualAlloc(NULL, (SIZE_T)size, MEM_RESERVE, PAGE_NOACCESS);
}

function b32 PlatformVMCommit(void *ptr, u64 size)
{
    return VirtualAlloc(ptr, (SIZE_T)size, MEM_COMMIT, PAGE_READWRITE) != NULL;
}

function void PlatformVMRelease(void *ptr, u64 size)
{
    // MEM_RELEASE requires a zero size and frees the whole reservation.
    (void)size;
    VirtualFree(ptr, 0, MEM_RELEASE);
}

function u64 PlatformVMPageSize(void)
{
    SYSTEM_INFO info = {0};
    GetSystemInfo(&info);
    return info.dwPageSize;
}
