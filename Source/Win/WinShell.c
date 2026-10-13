// Shell context menus: the IContextMenu of a selection or folder background is
// read into P_MenuItems for Filey's own menu UI and invoked by command id.
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

function b32 P_ContextMenuOpen(Arena *arena, String directory, const String *names, u64 count, P_ContextMenu *menu)
{
    *menu = (P_ContextMenu){0};
    P__EnsureCom();
    Scratch scratch = GetScratchEx(arena);
    P__ShellMenu *shell = ArenaPushStruct(arena, P__ShellMenu);
    shell->directory = P__FilePathWide(arena, directory);
    PIDLIST_ABSOLUTE directoryList = NULL;
    IShellFolder *folder = NULL;
    PITEMID_CHILD *children = ArenaPushArray(scratch.arena, Max(count, (u64)1), PITEMID_CHILD);
    u64 parsed = 0;
    b32 ok = shell->directory && SUCCEEDED(SHParseDisplayName(shell->directory, NULL, &directoryList, 0, NULL)) &&
             SUCCEEDED(SHBindToObject(NULL, directoryList, NULL, &P__IID_IShellFolder, (void **)&folder));
    if (ok && count == 0) {
        ok = SUCCEEDED(folder->lpVtbl->CreateViewObject(folder, P__shellOwner, &P__IID_IContextMenu, (void **)&shell->menu));
    } else if (ok) {
        for (; parsed < count && ok; ++parsed) {
            wchar *name = P__FilePathWide(scratch.arena, names[parsed]);
            ok = name && SUCCEEDED(folder->lpVtbl->ParseDisplayName(folder, P__shellOwner, NULL, name, NULL, (PIDLIST_RELATIVE *)&children[parsed], NULL));
        }
        ok = ok && SUCCEEDED(folder->lpVtbl->GetUIObjectOf(folder, P__shellOwner, (UINT)count, (PCUITEMID_CHILD_ARRAY)children, &P__IID_IContextMenu, NULL, (void **)&shell->menu));
    }
    for (u64 i = 0; i < parsed; ++i) ILFree(children[i]);
    if (folder) folder->lpVtbl->Release(folder);
    if (directoryList) ILFree(directoryList);

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
    ReleaseScratch(scratch);
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
