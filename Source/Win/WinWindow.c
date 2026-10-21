// Win32 window, input and WGL context. Requires WinPlatform.c (windows.h).

#pragma push_macro("function")
#undef function
#include <dwmapi.h>
#include <dbt.h>
#pragma pop_macro("function")

#define WGL_CONTEXT_MAJOR_VERSION_ARB    0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB    0x2092
#define WGL_CONTEXT_FLAGS_ARB            0x2094
#define WGL_CONTEXT_PROFILE_MASK_ARB     0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x0001
#define WGL_CONTEXT_DEBUG_BIT_ARB        0x0001

typedef HGLRC WINAPI WglCreateContextAttribsFn(HDC dc, HGLRC share, const int *attribs);
typedef BOOL WINAPI WglSwapIntervalFn(int interval);

typedef struct WinWindowState {
    HWND window;
    HDC dc;
    HGLRC glContext;
    HMODULE openGL;
    WglSwapIntervalFn *swapInterval;

    i32 width;
    i32 height;
    i32 restoredWidth, restoredHeight;  // the content's size when last neither maximized nor minimized
    u32 dpi;

    wchar highSurrogate;
    b32 mouseCaptured;
    b32 trackingLeave;  // asked to be told when the pointer leaves the window
    b32 postedPointer;  // FILEY_POSTED_POINTER: the pointer comes from posted messages while the real one is elsewhere, so it never leaves
    KeyMods postedMods; // and Shift, Ctrl and Alt from posted key messages, as the real keyboard's are someone else's
    b32 sizing;
    PlatformCursor cursor;
    HBRUSH background;  // paints the window until OpenGL draws the first frame
    HICON icons[2];     // the app icon at the window's size and at the small size, for its DPI
    b32 fullscreen;     // over the whole monitor without its frame, to go back to `placement`
    WINDOWPLACEMENT placement;
} WinWindowState;

global WinWindowState Win;

// One window for each settings folder: a named mutex says a run has it, and that run's window carries the folder's hash as a property, by
// which a later run finds it to hand it its folders.
#define WinInstanceProperty L"FileyInstance"
#define WinOpenPathsMagic 0x46494C59u
global HANDLE WinInstanceMutex;
global u64 WinInstanceKey;

function HWND Win__InstanceWindow(u64 key)
{
    for (HWND window = NULL; (window = FindWindowExW(NULL, window, L"FileyWindow", NULL));) {
        if ((u64)(uintptr_t)GetPropW(window, WinInstanceProperty) == key && IsWindowVisible(window)) return window;
    }
    return NULL;
}

function b32 PlatformInstanceClaim(String key, ArrayStr paths)
{
    WinInstanceKey = MapHashBytes(key.value, key.length) | 1;
    wchar name[64];
    swprintf(name, CArrayCount(name), L"Local\\Filey-%016llx", (unsigned long long)WinInstanceKey);
    HANDLE mutex = CreateMutexW(NULL, FALSE, name);
    if (!mutex) return true;
    if (GetLastError() != ERROR_ALREADY_EXISTS) {
        WinInstanceMutex = mutex;
        return true;
    }
    CloseHandle(mutex);
    Scratch scratch = GetScratch();
    StrBuilder text = {0};
    for (u64 i = 0; i < paths.header.count; ++i) {
        if (i) StrBuildChar(scratch.arena, &text, '\n');
        StrBuildStr(scratch.arena, &text, paths.v[i]);
    }
    // The run that has it may still be opening its window; one that does not answer, or has none, leaves this run its own.
    b32 handed = false;
    for (u32 tries = 0; tries < 100 && !handed; ++tries) {
        HWND window = Win__InstanceWindow(WinInstanceKey);
        if (!window) {
            Sleep(30);
            continue;
        }
        DWORD process = 0;
        GetWindowThreadProcessId(window, &process);
        // This run was just started by the user, so it may give the window the foreground.
        AllowSetForegroundWindow(process);
        COPYDATASTRUCT data = { .dwData = WinOpenPathsMagic, .cbData = (DWORD)text.buffer.length, .lpData = (void *)text.buffer.value };
        DWORD_PTR answer = 0;
        handed = SendMessageTimeoutW(window, WM_COPYDATA, 0, (LPARAM)&data, SMTO_ABORTIFHUNG, 5000, &answer) && answer;
        break;
    }
    ReleaseScratch(scratch);
    return !handed;
}

function void PlatformInstanceRelease(void)
{
    if (!WinInstanceMutex) return;
    if (Win.window) RemovePropW(Win.window, WinInstanceProperty);
    CloseHandle(WinInstanceMutex);
    WinInstanceMutex = NULL;
}

#define WinWakeMessage (WM_APP + 1)
#define WinShellChangeMessage (WM_APP + 2)

// The executable's icon resource (Resources/Filey.rc), loaded at the sizes the window's DPI asks for so the title bar and taskbar get sharp images.
function void WinSetIcons(void)
{
    HINSTANCE instance = GetModuleHandleW(NULL);
    i32 sizes[2] = { GetSystemMetricsForDpi(SM_CXICON, Win.dpi), GetSystemMetricsForDpi(SM_CXSMICON, Win.dpi) };
    WPARAM kinds[2] = { ICON_BIG, ICON_SMALL };
    for (u32 i = 0; i < 2; ++i) {
        HICON icon = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, sizes[i], sizes[i], LR_DEFAULTCOLOR);
        if (!icon) continue;
        SendMessageW(Win.window, WM_SETICON, kinds[i], (LPARAM)icon);
        if (Win.icons[i]) DestroyIcon(Win.icons[i]);
        Win.icons[i] = icon;
    }
}

function Key WinKeyFromVirtualKey(WPARAM vk)
{
    if (vk >= 'A' && vk <= 'Z') {
        return Key_A + (Key)(vk - 'A');
    }
    if (vk >= '0' && vk <= '9') {
        return Key_0 + (Key)(vk - '0');
    }
    if (vk >= VK_F1 && vk <= VK_F12) {
        return Key_F1 + (Key)(vk - VK_F1);
    }
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) {
        return Key_Keypad0 + (Key)(vk - VK_NUMPAD0);
    }

    switch (vk) {
        case VK_ESCAPE:  return Key_Escape;
        case VK_RETURN:  return Key_Enter;
        case VK_TAB:     return Key_Tab;
        case VK_BACK:    return Key_Backspace;
        case VK_DELETE:  return Key_Delete;
        case VK_INSERT:  return Key_Insert;
        case VK_SPACE:   return Key_Space;
        case VK_LEFT:    return Key_Left;
        case VK_RIGHT:   return Key_Right;
        case VK_UP:      return Key_Up;
        case VK_DOWN:    return Key_Down;
        case VK_HOME:    return Key_Home;
        case VK_END:     return Key_End;
        case VK_PRIOR:   return Key_PageUp;
        case VK_NEXT:    return Key_PageDown;
        case VK_SHIFT:   return Key_Shift;
        case VK_CONTROL: return Key_Control;
        case VK_MENU:    return Key_Alt;
        case VK_LWIN:
        case VK_RWIN:    return Key_Super;
        case VK_APPS:    return Key_ContextMenu;
        case VK_OEM_COMMA: return Key_Comma;
        case VK_OEM_PLUS:  return Key_Equals;
        case VK_OEM_MINUS: return Key_Minus;
        case VK_OEM_2:
        case VK_DIVIDE:    return Key_Slash;
        default:         return Key_None;
    }
}

function KeyMods WinCurrentMods(void)
{
    if (Win.postedPointer) return Win.postedMods;
    KeyMods mods = 0;
    if (GetKeyState(VK_SHIFT) & 0x8000)   mods |= KeyMod_Shift;
    if (GetKeyState(VK_CONTROL) & 0x8000) mods |= KeyMod_Control;
    if (GetKeyState(VK_MENU) & 0x8000)    mods |= KeyMod_Alt;
    if ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) mods |= KeyMod_Super;
    return mods;
}

function void WinPushMouseEvent(PlatformEventKind kind, LPARAM lParam, MouseButton button)
{
    PlatformEvent *event = PlatformPushEvent(kind);
    event->x = (f32)(i16)LOWORD(lParam);
    event->y = (f32)(i16)HIWORD(lParam);
    event->button = button;
    event->mods = WinCurrentMods();
}

function void WinMouseButton(PlatformEventKind kind, WPARAM wParam, LPARAM lParam, MouseButton button)
{
    // Capture keeps drags alive when the cursor leaves the window. wParam holds the buttons still down after this message.
    if (kind == PlatformEventKind_MouseDown && !Win.mouseCaptured) {
        SetCapture(Win.window);
        Win.mouseCaptured = true;
    }
    if (kind == PlatformEventKind_MouseUp && Win.mouseCaptured && (wParam & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON)) == 0) {
        ReleaseCapture();
        Win.mouseCaptured = false;
    }
    WinPushMouseEvent(kind, lParam, button);
}

// The window's drop target.
// Drags hover the UI like the mouse does, so the program can pick the folder under the pointer; WinShell.c forwards to that folder's shell drop target.
function void WinDropPointer(DWORD keys, POINTL point)
{
    POINT client = { point.x, point.y };
    ScreenToClient(Win.window, &client);
    PlatformEvent *event = PlatformPushEvent(PlatformEventKind_MouseMove);
    event->x = (f32)client.x;
    event->y = (f32)client.y;
    event->mods = ((keys & MK_SHIFT) ? KeyMod_Shift : 0) | ((keys & MK_CONTROL) ? KeyMod_Control : 0) | ((keys & MK_ALT) ? KeyMod_Alt : 0);
    // Filey's own drag runs the system's modal loop instead of the main loop.
    if (P__drop.dragging) Platform__RunModalFrame();
}

function HRESULT STDMETHODCALLTYPE WinDropQueryInterface(IDropTarget *target, REFIID iid, void **object)
{
    if (IsEqualIID(iid, &P__IID_IUnknown) || IsEqualIID(iid, &P__IID_IDropTarget)) {
        *object = target;
        return S_OK;
    }
    *object = NULL;
    return E_NOINTERFACE;
}

// The target is a global that outlives the registration.
function ULONG STDMETHODCALLTYPE WinDropAddRef(IDropTarget *target) { (void)target; return 1; }
function ULONG STDMETHODCALLTYPE WinDropRelease(IDropTarget *target) { (void)target; return 1; }

function HRESULT STDMETHODCALLTYPE WinDropDragEnter(IDropTarget *target, IDataObject *data, DWORD keys, POINTL point, DWORD *effect)
{
    (void)target;
    P__DropEnter(data);
    WinDropPointer(keys, point);
    P__DropOver(keys, point, effect);
    return S_OK;
}

function HRESULT STDMETHODCALLTYPE WinDropDragOver(IDropTarget *target, DWORD keys, POINTL point, DWORD *effect)
{
    (void)target;
    WinDropPointer(keys, point);
    P__DropOver(keys, point, effect);
    return S_OK;
}

function HRESULT STDMETHODCALLTYPE WinDropDragLeave(IDropTarget *target)
{
    (void)target;
    P__DropLeave();
    return S_OK;
}

function HRESULT STDMETHODCALLTYPE WinDropDrop(IDropTarget *target, IDataObject *data, DWORD keys, POINTL point, DWORD *effect)
{
    (void)target;
    (void)data;
    WinDropPointer(keys, point);
    P__DropDrop(keys, point, effect);
    return S_OK;
}

global IDropTargetVtbl WinDropVtbl = {
    WinDropQueryInterface, WinDropAddRef, WinDropRelease, WinDropDragEnter, WinDropDragOver, WinDropDragLeave, WinDropDrop,
};
global IDropTarget WinDropTarget = { &WinDropVtbl };

// The resize band along the top edge, which the client area covers once the caption is gone.
function i32 WinFrameThickness(void)
{
    return GetSystemMetricsForDpi(SM_CYFRAME, Win.dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, Win.dpi);
}

function HCURSOR WinCursor(PlatformCursor cursor)
{
    local_persist const LPCWSTR shapes[PlatformCursor_Count] = {
        [PlatformCursor_Arrow] = (LPCWSTR)IDC_ARROW, [PlatformCursor_ResizeHorizontal] = (LPCWSTR)IDC_SIZEWE, [PlatformCursor_ResizeVertical] = (LPCWSTR)IDC_SIZENS,
        [PlatformCursor_Text] = (LPCWSTR)IDC_IBEAM,
    };
    return LoadCursorW(NULL, shapes[cursor < PlatformCursor_Count ? cursor : PlatformCursor_Arrow]);
}

function void PlatformSetCursor(PlatformCursor cursor)
{
    if (cursor == Win.cursor || cursor >= PlatformCursor_Count) return;
    Win.cursor = cursor;
    // The shape changes at once while the pointer is over the content; WM_SETCURSOR keeps it there after.
    POINT point;
    if (GetCursorPos(&point) && WindowFromPoint(point) == Win.window && SendMessageW(Win.window, WM_NCHITTEST, 0, MAKELPARAM(point.x, point.y)) == HTCLIENT) {
        SetCursor(WinCursor(cursor));
    }
}

function LRESULT CALLBACK WinWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
        case WM_NCCALCSIZE: {
            if (!wParam) break;
            // The client area takes the caption's place; the side and bottom frames stay, invisible, for resizing.
            NCCALCSIZE_PARAMS *params = (NCCALCSIZE_PARAMS *)lParam;
            LONG top = params->rgrc[0].top;
            DefWindowProcW(window, message, wParam, lParam);
            params->rgrc[0].top = top;
            // A maximized window hangs its frame off the screen; a full screen one has none.
            if (IsZoomed(window) && !Win.fullscreen) params->rgrc[0].top += WinFrameThickness();
        } return 0;

        // Over the content the program's shape shows; the frame's edges keep the system's.
        case WM_SETCURSOR: {
            if (LOWORD(lParam) != HTCLIENT) break;
            SetCursor(WinCursor(Win.cursor));
        } return TRUE;

        case WM_NCHITTEST: {
            LRESULT hit = DefWindowProcW(window, message, wParam, lParam);
            if (hit != HTCLIENT || IsZoomed(window) || Win.fullscreen) return hit;
            POINT point = { (i16)LOWORD(lParam), (i16)HIWORD(lParam) };
            ScreenToClient(window, &point);
            return point.y < WinFrameThickness() ? HTTOP : HTCLIENT;
        }

        case WinWakeMessage: {
            if (Win.sizing) {
                Platform__RunModalFrame();
            }
        } return 0;

        case WM_CLOSE: {
            PlatformPushEvent(PlatformEventKind_CloseRequested);
        } return 0;

        // A later run's folders, or none when it only brings the window forward.
        case WM_COPYDATA: {
            const COPYDATASTRUCT *data = (const COPYDATASTRUCT *)lParam;
            if (!data || data->dwData != WinOpenPathsMagic) break;
            PlatformPushEventText(PlatformEventKind_OpenPaths, StrMake((const char *)data->lpData, data->cbData));
            if (IsIconic(window)) ShowWindow(window, SW_RESTORE);
            SetForegroundWindow(window);
        } return TRUE;

        case WM_SIZE: {
            Win.width = LOWORD(lParam);
            Win.height = HIWORD(lParam);
            if (wParam == SIZE_RESTORED && !Win.fullscreen) Win.restoredWidth = Win.width, Win.restoredHeight = Win.height;
            PlatformEvent *event = PlatformPushEvent(PlatformEventKind_Resized);
            event->width = Win.width;
            event->height = Win.height;
            if (Win.sizing) {
                Platform__RunModalFrame();
            }
        } return 0;

        // GL leaves the parts of the window that were off the screen undrawn, so a drag draws again as it brings them back.
        case WM_MOVE: {
            if (Win.sizing) {
                Platform__RunModalFrame();
            }
        } return 0;

        case WM_ENTERSIZEMOVE: {
            Win.sizing = true;
        } return 0;

        case WM_EXITSIZEMOVE: {
            Win.sizing = false;
        } return 0;

        case WM_DPICHANGED: {
            Win.dpi = HIWORD(wParam);
            WinSetIcons();
            const RECT *suggested = (const RECT *)lParam;
            SetWindowPos(window, NULL, suggested->left, suggested->top,
                         suggested->right - suggested->left, suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        } return 0;

        case WM_SETFOCUS:
        case WM_KILLFOCUS: {
            PlatformEvent *event = PlatformPushEvent(PlatformEventKind_FocusChanged);
            event->focused = message == WM_SETFOCUS;
        } return 0;

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYUP: {
            b32 down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
            KeyMods mod = wParam == VK_SHIFT ? KeyMod_Shift : wParam == VK_CONTROL ? KeyMod_Control : wParam == VK_MENU ? KeyMod_Alt : 0;
            if (mod) Win.postedMods = down ? (Win.postedMods | mod) : (Win.postedMods & ~mod);
            Key key = WinKeyFromVirtualKey(wParam);
            if (key != Key_None) {
                PlatformEvent *event = PlatformPushEvent(down ? PlatformEventKind_KeyDown : PlatformEventKind_KeyUp);
                event->key = key;
                event->mods = WinCurrentMods();
                event->isRepeat = down && (lParam & (1 << 30)) != 0;
            }
            // Let Alt+F4 and the system menu keep working.
            if (message == WM_SYSKEYDOWN || message == WM_SYSKEYUP) {
                break;
            }
        } return 0;

        case WM_CHAR: {
            wchar unit = (wchar)wParam;
            if (IS_HIGH_SURROGATE(unit)) {
                Win.highSurrogate = unit;
                return 0;
            }

            rune codepoint = unit;
            if (IS_LOW_SURROGATE(unit) && Win.highSurrogate != 0) {
                codepoint = 0x10000 + (((rune)Win.highSurrogate - 0xD800) << 10) + ((rune)unit - 0xDC00);
            }
            Win.highSurrogate = 0;

            // Control characters arrive as key events instead.
            if (codepoint >= 32 && codepoint != 127) {
                PlatformEvent *event = PlatformPushEvent(PlatformEventKind_Text);
                event->codepoint = codepoint;
                event->mods = WinCurrentMods();
            }
        } return 0;

        case WM_MOUSEMOVE: {
            // Windows tells of the pointer leaving only once asked, and asks again after each time.
            if (!Win.trackingLeave && !Win.postedPointer) {
                TRACKMOUSEEVENT track = { .cbSize = sizeof(track), .dwFlags = TME_LEAVE, .hwndTrack = window };
                Win.trackingLeave = TrackMouseEvent(&track);
            }
            WinPushMouseEvent(PlatformEventKind_MouseMove, lParam, 0);
            return 0;
        }
        // Volumes and media that come and go are told to every top-level window.
        // The shell's news of changed associations or icons, which the program loads its icons again for; the message itself wakes it.
        case WinShellChangeMessage: {
            PIDLIST_ABSOLUTE *items = NULL;
            LONG event = 0;
            HANDLE lock = SHChangeNotification_Lock((HANDLE)wParam, (DWORD)lParam, &items, &event);
            if (lock) {
                if (event & (SHCNE_ASSOCCHANGED | SHCNE_UPDATEIMAGE)) atomic_store(&P__iconsChanged, true);
                SHChangeNotification_Unlock(lock);
            }
        } return 0;
        case WM_DEVICECHANGE: {
            if (wParam == DBT_DEVICEARRIVAL || wParam == DBT_DEVICEREMOVECOMPLETE) PlatformPushEvent(PlatformEventKind_DrivesChanged);
            return TRUE;
        }
        case WM_MOUSELEAVE: {
            Win.trackingLeave = false;
            PlatformEvent *event = PlatformPushEvent(PlatformEventKind_MouseLeave);
            event->mods = WinCurrentMods();
            return 0;
        }
        case WM_LBUTTONDOWN: WinMouseButton(PlatformEventKind_MouseDown, wParam, lParam, MouseButton_Left); return 0;
        case WM_LBUTTONUP:   WinMouseButton(PlatformEventKind_MouseUp, wParam, lParam, MouseButton_Left); return 0;
        case WM_RBUTTONDOWN: WinMouseButton(PlatformEventKind_MouseDown, wParam, lParam, MouseButton_Right); return 0;
        case WM_RBUTTONUP:   WinMouseButton(PlatformEventKind_MouseUp, wParam, lParam, MouseButton_Right); return 0;
        case WM_MBUTTONDOWN: WinMouseButton(PlatformEventKind_MouseDown, wParam, lParam, MouseButton_Middle); return 0;
        case WM_MBUTTONUP:   WinMouseButton(PlatformEventKind_MouseUp, wParam, lParam, MouseButton_Middle); return 0;
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP: {
            PlatformEvent *event = PlatformPushEvent(message == WM_XBUTTONDOWN ? PlatformEventKind_KeyDown : PlatformEventKind_KeyUp);
            event->key = GET_XBUTTON_WPARAM(wParam) == XBUTTON1 ? Key_MouseX1 : Key_MouseX2;
            event->mods = WinCurrentMods();
        } return TRUE;

        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL: {
            // Wheel messages carry screen coordinates.
            POINT point = { (i16)LOWORD(lParam), (i16)HIWORD(lParam) };
            ScreenToClient(window, &point);

            PlatformEvent *event = PlatformPushEvent(PlatformEventKind_MouseWheel);
            f32 notches = (f32)(i16)HIWORD(wParam) / WHEEL_DELTA;
            event->x = (f32)point.x;
            event->y = (f32)point.y;
            event->wheelX = message == WM_MOUSEHWHEEL ? notches : 0;
            event->wheelY = message == WM_MOUSEWHEEL ? notches : 0;
            event->mods = WinCurrentMods();
        } return 0;

        case WM_CAPTURECHANGED: {
            Win.mouseCaptured = false;
        } return 0;

        case WM_ERASEBKGND: {
        } return 1;

        case WM_PAINT: {
            // The main loop is held up while the window is moved or sized, so what the system asks to be painted is drawn here.
            if (Win.sizing) {
                Platform__RunModalFrame();
            }
            if (Win.background) {
                PAINTSTRUCT paint;
                HDC dc = BeginPaint(window, &paint);
                FillRect(dc, &paint.rcPaint, Win.background);
                EndPaint(window, &paint);
            } else {
                ValidateRect(window, NULL);
            }
        } return 0;
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

function b32 WinCreateGLContext(void)
{
    PIXELFORMATDESCRIPTOR format = {
        .nSize      = sizeof(format),
        .nVersion   = 1,
        .dwFlags    = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,
        .iPixelType = PFD_TYPE_RGBA,
        .cColorBits = 32,
        .cAlphaBits = 8,
        .iLayerType = PFD_MAIN_PLANE,
    };
    i32 formatIndex = ChoosePixelFormat(Win.dc, &format);
    if (formatIndex == 0 || !SetPixelFormat(Win.dc, formatIndex, &format)) {
        return false;
    }

    // A legacy context is needed only to look up wglCreateContextAttribsARB.
    HGLRC legacy = wglCreateContext(Win.dc);
    if (legacy == NULL || !wglMakeCurrent(Win.dc, legacy)) {
        return false;
    }

    WglCreateContextAttribsFn *createContextAttribs =
        (WglCreateContextAttribsFn *)(void *)wglGetProcAddress("wglCreateContextAttribsARB");
    WglSwapIntervalFn *swapInterval = (WglSwapIntervalFn *)(void *)wglGetProcAddress("wglSwapIntervalEXT");

    if (createContextAttribs != NULL) {
        const int attribs[] = {
            WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
            WGL_CONTEXT_MINOR_VERSION_ARB, 3,
            WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
#if defined(DEBUG)
            WGL_CONTEXT_FLAGS_ARB, WGL_CONTEXT_DEBUG_BIT_ARB,
#endif
            0,
        };
        Win.glContext = createContextAttribs(Win.dc, NULL, attribs);
    }

    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(legacy);

    if (Win.glContext == NULL || !wglMakeCurrent(Win.dc, Win.glContext)) {
        return false;
    }

    if (swapInterval != NULL) {
        swapInterval(1);
    }
    Win.swapInterval = swapInterval;

    Win.openGL = GetModuleHandleW(L"opengl32.dll");
    return true;
}

function b32 PlatformWindowOpen(PlatformWindowDesc desc)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // Shell dialogs and menus hosted in this process (copy progress, context menus) follow the system's dark mode only
    // when the process opts in through uxtheme's unnamed SetPreferredAppMode (ordinal 135, 1 = follow the system).
    HMODULE uxtheme = LoadLibraryExW(L"uxtheme.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (uxtheme != NULL) {
        typedef i32 SetPreferredAppModeFn(i32 mode);
        typedef void FlushMenuThemesFn(void);
        SetPreferredAppModeFn *setPreferredAppMode = (SetPreferredAppModeFn *)(void *)GetProcAddress(uxtheme, MAKEINTRESOURCEA(135));
        FlushMenuThemesFn *flushMenuThemes = (FlushMenuThemesFn *)(void *)GetProcAddress(uxtheme, MAKEINTRESOURCEA(136));
        if (setPreferredAppMode != NULL) {
            setPreferredAppMode(1);
        }
        if (flushMenuThemes != NULL) {
            flushMenuThemes();
        }
    }

    HINSTANCE instance = GetModuleHandleW(NULL);
    WNDCLASSEXW windowClass = {
        .cbSize        = sizeof(windowClass),
        .style         = CS_OWNDC,
        .lpfnWndProc   = WinWindowProc,
        .hInstance     = instance,
        .hCursor       = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW),
        .lpszClassName = L"FileyWindow",
    };
    if (!RegisterClassExW(&windowClass)) {
        return false;
    }

    // Win32 wants a null-terminated UTF-16 title.
    wchar title[256] = {0};
    MultiByteToWideChar(CP_UTF8, 0, desc.title.value, (int)Min(desc.title.length, (u64)200), title, (int)CArrayCount(title) - 1);

    Win.postedPointer = GetEnvironmentVariableW(L"FILEY_POSTED_POINTER", NULL, 0) > 0;
    Win.window = CreateWindowExW(0, windowClass.lpszClassName, title, WS_OVERLAPPEDWINDOW,
                                 CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                 NULL, NULL, instance, NULL);
    if (Win.window == NULL) {
        return false;
    }
    if (WinInstanceMutex) SetPropW(Win.window, WinInstanceProperty, (HANDLE)(uintptr_t)WinInstanceKey);

    // Installers and Open With's "always" tell the shell of changed associations, and apps of replaced icons; Explorer is told so, and so is this window.
    PIDLIST_ABSOLUTE desktop = NULL;
    if (SUCCEEDED(SHGetSpecialFolderLocation(NULL, CSIDL_DESKTOP, &desktop))) {
        SHChangeNotifyEntry entry = { desktop, TRUE };
        SHChangeNotifyRegister(Win.window, SHCNRF_ShellLevel | SHCNRF_NewDelivery, SHCNE_ASSOCCHANGED | SHCNE_UPDATEIMAGE, WinShellChangeMessage, 1, &entry);
    }

    // Size the client area for the monitor the window landed on.
    Win.dpi = GetDpiForWindow(Win.window);
    WinSetIcons();
    b32 given = desc.contentWidth > 0 && desc.contentHeight > 0;
    RECT rect = {
        .right  = given ? desc.contentWidth : MulDiv(desc.width, (int)Win.dpi, USER_DEFAULT_SCREEN_DPI),
        .bottom = given ? desc.contentHeight : MulDiv(desc.height, (int)Win.dpi, USER_DEFAULT_SCREEN_DPI),
    };
    AdjustWindowRectExForDpi(&rect, WS_OVERLAPPEDWINDOW, FALSE, 0, Win.dpi);
    // There is no caption above the client area.
    rect.top = 0;
    SetWindowPos(Win.window, NULL, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

    // Loading the OpenGL driver takes over 100 ms, so the window is shown in the theme's colors first.
    // DWM composes it while this thread waits on the driver; the program's first frame then replaces it.
    b32 dark = true;
    DwmSetWindowAttribute(Win.window, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    Win.background = CreateSolidBrush(RGB((desc.background >> 16) & 0xFF, (desc.background >> 8) & 0xFF, desc.background & 0xFF));
    ShowWindow(Win.window, desc.maximized ? SW_SHOWMAXIMIZED : SW_SHOWDEFAULT);
    UpdateWindow(Win.window);

    // Shell context menus, drag and drop and the clipboard need OLE on this thread.
    OleInitialize(NULL);
    P__shellOwner = Win.window;
    RegisterDragDrop(Win.window, &WinDropTarget);

    Win.dc = GetDC(Win.window);
    if (!WinCreateGLContext()) {
        return false;
    }

    return true;
}

function PlatformWindowInfo PlatformWindowGetInfo(void)
{
    return (PlatformWindowInfo){
        .width  = Win.width,
        .height = Win.height,
        .scale  = (f32)Win.dpi / USER_DEFAULT_SCREEN_DPI,
    };
}

function PlatformEventList PlatformPollEvents(b32 wait)
{
    if (wait && EventQueue.pending.count == 0) {
        WaitMessage();
    }

    MSG message;
    while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    return PlatformTakeEvents();
}

function void P_WakeWindow(void)
{
    PostMessageW(Win.window, WinWakeMessage, 0, 0);
}

function void P_ReleaseWindowWake(void)
{
}

function b32 PlatformHasVSync(void) { return true; }
function b32 PlatformHasBackdrops(void) { return true; }

function void PlatformSetVSync(b32 on)
{
    if (Win.swapInterval) Win.swapInterval(on ? 1 : 0);
}

function void PlatformSwapBuffers(void)
{
    SwapBuffers(Win.dc);
    if (Win.background) {
        DeleteObject(Win.background);
        Win.background = NULL;
    }
}

function PlatformProc *PlatformGLGetProc(const char *name)
{
    // wglGetProcAddress only knows extensions and post-1.1 entry points; it may also return small sentinel values instead of NULL on failure.
    PROC proc = wglGetProcAddress(name);
    if ((uintptr_t)proc <= 3 || (intptr_t)proc == -1) {
        proc = GetProcAddress(Win.openGL, name);
    }
    return (PlatformProc *)proc;
}

function PlatformDoubleClick PlatformDoubleClickSettings(void)
{
    // SM_C*DOUBLECLK is the full width of the rectangle centered on the first click.
    i32 size = MaxI32(GetSystemMetrics(SM_CXDOUBLECLK), GetSystemMetrics(SM_CYDOUBLECLK));
    return (PlatformDoubleClick){
        .intervalMicroseconds = (u64)GetDoubleClickTime() * 1000,
        .distance = 0.5f * (f32)size,
    };
}

function void PlatformWindowSetTransparent(b32 transparent, PlatformBackdrop backdrop)
{
    // The client area becomes glass, and the frame's backdrop fills it wherever the alpha is below one.
    // The glass grows up from the bottom edge over the whole window rather than in from every edge: extended from the top, the system draws its
    // own caption buttons there over Filey's.
    MARGINS margins = transparent ? (MARGINS){ 0, 0, 0, 32767 } : (MARGINS){0};
    DwmExtendFrameIntoClientArea(Win.window, &margins);
    local_persist const i32 types[PlatformBackdrop_Count] = { DWMSBT_TRANSIENTWINDOW, DWMSBT_MAINWINDOW, DWMSBT_TABBEDWINDOW, DWMSBT_NONE };
    i32 type = transparent ? types[backdrop < PlatformBackdrop_Count ? backdrop : 0] : DWMSBT_AUTO;
    DwmSetWindowAttribute(Win.window, DWMWA_SYSTEMBACKDROP_TYPE, &type, sizeof(type));
}

function b32 PlatformWindowHasButtons(void) { return true; }
function b32 PlatformWindowIsMaximized(void) { return IsZoomed(Win.window) != 0; }
function b32 PlatformWindowNeedsBorder(void) { return false; }
function void PlatformWindowMinimize(void) { ShowWindow(Win.window, SW_MINIMIZE); }
function void PlatformWindowToggleMaximize(void) { ShowWindow(Win.window, IsZoomed(Win.window) ? SW_RESTORE : SW_MAXIMIZE); }

// The content takes the size; the invisible frame at its sides and bottom is added to it.
function void PlatformWindowSetSize(i32 width, i32 height, b32 maximized)
{
    if (maximized && IsZoomed(Win.window)) return;
    if (!maximized && IsZoomed(Win.window)) ShowWindow(Win.window, SW_RESTORE);
    // A maximized window takes the size first, which it goes back to when it is restored.
    RECT outer = {0}, inner = {0};
    if (width > 0 && height > 0 && GetWindowRect(Win.window, &outer) && GetClientRect(Win.window, &inner)) {
        i32 frameWidth = (outer.right - outer.left) - (inner.right - inner.left), frameHeight = (outer.bottom - outer.top) - (inner.bottom - inner.top);
        SetWindowPos(Win.window, NULL, 0, 0, width + frameWidth, height + frameHeight, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (maximized) ShowWindow(Win.window, SW_MAXIMIZE);
}

function void PlatformWindowRestoredSize(i32 *width, i32 *height)
{
    b32 known = Win.restoredWidth > 0 && Win.restoredHeight > 0;
    *width = known ? Win.restoredWidth : Win.width;
    *height = known ? Win.restoredHeight : Win.height;
}

function void PlatformWindowToggleFullscreen(void)
{
    LONG_PTR style = GetWindowLongPtrW(Win.window, GWL_STYLE);
    if (!Win.fullscreen) {
        MONITORINFO monitor = { .cbSize = sizeof(monitor) };
        Win.placement.length = sizeof(Win.placement);
        if (!GetWindowPlacement(Win.window, &Win.placement) || !GetMonitorInfoW(MonitorFromWindow(Win.window, MONITOR_DEFAULTTONEAREST), &monitor)) return;
        Win.fullscreen = true;
        SetWindowLongPtrW(Win.window, GWL_STYLE, style & ~(LONG_PTR)WS_OVERLAPPEDWINDOW);
        RECT r = monitor.rcMonitor;
        SetWindowPos(Win.window, HWND_TOP, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    } else {
        Win.fullscreen = false;
        SetWindowLongPtrW(Win.window, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(Win.window, &Win.placement);
        SetWindowPos(Win.window, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
}
function void PlatformWindowClose(void) { PlatformPushEvent(PlatformEventKind_CloseRequested); }

function void PlatformWindowHide(void) { ShowWindow(Win.window, SW_HIDE); }

function void PlatformWindowStartMove(void)
{
    // The system's move loop runs until the button is released, with snapping and the drag to the top that maximizes.
    POINT point;
    GetCursorPos(&point);
    ReleaseCapture();
    Win.mouseCaptured = false;
    SendMessageW(Win.window, WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(point.x, point.y));
    GetCursorPos(&point);
    ScreenToClient(Win.window, &point);
    PlatformEvent *event = PlatformPushEvent(PlatformEventKind_MouseUp);
    event->button = MouseButton_Left;
    event->x = (f32)point.x;
    event->y = (f32)point.y;
}

function void PlatformClipboardSetText(String text)
{
    if (text.length > INT32_MAX || !OpenClipboard(Win.window)) return;
    EmptyClipboard();
    i32 length = text.length ? MultiByteToWideChar(CP_UTF8, 0, text.value, (i32)text.length, NULL, 0) : 0;
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, ((u64)length + 1) * sizeof(wchar));
    wchar *wide = memory ? GlobalLock(memory) : NULL;
    if (wide) {
        if (length) MultiByteToWideChar(CP_UTF8, 0, text.value, (i32)text.length, wide, length);
        wide[length] = 0;
        GlobalUnlock(memory);
        // The clipboard owns the memory once it takes it.
        if (!SetClipboardData(CF_UNICODETEXT, memory)) GlobalFree(memory);
    } else if (memory) {
        GlobalFree(memory);
    }
    CloseClipboard();
}

function String PlatformClipboardGetText(Arena *arena)
{
    String result = {0};
    if (!OpenClipboard(Win.window)) return result;
    HANDLE data = GetClipboardData(CF_UNICODETEXT);
    const wchar *wide = data ? GlobalLock(data) : NULL;
    if (wide) {
        result = P__FileStringWide(arena, wide, lstrlenW(wide));
        GlobalUnlock(data);
    }
    CloseClipboard();
    return result;
}

function void PlatformClipboardSetFiles(const String *paths, u64 count, b32 cut)
{
    Scratch scratch = GetScratch();
    // CF_HDROP is a DROPFILES header followed by the paths, each null-terminated, and one more null.
    u64 length = 1;
    wchar **wide = ArenaPushArray(scratch.arena, Max(count, (u64)1), wchar *);
    for (u64 i = 0; i < count; ++i) {
        wide[i] = P__FilePathWide(scratch.arena, paths[i]);
        if (wide[i]) length += (u64)lstrlenW(wide[i]) + 1;
    }
    HGLOBAL drop = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DROPFILES) + length * sizeof(wchar));
    DROPFILES *header = drop ? GlobalLock(drop) : NULL;
    HGLOBAL effect = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
    DWORD *effectValue = effect ? GlobalLock(effect) : NULL;
    if (header && effectValue && OpenClipboard(Win.window)) {
        header->pFiles = sizeof(DROPFILES);
        header->fWide = TRUE;
        wchar *at = (wchar *)(header + 1);
        for (u64 i = 0; i < count; ++i) {
            if (!wide[i]) continue;
            u64 size = (u64)lstrlenW(wide[i]) + 1;
            MemCopy(at, wide[i], size * sizeof(wchar));
            at += size;
        }
        // Explorer reads whether to move or copy from the preferred drop effect.
        *effectValue = cut ? DROPEFFECT_MOVE : DROPEFFECT_COPY | DROPEFFECT_LINK;
        GlobalUnlock(drop);
        GlobalUnlock(effect);
        EmptyClipboard();
        if (SetClipboardData(CF_HDROP, drop)) drop = NULL;
        if (SetClipboardData(RegisterClipboardFormatW(L"Preferred DropEffect"), effect)) effect = NULL;
        CloseClipboard();
    } else {
        if (header) GlobalUnlock(drop);
        if (effectValue) GlobalUnlock(effect);
    }
    if (drop) GlobalFree(drop);
    if (effect) GlobalFree(effect);
    ReleaseScratch(scratch);
}

function b32 PlatformClipboardGetFiles(Arena *arena, ArrayStr *paths, b32 *cut)
{
    *paths = (ArrayStr){0};
    *cut = false;
    if (!OpenClipboard(Win.window)) return false;
    HDROP drop = GetClipboardData(CF_HDROP);
    u32 count = drop ? DragQueryFileW(drop, 0xFFFFFFFF, NULL, 0) : 0;
    for (u32 i = 0; i < count; ++i) {
        u32 length = DragQueryFileW(drop, i, NULL, 0);
        Scratch scratch = GetScratchEx(arena);
        wchar *name = ArenaPushArray(scratch.arena, (u64)length + 1, wchar);
        DragQueryFileW(drop, i, name, length + 1);
        String path = P__FileStringWide(arena, name, (i32)length);
        ReleaseScratch(scratch);
        for (u64 j = 0; j < path.length; ++j) {
            if (path.value[j] == '\\') ((char *)path.value)[j] = '/';
        }
        ArrayPush(arena, *paths, path);
    }
    HANDLE effect = count ? GetClipboardData(RegisterClipboardFormatW(L"Preferred DropEffect")) : NULL;
    const DWORD *value = effect ? GlobalLock(effect) : NULL;
    if (value) {
        *cut = (*value & DROPEFFECT_MOVE) != 0 && !(*value & DROPEFFECT_COPY);
        GlobalUnlock(effect);
    }
    CloseClipboard();
    return count != 0;
}
