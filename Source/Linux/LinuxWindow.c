// Wayland window, input and EGL context. Requires LinuxPlatform.c.

#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <sys/eventfd.h>
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
    Arena dragArena;
    String dragList;
    String dragFolder;
    u32 pointerSerial;        // of the latest button press, which starting a drag needs
    Arena clipboardArena;     // what Filey offers while it owns the selection
    const char *clipboardMimes[8];
    String clipboardData[8];
    u32 clipboardCount;
    u32 inputSerial;          // of the latest key or button press, which setting the selection needs
    struct xdg_wm_base   *wmBase;
    // Fractional scaling: the buffer is drawn at the exact scale and the viewport maps it onto the logical size.
    // Without them the compositor gets an integer buffer scale.
    struct wp_fractional_scale_manager_v1 *fractionalManager;
    struct wp_viewporter *viewporter;
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
    b32 configured;
    b32 closed;
    // Frame pacing: the next frame waits for the compositor's callback for the last one, or for the deadline when it never comes.
    struct wl_callback *frameCallback;
    u64 frameDeadline;
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
        default:                return Key_None;
    }
}

function Key LinuxKeyFromKeycode(u32 keycode)
{
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

    struct wl_cursor *cursor = wl_cursor_theme_get_cursor(Wl.cursorTheme, "default");
    if (cursor == NULL) {
        cursor = wl_cursor_theme_get_cursor(Wl.cursorTheme, "left_ptr");
    }
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

function void PointerEnter(void *data, struct wl_pointer *pointer, u32 serial, struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y)
{
    LinuxSetCursor(serial);
    Wl.pointerX = (f32)(wl_fixed_to_double(x) * Wl.scale);
    Wl.pointerY = (f32)(wl_fixed_to_double(y) * Wl.scale);
}

function void PointerLeave(void *data, struct wl_pointer *pointer, u32 serial, struct wl_surface *surface)
{
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

function void DataOfferOffer(void *data, struct wl_data_offer *offer, const char *mime)
{
    if (offer != Wl.newOffer) return;
    for (u32 i = 0; i < CArrayCount(LinuxFileMimes); ++i) {
        if (strcmp(mime, LinuxFileMimes[i]) == 0 && (!Wl.newOfferFiles || i == 0)) Wl.newOfferFiles = LinuxFileMimes[i];
    }
    if (strcmp(mime, "text/uri-list") == 0) Wl.newOfferUris = true;
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
    wl_data_offer_add_listener(offer, &DataOfferListener, NULL);
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

// Files are taken as uri-lists, copied by default and moved when the source or the user asks for it; Filey's own drags move.
function void LinuxAcceptDrag(void)
{
    if (!Wl.dragOffer) return;
    wl_data_offer_accept(Wl.dragOffer, Wl.dragSerial, Wl.dragFiles && P__drop.length ? Wl.dragFiles : NULL);
    if (wl_data_offer_get_version(Wl.dragOffer) >= 3) {
        u32 actions = Wl.dragFiles && P__drop.length ? WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY | WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE : 0;
        u32 preferred = !actions ? 0 : Wl.dragSource ? WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE : WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY;
        wl_data_offer_set_actions(Wl.dragOffer, actions, preferred);
    }
}

function void DataDeviceEnter(void *data, struct wl_data_device *device, u32 serial, struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y, struct wl_data_offer *offer)
{
    if (Wl.dragOffer) wl_data_offer_destroy(Wl.dragOffer);
    Wl.dragOffer = offer;
    Wl.dragSerial = serial;
    Wl.dragAction = 0;
    Wl.dragFiles = offer && offer == Wl.newOffer ? (Wl.newOfferUris ? "text/uri-list" : Wl.newOfferFiles) : NULL;
    LinuxDragPointer(x, y);
    LinuxAcceptDrag();
}

function void DataDeviceLeave(void *data, struct wl_data_device *device)
{
    if (Wl.dragOffer) wl_data_offer_destroy(Wl.dragOffer);
    Wl.dragOffer = NULL;
    Wl.dragFiles = NULL;
}

function void DataDeviceMotion(void *data, struct wl_data_device *device, u32 time, wl_fixed_t x, wl_fixed_t y)
{
    LinuxDragPointer(x, y);
    // The folder under the pointer changes as it moves, so whether a drop is welcome does too.
    LinuxAcceptDrag();
}

function String LinuxClipboardReceive(Arena *arena, const char *mime);
function String LinuxReceive(Arena *arena, struct wl_data_offer *offer, const char *mime);

function void DataDeviceDrop(void *data, struct wl_data_device *device)
{
    if (Wl.dragOffer && Wl.dragFiles && P__drop.length) {
        Scratch scratch = GetScratch();
        String folder = StrMake(P__drop.path, P__drop.length);
        b32 own = Wl.dragSource != NULL;
        String list = own ? Wl.dragList : LinuxReceive(scratch.arena, Wl.dragOffer, Wl.dragFiles);
        ArrayStr paths = {0};
        b32 cut = false;
        P__ParseFileList(scratch.arena, list, strcmp(Wl.dragFiles, LinuxFileMimes[0]) == 0, &paths, &cut);
        b32 move = Wl.dragAction == WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE;
        // Dropping Filey's own files back where they are would only duplicate them.
        if (!(own && StrIsEqual(folder, Wl.dragFolder))) P__TransferDropped(&paths, move);
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

function void DataSourceTarget(void *data, struct wl_data_source *source, const char *mime) {}

function void DataSourceSend(void *data, struct wl_data_source *source, const char *mime, i32 fd)
{
    String text = {0};
    if (source == Wl.dragSource) {
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
    if (source == Wl.dragSource) {
        LinuxEndDrag(source);
        return;
    }
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

function void LinuxApplySize(void)
{
    i32 width = LinuxPhysical(Wl.width);
    i32 height = LinuxPhysical(Wl.height);
    if (Wl.viewport) wp_viewport_set_destination(Wl.viewport, Wl.width, Wl.height);
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
}

function void ToplevelClose(void *data, struct xdg_toplevel *toplevel)
{
    PlatformPushEvent(PlatformEventKind_CloseRequested);
}

global const struct xdg_toplevel_listener ToplevelListener = {
    .configure = ToplevelConfigure,
    .close     = ToplevelClose,
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
    } else if (strcmp(interface, wl_data_device_manager_interface.name) == 0) {
        Wl.dataManager = wl_registry_bind(registry, name, &wl_data_device_manager_interface, Min(version, (u32)3));
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        Wl.wmBase = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
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
    };
}

function PlatformEventList PlatformPollEvents(b32 wait)
{
    wl_display_dispatch_pending(Wl.display);
    LinuxGenerateRepeats();

    // A wait ends only for something the program sees: an event, a wake from a job, or a key repeat that is due.
    // Compositor traffic alone, such as the buffer release that follows every swap, would otherwise wake the loop to draw again, forever.
    // While the last frame is still waiting for its callback, everything that arrives is held for the next frame, so bursts of input draw once per refresh and a hidden window does not draw at all.
    b32 woken = false;
    for (;;) {
        u64 now = PlatformNowMicroseconds();
        b32 pacing = Wl.frameCallback && !Wl.closed && now < Wl.frameDeadline;
        i32 timeout = 0;
        if (pacing || (wait && EventQueue.pending.count == 0)) {
            timeout = pacing ? (i32)((Wl.frameDeadline - now + 999) / 1000) : -1;
            if (Wl.repeating) {
                i32 repeat = Wl.repeatNextUs > now ? (i32)((Wl.repeatNextUs - now + 999) / 1000) : 0;
                timeout = timeout < 0 ? repeat : Min(timeout, repeat);
            }
        }

        while (wl_display_prepare_read(Wl.display) != 0) {
            wl_display_dispatch_pending(Wl.display);
        }
        wl_display_flush(Wl.display);

        struct pollfd fds[] = {
            { .fd = wl_display_get_fd(Wl.display), .events = POLLIN },
            { .fd = Wl.wakeFd, .events = POLLIN },
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

        wl_display_dispatch_pending(Wl.display);
        LinuxGenerateRepeats();

        // A dead compositor connection cannot recover; ask the app to quit.
        if (!Wl.closed && (wl_display_get_error(Wl.display) != 0 || (fds[0].revents & (POLLHUP | POLLERR)))) {
            Wl.closed = true;
            PlatformPushEvent(PlatformEventKind_CloseRequested);
        }

        if (Wl.frameCallback && !Wl.closed && PlatformNowMicroseconds() < Wl.frameDeadline) continue;
        if (!wait || timeout == 0 || woken || Wl.closed || EventQueue.pending.count) break;
    }

    return PlatformTakeEvents();
}

function void P_WakeWindow(void)
{
    u64 count = 1;
    while (write(Wl.wakeFd, &count, sizeof(count)) < 0 && errno == EINTR) {}
}

function void P_ReleaseWindowWake(void)
{
    close(Wl.wakeFd);
    Wl.wakeFd = -1;
}

function void FrameDone(void *data, struct wl_callback *callback, u32 time)
{
    wl_callback_destroy(callback);
    if (callback == Wl.frameCallback) Wl.frameCallback = NULL;
}

global const struct wl_callback_listener FrameListener = {
    .done = FrameDone,
};

function void PlatformSwapBuffers(void)
{
    // The request rides on the commit the swap makes.
    // A hidden window may never get its callback; the deadline keeps such a frame from holding up the next one for longer than a second.
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

function void PlatformWindowSetTransparent(b32 transparent)
{
    // The alpha the renderer writes is all Wayland needs.
    (void)transparent;
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
