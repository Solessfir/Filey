// Win32 window, input and WGL context. Requires WinPlatform.c (windows.h).

#pragma push_macro("function")
#undef function
#include <dwmapi.h>
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

    i32 width;
    i32 height;
    u32 dpi;

    wchar highSurrogate;
    b32 mouseCaptured;
    b32 sizing;
    HBRUSH background;  // paints the window until OpenGL draws the first frame
} WinWindowState;

global WinWindowState Win;

#define WinWakeMessage (WM_APP + 1)

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
        default:         return Key_None;
    }
}

function KeyMods WinCurrentMods(void)
{
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

function LRESULT CALLBACK WinWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
        case WinWakeMessage: {
            if (Win.sizing) {
                Platform__RunModalFrame();
            }
        } return 0;

        case WM_CLOSE: {
            PlatformPushEvent(PlatformEventKind_CloseRequested);
        } return 0;

        case WM_SIZE: {
            Win.width = LOWORD(lParam);
            Win.height = HIWORD(lParam);
            PlatformEvent *event = PlatformPushEvent(PlatformEventKind_Resized);
            event->width = Win.width;
            event->height = Win.height;
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

        case WM_MOUSEMOVE:   WinPushMouseEvent(PlatformEventKind_MouseMove, lParam, 0); return 0;
        case WM_LBUTTONDOWN: WinMouseButton(PlatformEventKind_MouseDown, wParam, lParam, MouseButton_Left); return 0;
        case WM_LBUTTONUP:   WinMouseButton(PlatformEventKind_MouseUp, wParam, lParam, MouseButton_Left); return 0;
        case WM_RBUTTONDOWN: WinMouseButton(PlatformEventKind_MouseDown, wParam, lParam, MouseButton_Right); return 0;
        case WM_RBUTTONUP:   WinMouseButton(PlatformEventKind_MouseUp, wParam, lParam, MouseButton_Right); return 0;
        case WM_MBUTTONDOWN: WinMouseButton(PlatformEventKind_MouseDown, wParam, lParam, MouseButton_Middle); return 0;
        case WM_MBUTTONUP:   WinMouseButton(PlatformEventKind_MouseUp, wParam, lParam, MouseButton_Middle); return 0;

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

    Win.openGL = GetModuleHandleW(L"opengl32.dll");
    return true;
}

function b32 PlatformWindowOpen(PlatformWindowDesc desc)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

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

    Win.window = CreateWindowExW(0, windowClass.lpszClassName, title, WS_OVERLAPPEDWINDOW,
                                 CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                 NULL, NULL, instance, NULL);
    if (Win.window == NULL) {
        return false;
    }

    // Size the client area for the monitor the window landed on.
    Win.dpi = GetDpiForWindow(Win.window);
    RECT rect = {
        .right  = MulDiv(desc.width, (int)Win.dpi, USER_DEFAULT_SCREEN_DPI),
        .bottom = MulDiv(desc.height, (int)Win.dpi, USER_DEFAULT_SCREEN_DPI),
    };
    AdjustWindowRectExForDpi(&rect, WS_OVERLAPPEDWINDOW, FALSE, 0, Win.dpi);
    SetWindowPos(Win.window, NULL, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    // Loading the OpenGL driver takes over 100 ms, so the window is shown in the theme's colors first.
    // DWM composes it while this thread waits on the driver; the program's first frame then replaces it.
    b32 dark = true;
    DwmSetWindowAttribute(Win.window, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    Win.background = CreateSolidBrush(RGB((desc.background >> 16) & 0xFF, (desc.background >> 8) & 0xFF, desc.background & 0xFF));
    ShowWindow(Win.window, SW_SHOWDEFAULT);
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

function void PlatformWindowSetTransparent(b32 transparent)
{
    // The client area becomes glass, and the frame's backdrop fills it wherever the alpha is below one.
    MARGINS margins = transparent ? (MARGINS){ -1, -1, -1, -1 } : (MARGINS){0};
    DwmExtendFrameIntoClientArea(Win.window, &margins);
    i32 backdrop = transparent ? DWMSBT_TRANSIENTWINDOW : DWMSBT_AUTO;
    DwmSetWindowAttribute(Win.window, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));
}
