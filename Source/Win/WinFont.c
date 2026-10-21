// Font rasterization through the C++ DirectWrite adapter. Requires WinPlatform.c.

#include "Win/WinFontRasterizer.h"

static_assert(sizeof(WinRasterGlyph) == sizeof(PlatformGlyph));
static_assert(offsetof(WinRasterGlyph, pixels) == offsetof(PlatformGlyph, pixels));
static_assert(offsetof(WinRasterGlyph, width) == offsetof(PlatformGlyph, width));
static_assert(offsetof(WinRasterGlyph, height) == offsetof(PlatformGlyph, height));
static_assert(offsetof(WinRasterGlyph, offsetX) == offsetof(PlatformGlyph, offsetX));
static_assert(offsetof(WinRasterGlyph, offsetY) == offsetof(PlatformGlyph, offsetY));
static_assert(offsetof(WinRasterGlyph, advance) == offsetof(PlatformGlyph, advance));

function void *WinRasterAlloc(void *user, u64 size)
{
    return ArenaPushArrayNoZero((Arena *)user, size, u8);
}

function PlatformFontFace *PlatformFontOpen(PlatformFontSource source, f32 pixelSize, PlatformFontMetrics *metrics)
{
    wchar name[128] = {0};
    MultiByteToWideChar(CP_UTF8, 0, source.family.value, (int)Min(source.family.length, (u64)100), name, (int)CArrayCount(name) - 1);
    WinRasterMetrics raster = {0};
    WinFontFace *face = WinFontOpen(name, source.data, source.size, pixelSize, &raster);
    *metrics = (PlatformFontMetrics){ raster.ascent, raster.descent, raster.lineGap };
    return (PlatformFontFace *)face;
}

function b32 PlatformFontGlyph(PlatformFontFace *face, Arena *arena, u32 codepoint, PlatformGlyph *glyph)
{
    return WinFontGlyph((WinFontFace *)face, codepoint, WinRasterAlloc, arena, (WinRasterGlyph *)glyph);
}

function void PlatformFontClose(PlatformFontFace *face) { WinFontClose((WinFontFace *)face); }

typedef struct WinFamilyList {
    Arena *arena;
    ArrayStr *names;
} WinFamilyList;

function void WinFamilyFound(void *user, const wchar *name, u32 length)
{
    WinFamilyList *list = user;
    ArrayPush(list->arena, *list->names, P__FileStringWide(list->arena, name, (i32)length));
}

function ArrayStr PlatformFontFamilies(Arena *arena)
{
    ArrayStr result = {0};
    WinFamilyList list = { arena, &result };
    WinFontFamilies(WinFamilyFound, &list);
    return result;
}
