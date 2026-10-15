// Shell context menus: the IContextMenu of a selection or folder background is read into P_MenuItems for Filey's own menu UI and invoked by command id.
// Drag and drop uses the shell's data objects and folder drop targets, so other programs see the same drags as Explorer's.
// Requires WinFiles.c and WinIcons.c (P__EnsureCom).

#pragma push_macro("function")
#undef function
#include <shlobj.h>
#include <winioctl.h>
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
            .cbSize = sizeof(info), .fMask = MIIM_FTYPE | MIIM_STATE | MIIM_ID | MIIM_SUBMENU | MIIM_STRING | MIIM_BITMAP,
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
            // Small handle values name system glyphs (HBMMENU_*), and HBMMENU_CALLBACK asks the owner to draw; only real bitmaps are pictures.
            if ((UINT_PTR)info.hbmpItem > (UINT_PTR)HBMMENU_POPUP_MINIMIZE && info.hbmpItem != HBMMENU_CALLBACK) {
                P__BitmapPixels(arena, info.hbmpItem, &item.icon);
            }
            // Submenu items can have verbs too ("windows.share"), but only plain items are invoked.
            if (info.wID >= P__MenuFirstId && info.wID <= P__MenuLastId) {
                if (info.hSubMenu == NULL) item.id = info.wID;
                wchar verb[64] = {0};
                if (SUCCEEDED(shell->menu->lpVtbl->GetCommandString(shell->menu, info.wID - P__MenuFirstId, GCS_VERBW, NULL, (LPSTR)verb, CArrayCount(verb) - 1)) && verb[0]) {
                    // Verbs are case-insensitive ("Windows.Share"); lowercase lets the program compare them directly.
                    for (wchar *c = verb; *c; ++c) {
                        if (*c >= L'A' && *c <= L'Z') *c = (wchar)(*c + (L'a' - L'A'));
                    }
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

// The folder that holds `paths`, which all share it, and their ids in it, for items such as drives whose folder has no path.
function IShellFolder *P__ShellParent(Arena *arena, const String *paths, u64 count, PIDLIST_ABSOLUTE *lists, PCUITEMID_CHILD *children)
{
    IShellFolder *result = NULL;
    b32 ok = count > 0;
    for (u64 i = 0; i < count && ok; ++i) {
        wchar *path = P__FilePathWide(arena, paths[i]);
        ok = path && SUCCEEDED(SHParseDisplayName(path, NULL, &lists[i], 0, NULL));
        if (ok) children[i] = ILFindLastID(lists[i]);
    }
    if (ok && FAILED(SHBindToParent(lists[0], &P__IID_IShellFolder, (void **)&result, NULL))) result = NULL;
    return result;
}

function b32 P_ContextMenuOpen(Arena *arena, String directory, const String *names, u64 count, P_ContextMenu *menu)
{
    *menu = (P_ContextMenu){0};
    P__EnsureCom();
    P__ShellMenu *shell = ArenaPushStruct(arena, P__ShellMenu);
    b32 ok = false;
    if (!directory.length) {
        // The items' own menus, with what their folder's background lacks, such as a drive's Eject and Format.
        Scratch scratch = GetScratchEx(arena);
        PIDLIST_ABSOLUTE *lists = ArenaPushArray(scratch.arena, Max(count, (u64)1), PIDLIST_ABSOLUTE);
        PCUITEMID_CHILD *children = ArenaPushArray(scratch.arena, Max(count, (u64)1), PCUITEMID_CHILD);
        IShellFolder *parent = P__ShellParent(scratch.arena, names, count, lists, children);
        ok = parent && SUCCEEDED(parent->lpVtbl->GetUIObjectOf(parent, P__shellOwner, (UINT)count, children, &P__IID_IContextMenu, NULL, (void **)&shell->menu));
        if (parent) parent->lpVtbl->Release(parent);
        for (u64 i = 0; i < count; ++i) ILFree(lists[i]);
        ReleaseScratch(scratch);
    } else {
        shell->directory = P__FilePathWide(arena, directory);
        IShellFolder *folder = P__ShellFolder(shell->directory);
        ok = folder != NULL;
        if (ok && count == 0) {
            ok = SUCCEEDED(folder->lpVtbl->CreateViewObject(folder, P__shellOwner, &P__IID_IContextMenu, (void **)&shell->menu));
        } else if (ok) {
            ok = P__ShellChildren(folder, names, count, &P__IID_IContextMenu, (void **)&shell->menu);
        }
        if (folder) folder->lpVtbl->Release(folder);
    }

    if (ok) {
        shell->popup = CreatePopupMenu();
        // Rename is offered for items in a folder, which the program then edits in place; a drive's label is not edited that way.
        ok = shell->popup && SUCCEEDED(shell->menu->lpVtbl->QueryContextMenu(shell->menu, shell->popup, 0, P__MenuFirstId, P__MenuLastId,
                                                                             CMF_NORMAL | (count && directory.length ? CMF_CANRENAME : 0)));
    }
    if (ok) {
        shell->menu->lpVtbl->QueryInterface(shell->menu, &P__IID_IContextMenu3, (void **)&shell->menu3);
        P__ArrayMenuItem items = {0};
        u32 first = 0;
        P__MenuRead(arena, shell, shell->popup, &items, &first, &menu->rootCount);
        // Dropped from the top level: view commands that the browser has its own versions of or does not support, and sharing on the folder background.
        // An item without a label is not shown.
        local_persist const String dropped[] = { S("groupby"), S("pintohome"), S("viewcustomwizard"), S("undo"), S("redo") };
        for (u32 i = 0; i < menu->rootCount; ++i) {
            P_MenuItem *item = &items.v[i];
            b32 drop = count == 0 && StrIsEqual(item->verb, S("windows.share"));
            for (u32 j = 0; j < CArrayCount(dropped) && !drop; ++j) drop = StrIsEqual(item->verb, dropped[j]);
            if (drop) item->label = (String){0};
        }
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

// Drags over the window are forwarded to the shell drop target of the folder Filey points at, which copies, moves, links and asks like Explorer does.
// WinWindow.c registers the window's IDropTarget that calls these.
typedef struct P__Drop {
    IDataObject *data;    // the drag over the window
    IDropTarget *shell;   // drop target of `bound`, entered with `data`
    Arena wantedArena, boundArena;
    String wanted;        // from P_SetDropDirectory
    String bound;         // the folder `shell` belongs to
    b32 entered;          // `bound` was chosen during this drag
    b32 dragging;         // a drag started by P_DragFiles is running
    b32 takesFolders;     // drops are kept for the program instead of going to `wanted`
    Arena droppedArena;
    ArrayStr dropped;     // folders dropped while `takesFolders` was on, until the program takes them
} P__Drop;

global P__Drop P__drop;

function void P_SetDropDirectory(String path)
{
    if (StrIsEqual(path, P__drop.wanted)) return;
    ArenaClear(&P__drop.wantedArena);
    P__drop.wanted = StrCopy(&P__drop.wantedArena, path);
}

function void P_SetDropTakesFolders(b32 takes) { P__drop.takesFolders = takes; }

function b32 P_DropPending(void) { return P__drop.data != NULL; }

function ArrayStr P_TakeDroppedFolders(Arena *arena)
{
    ArrayStr result = {0};
    for (u64 i = 0; i < P__drop.dropped.header.count; ++i) ArrayPush(arena, result, StrCopy(arena, P__drop.dropped.v[i]));
    P__drop.dropped = (ArrayStr){0};
    ArenaClear(&P__drop.droppedArena);
    return result;
}

// The folders among the files `data` carries, with forward slashes.
function void P__DropKeepFolders(IDataObject *data)
{
    FORMATETC format = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    STGMEDIUM medium = {0};
    if (FAILED(data->lpVtbl->GetData(data, &format, &medium))) return;
    HDROP drop = (HDROP)medium.hGlobal;
    u32 count = DragQueryFileW(drop, 0xFFFFFFFF, NULL, 0);
    for (u32 i = 0; i < count; ++i) {
        wchar path[1024];
        u32 length = DragQueryFileW(drop, i, path, (u32)CArrayCount(path));
        if (!length || length >= CArrayCount(path)) continue;
        DWORD attributes = GetFileAttributesW(path);
        if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        String folder = P__FileStringWide(&P__drop.droppedArena, path, (i32)length);
        for (u64 at = 0; at < folder.length; ++at) {
            if (folder.value[at] == '\\') ((char *)folder.value)[at] = '/';
        }
        ArrayPush(&P__drop.droppedArena, P__drop.dropped, folder);
    }
    ReleaseStgMedium(&medium);
}

function b32 P__DropHasFiles(IDataObject *data)
{
    FORMATETC format = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    return SUCCEEDED(data->lpVtbl->QueryGetData(data, &format));
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
    // Pinning only links to the folders, so the cursor says link where the source allows it.
    if (P__drop.takesFolders) {
        P__DropLeaveShell();
        P__drop.entered = false;
        if (P__DropHasFiles(P__drop.data)) *effect = (allowed & DROPEFFECT_LINK) ? DROPEFFECT_LINK : allowed & DROPEFFECT_COPY;
        return;
    }
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
    if (P__drop.takesFolders && *effect != DROPEFFECT_NONE) {
        P__DropKeepFolders(P__drop.data);
        result = true;
    } else if (P__drop.shell && *effect != DROPEFFECT_NONE) {
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

// Known folder ids, spelled out so that no uuid library is linked for them.
global const GUID P__FolderProfile   = { 0x5E6C858F, 0x0E22, 0x4760, { 0x9A, 0xFE, 0xEA, 0x33, 0x17, 0xB6, 0x71, 0x73 } };
global const GUID P__FolderDesktop   = { 0xB4BFCC3A, 0xDB2C, 0x424C, { 0xB0, 0x29, 0x7F, 0xE9, 0x9A, 0x87, 0xC6, 0x41 } };
global const GUID P__FolderDocuments = { 0xFDD39AD0, 0x238F, 0x46AF, { 0xAD, 0xB4, 0x6C, 0x85, 0x48, 0x03, 0x69, 0xC7 } };
global const GUID P__FolderDownloads = { 0x374DE290, 0x123F, 0x4565, { 0x91, 0x64, 0x39, 0xC4, 0x92, 0x5E, 0x46, 0x7B } };
global const GUID P__FolderMusic     = { 0x4BD8D571, 0x6D19, 0x48D3, { 0xBE, 0x97, 0x42, 0x22, 0x20, 0x08, 0x0E, 0x43 } };
global const GUID P__FolderPictures  = { 0x33E28130, 0x4E1E, 0x4676, { 0x83, 0x5A, 0x98, 0x39, 0x5C, 0x3B, 0xC3, 0xBB } };
global const GUID P__FolderVideos    = { 0x18989B1D, 0x99B5, 0x455B, { 0x84, 0x1C, 0xAB, 0x7C, 0x74, 0xE4, 0xDD, 0xFC } };
global const GUID P__FolderLocalData = { 0xF1B32785, 0x6FBA, 0x4FCF, { 0x9D, 0x55, 0x7B, 0x8E, 0x7F, 0x15, 0x70, 0x91 } };
global const GUID P__FolderRoamingData = { 0x3EB685DB, 0x65F9, 0x4CF6, { 0xA0, 0x3A, 0xE3, 0xEF, 0x65, 0x72, 0x9F, 0x3D } };

function String P__KnownFolder(Arena *arena, const GUID *id)
{
    wchar *wide = NULL;
    String result = {0};
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, NULL, &wide))) {
        Scratch scratch = GetScratchEx(arena);
        result = P_AbsolutePath(arena, P__FileStringWide(scratch.arena, wide, lstrlenW(wide)));
        ReleaseScratch(scratch);
    }
    CoTaskMemFree(wide);
    return result;
}

function P_DriveKind P_PathDriveKind(String path)
{
    if (path.length >= 2 && path.value[0] == '/' && path.value[1] == '/') return P_DriveKind_Network;
    if (path.length < 2 || path.value[1] != ':') return P_DriveKind_SolidState;
    wchar root[] = { (wchar)path.value[0], L':', L'\\', 0 }, volume[] = { L'\\', L'\\', L'.', L'\\', (wchar)path.value[0], L':', 0 };
    if (GetDriveTypeW(root) == DRIVE_REMOTE) return P_DriveKind_Network;
    // The volume opened without access rights still answers whether it seeks, without elevation.
    HANDLE handle = CreateFileW(volume, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (handle == INVALID_HANDLE_VALUE) return P_DriveKind_SolidState;
    STORAGE_PROPERTY_QUERY query = { .PropertyId = StorageDeviceSeekPenaltyProperty, .QueryType = PropertyStandardQuery };
    DEVICE_SEEK_PENALTY_DESCRIPTOR seek = {0};
    DWORD returned = 0;
    b32 answered = DeviceIoControl(handle, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), &seek, sizeof(seek), &returned, NULL) && returned >= sizeof(seek);
    CloseHandle(handle);
    return answered && seek.IncursSeekPenalty ? P_DriveKind_Spinning : P_DriveKind_SolidState;
}

function b32 P_PrefersDarkColors(void)
{
    DWORD light = 0, size = sizeof(light);
    LSTATUS status = RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme",
                                  RRF_RT_REG_DWORD, NULL, &light, &size);
    return status != ERROR_SUCCESS || light == 0;
}

function String P_TrashFolder(Arena *arena)
{
    wchar drive[8] = {0};
    u32 length = GetEnvironmentVariableW(L"SystemDrive", drive, (u32)CArrayCount(drive));
    if (!length || length >= CArrayCount(drive)) return S("C:/$Recycle.Bin");
    Scratch scratch = GetScratchEx(arena);
    String result = StrFormat(arena, "%S/$Recycle.Bin", P__FileStringWide(scratch.arena, drive, (i32)length));
    ReleaseScratch(scratch);
    return result;
}

function b32 P_IsTrashFolder(String path)
{
    while (path.length > 3 && path.value[path.length - 1] == '/') --path.length;
    String bin = S("/$recycle.bin");
    if (path.length != 2 + bin.length || path.value[1] != ':') return false;
    for (u64 i = 0; i < bin.length; ++i) {
        u8 c = (u8)path.value[2 + i];
        if ((c >= 'A' && c <= 'Z' ? c + 32 : c) != (u8)bin.value[i]) return false;
    }
    return true;
}

// The bin keeps each item as $R<code>, and beside it $I<code>: a version, the size, the deletion time and the original path,
// 260 UTF-16 units in version 1 and counted in version 2.
function P_ArrayTrashed P_ListTrash(Arena *arena, String path, const P_CancelToken *cancel)
{
    P_ArrayTrashed result = {0};
    Scratch scratch = GetScratchEx(arena);
    P_DirectoryResult users = P_EnumerateDirectory(scratch.arena, path, cancel);
    for (u64 u = 0; u < users.files.header.count; ++u) {
        if (users.files.v[u].kind != P_FileKind_Directory) continue;
        String folder = StrFormat(scratch.arena, "%S/%S", path, users.files.v[u].name);
        P_DirectoryResult items = P_EnumerateDirectory(scratch.arena, folder, cancel);
        for (u64 i = 0; i < items.files.header.count; ++i) {
            String name = items.files.v[i].name;
            if (name.length < 3 || name.value[0] != '$' || name.value[1] != 'I') continue;
            const P_File *kept = NULL;
            for (u64 j = 0; j < items.files.header.count && !kept; ++j) {
                String other = items.files.v[j].name;
                if (other.length == name.length && other.value[1] == 'R' && StrIsEqual(StrFrom(other, 2), StrFrom(name, 2))) kept = &items.files.v[j];
            }
            String record = {0};
            if (!kept || !P_ReadFile(scratch.arena, StrFormat(scratch.arena, "%S/%S", folder, name), &record) || record.length < 24) continue;
            i64 version = 0, size = 0, deleted = 0;
            MemCopy(&version, record.value, 8);
            MemCopy(&size, record.value + 8, 8);
            MemCopy(&deleted, record.value + 16, 8);
            u64 at = 24, units = 260;
            if (version == 2 && record.length >= 28) {
                u32 count = 0;
                MemCopy(&count, record.value + 24, 4);
                units = count, at = 28;
            }
            units = Min(units, (record.length - at) / 2);
            const wchar *wide = (const wchar *)(record.value + at);
            i32 length = 0;
            while ((u64)length < units && wide[length]) ++length;
            String original = P_AbsolutePath(scratch.arena, P__FileStringWide(scratch.arena, wide, length));
            Index slash = StrIndexChar(original, '/', SSF_SearchBackwards);
            if (!slash.exists) continue;
            P_Trashed *item = ArrayAddClear(arena, result, 1);
            item->file = *kept;
            item->file.name = StrFormat(arena, "%S/%S", folder, kept->name);
            if (kept->kind == P_FileKind_Directory) item->file.size = (u64)size;
            item->name = StrCopy(arena, StrFrom(original, slash.value + 1));
            item->original = StrCopy(arena, StrFromTo(original, 0, slash.value ? (i64)slash.value : 1));
            item->deletedTimeUnix = deleted > 116444736000000000LL ? (deleted - 116444736000000000LL) / 10000000 : 0;
        }
    }
    ReleaseScratch(scratch);
    return result;
}

function P_ArrayPlace P_UserPlaces(Arena *arena)
{
    local_persist const struct { const GUID *id; const char *name; } folders[] = {
        { &P__FolderProfile, "Home" }, { &P__FolderDesktop, "Desktop" }, { &P__FolderDocuments, "Documents" }, { &P__FolderDownloads, "Downloads" },
        { &P__FolderMusic, "Music" }, { &P__FolderPictures, "Pictures" }, { &P__FolderVideos, "Videos" },
    };
    P_ArrayPlace result = {0};
    for (u32 i = 0; i < CArrayCount(folders); ++i) {
        String path = P__KnownFolder(arena, folders[i].id);
        if (path.length) ArrayPush(arena, result, ((P_Place){ .name = StrFromCStr(folders[i].name), .path = path }));
    }
    ArrayPush(arena, result, ((P_Place){ .name = S("$Recycle.Bin"), .path = P_TrashFolder(arena) }));
    return result;
}

function ArrayStr P_PathRoots(Arena *arena)
{
    ArrayStr result = {0};
    P_ArrayPlace places = P_UserPlaces(arena);
    for (u64 i = 0; i < places.header.count; ++i) ArrayPush(arena, result, places.v[i].path);
    String local = P__KnownFolder(arena, &P__FolderLocalData), roaming = P__KnownFolder(arena, &P__FolderRoamingData);
    if (local.length) ArrayPush(arena, result, local);
    if (roaming.length) ArrayPush(arena, result, roaming);
    return result;
}

// What a drive says about itself, asked on a thread of its own, since a drive spinning up a disc answers only once it has.
// The asker and the thread share it, and the last one done frees it.
typedef struct P__DriveAnswer {
    atomic_int users;
    HANDLE done;
    wchar drive[4];
    wchar name[MAX_PATH], type[80], system[MAX_PATH + 1];
    ULARGE_INTEGER available, total;
    b32 sized, formatted;
} P__DriveAnswer;

function void P__DriveAnswerRelease(P__DriveAnswer *answer)
{
    if (atomic_fetch_sub(&answer->users, 1) != 1) return;
    CloseHandle(answer->done);
    HeapFree(GetProcessHeap(), 0, answer);
}

function DWORD WINAPI P__DriveAnswerThread(void *user)
{
    P__DriveAnswer *answer = user;
    // Drives without a medium fail quietly instead of asking for one.
    SetThreadErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX, NULL);
    b32 com = SUCCEEDED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED));
    // "Local Disk (C:)", as Explorer names it.
    SHFILEINFOW info = {0};
    if (SHGetFileInfoW(answer->drive, 0, &info, sizeof(info), SHGFI_DISPLAYNAME)) lstrcpynW(answer->name, info.szDisplayName, (i32)CArrayCount(answer->name));
    if (SHGetFileInfoW(answer->drive, 0, &info, sizeof(info), SHGFI_TYPENAME)) lstrcpynW(answer->type, info.szTypeName, (i32)CArrayCount(answer->type));
    answer->sized = GetDiskFreeSpaceExW(answer->drive, &answer->available, &answer->total, NULL);
    answer->formatted = GetVolumeInformationW(answer->drive, NULL, 0, NULL, NULL, NULL, answer->system, (u32)CArrayCount(answer->system));
    if (com) CoUninitialize();
    SetEvent(answer->done);
    P__DriveAnswerRelease(answer);
    return 0;
}

function P_ArrayPlace P_Drives(Arena *arena)
{
    P_ArrayPlace result = {0};
    wchar drives[512];
    u32 length = GetLogicalDriveStringsW((u32)CArrayCount(drives) - 1, drives);
    if (!length || length >= CArrayCount(drives)) return result;
    // Every drive is asked at once, and one that has not answered in a moment is listed as busy, so a disc spinning up holds nothing up.
    P__DriveAnswer *answers[26] = {0};
    u32 types[26] = {0}, count = 0;
    for (wchar *drive = drives; *drive && count < CArrayCount(answers); drive += lstrlenW(drive) + 1) {
        u32 type = GetDriveTypeW(drive);
        if (type == DRIVE_NO_ROOT_DIR || type == DRIVE_UNKNOWN || lstrlenW(drive) >= 4) continue;
        P__DriveAnswer *answer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(P__DriveAnswer));
        if (!answer) continue;
        lstrcpynW(answer->drive, drive, 4);
        answer->done = CreateEventW(NULL, TRUE, FALSE, NULL);
        atomic_store(&answer->users, 2);
        HANDLE thread = answer->done ? CreateThread(NULL, 0, P__DriveAnswerThread, answer, 0, NULL) : NULL;
        if (!thread) {
            if (answer->done) CloseHandle(answer->done);
            HeapFree(GetProcessHeap(), 0, answer);
            continue;
        }
        CloseHandle(thread);
        types[count] = type;
        answers[count++] = answer;
    }
    u64 deadline = PlatformNowMicroseconds() + 300000;
    Scratch scratch = GetScratchEx(arena);
    for (u32 i = 0; i < count; ++i) {
        P__DriveAnswer *answer = answers[i];
        u64 now = PlatformNowMicroseconds();
        b32 answered = WaitForSingleObject(answer->done, now < deadline ? (DWORD)((deadline - now) / 1000) : 0) == WAIT_OBJECT_0;
        // A fixed disk without a file system to read, such as one being formatted, is left out as the reference does; removable ones without a medium stay.
        if (answered && types[i] == DRIVE_FIXED && !answer->formatted) {
            P__DriveAnswerRelease(answer);
            continue;
        }
        P_Place place = { .path = P_AbsolutePath(arena, P__FileStringWide(scratch.arena, answer->drive, lstrlenW(answer->drive))), .busy = (b8)!answered };
        place.name = answered && answer->name[0] ? P__FileStringWide(arena, answer->name, lstrlenW(answer->name)) : place.path;
        if (answered) {
            if (answer->sized) {
                place.totalBytes = answer->total.QuadPart;
                place.freeBytes = answer->available.QuadPart;
            }
            if (answer->type[0]) place.type = P__FileStringWide(arena, answer->type, lstrlenW(answer->type));
            if (answer->formatted) place.fileSystem = P__FileStringWide(arena, answer->system, lstrlenW(answer->system));
        }
        ArrayPush(arena, result, place);
        P__DriveAnswerRelease(answer);
    }
    ReleaseScratch(scratch);
    return result;
}

function String P_ConfigDirectory(Arena *arena)
{
    wchar override[1024];
    u32 length = GetEnvironmentVariableW(L"FILEY_CONFIG_DIR", override, (u32)CArrayCount(override));
    String result = {0};
    if (length && length < CArrayCount(override)) {
        Scratch scratch = GetScratchEx(arena);
        result = P_AbsolutePath(arena, P__FileStringWide(scratch.arena, override, (i32)length));
        ReleaseScratch(scratch);
    } else {
        String local = P__KnownFolder(arena, &P__FolderLocalData);
        if (!local.length) return local;
        result = StrFormat(arena, "%S/Filey", local);
    }
    Scratch scratch = GetScratchEx(arena);
    wchar *wide = P__FilePathWide(scratch.arena, result);
    b32 ok = wide && (CreateDirectoryW(wide, NULL) || GetLastError() == ERROR_ALREADY_EXISTS);
    ReleaseScratch(scratch);
    return ok ? result : (String){0};
}
