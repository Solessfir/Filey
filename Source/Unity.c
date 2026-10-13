// Single translation unit for the whole application.

#include "Base/Base.h"
#include "Platform/Platform.h"
#include "Platform/Jobs.h"
#include "Platform/Files.h"
#include "Platform/Profile.h"
#include "Renderer/Renderer.h"
#include "UI/UI.h"
#include "Program/Program.h"

#include "Base/Base.c"
#include "Platform/PlatformEvents.c"

#if defined(_WIN32)
#include "Win/WinPlatform.c"
#include "Win/WinThreads.c"
#include "Win/WinFiles.c"
#include "Win/WinIcons.c"
#include "Win/WinWindow.c"
#include "Win/WinFont.c"
#elif defined(__linux__)
#include "Linux/LinuxPlatform.c"
#include "Linux/LinuxThreads.c"
#include "Linux/LinuxFiles.c"
#include "Linux/LinuxIcons.c"
#include "Linux/LinuxWindow.c"
#include "Linux/LinuxFont.c"
#else
#error "Unsupported platform"
#endif

#include "Renderer/OpenGLRenderer.c"
#include "Renderer/Font.c"
#include "UI/UI.c"
#include "Platform/PlatformJobs.c"
#include "Platform/PlatformProfile.c"
#include "Program/Icons.c"
#include "Program/Directory.c"
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
    R_Font icons;
    b32 running;
    b32 needsFrame;
    u64 startTime;  // cleared once the first frame is presented
} Prog_Loop;

function void Prog__Wake(void *user)
{
    (void)user;
    P_WakeWindow();
}

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
    ProfileScope("Frame") {
        ArenaClear(&state->frameArena);
        b32 completed = false;
        ProfileScope("TakeCompletions") completed = Prog_TakeCompletions(state);
        PlatformWindowInfo window = PlatformWindowGetInfo();
        ProfileScope("BuildUI") {
            UI_BeginFrame(&state->ui, &state->frameArena, (R_Rect){ 0, 0, (f32)window.width, (f32)window.height }, events, PlatformNowMicroseconds());
            Prog_HandleInput(state, events);
            Prog_BuildUI(state, &loop->font, &loop->icons, window.scale);
        }
        ProfileScope("Layout") UI_EndFrame();
        ProfileScope("ProcessCommands") loop->needsFrame = Prog_ProcessCommands(state) || completed || events.count != 0;
        ProfileScope("Render") {
            R_BeginFrame(window.width, window.height, Prog_ThemeCustom.background);
            UI_Draw();
            R_EndFrame();
        }
        // Includes the wait for vsync.
        ProfileScope("SwapBuffers") PlatformSwapBuffers();
    }
    if (loop->startTime) {
        ProfileRecordSince("StartupToFirstFrame", loop->startTime);
        loop->startTime = 0;
    }
}

#if defined(PROFILE)
function void Prog__WriteProfile(void)
{
    const char *path = "Filey.trace.json";
    FILE *file = fopen(path, "wb");
    if (file != NULL) {
        u64 written = P_ProfileWriteTrace(file);
        u64 recorded = (u64)atomic_load(&P_Profile.count);
        fclose(file);
        fprintf(stderr, "Wrote %" PRIu64 " profile events to %s (%" PRIu64 " dropped)\n", written, path, recorded - written);
    }
    P_ProfileRelease();
}
#endif

int main(void)
{
    Prog_Loop loop = { .running = true, .needsFrame = true, .startTime = PlatformNowMicroseconds() };
#if defined(PROFILE)
    // 32 MB of events, about an hour of frames.
    if (!P_ProfileInit(UINT64_C(1) << 20)) {
        fprintf(stderr, "Failed to start the profiler\n");
    }
#endif
    b32 ok = false;
    ProfileScope("OpenWindow") ok = PlatformWindowOpen((PlatformWindowDesc){ .title = S("Filey"), .width = 1280, .height = 800 });
    if (!ok) {
        fprintf(stderr, "Failed to open the window\n");
        return 1;
    }
    ProfileScope("InitProgram") ok = Prog_Init(&loop.state, S(""), Prog__Wake, NULL);
    if (!ok) {
        fprintf(stderr, "Failed to start workers\n");
        return 1;
    }
    ProfileScope("InitRenderer") ok = R_Init();
    if (!ok) {
        fprintf(stderr, "Failed to initialize the renderer\n");
        Prog_Release(&loop.state);
        P_ReleaseWindowWake();
        return 1;
    }

    Arena permanent = {0};
    PlatformWindowInfo startWindow = PlatformWindowGetInfo();
#if defined(_WIN32)
    const f32 fontPixelSize = 9.0f * (96.0f / 72.0f);
#else
    const f32 fontPixelSize = 15.0f;
#endif
    // Icons are drawn a little larger than the text's line height, as in the reference.
    ProfileScope("LoadFonts") {
        ok = R_FontCreate(&permanent, S("Segoe UI"), fontPixelSize * startWindow.scale, &loop.font) &&
             R_FontCreateFromMemory(&permanent, Icon_FontData, sizeof(Icon_FontData), floorf(loop.font.lineHeight * 1.1f + 0.5f),
                                    Icon_FirstRune, Icon_FirstRune + Icon_Count - 1, &loop.icons);
    }
    if (!ok) {
        fprintf(stderr, "Failed to load fonts\n");
        Prog_Release(&loop.state);
        P_ReleaseWindowWake();
        ArenaRelease(&permanent);
        return 1;
    }

    loop.state.ui.defaultFont = &loop.font;
    loop.state.ui.defaultColors = (UI_Colors){
        .background = Prog_ThemeCustom.background,
        .border = Prog_ThemeCustom.separator,
        .text = Prog_ThemeCustom.text,
    };
    PlatformDoubleClick doubleClick = PlatformDoubleClickSettings();
    loop.state.ui.doubleClickMicroseconds = doubleClick.intervalMicroseconds;
    loop.state.ui.doubleClickDistance = doubleClick.distance;
    PlatformSetModalFrameCallback(Prog__Frame, &loop);
    while (loop.running) {
        PlatformEventList events = {0};
        ProfileScope("PollEvents") events = PlatformPollEvents(!loop.needsFrame);
        Prog__Frame(&loop, events);
    }

    PlatformSetModalFrameCallback(NULL, NULL);
    Prog_Release(&loop.state);
    P_ReleaseWindowWake();
    ArenaRelease(&permanent);
#if defined(PROFILE)
    Prog__WriteProfile();
#endif
    return 0;
}
