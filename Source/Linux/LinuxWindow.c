// Wayland window, input and EGL context. Requires LinuxPlatform.c.

#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <linux/input-event-codes.h>

// wl_registry_listener has a member named `global`, which collides with the base macro.
#pragma push_macro("global")
#undef global
#include <EGL/egl.h>
#include <wayland-client.h>
#include <wayland-cursor.h>
#include <wayland-egl.h>
#include <xkbcommon/xkbcommon.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#include "Linux/Protocols/XdgShell.generated.h"
#include "Linux/Protocols/XdgShell.generated.c"
#include "Linux/Protocols/FractionalScale.generated.h"
#include "Linux/Protocols/FractionalScale.generated.c"
#include "Linux/Protocols/Viewporter.generated.h"
#include "Linux/Protocols/Viewporter.generated.c"
#include "Linux/Protocols/XdgActivation.generated.h"
#include "Linux/Protocols/XdgActivation.generated.c"
#pragma GCC diagnostic pop
#pragma pop_macro("global")

typedef struct LinuxWindowState {
    struct wl_display    *display;
    struct wl_registry   *registry;
    struct wl_compositor *compositor;
    struct wl_shm        *shm;
    struct wl_seat       *seat;
    // Clipboard. Offers are tracked from their first event, so the one that becomes the selection knows whether it has text.
    struct wl_data_device_manager *dataManager;
    struct wl_data_device *dataDevice;
    struct wl_data_offer *newOffer;
    const char *newOfferMime;
    const char *newOfferFiles;
    b32 newOfferUris;
    struct wl_data_offer *selection;
    const char *selectionMime;
    const char *selectionFiles;
    struct wl_data_offer *dragOffer;
    const char *dragFiles;    // the file list type the offer under the pointer has
    u32 dragSerial;
    u32 dragAction;           // what the compositor settled on for that offer
    struct wl_data_source *source;
    // A drag that Filey started; its own drops read the list from here instead of through a pipe this thread would have to fill.
    struct wl_data_source *dragSource;
    // A tab dragged out of the window: the drag the compositor runs for it, and its icon, the program's picture of the tab in a buffer of
    // its own, which the compositor moves with the pointer.
    struct wl_data_source *tabSource;
    struct wl_surface *tabIcon;
    struct wp_viewport *tabViewport;
    struct wl_buffer *tabBuffer;
    u32 *tabPixels;
    i32 tabWidth, tabHeight;
    b32 tabShown;
    Arena tabArena;
    String tabText;      // the tab's path and traits, which the window it is dropped on reads
    b32 tabTargeted;     // another window takes it where it is
    b32 tabTaken;        // another window took it
    b32 newOfferTab;     // the offer being made is another window's tab
    b32 foreignTab;      // the drag over the window is another window's tab
    Arena dragArena;
    String dragList;
    String dragFolder;
    u32 pointerSerial;        // of the latest button press, which starting a drag needs
    u32 enterSerial;          // of the pointer's arrival, which setting its shape needs; 0 while it is elsewhere
    PlatformCursor cursor;
    Arena clipboardArena;     // what Filey offers while it owns the selection
    u32 clipboardSequence;    // up one each time Filey offers something and each time another program takes the selection from it
    const char *clipboardMimes[8];
    String clipboardData[8];
    u32 clipboardCount;
    u32 inputSerial;          // of the latest key or button press, which setting the selection needs
    struct xdg_wm_base   *wmBase;
    // Fractional scaling: the buffer is drawn at the exact scale and the viewport maps it onto the logical size.
    // Without them the compositor gets an integer buffer scale.
    struct wp_fractional_scale_manager_v1 *fractionalManager;
    struct wp_viewporter *viewporter;
    struct xdg_activation_v1 *activation;  // brings the window forward with a token a later run's launcher gave it
    i32 instanceFd;                         // the socket later runs hand their folders to, -1 for none
    struct wp_fractional_scale_v1 *fractional;
    struct wp_viewport *viewport;
    u32 compositorVersion;

    struct wl_surface    *surface;
    struct xdg_surface   *xdgSurface;
    struct xdg_toplevel  *toplevel;
    struct wl_egl_window *eglWindow;

    EGLDisplay eglDisplay;
    EGLContext eglContext;
    EGLSurface eglSurface;

    struct wl_keyboard *keyboard;
    struct xkb_context *xkb;
    struct xkb_keymap  *keymap;
    struct xkb_state   *xkbState;
    KeyMods mods;

    struct wl_pointer       *pointer;
    struct wl_cursor_theme  *cursorTheme;
    struct wl_surface       *cursorSurface;
    i32 cursorThemeScale;     // cursors only take integer scales
    f32 pointerX;
    f32 pointerY;

    // Wayland leaves key repeat to the client.
    i32 repeatRate;      // keys per second, 0 disables repeat
    i32 repeatDelay;     // milliseconds
    b32 repeating;
    u32 repeatKeycode;
    u64 repeatNextUs;

    i32 width;           // logical (surface) size
    i32 height;
    i32 pendingWidth;
    i32 pendingHeight;
    f32 scale;           // physical / logical, fractional when the compositor supports it
    f32 rounding;        // logical pixels the compositor rounds the window's corners by
    b32 configured;
    b32 closed;
    b32 closeRequested;  // the compositor asked the window to close
    // Not visible at all, for example on a hidden workspace, so nothing is drawn until that changes.
    b32 suspended;
    b32 maximized;
    b32 fullscreen;
    // Tiling compositors cannot minimize; without that the window draws no buttons of its own.
    b32 noMinimize;
    // Frame pacing: the next frame waits for the compositor's callback for the last one, or for the deadline when it never comes.
    struct wl_callback *frameCallback;
    u64 frameDeadline;
    // A new size draws at once rather than after the callback, which comes only once the compositor has shown the old frame at the new size;
    // one such early frame per callback keeps a resize from drawing at the pointer's rate.
    b32 resizePending;
    b32 earlyFrame;
    i32 wakeFd;
} LinuxWindowState;

global LinuxWindowState Wl;

// -----------------------------------------------------------------------------
// Keyboard
// -----------------------------------------------------------------------------

// Physical positions for letters and digits, used when the active layout has no Latin symbol on the key (e.g. Cyrillic) so shortcuts still work.
global const Key LinuxEvdevKeys[] = {
    [KEY_A] = Key_A, [KEY_B] = Key_B, [KEY_C] = Key_C, [KEY_D] = Key_D, [KEY_E] = Key_E,
    [KEY_F] = Key_F, [KEY_G] = Key_G, [KEY_H] = Key_H, [KEY_I] = Key_I, [KEY_J] = Key_J,
    [KEY_K] = Key_K, [KEY_L] = Key_L, [KEY_M] = Key_M, [KEY_N] = Key_N, [KEY_O] = Key_O,
    [KEY_P] = Key_P, [KEY_Q] = Key_Q, [KEY_R] = Key_R, [KEY_S] = Key_S, [KEY_T] = Key_T,
    [KEY_U] = Key_U, [KEY_V] = Key_V, [KEY_W] = Key_W, [KEY_X] = Key_X, [KEY_Y] = Key_Y,
    [KEY_Z] = Key_Z,
    [KEY_0] = Key_0, [KEY_1] = Key_1, [KEY_2] = Key_2, [KEY_3] = Key_3, [KEY_4] = Key_4,
    [KEY_5] = Key_5, [KEY_6] = Key_6, [KEY_7] = Key_7, [KEY_8] = Key_8, [KEY_9] = Key_9,
};

function Key LinuxKeyFromKeysym(xkb_keysym_t sym)
{
    if (sym >= XKB_KEY_a && sym <= XKB_KEY_z) return Key_A + (Key)(sym - XKB_KEY_a);
    if (sym >= XKB_KEY_A && sym <= XKB_KEY_Z) return Key_A + (Key)(sym - XKB_KEY_A);
    if (sym >= XKB_KEY_0 && sym <= XKB_KEY_9) return Key_0 + (Key)(sym - XKB_KEY_0);
    if (sym >= XKB_KEY_F1 && sym <= XKB_KEY_F12) return Key_F1 + (Key)(sym - XKB_KEY_F1);
    if (sym >= XKB_KEY_KP_0 && sym <= XKB_KEY_KP_9) return Key_Keypad0 + (Key)(sym - XKB_KEY_KP_0);

    switch (sym) {
        case XKB_KEY_Escape:    return Key_Escape;
        case XKB_KEY_Return:
        case XKB_KEY_KP_Enter:  return Key_Enter;
        case XKB_KEY_Tab:
        case XKB_KEY_ISO_Left_Tab: return Key_Tab;
        case XKB_KEY_BackSpace: return Key_Backspace;
        case XKB_KEY_Delete:    return Key_Delete;
        case XKB_KEY_Insert:    return Key_Insert;
        case XKB_KEY_space:     return Key_Space;
        case XKB_KEY_Left:      return Key_Left;
        case XKB_KEY_Right:     return Key_Right;
        case XKB_KEY_Up:        return Key_Up;
        case XKB_KEY_Down:      return Key_Down;
        case XKB_KEY_Home:      return Key_Home;
        case XKB_KEY_End:       return Key_End;
        case XKB_KEY_Prior:     return Key_PageUp;
        case XKB_KEY_Next:      return Key_PageDown;
        case XKB_KEY_Shift_L:
        case XKB_KEY_Shift_R:   return Key_Shift;
        case XKB_KEY_Control_L:
        case XKB_KEY_Control_R: return Key_Control;
        case XKB_KEY_Alt_L:
        case XKB_KEY_Alt_R:     return Key_Alt;
        case XKB_KEY_Super_L:
        case XKB_KEY_Super_R:   return Key_Super;
        case XKB_KEY_Menu:      return Key_ContextMenu;
        case XKB_KEY_comma:     return Key_Comma;
        // With Shift held these come as the shifted symbols.
        case XKB_KEY_equal:     return Key_Equals;
        case XKB_KEY_plus:      return Key_Equals;
        case XKB_KEY_minus:     return Key_Minus;
        case XKB_KEY_underscore: return Key_Minus;
        case XKB_KEY_slash:
        case XKB_KEY_question:
        case XKB_KEY_KP_Divide: return Key_Slash;
        default:                return Key_None;
    }
}

function Key LinuxKeyFromKeycode(u32 keycode)
{
    // The keypad's digits are its second level, which Num Lock picks.
    xkb_keysym_t current = xkb_state_key_get_one_sym(Wl.xkbState, keycode);
    if (current >= XKB_KEY_KP_0 && current <= XKB_KEY_KP_9) return LinuxKeyFromKeysym(current);
    // The unshifted symbol, so Shift+1 still reports Key_1.
    xkb_layout_index_t layout = xkb_state_key_get_layout(Wl.xkbState, keycode);
    const xkb_keysym_t *syms = NULL;
    Key key = Key_None;
    if (xkb_keymap_key_get_syms_by_level(Wl.keymap, keycode, layout, 0, &syms) > 0) {
        key = LinuxKeyFromKeysym(syms[0]);
    }

    u32 evdev = keycode - 8;
    if (key == Key_None && evdev < CArrayCount(LinuxEvdevKeys)) {
        key = LinuxEvdevKeys[evdev];
    }
    return key;
}

function void LinuxPushKey(u32 keycode, b32 down, b32 isRepeat)
{
    Key key = LinuxKeyFromKeycode(keycode);
    if (key != Key_None) {
        PlatformEvent *event = PlatformPushEvent(down ? PlatformEventKind_KeyDown : PlatformEventKind_KeyUp);
        event->key = key;
        event->mods = Wl.mods;
        event->isRepeat = (b8)isRepeat;
    }

    // Shortcut chords never produce text, matching Win32's WM_CHAR behavior.
    if (down && (Wl.mods & (KeyMod_Control | KeyMod_Alt | KeyMod_Super)) == 0) {
        rune codepoint = (rune)xkb_state_key_get_utf32(Wl.xkbState, keycode);
        if (codepoint >= 32 && codepoint != 127) {
            PlatformEvent *event = PlatformPushEvent(PlatformEventKind_Text);
            event->codepoint = codepoint;
            event->mods = Wl.mods;
        }
    }
}

function void KeyboardKeymap(void *data, struct wl_keyboard *keyboard, u32 format, i32 fd, u32 size)
{
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        close(fd);
        return;
    }

    char *text = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (text == MAP_FAILED) {
        return;
    }

    struct xkb_keymap *keymap = xkb_keymap_new_from_string(Wl.xkb, text, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(text, size);
    if (keymap == NULL) {
        return;
    }

    xkb_state_unref(Wl.xkbState);
    xkb_keymap_unref(Wl.keymap);
    Wl.keymap = keymap;
    Wl.xkbState = xkb_state_new(keymap);
}

function void KeyboardEnter(void *data, struct wl_keyboard *keyboard, u32 serial, struct wl_surface *surface, struct wl_array *keys)
{
    PlatformEvent *event = PlatformPushEvent(PlatformEventKind_FocusChanged);
    event->focused = true;
}

function void KeyboardLeave(void *data, struct wl_keyboard *keyboard, u32 serial, struct wl_surface *surface)
{
    Wl.repeating = false;
    // Compositors such as Hyprland take the keyboard away while a tab's drag runs, which would end the drag the window still holds.
    if (Wl.tabSource) return;
    PlatformEvent *event = PlatformPushEvent(PlatformEventKind_FocusChanged);
    event->focused = false;
}

function void KeyboardKey(void *data, struct wl_keyboard *keyboard, u32 serial, u32 time, u32 key, u32 state)
{
    if (Wl.xkbState == NULL) {
        return;
    }

    u32 keycode = key + 8;
    b32 down = state == WL_KEYBOARD_KEY_STATE_PRESSED;
    if (down) Wl.inputSerial = serial;
    LinuxPushKey(keycode, down, false);

    if (down && Wl.repeatRate > 0 && xkb_keymap_key_repeats(Wl.keymap, keycode)) {
        Wl.repeating = true;
        Wl.repeatKeycode = keycode;
        Wl.repeatNextUs = PlatformNowMicroseconds() + (u64)Wl.repeatDelay * Thousands(1);
    } else if (!down && keycode == Wl.repeatKeycode) {
        Wl.repeating = false;
    }
}

function void KeyboardModifiers(void *data, struct wl_keyboard *keyboard, u32 serial, u32 depressed, u32 latched, u32 locked, u32 group)
{
    if (Wl.xkbState == NULL) {
        return;
    }

    xkb_state_update_mask(Wl.xkbState, depressed, latched, locked, 0, 0, group);

    Wl.mods = 0;
    if (xkb_state_mod_name_is_active(Wl.xkbState, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE) > 0) Wl.mods |= KeyMod_Shift;
    if (xkb_state_mod_name_is_active(Wl.xkbState, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0)  Wl.mods |= KeyMod_Control;
    if (xkb_state_mod_name_is_active(Wl.xkbState, XKB_MOD_NAME_ALT, XKB_STATE_MODS_EFFECTIVE) > 0)   Wl.mods |= KeyMod_Alt;
    if (xkb_state_mod_name_is_active(Wl.xkbState, XKB_MOD_NAME_LOGO, XKB_STATE_MODS_EFFECTIVE) > 0)  Wl.mods |= KeyMod_Super;
}

function void KeyboardRepeatInfo(void *data, struct wl_keyboard *keyboard, i32 rate, i32 delay)
{
    Wl.repeatRate = rate;
    Wl.repeatDelay = delay;
}

global const struct wl_keyboard_listener KeyboardListener = {
    .keymap      = KeyboardKeymap,
    .enter       = KeyboardEnter,
    .leave       = KeyboardLeave,
    .key         = KeyboardKey,
    .modifiers   = KeyboardModifiers,
    .repeat_info = KeyboardRepeatInfo,
};

function void LinuxGenerateRepeats(void)
{
    if (!Wl.repeating) {
        return;
    }

    u64 now = PlatformNowMicroseconds();
    u64 interval = Millions(1) / (u64)Wl.repeatRate;

    // After a long stall, resume repeating instead of replaying a burst.
    if (now > Wl.repeatNextUs + Millions(1)) {
        Wl.repeatNextUs = now;
    }

    while (now >= Wl.repeatNextUs) {
        LinuxPushKey(Wl.repeatKeycode, true, true);
        Wl.repeatNextUs += interval;
    }
}

// -----------------------------------------------------------------------------
// Pointer
// -----------------------------------------------------------------------------

function void LinuxSetCursor(u32 serial)
{
    // Themes are rasterized per size, so reload when the scale changes.
    i32 cursorScale = (i32)Wl.scale + (Wl.scale > (f32)(i32)Wl.scale);
    if (Wl.cursorTheme == NULL || Wl.cursorThemeScale != cursorScale) {
        if (Wl.cursorTheme != NULL) {
            wl_cursor_theme_destroy(Wl.cursorTheme);
        }
        const char *sizeText = getenv("XCURSOR_SIZE");
        i32 size = sizeText != NULL ? atoi(sizeText) : 0;
        Wl.cursorTheme = wl_cursor_theme_load(getenv("XCURSOR_THEME"), (size > 0 ? size : 24) * cursorScale, Wl.shm);
        Wl.cursorThemeScale = cursorScale;
    }
    if (Wl.cursorTheme == NULL) {
        return;
    }

    // The freedesktop names first, then the older X ones that some themes still use alone.
    local_persist const char *names[PlatformCursor_Count][3] = {
        [PlatformCursor_Arrow] = { "default", "left_ptr" },
        [PlatformCursor_ResizeHorizontal] = { "ew-resize", "col-resize", "sb_h_double_arrow" },
        [PlatformCursor_ResizeVertical] = { "ns-resize", "row-resize", "sb_v_double_arrow" },
        [PlatformCursor_Text] = { "text", "xterm" },
    };
    struct wl_cursor *cursor = NULL;
    for (u32 i = 0; i < 3 && !cursor && names[Wl.cursor][i]; ++i) cursor = wl_cursor_theme_get_cursor(Wl.cursorTheme, names[Wl.cursor][i]);
    if (cursor == NULL || cursor->image_count == 0) {
        return;
    }

    struct wl_cursor_image *image = cursor->images[0];
    wl_surface_set_buffer_scale(Wl.cursorSurface, cursorScale);
    wl_surface_attach(Wl.cursorSurface, wl_cursor_image_get_buffer(image), 0, 0);
    wl_surface_damage(Wl.cursorSurface, 0, 0, INT32_MAX, INT32_MAX);
    wl_surface_commit(Wl.cursorSurface);
    wl_pointer_set_cursor(Wl.pointer, serial, Wl.cursorSurface,
                          (i32)image->hotspot_x / cursorScale, (i32)image->hotspot_y / cursorScale);
}

function void PlatformSetCursor(PlatformCursor cursor)
{
    if (cursor == Wl.cursor || cursor >= PlatformCursor_Count) return;
    Wl.cursor = cursor;
    if (Wl.enterSerial) LinuxSetCursor(Wl.enterSerial);
}

function void PointerEnter(void *data, struct wl_pointer *pointer, u32 serial, struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y)
{
    Wl.enterSerial = serial;
    LinuxSetCursor(serial);
    Wl.pointerX = (f32)(wl_fixed_to_double(x) * Wl.scale);
    Wl.pointerY = (f32)(wl_fixed_to_double(y) * Wl.scale);
}

function void PointerLeave(void *data, struct wl_pointer *pointer, u32 serial, struct wl_surface *surface)
{
    Wl.enterSerial = 0;
    PlatformEvent *event = PlatformPushEvent(PlatformEventKind_MouseLeave);
    event->mods = Wl.mods;
}

function void PointerMotion(void *data, struct wl_pointer *pointer, u32 time, wl_fixed_t x, wl_fixed_t y)
{
    Wl.pointerX = (f32)(wl_fixed_to_double(x) * Wl.scale);
    Wl.pointerY = (f32)(wl_fixed_to_double(y) * Wl.scale);

    PlatformEvent *event = PlatformPushEvent(PlatformEventKind_MouseMove);
    event->x = Wl.pointerX;
    event->y = Wl.pointerY;
    event->mods = Wl.mods;
}

function void PointerButton(void *data, struct wl_pointer *pointer, u32 serial, u32 time, u32 button, u32 state)
{
    MouseButton mouseButton = 0;
    switch (button) {
        case BTN_LEFT:   mouseButton = MouseButton_Left; break;
        case BTN_RIGHT:  mouseButton = MouseButton_Right; break;
        case BTN_MIDDLE: mouseButton = MouseButton_Middle; break;
        case BTN_SIDE:
        case BTN_EXTRA: {
            PlatformEvent *event = PlatformPushEvent(state == WL_POINTER_BUTTON_STATE_PRESSED ? PlatformEventKind_KeyDown : PlatformEventKind_KeyUp);
            event->key = button == BTN_SIDE ? Key_MouseX1 : Key_MouseX2;
            event->mods = Wl.mods;
        } return;
        default:         return;
    }

    b32 down = state == WL_POINTER_BUTTON_STATE_PRESSED;
    if (down) Wl.inputSerial = Wl.pointerSerial = serial;
    PlatformEvent *event = PlatformPushEvent(down ? PlatformEventKind_MouseDown : PlatformEventKind_MouseUp);
    event->button = mouseButton;
    event->x = Wl.pointerX;
    event->y = Wl.pointerY;
    event->mods = Wl.mods;
}

function void PointerAxis(void *data, struct wl_pointer *pointer, u32 time, u32 axis, wl_fixed_t value)
{
    // ponytail: 10 surface units per wheel notch is the common compositor default; switch to axis_value120 (wl_pointer v8) for exact notches.
    f32 notches = (f32)(wl_fixed_to_double(value) / 10.0);

    PlatformEvent *event = PlatformPushEvent(PlatformEventKind_MouseWheel);
    event->x = Wl.pointerX;
    event->y = Wl.pointerY;
    event->mods = Wl.mods;
    // Wayland's vertical axis grows downwards; ours is positive for scrolling up.
    if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) {
        event->wheelY = -notches;
    } else {
        event->wheelX = notches;
    }
}

function void PointerFrame(void *data, struct wl_pointer *pointer) {}
function void PointerAxisSource(void *data, struct wl_pointer *pointer, u32 source) {}
function void PointerAxisStop(void *data, struct wl_pointer *pointer, u32 time, u32 axis) {}
function void PointerAxisDiscrete(void *data, struct wl_pointer *pointer, u32 axis, i32 discrete) {}

global const struct wl_pointer_listener PointerListener = {
    .enter         = PointerEnter,
    .leave         = PointerLeave,
    .motion        = PointerMotion,
    .button        = PointerButton,
    .axis          = PointerAxis,
    .frame         = PointerFrame,
    .axis_source   = PointerAxisSource,
    .axis_stop     = PointerAxisStop,
    .axis_discrete = PointerAxisDiscrete,
};

// -----------------------------------------------------------------------------
// Seat, shell and surface
// -----------------------------------------------------------------------------

function void SeatCapabilities(void *data, struct wl_seat *seat, u32 capabilities)
{
    if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && Wl.keyboard == NULL) {
        Wl.keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(Wl.keyboard, &KeyboardListener, NULL);
    }
    if ((capabilities & WL_SEAT_CAPABILITY_POINTER) && Wl.pointer == NULL) {
        Wl.pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(Wl.pointer, &PointerListener, NULL);
    }
}

function void SeatName(void *data, struct wl_seat *seat, const char *name) {}

global const struct wl_seat_listener SeatListener = {
    .capabilities = SeatCapabilities,
    .name         = SeatName,
};

// Text types in the order they are preferred.
global const char *const LinuxTextMimes[] = { "text/plain;charset=utf-8", "UTF8_STRING", "text/plain" };

// File lists, preferred first: GNOME's says whether the files were cut.
global const char *const LinuxFileMimes[] = { "x-special/gnome-copied-files", "text/uri-list" };

// What a tab dragged out of the window is offered as, which only Filey's windows take: its path and traits, a tab apart.
global const char LinuxTabMime[] = "application/x-filey-tab";

function void DataOfferOffer(void *data, struct wl_data_offer *offer, const char *mime)
{
    if (offer != Wl.newOffer) return;
    for (u32 i = 0; i < CArrayCount(LinuxFileMimes); ++i) {
        if (strcmp(mime, LinuxFileMimes[i]) == 0 && (!Wl.newOfferFiles || i == 0)) Wl.newOfferFiles = LinuxFileMimes[i];
    }
    if (strcmp(mime, "text/uri-list") == 0) Wl.newOfferUris = true;
    if (strcmp(mime, LinuxTabMime) == 0) Wl.newOfferTab = true;
    for (u32 i = 0; i < CArrayCount(LinuxTextMimes); ++i) {
        if (strcmp(mime, LinuxTextMimes[i]) != 0) continue;
        // Keeps the most preferred type offered.
        b32 better = !Wl.newOfferMime;
        for (u32 j = 0; j < CArrayCount(LinuxTextMimes) && !better; ++j) {
            if (Wl.newOfferMime == LinuxTextMimes[j]) better = i < j;
        }
        if (better) Wl.newOfferMime = LinuxTextMimes[i];
    }
}

function void DataOfferSourceActions(void *data, struct wl_data_offer *offer, u32 actions) {}
function void DataOfferAction(void *data, struct wl_data_offer *offer, u32 action)
{
    if (offer == Wl.dragOffer) Wl.dragAction = action;
}

global const struct wl_data_offer_listener DataOfferListener = {
    .offer = DataOfferOffer,
    .source_actions = DataOfferSourceActions,
    .action = DataOfferAction,
};

function void DataDeviceDataOffer(void *data, struct wl_data_device *device, struct wl_data_offer *offer)
{
    Wl.newOffer = offer;
    Wl.newOfferMime = NULL;
    Wl.newOfferFiles = NULL;
    Wl.newOfferUris = false;
    Wl.newOfferTab = false;
    wl_data_offer_add_listener(offer, &DataOfferListener, NULL);
}

function void LinuxEndTabDrag(b32 release);

// Another window's tab over this one, as a TabHover event, which makes room for it; it is taken, to open here.
function void LinuxTabHover(b32 over, wl_fixed_t x, wl_fixed_t y)
{
    PlatformEvent *event = PlatformPushEvent(PlatformEventKind_TabHover);
    event->focused = (b8)(over != 0);
    event->x = over ? (f32)(wl_fixed_to_double(x) * Wl.scale) : 0;
    event->y = over ? (f32)(wl_fixed_to_double(y) * Wl.scale) : 0;
    if (over && Wl.dragOffer) {
        wl_data_offer_accept(Wl.dragOffer, Wl.dragSerial, LinuxTabMime);
        if (wl_data_offer_get_version(Wl.dragOffer) >= 3) wl_data_offer_set_actions(Wl.dragOffer, WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE, WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE);
    }
}

// Pointer events stop while a drag is over the window, so drag motion moves the program's pointer to keep the folder under it highlighted.
function void LinuxDragPointer(wl_fixed_t x, wl_fixed_t y)
{
    Wl.pointerX = (f32)(wl_fixed_to_double(x) * Wl.scale);
    Wl.pointerY = (f32)(wl_fixed_to_double(y) * Wl.scale);
    PlatformEvent *event = PlatformPushEvent(PlatformEventKind_MouseMove);
    event->x = Wl.pointerX;
    event->y = Wl.pointerY;
    event->mods = Wl.mods;
}

// Files are taken as uri-lists, copied by default and moved when the source or the user asks for it.
// Filey's own drags move, as the drop then decides by the keys held.
function void LinuxAcceptDrag(void)
{
    if (!Wl.dragOffer) return;
    // Pinning folders takes their paths only, so it asks for a copy, which leaves the source alone.
    b32 welcome = Wl.dragFiles && (P__drop.length || P__drop.takesFolders);
    wl_data_offer_accept(Wl.dragOffer, Wl.dragSerial, welcome ? Wl.dragFiles : NULL);
    if (wl_data_offer_get_version(Wl.dragOffer) >= 3) {
        u32 actions = !welcome ? 0 : P__drop.takesFolders ? WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY : WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY | WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE;
        u32 preferred = !actions ? 0 : Wl.dragSource && !P__drop.takesFolders ? WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE : WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY;
        wl_data_offer_set_actions(Wl.dragOffer, actions, preferred);
    }
}

function void DataDeviceEnter(void *data, struct wl_data_device *device, u32 serial, struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y, struct wl_data_offer *offer)
{
    if (Wl.dragOffer) wl_data_offer_destroy(Wl.dragOffer);
    Wl.dragOffer = offer;
    P__drop.pending = offer != NULL;
    Wl.dragSerial = serial;
    Wl.dragAction = 0;
    Wl.dragFiles = offer && offer == Wl.newOffer ? (Wl.newOfferUris ? "text/uri-list" : Wl.newOfferFiles) : NULL;
    // The window's own tab, back over it, is no file drop; its moves reach the program as the pointer's, and it is taken, to be let go here.
    Wl.foreignTab = !Wl.tabSource && offer && offer == Wl.newOffer && Wl.newOfferTab;
    if ((Wl.tabSource || Wl.foreignTab) && offer) {
        P__drop.pending = false;
        wl_data_offer_accept(offer, serial, LinuxTabMime);
        if (wl_data_offer_get_version(offer) >= 3) wl_data_offer_set_actions(offer, WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE, WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE);
        if (Wl.foreignTab) LinuxTabHover(true, x, y);
        else LinuxDragPointer(x, y);
        return;
    }
    LinuxDragPointer(x, y);
    LinuxAcceptDrag();
}

function void DataDeviceLeave(void *data, struct wl_data_device *device)
{
    if (Wl.foreignTab) {
        LinuxTabHover(false, 0, 0);
        Wl.foreignTab = false;
    }
    // A tab's drag that leaves the window is outside it until it comes back, where a drop opens a window of its own.
    if (Wl.tabSource) {
        Wl.pointerX = Wl.pointerY = -1;
        PlatformEvent *event = PlatformPushEvent(PlatformEventKind_MouseMove);
        event->x = event->y = -1;
        event->mods = Wl.mods;
    }
    if (Wl.dragOffer) wl_data_offer_destroy(Wl.dragOffer);
    Wl.dragOffer = NULL;
    P__drop.pending = false;
    Wl.dragFiles = NULL;
}

function void DataDeviceMotion(void *data, struct wl_data_device *device, u32 time, wl_fixed_t x, wl_fixed_t y)
{
    if (Wl.foreignTab) {
        LinuxTabHover(true, x, y);
        return;
    }
    LinuxDragPointer(x, y);
    if (Wl.tabSource) return;
    // The folder under the pointer changes as it moves, so whether a drop is welcome does too.
    LinuxAcceptDrag();
}

function String LinuxClipboardReceive(Arena *arena, const char *mime);
function String LinuxReceive(Arena *arena, struct wl_data_offer *offer, const char *mime);

// Filey's own drops follow Explorer's keys, which compositors such as Hyprland do not pass on as the drop's action:
// Ctrl copies, Shift moves, and otherwise files move within their file system and are copied onto another.
function b32 LinuxOwnDropMoves(String target)
{
    if (Wl.mods & KeyMod_Control) return false;
    if (Wl.mods & KeyMod_Shift) return true;
    Scratch scratch = GetScratch();
    struct stat from = {0}, to = {0};
    char *source = P__FilePathCString(scratch.arena, Wl.dragFolder), *destination = P__FilePathCString(scratch.arena, target);
    b32 result = source && destination && stat(source, &from) == 0 && stat(destination, &to) == 0 && from.st_dev == to.st_dev;
    ReleaseScratch(scratch);
    return result;
}

function void DataDeviceDrop(void *data, struct wl_data_device *device)
{
    // Another window's tab opens here, where the hover made room for it, and that window lets it go.
    if (Wl.foreignTab && Wl.dragOffer) {
        Scratch scratch = GetScratch();
        String tab = LinuxReceive(scratch.arena, Wl.dragOffer, LinuxTabMime);
        if (wl_data_offer_get_version(Wl.dragOffer) >= 3) wl_data_offer_finish(Wl.dragOffer);
        if (tab.length) PlatformPushEventText(PlatformEventKind_OpenPaths, tab);
        ReleaseScratch(scratch);
        Wl.foreignTab = false;
        if (Wl.dragOffer) wl_data_offer_destroy(Wl.dragOffer);
        Wl.dragOffer = NULL;
        return;
    }
    if (Wl.tabSource && Wl.dragOffer) {
        if (wl_data_offer_get_version(Wl.dragOffer) >= 3) wl_data_offer_finish(Wl.dragOffer);
        LinuxEndTabDrag(true);
        DataDeviceLeave(data, device);
        return;
    }
    if (Wl.dragOffer && Wl.dragFiles && P__drop.takesFolders) {
        Scratch scratch = GetScratch();
        String list = Wl.dragSource ? Wl.dragList : LinuxReceive(scratch.arena, Wl.dragOffer, Wl.dragFiles);
        ArrayStr paths = {0};
        b32 cut = false;
        P__ParseFileList(scratch.arena, list, strcmp(Wl.dragFiles, LinuxFileMimes[0]) == 0, &paths, &cut);
        P__KeepDroppedFolders(&paths);
        if (wl_data_offer_get_version(Wl.dragOffer) >= 3) wl_data_offer_finish(Wl.dragOffer);
        ReleaseScratch(scratch);
    } else if (Wl.dragOffer && Wl.dragFiles && P__drop.length) {
        Scratch scratch = GetScratch();
        String folder = StrMake(P__drop.path, P__drop.length);
        b32 own = Wl.dragSource != NULL;
        String list = own ? Wl.dragList : LinuxReceive(scratch.arena, Wl.dragOffer, Wl.dragFiles);
        ArrayStr paths = {0};
        b32 cut = false;
        P__ParseFileList(scratch.arena, list, strcmp(Wl.dragFiles, LinuxFileMimes[0]) == 0, &paths, &cut);
        b32 move = own ? LinuxOwnDropMoves(folder) : Wl.dragAction == WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE;
        // Filey's own files moved back where they are stay put; copied there, they are duplicated, as Explorer's Ctrl+drag does.
        if (!(own && move && StrIsEqual(folder, Wl.dragFolder))) P__KeepDroppedFiles(&paths, folder, move);
        if (wl_data_offer_get_version(Wl.dragOffer) >= 3) wl_data_offer_finish(Wl.dragOffer);
        ReleaseScratch(scratch);
    }
    DataDeviceLeave(data, device);
}

function void DataDeviceSelection(void *data, struct wl_data_device *device, struct wl_data_offer *offer)
{
    if (Wl.selection) wl_data_offer_destroy(Wl.selection);
    Wl.selection = offer;
    Wl.selectionMime = offer && offer == Wl.newOffer ? Wl.newOfferMime : NULL;
    Wl.selectionFiles = offer && offer == Wl.newOffer ? Wl.newOfferFiles : NULL;
}

global const struct wl_data_device_listener DataDeviceListener = {
    .data_offer = DataDeviceDataOffer,
    .enter = DataDeviceEnter,
    .leave = DataDeviceLeave,
    .motion = DataDeviceMotion,
    .drop = DataDeviceDrop,
    .selection = DataDeviceSelection,
};

// Whether the window under a tab's drag takes it, which outside this window is another of the program's windows.
function void DataSourceTarget(void *data, struct wl_data_source *source, const char *mime)
{
    if (source == Wl.tabSource) Wl.tabTargeted = mime != NULL;
}

function void DataSourceSend(void *data, struct wl_data_source *source, const char *mime, i32 fd)
{
    String text = {0};
    if (source == Wl.tabSource) {
        text = Wl.tabText;
    } else if (source == Wl.dragSource) {
        text = Wl.dragList;
    } else {
        for (u32 i = 0; i < Wl.clipboardCount; ++i) {
            if (strcmp(mime, Wl.clipboardMimes[i]) == 0) text = Wl.clipboardData[i];
        }
    }
    for (u64 at = 0; at < text.length;) {
        ssize_t written = write(fd, text.value + at, text.length - at);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) break;
        at += (u64)written;
    }
    close(fd);
}

function void LinuxEndDrag(struct wl_data_source *source)
{
    wl_data_source_destroy(source);
    Wl.dragSource = NULL;
    ArenaClear(&Wl.dragArena);
    Wl.dragList = Wl.dragFolder = (String){0};
}

function void DataSourceCancelled(void *data, struct wl_data_source *source)
{
    // A tab let go where nothing took it, outside every window, is let go there.
    if (source == Wl.tabSource) {
        LinuxEndTabDrag(true);
        return;
    }
    if (source == Wl.dragSource) {
        LinuxEndDrag(source);
        return;
    }
    if (source == Wl.source) ++Wl.clipboardSequence;
    wl_data_source_destroy(source);
    if (source == Wl.source) {
        Wl.source = NULL;
        ArenaClear(&Wl.clipboardArena);
        Wl.clipboardCount = 0;
    }
}

function void DataSourceDropPerformed(void *data, struct wl_data_source *source) {}
// A target that moves the files moves them itself, so the source only lets go.
function void DataSourceFinished(void *data, struct wl_data_source *source)
{
    if (source == Wl.dragSource) LinuxEndDrag(source);
    // Taken by another window, which opened it; the release the compositor kept ends the drag here, outside.
    if (source == Wl.tabSource) {
        Wl.tabTaken = true;
        Wl.pointerX = Wl.pointerY = -1;
        LinuxEndTabDrag(true);
    }
}
function void DataSourceAction(void *data, struct wl_data_source *source, u32 action) {}

global const struct wl_data_source_listener DataSourceListener = {
    .target = DataSourceTarget,
    .send = DataSourceSend,
    .cancelled = DataSourceCancelled,
    .dnd_drop_performed = DataSourceDropPerformed,
    .dnd_finished = DataSourceFinished,
    .action = DataSourceAction,
};

function void WmBasePing(void *data, struct xdg_wm_base *wmBase, u32 serial)
{
    xdg_wm_base_pong(wmBase, serial);
}

global const struct xdg_wm_base_listener WmBaseListener = {
    .ping = WmBasePing,
};

// The buffer size for a logical size, rounded as the fractional scale protocol asks.
function i32 LinuxPhysical(i32 logical)
{
    return (i32)((f32)logical * Wl.scale + 0.5f);
}

function b32 LinuxDrawsEarly(void) { return Wl.resizePending && !Wl.earlyFrame; }

function void LinuxApplySize(void)
{
    i32 width = LinuxPhysical(Wl.width);
    i32 height = LinuxPhysical(Wl.height);
    if (Wl.viewport) wp_viewport_set_destination(Wl.viewport, Wl.width, Wl.height);
    Wl.resizePending = true;
    if (Wl.eglWindow != NULL) {
        wl_egl_window_resize(Wl.eglWindow, width, height, 0, 0);
    }

    PlatformEvent *event = PlatformPushEvent(PlatformEventKind_Resized);
    event->width = width;
    event->height = height;
}

function void XdgSurfaceConfigure(void *data, struct xdg_surface *xdgSurface, u32 serial)
{
    xdg_surface_ack_configure(xdgSurface, serial);
    Wl.configured = true;

    // Zero means the compositor leaves the size to us.
    i32 width = Wl.pendingWidth > 0 ? Wl.pendingWidth : Wl.width;
    i32 height = Wl.pendingHeight > 0 ? Wl.pendingHeight : Wl.height;
    if (width != Wl.width || height != Wl.height) {
        Wl.width = width;
        Wl.height = height;
        LinuxApplySize();
    }
}

global const struct xdg_surface_listener XdgSurfaceListener = {
    .configure = XdgSurfaceConfigure,
};

function void ToplevelConfigure(void *data, struct xdg_toplevel *toplevel, i32 width, i32 height, struct wl_array *states)
{
    Wl.pendingWidth = width;
    Wl.pendingHeight = height;
    b32 suspended = false;
    b32 maximized = false, fullscreen = false;
    for (u32 *state = states->data; (u8 *)state < (u8 *)states->data + states->size; ++state) {
        suspended |= *state == XDG_TOPLEVEL_STATE_SUSPENDED;
        maximized |= *state == XDG_TOPLEVEL_STATE_MAXIMIZED;
        fullscreen |= *state == XDG_TOPLEVEL_STATE_FULLSCREEN;
    }
    Wl.suspended = suspended;
    Wl.maximized = maximized;
    Wl.fullscreen = fullscreen;
}

function void ToplevelClose(void *data, struct xdg_toplevel *toplevel)
{
    Wl.closeRequested = true;
    PlatformPushEvent(PlatformEventKind_CloseRequested);
}

function void ToplevelConfigureBounds(void *data, struct xdg_toplevel *toplevel, i32 width, i32 height) {}
function void ToplevelWmCapabilities(void *data, struct xdg_toplevel *toplevel, struct wl_array *capabilities)
{
    b32 minimize = false;
    for (u32 *capability = capabilities->data; (u8 *)capability < (u8 *)capabilities->data + capabilities->size; ++capability) {
        minimize |= *capability == XDG_TOPLEVEL_WM_CAPABILITIES_MINIMIZE;
    }
    Wl.noMinimize = !minimize;
}

global const struct xdg_toplevel_listener ToplevelListener = {
    .configure        = ToplevelConfigure,
    .close            = ToplevelClose,
    .configure_bounds = ToplevelConfigureBounds,
    .wm_capabilities  = ToplevelWmCapabilities,
};

function void SurfaceEnter(void *data, struct wl_surface *surface, struct wl_output *output) {}
function void SurfaceLeave(void *data, struct wl_surface *surface, struct wl_output *output) {}

function void SurfacePreferredBufferScale(void *data, struct wl_surface *surface, i32 scale)
{
    // The fractional scale says the same more precisely when the compositor has it.
    if (Wl.fractional || scale <= 0 || (f32)scale == Wl.scale) return;
    Wl.scale = (f32)scale;
    wl_surface_set_buffer_scale(surface, scale);
    LinuxApplySize();
}

// The scale arrives in 120ths, so 125% is 150.
function void FractionalPreferredScale(void *data, struct wp_fractional_scale_v1 *fractional, u32 scale)
{
    f32 value = (f32)scale / 120.0f;
    if (scale == 0 || value == Wl.scale) return;
    Wl.scale = value;
    LinuxApplySize();
}

global const struct wp_fractional_scale_v1_listener FractionalListener = {
    .preferred_scale = FractionalPreferredScale,
};

function void SurfacePreferredBufferTransform(void *data, struct wl_surface *surface, u32 transform) {}

global const struct wl_surface_listener SurfaceListener = {
    .enter                      = SurfaceEnter,
    .leave                      = SurfaceLeave,
    .preferred_buffer_scale     = SurfacePreferredBufferScale,
    .preferred_buffer_transform = SurfacePreferredBufferTransform,
};

function void RegistryGlobal(void *data, struct wl_registry *registry, u32 name, const char *interface, u32 version)
{
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        Wl.compositorVersion = Min(version, (u32)6);
        Wl.compositor = wl_registry_bind(registry, name, &wl_compositor_interface, Wl.compositorVersion);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        Wl.shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, wl_seat_interface.name) == 0 && Wl.seat == NULL) {
        Wl.seat = wl_registry_bind(registry, name, &wl_seat_interface, Min(version, (u32)5));
        wl_seat_add_listener(Wl.seat, &SeatListener, NULL);
    } else if (strcmp(interface, wp_fractional_scale_manager_v1_interface.name) == 0) {
        Wl.fractionalManager = wl_registry_bind(registry, name, &wp_fractional_scale_manager_v1_interface, 1);
    } else if (strcmp(interface, wp_viewporter_interface.name) == 0) {
        Wl.viewporter = wl_registry_bind(registry, name, &wp_viewporter_interface, 1);
    } else if (strcmp(interface, xdg_activation_v1_interface.name) == 0) {
        Wl.activation = wl_registry_bind(registry, name, &xdg_activation_v1_interface, 1);
    } else if (strcmp(interface, wl_data_device_manager_interface.name) == 0) {
        Wl.dataManager = wl_registry_bind(registry, name, &wl_data_device_manager_interface, Min(version, (u32)3));
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        // Version 6 adds the suspended state.
        Wl.wmBase = wl_registry_bind(registry, name, &xdg_wm_base_interface, Min(version, (u32)6));
        xdg_wm_base_add_listener(Wl.wmBase, &WmBaseListener, NULL);
    }
}

function void RegistryGlobalRemove(void *data, struct wl_registry *registry, u32 name) {}

#pragma push_macro("global")
#undef global
static const struct wl_registry_listener RegistryListener = {
    .global        = RegistryGlobal,
    .global_remove = RegistryGlobalRemove,
};
#pragma pop_macro("global")

// -----------------------------------------------------------------------------
// EGL
// -----------------------------------------------------------------------------

function b32 LinuxCreateGLContext(void)
{
    Wl.eglDisplay = eglGetDisplay((EGLNativeDisplayType)Wl.display);
    if (Wl.eglDisplay == EGL_NO_DISPLAY || !eglInitialize(Wl.eglDisplay, NULL, NULL) || !eglBindAPI(EGL_OPENGL_API)) {
        return false;
    }

    // Opaque frames write an alpha of one; transparent ones let the compositor blend and blur.
    const EGLint configAttribs[] = {
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_RED_SIZE,        8,
        EGL_GREEN_SIZE,      8,
        EGL_BLUE_SIZE,       8,
        EGL_ALPHA_SIZE,      8,
        EGL_NONE,
    };
    EGLConfig config = NULL;
    EGLint configCount = 0;
    if (!eglChooseConfig(Wl.eglDisplay, configAttribs, &config, 1, &configCount) || configCount == 0) {
        return false;
    }

    const EGLint contextAttribs[] = {
        EGL_CONTEXT_MAJOR_VERSION,       3,
        EGL_CONTEXT_MINOR_VERSION,       3,
        EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
#if defined(DEBUG)
        EGL_CONTEXT_OPENGL_DEBUG,        EGL_TRUE,
#endif
        EGL_NONE,
    };
    Wl.eglContext = eglCreateContext(Wl.eglDisplay, config, EGL_NO_CONTEXT, contextAttribs);
    if (Wl.eglContext == EGL_NO_CONTEXT) {
        return false;
    }

    Wl.eglWindow = wl_egl_window_create(Wl.surface, LinuxPhysical(Wl.width), LinuxPhysical(Wl.height));
    Wl.eglSurface = eglCreateWindowSurface(Wl.eglDisplay, config, (EGLNativeWindowType)Wl.eglWindow, NULL);
    if (Wl.eglSurface == EGL_NO_SURFACE || !eglMakeCurrent(Wl.eglDisplay, Wl.eglSurface, Wl.eglSurface, Wl.eglContext)) {
        return false;
    }

    // A blocking swap would stall the event loop while the window is hidden, so frames are paced by frame callbacks instead.
    eglSwapInterval(Wl.eglDisplay, 0);
    return true;
}

// -----------------------------------------------------------------------------
// Platform contract
// -----------------------------------------------------------------------------

function void LinuxEnsureDataDevice(void);

// Hyprland's reply to `request` on its socket, empty when there is no Hyprland to ask. Before 0.40 the socket was under /tmp.
function void LinuxHyprlandAsk(const char *request, char *reply, u64 capacity)
{
    const char *runtime = getenv("XDG_RUNTIME_DIR"), *instance = getenv("HYPRLAND_INSTANCE_SIGNATURE");
    reply[0] = 0;
    for (u32 attempt = 0; instance && attempt < 2 && !reply[0]; ++attempt) {
        struct sockaddr_un address = { .sun_family = AF_UNIX };
        i32 length = attempt == 1 ? snprintf(address.sun_path, sizeof(address.sun_path), "/tmp/hypr/%s/.socket.sock", instance)
                   : runtime ? snprintf(address.sun_path, sizeof(address.sun_path), "%s/hypr/%s/.socket.sock", runtime, instance) : -1;
        if (length <= 0 || length >= (i32)sizeof(address.sun_path)) continue;
        i32 socketFd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (socketFd < 0) return;
        struct timeval timeout = { .tv_usec = 200000 };
        setsockopt(socketFd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(socketFd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        u64 received = 0, size = strlen(request);
        if (connect(socketFd, (struct sockaddr *)&address, sizeof(address)) == 0 && write(socketFd, request, size) == (i64)size) {
            for (i64 count; received < capacity - 1 && (count = read(socketFd, reply + received, capacity - 1 - received)) > 0;) received += (u64)count;
        }
        reply[received] = 0;
        close(socketFd);
    }
}

// Hyprland rounds windows' corners by its decoration options, which no Wayland protocol tells, so they are asked of its socket.
// ponytail: read once at startup and without window rules; ask again on a configure if a reloaded config or a rule should show.
function f32 LinuxHyprlandRounding(void)
{
    char reply[256];
    LinuxHyprlandAsk("j/getoption decoration:rounding", reply, sizeof(reply));
    // {"option": "decoration:rounding", "int": 8, "set": true }
    const char *rounding = strstr(reply, "\"int\":");
    if (!rounding) return 0;
    f32 radius = strtof(rounding + 6, NULL);
    // Hyprland 0.46 added the power, and grows the radius with it so that corners look as round.
    // Drawn as a circle, a radius so grown matches powers other than 2 only roughly. Before it, corners were circles.
    LinuxHyprlandAsk("j/getoption decoration:rounding_power", reply, sizeof(reply));
    const char *power = strstr(reply, "\"float\":");
    f32 grow = power ? strtof(power + 8, NULL) / 2.0f : 1.0f;
    return grow > 0 ? radius * grow : radius;
}

function b32 PlatformWindowOpen(PlatformWindowDesc desc)
{
    Wl.display = wl_display_connect(NULL);
    if (Wl.display == NULL) {
        return false;
    }

    Wl.xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    Wl.registry = wl_display_get_registry(Wl.display);
    wl_registry_add_listener(Wl.registry, &RegistryListener, NULL);
    wl_display_roundtrip(Wl.display);
    LinuxEnsureDataDevice();

    if (Wl.compositor == NULL || Wl.wmBase == NULL || Wl.shm == NULL || Wl.xkb == NULL) {
        return false;
    }

    Wl.width = desc.width;
    Wl.height = desc.height;
    Wl.scale = 1;
    Wl.rounding = LinuxHyprlandRounding();

    Wl.surface = wl_compositor_create_surface(Wl.compositor);
    Wl.cursorSurface = wl_compositor_create_surface(Wl.compositor);
    wl_surface_add_listener(Wl.surface, &SurfaceListener, NULL);
    if (Wl.fractionalManager && Wl.viewporter) {
        Wl.viewport = wp_viewporter_get_viewport(Wl.viewporter, Wl.surface);
        wp_viewport_set_destination(Wl.viewport, Wl.width, Wl.height);
        Wl.fractional = wp_fractional_scale_manager_v1_get_fractional_scale(Wl.fractionalManager, Wl.surface);
        wp_fractional_scale_v1_add_listener(Wl.fractional, &FractionalListener, NULL);
    }

    Wl.xdgSurface = xdg_wm_base_get_xdg_surface(Wl.wmBase, Wl.surface);
    xdg_surface_add_listener(Wl.xdgSurface, &XdgSurfaceListener, NULL);
    Wl.toplevel = xdg_surface_get_toplevel(Wl.xdgSurface);
    xdg_toplevel_add_listener(Wl.toplevel, &ToplevelListener, NULL);

    // Wayland wants null-terminated UTF-8.
    char title[256] = {0};
    MemCopy(title, desc.title.value, Min(desc.title.length, (u64)CArrayCount(title) - 1));
    xdg_toplevel_set_title(Wl.toplevel, title);
    xdg_toplevel_set_app_id(Wl.toplevel, "filey");

    // The first configure must be acknowledged before anything is drawn.
    wl_surface_commit(Wl.surface);
    while (!Wl.configured) {
        if (wl_display_dispatch(Wl.display) < 0) {
            return false;
        }
    }

    if (!LinuxCreateGLContext()) {
        return false;
    }

    Wl.wakeFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (Wl.wakeFd < 0) {
        return false;
    }

    PlatformEvent *event = PlatformPushEvent(PlatformEventKind_Resized);
    event->width = LinuxPhysical(Wl.width);
    event->height = LinuxPhysical(Wl.height);
    return true;
}

function PlatformWindowInfo PlatformWindowGetInfo(void)
{
    return (PlatformWindowInfo){
        .width  = LinuxPhysical(Wl.width),
        .height = LinuxPhysical(Wl.height),
        .scale  = Wl.scale,
        .cornerRadius = Wl.rounding * Wl.scale,
    };
}

function void LinuxTakeInstanceMessage(void);

function PlatformEventList PlatformPollEvents(b32 wait, u64 wakeAt)
{
    wl_display_dispatch_pending(Wl.display);
    LinuxGenerateRepeats();

    // A wait ends only for something the program sees: an event, a wake from a job, or a key repeat that is due.
    // Compositor traffic alone, such as the buffer release that follows every swap, would otherwise wake the loop to draw again, forever.
    // While the last frame is still waiting for its callback, everything that arrives is held for the next frame, so bursts of input draw once per refresh and a hidden window does not draw at all.
    // A suspended window holds everything until the compositor shows it again, except a request to close.
    b32 woken = false;
    for (;;) {
        u64 now = PlatformNowMicroseconds();
        b32 holding = !Wl.closed && !Wl.closeRequested && (Wl.suspended || (Wl.frameCallback && now < Wl.frameDeadline && !LinuxDrawsEarly()));
        b32 pacing = holding && !Wl.suspended;
        i32 timeout = 0;
        if (holding || (wait && EventQueue.pending.count == 0)) {
            timeout = pacing ? (i32)((Wl.frameDeadline - now + 999) / 1000) : -1;
            if (Wl.repeating) {
                i32 repeat = Wl.repeatNextUs > now ? (i32)((Wl.repeatNextUs - now + 999) / 1000) : 0;
                timeout = timeout < 0 ? repeat : Min(timeout, repeat);
            }
            if (wakeAt && !pacing) {
                i32 due = wakeAt > now ? (i32)Min((wakeAt - now + 999) / 1000, (u64)INT32_MAX) : 0;
                timeout = timeout < 0 ? due : Min(timeout, due);
            }
        }

        while (wl_display_prepare_read(Wl.display) != 0) {
            wl_display_dispatch_pending(Wl.display);
        }
        wl_display_flush(Wl.display);

        struct pollfd fds[] = {
            { .fd = wl_display_get_fd(Wl.display), .events = POLLIN },
            { .fd = Wl.wakeFd, .events = POLLIN },
            { .fd = Wl.instanceFd > 0 ? Wl.instanceFd : -1, .events = POLLIN },
        };
        if (poll(fds, CArrayCount(fds), timeout) > 0 && (fds[0].revents & POLLIN)) {
            wl_display_read_events(Wl.display);
        } else {
            wl_display_cancel_read(Wl.display);
        }
        if (fds[1].revents & POLLIN) {
            u64 count;
            while (read(Wl.wakeFd, &count, sizeof(count)) < 0 && errno == EINTR) {}
            woken = true;
        }
        if (fds[2].revents & POLLIN) {
            LinuxTakeInstanceMessage();
            woken = true;
        }

        wl_display_dispatch_pending(Wl.display);
        LinuxGenerateRepeats();

        // A dead compositor connection cannot recover; ask the app to quit.
        if (!Wl.closed && (wl_display_get_error(Wl.display) != 0 || (fds[0].revents & (POLLHUP | POLLERR)))) {
            Wl.closed = true;
            PlatformPushEvent(PlatformEventKind_CloseRequested);
        }

        if (!Wl.closed && !Wl.closeRequested && (Wl.suspended || (Wl.frameCallback && PlatformNowMicroseconds() < Wl.frameDeadline && !LinuxDrawsEarly()))) continue;
        if (!wait || timeout == 0 || woken || Wl.closed || EventQueue.pending.count) break;
    }

    return PlatformTakeEvents();
}

function void P_WakeWindow(void)
{
    u64 count = 1;
    while (write(Wl.wakeFd, &count, sizeof(count)) < 0 && errno == EINTR) {}
}

// One window for each settings folder: the run that has it listens on an abstract socket named after the folder, for its user, and a
// later run connects to hand it the activation token its launcher gave it, on the first line, and its folders on the next ones.
function b32 LinuxInstanceAddress(String key, struct sockaddr_un *address, socklen_t *size)
{
    *address = (struct sockaddr_un){ .sun_family = AF_UNIX };
    i32 length = snprintf(address->sun_path + 1, sizeof(address->sun_path) - 1, "filey-%u-%016llx", (u32)getuid(), (unsigned long long)MapHashBytes(key.value, key.length));
    *size = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 + (u64)length);
    return length > 0;
}

// The freedesktop file manager service, which other programs ask to show a file or a folder, as a browser's Show in Folder does. The window
// that keeps the run's instance socket claims its name on the session bus through libdbus, loaded at run time, and a thread of its own
// answers its calls by handing their paths over that socket, as a later run does: ShowItems and ShowItemProperties select each item in its
// folder, the latter with the inspector, and ShowFolders opens each folder.
// ponytail: only the first window's run answers, and only when no other file manager holds the name; installing a D-Bus service file would
// start Filey for it too.
typedef struct LinuxBus {
    void *(*get)(i32 type, void *error);
    i32 (*requestName)(void *connection, const char *name, u32 flags, void *error);
    i32 (*readWrite)(void *connection, i32 timeout);
    void *(*pop)(void *connection);
    i32 (*isCall)(void *message, const char *interface, const char *method);
    i32 (*iterInit)(void *message, void *iter);
    i32 (*argType)(void *iter);
    void (*recurse)(void *iter, void *sub);
    void (*basic)(void *iter, void *value);
    i32 (*next)(void *iter);
    void *(*newReturn)(void *message);
    i32 (*appendArgs)(void *message, i32 type, ...);
    i32 (*send)(void *connection, void *message, u32 *serial);
    void (*unref)(void *message);
    void (*errorInit)(void *error);
    struct sockaddr_un address;
    socklen_t size;
} LinuxBus;

global LinuxBus LinuxBus_;

// Hands `text` (an activation token, then a path a line) to this run's window over its instance socket.
function void LinuxBusHand(String text)
{
    i32 fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return;
    if (connect(fd, (struct sockaddr *)&LinuxBus_.address, LinuxBus_.size) == 0) {
        for (u64 at = 0; at < text.length;) {
            ssize_t wrote = write(fd, text.value + at, text.length - at);
            if (wrote < 0 && errno == EINTR) continue;
            if (wrote <= 0) break;
            at += (u64)wrote;
        }
    }
    close(fd);
}

function void *LinuxBusThread(void *unused)
{
    (void)unused;
    LinuxBus *bus = &LinuxBus_;
    // DBusError's and DBusMessageIter's storage, larger than either; they are filled by the library.
    alignas(8) u8 error[64], iter[128], items[128];
    bus->errorInit(error);
    void *connection = bus->get(0, error);
    // DBUS_NAME_FLAG_DO_NOT_QUEUE, and 1 for DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER: another file manager may hold the name.
    if (!connection || bus->requestName(connection, "org.freedesktop.FileManager1", 4, error) != 1) return NULL;
    local_persist const char introspection[] =
        "<node><interface name=\"org.freedesktop.FileManager1\">"
        "<method name=\"ShowFolders\"><arg type=\"as\" direction=\"in\"/><arg type=\"s\" direction=\"in\"/></method>"
        "<method name=\"ShowItems\"><arg type=\"as\" direction=\"in\"/><arg type=\"s\" direction=\"in\"/></method>"
        "<method name=\"ShowItemProperties\"><arg type=\"as\" direction=\"in\"/><arg type=\"s\" direction=\"in\"/></method>"
        "</interface></node>";
    while (bus->readWrite(connection, -1)) {
        for (void *message; (message = bus->pop(connection)) != NULL; bus->unref(message)) {
            const char *interface = "org.freedesktop.FileManager1";
            b32 folders = bus->isCall(message, interface, "ShowFolders"), show = bus->isCall(message, interface, "ShowItems");
            b32 properties = bus->isCall(message, interface, "ShowItemProperties");
            b32 introspect = bus->isCall(message, "org.freedesktop.DBus.Introspectable", "Introspect");
            if (!folders && !show && !properties && !introspect) continue;
            void *reply = bus->newReturn(message);
            if (introspect) {
                const char *xml = introspection;
                // DBUS_TYPE_STRING, then DBUS_TYPE_INVALID ending the arguments.
                if (reply) bus->appendArgs(reply, 's', &xml, 0);
            } else {
                // The URIs, then the startup id, which raises the window as an activation token does.
                Scratch scratch = GetScratch();
                StrBuilder text = {0}, lines = {0};
                if (bus->iterInit(message, iter) && bus->argType(iter) == 'a') {
                    bus->recurse(iter, items);
                    for (; bus->argType(items) == 's'; bus->next(items)) {
                        const char *uri = NULL;
                        bus->basic(items, &uri);
                        String path = uri ? P__FilePathFromUri(scratch.arena, StrFromCStr(uri)) : (String){0};
                        if (!path.length && uri && uri[0] == '/') path = StrFromCStr(uri);
                        if (path.length) StrBuildStr(scratch.arena, &lines, StrFormat(scratch.arena, "\n%S", path));
                    }
                    const char *startup = NULL;
                    if (bus->next(iter) && bus->argType(iter) == 's') bus->basic(iter, &startup);
                    if (startup) StrBuildStr(scratch.arena, &text, StrFromCStr(startup));
                }
                if (lines.buffer.length) {
                    if (!folders) StrBuildStr(scratch.arena, &text, S("\n--select"));
                    if (properties) StrBuildStr(scratch.arena, &text, S("\n--inspect"));
                    StrBuildStr(scratch.arena, &text, lines.buffer);
                    LinuxBusHand(text.buffer);
                }
                ReleaseScratch(scratch);
            }
            if (reply) {
                bus->send(connection, reply, NULL);
                bus->unref(reply);
            }
        }
    }
    return NULL;
}

// Claims the file manager service for this run's window, whose instance socket is at `address`, when libdbus is there.
function void LinuxBusStart(struct sockaddr_un address, socklen_t size)
{
    void *library = dlopen("libdbus-1.so.3", RTLD_NOW | RTLD_LOCAL);
    if (!library) return;
    LinuxBus *bus = &LinuxBus_;
    struct { void **slot; const char *name; } symbols[] = {
        { (void **)&bus->get, "dbus_bus_get" }, { (void **)&bus->requestName, "dbus_bus_request_name" },
        { (void **)&bus->readWrite, "dbus_connection_read_write" }, { (void **)&bus->pop, "dbus_connection_pop_message" },
        { (void **)&bus->isCall, "dbus_message_is_method_call" }, { (void **)&bus->iterInit, "dbus_message_iter_init" },
        { (void **)&bus->argType, "dbus_message_iter_get_arg_type" }, { (void **)&bus->recurse, "dbus_message_iter_recurse" },
        { (void **)&bus->basic, "dbus_message_iter_get_basic" }, { (void **)&bus->next, "dbus_message_iter_next" },
        { (void **)&bus->newReturn, "dbus_message_new_method_return" }, { (void **)&bus->appendArgs, "dbus_message_append_args" },
        { (void **)&bus->send, "dbus_connection_send" }, { (void **)&bus->unref, "dbus_message_unref" }, { (void **)&bus->errorInit, "dbus_error_init" },
    };
    for (u32 i = 0; i < CArrayCount(symbols); ++i) {
        if (!(*symbols[i].slot = dlsym(library, symbols[i].name))) return;
    }
    bus->address = address, bus->size = size;
    pthread_t thread;
    if (pthread_create(&thread, NULL, LinuxBusThread, NULL) == 0) pthread_detach(thread);
}

function b32 PlatformInstanceClaim(String key, ArrayStr paths)
{
    struct sockaddr_un address;
    socklen_t size = 0;
    i32 fd = LinuxInstanceAddress(key, &address, &size) ? socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0) : -1;
    if (fd < 0) return true;
    if (bind(fd, (struct sockaddr *)&address, size) == 0 && listen(fd, 8) == 0) {
        Wl.instanceFd = fd;
        LinuxBusStart(address, size);
        return true;
    }
    b32 handed = false;
    if (connect(fd, (struct sockaddr *)&address, size) == 0) {
        Scratch scratch = GetScratch();
        StrBuilder text = {0};
        const char *token = getenv("XDG_ACTIVATION_TOKEN");
        if (token) StrBuildStr(scratch.arena, &text, StrFromCStr(token));
        for (u64 i = 0; i < paths.header.count; ++i) {
            StrBuildChar(scratch.arena, &text, '\n');
            StrBuildStr(scratch.arena, &text, paths.v[i]);
        }
        handed = true;
        for (u64 at = 0; handed && at < text.buffer.length;) {
            ssize_t wrote = write(fd, text.buffer.value + at, text.buffer.length - at);
            if (wrote < 0 && errno == EINTR) continue;
            handed = wrote > 0;
            at += wrote > 0 ? (u64)wrote : 0;
        }
        ReleaseScratch(scratch);
    }
    close(fd);
    return !handed;
}

// Ends a tab's drag, pushing the release the compositor kept while it ran when `release`, where the pointer was last seen.
function void LinuxEndTabDrag(b32 release)
{
    if (!Wl.tabSource) return;
    wl_data_source_destroy(Wl.tabSource);
    Wl.tabSource = NULL;
    if (Wl.tabViewport) wp_viewport_destroy(Wl.tabViewport);
    if (Wl.tabIcon) wl_surface_destroy(Wl.tabIcon);
    if (Wl.tabBuffer) wl_buffer_destroy(Wl.tabBuffer);
    if (Wl.tabPixels) munmap(Wl.tabPixels, (size_t)Wl.tabWidth * (size_t)Wl.tabHeight * 4);
    Wl.tabViewport = NULL, Wl.tabIcon = NULL, Wl.tabBuffer = NULL, Wl.tabPixels = NULL, Wl.tabShown = false;
    if (release) {
        PlatformEvent *event = PlatformPushEvent(PlatformEventKind_MouseUp);
        event->button = MouseButton_Left;
        event->x = Wl.pointerX;
        event->y = Wl.pointerY;
        event->mods = Wl.mods;
    }
}

// Wayland places no window at the pointer for a client, so the tab is the icon of a drag the compositor runs, which moves it over every
// window; the first picture starts the drag, as the pointer leaves the window with the button held. The picture, in the window's pixels,
// keeps its logical size, and the pointer stays at (`hotX`, `hotY`) in it. Back over the window the icon hides, as the window draws the tab.
function void PlatformDragGhost(const u32 *bgra, i32 width, i32 height, i32 hotX, i32 hotY)
{
    if (!bgra) {
        if (Wl.tabIcon && Wl.tabShown) {
            wl_surface_attach(Wl.tabIcon, NULL, 0, 0);
            wl_surface_commit(Wl.tabIcon);
            Wl.tabShown = false;
        }
        return;
    }
    LinuxEnsureDataDevice();
    if (!Wl.tabSource && Wl.dataDevice && Wl.pointerSerial) {
        Wl.tabSource = wl_data_device_manager_create_data_source(Wl.dataManager);
        wl_data_source_add_listener(Wl.tabSource, &DataSourceListener, NULL);
        wl_data_source_offer(Wl.tabSource, LinuxTabMime);
        Wl.tabTargeted = Wl.tabTaken = false;
        if (wl_data_source_get_version(Wl.tabSource) >= 3) wl_data_source_set_actions(Wl.tabSource, WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE);
        Wl.tabIcon = wl_compositor_create_surface(Wl.compositor);
        if (Wl.viewporter) Wl.tabViewport = wp_viewporter_get_viewport(Wl.viewporter, Wl.tabIcon);
        // The pixels live in shared memory the compositor reads; a picture of another size later is left out rather than reallocated.
        i32 fd = memfd_create("filey-tab", MFD_CLOEXEC);
        u64 bytes = (u64)width * (u64)height * 4;
        void *memory = fd >= 0 && ftruncate(fd, (off_t)bytes) == 0 ? mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0) : MAP_FAILED;
        if (memory != MAP_FAILED) {
            struct wl_shm_pool *pool = wl_shm_create_pool(Wl.shm, fd, (i32)bytes);
            Wl.tabBuffer = wl_shm_pool_create_buffer(pool, 0, width, height, width * 4, WL_SHM_FORMAT_ARGB8888);
            wl_shm_pool_destroy(pool);
            Wl.tabPixels = memory, Wl.tabWidth = width, Wl.tabHeight = height;
        }
        if (fd >= 0) close(fd);
        wl_data_device_start_drag(Wl.dataDevice, Wl.tabSource, Wl.surface, Wl.tabIcon, Wl.pointerSerial);
    }
    if (!Wl.tabBuffer || width != Wl.tabWidth || height != Wl.tabHeight) return;
    b32 changed = MemCompare(Wl.tabPixels, bgra, (u64)width * (u64)height * 4) != 0;
    if (Wl.tabShown && !changed) return;
    MemCopy(Wl.tabPixels, bgra, (u64)width * (u64)height * 4);
    i32 logicalWidth = (i32)((f32)width / Wl.scale + 0.5f), logicalHeight = (i32)((f32)height / Wl.scale + 0.5f);
    i32 dx = -(i32)((f32)hotX / Wl.scale + 0.5f), dy = -(i32)((f32)hotY / Wl.scale + 0.5f);
    if (Wl.tabViewport) wp_viewport_set_destination(Wl.tabViewport, Max(logicalWidth, 1), Max(logicalHeight, 1));
    else wl_surface_set_buffer_scale(Wl.tabIcon, Max((i32)(Wl.scale + 0.5f), 1));
    // The icon's place is given once, as an offset from the pointer, and kept as its picture changes.
    b32 placed = Wl.tabShown;
    if (Wl.compositorVersion >= 5) {
        wl_surface_attach(Wl.tabIcon, Wl.tabBuffer, 0, 0);
        if (!placed) wl_surface_offset(Wl.tabIcon, dx, dy);
    } else {
        wl_surface_attach(Wl.tabIcon, Wl.tabBuffer, placed ? 0 : dx, placed ? 0 : dy);
    }
    wl_surface_damage_buffer(Wl.tabIcon, 0, 0, width, height);
    wl_surface_commit(Wl.tabIcon);
    Wl.tabShown = true;
    wl_display_flush(Wl.display);
}

// The window under a tab's drag learns of it from the drag itself; only the tab's text is kept, for it to read once the tab is dropped there.
function void PlatformDragHover(b32 active, String tab)
{
    if (!active || StrIsEqual(tab, Wl.tabText)) return;
    ArenaClear(&Wl.tabArena);
    Wl.tabText = StrCopy(&Wl.tabArena, tab);
}

// Whether another window takes the dragged tab where it is, or, asked with the tab's text at its drop, took it.
function b32 PlatformOtherWindowAtPointer(String paths)
{
    if (!paths.length) return Wl.tabSource && Wl.tabTargeted && Wl.pointerX < 0;
    b32 taken = Wl.tabTaken;
    Wl.tabTaken = false;
    return taken;
}

function void PlatformInstanceRelease(void)
{
    if (Wl.instanceFd > 0) close(Wl.instanceFd);
    Wl.instanceFd = -1;
}

// A later run's message, read whole, as it writes it at once and closes.
function void LinuxTakeInstanceMessage(void)
{
    i32 client = accept4(Wl.instanceFd, NULL, NULL, SOCK_CLOEXEC);
    if (client < 0) return;
    Scratch scratch = GetScratch();
    StrBuilder text = {0};
    char buffer[4096];
    struct pollfd fd = { .fd = client, .events = POLLIN };
    while (text.buffer.length < Megabytes(1) && poll(&fd, 1, 500) > 0) {
        ssize_t got = read(client, buffer, sizeof(buffer));
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) break;
        StrBuildStr(scratch.arena, &text, StrMake(buffer, (u64)got));
    }
    close(client);
    Index line = StrIndexChar(text.buffer, '\n', SSF_None);
    String token = line.exists ? StrFromTo(text.buffer, 0, (i64)line.value) : text.buffer;
    if (token.length && Wl.activation && Wl.surface) {
        char *copy = ArenaPushArray(scratch.arena, token.length + 1, char);
        MemCopy(copy, token.value, token.length);
        xdg_activation_v1_activate(Wl.activation, copy, Wl.surface);
    }
    PlatformPushEventText(PlatformEventKind_OpenPaths, line.exists ? StrFrom(text.buffer, line.value + 1) : (String){0});
    ReleaseScratch(scratch);
}

function void P_ReleaseWindowWake(void)
{
    close(Wl.wakeFd);
    Wl.wakeFd = -1;
}

function void FrameDone(void *data, struct wl_callback *callback, u32 time)
{
    wl_callback_destroy(callback);
    if (callback == Wl.frameCallback) {
        Wl.frameCallback = NULL;
        Wl.earlyFrame = false;
    }
}

global const struct wl_callback_listener FrameListener = {
    .done = FrameDone,
};

// The compositor's frame callbacks pace the frames either way, and a swap that does not wait for them would only draw frames that are never shown.
function void PlatformSetVSync(b32 on) { (void)on; }
function b32 PlatformHasVSync(void) { return false; }
// Hyprland and the like blur by their own settings.
function b32 PlatformHasBackdrops(void) { return false; }

function void PlatformSwapBuffers(void)
{
    // The request rides on the commit the swap makes.
    // A hidden window may never get its callback; the deadline keeps such a frame from holding up the next one for longer than a second.
    Wl.earlyFrame = Wl.frameCallback != NULL && PlatformNowMicroseconds() < Wl.frameDeadline;
    Wl.resizePending = false;
    if (Wl.frameCallback) wl_callback_destroy(Wl.frameCallback);
    Wl.frameCallback = wl_surface_frame(Wl.surface);
    wl_callback_add_listener(Wl.frameCallback, &FrameListener, NULL);
    Wl.frameDeadline = PlatformNowMicroseconds() + Millions(1);
    eglSwapBuffers(Wl.eglDisplay, Wl.eglSurface);
}

function PlatformProc *PlatformGLGetProc(const char *name)
{
    return (PlatformProc *)eglGetProcAddress(name);
}

function PlatformDoubleClick PlatformDoubleClickSettings(void)
{
    // ponytail: Wayland has no double-click setting; these are GTK's defaults.
    // Read the desktop's settings portal if users report mismatches.
    return (PlatformDoubleClick){
        .intervalMicroseconds = 400000,
        .distance = 5.0f * Wl.scale,
    };
}

function void PlatformWindowSetTransparent(b32 transparent, PlatformBackdrop backdrop)
{
    // The alpha the renderer writes is all Wayland needs.
    (void)transparent, (void)backdrop;
}

// Compositors older than xdg-shell version 5 never send their capabilities, and those are taken to support everything.
function b32 PlatformWindowHasButtons(void) { return !Wl.noMinimize; }
function b32 PlatformWindowIsMaximized(void) { return Wl.maximized; }
// Maximized tells nothing here: Hyprland reports every window maximized, so that programs leave out their frames.
function b32 PlatformWindowNeedsBorder(void) { return !Wl.fullscreen; }
function void PlatformWindowMinimize(void) { xdg_toplevel_set_minimized(Wl.toplevel); }

// A surface without a buffer is unmapped.
function void PlatformWindowHide(void)
{
    wl_surface_attach(Wl.surface, NULL, 0, 0);
    wl_surface_commit(Wl.surface);
    wl_display_flush(Wl.display);
}

// Only a size the compositor leaves to the window, as when it floats, takes.
function void PlatformWindowSetSize(i32 width, i32 height, b32 maximized)
{
    if (maximized) {
        xdg_toplevel_set_maximized(Wl.toplevel);
        return;
    }
    if (Wl.maximized) xdg_toplevel_unset_maximized(Wl.toplevel);
    if (width <= 0 || height <= 0 || Wl.pendingWidth > 0 || Wl.fullscreen) return;
    Wl.width = (i32)((f32)width / Wl.scale + 0.5f);
    Wl.height = (i32)((f32)height / Wl.scale + 0.5f);
    LinuxApplySize();
}
// ponytail: the current size, maximized or not; track the floating size from the configure events if restoring a maximized window's size matters.
function void PlatformWindowRestoredSize(i32 *width, i32 *height)
{
    PlatformWindowInfo info = PlatformWindowGetInfo();
    *width = info.width, *height = info.height;
}
function void PlatformWindowClose(void) { PlatformPushEvent(PlatformEventKind_CloseRequested); }

function void PlatformWindowToggleMaximize(void)
{
    if (Wl.maximized) xdg_toplevel_unset_maximized(Wl.toplevel);
    else xdg_toplevel_set_maximized(Wl.toplevel);
}

function void PlatformWindowToggleFullscreen(void)
{
    if (Wl.fullscreen) xdg_toplevel_unset_fullscreen(Wl.toplevel);
    else xdg_toplevel_set_fullscreen(Wl.toplevel, NULL);
}

function void PlatformWindowStartMove(void)
{
    if (!Wl.seat) return;
    xdg_toplevel_move(Wl.toplevel, Wl.seat, Wl.pointerSerial);
    // The compositor keeps the button's release.
    PlatformEvent *event = PlatformPushEvent(PlatformEventKind_MouseUp);
    event->button = MouseButton_Left;
    event->x = Wl.pointerX;
    event->y = Wl.pointerY;
}

// The data device needs both the seat and the manager, which the registry may announce in either order.
function void LinuxEnsureDataDevice(void)
{
    if (!Wl.dataDevice && Wl.dataManager && Wl.seat) {
        Wl.dataDevice = wl_data_device_manager_get_data_device(Wl.dataManager, Wl.seat);
        wl_data_device_add_listener(Wl.dataDevice, &DataDeviceListener, NULL);
    }
}

// Starts offering the clipboard data added with LinuxClipboardAdd; LinuxClipboardBegin drops the previous offer.
function void LinuxClipboardBegin(void)
{
    ++Wl.clipboardSequence;
    if (Wl.source) wl_data_source_destroy(Wl.source);
    Wl.source = NULL;
    ArenaClear(&Wl.clipboardArena);
    Wl.clipboardCount = 0;
}

function void LinuxClipboardAdd(const char *mime, String data)
{
    if (Wl.clipboardCount >= CArrayCount(Wl.clipboardMimes)) return;
    Wl.clipboardMimes[Wl.clipboardCount] = mime;
    Wl.clipboardData[Wl.clipboardCount++] = StrCopy(&Wl.clipboardArena, data);
}

function void LinuxClipboardOffer(void)
{
    LinuxEnsureDataDevice();
    if (!Wl.dataDevice) return;
    Wl.source = wl_data_device_manager_create_data_source(Wl.dataManager);
    wl_data_source_add_listener(Wl.source, &DataSourceListener, NULL);
    for (u32 i = 0; i < Wl.clipboardCount; ++i) wl_data_source_offer(Wl.source, Wl.clipboardMimes[i]);
    wl_data_device_set_selection(Wl.dataDevice, Wl.source, Wl.inputSerial);
    wl_display_flush(Wl.display);
}

// The data Filey offers for `mime` while it owns the selection.
function String LinuxClipboardOwn(const char *mime)
{
    for (u32 i = 0; i < Wl.clipboardCount; ++i) {
        if (strcmp(Wl.clipboardMimes[i], mime) == 0) return Wl.clipboardData[i];
    }
    return (String){0};
}

function void PlatformClipboardSetText(String text)
{
    LinuxClipboardBegin();
    for (u32 i = 0; i < CArrayCount(LinuxTextMimes); ++i) LinuxClipboardAdd(LinuxTextMimes[i], text);
    LinuxClipboardOffer();
}

// Reads what `offer` holds as `mime`.
function String LinuxReceive(Arena *arena, struct wl_data_offer *offer, const char *mime)
{
    i32 fds[2];
    if (!offer || !mime || pipe2(fds, O_CLOEXEC) != 0) return (String){0};
    wl_data_offer_receive(offer, mime, fds[1]);
    close(fds[1]);
    wl_display_flush(Wl.display);
    // ponytail: a fixed 1 MiB cap and a one second wait per read; text fields never need more.
    u64 capacity = Millions(1);
    char *bytes = ArenaPushArrayNoZero(arena, capacity, char);
    u64 length = 0;
    struct pollfd fd = { .fd = fds[0], .events = POLLIN };
    while (length < capacity && poll(&fd, 1, 1000) > 0) {
        ssize_t count = read(fds[0], bytes + length, capacity - length);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
        length += (u64)count;
    }
    close(fds[0]);
    return StrMake(bytes, length);
}

function String LinuxClipboardReceive(Arena *arena, const char *mime)
{
    return LinuxReceive(arena, Wl.selection, mime);
}

function String PlatformClipboardGetText(Arena *arena)
{
    // Reading our own selection through the compositor would wait on a pipe this thread has to fill.
    if (Wl.source) return StrCopy(arena, LinuxClipboardOwn(LinuxTextMimes[0]));
    return LinuxClipboardReceive(arena, Wl.selectionMime);
}

// Hands the drag to the compositor and returns; drops and the end of the drag arrive as data device and source events.
function b32 LinuxStartDrag(String directory, const String *names, u64 count)
{
    LinuxEnsureDataDevice();
    if (!Wl.dataDevice || !count || !Wl.pointerSerial) return false;
    if (Wl.dragSource) LinuxEndDrag(Wl.dragSource);
    StrBuilder list = {0};
    for (u64 i = 0; i < count; ++i) {
        StrBuildStr(&Wl.dragArena, &list, P__FileUri(&Wl.dragArena, StrFormat(&Wl.dragArena, "%S/%S", directory, names[i])));
        StrBuildCStr(&Wl.dragArena, &list, "\r\n");
    }
    Wl.dragList = list.buffer;
    Wl.dragFolder = StrCopy(&Wl.dragArena, directory);
    Wl.dragSource = wl_data_device_manager_create_data_source(Wl.dataManager);
    wl_data_source_add_listener(Wl.dragSource, &DataSourceListener, NULL);
    wl_data_source_offer(Wl.dragSource, "text/uri-list");
    if (wl_data_source_get_version(Wl.dragSource) >= 3) {
        wl_data_source_set_actions(Wl.dragSource, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY | WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE);
    }
    wl_data_device_start_drag(Wl.dataDevice, Wl.dragSource, Wl.surface, NULL, Wl.pointerSerial);
    wl_display_flush(Wl.display);
    return true;
}

function void PlatformClipboardSetFiles(const String *paths, u64 count, b32 cut)
{
    Scratch scratch = GetScratch();
    StrBuilder gnome = {0}, uris = {0}, text = {0};
    StrBuildCStr(scratch.arena, &gnome, cut ? "cut" : "copy");
    for (u64 i = 0; i < count; ++i) {
        String uri = P__FileUri(scratch.arena, paths[i]);
        StrBuildChar(scratch.arena, &gnome, '\n');
        StrBuildStr(scratch.arena, &gnome, uri);
        StrBuildStr(scratch.arena, &uris, uri);
        StrBuildCStr(scratch.arena, &uris, "\r\n");
        if (i) StrBuildChar(scratch.arena, &text, '\n');
        StrBuildStr(scratch.arena, &text, paths[i]);
    }
    LinuxClipboardBegin();
    LinuxClipboardAdd(LinuxFileMimes[0], gnome.buffer);
    LinuxClipboardAdd(LinuxFileMimes[1], uris.buffer);
    // KDE's own flag for cut files.
    if (cut) LinuxClipboardAdd("application/x-kde-cutselection", S("1"));
    for (u32 i = 0; i < CArrayCount(LinuxTextMimes); ++i) LinuxClipboardAdd(LinuxTextMimes[i], text.buffer);
    LinuxClipboardOffer();
    ReleaseScratch(scratch);
}

function u32 PlatformClipboardSequence(void)
{
    return Wl.clipboardSequence;
}

function b32 PlatformClipboardGetFiles(Arena *arena, ArrayStr *paths, b32 *cut)
{
    *paths = (ArrayStr){0};
    *cut = false;
    const char *mime = Wl.source ? (LinuxClipboardOwn(LinuxFileMimes[0]).length ? LinuxFileMimes[0] : NULL) : Wl.selectionFiles;
    if (!mime) return false;
    Scratch scratch = GetScratchEx(arena);
    String list = Wl.source ? LinuxClipboardOwn(mime) : LinuxClipboardReceive(scratch.arena, mime);
    P__ParseFileList(arena, list, strcmp(mime, LinuxFileMimes[0]) == 0, paths, cut);
    ReleaseScratch(scratch);
    return paths->header.count != 0;
}
