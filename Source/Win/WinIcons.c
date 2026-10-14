// Shell file icons through the system image lists, and shell type names. Requires WinFiles.c.

#pragma push_macro("function")
#undef function
#include <commctrl.h>
#include <commoncontrols.h>
#include <shobjidl.h>
#pragma pop_macro("function")

// Extensions whose files carry their own icon, so they cannot share one per type.
global const String P__IconPerPathExtensions[] = {
    S("exe"), S("lnk"), S("ico"), S("cur"), S("ani"), S("url"), S("scr"), S("msc"), S("cpl"), S("appref-ms"),
};

function String P_FileTypeKey(Arena *arena, String path, P_FileKind kind)
{
    if (kind == P_FileKind_Directory) {
        return S("/folder");
    }
    Index slash = StrIndexChar(path, '/', SSF_SearchBackwards | SSF_IndexPlusOne | SSF_CountOnFail);
    String name = StrFrom(path, slash.exists ? slash.value : 0);
    // The shell treats a leading dot as an extension too (".gitignore").
    Index dot = StrIndexChar(name, '.', SSF_SearchBackwards);
    if (!dot.exists || dot.value + 1 == name.length) {
        return S("/file");
    }
    String result = StrFormat(arena, ".%S", StrFrom(name, dot.value + 1));
    for (u64 i = 0; i < result.length; ++i) {
        char c = result.value[i];
        if (c >= 'A' && c <= 'Z') ((char *)result.value)[i] = (char)(c + ('a' - 'A'));
    }
    return result;
}

function String P_FileIconKey(Arena *arena, String path, P_FileKind kind)
{
    // Each drive has its own icon (system, removable, optical, network).
    if (kind == P_FileKind_Drive) return StrCopy(arena, path);
    String result = P_FileTypeKey(arena, path, kind);
    for (u64 i = 0; result.value[0] == '.' && i < CArrayCount(P__IconPerPathExtensions); ++i) {
        if (StrIsEqual(StrFrom(result, 1), P__IconPerPathExtensions[i])) return StrCopy(arena, path);
    }
    return result;
}

// 32-bit shell bitmaps as straight RGBA. Some are premultiplied (menu pictures) and some are not; a channel brighter than its alpha shows which. Bitmaps without alpha are opaque.
function b32 P__BitmapPixels(Arena *arena, HBITMAP bitmap, P_Image *image)
{
    BITMAP info = {0};
    if (GetObjectW(bitmap, sizeof(info), &info) != sizeof(info) || info.bmWidth <= 0 || info.bmHeight <= 0) return false;
    i32 width = info.bmWidth, height = info.bmHeight;
    BITMAPINFO format = { .bmiHeader = {
        .biSize = sizeof(BITMAPINFOHEADER), .biWidth = width, .biHeight = -height, .biPlanes = 1, .biBitCount = 32, .biCompression = BI_RGB,
    } };
    u32 *pixels = ArenaPushArray(arena, (u64)width * (u64)height, u32);
    HDC dc = GetDC(NULL);
    b32 result = GetDIBits(dc, bitmap, 0, (UINT)height, pixels, &format, DIB_RGB_COLORS) == height;
    ReleaseDC(NULL, dc);
    if (!result) return false;
    i64 count = (i64)width * height;
    b32 hasAlpha = false, straight = false;
    for (i64 i = 0; i < count; ++i) {
        u32 p = pixels[i], a = p >> 24;
        hasAlpha |= a != 0;
        straight |= ((p >> 16) & 0xFF) > a || ((p >> 8) & 0xFF) > a || (p & 0xFF) > a;
    }
    for (i64 i = 0; i < count; ++i) {
        u32 p = pixels[i];
        u32 a = hasAlpha ? p >> 24 : 255, r = (p >> 16) & 0xFF, g = (p >> 8) & 0xFF, b = p & 0xFF;
        if (hasAlpha && !straight && a && a < 255) {
            r = Min(255u, r * 255 / a);
            g = Min(255u, g * 255 / a);
            b = Min(255u, b * 255 / a);
        }
        pixels[i] = (a << 24) | (b << 16) | (g << 8) | r;
    }
    *image = (P_Image){ pixels, width, height };
    return true;
}

// Shell calls need COM.
// Creating and tearing down an apartment per call costs about half a millisecond, so each thread enters one on first use and keeps it.
global thread_local b32 P__comReady;

function void P__EnsureCom(void)
{
    if (!P__comReady) {
        HRESULT com = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        P__comReady = SUCCEEDED(com) || com == RPC_E_CHANGED_MODE;
        // Until the system image list is set up, the shell fails icon lookups of real paths such as drive roots for the first few calls.
        // FileIconInit sets it up; shell32 exports it by ordinal only.
        typedef BOOL WINAPI P__FileIconInit(BOOL restoreCache);
        P__FileIconInit *fileIconInit = (P__FileIconInit *)(void *)GetProcAddress(GetModuleHandleW(L"shell32.dll"), MAKEINTRESOURCEA(660));
        if (fileIconInit) fileIconInit(TRUE);
    }
}

// {46EB5926-582E-4017-9FDF-E8998DAA0950}
global const IID P__IID_IImageList = { 0x46EB5926, 0x582E, 0x4017, { 0x9F, 0xDF, 0xE8, 0x99, 0x8D, 0xAA, 0x09, 0x50 } };

// Icon bitmaps are straight-alpha BGRA. Old icons without alpha use their mask instead.
function b32 P__IconPixels(Arena *arena, HICON icon, P_Image *image)
{
    ICONINFO info = {0};
    if (!GetIconInfo(icon, &info)) return false;
    b32 result = false;
    BITMAP bitmap = {0};
    if (info.hbmColor && GetObjectW(info.hbmColor, sizeof(bitmap), &bitmap) == sizeof(bitmap) && bitmap.bmWidth > 0 && bitmap.bmHeight > 0) {
        i32 width = bitmap.bmWidth, height = bitmap.bmHeight;
        BITMAPINFO format = { .bmiHeader = {
            .biSize = sizeof(BITMAPINFOHEADER), .biWidth = width, .biHeight = -height, .biPlanes = 1, .biBitCount = 32, .biCompression = BI_RGB,
        } };
        u32 *pixels = ArenaPushArray(arena, (u64)width * (u64)height, u32);
        HDC dc = GetDC(NULL);
        if (GetDIBits(dc, info.hbmColor, 0, (UINT)height, pixels, &format, DIB_RGB_COLORS) == height) {
            b32 hasAlpha = false;
            for (i64 i = 0; i < (i64)width * height && !hasAlpha; ++i) hasAlpha = (pixels[i] >> 24) != 0;
            if (!hasAlpha && info.hbmMask) {
                Scratch scratch = GetScratchEx(arena);
                u32 *mask = ArenaPushArray(scratch.arena, (u64)width * (u64)height, u32);
                if (GetDIBits(dc, info.hbmMask, 0, (UINT)height, mask, &format, DIB_RGB_COLORS) == height) {
                    for (i64 i = 0; i < (i64)width * height; ++i) {
                        if ((mask[i] & 0xFFFFFF) == 0) pixels[i] |= 0xFF000000u;
                    }
                }
                ReleaseScratch(scratch);
            }
            for (i64 i = 0; i < (i64)width * height; ++i) {
                u32 p = pixels[i];
                pixels[i] = (p & 0xFF00FF00u) | ((p >> 16) & 0xFFu) | ((p & 0xFFu) << 16);
            }
            *image = (P_Image){ pixels, width, height };
            result = true;
        }
        ReleaseDC(NULL, dc);
    }
    if (info.hbmColor) DeleteObject(info.hbmColor);
    if (info.hbmMask) DeleteObject(info.hbmMask);
    return result;
}

function b32 P_LoadFileIcon(Arena *arena, String path, P_FileKind kind, i32 size, P_Image *image)
{
    Scratch scratch = GetScratchEx(arena);
    // Type-keyed icons come from the name and attributes alone, so the shell does not open the file.
    // Shared generic keys use a neutral name, since the first file to request one decides it for all.
    String key = P_FileIconKey(scratch.arena, path, kind);
    b32 byType = !StrIsEqual(key, path);
    b32 generic = StrIsEqual(key, S("/file")) || StrIsEqual(key, S("/folder"));
    wchar *wide = P__FilePathWide(scratch.arena, generic ? S("item") : path);
    b32 result = false;
    if (wide) {
        P__EnsureCom();
        DWORD attributes = kind == P_FileKind_Directory || kind == P_FileKind_Drive ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
        SHFILEINFOW info = {0};
        if (SHGetFileInfoW(wide, attributes, &info, sizeof(info), SHGFI_SYSICONINDEX | (byType ? SHGFI_USEFILEATTRIBUTES : 0))) {
            // The lists are sized by system DPI: take the smallest one that covers the request.
            local_persist const i32 lists[] = { SHIL_SMALL, SHIL_LARGE, SHIL_EXTRALARGE, SHIL_JUMBO };
            IImageList *chosen = NULL;
            for (u32 i = 0; i < CArrayCount(lists); ++i) {
                IImageList *list = NULL;
                if (FAILED(SHGetImageList(lists[i], &P__IID_IImageList, (void **)&list))) continue;
                int width = 0, height = 0;
                list->lpVtbl->GetIconSize(list, &width, &height);
                if (chosen) chosen->lpVtbl->Release(chosen);
                chosen = list;
                if (width >= size) break;
            }
            HICON icon = NULL;
            if (chosen && SUCCEEDED(chosen->lpVtbl->GetIcon(chosen, info.iIcon, ILD_TRANSPARENT, &icon)) && icon) {
                result = P__IconPixels(arena, icon, image);
                DestroyIcon(icon);
            }
            if (chosen) chosen->lpVtbl->Release(chosen);
        }
    }
    ReleaseScratch(scratch);
    return result;
}

// {BCC18B79-BA16-442F-80C4-8A59C30C463B}
global const IID P__IID_IShellItemImageFactory = { 0xBCC18B79, 0xBA16, 0x442F, { 0x80, 0xC4, 0x8A, 0x59, 0xC3, 0x0C, 0x46, 0x3B } };

function String P_FileThumbnailKey(Arena *arena, String path, P_FileKind kind)
{
    (void)kind;
    return StrCopy(arena, path);
}

// The shell's image factory gives a thumbnail where the file has one and its icon otherwise, at any size.
function b32 P_LoadFileThumbnail(Arena *arena, String path, P_FileKind kind, i32 size, P_Image *image)
{
    Scratch scratch = GetScratchEx(arena);
    wchar *wide = P__FilePathWide(scratch.arena, path);
    b32 result = false;
    if (wide) {
        P__EnsureCom();
        IShellItemImageFactory *factory = NULL;
        if (SUCCEEDED(SHCreateItemFromParsingName(wide, NULL, &P__IID_IShellItemImageFactory, (void **)&factory))) {
            HBITMAP bitmap = NULL;
            if (SUCCEEDED(factory->lpVtbl->GetImage(factory, (SIZE){ size, size }, SIIGBF_RESIZETOFIT, &bitmap)) && bitmap) {
                result = P__BitmapPixels(arena, bitmap, image);
                DeleteObject(bitmap);
            }
            factory->lpVtbl->Release(factory);
        }
    }
    ReleaseScratch(scratch);
    return result || P_LoadFileIcon(arena, path, kind, size, image);
}

function String P_FileTypeName(Arena *arena, String name, P_FileKind kind)
{
    Scratch scratch = GetScratchEx(arena);
    wchar *wide = P__FilePathWide(scratch.arena, name);
    String result = {0};
    if (wide) {
        P__EnsureCom();
        SHFILEINFOW info = {0};
        DWORD attributes = kind == P_FileKind_Directory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
        if (SHGetFileInfoW(wide, attributes, &info, sizeof(info), SHGFI_TYPENAME | SHGFI_USEFILEATTRIBUTES) && info.szTypeName[0]) {
            result = P__FileStringWide(arena, info.szTypeName, (i32)wcslen(info.szTypeName));
        }
    }
    ReleaseScratch(scratch);
    return result.length ? result : kind == P_FileKind_Directory ? S("File folder") : S("File");
}
