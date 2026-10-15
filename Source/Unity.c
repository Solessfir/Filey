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
#include "Linux/LinuxApps.c"
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
        "Text", "MouseMove", "MouseDown", "MouseUp", "MouseWheel", "MouseLeave",
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
    R_Font detailFont;
    R_Font icons;
    R_Font smallIcons;
    f32 fontScale;    // the display scale the fonts were rasterized for
    // The inspector's monospace font, made at its size once it is first shown.
    Arena monoArena;
    R_Font mono;
    f32 monoPixels;
    // The families the fonts were last made from.
    Prog_FontName fontName, monoName;
    b32 running;
    b32 needsFrame;
    b32 transparent;  // what the window was last set to
    u8 backdrop;
    b32 vsyncOff;
    u64 startTime;  // cleared once the first frame is presented
    // A frame that is running; the system's modal loops can call back for a frame while one is, and that one is skipped.
    b32 inFrame;
    // FILEY_CAPTURE_DIR: each presented frame is written there as frame.bmp, or with FILEY_CAPTURE_SEQUENCE as numbered files named after their time,
    // for comparing screenshots without the window on screen, where the OS may not keep what is drawn.
    String captureDirectory;
    b32 captureSequence;
    u64 captureCount;
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
    R_Font font = {0}, detailFont = {0}, icons = {0}, smallIcons = {0};
    const Prog_FontName *name = &loop->state.font;
    String family = name->length ? StrMake(name->text, name->length) : S("Segoe UI");
    // Icons are a little smaller than the text's line height, which gives their strokes the reference's weight.
    b32 ok = R_FontCreate(&arena, family, fontPixelSize * scale, &font) &&
             R_FontCreate(&arena, family, (fontPixelSize - 96.0f / 72.0f) * scale, &detailFont) &&
             R_FontCreateFromMemory(&arena, Icon_FontData, sizeof(Icon_FontData), floorf(font.lineHeight * 0.92f + 0.5f),
                                    Icon_FirstRune, Icon_FirstRune + Icon_Count - 1, &icons) &&
             R_FontCreateFromMemory(&arena, Icon_FontData, sizeof(Icon_FontData), floorf(font.lineHeight * 0.92f * 0.7f + 0.5f),
                                    Icon_FirstRune, Icon_FirstRune + Icon_Count - 1, &smallIcons);
    if (!ok) {
        R_TextureRelease(font.atlas);
        R_TextureRelease(detailFont.atlas);
        R_TextureRelease(icons.atlas);
        ArenaRelease(&arena);
        return false;
    }
    R_TextureRelease(loop->font.atlas);
    R_TextureRelease(loop->detailFont.atlas);
    R_TextureRelease(loop->icons.atlas);
    R_TextureRelease(loop->smallIcons.atlas);
    ArenaRelease(&loop->fontArena);
    loop->fontArena = arena;
    loop->font = font;
    loop->detailFont = detailFont;
    loop->state.smallFont = &loop->detailFont;
    loop->icons = icons;
    loop->smallIcons = smallIcons;
    loop->state.smallIcons = &loop->smallIcons;
    loop->fontScale = scale;
    loop->fontName = *name;
    return true;
}

function void Prog__CaptureFrame(Prog_Loop *loop, u32 width, u32 height)
{
    if (!width || !height) return;
    Arena *arena = &loop->state.frameArena;
    u64 pixels = (u64)width * height * 4, size = 54 + pixels;
    u8 *file = ArenaPushArray(arena, size, u8);
    // A 32-bit bitmap is stored bottom row first, as the pixels are read.
    file[0] = 'B'; file[1] = 'M';
    u32 fields[] = { (u32)size, 0, 54, 40, width, height };
    MemCopy(file + 2, fields, sizeof(fields));
    u16 planes[] = { 1, 32 };
    MemCopy(file + 26, planes, sizeof(planes));
    u32 rest[] = { 0, (u32)pixels };
    MemCopy(file + 30, rest, sizeof(rest));
    R_ReadPixels(width, height, file + 54);
    String name = loop->captureSequence ? StrFormat(arena, "frame-%06" PRIu64 "-%" PRIu64 ".bmp", loop->captureCount++, PlatformNowMicroseconds()) : S("frame.bmp");
    P_WriteFile(arena, StrFormat(arena, "%S/%S", loop->captureDirectory, name), StrMake((const char *)file, size));
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
    if (!loop->running || loop->inFrame) {
        return;
    }
    loop->inFrame = true;
    ProfileScope("Frame") {
        ArenaClear(&state->frameArena);
        b32 completed = false, handled = false;
        ProfileScope("TakeCompletions") completed = Prog_TakeCompletions(state);
        PlatformWindowInfo window = PlatformWindowGetInfo();
        // The settings' scale multiplies the display's.
        f32 scale = window.scale * (state->uiScale > 0 ? state->uiScale : 1);
        b32 fontChanged = !StrIsEqual(StrMake(loop->fontName.text, loop->fontName.length), StrMake(state->font.text, state->font.length));
        if (scale != loop->fontScale || fontChanged) {
            ProfileScope("LoadFonts") Prog__LoadFonts(loop, scale);
            state->ui.doubleClickDistance = PlatformDoubleClickSettings().distance;
        }
        if (state->previewShown) {
            f32 points = (f32)(state->inspectorFontSize ? state->inspectorFontSize : Prog_InspectorFontSize);
            f32 pixels = points * (96.0f / 72.0f) * scale;
            const Prog_FontName *name = &state->inspectorFont;
            b32 monoChanged = !StrIsEqual(StrMake(loop->monoName.text, loop->monoName.length), StrMake(name->text, name->length));
            if (pixels != loop->monoPixels || monoChanged) {
                loop->monoPixels = pixels;
                loop->monoName = *name;
#if defined(_WIN32)
                String family = name->length ? StrMake(name->text, name->length) : S("Consolas");
#else
                String family = name->length ? StrMake(name->text, name->length) : S("monospace");
#endif
                Arena arena = {0};
                R_Font mono = {0};
                if (R_FontCreate(&arena, family, pixels, &mono)) {
                    R_TextureRelease(loop->mono.atlas);
                    ArenaRelease(&loop->monoArena);
                    loop->monoArena = arena;
                    loop->mono = mono;
                } else {
                    ArenaRelease(&arena);
                }
            }
            state->monoFont = loop->mono.atlas.u64[0] ? &loop->mono : NULL;
        }
        state->ui.animate = !state->animationsOff;
        ProfileScope("BuildUI") {
            UI_BeginFrame(&state->ui, &state->frameArena, (R_Rect){ 0, 0, (f32)window.width, (f32)window.height }, events, PlatformNowMicroseconds());
            Prog_HandleInput(state, events);
            // Hotkey commands apply before the UI is built, so this frame already shows them.
            handled = Prog_ProcessCommands(state);
            Prog_BuildUI(state, &loop->font, &loop->icons, scale);
        }
        ProfileScope("Layout") UI_EndFrame();
        ProfileScope("ProcessCommands") loop->needsFrame = Prog_ProcessCommands(state) || handled || completed || events.count != 0 || state->ui.animating;
        ProfileScope("Render") {
            if (loop->vsyncOff != state->noVSync) {
                loop->vsyncOff = state->noVSync;
                PlatformSetVSync(!loop->vsyncOff);
            }
            if (loop->transparent != state->transparent || loop->backdrop != state->glassBackdrop) {
                loop->transparent = state->transparent;
                loop->backdrop = state->glassBackdrop;
                PlatformWindowSetTransparent(loop->transparent, (PlatformBackdrop)loop->backdrop);
            }
            R_BeginFrame(window.width, window.height, Prog_CurrentTheme()->window);
            UI_Draw();
            R_EndFrame();
            if (loop->captureDirectory.length) Prog__CaptureFrame(loop, (u32)Max(window.width, 0), (u32)Max(window.height, 0));
        }
        // Includes the wait for vsync.
        ProfileScope("SwapBuffers") PlatformSwapBuffers();
    }
    loop->inFrame = false;
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
    if (ok) Prog_LoadSettings(&loop.state, P_ConfigDirectory(startup.arena));
    // Without folders to open, the last run's panels and tabs come back, or the place the settings start from.
    if (ok && !startPaths.header.count) Prog_StartUp(&loop.state);
    if (ok && getenv("FILEY_DEBUG_THEME")) Prog_SetDebugTheme(&loop.state, true);
    const char *capture = getenv("FILEY_CAPTURE_DIR");
    if (capture && capture[0]) {
        loop.captureDirectory = StrFromCStr(capture);
        loop.captureSequence = getenv("FILEY_CAPTURE_SEQUENCE") != NULL;
    }
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
    Prog_SetTransparent(&loop.state, loop.state.transparent);
    PlatformDoubleClick doubleClick = PlatformDoubleClickSettings();
    loop.state.ui.doubleClickMicroseconds = doubleClick.intervalMicroseconds;
    loop.state.ui.doubleClickDistance = doubleClick.distance;
    loop.state.warmMenus = true;
    PlatformSetModalFrameCallback(Prog__Frame, &loop);
    while (loop.running) {
        PlatformEventList events = {0};
        ProfileScope("PollEvents") events = PlatformPollEvents(!loop.needsFrame);
        Prog__Frame(&loop, events);
        if (loop.state.moveRequested) {
            loop.state.moveRequested = false;
            PlatformWindowStartMove();
            loop.needsFrame = true;
        }
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
