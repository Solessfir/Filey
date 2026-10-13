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

function b32 PlatformFontRasterize(Arena *arena, PlatformFontSource source, f32 pixelSize, u32 firstCodepoint, u32 lastCodepoint, PlatformFont *font)
{
    Assert(firstCodepoint <= lastCodepoint);

    wchar name[128] = {0};
    MultiByteToWideChar(CP_UTF8, 0, source.family.value, (int)Min(source.family.length, (u64)100), name, (int)CArrayCount(name) - 1);

    u32 count = lastCodepoint - firstCodepoint + 1;
    PlatformGlyph *glyphs = ArenaPushArray(arena, count, PlatformGlyph);
    WinRasterMetrics metrics = {0};
    if (!WinRasterizeFont(name, source.data, source.size, pixelSize, firstCodepoint, count, WinRasterAlloc, arena, (WinRasterGlyph *)glyphs, &metrics)) {
        return false;
    }

    *font = (PlatformFont){
        .ascent = metrics.ascent,
        .descent = metrics.descent,
        .lineGap = metrics.lineGap,
        .firstCodepoint = firstCodepoint,
        .glyphCount = count,
        .glyphs = glyphs,
    };
    return true;
}
