// Font rasterization through fontconfig and FreeType. Requires LinuxPlatform.c.

#include <ft2build.h>
#include FT_FREETYPE_H
#include <fontconfig/fontconfig.h>

function b32 PlatformFontRasterize(Arena *arena, PlatformFontSource source, f32 pixelSize, u32 firstCodepoint, u32 lastCodepoint, PlatformFont *font)
{
    Assert(firstCodepoint <= lastCodepoint);

    FT_Library library = NULL;
    FT_Face face = NULL;
    b32 ok = FT_Init_FreeType(&library) == 0;
    if (ok && source.data != NULL) {
        ok = FT_New_Memory_Face(library, source.data, (FT_Long)source.size, 0, &face) == 0;
    } else if (ok) {
        char name[128] = {0};
        MemCopy(name, source.family.value, Min(source.family.length, (u64)sizeof(name) - 1));

        // fontconfig always returns its closest match, so unknown families fall back to the configured default.
        FcPattern *pattern = FcNameParse((const FcChar8 *)name);
        FcConfigSubstitute(NULL, pattern, FcMatchPattern);
        FcDefaultSubstitute(pattern);
        FcResult result = FcResultNoMatch;
        FcPattern *match = FcFontMatch(NULL, pattern, &result);
        FcPatternDestroy(pattern);

        FcChar8 *file = NULL;
        i32 faceIndex = 0;
        ok = match != NULL && FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch;
        if (ok) {
            FcPatternGetInteger(match, FC_INDEX, 0, &faceIndex);
            ok = FT_New_Face(library, (const char *)file, faceIndex, &face) == 0;
        }
        if (match != NULL) {
            FcPatternDestroy(match);
        }
    }
    ok = ok && FT_Set_Char_Size(face, 0, (FT_F26Dot6)(pixelSize * 64.0f), 72, 72) == 0;

    u32 count = lastCodepoint - firstCodepoint + 1;
    PlatformGlyph *glyphs = ok ? ArenaPushArray(arena, count, PlatformGlyph) : NULL;
    for (u32 i = 0; ok && i < count; ++i) {
        // Light hinting snaps only vertically, the closest match to DirectWrite's natural symmetric rendering.
        if (FT_Load_Char(face, firstCodepoint + i, FT_LOAD_RENDER | FT_LOAD_TARGET_LIGHT) != 0) {
            continue;
        }
        FT_GlyphSlot slot = face->glyph;
        PlatformGlyph *glyph = &glyphs[i];
        glyph->advance = (f32)((slot->advance.x + 32) >> 6);

        FT_Bitmap *bitmap = &slot->bitmap;
        if (bitmap->width == 0 || bitmap->rows == 0 || bitmap->pixel_mode != FT_PIXEL_MODE_GRAY) {
            continue;
        }
        glyph->width = (i32)bitmap->width;
        glyph->height = (i32)bitmap->rows;
        glyph->offsetX = (f32)slot->bitmap_left;
        glyph->offsetY = (f32)-slot->bitmap_top;
        glyph->pixels = ArenaPushArrayNoZero(arena, (u64)glyph->width * (u64)glyph->height, u8);
        for (i32 y = 0; y < glyph->height; ++y) {
            MemCopy(glyph->pixels + (u64)y * (u64)glyph->width, bitmap->buffer + (i64)y * bitmap->pitch, (u64)glyph->width);
        }
    }

    if (ok) {
        FT_Size_Metrics *metrics = &face->size->metrics;
        f32 ascent = (f32)metrics->ascender / 64.0f;
        f32 descent = (f32)-metrics->descender / 64.0f;
        *font = (PlatformFont){
            .ascent = ascent,
            .descent = descent,
            .lineGap = MaxF32((f32)metrics->height / 64.0f - ascent - descent, 0.0f),
            .firstCodepoint = firstCodepoint,
            .glyphCount = count,
            .glyphs = glyphs,
        };
    }

    if (face != NULL) {
        FT_Done_Face(face);
    }
    if (library != NULL) {
        FT_Done_FreeType(library);
    }
    return ok;
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
