// Backend-neutral glyph atlas packing and text layout on top of the platform rasterizer.

#include <math.h>

typedef struct R_CodepointRange {
    u32 first;
    u32 last;
} R_CodepointRange;

// ponytail: fixed ranges rasterized up front, the rest draws as '?'.
// Add on-demand ranges when CJK or other large scripts are needed.
global const R_CodepointRange R_FontRanges[] = {
    { 0x0020, 0x017F },  // Basic Latin, Latin-1 Supplement, Latin Extended-A
    { 0x0400, 0x04FF },  // Cyrillic
    { 0x2010, 0x203A },  // dashes, quotes, ellipsis, angle quotes (breadcrumb separator)
    { 0x2190, 0x2193 },  // arrows (toolbar)
};

#define R_FontAtlasWidth   512
#define R_FontGlyphPadding 1

function u32 R_NextPow2(u32 value)
{
    u32 result = 1;
    while (result < value) {
        result <<= 1;
    }
    return result;
}

function b32 R__FontCreate(Arena *arena, PlatformFontSource source, f32 pixelSize, const R_CodepointRange *sourceRanges, u32 rangeCount, R_Font *font)
{
    Scratch scratch = GetScratchEx(arena);
    PlatformFont *rasters = ArenaPushArray(scratch.arena, rangeCount, PlatformFont);
    for (u32 r = 0; r < rangeCount; ++r) {
        if (!PlatformFontRasterize(scratch.arena, source, pixelSize, sourceRanges[r].first, sourceRanges[r].last, &rasters[r])) {
            ReleaseScratch(scratch);
            return false;
        }
    }

    // Shelf packing in codepoint order: glyphs of one font have similar heights, so rows waste little space.
    typedef struct { i32 x; i32 y; } Placement;
    Placement **placements = ArenaPushArray(scratch.arena, rangeCount, Placement *);
    i32 penX = R_FontGlyphPadding;
    i32 penY = R_FontGlyphPadding;
    i32 rowHeight = 0;
    for (u32 r = 0; r < rangeCount; ++r) {
        placements[r] = ArenaPushArray(scratch.arena, rasters[r].glyphCount, Placement);
        for (u32 i = 0; i < rasters[r].glyphCount; ++i) {
            PlatformGlyph *glyph = &rasters[r].glyphs[i];
            Assert(glyph->width + 2 * R_FontGlyphPadding <= R_FontAtlasWidth);
            if (penX + glyph->width + R_FontGlyphPadding > R_FontAtlasWidth) {
                penX = R_FontGlyphPadding;
                penY += rowHeight + R_FontGlyphPadding;
                rowHeight = 0;
            }
            placements[r][i] = (Placement){ penX, penY };
            penX += glyph->width + R_FontGlyphPadding;
            rowHeight = MaxI32(rowHeight, glyph->height);
        }
    }
    i32 atlasWidth = R_FontAtlasWidth;
    i32 atlasHeight = (i32)R_NextPow2((u32)(penY + rowHeight + R_FontGlyphPadding));

    u8 *pixels = ArenaPushArray(scratch.arena, (u64)atlasWidth * (u64)atlasHeight, u8);
    R_GlyphRange *ranges = ArenaPushArray(arena, rangeCount, R_GlyphRange);
    for (u32 r = 0; r < rangeCount; ++r) {
        PlatformFont *raster = &rasters[r];
        R_GlyphRange *range = &ranges[r];
        range->firstCodepoint = raster->firstCodepoint;
        range->glyphCount = raster->glyphCount;
        range->glyphs = ArenaPushArray(arena, raster->glyphCount, R_Glyph);

        for (u32 i = 0; i < raster->glyphCount; ++i) {
            PlatformGlyph *source = &raster->glyphs[i];
            Placement at = placements[r][i];
            for (i32 y = 0; y < source->height; ++y) {
                MemCopy(pixels + (u64)(at.y + y) * (u64)atlasWidth + (u64)at.x, source->pixels + (u64)y * (u64)source->width, (u64)source->width);
            }

            range->glyphs[i] = (R_Glyph){
                .box = { source->offsetX, source->offsetY, source->offsetX + (f32)source->width, source->offsetY + (f32)source->height },
                .uv = {
                    (f32)at.x / (f32)atlasWidth,
                    (f32)at.y / (f32)atlasHeight,
                    (f32)(at.x + source->width) / (f32)atlasWidth,
                    (f32)(at.y + source->height) / (f32)atlasHeight,
                },
                .advance = source->advance,
            };
        }
    }

    // Line metrics are per face, so every range reports the same ones.
    *font = (R_Font){
        .atlas = R_TextureCreateAlpha(atlasWidth, atlasHeight, pixels),
        .ascent = rasters[0].ascent,
        .descent = rasters[0].descent,
        .lineHeight = rasters[0].ascent + rasters[0].descent + rasters[0].lineGap,
        .rangeCount = rangeCount,
        .ranges = ranges,
    };
    ReleaseScratch(scratch);
    return true;
}

function b32 R_FontCreate(Arena *arena, String family, f32 pixelSize, R_Font *font)
{
    return R__FontCreate(arena, (PlatformFontSource){ .family = family }, pixelSize, R_FontRanges, (u32)CArrayCount(R_FontRanges), font);
}

function b32 R_FontCreateFromMemory(Arena *arena, const u8 *data, u64 size, f32 pixelSize, u32 firstCodepoint, u32 lastCodepoint, R_Font *font)
{
    R_CodepointRange range = { firstCodepoint, lastCodepoint };
    return R__FontCreate(arena, (PlatformFontSource){ .data = data, .size = size }, pixelSize, &range, 1, font);
}

function const R_Glyph *R__FontFindGlyph(const R_Font *font, rune codepoint)
{
    for (u32 r = 0; r < font->rangeCount; ++r) {
        const R_GlyphRange *range = &font->ranges[r];
        u32 index = (u32)codepoint - range->firstCodepoint;
        if (index < range->glyphCount) {
            return &range->glyphs[index];
        }
    }
    return NULL;
}

function const R_Glyph *R_FontGlyph(const R_Font *font, rune codepoint)
{
    const R_Glyph *result = R__FontFindGlyph(font, codepoint);
    if (result == NULL) {
        result = R__FontFindGlyph(font, '?');
    }
    return result;
}

function f32 R_TextWidth(const R_Font *font, String text)
{
    f32 width = 0.0f;
    for (u64 pos = 0; pos < text.length;) {
        const R_Glyph *glyph = R_FontGlyph(font, StrDecodeUtf8(text, &pos));
        width += glyph != NULL ? glyph->advance : 0.0f;
    }
    return width;
}

function void R_DrawText(const R_Font *font, f32 x, f32 y, R_Color color, String text)
{
    // Glyph bitmaps are rasterized at whole pixels, so the pen stays on the grid.
    f32 penX = floorf(x + 0.5f);
    f32 baseline = floorf(y + font->ascent + 0.5f);
    for (u64 pos = 0; pos < text.length;) {
        const R_Glyph *glyph = R_FontGlyph(font, StrDecodeUtf8(text, &pos));
        if (glyph == NULL) {
            continue;
        }
        if (glyph->box.x1 > glyph->box.x0) {
            R_Rect rect = { penX + glyph->box.x0, baseline + glyph->box.y0, penX + glyph->box.x1, baseline + glyph->box.y1 };
            R_DrawTexturedRect(rect, glyph->uv, font->atlas, color);
        }
        penX += glyph->advance;
    }
}
