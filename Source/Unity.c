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
#include "Win/WinShell.c"
#include "Win/WinLaunch.c"
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
    Arena fontArena;
    R_Font font;
    R_Font icons;
    f32 fontScale;    // the display scale the fonts were rasterized for
    b32 running;
    b32 needsFrame;
    b32 transparent;  // what the window was last set to
    u64 startTime;  // cleared once the first frame is presented
} Prog_Loop;

// Fonts are rasterized for one display scale, so another scale (a window moved to another monitor, or a changed display setting) rebuilds them.
// The old fonts stay when the new ones fail.
function b32 Prog__LoadFonts(Prog_Loop *loop, f32 scale)
{
#if defined(_WIN32)
    const f32 fontPixelSize = 9.0f * (96.0f / 72.0f);
#else
    const f32 fontPixelSize = 15.0f;
#endif
    Arena arena = {0};
    R_Font font = {0}, icons = {0};
    // Icons are drawn a little larger than the text's line height, as in the reference.
    b32 ok = R_FontCreate(&arena, S("Segoe UI"), fontPixelSize * scale, &font) &&
             R_FontCreateFromMemory(&arena, Icon_FontData, sizeof(Icon_FontData), floorf(font.lineHeight * 1.1f + 0.5f),
                                    Icon_FirstRune, Icon_FirstRune + Icon_Count - 1, &icons);
    if (!ok) {
        R_TextureRelease(font.atlas);
        ArenaRelease(&arena);
        return false;
    }
    R_TextureRelease(loop->font.atlas);
    R_TextureRelease(loop->icons.atlas);
    ArenaRelease(&loop->fontArena);
    loop->fontArena = arena;
    loop->font = font;
    loop->icons = icons;
    loop->fontScale = scale;
    return true;
}

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
        if (window.scale != loop->fontScale) {
            ProfileScope("LoadFonts") Prog__LoadFonts(loop, window.scale);
            state->ui.doubleClickDistance = PlatformDoubleClickSettings().distance;
        }
        ProfileScope("BuildUI") {
            UI_BeginFrame(&state->ui, &state->frameArena, (R_Rect){ 0, 0, (f32)window.width, (f32)window.height }, events, PlatformNowMicroseconds());
            Prog_HandleInput(state, events);
            Prog_BuildUI(state, &loop->font, &loop->icons, window.scale);
        }
        ProfileScope("Layout") UI_EndFrame();
        ProfileScope("ProcessCommands") loop->needsFrame = Prog_ProcessCommands(state) || completed || events.count != 0;
        ProfileScope("Render") {
            if (loop->transparent != state->transparent) {
                loop->transparent = state->transparent;
                PlatformWindowSetTransparent(loop->transparent);
            }
            R_BeginFrame(window.width, window.height, Prog_CurrentTheme()->window);
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
    R_Color background = Prog_ThemeCustom.window;
    u32 backgroundRgb = (u32)(background.r * 255.0f + 0.5f) << 16 | (u32)(background.g * 255.0f + 0.5f) << 8 | (u32)(background.b * 255.0f + 0.5f);
    ProfileScope("OpenWindow") ok = PlatformWindowOpen((PlatformWindowDesc){ .title = S("Filey"), .width = 1280, .height = 800, .background = backgroundRgb });
    if (!ok) {
        fprintf(stderr, "Failed to open the window\n");
        return 1;
    }
    // Folders given on the command line, as the desktop entry and "Open with" pass them, open in tabs instead of the working directory.
    Scratch startup = GetScratch();
    ArrayStr startPaths = P_StartupPaths(startup.arena);
    ProfileScope("InitProgram") ok = Prog_Init(&loop.state, startPaths.header.count ? startPaths.v[0] : S(""), Prog__Wake, NULL);
    for (u64 i = 1; ok && i < startPaths.header.count; ++i) Prog_OpenTab(&loop.state, startPaths.v[i], (u32)i, false);
    ReleaseScratch(startup);
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
    ProfileScope("LoadFonts") ok = Prog__LoadFonts(&loop, PlatformWindowGetInfo().scale);
    if (!ok) {
        fprintf(stderr, "Failed to load fonts\n");
        Prog_Release(&loop.state);
        P_ReleaseWindowWake();
        ArenaRelease(&permanent);
        return 1;
    }

    loop.state.ui.defaultFont = &loop.font;
    Prog_SetTransparent(&loop.state, false);
    PlatformDoubleClick doubleClick = PlatformDoubleClickSettings();
    loop.state.ui.doubleClickMicroseconds = doubleClick.intervalMicroseconds;
    loop.state.ui.doubleClickDistance = doubleClick.distance;
    loop.state.warmMenus = true;
    PlatformSetModalFrameCallback(Prog__Frame, &loop);
    while (loop.running) {
        PlatformEventList events = {0};
        ProfileScope("PollEvents") events = PlatformPollEvents(!loop.needsFrame);
        Prog__Frame(&loop, events);
        if (loop.state.dragRequested) {
            Prog_DragSelection(&loop.state);
            loop.needsFrame = true;
        }
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
