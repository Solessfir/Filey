// Shell file icons through the system image lists, and shell type names. Requires WinFiles.c.

#pragma push_macro("function")
#undef function
#include <commctrl.h>
#include <commoncontrols.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <wincodec.h>
#include <shlwapi.h>
#pragma pop_macro("function")

// Set by WinWindow.c when the shell tells of changed file associations or replaced icons, after which the icons loaded before are out of date.
global atomic_bool P__iconsChanged;

function b32 P_IconThemeChanged(void) { return atomic_exchange(&P__iconsChanged, false); }

// Extensions whose files carry their own icon, so they cannot share one per type.
global const String P__IconPerPathExtensions[] = {
    SInit("exe"), SInit("lnk"), SInit("ico"), SInit("cur"), SInit("ani"), SInit("url"), SInit("scr"), SInit("msc"), SInit("cpl"), SInit("appref-ms"),
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

function b32 P_LoadFileIcon(Arena *arena, String path, P_FileKind kind, i32 size, f32 scale, b32 exact, P_Image *image)
{
    (void)scale;
    Scratch scratch = GetScratchEx(arena);
    // Type-keyed icons come from the name and attributes alone, so the shell does not open the file.
    // Shared generic keys use a neutral name, since the first file to request one decides it for all.
    String key = P_FileIconKey(scratch.arena, path, kind);
    b32 byType = !exact && !StrIsEqual(key, path);
    b32 generic = !exact && (StrIsEqual(key, S("/file")) || StrIsEqual(key, S("/folder")));
    wchar *wide = P__FilePathWide(scratch.arena, generic ? S("item") : path);
    b32 result = false;
    if (wide) {
        P__EnsureCom();
        DWORD attributes = kind == P_FileKind_Directory || kind == P_FileKind_Drive ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
        SHFILEINFOW info = {0};
        b32 found = SHGetFileInfoW(wide, attributes, &info, sizeof(info), SHGFI_SYSICONINDEX | (byType ? SHGFI_USEFILEATTRIBUTES : 0)) != 0;
        // Shell folders without a file path, such as This PC by its class id, are found by their item id instead; the Recycle Bin's
        // folder on disk is a plain one to the shell, so its own icon, empty or full, comes from the Recycle Bin itself.
        PIDLIST_ABSOLUTE list = NULL;
        if (exact && P_IsTrashFolder(path) && SUCCEEDED(SHGetSpecialFolderLocation(NULL, CSIDL_BITBUCKET, &list))) {
            found = SHGetFileInfoW((const wchar *)list, 0, &info, sizeof(info), SHGFI_PIDL | SHGFI_SYSICONINDEX) != 0;
            ILFree(list);
            list = NULL;
        }
        if (!found && !byType && SUCCEEDED(SHParseDisplayName(wide, NULL, &list, 0, NULL))) {
            found = SHGetFileInfoW((const wchar *)list, 0, &info, sizeof(info), SHGFI_PIDL | SHGFI_SYSICONINDEX) != 0;
            ILFree(list);
        }
        if (found) {
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

// The brightest of a straight RGBA pixel's color channels.
function u32 P__Brightest(u32 p)
{
    return Max(Max(p & 0xFF, (p >> 8) & 0xFF), (p >> 16) & 0xFF);
}

// The shell's image factory gives a thumbnail where the file has one and its icon otherwise, at any size.
// The shell's thumbnail cache gives some folders' pictures opaque on black instead of see-through, and compressed, so that the black is
// noisy near the folder. The near-black that reaches the edges is cleared, which keeps what the folder's picture shows of its contents,
// and the folder's rim, which was blended with the black, gets its see-through back. Some of those pictures are only the folder's icon
// drawn small in the middle, its shadow still dark around it; for those this returns false, so that the folder's own icon is shown instead.
function b32 P__ClearFolderBackground(Arena *scratch, P_Image *image)
{
    // The compression's noise in the black stays under this.
    enum { Dark = 24 };
    i64 count = (i64)image->width * image->height;
    if (count <= 0 || P__Brightest(image->pixels[0]) > Dark) return true;
    for (i64 i = 0; i < count; ++i) if ((image->pixels[i] >> 24) != 0xFF) return true;
    // Each pixel is cleared as it is pushed, so that none is pushed twice.
    u32 *stack = ArenaPushArrayNoZero(scratch, (u64)count, u32), *pixels = image->pixels;
    u64 top = 0;
    u32 w = (u32)image->width, h = (u32)image->height;
    for (u32 i = 0; i < (u32)count; ++i) {
        u32 x = i % w, y = i / w;
        if ((x == 0 || y == 0 || x + 1 == w || y + 1 == h) && pixels[i] && P__Brightest(pixels[i]) <= Dark) { pixels[i] = 0; stack[top++] = i; }
    }
    while (top) {
        u32 at = stack[--top], x = at % w, y = at / w;
        u32 next[4] = { x > 0 ? at - 1 : UINT32_MAX, x + 1 < w ? at + 1 : UINT32_MAX, y > 0 ? at - w : UINT32_MAX, y + 1 < h ? at + w : UINT32_MAX };
        for (u32 n = 0; n < 4; ++n) {
            if (next[n] != UINT32_MAX && pixels[next[n]] && P__Brightest(pixels[next[n]]) <= Dark) { pixels[next[n]] = 0; stack[top++] = next[n]; }
        }
    }
    // A rim pixel beside the cleared black is the folder's color darkened by it: as see-through as it is darker than its brightest neighbor.
    u32 *rim = ArenaPushArrayNoZero(scratch, (u64)count, u32);
    MemCopy(rim, pixels, (u64)count * sizeof(u32));
    for (u32 i = 0; i < (u32)count; ++i) {
        u32 x = i % w, y = i / w, own = P__Brightest(pixels[i]), brightest = 0;
        b32 edge = false;
        if (!pixels[i]) continue;
        for (i32 dy = -1; dy <= 1; ++dy) for (i32 dx = -1; dx <= 1; ++dx) {
            i64 nx = (i64)x + dx, ny = (i64)y + dy;
            if ((!dx && !dy) || nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
            u32 p = pixels[ny * w + nx];
            if (!p) edge = true;
            else brightest = Max(brightest, P__Brightest(p));
        }
        if (!edge || brightest <= own) continue;
        u32 a = own * 255 / brightest, p = pixels[i];
        if (!a) { rim[i] = 0; continue; }
        u32 r = Min(255u, (p & 0xFF) * 255 / a), g = Min(255u, ((p >> 8) & 0xFF) * 255 / a), b = Min(255u, ((p >> 16) & 0xFF) * 255 / a);
        rim[i] = (a << 24) | (b << 16) | (g << 8) | r;
    }
    MemCopy(pixels, rim, (u64)count * sizeof(u32));
    u32 x0 = w, y0 = h, x1 = 0, y1 = 0;
    for (u32 i = 0; i < (u32)count; ++i) {
        if (!pixels[i]) continue;
        u32 x = i % w, y = i / w;
        x0 = Min(x0, x), y0 = Min(y0, y), x1 = Max(x1, x), y1 = Max(y1, y);
    }
    return x0 <= x1 && (x1 - x0 + 1) * 2 > w && (y1 - y0 + 1) * 2 > h;
}

// The shell's image factory's picture of `path` with `flags` added to resizing it to fit.
function b32 P__ShellThumbnail(Arena *arena, String path, P_FileKind kind, i32 size, SIIGBF flags, P_Image *image)
{
    Scratch scratch = GetScratchEx(arena);
    wchar *wide = P__FilePathWide(scratch.arena, path);
    b32 result = false;
    if (wide) {
        P__EnsureCom();
        IShellItemImageFactory *factory = NULL;
        if (SUCCEEDED(SHCreateItemFromParsingName(wide, NULL, &P__IID_IShellItemImageFactory, (void **)&factory))) {
            HBITMAP bitmap = NULL;
            if (SUCCEEDED(factory->lpVtbl->GetImage(factory, (SIZE){ size, size }, SIIGBF_RESIZETOFIT | flags, &bitmap)) && bitmap) {
                result = P__BitmapPixels(arena, bitmap, image);
                DeleteObject(bitmap);
                if (result && kind == P_FileKind_Directory && !P__ClearFolderBackground(scratch.arena, image)) {
                    *image = (P_Image){0};
                    result = false;
                }
            }
            factory->lpVtbl->Release(factory);
        }
    }
    ReleaseScratch(scratch);
    return result;
}

function b32 P_LoadFileThumbnail(Arena *arena, String path, P_FileKind kind, i32 size, b32 thumbnailOnly, const P_CancelToken *cancel, P_Image *image)
{
    (void)cancel;
    return P__ShellThumbnail(arena, path, kind, size, thumbnailOnly ? SIIGBF_THUMBNAILONLY : 0, image) || (!thumbnailOnly && P_LoadFileIcon(arena, path, kind, size, 1, false, image));
}

// The shell's thumbnail cache answers without opening the file, which for a video on a spinning disk takes a second or more.
// Without SIIGBF_THUMBNAILONLY a miss would come back as the type's icon rather than as a failure.
function b32 P_LoadCachedThumbnail(Arena *arena, String path, P_FileKind kind, i32 size, P_Image *image)
{
    return P__ShellThumbnail(arena, path, kind, size, SIIGBF_INCACHEONLY | SIIGBF_THUMBNAILONLY, image);
}

// The Windows Imaging Component's, defined here so that windowscodecs.lib need not be linked.
// {CACAF262-9370-4615-A13B-9F5539DA4C0A}
global const CLSID P__CLSID_WICImagingFactory = { 0xCACAF262, 0x9370, 0x4615, { 0xA1, 0x3B, 0x9F, 0x55, 0x39, 0xDA, 0x4C, 0x0A } };
// {EC5EC8A9-C395-4314-9C77-54D7A935FF70}
global const IID P__IID_IWICImagingFactory = { 0xEC5EC8A9, 0xC395, 0x4314, { 0x9C, 0x77, 0x54, 0xD7, 0xA9, 0x35, 0xFF, 0x70 } };
// {F5C7AD2D-6A8D-43DD-A7A8-A29935261AE9}, straight-alpha RGBA with red in the lowest byte, as P_Image holds it.
global const GUID P__WICPixelFormat32bppRGBA = { 0xF5C7AD2D, 0x6A8D, 0x43DD, { 0xA7, 0xA8, 0xA2, 0x99, 0x35, 0x26, 0x1A, 0xE9 } };

function IStream *P__PdfPageStream(const wchar *path, u32 page, u32 side, b32 sideIsWidth);

// A PDF is drawn as its first page, which WIC has no codec for.
function b32 P__IsPdf(const wchar *path)
{
    i32 length = lstrlenW(path);
    return length >= 4 && CompareStringOrdinal(path + length - 4, 4, L".pdf", 4, TRUE) == CSTR_EQUAL;
}

function b32 P__LoadImage(Arena *arena, const wchar *wide, IStream *stream, i32 maxSide, P_Image *image, i32 *sourceWidth, i32 *sourceHeight);

function b32 P_LoadImageFile(Arena *arena, String path, i32 maxSide, P_Image *image, i32 *sourceWidth, i32 *sourceHeight)
{
    Scratch scratch = GetScratchEx(arena);
    wchar *wide = P__FilePathWide(scratch.arena, path);
    IStream *pdf = wide && P__IsPdf(wide) ? P__PdfPageStream(wide, 0, (u32)maxSide, false) : NULL;
    b32 result = wide && (pdf || !P__IsPdf(wide)) && P__LoadImage(arena, wide, pdf, maxSide, image, sourceWidth, sourceHeight);
    if (pdf) pdf->lpVtbl->Release(pdf);
    ReleaseScratch(scratch);
    return result;
}

function b32 P_LoadImageBytes(Arena *arena, String data, i32 maxSide, P_Image *image)
{
    IStream *stream = data.length && data.length <= UINT32_MAX ? SHCreateMemStream((const BYTE *)data.value, (UINT)data.length) : NULL;
    i32 width = 0, height = 0;
    b32 result = stream && P__LoadImage(arena, NULL, stream, maxSide, image, &width, &height);
    if (stream) stream->lpVtbl->Release(stream);
    return result;
}

function b32 P_LoadPdfPage(Arena *arena, String path, u32 page, i32 width, P_Image *image)
{
    Scratch scratch = GetScratchEx(arena);
    wchar *wide = P__FilePathWide(scratch.arena, path);
    IStream *drawn = wide && width > 0 ? P__PdfPageStream(wide, page, (u32)width, true) : NULL;
    i32 shownWidth = 0, shownHeight = 0;
    b32 result = drawn && P__LoadImage(arena, wide, drawn, 1 << 15, image, &shownWidth, &shownHeight);
    if (drawn) drawn->lpVtbl->Release(drawn);
    ReleaseScratch(scratch);
    return result;
}

// The picture of the file `wide`, or of `stream` when not NULL, shrunk to fit `maxSide`, in straight RGBA.
function b32 P__LoadImage(Arena *arena, const wchar *wide, IStream *stream, i32 maxSide, P_Image *image, i32 *sourceWidth, i32 *sourceHeight)
{
    b32 result = false;
    P__EnsureCom();
    IWICImagingFactory *factory = NULL;
    IWICBitmapDecoder *decoder = NULL;
    IWICBitmapFrameDecode *frame = NULL;
    IWICBitmapScaler *scaler = NULL;
    IWICFormatConverter *converter = NULL;
    UINT width = 0, height = 0;
    if (SUCCEEDED(CoCreateInstance(&P__CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &P__IID_IWICImagingFactory, (void **)&factory)) &&
        (stream ? SUCCEEDED(factory->lpVtbl->CreateDecoderFromStream(factory, stream, NULL, WICDecodeMetadataCacheOnDemand, &decoder))
                : SUCCEEDED(factory->lpVtbl->CreateDecoderFromFilename(factory, wide, NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder))) &&
        SUCCEEDED(decoder->lpVtbl->GetFrame(decoder, 0, &frame)) &&
        SUCCEEDED(frame->lpVtbl->GetSize(frame, &width, &height)) && width && height) {
        // A picture larger than the square is scaled down as it is decoded.
        IWICBitmapSource *source = (IWICBitmapSource *)frame;
        UINT shownWidth = width, shownHeight = height;
        if (width > (UINT)maxSide || height > (UINT)maxSide) {
            f64 shrink = (f64)maxSide / (f64)Max(width, height);
            shownWidth = (UINT)Max(1.0, (f64)width * shrink);
            shownHeight = (UINT)Max(1.0, (f64)height * shrink);
            if (SUCCEEDED(factory->lpVtbl->CreateBitmapScaler(factory, &scaler)) &&
                SUCCEEDED(scaler->lpVtbl->Initialize(scaler, source, shownWidth, shownHeight, WICBitmapInterpolationModeFant))) {
                source = (IWICBitmapSource *)scaler;
            } else {
                source = NULL;
            }
        }
        if (source && SUCCEEDED(factory->lpVtbl->CreateFormatConverter(factory, &converter)) &&
            SUCCEEDED(converter->lpVtbl->Initialize(converter, source, &P__WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, NULL, 0, WICBitmapPaletteTypeCustom))) {
            u64 count = (u64)shownWidth * shownHeight;
            u32 *pixels = ArenaPushArrayNoZero(arena, count, u32);
            if (SUCCEEDED(converter->lpVtbl->CopyPixels(converter, NULL, shownWidth * 4, (UINT)(count * 4), (BYTE *)pixels))) {
                *image = (P_Image){ pixels, (i32)shownWidth, (i32)shownHeight };
                *sourceWidth = (i32)width;
                *sourceHeight = (i32)height;
                result = true;
            }
        }
    }
    if (converter) converter->lpVtbl->Release(converter);
    if (scaler) scaler->lpVtbl->Release(scaler);
    if (frame) frame->lpVtbl->Release(frame);
    if (decoder) decoder->lpVtbl->Release(decoder);
    if (factory) factory->lpVtbl->Release(factory);
    return result;
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
