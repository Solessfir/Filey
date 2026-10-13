// Shell file icons through the system image lists. Requires WinFiles.c.

#pragma push_macro("function")
#undef function
#include <commctrl.h>
#include <commoncontrols.h>
#pragma pop_macro("function")

// Extensions whose files carry their own icon, so they cannot share one per type.
global const String P__IconPerPathExtensions[] = {
    S("exe"), S("lnk"), S("ico"), S("cur"), S("ani"), S("url"), S("scr"), S("msc"), S("cpl"), S("appref-ms"),
};

function String P_FileIconKey(Arena *arena, String path, P_FileKind kind)
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
    String extension = StrCopy(arena, StrFrom(name, dot.value + 1));
    for (u64 i = 0; i < extension.length; ++i) {
        char c = extension.value[i];
        if (c >= 'A' && c <= 'Z') ((char *)extension.value)[i] = (char)(c + ('a' - 'A'));
    }
    for (u64 i = 0; i < CArrayCount(P__IconPerPathExtensions); ++i) {
        if (StrIsEqual(extension, P__IconPerPathExtensions[i])) return StrCopy(arena, path);
    }
    return StrFormat(arena, ".%S", extension);
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
        HRESULT com = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        DWORD attributes = kind == P_FileKind_Directory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
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
        if (SUCCEEDED(com)) CoUninitialize();
    }
    ReleaseScratch(scratch);
    return result;
}
