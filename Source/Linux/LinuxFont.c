// Font rasterization through fontconfig and FreeType. Requires LinuxPlatform.c.

#include <ft2build.h>
#include FT_FREETYPE_H
#include <fontconfig/fontconfig.h>

#define LinuxMaxFallbacks 16

struct PlatformFontFace {
    Arena arena;  // the one it is in
    FT_Library library;
    FT_Face face;
    FcPattern *pattern;  // the family asked for, which fallback fonts are matched against; NULL for a font from memory
    f32 pixelSize;
    FT_Face fallbacks[LinuxMaxFallbacks];
    u32 fallbackCount;
};

// How much a face's glyphs are scaled to the font's size: 1 for an outline font, less for a font of pictures at fixed sizes, as most color
// emoji fonts are.
function f32 LinuxFaceScale(const PlatformFontFace *font, FT_Face face)
{
    return FT_IS_SCALABLE(face) || !face->size ? 1.0f : font->pixelSize / MaxF32((f32)face->size->metrics.y_ppem, 1.0f);
}

function void PlatformFontClose(PlatformFontFace *font)
{
    if (!font) return;
    for (u32 i = 0; i < font->fallbackCount; ++i) FT_Done_Face(font->fallbacks[i]);
    if (font->face) FT_Done_Face(font->face);
    if (font->pattern) FcPatternDestroy(font->pattern);
    if (font->library) FT_Done_FreeType(font->library);
    Arena arena = font->arena;
    ArenaRelease(&arena);
}

// The face of the font fontconfig picks for `pattern`, at the font's size, or NULL.
function FT_Face LinuxOpenMatch(PlatformFontFace *font, FcPattern *pattern)
{
    FcResult result = FcResultNoMatch;
    FcPattern *match = FcFontMatch(NULL, pattern, &result);
    FcChar8 *file = NULL;
    i32 index = 0;
    FT_Face face = NULL;
    if (match && FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch) {
        FcPatternGetInteger(match, FC_INDEX, 0, &index);
        if (FT_New_Face(font->library, (const char *)file, index, &face) != 0) face = NULL;
    }
    if (match) FcPatternDestroy(match);
    // A font of pictures takes the smallest of its sizes that is not smaller than the font's, or its largest, which are scaled to it.
    b32 sized = false;
    if (face && FT_IS_SCALABLE(face)) {
        sized = FT_Set_Char_Size(face, 0, (FT_F26Dot6)(font->pixelSize * 64.0f), 72, 72) == 0;
    } else if (face && FT_HAS_FIXED_SIZES(face) && face->num_fixed_sizes > 0) {
        i32 best = 0;
        for (i32 i = 1; i < face->num_fixed_sizes; ++i) {
            f32 have = (f32)face->available_sizes[i].y_ppem / 64.0f, chosen = (f32)face->available_sizes[best].y_ppem / 64.0f;
            if ((chosen < font->pixelSize && have > chosen) || (have >= font->pixelSize && have < chosen)) best = i;
        }
        sized = FT_Select_Size(face, best) == 0;
    }
    if (face && !sized) {
        FT_Done_Face(face);
        face = NULL;
    }
    return face;
}

function PlatformFontFace *PlatformFontOpen(PlatformFontSource source, f32 pixelSize, PlatformFontMetrics *metrics)
{
    Arena arena = {0};
    PlatformFontFace *font = ArenaPushStruct(&arena, PlatformFontFace);
    font->arena = arena;
    font->pixelSize = pixelSize;
    b32 ok = FT_Init_FreeType(&font->library) == 0;
    if (ok && source.data != NULL) {
        ok = FT_New_Memory_Face(font->library, source.data, (FT_Long)source.size, 0, &font->face) == 0 &&
             FT_Set_Char_Size(font->face, 0, (FT_F26Dot6)(pixelSize * 64.0f), 72, 72) == 0;
    } else if (ok) {
        char name[128] = {0};
        MemCopy(name, source.family.value, Min(source.family.length, (u64)sizeof(name) - 1));
        // fontconfig always returns its closest match, so unknown families fall back to the configured default.
        font->pattern = FcNameParse((const FcChar8 *)name);
        FcConfigSubstitute(NULL, font->pattern, FcMatchPattern);
        FcDefaultSubstitute(font->pattern);
        font->face = LinuxOpenMatch(font, font->pattern);
        ok = font->face != NULL;
    }
    if (!ok) {
        PlatformFontClose(font);
        return NULL;
    }
    FT_Size_Metrics *size = &font->face->size->metrics;
    f32 ascent = (f32)size->ascender / 64.0f, descent = (f32)-size->descender / 64.0f;
    *metrics = (PlatformFontMetrics){ ascent, descent, MaxF32((f32)size->height / 64.0f - ascent - descent, 0.0f) };
    return font;
}

// A color glyph's premultiplied BGRA `bitmap`, shrunk by `scale` with each pixel the average of those it covers, into `glyph` as straight RGBA.
function void LinuxColorGlyph(Arena *arena, const FT_Bitmap *bitmap, f32 scale, PlatformGlyph *glyph)
{
    i32 sourceWidth = (i32)bitmap->width, sourceHeight = (i32)bitmap->rows;
    glyph->width = Max(1, (i32)((f32)sourceWidth * scale + 0.5f));
    glyph->height = Max(1, (i32)((f32)sourceHeight * scale + 0.5f));
    glyph->color = true;
    glyph->pixels = ArenaPushArrayNoZero(arena, (u64)glyph->width * (u64)glyph->height * 4, u8);
    for (i32 y = 0; y < glyph->height; ++y) {
        i32 sy0 = y * sourceHeight / glyph->height, sy1 = Max(sy0 + 1, (y + 1) * sourceHeight / glyph->height);
        for (i32 x = 0; x < glyph->width; ++x) {
            i32 sx0 = x * sourceWidth / glyph->width, sx1 = Max(sx0 + 1, (x + 1) * sourceWidth / glyph->width);
            u32 sum[4] = {0}, count = 0;
            for (i32 sy = sy0; sy < sy1; ++sy) {
                const u8 *row = bitmap->buffer + (i64)sy * bitmap->pitch;
                for (i32 sx = sx0; sx < sx1; ++sx, ++count) for (u32 k = 0; k < 4; ++k) sum[k] += row[(u32)sx * 4 + k];
            }
            u8 *out = glyph->pixels + ((u64)y * (u64)glyph->width + (u64)x) * 4;
            u32 alpha = sum[3];
            // BGRA, premultiplied, to RGBA, straight.
            out[0] = alpha ? (u8)Min(sum[2] * 255 / alpha, 255u) : 0;
            out[1] = alpha ? (u8)Min(sum[1] * 255 / alpha, 255u) : 0;
            out[2] = alpha ? (u8)Min(sum[0] * 255 / alpha, 255u) : 0;
            out[3] = (u8)(alpha / count);
        }
    }
}

// The glyph of `codepoint` in `face` into `glyph`, in its own colors when the face has them; false when it has none or it cannot be drawn.
function b32 LinuxDrawGlyph(const PlatformFontFace *font, FT_Face face, Arena *arena, u32 codepoint, PlatformGlyph *glyph)
{
    *glyph = (PlatformGlyph){0};
    // Light hinting snaps only vertically, the closest match to DirectWrite's natural symmetric rendering.
    if (FT_Load_Char(face, codepoint, FT_LOAD_RENDER | FT_LOAD_TARGET_LIGHT | FT_LOAD_COLOR) != 0) return false;
    FT_GlyphSlot slot = face->glyph;
    f32 scale = LinuxFaceScale(font, face);
    glyph->advance = floorf((f32)slot->advance.x / 64.0f * scale + 0.5f);
    FT_Bitmap *bitmap = &slot->bitmap;
    if (bitmap->width && bitmap->rows && bitmap->pixel_mode == FT_PIXEL_MODE_BGRA) {
        LinuxColorGlyph(arena, bitmap, scale, glyph);
        glyph->offsetX = floorf((f32)slot->bitmap_left * scale + 0.5f);
        glyph->offsetY = floorf((f32)-slot->bitmap_top * scale + 0.5f);
        return true;
    }
    if (bitmap->width == 0 || bitmap->rows == 0 || bitmap->pixel_mode != FT_PIXEL_MODE_GRAY) return true;
    glyph->width = (i32)bitmap->width;
    glyph->height = (i32)bitmap->rows;
    glyph->offsetX = (f32)slot->bitmap_left;
    glyph->offsetY = (f32)-slot->bitmap_top;
    glyph->pixels = ArenaPushArrayNoZero(arena, (u64)glyph->width * (u64)glyph->height, u8);
    for (i32 y = 0; y < glyph->height; ++y) {
        MemCopy(glyph->pixels + (u64)y * (u64)glyph->width, bitmap->buffer + (i64)y * bitmap->pitch, (u64)glyph->width);
    }
    return true;
}

function b32 PlatformFontGlyph(PlatformFontFace *font, Arena *arena, u32 codepoint, PlatformGlyph *glyph)
{
    if (FT_Get_Char_Index(font->face, codepoint)) return LinuxDrawGlyph(font, font->face, arena, codepoint, glyph);
    if (!font->pattern) return false;
    // The fonts already lent from first, then the one fontconfig matches to the family with the character in it.
    for (u32 i = 0; i < font->fallbackCount; ++i) {
        if (FT_Get_Char_Index(font->fallbacks[i], codepoint)) return LinuxDrawGlyph(font, font->fallbacks[i], arena, codepoint, glyph);
    }
    if (font->fallbackCount == LinuxMaxFallbacks) return false;
    FcPattern *pattern = FcPatternDuplicate(font->pattern);
    FcCharSet *characters = FcCharSetCreate();
    FT_Face face = NULL;
    if (pattern && characters && FcCharSetAddChar(characters, codepoint) && FcPatternAddCharSet(pattern, FC_CHARSET, characters)) face = LinuxOpenMatch(font, pattern);
    if (characters) FcCharSetDestroy(characters);
    if (pattern) FcPatternDestroy(pattern);
    if (!face) return false;
    if (!FT_Get_Char_Index(face, codepoint)) {
        FT_Done_Face(face);
        return false;
    }
    font->fallbacks[font->fallbackCount++] = face;
    return LinuxDrawGlyph(font, face, arena, codepoint, glyph);
}

function ArrayStr PlatformFontFamilies(Arena *arena)
{
    ArrayStr result = {0};
    FcPattern *pattern = FcPatternCreate();
    FcObjectSet *objects = FcObjectSetBuild(FC_FAMILY, (char *)0);
    FcFontSet *fonts = pattern && objects ? FcFontList(NULL, pattern, objects) : NULL;
    for (i32 i = 0; fonts && i < fonts->nfont; ++i) {
        FcChar8 *family = NULL;
        if (FcPatternGetString(fonts->fonts[i], FC_FAMILY, 0, &family) == FcResultMatch && family) ArrayPush(arena, result, StrCopy(arena, StrFromCStr((const char *)family)));
    }
    if (fonts) FcFontSetDestroy(fonts);
    if (objects) FcObjectSetDestroy(objects);
    if (pattern) FcPatternDestroy(pattern);
    return result;
}
