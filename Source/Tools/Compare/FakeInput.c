// Loaded into an app that Compare.py started, so that the app sees the pointer, focus and held keys the script sets in shared memory instead of the real ones.
// Only the app's own imports are patched; nothing outside the process changes.
#include <windows.h>

typedef struct { LONG x, y; LONG active; LONG hwnd; BYTE keys[256]; } Fake;
static Fake *fake;
static BOOL (WINAPI *RealTranslateMessage)(const MSG *);

static BOOL WINAPI FakeGetCursorPos(LPPOINT p) { p->x = fake->x; p->y = fake->y; return TRUE; }
static HWND WINAPI FakeGetForegroundWindow(void) { return fake->active ? (HWND)(LONG_PTR)fake->hwnd : NULL; }
static SHORT WINAPI FakeGetKeyState(int vk) { return (vk >= 0 && vk < 256 && fake->keys[vk]) ? (SHORT)0x8000 : 0; }

// A held Ctrl or Alt keeps a posted key from turning into a typed character, as a real keyboard would.
static BOOL WINAPI FakeTranslateMessage(const MSG *msg)
{
    if (fake->keys[VK_CONTROL] || fake->keys[VK_MENU]) return FALSE;
    return RealTranslateMessage(msg);
}

static void Patch(HMODULE module)
{
    BYTE *base = (BYTE *)module;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return;
    for (IMAGE_IMPORT_DESCRIPTOR *d = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir.VirtualAddress); d->Name; ++d) {
        IMAGE_THUNK_DATA *names = (IMAGE_THUNK_DATA *)(base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        IMAGE_THUNK_DATA *slots = (IMAGE_THUNK_DATA *)(base + d->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            const char *name = (const char *)((IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData))->Name;
            void *to = 0;
            if (!lstrcmpA(name, "GetCursorPos")) to = (void *)FakeGetCursorPos;
            else if (!lstrcmpA(name, "GetForegroundWindow")) to = (void *)FakeGetForegroundWindow;
            else if (!lstrcmpA(name, "GetKeyState") || !lstrcmpA(name, "GetAsyncKeyState")) to = (void *)FakeGetKeyState;
            else if (!lstrcmpA(name, "TranslateMessage")) to = (void *)FakeTranslateMessage;
            if (!to) continue;
            DWORD old;
            VirtualProtect(&slots->u1.Function, sizeof(void *), PAGE_READWRITE, &old);
            slots->u1.Function = (ULONG_PTR)to;
            VirtualProtect(&slots->u1.Function, sizeof(void *), old, &old);
        }
    }
}

BOOL WINAPI DllMain(HINSTANCE self, DWORD reason, LPVOID reserved)
{
    (void)self; (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        char name[64];
        wsprintfA(name, "Local\\FakeInput%lu", GetCurrentProcessId());
        HANDLE map = OpenFileMappingA(FILE_MAP_READ, FALSE, name);
        if (!map) return TRUE;
        fake = (Fake *)MapViewOfFile(map, FILE_MAP_READ, 0, 0, sizeof(Fake));
        RealTranslateMessage = (BOOL (WINAPI *)(const MSG *))GetProcAddress(GetModuleHandleA("user32.dll"), "TranslateMessage");
        if (fake) Patch(GetModuleHandleA(NULL));
        // FAKE_INPUT_SYSTEM=1 makes the system's own callers, such as the drag loop, see the fake pointer and buttons too.
        if (fake && GetEnvironmentVariableA("FAKE_INPUT_SYSTEM", name, sizeof(name))) {
            HMODULE user32 = GetModuleHandleA("user32.dll");
            const char *names[] = { "GetCursorPos", "GetPhysicalCursorPos", "GetKeyState", "GetAsyncKeyState" };
            void *fakes[] = { (void *)FakeGetCursorPos, (void *)FakeGetCursorPos, (void *)FakeGetKeyState, (void *)FakeGetKeyState };
            for (int i = 0; i < 4; ++i) {
                BYTE *at = (BYTE *)GetProcAddress(user32, names[i]);
                DWORD old;
                if (!at) continue;
                // mov rax, fake; jmp rax
                VirtualProtect(at, 12, PAGE_EXECUTE_READWRITE, &old);
                at[0] = 0x48; at[1] = 0xB8; *(void **)(at + 2) = fakes[i]; at[10] = 0xFF; at[11] = 0xE0;
                VirtualProtect(at, 12, old, &old);
                FlushInstructionCache(GetCurrentProcess(), at, 12);
            }
        }
    }
    return TRUE;
}
