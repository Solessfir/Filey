// Backend-neutral glyph atlas packing and text layout on top of the platform rasterizer.
// Glyphs are drawn the first time text needs them, any codepoint the font or the system's fallback fonts have, into atlas pages that are
// filled shelf by shelf; one that none has draws as '?'.

#include <math.h>

#define R_FontPageSize     1024
#define R_FontMaxPages     8
#define R_FontGlyphPadding 1
// Codepoints below this find their glyph by index, as most text is in them; the rest go through a map.
#define R_FontDirectCodepoints 0x3000

typedef struct R_FontGlyphMap { MapHeader header; struct { u32 key; u32 value; } *v; } R_FontGlyphMap;

struct R_FontCache {
    Arena arena;  // the glyphs, which grow as text needs them
    PlatformFontFace *face;
    struct { ArrayHeader header; R_Glyph *v; } glyphs;  // [0] is the missing glyph, '?' once drawn
    u32 *direct;              // each of the first codepoints' glyph plus one, 0 for not asked for yet
    R_FontGlyphMap others;
    R_TextureHandle pages[R_FontMaxPages];
    u32 pageCount;
    i32 penX, penY, rowHeight;  // where the last page is filled to
    b32 full;                   // every page is full, so new glyphs draw as missing
};

// A place for a `width` by `height` glyph in the last page, or a new one, from shelves left to right and top to bottom.
function b32 R__FontPlace(R_FontCache *cache, i32 width, i32 height, i32 *x, i32 *y)
{
    if (cache->full || width + 2 * R_FontGlyphPadding > R_FontPageSize || height + 2 * R_FontGlyphPadding > R_FontPageSize) return false;
    if (cache->pageCount && cache->penX + width + R_FontGlyphPadding > R_FontPageSize) {
        cache->penX = R_FontGlyphPadding;
        cache->penY += cache->rowHeight + R_FontGlyphPadding;
        cache->rowHeight = 0;
    }
    if (!cache->pageCount || cache->penY + height + R_FontGlyphPadding > R_FontPageSize) {
        if (cache->pageCount == R_FontMaxPages) {
            cache->full = true;
            return false;
        }
        Scratch scratch = GetScratch();
        u8 *clear = ArenaPushArray(scratch.arena, (u64)R_FontPageSize * R_FontPageSize, u8);
        cache->pages[cache->pageCount++] = R_TextureCreateAlpha(R_FontPageSize, R_FontPageSize, clear);
        ReleaseScratch(scratch);
        cache->penX = cache->penY = R_FontGlyphPadding;
        cache->rowHeight = 0;
    }
    *x = cache->penX;
    *y = cache->penY;
    cache->penX += width + R_FontGlyphPadding;
    cache->rowHeight = MaxI32(cache->rowHeight, height);
    return true;
}

// The glyph of `codepoint`, drawn now when it is new; the missing glyph when no font has it.
function const R_Glyph *R__FontGlyph(R_FontCache *cache, rune codepoint)
{
    u32 c = (u32)codepoint;
    if (c < R_FontDirectCodepoints && cache->direct[c]) return &cache->glyphs.v[cache->direct[c] - 1];
    u32 *known = c >= R_FontDirectCodepoints ? MapGetPtrNull(cache->others, c) : NULL;
    if (known) return &cache->glyphs.v[*known];
    Scratch scratch = GetScratch();
    PlatformGlyph drawn = {0};
    u32 index = 0;
    if (c >= 0x20 && PlatformFontGlyph(cache->face, scratch.arena, c, &drawn)) {
        R_Glyph glyph = { .box = { drawn.offsetX, drawn.offsetY, drawn.offsetX + (f32)drawn.width, drawn.offsetY + (f32)drawn.height }, .advance = drawn.advance };
        i32 x = 0, y = 0;
        b32 placed = drawn.width <= 0 || drawn.height <= 0;
        if (!placed && R__FontPlace(cache, drawn.width, drawn.height, &x, &y)) {
            R_TextureHandle page = cache->pages[cache->pageCount - 1];
            R_TextureUpdateAlpha(page, x, y, drawn.width, drawn.height, drawn.pixels);
            f32 unit = 1.0f / (f32)R_FontPageSize;
            glyph.page = page;
            glyph.uv = (R_Rect){ (f32)x * unit, (f32)y * unit, (f32)(x + drawn.width) * unit, (f32)(y + drawn.height) * unit };
            placed = true;
        }
        if (placed) {
            index = (u32)cache->glyphs.header.count;
            ArrayPush(&cache->arena, cache->glyphs, glyph);
        }
    }
    ReleaseScratch(scratch);
    // A codepoint none has, or one with no room, keeps the missing glyph, so that it is not asked for again.
    if (c < R_FontDirectCodepoints) cache->direct[c] = index + 1;
    else MapPut(&cache->arena, cache->others, c, index);
    return &cache->glyphs.v[index];
}

function b32 R__FontCreate(Arena *arena, PlatformFontSource source, f32 pixelSize, u32 firstCodepoint, u32 lastCodepoint, R_Font *font)
{
    PlatformFontMetrics metrics = {0};
    PlatformFontFace *face = PlatformFontOpen(source, pixelSize, &metrics);
    if (!face) return false;
    R_FontCache *cache = ArenaPushStruct(arena, R_FontCache);
    cache->face = face;
    cache->direct = ArenaPushArray(&cache->arena, R_FontDirectCodepoints, u32);
    // The missing glyph's slot, which '?' fills once drawn.
    ArrayPush(&cache->arena, cache->glyphs, ((R_Glyph){0}));
    const R_Glyph *question = R__FontGlyph(cache, '?');
    cache->glyphs.v[0] = *question;
    *font = (R_Font){
        .ascent = metrics.ascent,
        .descent = metrics.descent,
        .lineHeight = metrics.ascent + metrics.descent + metrics.lineGap,
        .cache = cache,
    };
    // The glyphs nearly all text has are drawn now, so that the first frames do not stop for them.
    for (u32 c = firstCodepoint; c <= lastCodepoint; ++c) R__FontGlyph(cache, (rune)c);
    return true;
}

function b32 R_FontCreate(Arena *arena, String family, f32 pixelSize, R_Font *font)
{
    return R__FontCreate(arena, (PlatformFontSource){ .family = family }, pixelSize, 0x20, 0x7E, font);
}

function b32 R_FontCreateFromMemory(Arena *arena, const u8 *data, u64 size, f32 pixelSize, u32 firstCodepoint, u32 lastCodepoint, R_Font *font)
{
    return R__FontCreate(arena, (PlatformFontSource){ .data = data, .size = size }, pixelSize, firstCodepoint, lastCodepoint, font);
}

function void R_FontRelease(R_Font *font)
{
    R_FontCache *cache = font->cache;
    if (!cache) return;
    for (u32 i = 0; i < cache->pageCount; ++i) R_TextureRelease(cache->pages[i]);
    PlatformFontClose(cache->face);
    ArenaRelease(&cache->arena);
    *font = (R_Font){0};
}

function const R_Glyph *R_FontGlyph(const R_Font *font, rune codepoint)
{
    return font->cache ? R__FontGlyph(font->cache, codepoint) : NULL;
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
            R_DrawTexturedRect(rect, glyph->uv, glyph->page, color);
        }
        penX += glyph->advance;
    }
}
