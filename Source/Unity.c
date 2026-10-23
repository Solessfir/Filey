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
#include "Win/WinAudio.c"
#include "Win/WinWindow.c"
#include "Win/WinFont.c"
#elif defined(__linux__)
#include "Linux/LinuxPlatform.c"
#include "Linux/LinuxThreads.c"
#include "Linux/LinuxFiles.c"
#include "Linux/LinuxIcons.c"
#include "Linux/LinuxApps.c"
#include "Linux/LinuxAudio.c"
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
#include "Program/Blp.c"
#include "Program/Models.c"
#include "Program/Office.c"
#include "Program/Media.c"
#include "Program/Commands.c"
#include "Program/Program.c"

#if defined(LOG_EVENTS)
function void LogEvent(const PlatformEvent *event)
{
    static const char *names[] = {
        "None", "CloseRequested", "Resized", "FocusChanged", "KeyDown", "KeyUp",
        "Text", "MouseMove", "MouseDown", "MouseUp", "MouseWheel", "MouseLeave", "DrivesChanged",
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
    R_Font mediumIcons;
    f32 fontScale;    // the display scale the fonts were rasterized for
    // The inspector's monospace font, made at its size once it is first shown.
    Arena monoArena;
    R_Font mono;
    f32 monoPixels;
    // The Setup page's text at its three sizes, made while it shows, and the scale they were made for.
    Arena setupArena;
    R_Font setupFonts[3];
    f32 setupScale;
    // The families the fonts were last made from.
    Prog_FontName fontName, monoName;
    b32 running;
    b32 needsFrame;
    b32 transparent;  // what the window was last set to
    u8 backdrop;
    b32 vsyncOff;
    u64 startTime;  // cleared once the first frame is presented
    u64 launchTime;
    b32 shown;      // a frame has been presented
    u32 shownFrames;  // frames presented, up to the warm-up after the first
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
    const f32 fontPixelSize = 9.0f * (96.0f / 72.0f);
    Arena arena = {0};
    R_Font font = {0}, detailFont = {0}, icons = {0}, smallIcons = {0}, mediumIcons = {0};
    const Prog_FontName *name = &loop->state.font;
    String family = name->length ? StrMake(name->text, name->length) : Prog_DefaultFont;
    // The families the defaults are drawn in, as the system matches them, for Options to name.
    Scratch scratch = GetScratch();
    Prog_FontName *used[2] = { &loop->state.fontUsed, &loop->state.inspectorFontUsed };
    String asked[2] = { Prog_DefaultFont, Prog_DefaultInspectorFont };
    for (u32 i = 0; i < 2; ++i) {
        String found = PlatformFontFamilyUsed(scratch.arena, asked[i]);
        used[i]->length = (u32)Min(found.length, sizeof(used[i]->text));
        MemCopy(used[i]->text, found.value, used[i]->length);
    }
    ReleaseScratch(scratch);
    // Icons are a little smaller than the text's line height, which gives their strokes the right weight.
    b32 ok = R_FontCreate(&arena, family, fontPixelSize * scale, &font) &&
             R_FontCreate(&arena, family, (fontPixelSize - 96.0f / 72.0f) * scale, &detailFont) &&
             R_FontCreateFromMemory(&arena, Icon_FontData, sizeof(Icon_FontData), floorf(font.lineHeight * 0.92f + 0.5f),
                                    Icon_FirstRune, Icon_FirstRune + Icon_Count - 1, &icons) &&
             R_FontCreateFromMemory(&arena, Icon_FontData, sizeof(Icon_FontData), floorf(font.lineHeight * 0.92f * 0.7f + 0.5f),
                                    Icon_FirstRune, Icon_FirstRune + Icon_Count - 1, &smallIcons) &&
             R_FontCreateFromMemory(&arena, Icon_FontData, sizeof(Icon_FontData), floorf(font.lineHeight * 0.92f * 0.82f + 0.5f),
                                    Icon_FirstRune, Icon_FirstRune + Icon_Count - 1, &mediumIcons);
    if (!ok) {
        R_FontRelease(&font);
        R_FontRelease(&detailFont);
        R_FontRelease(&icons);
        R_FontRelease(&smallIcons);
        R_FontRelease(&mediumIcons);
        ArenaRelease(&arena);
        return false;
    }
    R_FontRelease(&loop->font);
    R_FontRelease(&loop->detailFont);
    R_FontRelease(&loop->icons);
    R_FontRelease(&loop->smallIcons);
    R_FontRelease(&loop->mediumIcons);
    ArenaRelease(&loop->fontArena);
    loop->fontArena = arena;
    loop->font = font;
    loop->detailFont = detailFont;
    loop->state.smallFont = &loop->detailFont;
    loop->icons = icons;
    loop->smallIcons = smallIcons;
    loop->state.smallIcons = &loop->smallIcons;
    loop->mediumIcons = mediumIcons;
    loop->state.mediumIcons = &loop->mediumIcons;
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
        state->wakeAt = 0;
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
                String family = name->length ? StrMake(name->text, name->length) : Prog_DefaultInspectorFont;
                Arena arena = {0};
                R_Font mono = {0};
                if (R_FontCreate(&arena, family, pixels, &mono)) {
                    R_FontRelease(&loop->mono);
                    ArenaRelease(&loop->monoArena);
                    loop->monoArena = arena;
                    loop->mono = mono;
                } else {
                    ArenaRelease(&arena);
                }
            }
            state->monoFont = loop->mono.cache ? &loop->mono : NULL;
        }
        // The Setup page's 20, 14 and 11 point text, in the interface's font, gone again with the page.
        if (state->setupOpen != (loop->setupScale == scale)) {
            for (u32 i = 0; i < CArrayCount(loop->setupFonts); ++i) R_FontRelease(&loop->setupFonts[i]);
            ArenaRelease(&loop->setupArena);
            MemZero(loop->setupFonts, sizeof(loop->setupFonts));
            loop->setupScale = 0;
            if (state->setupOpen) {
                local_persist const f32 points[3] = { 20, 14, 11 };
                String family = state->font.length ? StrMake(state->font.text, state->font.length) : Prog_DefaultFont;
                b32 made = true;
                for (u32 i = 0; i < CArrayCount(points); ++i) made = made && R_FontCreate(&loop->setupArena, family, points[i] * (96.0f / 72.0f) * scale, &loop->setupFonts[i]);
                if (made) loop->setupScale = scale;
            }
        }
        for (u32 i = 0; i < CArrayCount(state->setupFonts); ++i) state->setupFonts[i] = loop->setupScale > 0 ? &loop->setupFonts[i] : NULL;
        // Nothing eases while the window waits to show, so that its first frame has everything in place, as the layout settled unseen.
        state->ui.animate = !state->animationsOff && loop->shown;
        ProfileScope("BuildUI") {
            UI_BeginFrame(&state->ui, &state->frameArena, (R_Rect){ 0, 0, (f32)window.width, (f32)window.height }, events, PlatformNowMicroseconds());
            Prog_HandleInput(state, events);
            // Hotkey commands apply before the UI is built, so this frame already shows them.
            handled = Prog_ProcessCommands(state);
            Prog_BuildUI(state, &loop->font, &loop->icons, scale);
            PlatformSetCursor(state->cursor);
        }
        ProfileScope("Layout") UI_EndFrame();
        ProfileScope("ProcessCommands") loop->needsFrame = Prog_ProcessCommands(state) || handled || completed || events.count != 0 || state->ui.animating;
        // The first frame waits for the folder, the sidebar and their icons, for a moment at most, so that the window opens whole rather
        // than filling in; until then it shows the theme's color.
        loop->shown = loop->shown || Prog_ReadyToShow(state) || PlatformNowMicroseconds() - loop->launchTime > 300000;
        // Until then it waits for the jobs it waits on, which wake it, or for that moment, rather than going round frame after frame.
        if (!loop->shown) Prog_WakeAt(state, loop->launchTime + 300001);
        if (loop->shown) ProfileScope("Render") {
            if (loop->vsyncOff != state->noVSync) {
                loop->vsyncOff = state->noVSync;
                PlatformSetVSync(!loop->vsyncOff);
            }
            if (loop->transparent != state->transparent || loop->backdrop != state->glassBackdrop) {
                loop->transparent = state->transparent;
                loop->backdrop = state->glassBackdrop;
                PlatformWindowSetTransparent(loop->transparent, (PlatformBackdrop)loop->backdrop);
            }
            // The frame after the first one shown draws the popups' layer, blur and shadow once unseen, so that the driver's first-use work
            // for them is done before a popup first opens rather than stalling it.
            if (loop->shownFrames < 2 && loop->shownFrames++ == 1) ProfileScope("WarmUp") {
                R_Rect box = { 0, 0, (f32)Min(window.width, 256), (f32)Min(window.height, 256) };
                R_BeginFrame(window.width, window.height, Prog_CurrentTheme()->window);
                R_DrawSoftRect(box, Prog_CurrentTheme()->window, 8, 8);
                R_PushLayer(box);
                R_BlurBehind(box, 8, 24, 1);
                R_DrawRect(box, Prog_CurrentTheme()->window, 8);
                R_PopLayer(0.5f);
                R_EndFrame();
            }
            // A tab dragged out of the window is drawn on its own first and shown at the pointer as a window over the others.
            UI_Block *ghost = state->tabGhost;
            i32 ghostWidth = ghost ? (i32)(ghost->rect.x1 - ghost->rect.x0) : 0, ghostHeight = ghost ? (i32)(ghost->rect.y1 - ghost->rect.y0) : 0;
            if (ghostWidth > 0 && ghostHeight > 0 && ghostWidth <= window.width && ghostHeight <= window.height) {
                R_BeginFrame(window.width, window.height, (R_Color){0});
                UI_DrawDetached(ghost);
                R_EndFrame();
                Scratch scratch = GetScratch();
                u32 *pixels = ArenaPushArrayNoZero(scratch.arena, (u64)ghostWidth * (u64)ghostHeight, u32);
                R_ReadRect((i32)ghost->rect.x0, (i32)ghost->rect.y0, ghostWidth, ghostHeight, pixels);
                PlatformDragGhost(pixels, ghostWidth, ghostHeight, (i32)state->tabGhostHot.x, (i32)state->tabGhostHot.y);
                ReleaseScratch(scratch);
            } else {
                PlatformDragGhost(NULL, 0, 0, 0, 0);
            }
            R_BeginFrame(window.width, window.height, Prog_CurrentTheme()->window);
            UI_Draw();
            R_EndFrame();
            if (loop->shownFrames == 1) loop->needsFrame = true;
            if (loop->captureDirectory.length) Prog__CaptureFrame(loop, (u32)Max(window.width, 0), (u32)Max(window.height, 0));
        }
        // Includes the wait for vsync.
        if (loop->shown) ProfileScope("SwapBuffers") PlatformSwapBuffers();
    }
    loop->inFrame = false;
    if (loop->startTime && loop->shown) {
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
    loop.launchTime = loop.startTime;
#if defined(PROFILE)
    // 32 MB of events, about an hour of frames.
    if (!P_ProfileInit(UINT64_C(1) << 20)) {
        fprintf(stderr, "Failed to start the profiler\n");
    }
#endif
    b32 ok = false;
    // The window opens at the size the last run left it at, rather than growing to it once the settings are read, and as they make it look.
    Scratch startup = GetScratch();
    String configDirectory = P_ConfigDirectory(startup.arena);
    // Folders given on the command line, as the desktop entry and "Open with" pass them, open in tabs instead of the working directory.
    ArrayStr startPaths = P_StartupPaths(startup.arena);
    // With one window for the settings, a later run hands its folders to the one already open and quits; New Window's runs get their own.
    b32 separateWindow = P_StartupFlag(S("--new-window"));
    // The system's list of installed apps uninstalls through the Filey Setup page, in a window of its own.
    b32 uninstall = P_StartupFlag(S("--uninstall"));
    separateWindow |= uninstall;
    // This PC, which is no folder, comes as a flag from a window that opens another for it.
    if (P_StartupFlag(S("--this-pc"))) ArrayPush(startup.arena, startPaths, Prog_ThisPC);
    // A run an update started takes the window over once the one it replaces has quit and saved its session, and removes the copy it was.
    String after = P_StartupOption(startup.arena, S("--after="));
    u64 replaced = 0;
    if (after.length && Prog__TakeNumber(&after, &replaced)) {
        P_WaitForExit((u32)replaced, 15000);
        P_RemoveReplaced(P_ExecutablePath(startup.arena));
    }
    if (!separateWindow && !Prog_SeparateInstances(configDirectory) && !PlatformInstanceClaim(configDirectory, startPaths)) {
        ReleaseScratch(startup);
        return 0;
    }
    PlatformWindowDesc window = { .title = S("Filey"), .width = 1280, .height = 800 };
    Prog_StartupWindow(configDirectory, &window);
    Prog_SessionWindow(configDirectory, &window);
    // Its first frame finds it as it opened, with nothing to set again.
    loop.transparent = window.transparent;
    loop.backdrop = window.backdrop;
    ProfileScope("OpenWindow") ok = PlatformWindowOpen(window);
    if (!ok) {
        fprintf(stderr, "Failed to open the window\n");
        return 1;
    }
    // A file given, as Explorer's Show in Filey passes one, opens its folder with it selected.
    String first = startPaths.header.count ? startPaths.v[0] : S(""), picked = {0};
    if (first.length && P_PathExists(first) && !P_IsDirectory(first)) picked = Prog__FolderName(first), first = Prog_ParentPath(first);
    ProfileScope("InitProgram") ok = Prog_Init(&loop.state, first, Prog__Wake, NULL);
    if (ok) Prog_LoadSettings(&loop.state, configDirectory, P_CacheDirectory(startup.arena));
    // A crash from here on leaves a report in the cache folder, and one the last run left is offered to be reported.
    if (ok && loop.state.cacheDirectory.length) {
        P_CatchCrashes(Prog_CrashFolder(startup.arena, &loop.state), S(FILEY_VERSION));
        Prog_CheckCrashes(&loop.state);
#if defined(DEBUG)
        // FILEY_TEST_CRASH=1 has a debug build crash as it starts, to try the crash report.
        if (getenv("FILEY_TEST_CRASH")) {
            volatile i32 *nowhere = NULL;
            *nowhere = 1;
        }
#endif
    }
    loop.state.separateWindow = separateWindow;
    if (ok && picked.length) Prog_SelectNext(&loop.state, picked);
    // After the settings, so that these tabs start with their choices.
    for (u64 i = 1; ok && i < startPaths.header.count; ++i) {
        String path = startPaths.v[i];
        if (P_PathExists(path) && !P_IsDirectory(path)) Prog_ShowInFolder(&loop.state, path, true);
        else Prog_OpenTab(&loop.state, path, (u32)i, false);
    }
    if (ok && uninstall) Prog__OpenSetup(&loop.state);
    // Without folders to open, the last run's panels and tabs come back, or the place the settings start from.
    if (ok && !startPaths.header.count) Prog_StartUp(&loop.state);
    // The torrent downloads the last run left unfinished go on, in the main window only.
    if (ok && !separateWindow) Prog_ResumeTorrents(&loop.state);
    // A tab moved out of another window brings its zoom and focused item.
    String traits = ok ? P_StartupOption(startup.arena, S("--tab=")) : (String){0};
    if (traits.length) Prog_ApplyTabTraits(&loop.state, traits);
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
        ProfileScope("PollEvents") events = PlatformPollEvents(!loop.needsFrame, loop.state.wakeAt);
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
    PlatformInstanceRelease();
    // Closed, the window goes at once; the session is saved and the background work stops behind it, which a slow disk can draw out.
    PlatformWindowHide();
    Prog_Release(&loop.state);
    P_ReleaseWindowWake();
    ArenaRelease(&permanent);
#if defined(PROFILE)
    Prog__WriteProfile();
#endif
    return 0;
}
