// Single translation unit for the whole application.

#include "Base/Base.h"
#include "Platform/Platform.h"

#if defined(_WIN32)
#include "Win/WinPlatform.c"
#elif defined(__linux__)
#include "Linux/LinuxPlatform.c"
#else
#error "Unsupported platform"
#endif

#include "Base/Base.c"

int main(void)
{
    Arena arena = {0};
    String message = StrFormat(&arena, "Filey %d", 0);
    printf("%.*s\n", (int)message.length, message.value);
    ArenaRelease(&arena);
    return 0;
}
