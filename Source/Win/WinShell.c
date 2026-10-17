// Shell context menus: the IContextMenu of a selection or folder background is read into P_MenuItems for Filey's own menu UI and invoked by command id.
// Drag and drop uses the shell's data objects and folder drop targets, so other programs see the same drags as Explorer's.
// Requires WinFiles.c and WinIcons.c (P__EnsureCom).

#pragma push_macro("function")
#undef function
#include <shlobj.h>
#include <shlwapi.h>
#include <winioctl.h>
#include <winver.h>
#pragma pop_macro("function")

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

// The Recycle Bin's own items for the bin files at `paths`, or all of them when `count` is 0, as the shell lists them: their menu is the
// bin's, with Restore, and their Restore tells every window. NULL when the shell lists none of them, as for a bin it does not know.
function IContextMenu *P__RecycleBinMenu(Arena *arena, const String *paths, u64 count)
{
    PIDLIST_ABSOLUTE binList = NULL;
    IShellFolder *bin = NULL;
    if (FAILED(SHGetSpecialFolderLocation(NULL, CSIDL_BITBUCKET, &binList))) return NULL;
    if (FAILED(SHBindToObject(NULL, binList, NULL, &P__IID_IShellFolder, (void **)&bin))) bin = NULL;
    ILFree(binList);
    if (!bin) return NULL;
    Scratch scratch = GetScratchEx(arena);
    wchar **wanted = ArenaPushArray(scratch.arena, Max(count, (u64)1), wchar *);
    for (u64 i = 0; i < count; ++i) wanted[i] = P__FilePathWide(scratch.arena, paths[i]);
    struct { ArrayHeader header; PITEMID_CHILD *v; } kept = {0};
    IEnumIDList *items = NULL;
    if (SUCCEEDED(bin->lpVtbl->EnumObjects(bin, P__shellOwner, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS | SHCONTF_INCLUDEHIDDEN, &items)) && items) {
        PITEMID_CHILD child = NULL;
        while (items->lpVtbl->Next(items, 1, &child, NULL) == S_OK) {
            b32 take = !count;
            STRRET text = {0};
            wchar *name = NULL;
            if (!take && SUCCEEDED(bin->lpVtbl->GetDisplayNameOf(bin, child, SHGDN_FORPARSING, &text)) && SUCCEEDED(StrRetToStrW(&text, child, &name))) {
                for (u64 i = 0; i < count && !take; ++i) take = wanted[i] && CompareStringOrdinal(name, -1, wanted[i], -1, TRUE) == CSTR_EQUAL;
                CoTaskMemFree(name);
            }
            if (take) ArrayPush(scratch.arena, kept, child);
            else ILFree(child);
        }
        items->lpVtbl->Release(items);
    }
    IContextMenu *result = NULL;
    if (kept.header.count && FAILED(bin->lpVtbl->GetUIObjectOf(bin, P__shellOwner, (UINT)kept.header.count, (PCUITEMID_CHILD_ARRAY)kept.v, &P__IID_IContextMenu, NULL, (void **)&result))) {
        result = NULL;
    }
    for (u64 i = 0; i < kept.header.count; ++i) ILFree(kept.v[i]);
    bin->lpVtbl->Release(bin);
    ReleaseScratch(scratch);
    return result;
}

// Whether `path` is inside a drive's $Recycle.Bin.
function b32 P__InRecycleBin(String path)
{
    String bin = S("X:/$Recycle.Bin");
    return path.length > bin.length && path.value[bin.length] == '/' && P_IsTrashFolder(StrFromTo(path, 0, (i64)bin.length));
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
        // A bin's items have the bin's own menu, as in Explorer, rather than their files'.
        if (count && P__InRecycleBin(directory)) {
            Scratch scratch = GetScratchEx(arena);
            String *paths = ArenaPushArray(scratch.arena, count, String);
            for (u64 i = 0; i < count; ++i) paths[i] = StrFormat(scratch.arena, "%S/%S", directory, names[i]);
            shell->menu = P__RecycleBinMenu(arena, paths, count);
            ReleaseScratch(scratch);
        }
        IShellFolder *folder = shell->menu ? NULL : P__ShellFolder(shell->directory);
        ok = shell->menu || folder != NULL;
        if (shell->menu) {
        } else if (ok && count == 0) {
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
        local_persist const String dropped[] = { SInit("groupby"), SInit("pintohome"), SInit("viewcustomwizard"), SInit("undo"), SInit("redo") };
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
    // A folder inside an archive takes no drop from another program, so its files are kept for the program, which adds them itself.
    b32 keepsFiles;       // `bound` is such a folder
    Arena filesArena;
    ArrayStr files;       // dropped there, until the program takes them
    String filesDirectory;
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

function ArrayStr P_TakeDroppedFiles(Arena *arena, String *directory, b32 *move)
{
    ArrayStr result = {0};
    for (u64 i = 0; i < P__drop.files.header.count; ++i) ArrayPush(arena, result, StrCopy(arena, P__drop.files.v[i]));
    *directory = StrCopy(arena, P__drop.filesDirectory);
    *move = false;
    P__drop.files = (ArrayStr){0};
    P__drop.filesDirectory = (String){0};
    ArenaClear(&P__drop.filesArena);
    return result;
}

// Whether `path` goes on inside a file, as a folder inside an archive does: a folder above it is a file on disk.
function b32 P__InsideFile(String path)
{
    Scratch scratch = GetScratch();
    b32 result = false;
    for (u64 at = 3; at < path.length && !result; ++at) {
        if (path.value[at] != '/') continue;
        wchar *wide = P__FilePathWide(scratch.arena, StrFromTo(path, 0, (i64)at));
        DWORD attributes = wide ? GetFileAttributesW(wide) : INVALID_FILE_ATTRIBUTES;
        result = attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
    }
    if (!result && path.length) {
        wchar *wide = P__FilePathWide(scratch.arena, path);
        DWORD attributes = wide ? GetFileAttributesW(wide) : INVALID_FILE_ATTRIBUTES;
        result = attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
    }
    ReleaseScratch(scratch);
    return result;
}

global const CLSID P__CLSID_ShellLink = { 0x00021401, 0x0000, 0x0000, { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
global const IID P__IID_IShellLinkW = { 0x000214F9, 0x0000, 0x0000, { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
global const IID P__IID_IPersistFile = { 0x0000010B, 0x0000, 0x0000, { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };

function b32 P_RealPath(Arena *arena, String path, String *real)
{
    *real = (String){0};
    Scratch scratch = GetScratchEx(arena);
    String at = path;
    // A shortcut names its target inside it; one to something that is no file, such as a Control Panel item, has no path and stays itself.
    String tail = path.length >= 4 ? StrFrom(path, path.length - 4) : (String){0};
    wchar *wide = P__FilePathWide(scratch.arena, path);
    if (wide && tail.length && tail.value[0] == '.' && (tail.value[1] | 0x20) == 'l' && (tail.value[2] | 0x20) == 'n' && (tail.value[3] | 0x20) == 'k') {
        P__EnsureCom();
        IShellLinkW *link = NULL;
        IPersistFile *file = NULL;
        wchar target[2048] = {0};
        if (SUCCEEDED(CoCreateInstance(&P__CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &P__IID_IShellLinkW, (void **)&link))) {
            if (SUCCEEDED(link->lpVtbl->QueryInterface(link, &P__IID_IPersistFile, (void **)&file)) && SUCCEEDED(file->lpVtbl->Load(file, wide, STGM_READ)) &&
                SUCCEEDED(link->lpVtbl->GetPath(link, target, (i32)CArrayCount(target), NULL, 0)) && target[0]) {
                at = P__FileStringWide(scratch.arena, target, lstrlenW(target));
            }
            if (file) file->lpVtbl->Release(file);
            link->lpVtbl->Release(link);
        }
    }
    // Symbolic links and junctions on the way end where the file system takes them.
    wchar *where = P__FilePathWide(scratch.arena, at);
    HANDLE handle = where ? CreateFileW(where, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL)
                          : INVALID_HANDLE_VALUE;
    b32 result = handle != INVALID_HANDLE_VALUE;
    if (result) {
        u32 length = GetFinalPathNameByHandleW(handle, NULL, 0, FILE_NAME_NORMALIZED);
        wchar *final = length ? ArenaPushArray(scratch.arena, (u64)length + 1, wchar) : NULL;
        if (final && GetFinalPathNameByHandleW(handle, final, length + 1, FILE_NAME_NORMALIZED) <= length) {
            // Without the long-path prefix: "\\?\C:\..." is "C:\...", and "\\?\UNC\server\share" is "\\server\share".
            wchar *start = final;
            if (!wcsncmp(final, L"\\\\?\\UNC\\", 8)) {
                start = final + 6;
                start[0] = L'\\';
            } else if (!wcsncmp(final, L"\\\\?\\", 4)) {
                start = final + 4;
            }
            at = P__FileStringWide(scratch.arena, start, lstrlenW(start));
        }
        CloseHandle(handle);
        *real = P_AbsolutePath(arena, at);
    }
    ReleaseScratch(scratch);
    return result && real->length;
}

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

// Keeps the files of a drop into a folder inside an archive for P_TakeDroppedFiles.
function void P__DropKeepFiles(IDataObject *data)
{
    FORMATETC format = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    STGMEDIUM medium = {0};
    if (FAILED(data->lpVtbl->GetData(data, &format, &medium))) return;
    HDROP drop = (HDROP)medium.hGlobal;
    ArenaClear(&P__drop.filesArena);
    P__drop.files = (ArrayStr){0};
    P__drop.filesDirectory = StrCopy(&P__drop.filesArena, P__drop.bound);
    u32 count = DragQueryFileW(drop, 0xFFFFFFFF, NULL, 0);
    for (u32 i = 0; i < count; ++i) {
        wchar path[1024];
        u32 length = DragQueryFileW(drop, i, path, (u32)CArrayCount(path));
        if (!length || length >= CArrayCount(path)) continue;
        String file = P__FileStringWide(&P__drop.filesArena, path, (i32)length);
        for (u64 at = 0; at < file.length; ++at) {
            if (file.value[at] == '\\') ((char *)file.value)[at] = '/';
        }
        ArrayPush(&P__drop.filesArena, P__drop.files, file);
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
        P__drop.keepsFiles = P__InsideFile(P__drop.bound);
        Scratch scratch = GetScratch();
        IShellFolder *folder = P__drop.bound.length && !P__drop.keepsFiles ? P__ShellFolder(P__FilePathWide(scratch.arena, P__drop.bound)) : NULL;
        IDropTarget *shell = NULL;
        if (folder && SUCCEEDED(folder->lpVtbl->CreateViewObject(folder, P__shellOwner, &P__IID_IDropTarget, (void **)&shell))) {
            DWORD entered = allowed;
            if (SUCCEEDED(shell->lpVtbl->DragEnter(shell, P__drop.data, keys, point, &entered))) P__drop.shell = shell;
            else shell->lpVtbl->Release(shell);
        }
        if (folder) folder->lpVtbl->Release(folder);
        ReleaseScratch(scratch);
    }
    if (P__drop.keepsFiles) {
        *effect = P__DropHasFiles(P__drop.data) ? allowed & DROPEFFECT_COPY : DROPEFFECT_NONE;
    } else if (P__drop.shell) {
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
    } else if (P__drop.keepsFiles && *effect != DROPEFFECT_NONE) {
        P__DropKeepFiles(P__drop.data);
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

// The shell's own data object of an archive's items extracts them where they are dropped.
function b32 P_DragArchiveItems(String archive, String inner, const String *names, u64 count)
{
    Scratch scratch = GetScratch();
    b32 result = P_DragFiles(inner.length ? StrFormat(scratch.arena, "%S/%S", archive, inner) : archive, names, count);
    ReleaseScratch(scratch);
    return result;
}

function b32 P_ShowProperties(String directory, const String *names, u64 count)
{
    if (!count) return false;
    P__EnsureCom();
    Scratch scratch = GetScratch();
    IShellFolder *folder = P__ShellFolder(P__FilePathWide(scratch.arena, directory));
    IDataObject *data = NULL;
    b32 result = folder && P__ShellChildren(folder, names, count, &P__IID_IDataObject, (void **)&data) && SUCCEEDED(SHMultiFileProperties(data, 0));
    if (data) data->lpVtbl->Release(data);
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

function b32 P_TrashHasItems(void)
{
    wchar root[8] = {0};
    u32 length = GetEnvironmentVariableW(L"SystemDrive", root, (u32)CArrayCount(root) - 1);
    if (!length || length >= CArrayCount(root) - 1) lstrcpyW(root, L"C:");
    lstrcatW(root, L"\\");
    SHQUERYRBINFO info = { .cbSize = sizeof(info) };
    return SUCCEEDED(SHQueryRecycleBinW(root, &info)) && info.i64NumItems > 0;
}

function P_FileError P_EmptyTrash(Arena *arena, b32 confirm)
{
    (void)arena;
    P__EnsureCom();
    // The system's confirmation, with how many items go, and its progress window for a big bin; an empty bin or a No is not an error.
    HRESULT status = SHEmptyRecycleBinW(P__shellOwner, NULL, confirm ? 0 : SHERB_NOCONFIRMATION);
    return SUCCEEDED(status) || status == E_UNEXPECTED || status == HRESULT_FROM_WIN32(ERROR_CANCELLED) ? (P_FileError){0} : (P_FileError){ .code = (u32)status, .message = S("The Recycle Bin could not be emptied.") };
}

function b32 P_EmptyTrashAsks(void) { return true; }

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

function P_FileError P_RestoreTrash(Arena *arena, String bin, const String *paths, u64 count)
{
    // The shell's Restore for the system's bins, which asks about a taken name itself and lets every window and the bin's icon know.
    if (P_IsTrashFolder(bin)) {
        P__EnsureCom();
        IContextMenu *menu = P__RecycleBinMenu(arena, paths, count);
        if (menu) {
            // The shell moves them on after it returns, so the job waits until they have left the bin, for what is shown of it to be right.
            Scratch waits = GetScratchEx(arena);
            const String *going = paths;
            u64 goingCount = count;
            if (!count) {
                P_ArrayTrashed binned = P_ListTrash(waits.arena, bin, NULL);
                String *all = ArenaPushArray(waits.arena, Max(binned.header.count, (u64)1), String);
                for (u64 i = 0; i < binned.header.count; ++i) all[i] = binned.v[i].file.name;
                going = all;
                goingCount = binned.header.count;
            }
            HMENU popup = CreatePopupMenu();
            HRESULT result = popup ? menu->lpVtbl->QueryContextMenu(menu, popup, 0, P__MenuFirstId, P__MenuLastId, CMF_NORMAL) : E_FAIL;
            if (SUCCEEDED(result)) {
                CMINVOKECOMMANDINFOEX info = { .cbSize = sizeof(info), .fMask = CMIC_MASK_UNICODE, .hwnd = P__shellOwner, .lpVerb = "undelete", .lpVerbW = L"undelete", .nShow = SW_SHOWNORMAL };
                result = menu->lpVtbl->InvokeCommand(menu, (CMINVOKECOMMANDINFO *)&info);
            }
            if (popup) DestroyMenu(popup);
            menu->lpVtbl->Release(menu);
            if (SUCCEEDED(result)) P_WaitGone(going, goingCount, 10000);
            ReleaseScratch(waits);
            if (SUCCEEDED(result) || result == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return (P_FileError){0};
            return P__FileError(arena, HRESULT_FACILITY(result) == FACILITY_WIN32 ? (u32)HRESULT_CODE(result) : (u32)result);
        }
    }
    Scratch scratch = GetScratchEx(arena);
    P_ArrayTrashed binned = P_ListTrash(scratch.arena, bin, NULL);
    u32 error = 0, taken = 0;
    for (u64 i = 0; i < binned.header.count && !error; ++i) {
        const P_Trashed *item = &binned.v[i];
        b32 wanted = !count;
        for (u64 p = 0; p < count && !wanted; ++p) wanted = StrIsEqual(item->file.name, paths[p]);
        if (!wanted) continue;
        String folder = item->original, target = StrFormat(scratch.arena, "%S/%S", folder, item->name);
        wchar *from = P__FileExtendedPath(scratch.arena, item->file.name), *to = P__FileExtendedPath(scratch.arena, target);
        wchar *made = P__FilePathWide(scratch.arena, folder.length == 2 ? StrFormat(scratch.arena, "%S/", folder) : folder);
        if (!from || !to || !made) {
            error = ERROR_INVALID_NAME;
            break;
        }
        if (GetFileAttributesW(to) != INVALID_FILE_ATTRIBUTES) {
            ++taken;
            continue;
        }
        i32 status = SHCreateDirectoryExW(NULL, made, NULL);
        if (status != ERROR_SUCCESS && status != ERROR_ALREADY_EXISTS && status != ERROR_FILE_EXISTS) {
            error = (u32)status;
            break;
        }
        if (!MoveFileExW(from, to, MOVEFILE_COPY_ALLOWED)) {
            error = GetLastError();
            break;
        }
        // Explorer tells of a restore as the item's rename out of the bin, which open windows and the bin's icon follow.
        wchar *fromPlain = P__FilePathWide(scratch.arena, item->file.name), *toPlain = P__FilePathWide(scratch.arena, target);
        if (fromPlain && toPlain) SHChangeNotify(item->file.kind == P_FileKind_Directory ? SHCNE_RENAMEFOLDER : SHCNE_RENAMEITEM, SHCNF_PATHW, fromPlain, toPlain);
        // The record of where it came from, $I beside the item's $R, goes too.
        Index slash = StrIndexChar(item->file.name, '/', SSF_SearchBackwards);
        String record = StrFormat(scratch.arena, "%S/$I%S", StrFromTo(item->file.name, 0, (i64)slash.value), StrFrom(item->file.name, slash.value + 3));
        wchar *recordWide = P__FileExtendedPath(scratch.arena, record);
        if (recordWide) DeleteFileW(recordWide);
    }
    // The bin's own item, which the desktop and Filey draw full or empty.
    PIDLIST_ABSOLUTE recycleBin = NULL;
    if (SUCCEEDED(SHGetSpecialFolderLocation(NULL, CSIDL_BITBUCKET, &recycleBin))) {
        SHChangeNotify(SHCNE_UPDATEDIR, SHCNF_IDLIST | SHCNF_FLUSH, (const wchar *)recycleBin, NULL);
        SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_IDLIST | SHCNF_FLUSH, (const wchar *)recycleBin, NULL);
        ILFree(recycleBin);
    }
    ReleaseScratch(scratch);
    if (error) return P__FileError(arena, error);
    if (taken) return (P_FileError){ .code = ERROR_ALREADY_EXISTS, .message = StrFormat(arena, "%u item%s stayed in the Recycle Bin, as something else is where %s came from.", taken, taken == 1 ? "" : "s", taken == 1 ? "it" : "they") };
    return (P_FileError){0};
}

function P_ArrayPlace P_UserPlaces(Arena *arena)
{
    local_persist const struct { const GUID *id; const char *name; } folders[] = {
        { &P__FolderProfile, NULL }, { &P__FolderDesktop, "Desktop" }, { &P__FolderDocuments, "Documents" }, { &P__FolderDownloads, "Downloads" },
        { &P__FolderMusic, "Music" }, { &P__FolderPictures, "Pictures" }, { &P__FolderVideos, "Videos" },
    };
    P_ArrayPlace result = {0};
    for (u32 i = 0; i < CArrayCount(folders); ++i) {
        String path = P__KnownFolder(arena, folders[i].id);
        if (!path.length) continue;
        String name = folders[i].name ? StrFromCStr(folders[i].name) : StrFrom(path, StrIndexChar(path, '/', SSF_SearchBackwards).value + 1);
        ArrayPush(arena, result, ((P_Place){ .name = name, .path = path, .home = !folders[i].name }));
    }
    ArrayPush(arena, result, ((P_Place){ .name = S("Recycle Bin"), .path = P_TrashFolder(arena) }));
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
    atomic_int named;  // the name and type are in, which may come before the rest
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
    atomic_store(&answer->named, 1);
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
        // A fixed disk without a file system to read, such as one being formatted, is left out; removable ones without a medium stay.
        if (answered && types[i] == DRIVE_FIXED && !answer->formatted) {
            P__DriveAnswerRelease(answer);
            continue;
        }
        P_Place place = { .path = P_AbsolutePath(arena, P__FileStringWide(scratch.arena, answer->drive, lstrlenW(answer->drive))), .busy = (b8)!answered };
        // A busy drive's name may be in already; without it, the kind of drive names it, as "CD Drive (E:)".
        b32 named = answered || atomic_load(&answer->named);
        if (named && answer->name[0]) {
            place.name = P__FileStringWide(arena, answer->name, lstrlenW(answer->name));
        } else {
            local_persist const char *kinds[] = { [DRIVE_REMOVABLE] = "USB Drive", [DRIVE_FIXED] = "Local Disk", [DRIVE_REMOTE] = "Network Drive", [DRIVE_CDROM] = "CD Drive" };
            const char *kind = types[i] < CArrayCount(kinds) ? kinds[types[i]] : NULL;
            place.name = kind ? StrFormat(arena, "%s (%c:)", kind, (char)answer->drive[0]) : place.path;
        }
        if (named && answer->type[0]) place.type = P__FileStringWide(arena, answer->type, lstrlenW(answer->type));
        if (answered) {
            if (answer->sized) {
                place.totalBytes = answer->total.QuadPart;
                place.freeBytes = answer->available.QuadPart;
            }
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

// The install: Filey.exe in "Filey" under the roaming application data, and its shortcuts in the Start menu and on the desktop.
global const GUID P__FolderPrograms = { 0xA77F5D77, 0x2E2B, 0x44C3, { 0xA6, 0xA2, 0xAB, 0xA6, 0x01, 0x05, 0x4A, 0x51 } };

typedef struct P__InstallPaths {
    wchar *folder, *executable, *aside, *running;
    wchar *startLink, *desktopLink;  // NULL where the system has no such folder
} P__InstallPaths;

function b32 P_CanInstall(void) { return true; }

function P__InstallPaths P__InstallPathsGet(Arena *arena)
{
    P__InstallPaths result = {0};
    String roaming = P__KnownFolder(arena, &P__FolderRoamingData), programs = P__KnownFolder(arena, &P__FolderPrograms), desktop = P__KnownFolder(arena, &P__FolderDesktop);
    wchar running[1024];
    u32 length = GetModuleFileNameW(NULL, running, (u32)CArrayCount(running));
    if (!roaming.length || !length || length >= CArrayCount(running)) return result;
    result.folder = P__FilePathWide(arena, StrFormat(arena, "%S/Filey", roaming));
    result.executable = P__FilePathWide(arena, StrFormat(arena, "%S/Filey/Filey.exe", roaming));
    // A copy that was running when another replaced it is moved aside here, and goes once nothing runs it.
    result.aside = P__FilePathWide(arena, StrFormat(arena, "%S/Filey/Filey.old.exe", roaming));
    result.running = ArenaPushArray(arena, (u64)length + 1, wchar);
    MemCopy(result.running, running, (u64)length * sizeof(wchar));
    if (programs.length) result.startLink = P__FilePathWide(arena, StrFormat(arena, "%S/Filey.lnk", programs));
    if (desktop.length) result.desktopLink = P__FilePathWide(arena, StrFormat(arena, "%S/Filey.lnk", desktop));
    return result;
}

// Whether two full paths name one file, as Windows compares names: without regard to case.
function b32 P__SamePathWide(const wchar *a, const wchar *b)
{
    return a && b && CompareStringOrdinal(a, -1, b, -1, TRUE) == CSTR_EQUAL;
}

// Whether the shortcut `link` starts `target`, so that one the user made for something else is left alone.
function b32 P__ShortcutStarts(Arena *arena, const wchar *link, const wchar *target)
{
    if (!link || GetFileAttributesW(link) == INVALID_FILE_ATTRIBUTES) return false;
    Scratch scratch = GetScratchEx(arena);
    String real = {0};
    wchar *started = P_RealPath(scratch.arena, P__FileStringWide(scratch.arena, link, lstrlenW(link)), &real) ? P__FilePathWide(scratch.arena, real) : NULL;
    b32 result = P__SamePathWide(started, target);
    ReleaseScratch(scratch);
    return result;
}

function b32 P__WriteShortcut(const wchar *link, const wchar *target, const wchar *folder)
{
    P__EnsureCom();
    IShellLinkW *shell = NULL;
    IPersistFile *file = NULL;
    b32 result = false;
    if (SUCCEEDED(CoCreateInstance(&P__CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &P__IID_IShellLinkW, (void **)&shell))) {
        shell->lpVtbl->SetPath(shell, target);
        shell->lpVtbl->SetWorkingDirectory(shell, folder);
        shell->lpVtbl->SetDescription(shell, L"Filey");
        shell->lpVtbl->SetIconLocation(shell, target, 0);
        if (SUCCEEDED(shell->lpVtbl->QueryInterface(shell, &P__IID_IPersistFile, (void **)&file))) {
            result = SUCCEEDED(file->lpVtbl->Save(file, link, TRUE));
            file->lpVtbl->Release(file);
        }
        shell->lpVtbl->Release(shell);
    }
    return result;
}

// The user's PATH from the registry, with its type, or NULL without one.
function wchar *P__UserPath(Arena *arena, HKEY key, DWORD *type)
{
    DWORD size = 0;
    *type = REG_EXPAND_SZ;
    if (RegQueryValueExW(key, L"Path", NULL, type, NULL, &size) != ERROR_SUCCESS || (*type != REG_SZ && *type != REG_EXPAND_SZ)) {
        *type = REG_EXPAND_SZ;
        return NULL;
    }
    wchar *result = ArenaPushArray(arena, size / sizeof(wchar) + 2, wchar);
    if (RegQueryValueExW(key, L"Path", NULL, type, (u8 *)result, &size) != ERROR_SUCCESS) return NULL;
    result[size / sizeof(wchar)] = 0;
    return result;
}

// Whether the PATH entry `entry`, `length` characters long, is `folder`, with or without a backslash at its end.
function b32 P__PathEntryIs(const wchar *entry, i32 length, const wchar *folder)
{
    while (length > 0 && (entry[length - 1] == L'\\' || entry[length - 1] == L' ')) --length;
    i32 folderLength = lstrlenW(folder);
    return length == folderLength && CompareStringOrdinal(entry, length, folder, folderLength, TRUE) == CSTR_EQUAL;
}

function b32 P__PathHas(const wchar *path, const wchar *folder)
{
    for (const wchar *at = path; at && *at;) {
        const wchar *end = at;
        while (*end && *end != L';') ++end;
        if (P__PathEntryIs(at, (i32)(end - at), folder)) return true;
        at = *end ? end + 1 : end;
    }
    return false;
}

// The PATH `path` with `folder` at its end or taken out, every other entry as it was.
function wchar *P__EditPath(Arena *arena, const wchar *path, const wchar *folder, b32 on)
{
    u64 length = path ? (u64)lstrlenW(path) : 0, folderLength = (u64)lstrlenW(folder), out = 0;
    wchar *result = ArenaPushArray(arena, length + folderLength + 2, wchar);
    for (const wchar *at = path; at && *at;) {
        const wchar *end = at;
        while (*end && *end != L';') ++end;
        if (!P__PathEntryIs(at, (i32)(end - at), folder)) {
            if (out) result[out++] = L';';
            MemCopy(result + out, at, (u64)(end - at) * sizeof(wchar));
            out += (u64)(end - at);
        }
        at = *end ? end + 1 : end;
    }
    if (on) {
        if (out) result[out++] = L';';
        MemCopy(result + out, folder, folderLength * sizeof(wchar));
        out += folderLength;
    }
    result[out] = 0;
    return result;
}

// Puts `folder` on the user's PATH or takes it off, and tells running programs.
function u32 P__SetUserPath(Arena *arena, const wchar *folder, b32 on)
{
    HKEY key = NULL;
    u32 error = (u32)RegCreateKeyExW(HKEY_CURRENT_USER, L"Environment", 0, NULL, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, NULL, &key, NULL);
    if (error != ERROR_SUCCESS) return error;
    Scratch scratch = GetScratchEx(arena);
    DWORD type;
    wchar *path = P__UserPath(scratch.arena, key, &type);
    if (on != P__PathHas(path, folder)) {
        wchar *changed = P__EditPath(scratch.arena, path, folder, on);
        error = (u32)RegSetValueExW(key, L"Path", 0, type, (const u8 *)changed, (DWORD)(((u64)lstrlenW(changed) + 1) * sizeof(wchar)));
        if (error == ERROR_SUCCESS) SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"Environment", SMTO_ABORTIFHUNG, 1000, NULL);
    }
    ReleaseScratch(scratch);
    RegCloseKey(key);
    return error;
}

// The product version in the executable `path`'s version resource.
function String P__FileVersion(Arena *arena, const wchar *path)
{
    DWORD unused = 0, size = GetFileVersionInfoSizeW(path, &unused);
    if (!size) return (String){0};
    Scratch scratch = GetScratchEx(arena);
    void *info = ArenaPushArray(scratch.arena, size, u8);
    wchar *value = NULL;
    UINT length = 0;
    String result = {0};
    if (GetFileVersionInfoW(path, 0, size, info) && VerQueryValueW(info, L"\\StringFileInfo\\040904B0\\ProductVersion", (void **)&value, &length) && length) {
        result = P__FileStringWide(arena, value, lstrlenW(value));
    }
    ReleaseScratch(scratch);
    return result;
}

function P_Install P_InstallState(Arena *arena)
{
    Scratch scratch = GetScratchEx(arena);
    P__InstallPaths paths = P__InstallPathsGet(scratch.arena);
    P_Install result = {0};
    if (paths.folder) {
        DeleteFileW(paths.aside);
        result.folder = P_AbsolutePath(arena, P__FileStringWide(scratch.arena, paths.folder, lstrlenW(paths.folder)));
        result.installed = GetFileAttributesW(paths.executable) != INVALID_FILE_ATTRIBUTES;
        result.running = P__SamePathWide(paths.running, paths.executable);
        if (result.installed) {
            result.version = P__FileVersion(arena, paths.executable);
            String mine = {0}, theirs = {0};
            result.same = result.running || (P_ReadFile(scratch.arena, P__FileStringWide(scratch.arena, paths.running, lstrlenW(paths.running)), &mine) &&
                                             P_ReadFile(scratch.arena, P__FileStringWide(scratch.arena, paths.executable, lstrlenW(paths.executable)), &theirs) &&
                                             StrIsEqual(mine, theirs));
            result.desktopShortcut = P__ShortcutStarts(scratch.arena, paths.desktopLink, paths.executable);
            HKEY key = NULL;
            if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Environment", 0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
                DWORD type;
                result.onPath = P__PathHas(P__UserPath(scratch.arena, key, &type), paths.folder);
                RegCloseKey(key);
            }
        }
    }
    ReleaseScratch(scratch);
    return result;
}

function P_FileError P_InstallApply(Arena *arena, b32 desktopShortcut, b32 onPath)
{
    Scratch scratch = GetScratchEx(arena);
    P__InstallPaths paths = P__InstallPathsGet(scratch.arena);
    u32 error = paths.folder ? ERROR_SUCCESS : ERROR_PATH_NOT_FOUND;
    if (!error && !CreateDirectoryW(paths.folder, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) error = GetLastError();
    // A copy that another Filey runs cannot be written over, but it can be moved aside.
    if (!error && !P__SamePathWide(paths.running, paths.executable)) {
        DeleteFileW(paths.aside);
        if (GetFileAttributesW(paths.executable) != INVALID_FILE_ATTRIBUTES && !DeleteFileW(paths.executable)) MoveFileExW(paths.executable, paths.aside, MOVEFILE_REPLACE_EXISTING);
        if (!CopyFileW(paths.running, paths.executable, FALSE)) error = GetLastError();
    }
    if (!error && paths.startLink && !P__WriteShortcut(paths.startLink, paths.executable, paths.folder)) error = ERROR_CANNOT_MAKE;
    if (!error && paths.desktopLink && desktopShortcut && !P__WriteShortcut(paths.desktopLink, paths.executable, paths.folder)) error = ERROR_CANNOT_MAKE;
    if (!error && paths.desktopLink && !desktopShortcut && P__ShortcutStarts(scratch.arena, paths.desktopLink, paths.executable)) DeleteFileW(paths.desktopLink);
    if (!error) error = P__SetUserPath(scratch.arena, paths.folder, onPath);
    ReleaseScratch(scratch);
    return error ? P__FileError(arena, error) : (P_FileError){0};
}

function P_FileError P_Uninstall(Arena *arena)
{
    Scratch scratch = GetScratchEx(arena);
    P__InstallPaths paths = P__InstallPathsGet(scratch.arena);
    u32 error = paths.folder ? ERROR_SUCCESS : ERROR_PATH_NOT_FOUND;
    if (!error) {
        if (P__ShortcutStarts(scratch.arena, paths.startLink, paths.executable)) DeleteFileW(paths.startLink);
        if (P__ShortcutStarts(scratch.arena, paths.desktopLink, paths.executable)) DeleteFileW(paths.desktopLink);
        error = P__SetUserPath(scratch.arena, paths.folder, false);
        DeleteFileW(paths.aside);
        // The running copy cannot be deleted, but it can move to the temporary folder, which is on the same drive under the profile, and its
        // folder then goes with it.
        // ponytail: a temporary folder on another drive leaves the copy where it is, until it is uninstalled from another Filey.
        if (P__SamePathWide(paths.running, paths.executable)) {
            wchar temp[MAX_PATH + 1], moved[MAX_PATH + 40];
            u32 length = GetTempPathW((u32)CArrayCount(temp), temp);
            if (length && length < CArrayCount(temp)) {
                swprintf(moved, CArrayCount(moved), L"%lsFiley-%lu.exe", temp, GetCurrentProcessId());
                MoveFileExW(paths.executable, moved, MOVEFILE_REPLACE_EXISTING);
            }
        } else {
            DeleteFileW(paths.executable);
        }
        if (!error && GetFileAttributesW(paths.executable) != INVALID_FILE_ATTRIBUTES) error = ERROR_SHARING_VIOLATION;
        // Only an empty folder goes, so nothing put there by hand is lost.
        RemoveDirectoryW(paths.folder);
    }
    ReleaseScratch(scratch);
    return error ? P__FileError(arena, error) : (P_FileError){0};
}

function b32 P_LoadAppIcon(Arena *arena, i32 size, P_Image *image)
{
    // The executable's own icon resource, which Windows scales from its nearest size.
    HICON icon = (HICON)LoadImageW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(1), IMAGE_ICON, size, size, LR_DEFAULTCOLOR);
    b32 result = icon && P__IconPixels(arena, icon, image);
    if (icon) DestroyIcon(icon);
    return result;
}
