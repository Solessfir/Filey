// Single translation unit for the whole application.

#include "Base/Base.h"
#include "Platform/Platform.h"
#include "Renderer/Renderer.h"

#include "Base/Base.c"
#include "Platform/PlatformEvents.c"

#if defined(_WIN32)
#include "Win/WinPlatform.c"
#include "Win/WinWindow.c"
#elif defined(__linux__)
#include "Linux/LinuxPlatform.c"
#include "Linux/LinuxWindow.c"
#else
#error "Unsupported platform"
#endif

#include "Renderer/OpenGLRenderer.c"

#if defined(LOG_EVENTS)
function void LogEvent(const PlatformEvent *event)
{
    static const char *names[] = {
        "None", "CloseRequested", "Resized", "FocusChanged", "KeyDown", "KeyUp",
        "Text", "MouseMove", "MouseDown", "MouseUp", "MouseWheel",
    };
    static_assert(CArrayCount(names) == PlatformEventKind_Count);

    fprintf(stderr, "%-14s key=%u mods=%u rune=%d button=%u pos=(%.0f,%.0f) wheel=(%.2f,%.2f) size=%dx%d%s\n",
            names[event->kind], event->key, event->mods, event->codepoint, event->button,
            (f64)event->x, (f64)event->y, (f64)event->wheelX, (f64)event->wheelY,
            event->width, event->height, event->isRepeat ? " repeat" : "");
}
#endif

int main(void)
{
    if (!PlatformWindowOpen((PlatformWindowDesc){ .title = S("Filey"), .width = 1280, .height = 800 })) {
        fprintf(stderr, "Failed to open the window\n");
        return 1;
    }
    if (!R_Init()) {
        fprintf(stderr, "Failed to initialize the renderer\n");
        return 1;
    }

    b32 running = true;
    while (running) {
        PlatformEventList events = PlatformPollEvents(true);

        for (PlatformEvent *event = events.first; event != NULL; event = event->next) {
#if defined(LOG_EVENTS)
            LogEvent(event);
#endif
            if (event->kind == PlatformEventKind_CloseRequested) {
                running = false;
            }
        }

        PlatformWindowInfo window = PlatformWindowGetInfo();
        R_BeginFrame(window.width, window.height, (R_Color){ 0.11f, 0.12f, 0.14f, 1.0f });
        R_EndFrame();
        PlatformSwapBuffers();
    }

    return 0;
}
