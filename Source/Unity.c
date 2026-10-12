// Single translation unit for the whole application.

#include "Base/Base.h"
#include "Platform/Platform.h"
#include "Renderer/Renderer.h"
#include "UI/UI.h"
#include "Program/Program.h"

#include "Base/Base.c"
#include "Platform/PlatformEvents.c"

#if defined(_WIN32)
#include "Win/WinPlatform.c"
#include "Win/WinWindow.c"
#include "Win/WinFont.c"
#elif defined(__linux__)
#include "Linux/LinuxPlatform.c"
#include "Linux/LinuxWindow.c"
#include "Linux/LinuxFont.c"
#else
#error "Unsupported platform"
#endif

#include "Renderer/OpenGLRenderer.c"
#include "Renderer/Font.c"
#include "UI/UI.c"
#include "Program/Commands.c"
#include "Program/Program.c"

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

typedef struct Prog_Loop {
    Prog_State state;
    R_Font font;
    b32 running;
    b32 needsFrame;
} Prog_Loop;

function void Prog__Frame(void *user, PlatformEventList events)
{
    Prog_Loop *loop = user;
    Prog_State *state = &loop->state;
    if (!loop->running) {
        return;
    }
    for (PlatformEvent *event = events.first; event; event = event->next) {
#if defined(LOG_EVENTS)
        LogEvent(event);
#endif
        if (event->kind == PlatformEventKind_CloseRequested) {
            loop->running = false;
        }
    }
    if (!loop->running) {
        return;
    }
    ArenaClear(&state->frameArena);
    PlatformWindowInfo window = PlatformWindowGetInfo();
    UI_BeginFrame(&state->ui, &state->frameArena, (R_Rect){ 0, 0, (f32)window.width, (f32)window.height }, events, PlatformNowMicroseconds());
    Prog_BuildUI(state, &loop->font, window.scale);
    UI_EndFrame();
    loop->needsFrame = Prog_ProcessCommands(state) || events.count != 0;
    R_BeginFrame(window.width, window.height, (R_Color){ 0.11f, 0.12f, 0.14f, 1.0f });
    UI_Draw();
    R_EndFrame();
    PlatformSwapBuffers();
}

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

    Arena permanent = {0};
    Prog_Loop loop = { .running = true, .needsFrame = true };
    PlatformWindowInfo startWindow = PlatformWindowGetInfo();
#if defined(_WIN32)
    const f32 fontPixelSize = 9.0f * (96.0f / 72.0f);
#else
    const f32 fontPixelSize = 15.0f;
#endif
    if (!R_FontCreate(&permanent, S("Segoe UI"), fontPixelSize * startWindow.scale, &loop.font)) {
        fprintf(stderr, "Failed to load fonts\n");
        return 1;
    }

    loop.state.ui.defaultFont = &loop.font;
    loop.state.ui.defaultColors = (UI_Colors){
        .background = { 0.11f, 0.12f, 0.14f, 1 },
        .border = { 0.25f, 0.27f, 0.31f, 1 },
        .text = { 0.90f, 0.91f, 0.93f, 1 },
    };
    PlatformDoubleClick doubleClick = PlatformDoubleClickSettings();
    loop.state.ui.doubleClickMicroseconds = doubleClick.intervalMicroseconds;
    loop.state.ui.doubleClickDistance = doubleClick.distance;
    PlatformSetModalFrameCallback(Prog__Frame, &loop);
    while (loop.running) {
        PlatformEventList events = PlatformPollEvents(!loop.needsFrame);
        Prog__Frame(&loop, events);
    }

    PlatformSetModalFrameCallback(NULL, NULL);
    UI_Release(&loop.state.ui);
    ArenaRelease(&loop.state.frameArena);
    ArenaRelease(&permanent);
    return 0;
}
