// Shell context menus: the IContextMenu of a selection or folder background is
// read into P_MenuItems for Filey's own menu UI and invoked by command id.
// Drag and drop uses the shell's data objects and folder drop targets, so other
// programs see the same drags as Explorer's.
// Requires WinFiles.c and WinIcons.c (P__EnsureCom).

#pragma push_macro("function")
#undef function
#include <shlobj.h>
#pragma pop_macro("function")

// Set by WinWindow.c; dialogs that verbs open are owned by it.
global HWND P__shellOwner;

global const IID P__IID_IShellFolder  = { 0x000214E6, 0x0000, 0x0000, { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
global const IID P__IID_IContextMenu  = { 0x000214E4, 0x0000, 0x0000, { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
global const IID P__IID_IContextMenu3 = { 0xBCFCE0A0, 0xEC17, 0x11D0, { 0x8D, 0x10, 0x00, 0xA0, 0xC9, 0x0F, 0x27, 0x19 } };
global const IID P__IID_IUnknown      = { 0x00000000, 0x0000, 0x0000, { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
global const IID P__IID_IDataObject   = { 0x0000010E, 0x0000, 0x0000, { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
global const IID P__IID_IDropTarget   = { 0x00000122, 0x0000, 0x0000, { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };

#define P__MenuFirstId 1
#define P__MenuLastId  0x7FFF

typedef struct P__ShellMenu {
    IContextMenu *menu;
    IContextMenu3 *menu3;  // NULL when the handler does not populate submenus lazily
    HMENU popup;
    wchar *directory;
} P__ShellMenu;

typedef struct P__ArrayMenuItem { ArrayHeader header; P_MenuItem *v; } P__ArrayMenuItem;

// "&Open\tCtrl+O" -> "Open": accelerator markers and the shortcut column are dropped.
function String P__MenuLabel(Arena *arena, const wchar *text)
{
    wchar clean[256];
    u32 length = 0;
    for (u32 i = 0; text[i] && text[i] != L'\t' && length < CArrayCount(clean) - 1; ++i) {
        if (text[i] == L'&') {
            if (text[i + 1] != L'&') continue;
            ++i;
        }
        clean[length++] = text[i];
    }
    return P__FileStringWide(arena, clean, (i32)length);
}

// Appends `popup`'s items as one contiguous block, then the blocks of its submenus.
function void P__MenuRead(Arena *arena, P__ShellMenu *shell, HMENU popup, P__ArrayMenuItem *items, u32 *first, u32 *count)
{
    i32 itemCount = GetMenuItemCount(popup);
    *first = (u32)items->header.count;
    *count = itemCount > 0 ? (u32)itemCount : 0;
    if (!*count) return;
    ArrayAddClear(arena, *items, *count);
    for (u32 i = 0; i < *count; ++i) {
        wchar text[256] = {0};
        MENUITEMINFOW info = {
            .cbSize = sizeof(info), .fMask = MIIM_FTYPE | MIIM_STATE | MIIM_ID | MIIM_SUBMENU | MIIM_STRING,
            .dwTypeData = text, .cch = CArrayCount(text) - 1,
        };
        if (!GetMenuItemInfoW(popup, i, TRUE, &info)) continue;
        P_MenuItem item = {
            .separator = (info.fType & MFT_SEPARATOR) != 0,
            .disabled = (info.fState & MFS_DISABLED) != 0,
            .isDefault = (info.fState & MFS_DEFAULT) != 0,
        };
        if (!item.separator) {
            item.label = P__MenuLabel(arena, text);
            if (info.hSubMenu == NULL && info.wID >= P__MenuFirstId && info.wID <= P__MenuLastId) {
                item.id = info.wID;
                wchar verb[64] = {0};
                if (SUCCEEDED(shell->menu->lpVtbl->GetCommandString(shell->menu, info.wID - P__MenuFirstId, GCS_VERBW, NULL, (LPSTR)verb, CArrayCount(verb) - 1)) && verb[0]) {
                    item.verb = P__FileStringWide(arena, verb, (i32)wcslen(verb));
                }
            }
        }
        if (info.hSubMenu) {
            // "Send to" and "Open with" fill their submenus only when they are about to show.
            if (shell->menu3) {
                LRESULT ignored = 0;
                shell->menu3->lpVtbl->HandleMenuMsg2(shell->menu3, WM_INITMENUPOPUP, (WPARAM)info.hSubMenu, MAKELPARAM(i, FALSE), &ignored);
            }
            P__MenuRead(arena, shell, info.hSubMenu, items, &item.firstChild, &item.childCount);
        }
        items->v[*first + i] = item;
    }
}

function IShellFolder *P__ShellFolder(const wchar *path)
{
    IShellFolder *result = NULL;
    PIDLIST_ABSOLUTE list = NULL;
    if (path && SUCCEEDED(SHParseDisplayName(path, NULL, &list, 0, NULL))) {
        if (FAILED(SHBindToObject(NULL, list, NULL, &P__IID_IShellFolder, (void **)&result))) result = NULL;
        ILFree(list);
    }
    return result;
}

// A shell object (`iid`) for `names` inside `folder`, such as their context menu or data object.
function b32 P__ShellChildren(IShellFolder *folder, const String *names, u64 count, const IID *iid, void **object)
{
    Scratch scratch = GetScratch();
    PITEMID_CHILD *children = ArenaPushArray(scratch.arena, Max(count, (u64)1), PITEMID_CHILD);
    u64 parsed = 0;
    b32 ok = true;
    for (; parsed < count && ok; ++parsed) {
        wchar *name = P__FilePathWide(scratch.arena, names[parsed]);
        ok = name && SUCCEEDED(folder->lpVtbl->ParseDisplayName(folder, P__shellOwner, NULL, name, NULL, (PIDLIST_RELATIVE *)&children[parsed], NULL));
    }
    ok = ok && SUCCEEDED(folder->lpVtbl->GetUIObjectOf(folder, P__shellOwner, (UINT)count, (PCUITEMID_CHILD_ARRAY)children, iid, NULL, object));
    for (u64 i = 0; i < parsed; ++i) ILFree(children[i]);
    ReleaseScratch(scratch);
    return ok;
}

function b32 P_ContextMenuOpen(Arena *arena, String directory, const String *names, u64 count, P_ContextMenu *menu)
{
    *menu = (P_ContextMenu){0};
    P__EnsureCom();
    P__ShellMenu *shell = ArenaPushStruct(arena, P__ShellMenu);
    shell->directory = P__FilePathWide(arena, directory);
    IShellFolder *folder = P__ShellFolder(shell->directory);
    b32 ok = folder != NULL;
    if (ok && count == 0) {
        ok = SUCCEEDED(folder->lpVtbl->CreateViewObject(folder, P__shellOwner, &P__IID_IContextMenu, (void **)&shell->menu));
    } else if (ok) {
        ok = P__ShellChildren(folder, names, count, &P__IID_IContextMenu, (void **)&shell->menu);
    }
    if (folder) folder->lpVtbl->Release(folder);

    if (ok) {
        shell->popup = CreatePopupMenu();
        ok = shell->popup && SUCCEEDED(shell->menu->lpVtbl->QueryContextMenu(shell->menu, shell->popup, 0, P__MenuFirstId, P__MenuLastId, CMF_NORMAL));
    }
    if (ok) {
        shell->menu->lpVtbl->QueryInterface(shell->menu, &P__IID_IContextMenu3, (void **)&shell->menu3);
        P__ArrayMenuItem items = {0};
        u32 first = 0;
        P__MenuRead(arena, shell, shell->popup, &items, &first, &menu->rootCount);
        menu->items = items.v;
        menu->count = (u32)items.header.count;
        menu->native = shell;
    } else {
        menu->native = shell;
        P_ContextMenuClose(menu);
    }
    return ok;
}

function P_FileError P_ContextMenuInvoke(Arena *arena, P_ContextMenu *menu, u32 id)
{
    P__ShellMenu *shell = menu->native;
    if (shell == NULL || shell->menu == NULL || id < P__MenuFirstId || id > P__MenuLastId) {
        return P__FileError(arena, ERROR_INVALID_PARAMETER);
    }
    CMINVOKECOMMANDINFOEX info = {
        .cbSize = sizeof(info),
        .fMask = CMIC_MASK_UNICODE,
        .hwnd = P__shellOwner,
        .lpVerb = MAKEINTRESOURCEA(id - P__MenuFirstId),
        .lpVerbW = MAKEINTRESOURCEW(id - P__MenuFirstId),
        .lpDirectoryW = shell->directory,
        .nShow = SW_SHOWNORMAL,
    };
    HRESULT result = shell->menu->lpVtbl->InvokeCommand(shell->menu, (CMINVOKECOMMANDINFO *)&info);
    // Dismissing a verb's own dialog is not an error.
    if (SUCCEEDED(result) || result == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return (P_FileError){0};
    return P__FileError(arena, HRESULT_FACILITY(result) == FACILITY_WIN32 ? (u32)HRESULT_CODE(result) : (u32)result);
}

function void P_ContextMenuClose(P_ContextMenu *menu)
{
    P__ShellMenu *shell = menu->native;
    if (shell) {
        if (shell->menu3) shell->menu3->lpVtbl->Release(shell->menu3);
        if (shell->menu) shell->menu->lpVtbl->Release(shell->menu);
        if (shell->popup) DestroyMenu(shell->popup);
    }
    *menu = (P_ContextMenu){0};
}

// Drags over the window are forwarded to the shell drop target of the folder
// Filey points at, which copies, moves, links and asks like Explorer does.
// WinWindow.c registers the window's IDropTarget that calls these.
typedef struct P__Drop {
    IDataObject *data;    // the drag over the window
    IDropTarget *shell;   // drop target of `bound`, entered with `data`
    Arena wantedArena, boundArena;
    String wanted;        // from P_SetDropDirectory
    String bound;         // the folder `shell` belongs to
    b32 entered;          // `bound` was chosen during this drag
    b32 dragging;         // a drag started by P_DragFiles is running
} P__Drop;

global P__Drop P__drop;

function void P_SetDropDirectory(String path)
{
    if (StrIsEqual(path, P__drop.wanted)) return;
    ArenaClear(&P__drop.wantedArena);
    P__drop.wanted = StrCopy(&P__drop.wantedArena, path);
}

function void P__DropLeaveShell(void)
{
    if (P__drop.shell) {
        P__drop.shell->lpVtbl->DragLeave(P__drop.shell);
        P__drop.shell->lpVtbl->Release(P__drop.shell);
        P__drop.shell = NULL;
    }
}

function void P__DropLeave(void)
{
    P__DropLeaveShell();
    if (P__drop.data) P__drop.data->lpVtbl->Release(P__drop.data);
    P__drop.data = NULL;
}

function void P__DropEnter(IDataObject *data)
{
    P__DropLeave();
    P__drop.data = data;
    data->lpVtbl->AddRef(data);
    P__drop.entered = false;
}

// `effect` holds the source's allowed effects on input and the chosen one on output.
function void P__DropOver(DWORD keys, POINTL point, DWORD *effect)
{
    DWORD allowed = *effect;
    *effect = DROPEFFECT_NONE;
    if (!P__drop.data) return;
    if (!P__drop.entered || !StrIsEqual(P__drop.bound, P__drop.wanted)) {
        P__DropLeaveShell();
        ArenaClear(&P__drop.boundArena);
        P__drop.bound = StrCopy(&P__drop.boundArena, P__drop.wanted);
        P__drop.entered = true;
        Scratch scratch = GetScratch();
        IShellFolder *folder = P__drop.bound.length ? P__ShellFolder(P__FilePathWide(scratch.arena, P__drop.bound)) : NULL;
        IDropTarget *shell = NULL;
        if (folder && SUCCEEDED(folder->lpVtbl->CreateViewObject(folder, P__shellOwner, &P__IID_IDropTarget, (void **)&shell))) {
            DWORD entered = allowed;
            if (SUCCEEDED(shell->lpVtbl->DragEnter(shell, P__drop.data, keys, point, &entered))) P__drop.shell = shell;
            else shell->lpVtbl->Release(shell);
        }
        if (folder) folder->lpVtbl->Release(folder);
        ReleaseScratch(scratch);
    }
    if (P__drop.shell) {
        *effect = allowed;
        if (FAILED(P__drop.shell->lpVtbl->DragOver(P__drop.shell, keys, point, effect))) *effect = DROPEFFECT_NONE;
    }
}

function b32 P__DropDrop(DWORD keys, POINTL point, DWORD *effect)
{
    P__DropOver(keys, point, effect);
    b32 result = false;
    if (P__drop.shell && *effect != DROPEFFECT_NONE) {
        result = SUCCEEDED(P__drop.shell->lpVtbl->Drop(P__drop.shell, P__drop.data, keys, point, effect));
        // Drop ends the shell target's drag; it must not also get DragLeave.
        P__drop.shell->lpVtbl->Release(P__drop.shell);
        P__drop.shell = NULL;
    }
    P__DropLeave();
    return result;
}

function b32 P_DragFiles(String directory, const String *names, u64 count)
{
    if (!count || P__drop.dragging) return false;
    P__EnsureCom();
    Scratch scratch = GetScratch();
    IShellFolder *folder = P__ShellFolder(P__FilePathWide(scratch.arena, directory));
    IDataObject *data = NULL;
    b32 result = false;
    if (folder && P__ShellChildren(folder, names, count, &P__IID_IDataObject, (void **)&data)) {
        DWORD effect = DROPEFFECT_NONE;
        P__drop.dragging = true;
        // A NULL drop source selects the shell's own, which also draws the drag image.
        result = SHDoDragDrop(P__shellOwner, data, NULL, DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK, &effect) == DRAGDROP_S_DROP;
        P__drop.dragging = false;
        data->lpVtbl->Release(data);
    }
    if (folder) folder->lpVtbl->Release(folder);
    ReleaseScratch(scratch);
    return result;
}

function void P_ContextMenuWarm(String directory, String name)
{
    Arena arena = {0};
    P_ContextMenu menu = {0};
    if (P_ContextMenuOpen(&arena, directory, NULL, 0, &menu)) P_ContextMenuClose(&menu);
    if (name.length && P_ContextMenuOpen(&arena, directory, &name, 1, &menu)) P_ContextMenuClose(&menu);
    ArenaRelease(&arena);
}
