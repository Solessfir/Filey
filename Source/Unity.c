// Single translation unit for the whole application.

#include "Base/Base.h"
#include "Platform/Platform.h"
#include "Renderer/Renderer.h"

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

// Temporary renderer smoke test until the UI layer exists.
function void DrawRendererDemo(const R_Font *uiFont, const R_Font *monoFont, f32 scale)
{
    R_Color text = { 0.90f, 0.91f, 0.93f, 1.0f };
    R_Color dim = { 0.60f, 0.62f, 0.66f, 1.0f };
    R_Color accent = { 0.29f, 0.53f, 0.91f, 1.0f };

    f32 x = 24.0f * scale;
    f32 y = 24.0f * scale;
    R_DrawText(uiFont, x, y, text, S("Filey - the quick brown fox jumps over the lazy dog"));
    y += uiFont->lineHeight;
    R_DrawText(uiFont, x, y, dim, S("Ångström façade naïve Ťažký 0123456789 !@#$%^&*()"));
    y += uiFont->lineHeight;
    R_DrawText(uiFont, x, y, dim, S("Съешь же ещё этих мягких французских булок, да выпей чаю. Ёжик, подъезд, щука"));
    y += uiFont->lineHeight;
    R_DrawText(monoFont, x, y, dim, S("int main(void) { return 0; } // monospace"));
    y += monoFont->lineHeight + 16.0f * scale;

    String label = S("Rounded button");
    f32 padding = 12.0f * scale;
    R_Rect button = { x, y, x + R_TextWidth(uiFont, label) + 2.0f * padding, y + uiFont->lineHeight + padding };
    R_DrawRect(button, accent, 6.0f * scale);
    R_DrawText(uiFont, button.x0 + padding, button.y0 + 0.5f * padding, text, label);

    R_Rect outline = { button.x1 + 16.0f * scale, button.y0, button.x1 + 176.0f * scale, button.y1 };
    R_DrawRectOutline(outline, dim, 6.0f * scale, 1.0f * scale);
    R_PushClip((R_Rect){ outline.x0, outline.y0, outline.x0 + 0.5f * (outline.x1 - outline.x0), outline.y1 });
    R_DrawText(uiFont, outline.x0 + padding, outline.y0 + 0.5f * padding, text, S("Clipped in half"));
    R_PopClip();
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
    R_Font uiFont = {0};
    R_Font monoFont = {0};
    PlatformWindowInfo startWindow = PlatformWindowGetInfo();
    if (!R_FontCreate(&permanent, S("Segoe UI"), 15.0f * startWindow.scale, &uiFont) ||
        !R_FontCreate(&permanent, S("Consolas"), 14.0f * startWindow.scale, &monoFont)) {
        fprintf(stderr, "Failed to load fonts\n");
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
        DrawRendererDemo(&uiFont, &monoFont, window.scale);
        R_EndFrame();
        PlatformSwapBuffers();
    }

    return 0;
}
