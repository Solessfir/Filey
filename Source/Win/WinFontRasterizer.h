#pragma once

// C boundary of the C++ DirectWrite rasterizer, included from both languages.
// C gets the type aliases from Base.h; C++ cannot include it, so it gets the same ones here.

#if defined(__cplusplus)
#include <stddef.h>
#include <stdint.h>
typedef uint8_t  u8;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int32_t  i32;
typedef float    f32;
typedef i32      b32;
typedef wchar_t  wchar;
#endif

typedef struct WinRasterGlyph {
    u8 *pixels;
    i32 width;
    i32 height;
    f32 offsetX;
    f32 offsetY;
    f32 advance;
    b32 color;
} WinRasterGlyph;

typedef struct WinRasterMetrics {
    f32 ascent;
    f32 descent;
    f32 lineGap;
} WinRasterMetrics;

// Returns NULL on failure; the memory need not be zeroed.
typedef void *WinRasterAllocFn(void *user, u64 size);

// A font kept open to draw glyphs one at a time, with the faces it took fallback glyphs from.
typedef struct WinFontFace WinFontFace;

#if defined(__cplusplus)
extern "C" {
#endif
// `data` (a font file in memory, which has no fallback) takes precedence over `family`; NULL on failure.
WinFontFace *WinFontOpen(const wchar *family, const u8 *data, u64 dataSize, f32 pixelSize, WinRasterMetrics *metrics);
// `codepoint`'s glyph, from the font or else from the one the system's fallback picks for it; false when neither has it.
b32 WinFontGlyph(WinFontFace *font, u32 codepoint, WinRasterAllocFn *alloc, void *allocUser, WinRasterGlyph *glyph);
void WinFontClose(WinFontFace *font);
#if defined(__cplusplus)
}
#endif

typedef void WinFontFamilyFn(void *user, const wchar *name, u32 length);

#if defined(__cplusplus)
extern "C"
#endif
// Calls `each` with the English name, or else the first, of every family of the system's font collection, which the rasterizer looks names up in.
void WinFontFamilies(WinFontFamilyFn *each, void *user);
